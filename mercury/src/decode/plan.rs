//! Full-image T2 parse plan: stream the codestream's packet headers once and
//! build, per tile and subband, a table of every code-block's compressed
//! bytes (absolute file offset + length) and decode parameters.
//!
//! The T2 "feeder" (docs/weft-design.md §2): headers are parsed with a small
//! sliding window (bodies seeked over, never read), so planning is O(headers)
//! and an 800 MB file never enters memory. Block bytes are read on demand by
//! the SubbandDecode nodes via `pread` while decoding.
//!
//! Scope: any tile grid, any progression order, any layer count,
//! multi-component, multi-tile-part, SOP/EPH. Violations error rather than
//! produce wrong output.

use crate::decode::ReadAt;

use crate::codec::packet::{BlockState, PacketBitReader, TagTree, comb_packet_header};
use crate::codec::params::{CodParams, ProgressionOrder, QcdParams, SizParams};
use crate::codec::tile_geom::{Dims, TileGeom, chart_tile_geom};
use crate::decode::DecodeError;
use crate::decode::window::{TileCompWindow, chart_tile_comp_window, intersect, intersects};

/// One tile-part's position from the host's parsed TLM markers.
pub struct TlmEntry {
    pub tile: u16,
    /// Absolute file offset of the tile-part's SOT marker.
    pub offset: u64,
    /// Tile-part length (Psot).
    pub length: u32,
}

/// The main-header coding parameters the host codec parsed and handed in
/// (mercury no longer parses the main header itself; see `crate::capi`).
pub struct MainHeaderIn {
    pub siz: SizParams,
    pub cod: CodParams,
    /// Default quantization (QCD).
    pub qcd: QcdParams,
    /// Per-component quantization overrides (QCC): `(component index, params)`.
    pub qcc: Vec<(usize, QcdParams)>,
    /// Absolute file offset of the first SOT marker.
    pub first_sot_off: u64,
    /// Tile-part table from TLM markers, per tile in tile-part order; empty
    /// when the stream has no (trusted) TLM. With it, the plan reads only the
    /// tile-part headers of tiles it will decode instead of scanning the SOT
    /// chain.
    pub tlm: Vec<TlmEntry>,
}

/// A later-layer contribution to a code-block (bytes appended to the first).
#[derive(Clone)]
pub struct Chunk {
    pub file_off: u64,
    pub len: u32,
}

/// One code-block's compressed data location and decode parameters. Blocks
/// never included in any packet have `num_passes == 0` and decode to zeros.
///
/// Kept small on purpose: one per code-block (~366k for the ESP image), so
/// every byte here is ~0.4 MB of RSS. Multi-layer streams box their extra
/// chunks; multi-segment blocks (bypass modes; none in the corpus) box their
/// segment table.
#[derive(Clone, Default)]
pub struct BlockRec {
    /// Absolute file offset of the first byte of coded data.
    pub file_off: u64,
    /// Coded bytes of the first contribution.
    pub len: u32,
    /// All segment lengths when `num_segments > 1`, else None.
    pub seg_lens: Option<Box<[u32]>>,
    /// Later-layer contributions, in stream order.
    pub extra: Option<Box<Vec<Chunk>>>,
    pub num_passes: u8,
    pub missing_msbs: u8,
    pub num_segments: u8,
}

impl BlockRec {
    /// Total coded bytes across all contributions.
    pub fn bolt_len(&self) -> usize {
        self.len as usize
            + self
                .extra
                .as_ref()
                .map_or(0, |v| v.iter().map(|c| c.len as usize).sum())
    }
}

/// Everything needed to decode one subband independently.
pub struct BandPlan {
    /// 0=LL, 1=HL, 2=LH, 3=HH (T1 context orientation).
    pub band_type: u8,
    /// Band rectangle in band-domain canvas coordinates. The code-block grid
    /// is anchored at the canvas origin, so the first block row/column is
    /// partial when x0/y0 aren't block-aligned.
    pub x0: u32,
    pub y0: u32,
    pub width: u32,
    pub height: u32,
    /// Global code-block grid (canvas-anchored).
    pub blocks_wide: u32,
    /// `guard_bits + epsilon_b - 1` for this band.
    pub k_max_prime: i32,
    /// Irreversible dequant scale: raw QCD step size times accumulated
    /// synthesis-gain normalization (0.0 for reversible).
    pub delta: f32,
    /// Blocks in band-global raster order.
    pub blocks: Vec<BlockRec>,
}

/// Per-resolution bands: res 0 has one (LL); higher have three (HL, LH, HH).
pub struct ResPlan {
    pub bands: Vec<BandPlan>,
}

/// One tile's decode plan.
pub struct TilePlan {
    /// [component][resolution]. Empty when the tile misses the decode window.
    pub comps: Vec<Vec<ResPlan>>,
    pub geom: TileGeom,
    /// Per-component padded decode windows; None on a whole-tile decode.
    pub win: Option<Vec<TileCompWindow>>,
    /// False when the decode window misses this tile entirely: nothing of it
    /// was parsed and the graph must not build it.
    pub in_window: bool,
}

pub struct DecodePlan {
    /// Output dims, already reduced (the window's when one is set).
    pub width: u32,
    pub height: u32,
    /// Tiles in raster order (index = ty * tiles_across + tx).
    pub tiles: Vec<TilePlan>,
    pub cod: CodParams,
    pub siz: SizParams,
    /// Resolutions skipped from the top (0 = full resolution).
    pub reduce: u8,
    /// Decode window in canvas coordinates (unreduced, clipped to the image);
    /// None decodes the whole image.
    pub window: Option<Dims>,
}

/// `ceil(a / 2^b)`, the canvas reduction of T.800 Annex B.
pub fn ceildivpow2(a: u32, b: u8) -> u32 {
    ((a as u64 + (1u64 << b) - 1) >> b) as u32
}

/// Sliding window over the file for header parsing. Bodies are skipped by
/// seeking forward; the window refills lazily.
struct StreamWin<'f> {
    file: &'f dyn ReadAt,
    file_len: u64,
    buf: Vec<u8>,
    /// File offset of buf[0].
    buf_off: u64,
    /// Valid bytes in buf.
    valid: usize,
}

const WIN_SIZE: usize = 4 << 20;
/// Max bytes handed to one packet-header parse.
const MAX_HDR: usize = 256 << 10;

impl<'f> StreamWin<'f> {
    fn warp(file: &'f dyn ReadAt) -> std::io::Result<Self> {
        let file_len = file.extent()?;
        Ok(StreamWin {
            file,
            file_len,
            buf: vec![0u8; WIN_SIZE],
            buf_off: 0,
            valid: 0,
        })
    }

    /// A slice starting at absolute offset `off`, as long as available up to
    /// `limit`, refilling the window if needed.
    fn strand_at(&mut self, off: u64, limit: usize) -> std::io::Result<&[u8]> {
        let want = limit.min(MAX_HDR).min((self.file_len - off) as usize);
        let in_window =
            off >= self.buf_off && off + want as u64 <= self.buf_off + self.valid as u64;
        if !in_window {
            let n = WIN_SIZE.min((self.file_len - off) as usize);
            self.file.draw_at(&mut self.buf[..n], off)?;
            self.buf_off = off;
            self.valid = n;
        }
        let start = (off - self.buf_off) as usize;
        Ok(&self.buf[start..start + want])
    }
}

/// The packet-data stream of one tile: the concatenation of its tile-parts'
/// data ranges. Virtual offsets shuttle packet by packet; a packet never
/// spans tile-parts, so each packet maps to one contiguous real range.
struct TileStream {
    /// (real file offset, length) of each tile-part's packet data.
    segs: Vec<(u64, u64)>,
    /// Prefix sums of segment lengths (cum[i] = virtual offset of segs[i]).
    cum: Vec<u64>,
    total: u64,
}

impl TileStream {
    fn warp(segs: Vec<(u64, u64)>) -> Self {
        let mut cum = Vec::with_capacity(segs.len());
        let mut total = 0u64;
        for &(_, len) in &segs {
            cum.push(total);
            total += len;
        }
        TileStream { segs, cum, total }
    }

    /// Map a virtual offset to (real file offset, bytes remaining in the
    /// containing segment).
    fn unspool(&self, vpos: u64) -> Result<(u64, u64), DecodeError> {
        if vpos >= self.total {
            return Err(DecodeError::Logic(format!(
                "packet stream overrun: vpos {vpos} of {}",
                self.total
            )));
        }
        let i = match self.cum.binary_search(&vpos) {
            Ok(i) => i,
            Err(i) => i - 1,
        };
        let within = vpos - self.cum[i];
        Ok((self.segs[i].0 + within, self.segs[i].1 - within))
    }
}

/// One packet in progression order.
struct Pkt {
    comp: u16,
    res: u8,
    layer: u16,
    /// Precinct raster index within its (comp, res) precinct grid.
    prec: u32,
    /// False when the precinct misses the padded decode window in every band.
    in_win: bool,
}

/// Does the precinct at grid position (px, py) touch the padded decode window
/// in any of its bands? Mirrors classic's per-packet window skip (band
/// precinct bounds against getBandWindowPadded).
fn precinct_in_window(
    res: &crate::codec::tile_geom::ResolutionGeom,
    win_bands: &[Dims],
    px: u32,
    py: u32,
    ps: crate::codec::params::PrecinctSize,
    r: usize,
) -> bool {
    let band_scale = if r == 0 { 1 } else { 2 };
    let bpw = ps.width / band_scale;
    let bph = ps.height / band_scale;
    res.subbands.iter().zip(win_bands).any(|(sb, w)| {
        let rect = Dims {
            x0: (px * bpw).max(sb.dims.x0),
            y0: (py * bph).max(sb.dims.y0),
            x1: ((px + 1) * bpw).min(sb.dims.x1),
            y1: ((py + 1) * bph).min(sb.dims.y1),
        };
        intersects(&rect, w)
    })
}

/// Persistent per-precinct parse state (tag trees survive across layers).
struct PrecState {
    trees: Vec<TagTree>,
    states: Vec<Vec<BlockState>>,
}

/// Per-band block range of one precinct (band-global grid indices).
#[derive(Clone)]
struct PrecBand {
    bx0: u32,
    by0: u32,
    nbw: u32,
    nbh: u32,
}

/// `max_layers` keeps only the first that many quality layers (0 = all),
/// truncating the coding passes the dropped layers carry. `use_plt` lets the
/// parse hop over non-contributing packets by their PLT lengths. `window`
/// (canvas coordinates, unreduced, clipped to the image) restricts the decode:
/// tiles it misses are never parsed, precincts outside the padded band
/// windows are never filled, and packets past the last contributing one are
/// never touched.
pub fn draft(
    file: &dyn ReadAt,
    hdr: &MainHeaderIn,
    reduce: u8,
    max_layers: u16,
    use_plt: bool,
    window: Option<Dims>,
) -> Result<DecodePlan, DecodeError> {
    // The host codec parsed the main header (SIZ/COD/QCD/QCC) and handed it in;
    // mercury only walks the SOT/tile-part chain and the packet headers below.
    let file_len = file.extent().map_err(io_snag)?;

    // Whitelist code-block modes: RESET/CAUSAL/ERTERM/SEGMARK are handled by
    // the T1 and verified bit-exact. BYPASS/RESTART split codewords into
    // multiple segments (the packet parser reads one length per contribution)
    // and HT/HTMIX use part-15 packet-length signalling — all would misparse,
    // not just misdecode, so reject at plan time and let the host fall back.
    {
        use crate::codec::params::CodingModes;
        let supported =
            CodingModes::RESET | CodingModes::CAUSAL | CodingModes::ERTERM | CodingModes::SEGMARK;
        let m = hdr.cod.modes.0;
        if m & !supported != 0 {
            return Err(DecodeError::Logic(format!(
                "plan: unsupported code-block modes (Cmodes {m:#x})"
            )));
        }
    }

    // A zero-level image is just its LL band; the graph builder wires leaf
    // slices per decomposition level, so it cannot represent levels == 0.
    if hdr.cod.num_levels == 0 {
        return Err(DecodeError::Logic("plan: no decomposition levels".into()));
    }
    // A reduced decode runs a truncated chain, which still needs one level, so
    // the target resolution can never be 0 either.
    if reduce >= hdr.cod.num_levels {
        return Err(DecodeError::Logic(format!(
            "plan: reduce {reduce} leaves no decomposition level ({} available)",
            hdr.cod.num_levels
        )));
    }

    let num_tiles = (hdr.siz.tiles_across() * hdr.siz.tiles_down()) as usize;
    let num_comps = hdr.siz.comp_count();
    let n_res = (hdr.cod.num_levels + 1) as usize;
    let d = hdr.cod.num_levels as u32;
    let target_res = n_res - 1 - reduce as usize;

    // Per-component quantization: QCD unless a QCC override exists.
    let quant: Vec<QcdParams> = (0..num_comps)
        .map(|c| {
            hdr.qcc
                .iter()
                .find(|(idx, _)| *idx == c)
                .map(|(_, q)| q.clone())
                .unwrap_or_else(|| hdr.qcd.clone())
        })
        .collect();

    // Tiles the decode window misses are never parsed: with TLM their
    // tile-part headers are never even read, and the sequential walk only
    // reads their SOT to find the next tile-part.
    let tile_in_window: Vec<bool> = (0..num_tiles as u32)
        .map(|t| {
            let Some(w) = window else { return true };
            let ntx = hdr.siz.tiles_across();
            let (tx, ty) = (t % ntx, t / ntx);
            let rect = Dims {
                x0: (hdr.siz.xt_o_siz + tx * hdr.siz.xt_siz).max(hdr.siz.x_o_siz),
                y0: (hdr.siz.yt_o_siz + ty * hdr.siz.yt_siz).max(hdr.siz.y_o_siz),
                x1: (hdr.siz.xt_o_siz + (tx + 1) * hdr.siz.xt_siz).min(hdr.siz.x_siz),
                y1: (hdr.siz.yt_o_siz + (ty + 1) * hdr.siz.yt_siz).min(hdr.siz.y_siz),
            };
            intersects(&w, &rect)
        })
        .collect();

    // --- collect each decoded tile's packet-data segments ---
    let mut tile_segs: Vec<Vec<(u64, u64)>> = vec![Vec::new(); num_tiles];
    // Raw PLT payloads per tile: (Zplt, comma-coded length bytes).
    let mut tile_plt: Vec<Vec<(u8, Vec<u8>)>> = vec![Vec::new(); num_tiles];
    if !hdr.tlm.is_empty() {
        for e in &hdr.tlm {
            let t = e.tile as usize;
            if t >= num_tiles {
                return Err(DecodeError::Logic(format!(
                    "plan: TLM tile index {} out of range",
                    e.tile
                )));
            }
            if !tile_in_window[t] {
                continue;
            }
            if e.length < 14 {
                return Err(DecodeError::Logic("plan: TLM tile-part too short".into()));
            }
            let mut sot = [0u8; 12];
            file.draw_at(&mut sot, e.offset).map_err(io_snag)?;
            if u16::from_be_bytes([sot[0], sot[1]]) != 0xFF90
                || u16::from_be_bytes([sot[4], sot[5]]) != e.tile
            {
                return Err(DecodeError::Logic(format!(
                    "plan: TLM entry for tile {} does not point at its SOT",
                    e.tile
                )));
            }
            let seg = comb_tile_part(
                file,
                e.offset,
                e.length as u64,
                use_plt.then(|| &mut tile_plt[t]),
            )?;
            tile_segs[t].push(seg);
        }
    } else {
        // walk the SOT chain
        let mut sot_pos = hdr.first_sot_off;
        loop {
            let mut sot = [0u8; 12];
            file.draw_at(&mut sot, sot_pos).map_err(io_snag)?;
            if u16::from_be_bytes([sot[0], sot[1]]) != 0xFF90 {
                return Err(DecodeError::Logic(format!("expected SOT at {sot_pos}")));
            }
            let isot = u16::from_be_bytes([sot[4], sot[5]]) as usize;
            let psot = u32::from_be_bytes([sot[6], sot[7], sot[8], sot[9]]) as u64;
            if isot >= num_tiles {
                return Err(DecodeError::Logic(format!(
                    "plan: SOT tile index {isot} out of range"
                )));
            }
            if psot == 0 {
                return Err(DecodeError::Logic("plan: Psot=0 not supported".into()));
            }
            if tile_in_window[isot] {
                let seg =
                    comb_tile_part(file, sot_pos, psot, use_plt.then(|| &mut tile_plt[isot]))?;
                tile_segs[isot].push(seg);
            }
            sot_pos += psot;
            if sot_pos + 2 > file_len {
                break;
            }
            let mut m = [0u8; 2];
            file.draw_at(&mut m, sot_pos).map_err(io_snag)?;
            if u16::from_be_bytes(m) == 0xFFD9 {
                break;
            }
        }
    }

    // --- per-tile plans ---
    let mut win = StreamWin::warp(file).map_err(io_snag)?;
    let mut tiles: Vec<TilePlan> = Vec::with_capacity(num_tiles);
    for t in 0..num_tiles {
        let geom = chart_tile_geom(&hdr.siz, &hdr.cod, t as u16);
        // Per-component padded windows. A tile the window misses is never
        // parsed at all; its packet-data segments are simply dropped.
        let mut tile_wins: Option<Vec<TileCompWindow>> = None;
        if let Some(w) = window {
            if !tile_in_window[t] {
                tiles.push(TilePlan {
                    comps: Vec::new(),
                    geom,
                    win: None,
                    in_window: false,
                });
                continue;
            }
            let wins = geom
                .components
                .iter()
                .map(|tc| {
                    let clipped = intersect(w, tc.resolutions[n_res - 1].dims);
                    let resolutions: Vec<(Dims, Vec<(u8, Dims)>)> = tc
                        .resolutions
                        .iter()
                        .map(|res| {
                            (
                                res.dims,
                                res.subbands
                                    .iter()
                                    .map(|sb| (sb.band_type, sb.dims))
                                    .collect(),
                            )
                        })
                        .collect();
                    chart_tile_comp_window(
                        clipped,
                        &resolutions,
                        hdr.cod.num_levels,
                        hdr.cod.reversible,
                    )
                })
                .collect();
            tile_wins = Some(wins);
        }
        let plt = plt_packet_lengths(std::mem::take(&mut tile_plt[t]));
        let comps = comb_tile(
            file,
            &mut win,
            &hdr.cod,
            &quant,
            &hdr.siz,
            &geom,
            TileStream::warp(std::mem::take(&mut tile_segs[t])),
            n_res,
            d,
            target_res,
            max_layers,
            plt.as_deref(),
            tile_wins.as_deref(),
        )?;
        tiles.push(TilePlan {
            comps,
            geom,
            win: tile_wins,
            in_window: true,
        });
    }

    // Bound then subtract, matching grok's GrkImage::subsampleAndReduce:
    // reducing the difference instead would be off by one on odd origins.
    let out = window.unwrap_or(Dims {
        x0: hdr.siz.x_o_siz,
        y0: hdr.siz.y_o_siz,
        x1: hdr.siz.x_siz,
        y1: hdr.siz.y_siz,
    });
    Ok(DecodePlan {
        width: ceildivpow2(out.x1, reduce) - ceildivpow2(out.x0, reduce),
        height: ceildivpow2(out.y1, reduce) - ceildivpow2(out.y0, reduce),
        tiles,
        cod: hdr.cod.clone(),
        siz: hdr.siz.clone(),
        reduce,
        window,
    })
}

/// Scan one tile-part's header markers to SOD, collecting PLT payloads on the
/// way when a sink is given. Returns the packet-data segment (absolute
/// offset, length).
fn comb_tile_part(
    file: &dyn ReadAt,
    sot_pos: u64,
    psot: u64,
    mut plt_sink: Option<&mut Vec<(u8, Vec<u8>)>>,
) -> Result<(u64, u64), DecodeError> {
    let mut mp = sot_pos + 12;
    loop {
        let mut m = [0u8; 4];
        file.draw_at(&mut m, mp).map_err(io_snag)?;
        let code = u16::from_be_bytes([m[0], m[1]]);
        if code == 0xFF93 {
            mp += 2;
            break;
        }
        match code {
            // COD/COC/QCD/QCC tile overrides would silently change
            // decode parameters; PPT means packed headers elsewhere.
            0xFF52 | 0xFF53 | 0xFF5C | 0xFF5D | 0xFF61 => {
                return Err(DecodeError::Logic(format!(
                    "plan: tile-part marker {code:#x} not supported"
                )));
            }
            _ => {}
        }
        let l = u16::from_be_bytes([m[2], m[3]]) as u64;
        // PLT (A.7.1): Zplt byte then comma-coded packet lengths.
        if code == 0xFF58 && l >= 3 {
            if let Some(sink) = plt_sink.as_deref_mut() {
                let mut payload = vec![0u8; (l - 2) as usize];
                file.draw_at(&mut payload, mp + 4).map_err(io_snag)?;
                let lengths = payload.split_off(1);
                sink.push((payload[0], lengths));
            }
        }
        mp += 2 + l;
        if mp + 4 > sot_pos + psot {
            return Err(DecodeError::Logic("SOD not found in tile-part".into()));
        }
    }
    Ok((mp, sot_pos + psot - mp))
}

/// Decode one tile's PLT markers into per-packet lengths, in packet order.
/// Markers sort by Zplt, stably, so within one Zplt key tile-parts keep stream
/// order (same grouping classic grok uses); a comma-coded length may continue
/// across markers. None disables the shortcut: no markers, a dangling
/// continuation, a zero length (a packet is at least one byte), or a length
/// past 32 bits.
fn plt_packet_lengths(mut markers: Vec<(u8, Vec<u8>)>) -> Option<Vec<u32>> {
    if markers.is_empty() {
        return None;
    }
    markers.sort_by_key(|&(zplt, _)| zplt);
    let mut lengths = Vec::new();
    let mut acc: u64 = 0;
    let mut continued = false;
    for (_, data) in &markers {
        for &b in data {
            acc = (acc << 7) | (b & 0x7F) as u64;
            if acc > u32::MAX as u64 {
                return None;
            }
            continued = b & 0x80 != 0;
            if !continued {
                if acc == 0 {
                    return None;
                }
                lengths.push(acc as u32);
                acc = 0;
            }
        }
    }
    if continued { None } else { Some(lengths) }
}

/// Parse all of one tile's packets and build its band plans.
#[allow(clippy::too_many_arguments)]
fn comb_tile(
    file: &dyn ReadAt,
    win: &mut StreamWin<'_>,
    cod: &CodParams,
    quant: &[QcdParams],
    siz: &SizParams,
    geom: &TileGeom,
    stream: TileStream,
    n_res: usize,
    d: u32,
    target_res: usize,
    max_layers: u16,
    plt: Option<&[u32]>,
    wins: Option<&[TileCompWindow]>,
) -> Result<Vec<Vec<ResPlan>>, DecodeError> {
    let pkt_debug = std::env::var_os("MERCURY_PKT_DEBUG").is_some();
    let num_comps = geom.components.len();
    let block_w = cod.block_width;
    let block_h = cod.block_height;

    // Precincts smaller than the code-block would clamp the effective
    // block size (B.7: xcb' = min(xcb, PPx)); that clamp isn't wired, so
    // reject rather than mis-decode (dyadic precinct chains hit this).
    for r in 0..n_res {
        let ps = cod.precinct_span(r as u8);
        let scale = if r == 0 { 1 } else { 2 };
        if ps.width / scale < block_w || ps.height / scale < block_h {
            return Err(DecodeError::Logic(format!(
                "plan: res {r} precinct {}x{} smaller than code-block {block_w}x{block_h} \
                 (effective block clamping not wired)",
                ps.width, ps.height
            )));
        }
    }

    // --- band plans, sized from geometry ---
    let mut comps: Vec<Vec<ResPlan>> = Vec::with_capacity(num_comps);
    for c in 0..num_comps {
        let tc = &geom.components[c];
        let q = &quant[c];
        let mut res_plans: Vec<ResPlan> = Vec::with_capacity(n_res);
        for r in 0..n_res {
            let res = &tc.resolutions[r];
            let bands = res
                .subbands
                .iter()
                .enumerate()
                .map(|(i, sb)| {
                    let qcd_idx = if r == 0 { 0 } else { 1 + 3 * (r - 1) + i };
                    let epsilon_b = band_ranging(q, qcd_idx)?;
                    Ok(BandPlan {
                        band_type: sb.band_type,
                        x0: sb.dims.x0,
                        y0: sb.dims.y0,
                        width: sb.dims.width(),
                        height: sb.dims.height(),
                        blocks_wide: sb.blocks_wide,
                        k_max_prime: q.guard_bits as i32 + epsilon_b - 1,
                        delta: 0.0,
                        // reduced-away resolutions are still parsed (packet
                        // lengths only come from the headers) but never decoded
                        blocks: if r > target_res {
                            Vec::new()
                        } else {
                            vec![BlockRec::default(); (sb.blocks_wide * sb.blocks_high) as usize]
                        },
                    })
                })
                .collect::<Result<Vec<_>, DecodeError>>()?;
            res_plans.push(ResPlan { bands });
        }
        comps.push(res_plans);
    }

    // --- irreversible per-band deltas ---
    // Synthesis normalization recursion: the root engine gets normalization
    // 1.0; each level's bands divide by the kernel's low/high analysis scales
    // per direction, and the LL range feeds the next level down. The leaf
    // decoder's delta is the raw QCD step times that accumulated range.
    if !cod.reversible {
        let (low, high) = crate::dwt::level_builder::w9x7_gains();
        for c in 0..num_comps {
            let tc = &geom.components[c];
            let steps = &quant[c].steps;
            let step = |i: usize| -> Result<f32, DecodeError> {
                steps
                    .get(i)
                    .copied()
                    .ok_or_else(|| DecodeError::Logic("QCD: missing irreversible step".into()))
            };
            // Normalization accumulates from the top of the chain that will
            // actually run, so under reduce it restarts at the target.
            let mut norm = 1.0f32;
            for l in (0..target_res).rev() {
                let res = &tc.resolutions[l + 1];
                let ll = &tc.resolutions[l].dims;
                let hl = &res.subbands[0].dims;
                let lh = &res.subbands[1].dims;
                let unit_h = lh.height() == 0 || ll.height() == 0;
                let unit_w = hl.width() == 0 || ll.width() == 0;
                let mut rg = [norm; 4]; // LL, HL, LH, HH
                if !unit_h {
                    rg[0] /= low;
                    rg[1] /= low;
                    rg[2] /= high;
                    rg[3] /= high;
                }
                if !unit_w {
                    rg[0] /= low;
                    rg[1] /= high;
                    rg[2] /= low;
                    rg[3] /= high;
                }
                for bi in 0..comps[c][l + 1].bands.len() {
                    let raw = step(1 + 3 * l + bi)?;
                    comps[c][l + 1].bands[bi].delta = raw * rg[1 + bi];
                }
                norm = rg[0];
            }
            comps[c][0].bands[0].delta = step(0)? * norm;
        }
    }

    // --- precinct grids per (comp, res) ---
    // grid[c][r] = (px0, py0, npx, npy); raster index = (py-py0)*npx + (px-px0).
    let mut grids: Vec<Vec<(u32, u32, u32, u32)>> = Vec::with_capacity(num_comps);
    for c in 0..num_comps {
        let tc = &geom.components[c];
        let mut g = Vec::with_capacity(n_res);
        for r in 0..n_res {
            let res = &tc.resolutions[r];
            if res.dims.is_empty() {
                g.push((0, 0, 0, 0));
                continue;
            }
            let ps = cod.precinct_span(r as u8);
            let px0 = res.dims.x0 / ps.width;
            let py0 = res.dims.y0 / ps.height;
            let px1 = (res.dims.x1 - 1) / ps.width;
            let py1 = (res.dims.y1 - 1) / ps.height;
            g.push((px0, py0, px1 - px0 + 1, py1 - py0 + 1));
        }
        grids.push(g);
    }

    // --- packet schedule in progression order ---
    // Position (for RPCL/PCRL/CPRL) is the precinct grid line at res r
    // projected onto the reference grid, see prec_position.
    let mut pkts: Vec<([u64; 5], Pkt)> = Vec::new();
    let order = cod.order;
    for c in 0..num_comps {
        let dx = siz.components[c].xr_siz as u64;
        let dy = siz.components[c].yr_siz as u64;
        let tc = &geom.components[c];
        for r in 0..n_res {
            let (px0, py0, npx, npy) = grids[c][r];
            if npx == 0 {
                continue;
            }
            let res = &tc.resolutions[r];
            let ps = cod.precinct_span(r as u8);
            let scale = 1u64 << (d - r as u32);
            for py in py0..py0 + npy {
                for px in px0..px0 + npx {
                    let ypos =
                        prec_position(py, py0, ps.height, res.dims.y0, geom.tile_y0, scale * dy);
                    let xpos =
                        prec_position(px, px0, ps.width, res.dims.x0, geom.tile_x0, scale * dx);
                    let prec = (py - py0) * npx + (px - px0);
                    let in_win = wins
                        .map(|ws| precinct_in_window(res, &ws[c].bands[r], px, py, ps, r))
                        .unwrap_or(true);
                    for l in 0..cod.num_layers {
                        let (cu, ru, lu) = (c as u64, r as u64, l as u64);
                        let key = match order {
                            ProgressionOrder::Lrcp => [lu, ru, cu, py as u64, px as u64],
                            ProgressionOrder::Rlcp => [ru, lu, cu, py as u64, px as u64],
                            ProgressionOrder::Rpcl => [ru, ypos, xpos, cu, lu],
                            ProgressionOrder::Pcrl => [ypos, xpos, cu, ru, lu],
                            ProgressionOrder::Cprl => [cu, ypos, xpos, ru, lu],
                        };
                        pkts.push((
                            key,
                            Pkt {
                                comp: c as u16,
                                res: r as u8,
                                layer: l,
                                prec,
                                in_win,
                            },
                        ));
                    }
                }
            }
        }
    }
    pkts.sort_by(|a, b| a.0.cmp(&b.0));

    // --- parse packet headers in stream order ---
    // Persistent precinct state, keyed [comp][res][precinct raster index].
    let mut prec_states: Vec<Vec<Vec<Option<PrecState>>>> = (0..num_comps)
        .map(|c| {
            (0..n_res)
                .map(|r| {
                    let (_, _, npx, npy) = grids[c][r];
                    (0..npx * npy).map(|_| None).collect()
                })
                .collect()
        })
        .collect();

    // A packet contributes when its resolution survives reduce, its layer
    // survives the limit, and its precinct touches the decode window.
    let contributes = |pkt: &Pkt| {
        (pkt.res as usize) <= target_res
            && !(max_layers != 0 && pkt.layer >= max_layers)
            && pkt.in_win
    };
    // Nothing after the last contributing packet is ever needed, so the walk
    // ends there: no header parse, no PLT hop, no read. This is the uniform
    // form of classic's per-progression window bail-outs.
    let last_needed = pkts.iter().rposition(|(_, p)| contributes(p));
    let walk = last_needed.map_or(&pkts[..0], |i| &pkts[..=i]);

    let mut vpos: u64 = 0;
    let mut plt_idx: usize = 0;
    for (_, pkt) in walk {
        let (c, r) = (pkt.comp as usize, pkt.res as usize);
        // A PLT length covers the whole packet, SOP and header included
        // (A.7.1), so a packet that contributes no block records hops by
        // length, header unparsed. Skipped layers are the tail layers of a
        // precinct, skipped resolutions and out-of-window precincts never
        // parse at all, so the tag-tree state the parsed packets need is
        // never behind.
        let contributes = contributes(pkt);
        let plt_len = plt.and_then(|v| v.get(plt_idx)).copied();
        if !contributes {
            if let Some(len) = plt_len {
                plt_idx += 1;
                vpos += len as u64;
                continue;
            }
        }
        let pkt_start = vpos;
        let tc = &geom.components[c];
        let res = &tc.resolutions[r];
        let (px0, py0, npx, _) = grids[c][r];
        let ps = cod.precinct_span(r as u8);
        let px = px0 + pkt.prec % npx;
        let py = py0 + pkt.prec / npx;

        // Band-domain precinct rectangle: for r>0 the precinct halves.
        let band_scale = if r == 0 { 1 } else { 2 };
        let bpw = ps.width / band_scale;
        let bph = ps.height / band_scale;

        // Per-band block ranges for this precinct. Both the precinct and
        // code-block grids are anchored at the canvas origin.
        let prec_bands: Vec<PrecBand> = res
            .subbands
            .iter()
            .map(|sb| {
                let rx0 = (px * bpw).max(sb.dims.x0);
                let ry0 = (py * bph).max(sb.dims.y0);
                let rx1 = ((px + 1) * bpw).min(sb.dims.x1);
                let ry1 = ((py + 1) * bph).min(sb.dims.y1);
                if rx0 >= rx1 || ry0 >= ry1 {
                    return PrecBand {
                        bx0: 0,
                        by0: 0,
                        nbw: 0,
                        nbh: 0,
                    };
                }
                let first_bx = sb.dims.x0 / block_w;
                let first_by = sb.dims.y0 / block_h;
                let bx0 = rx0 / block_w - first_bx;
                let by0 = ry0 / block_h - first_by;
                let bx1 = (rx1 - 1) / block_w - first_bx;
                let by1 = (ry1 - 1) / block_h - first_by;
                PrecBand {
                    bx0,
                    by0,
                    nbw: bx1 - bx0 + 1,
                    nbh: by1 - by0 + 1,
                }
            })
            .collect();
        let state_slot = &mut prec_states[c][r][pkt.prec as usize];
        let state = state_slot.get_or_insert_with(|| PrecState {
            trees: prec_bands
                .iter()
                .map(|pb| TagTree::warp(pb.nbw.max(1), pb.nbh.max(1)))
                .collect(),
            states: prec_bands
                .iter()
                .map(|pb| vec![BlockState::warp(); (pb.nbw * pb.nbh) as usize])
                .collect(),
        });

        // SOP marker segment (6 bytes) before the packet, if signalled.
        if cod.use_sop {
            let (rp, avail) = stream.unspool(vpos)?;
            if avail >= 2 {
                let mut m = [0u8; 2];
                file.draw_at(&mut m, rp).map_err(io_snag)?;
                if u16::from_be_bytes(m) == 0xFF91 {
                    vpos += 6;
                }
            }
        }

        let (real_pos, seg_avail) = stream.unspool(vpos)?;
        let slice = win
            .strand_at(real_pos, seg_avail as usize)
            .map_err(io_snag)?;
        let mut reader = PacketBitReader::warp(slice);
        let parsed = comb_packet_header(
            &mut reader,
            &mut state.trees,
            &mut state.states,
            pkt.layer,
            0,
        )
        .map_err(|e| DecodeError::Logic(format!("packet parse at vpos {vpos}: {e:?}")))?;
        let mut hdr_len = parsed.header_bytes as u64;
        if pkt_debug {
            eprintln!(
                "PKT c={c} r={r} prec={} layer={} vpos={vpos} hdr={hdr_len} body={} nonempty={}",
                pkt.prec, pkt.layer, parsed.body_bytes, parsed.non_empty
            );
            for (band_idx, contribs) in parsed.contributions.iter().enumerate() {
                for (li, ct) in contribs.iter().enumerate() {
                    if ct.new_passes != 0 {
                        eprintln!(
                            "  band={band_idx} blk={li} passes={} msbs={} len={}",
                            ct.new_passes, ct.missing_msbs, ct.segment_lengths[0]
                        );
                    }
                }
            }
        }

        // EPH marker (2 bytes) terminates the packet header, if signalled.
        if cod.use_eph {
            hdr_len += 2;
        }

        // Assign body offsets in contribution order. Bodies are contiguous
        // in the real file (a packet never spans tile-parts).
        let (body_real, _) = stream.unspool(vpos + hdr_len).unwrap_or((0, 0));
        let mut body = body_real;
        // a non-contributing packet only reaches here with no PLT length to
        // hop by; its header is still parsed but fills no block records
        let bands_to_fill: &[PrecBand] = if contributes { &prec_bands } else { &[] };
        for (band_idx, pb) in bands_to_fill.iter().enumerate() {
            let contribs = &parsed.contributions[band_idx];
            let band = &mut comps[c][r].bands[band_idx];
            for (li, contrib) in contribs.iter().enumerate() {
                if contrib.new_passes == 0 {
                    continue;
                }
                let total: u32 = contrib
                    .segment_lengths
                    .iter()
                    .take(contrib.num_segments as usize)
                    .sum();
                let lx = li as u32 % pb.nbw;
                let ly = li as u32 / pb.nbw;
                let g = (pb.by0 + ly) * band.blocks_wide + (pb.bx0 + lx);
                let rec = &mut band.blocks[g as usize];
                if rec.num_passes == 0 {
                    // First contribution (inclusion always adds ≥1 pass).
                    rec.file_off = body;
                    rec.len = total;
                    rec.num_passes = contrib.new_passes;
                    rec.missing_msbs = contrib.missing_msbs;
                    rec.num_segments = contrib.num_segments;
                    if contrib.num_segments > 1 {
                        rec.seg_lens =
                            Some(contrib.segment_lengths[..contrib.num_segments as usize].into());
                    }
                } else {
                    // Later layer: append bytes, accumulate passes.
                    if rec.num_segments > 1 || contrib.num_segments > 1 {
                        return Err(DecodeError::Logic(
                            "plan: multi-segment multi-layer block not supported".into(),
                        ));
                    }
                    rec.num_passes = rec.num_passes.saturating_add(contrib.new_passes);
                    rec.extra.get_or_insert_with(Default::default).push(Chunk {
                        file_off: body,
                        len: total,
                    });
                }
                body += total as u64;
            }
        }
        // Single-layer streams never revisit a precinct: free its state now
        // rather than holding ~30k tag trees alive for the whole parse.
        if cod.num_layers == 1 {
            prec_states[c][r][pkt.prec as usize] = None;
        }
        vpos += hdr_len + parsed.body_bytes;
        // a PLT length that disagrees with the parse means one of them lied,
        // so reject and fall back to classic
        if let Some(len) = plt_len {
            plt_idx += 1;
            let parsed_len = vpos - pkt_start;
            if parsed_len != len as u64 {
                return Err(DecodeError::Logic(format!(
                    "plan: PLT length {len} != parsed packet length {parsed_len} at vpos {pkt_start}"
                )));
            }
        }
    }

    Ok(comps)
}

/// Reference-grid position of one precinct along an axis, the sort key of the
/// position-ordered progressions. A first row or column the tile edge clips
/// keys on the tile origin (T.800 B.12, the "y = ty0" case), which is what puts
/// every resolution's first precinct on one key; the resolution origin misses
/// the precinct grid exactly when it is clipped.
fn prec_position(
    index: u32,
    first_index: u32,
    prec_span: u32,
    res_origin: u32,
    tile_origin: u32,
    projection: u64,
) -> u64 {
    if index == first_index && res_origin % prec_span != 0 {
        tile_origin as u64
    } else {
        index as u64 * prec_span as u64 * projection
    }
}

/// Ranging exponent epsilon_b for band `qcd_idx`.
fn band_ranging(q: &QcdParams, qcd_idx: usize) -> Result<i32, DecodeError> {
    use crate::codec::params::QuantStyle;
    match q.style {
        QuantStyle::Reversible => q
            .ranges
            .get(qcd_idx)
            .map(|&e| e as i32)
            .ok_or_else(|| DecodeError::Logic("QCD: missing range entry".into())),
        QuantStyle::Expounded => q
            .steps
            .get(qcd_idx)
            .map(|&s| step_ranging(s))
            .ok_or_else(|| DecodeError::Logic("QCC: missing step entry".into())),
        QuantStyle::Derived => Err(DecodeError::Logic(
            "plan: derived quantization not wired yet".into(),
        )),
    }
}

/// Recover the ranging exponent from a parsed step size:
/// step = (1 + mu/2^11) / 2^eps with mu in [0, 2^11), so eps = -floor(log2 step).
fn step_ranging(step: f32) -> i32 {
    debug_assert!(step > 0.0);
    -(step.log2().floor() as i32)
}

fn io_snag(e: std::io::Error) -> DecodeError {
    DecodeError::Logic(format!("io: {e}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::codec::params::{
        CodingModes, ProgressionOrder, QuantStyle, SizComponent, SizParams,
    };

    const NUM_LEVELS: u8 = 5;

    fn siz() -> SizParams {
        SizParams {
            x_siz: 4001,
            y_siz: 3003,
            x_o_siz: 7,
            y_o_siz: 13,
            xt_siz: 1024,
            yt_siz: 1024,
            xt_o_siz: 0,
            yt_o_siz: 0,
            components: vec![SizComponent {
                precision: 8,
                is_signed: false,
                xr_siz: 1,
                yr_siz: 1,
            }],
        }
    }

    fn cod() -> CodParams {
        CodParams {
            order: ProgressionOrder::Lrcp,
            num_layers: 1,
            use_ycc: false,
            num_levels: NUM_LEVELS,
            block_width: 64,
            block_height: 64,
            modes: CodingModes(0),
            reversible: true,
            use_sop: false,
            use_eph: false,
            precincts: vec![],
        }
    }

    fn header() -> MainHeaderIn {
        MainHeaderIn {
            siz: siz(),
            cod: cod(),
            qcd: QcdParams {
                guard_bits: 2,
                style: QuantStyle::Reversible,
                ranges: vec![8; 3 * NUM_LEVELS as usize + 1],
                steps: vec![],
            },
            qcc: vec![],
            first_sot_off: 0,
            tlm: vec![],
        }
    }

    #[test]
    fn reduce_past_the_last_level_is_rejected() {
        let empty: Vec<u8> = Vec::new();
        for reduce in [NUM_LEVELS, NUM_LEVELS + 1, 200] {
            let Err(DecodeError::Logic(msg)) = draft(&empty, &header(), reduce, 0, true, None)
            else {
                panic!("reduce {reduce} must be rejected");
            };
            assert!(msg.contains("no decomposition level"), "got {msg}");
        }
    }

    #[test]
    fn reduce_within_range_passes_validation() {
        // there is no codestream behind the empty source, so the SOT walk fails;
        // what matters is that the failure is not the reduce rejection
        let empty: Vec<u8> = Vec::new();
        for reduce in 0..NUM_LEVELS {
            let Err(DecodeError::Logic(msg)) = draft(&empty, &header(), reduce, 0, true, None)
            else {
                panic!("there is no SOT to walk");
            };
            assert!(!msg.contains("no decomposition level"), "got {msg}");
        }
    }

    /// The reported output dims must equal the extent of the tile geometry the
    /// graph actually synthesizes at the target resolution.
    #[test]
    fn reduced_dims_span_the_target_resolution() {
        let (siz, cod) = (siz(), cod());
        let n_res = cod.num_levels as usize + 1;
        for reduce in 0..NUM_LEVELS {
            let target = n_res - 1 - reduce as usize;
            let mut x0 = u32::MAX;
            let mut x1 = 0u32;
            let mut y0 = u32::MAX;
            let mut y1 = 0u32;
            let num_tiles = siz.tiles_across() * siz.tiles_down();
            for t in 0..num_tiles {
                let dims = crate::codec::tile_geom::chart_tile_geom(&siz, &cod, t as u16)
                    .components[0]
                    .resolutions[target]
                    .dims;
                x0 = x0.min(dims.x0);
                x1 = x1.max(dims.x1);
                y0 = y0.min(dims.y0);
                y1 = y1.max(dims.y1);
            }
            assert_eq!(
                ceildivpow2(siz.x_siz, reduce) - ceildivpow2(siz.x_o_siz, reduce),
                x1 - x0,
                "width at reduce {reduce}"
            );
            assert_eq!(
                ceildivpow2(siz.y_siz, reduce) - ceildivpow2(siz.y_o_siz, reduce),
                y1 - y0,
                "height at reduce {reduce}"
            );
            assert_eq!(ceildivpow2(siz.x_o_siz, reduce), x0);
            assert_eq!(ceildivpow2(siz.y_o_siz, reduce), y0);
        }
    }

    /// First-precinct position of every non-empty resolution of one tile,
    /// computed the way `comb_tile` does.
    fn first_positions(siz: &SizParams, cod: &CodParams, tile_idx: u16) -> Vec<(u64, u64)> {
        let geom = crate::codec::tile_geom::chart_tile_geom(siz, cod, tile_idx);
        let d = cod.num_levels as u32;
        (0..=cod.num_levels as usize)
            .filter_map(|r| {
                let res = &geom.components[0].resolutions[r];
                if res.dims.is_empty() {
                    return None;
                }
                let ps = cod.precinct_span(r as u8);
                let scale = 1u64 << (d - r as u32);
                let (px0, py0) = (res.dims.x0 / ps.width, res.dims.y0 / ps.height);
                Some((
                    prec_position(px0, px0, ps.width, res.dims.x0, geom.tile_x0, scale),
                    prec_position(py0, py0, ps.height, res.dims.y0, geom.tile_y0, scale),
                ))
            })
            .collect()
    }

    /// A tile whose origin misses the precinct grid keeps every resolution's
    /// first precinct on the tile origin, so the position-ordered progressions
    /// cannot reorder those packets against the stream.
    #[test]
    fn first_precincts_of_an_unaligned_tile_share_the_tile_origin() {
        let mut siz = siz();
        siz.x_siz = 61;
        siz.y_siz = 69;
        siz.x_o_siz = 0;
        siz.y_o_siz = 0;
        siz.xt_siz = 14;
        siz.yt_siz = 15;
        let cod = cod();

        // tile (1, 1): origin (14, 15), which no resolution's ceil-rounded
        // origin projects back onto
        let positions = first_positions(&siz, &cod, siz.tiles_across() as u16 + 1);
        assert!(positions.len() > 1, "need several resolutions to compare");
        assert!(
            positions.iter().all(|&pos| pos == (14, 15)),
            "got {positions:?}"
        );
    }

    /// Later precinct rows and columns still key on their grid line, which the
    /// tile origin must not swallow.
    #[test]
    fn interior_precincts_key_on_the_precinct_grid() {
        let mut siz = siz();
        siz.x_siz = 4000;
        siz.y_siz = 4000;
        siz.x_o_siz = 0;
        siz.y_o_siz = 0;
        siz.xt_siz = 1000;
        siz.yt_siz = 1000;
        let mut cod = cod();
        cod.precincts = vec![
            crate::codec::params::PrecinctSize {
                width: 64,
                height: 64,
            };
            NUM_LEVELS as usize + 1
        ];

        let tile_idx = siz.tiles_across() as u16 + 1;
        let geom = crate::codec::tile_geom::chart_tile_geom(&siz, &cod, tile_idx);
        let res = &geom.components[0].resolutions[NUM_LEVELS as usize];
        let py0 = res.dims.y0 / 64;
        assert_eq!(
            prec_position(py0, py0, 64, res.dims.y0, geom.tile_y0, 1),
            1000
        );
        assert_eq!(
            prec_position(py0 + 1, py0, 64, res.dims.y0, geom.tile_y0, 1),
            (py0 as u64 + 1) * 64
        );
    }

    /// A synthetic one-tile stream: 8x8, one component, one decomposition
    /// level, one precinct and one code-block per band, LRCP. Every packet
    /// contributes one coding pass of `BODY_LEN` bytes to each of its blocks.
    const LAYERS: u16 = 3;
    const BLOCKS_PER_LAYER: u32 = 4;
    const BODY_LEN: u32 = 5;

    /// MSB-first packet-header bit writer. The test data is chosen to avoid
    /// 0xFF header bytes, so no bit stuffing is written.
    struct BitWriter {
        bytes: Vec<u8>,
        cur: u8,
        used: u8,
    }

    impl BitWriter {
        fn new() -> Self {
            BitWriter {
                bytes: Vec::new(),
                cur: 0,
                used: 0,
            }
        }

        fn put(&mut self, bit: u32) {
            self.cur = (self.cur << 1) | (bit as u8 & 1);
            self.used += 1;
            if self.used == 8 {
                assert_ne!(self.cur, 0xFF, "synthetic header must not need stuffing");
                self.bytes.push(self.cur);
                self.cur = 0;
                self.used = 0;
            }
        }

        fn put_bits(&mut self, value: u32, count: u32) {
            for i in (0..count).rev() {
                self.put((value >> i) & 1);
            }
        }

        fn finish(mut self) -> Vec<u8> {
            while self.used != 0 {
                self.put(0);
            }
            self.bytes
        }
    }

    /// One packet: `bands` blocks, one new coding pass each.
    fn synth_packet(first_layer: bool, bands: usize) -> Vec<u8> {
        let mut w = BitWriter::new();
        w.put(1); // non-empty
        for _ in 0..bands {
            if first_layer {
                w.put(1); // inclusion tag tree: included in this layer
                w.put(1); // missing-MSBs tag tree: zero planes
            } else {
                w.put(1); // already included: plain inclusion bit
            }
            w.put(0); // one new pass
            w.put(0); // beta unchanged (3)
            w.put_bits(BODY_LEN, 3); // beta 3 + ceil_log2(1 pass) length bits
        }
        let mut out = w.finish();
        out.resize(out.len() + bands * BODY_LEN as usize, 0xA5);
        out
    }

    /// The stream's packets in progression (= stream) order.
    fn synth_packets() -> Vec<Vec<u8>> {
        let mut packets = Vec::new();
        for layer in 0..LAYERS {
            packets.push(synth_packet(layer == 0, 1)); // res 0: LL
            packets.push(synth_packet(layer == 0, 3)); // res 1: HL, LH, HH
        }
        packets
    }

    /// One tile-part carrying the given PLT markers and packets.
    fn assemble_stream(packets: &[Vec<u8>], plt_markers: &[(u8, Vec<u8>)]) -> Vec<u8> {
        let body: usize = packets.iter().map(|p| p.len()).sum();
        let markers: usize = plt_markers.iter().map(|(_, d)| 5 + d.len()).sum();
        const TILE_PART_HEADER: usize = 14; // SOT (12) + SOD (2)
        let psot = (TILE_PART_HEADER + markers + body) as u32;
        let mut out = vec![0xFF, 0x90, 0x00, 0x0A, 0x00, 0x00];
        out.extend(psot.to_be_bytes());
        out.extend([0x00, 0x01]); // TPsot, TNsot
        for (zplt, data) in plt_markers {
            out.extend([0xFF, 0x58]);
            out.extend(((data.len() + 3) as u16).to_be_bytes());
            out.push(*zplt);
            out.extend(data);
        }
        out.extend([0xFF, 0x93]); // SOD
        for p in packets {
            out.extend(p);
        }
        out.extend([0xFF, 0xD9]); // EOC
        out
    }

    fn synth_stream() -> Vec<u8> {
        assemble_stream(&synth_packets(), &[])
    }

    /// Comma-code packet lengths (A.7.1): 7-bit groups, high bit = continued.
    fn plt_encode(lengths: &[u32]) -> Vec<u8> {
        let mut out = Vec::new();
        for &len in lengths {
            let mut groups = vec![(len & 0x7F) as u8];
            let mut v = len >> 7;
            while v != 0 {
                groups.push((v & 0x7F) as u8 | 0x80);
                v >>= 7;
            }
            groups.reverse();
            out.extend(groups);
        }
        out
    }

    fn synth_header() -> MainHeaderIn {
        let mut siz = siz();
        siz.x_siz = 8;
        siz.y_siz = 8;
        siz.x_o_siz = 0;
        siz.y_o_siz = 0;
        siz.xt_siz = 8;
        siz.yt_siz = 8;
        let mut cod = cod();
        cod.num_levels = 1;
        cod.num_layers = LAYERS;
        MainHeaderIn {
            siz,
            cod,
            qcd: QcdParams {
                guard_bits: 2,
                style: QuantStyle::Reversible,
                ranges: vec![8; 4],
                steps: vec![],
            },
            qcc: vec![],
            first_sot_off: 0,
            tlm: vec![],
        }
    }

    fn coded_bytes(plan: &DecodePlan) -> usize {
        plan.tiles
            .iter()
            .flat_map(|t| t.comps.iter().flatten())
            .flat_map(|r| r.bands.iter())
            .flat_map(|b| b.blocks.iter())
            .map(|blk| blk.bolt_len())
            .sum()
    }

    fn passes(plan: &DecodePlan) -> Vec<u8> {
        plan.tiles
            .iter()
            .flat_map(|t| t.comps.iter().flatten())
            .flat_map(|r| r.bands.iter())
            .flat_map(|b| b.blocks.iter())
            .map(|blk| blk.num_passes)
            .collect()
    }

    #[test]
    fn layer_limit_drops_later_contributions() {
        let stream = synth_stream();
        let hdr = synth_header();
        let per_layer = (BLOCKS_PER_LAYER * BODY_LEN) as usize;

        for keep in 1..=LAYERS {
            let plan = draft(&stream, &hdr, 0, keep, true, None).expect("plan must build");
            assert_eq!(
                coded_bytes(&plan),
                per_layer * keep as usize,
                "max_layers {keep}"
            );
            assert_eq!(
                passes(&plan),
                vec![keep as u8; BLOCKS_PER_LAYER as usize],
                "max_layers {keep}"
            );
        }
    }

    #[test]
    fn zero_and_oversized_layer_limits_decode_everything() {
        let stream = synth_stream();
        let hdr = synth_header();
        let full =
            coded_bytes(&draft(&stream, &hdr, 0, LAYERS, true, None).expect("plan must build"));
        for max_layers in [0, LAYERS + 1, u16::MAX] {
            let plan = draft(&stream, &hdr, 0, max_layers, true, None).expect("plan must build");
            assert_eq!(coded_bytes(&plan), full, "max_layers {max_layers}");
        }
    }

    fn packet_lengths(packets: &[Vec<u8>]) -> Vec<u32> {
        packets.iter().map(|p| p.len() as u32).collect()
    }

    fn d(x0: u32, y0: u32, x1: u32, y1: u32) -> Dims {
        Dims { x0, y0, x1, y1 }
    }

    /// A stream cut after layer 0 (in LRCP the dropped layers are a trailing
    /// run): the walk must end at the last contributing packet and never touch
    /// the missing bytes, with or without PLT.
    #[test]
    fn walk_stops_at_the_last_contributing_packet() {
        let packets = synth_packets();
        let layer0 = &packets[..2];
        let hdr = synth_header();
        let baseline = draft(&synth_stream(), &hdr, 0, 1, true, None).expect("baseline must build");
        for use_plt in [false, true] {
            let plt = if use_plt {
                vec![(0, plt_encode(&packet_lengths(&packets)))]
            } else {
                Vec::new()
            };
            let stream = assemble_stream(layer0, &plt);
            let plan = draft(&stream, &hdr, 0, 1, use_plt, None)
                .expect("plan must stop before the missing packets");
            assert_eq!(coded_bytes(&plan), coded_bytes(&baseline), "plt {use_plt}");
            assert_eq!(passes(&plan), passes(&baseline), "plt {use_plt}");
        }
    }

    /// RLCP puts the dropped layers of resolution 0 BETWEEN contributing
    /// packets, so early stop cannot cross them: only a PLT hop can. Their
    /// bytes are garbage, and a parse that touched them would land the next
    /// contributing packet on a wrong offset and die on the PLT cross-check,
    /// so a matching plan proves the packets went unparsed.
    #[test]
    fn plt_hops_mid_stream_skipped_packets_without_parsing() {
        let mut hdr = synth_header();
        hdr.cod.order = ProgressionOrder::Rlcp;
        // RLCP stream order: res 0 layers 0..3, then res 1 layers 0..3
        let clean: Vec<Vec<u8>> = (0..2u8)
            .flat_map(|r| {
                (0..LAYERS).map(move |l| synth_packet(l == 0, if r == 0 { 1 } else { 3 }))
            })
            .collect();
        let baseline_stream = assemble_stream(&clean, &[]);
        let baseline =
            draft(&baseline_stream, &hdr, 0, 1, false, None).expect("baseline must build");

        let mut packets = clean;
        for garbaged in &mut packets[1..=2] {
            let n = garbaged.len();
            *garbaged = vec![0xFF; n];
        }
        let plt = plt_encode(&packet_lengths(&packets));
        let stream = assemble_stream(&packets, &[(0, plt)]);
        let plan = draft(&stream, &hdr, 0, 1, true, None)
            .expect("plan must hop the garbaged dropped layers");
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
        assert_eq!(passes(&plan), passes(&baseline));
    }

    /// Every packet contributes, so every PLT length is cross-checked against
    /// its parsed extent, and the plan matches the no-PLT parse.
    #[test]
    fn plt_lengths_agree_with_a_full_parse() {
        let packets = synth_packets();
        let plt = plt_encode(&packet_lengths(&packets));
        let stream = assemble_stream(&packets, &[(0, plt)]);
        let hdr = synth_header();

        let baseline = draft(&synth_stream(), &hdr, 0, 0, true, None).expect("baseline must build");
        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("plan must build");
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
        assert_eq!(passes(&plan), passes(&baseline));
    }

    /// A window covering the whole image must not change the plan.
    #[test]
    fn full_cover_window_changes_nothing() {
        let stream = synth_stream();
        let hdr = synth_header();
        let full = draft(&stream, &hdr, 0, 0, true, None).expect("baseline must build");
        let windowed = draft(&stream, &hdr, 0, 0, true, Some(d(0, 0, 8, 8)))
            .expect("windowed plan must build");
        assert_eq!(coded_bytes(&windowed), coded_bytes(&full));
        assert_eq!(passes(&windowed), passes(&full));
        assert_eq!((windowed.width, windowed.height), (full.width, full.height));
        assert!(windowed.tiles[0].in_window);
    }

    /// Two tiles, the second one's packet bytes absent from the stream: a
    /// window over the first tile must never touch the second, so the plan
    /// builds where a whole-image parse cannot.
    #[test]
    fn window_skips_tiles_it_misses() {
        let mut hdr = synth_header();
        hdr.siz.x_siz = 16;
        let packets = synth_packets();
        let tile_part = |isot: u16, with_data: bool| -> Vec<u8> {
            let body: usize = packets.iter().map(|p| p.len()).sum();
            let psot = (14 + body) as u32;
            let mut out = vec![0xFF, 0x90, 0x00, 0x0A];
            out.extend(isot.to_be_bytes());
            out.extend(psot.to_be_bytes());
            out.extend([0x00, 0x01]); // TPsot, TNsot
            out.extend([0xFF, 0x93]); // SOD
            if with_data {
                for p in &packets {
                    out.extend(p.iter());
                }
            }
            out
        };
        let mut whole = tile_part(0, true);
        whole.extend(tile_part(1, true));
        whole.extend([0xFF, 0xD9]);
        let baseline = draft(&whole, &hdr, 0, 0, true, None).expect("baseline must build");

        let mut cut = tile_part(0, true);
        cut.extend(tile_part(1, false));
        cut.extend([0xFF, 0xD9]); // EOC where tile 1's packet bytes should be
        let win = Some(d(0, 0, 8, 8));
        let plan = draft(&cut, &hdr, 0, 0, true, win).expect("tile 1 must never be touched");
        assert!(!plan.tiles[1].in_window);
        assert!(plan.tiles[1].comps.is_empty());
        assert_eq!((plan.width, plan.height), (8, 8));
        // tile 0's records match the whole-image parse
        let tile0_passes = |p: &DecodePlan| -> Vec<u8> {
            p.tiles[0]
                .comps
                .iter()
                .flatten()
                .flat_map(|r| r.bands.iter())
                .flat_map(|b| b.blocks.iter())
                .map(|blk| blk.num_passes)
                .collect()
        };
        assert_eq!(tile0_passes(&plan), tile0_passes(&baseline));

        assert!(
            draft(&cut, &hdr, 0, 0, true, None).is_err(),
            "a whole-image parse needs tile 1's missing bytes"
        );
    }

    #[test]
    fn plt_disagreeing_with_a_parsed_packet_is_rejected() {
        let packets = synth_packets();
        let mut lengths = packet_lengths(&packets);
        lengths[0] += 1;
        let stream = assemble_stream(&packets, &[(0, plt_encode(&lengths))]);
        let Err(DecodeError::Logic(msg)) = draft(&stream, &synth_header(), 0, 0, true, None) else {
            panic!("a lying PLT must be rejected");
        };
        assert!(msg.contains("PLT length"), "got {msg}");
    }

    #[test]
    fn corrupt_plt_is_ignored() {
        let packets = synth_packets();
        let mut data = plt_encode(&packet_lengths(&packets));
        *data.last_mut().unwrap() |= 0x80; // dangling continuation
        let stream = assemble_stream(&packets, &[(0, data)]);
        let hdr = synth_header();

        let baseline = draft(&synth_stream(), &hdr, 0, 0, true, None).expect("baseline must build");
        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("plan must parse without PLT");
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
    }

    #[test]
    fn plt_lengths_thread_across_markers_and_sort_by_zplt() {
        // 300 comma-codes as [0x82, 0x2C]; split it across two markers handed
        // out of stream order
        let markers = vec![(1u8, vec![0x2C, 5]), (0u8, vec![7, 0x82])];
        assert_eq!(plt_packet_lengths(markers), Some(vec![7, 300, 5]));
    }

    #[test]
    fn plt_lengths_reject_malformed_codes() {
        assert_eq!(plt_packet_lengths(vec![]), None);
        assert_eq!(plt_packet_lengths(vec![(0, vec![0x85])]), None); // dangling
        assert_eq!(plt_packet_lengths(vec![(0, vec![0x00])]), None); // zero length
        assert_eq!(plt_packet_lengths(vec![(0, vec![0xFF; 5])]), None); // > 32 bits
    }
}
