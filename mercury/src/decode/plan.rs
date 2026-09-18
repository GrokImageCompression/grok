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
//! multi-component, multi-tile-part, SOP/EPH, and a tile-part header giving
//! its tile its own coding style and quantization. Violations error rather
//! than produce wrong output.

use crate::decode::ReadAt;

use crate::codec::packet::{BlockState, PacketBitReader, TagTree, comb_packet_header};
use crate::codec::params::{CodParams, ProgressionOrder, ProgressionVolume, QcdParams, SizParams};
use crate::codec::tile_geom::{Dims, TileGeom, chart_tile_geom};
use crate::decode::DecodeError;
use crate::decode::window::comp_window;
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
    /// Absolute file offset of the SOC marker.
    pub codestream_off: u64,
    /// Codestream length from `codestream_off`; 0 means it runs to the end of
    /// the file (raw .j2k input, or a jp2c box of undefined length).
    pub codestream_len: u64,
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
    // the box keeps BlockRec at 40 bytes instead of 56
    #[allow(clippy::box_collection)]
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
    /// Effective code-block size at this band's resolution (T.800 B.7 clamp).
    pub block_w: u32,
    pub block_h: u32,
    /// `guard_bits + epsilon_b - 1` for this band.
    pub k_max_prime: i32,
    /// Irreversible dequant scale: raw QCD step size times accumulated
    /// synthesis-gain normalization (0.0 for reversible).
    pub delta: f32,
    /// Stored block rectangle in the band-global block grid. A windowed decode
    /// keeps only the blocks its in-window precincts fill, so the origin is
    /// not the band's first block.
    stored_block_x0: u32,
    stored_block_y0: u32,
    stored_blocks_wide: u32,
    stored_blocks_high: u32,
    /// Blocks of the stored rectangle in raster order.
    blocks: Vec<BlockRec>,
}

impl BandPlan {
    /// Offset of a band-global block position within `blocks`. A position
    /// outside the stored rectangle is a planner bug, and the assert keeps a
    /// wrapped subtraction from landing on some other row's block.
    fn stored_index(&self, block_x: u32, block_y: u32) -> usize {
        let x = block_x.wrapping_sub(self.stored_block_x0);
        let y = block_y.wrapping_sub(self.stored_block_y0);
        assert!(
            x < self.stored_blocks_wide && y < self.stored_blocks_high,
            "block ({block_x}, {block_y}) outside the stored rectangle"
        );
        y as usize * self.stored_blocks_wide as usize + x as usize
    }

    fn block_mut(&mut self, block_x: u32, block_y: u32) -> &mut BlockRec {
        let i = self.stored_index(block_x, block_y);
        &mut self.blocks[i]
    }

    /// The `count` blocks from band-global block column `block_x` of block row
    /// `block_y`.
    pub fn block_row(&self, block_y: u32, block_x: u32, count: u32) -> &[BlockRec] {
        let first = self.stored_index(block_x, block_y);
        let last = self.stored_index(block_x + count - 1, block_y);
        &self.blocks[first..last + 1]
    }

    #[cfg(test)]
    fn blocks(&self) -> impl Iterator<Item = &BlockRec> {
        self.blocks.iter()
    }
}

/// Per-resolution bands: res 0 has one (LL); higher have three (HL, LH, HH).
pub struct ResPlan {
    pub bands: Vec<BandPlan>,
}

/// One tile's decode plan.
pub struct TilePlan {
    /// [component][resolution]. Empty when the tile misses the decode window.
    pub comps: Vec<Vec<ResPlan>>,
    /// This tile's coding style: the main header's, with its tile-part
    /// header's COD and COC applied.
    pub cod: CodParams,
    pub geom: TileGeom,
    /// Per-component padded decode windows; None on a whole-tile decode.
    pub win: Option<Vec<TileCompWindow>>,
    /// False when the decode window misses this tile entirely: nothing of it
    /// was parsed and the graph must not build it.
    pub in_window: bool,
}

pub struct DecodePlan {
    /// Output dims on the canvas plane, already reduced (the window's when one
    /// is set).
    pub width: u32,
    pub height: u32,
    /// Per component, the output rectangle on that component's reduced plane:
    /// grok's GrkImage::subsampleAndReduce, so `x0`/`y0` are the component's
    /// origin and `width()`/`height()` its sample counts.
    pub comp_dims: Vec<Dims>,
    /// Tiles in raster order (index = ty * tiles_across + tx).
    pub tiles: Vec<TilePlan>,
    pub cod: CodParams,
    pub siz: SizParams,
    /// Resolutions skipped from the top (0 = full resolution).
    pub reduce: u8,
    /// Irreversible 9/7 runs on int16 Q13 fixed-point samples instead of f32
    /// (see dwt::fixed97). Band deltas are then one-level gains, not the
    /// accumulated normalization.
    pub q13: bool,
}

/// The int16 fixed-point 9/7 path fits when every component's precision is
/// at most 8 bits (grok's grk_get_data_type rule, prec + 8 <= 16): below
/// that the Q13 fractional margin absorbs the lifting rounding, above it the
/// accumulated rounding shows against the T.803 tolerances.
/// MERCURY_FORCE_F32 keeps the float path for A/B comparison.
pub fn q13_97(cod: &CodParams, siz: &SizParams) -> bool {
    // every component's kernel is the same; draft rejects mixed ones
    !cod.comps[0].reversible
        && siz.components.iter().all(|c| c.precision <= 8)
        && std::env::var_os("MERCURY_FORCE_F32").is_none()
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

/// Half-open block rectangle in the band-global block grid.
#[derive(Clone, Copy)]
struct BlockRect {
    x0: u32,
    y0: u32,
    x1: u32,
    y1: u32,
}

impl BlockRect {
    const EMPTY: BlockRect = BlockRect {
        x0: 0,
        y0: 0,
        x1: 0,
        y1: 0,
    };
}

/// Grow `rect` to cover one precinct's block range in one band.
fn unite_block_rect(rect: &mut Option<BlockRect>, pb: &PrecBand) {
    if pb.nbw == 0 || pb.nbh == 0 {
        return;
    }
    let add = BlockRect {
        x0: pb.bx0,
        y0: pb.by0,
        x1: pb.bx0 + pb.nbw,
        y1: pb.by0 + pb.nbh,
    };
    *rect = Some(match *rect {
        None => add,
        Some(had) => BlockRect {
            x0: had.x0.min(add.x0),
            y0: had.y0.min(add.y0),
            x1: had.x1.max(add.x1),
            y1: had.y1.max(add.y1),
        },
    });
}

/// Per-band block ranges of the precinct at grid position (px, py). Both the
/// precinct and code-block grids are anchored at the canvas origin, so a block
/// never straddles two precincts.
fn chart_prec_bands(
    res: &crate::codec::tile_geom::ResolutionGeom,
    px: u32,
    py: u32,
    ps: crate::codec::params::PrecinctSize,
    r: usize,
    block_w: u32,
    block_h: u32,
) -> Vec<PrecBand> {
    // for r > 0 the precinct halves into the band domain
    let band_scale = if r == 0 { 1 } else { 2 };
    let bpw = ps.width / band_scale;
    let bph = ps.height / band_scale;
    res.subbands
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
        .collect()
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
    // for a jp2c box followed by more boxes the codestream ends before the file does
    let codestream_end = if hdr.codestream_len == 0 {
        file_len
    } else {
        hdr.codestream_off
            .saturating_add(hdr.codestream_len)
            .min(file_len)
    };

    check_coding_style(&hdr.cod, hdr.siz.comp_count(), reduce, "")?;

    let num_tiles = (hdr.siz.tiles_across() * hdr.siz.tiles_down()) as usize;
    let num_comps = hdr.siz.comp_count();

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
    // Each tile's first tile-part header's COD/COC/QCD/QCC segments, in header
    // order: (marker code, payload after Lmar).
    let mut tile_styles: Vec<Vec<(u16, Vec<u8>)>> = vec![Vec::new(); num_tiles];
    // A tile's coding style has to be known before its first packet, so only
    // the tile-part that opens a tile may carry those markers. TPsot names it,
    // but streams with a wrong TPsot decode anyway, so stream order decides.
    let mut tile_opened = vec![false; num_tiles];
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
                codestream_end,
                e.offset,
                e.length as u64,
                use_plt.then(|| &mut tile_plt[t]),
                (!std::mem::replace(&mut tile_opened[t], true)).then(|| &mut tile_styles[t]),
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
            // A.4.2: only the last tile-part may carry Psot=0, and it runs to the EOC
            let last = psot == 0;
            let psot = if last {
                let mut span = codestream_end.saturating_sub(sot_pos);
                if span >= 2 {
                    let mut m = [0u8; 2];
                    file.draw_at(&mut m, codestream_end - 2).map_err(io_snag)?;
                    if u16::from_be_bytes(m) == 0xFFD9 {
                        span -= 2;
                    }
                }
                span
            } else {
                psot
            };
            if tile_in_window[isot] {
                let seg = comb_tile_part(
                    file,
                    codestream_end,
                    sot_pos,
                    psot,
                    use_plt.then(|| &mut tile_plt[isot]),
                    (!std::mem::replace(&mut tile_opened[isot], true))
                        .then(|| &mut tile_styles[isot]),
                )?;
                tile_segs[isot].push(seg);
            }
            if last {
                break;
            }
            sot_pos += psot;
            if sot_pos + 2 > codestream_end {
                break;
            }
            // classic ignores whatever follows a tile-part if it is not an SOT
            let mut m = [0u8; 2];
            file.draw_at(&mut m, sot_pos).map_err(io_snag)?;
            if u16::from_be_bytes(m) != 0xFF90 {
                break;
            }
        }
    }

    // --- per-tile coding parameters ---
    let mut tile_params: Vec<(CodParams, Vec<QcdParams>)> = Vec::with_capacity(num_tiles);
    for (t, segs) in tile_styles.iter().enumerate() {
        if segs.is_empty() {
            tile_params.push((hdr.cod.clone(), quant.clone()));
            continue;
        }
        let (mut cod, tile_quant) = comb_tile_params(&hdr.cod, &quant, segs)?;
        // Classic bounds every tile at the main header's layer count, or at the
        // caller's limit when it set one: grok copies layersToDecompress_ into
        // the tile's coding params before reading the tile-part COD, and a
        // tile-part COD never raises it.
        let layer_bound = if max_layers != 0 {
            max_layers
        } else {
            hdr.cod.num_layers
        };
        cod.num_layers = cod.num_layers.min(layer_bound);
        check_coding_style(&cod, num_comps, reduce, &format!(" tile {t}"))?;
        // The sample path and the color transform are chosen once for the
        // whole image, in weave_sink, so no tile may move them.
        if cod.comps[0].reversible != hdr.cod.comps[0].reversible {
            return Err(DecodeError::Logic(format!(
                "plan: tile {t} changes the wavelet kernel"
            )));
        }
        if cod.use_ycc != hdr.cod.use_ycc {
            return Err(DecodeError::Logic(format!(
                "plan: tile {t} changes the component transform"
            )));
        }
        tile_params.push((cod, tile_quant));
    }

    // --- per-tile plans ---
    let mut win = StreamWin::warp(file).map_err(io_snag)?;
    let mut tiles: Vec<TilePlan> = Vec::with_capacity(num_tiles);
    for (t, (cod, tile_quant)) in tile_params.into_iter().enumerate() {
        let geom = chart_tile_geom(&hdr.siz, &cod, t as u16);
        // Per-component padded windows. A tile the window misses is never
        // parsed at all; its packet-data segments are simply dropped.
        let mut tile_wins: Option<Vec<TileCompWindow>> = None;
        if let Some(w) = window {
            if !tile_in_window[t] {
                tiles.push(TilePlan {
                    comps: Vec::new(),
                    cod,
                    geom,
                    win: None,
                    in_window: false,
                });
                continue;
            }
            let wins = geom
                .components
                .iter()
                .enumerate()
                .map(|(c, tc)| {
                    let style = &cod.comps[c];
                    let comp = &hdr.siz.components[c];
                    // the window arrives in canvas coordinates; every rect below
                    // this line lives on the component's own plane
                    let on_comp = comp_window(w, comp.xr_siz as u32, comp.yr_siz as u32);
                    let clipped =
                        intersect(on_comp, tc.resolutions[style.num_levels as usize].dims);
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
                        style.num_levels,
                        style.reversible,
                    )
                })
                .collect();
            tile_wins = Some(wins);
        }
        let plt = plt_packet_lengths(std::mem::take(&mut tile_plt[t]));
        let comps = comb_tile(
            file,
            &mut win,
            &cod,
            &tile_quant,
            &hdr.siz,
            &geom,
            TileStream::warp(std::mem::take(&mut tile_segs[t])),
            reduce,
            max_layers,
            plt.as_deref(),
            tile_wins.as_deref(),
        )?;
        tiles.push(TilePlan {
            comps,
            cod,
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
    let comp_dims = hdr
        .siz
        .components
        .iter()
        .map(|c| {
            let on_comp = comp_window(out, c.xr_siz as u32, c.yr_siz as u32);
            Dims {
                x0: ceildivpow2(on_comp.x0, reduce),
                y0: ceildivpow2(on_comp.y0, reduce),
                x1: ceildivpow2(on_comp.x1, reduce),
                y1: ceildivpow2(on_comp.y1, reduce),
            }
        })
        .collect();
    Ok(DecodePlan {
        width: ceildivpow2(out.x1, reduce) - ceildivpow2(out.x0, reduce),
        height: ceildivpow2(out.y1, reduce) - ceildivpow2(out.y0, reduce),
        comp_dims,
        tiles,
        cod: hdr.cod.clone(),
        siz: hdr.siz.clone(),
        reduce,
        q13: q13_97(&hdr.cod, &hdr.siz),
    })
}

/// Reject coding parameters mercury cannot decode. `label` names the tile the
/// parameters belong to, and is empty for the main header's.
fn check_coding_style(
    cod: &CodParams,
    num_comps: usize,
    reduce: u8,
    label: &str,
) -> Result<(), DecodeError> {
    // Whitelist code-block modes: RESET/CAUSAL/ERTERM/SEGMARK are handled by
    // the T1 and verified bit-exact. BYPASS/RESTART split codewords into
    // multiple segments (the packet parser reads one length per contribution)
    // and HT/HTMIX use part-15 packet-length signalling — all would misparse,
    // not just misdecode, so reject at plan time and let the host fall back.
    use crate::codec::params::CodingModes;
    let supported =
        CodingModes::RESET | CodingModes::CAUSAL | CodingModes::ERTERM | CodingModes::SEGMARK;
    for style in &cod.comps {
        let m = style.modes.0;
        if m & !supported != 0 {
            return Err(DecodeError::Logic(format!(
                "plan:{label} unsupported code-block modes (Cmodes {m:#x})"
            )));
        }
    }

    if cod.comps.len() != num_comps {
        return Err(DecodeError::Logic(format!(
            "plan:{label} {} coding styles for {num_comps} components",
            cod.comps.len()
        )));
    }
    // The sample path (5/3 integer or 9/7 float) is chosen once for the whole
    // image, so components cannot disagree on the kernel.
    if cod
        .comps
        .iter()
        .any(|s| s.reversible != cod.comps[0].reversible)
    {
        return Err(DecodeError::Logic(format!(
            "plan:{label} mixed wavelet kernels across components"
        )));
    }

    // Every component reduces by the same `reduce`, so the one with the fewest
    // levels is the binding limit.
    let fewest_levels = cod.comps.iter().map(|s| s.num_levels).min().unwrap_or(0);
    // A zero-level image is just its LL band; the graph builder wires leaf
    // slices per decomposition level, so it cannot represent levels == 0.
    if fewest_levels == 0 {
        return Err(DecodeError::Logic(format!(
            "plan:{label} no decomposition levels"
        )));
    }
    // A reduced decode runs a truncated chain, which still needs one level, so
    // the target resolution can never be 0 either.
    if reduce >= fewest_levels {
        return Err(DecodeError::Logic(format!(
            "plan:{label} reduce {reduce} leaves no decomposition level ({fewest_levels} available)"
        )));
    }
    Ok(())
}

/// One tile's coding parameters: the main header's with its tile-part header's
/// markers applied in header order (T.800 A.6.1, A.6.2, A.6.4, A.6.5). A COD
/// replaces every component's coding style and a QCD every component's
/// quantization, except the components a COC or QCC of the same tile-part
/// header covers.
fn comb_tile_params(
    base_cod: &CodParams,
    base_quant: &[QcdParams],
    segs: &[(u16, Vec<u8>)],
) -> Result<(CodParams, Vec<QcdParams>), DecodeError> {
    use crate::codec::markers;
    let num_comps = base_quant.len();
    let mut cod = base_cod.clone();
    let mut quant = base_quant.to_vec();
    let mut saw_cod = false;
    let mut saw_qcd = false;
    let mut saw_qcc = vec![false; num_comps];
    for (code, payload) in segs {
        let snag = |why: String| DecodeError::Logic(format!("plan: tile-part {code:#x}: {why}"));
        match *code {
            0xFF52 => {
                if saw_cod {
                    return Err(snag("second COD in one tile header".into()));
                }
                saw_cod = true;
                markers::comb_cod(payload, &mut cod).map_err(snag)?;
            }
            0xFF53 => {
                markers::comb_coc(payload, &mut cod).map_err(snag)?;
            }
            0xFF5C => {
                if saw_qcd {
                    return Err(snag("second QCD in one tile header".into()));
                }
                saw_qcd = true;
                let q = markers::comb_qcd(payload).map_err(snag)?;
                if !saw_qcc[0] {
                    quant[0] = q;
                }
                for c in 1..num_comps {
                    if !saw_qcc[c] {
                        quant[c] = quant[0].clone();
                    }
                }
            }
            0xFF5D => {
                let (c, q) = markers::comb_qcc(payload, num_comps).map_err(snag)?;
                if saw_qcc[c] {
                    return Err(snag(format!("second QCC for component {c}")));
                }
                saw_qcc[c] = true;
                quant[c] = q;
            }
            _ => unreachable!("only COD/COC/QCD/QCC are collected"),
        }
    }
    Ok((cod, quant))
}

/// Scan one tile-part's header markers to SOD, collecting PLT payloads on the
/// way when a sink is given. Returns the packet-data segment (absolute
/// offset, length), clipped to the codestream end: a Psot reaching past it
/// means a truncated (or lying) stream, and those bytes do not exist.
fn comb_tile_part(
    file: &dyn ReadAt,
    codestream_end: u64,
    sot_pos: u64,
    psot: u64,
    mut plt_sink: Option<&mut Vec<(u8, Vec<u8>)>>,
    mut style_sink: Option<&mut Vec<(u16, Vec<u8>)>>,
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
            // PPT means packed headers elsewhere, a POC reorders the tile's
            // packets away from the COD progression, and an RGN upshifts the
            // tile's coefficients.
            0xFF5E | 0xFF5F | 0xFF61 => {
                return Err(DecodeError::Logic(format!(
                    "plan: tile-part marker {code:#x} not supported"
                )));
            }
            _ => {}
        }
        let l = u16::from_be_bytes([m[2], m[3]]) as u64;
        // COD/COC/QCD/QCC give the tile its own coding style and quantization
        // (A.6); only the first tile-part of a tile may carry them, which is
        // the one the sink is given for.
        if matches!(code, 0xFF52 | 0xFF53 | 0xFF5C | 0xFF5D) {
            let Some(sink) = style_sink.as_deref_mut() else {
                return Err(DecodeError::Logic(format!(
                    "plan: tile-part marker {code:#x} outside the first tile-part"
                )));
            };
            if l < 3 {
                return Err(DecodeError::Logic(format!(
                    "plan: tile-part marker {code:#x} carries no payload"
                )));
            }
            let mut payload = vec![0u8; (l - 2) as usize];
            file.draw_at(&mut payload, mp + 4).map_err(io_snag)?;
            sink.push((code, payload));
        }
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
    Ok((mp, (sot_pos + psot).min(codestream_end).saturating_sub(mp)))
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
    reduce: u8,
    max_layers: u16,
    plt: Option<&[u32]>,
    wins: Option<&[TileCompWindow]>,
) -> Result<Vec<Vec<ResPlan>>, DecodeError> {
    let pkt_debug = std::env::var_os("MERCURY_PKT_DEBUG").is_some();
    let num_comps = geom.components.len();
    // Resolution counts and the reduce target are per component (COC).
    let n_res = |c: usize| cod.comps[c].num_levels as usize + 1;
    let target_res = |c: usize| cod.comps[c].num_levels as usize - reduce as usize;

    // --- precinct grids per (comp, res) ---
    // grid[c][r] = (px0, py0, npx, npy); raster index = (py-py0)*npx + (px-px0).
    let mut grids: Vec<Vec<(u32, u32, u32, u32)>> = Vec::with_capacity(num_comps);
    for c in 0..num_comps {
        let tc = &geom.components[c];
        let mut g = Vec::with_capacity(n_res(c));
        for r in 0..n_res(c) {
            let res = &tc.resolutions[r];
            if res.dims.is_empty() {
                g.push((0, 0, 0, 0));
                continue;
            }
            let ps = cod.comps[c].precinct_span(r as u8);
            let px0 = res.dims.x0 / ps.width;
            let py0 = res.dims.y0 / ps.height;
            let px1 = (res.dims.x1 - 1) / ps.width;
            let py1 = (res.dims.y1 - 1) / ps.height;
            g.push((px0, py0, px1 - px0 + 1, py1 - py0 + 1));
        }
        grids.push(g);
    }

    // Every packet spends at least one bit of packet data, so a tile whose
    // declared packet count needs more than eight bits per byte it has cannot
    // be real. Checked before the enumeration below so neither that loop nor
    // `pkts` ever scales with a hostile precinct count.
    let declared_packets = grids
        .iter()
        .flatten()
        .fold(0u64, |total, &(_, _, npx, npy)| {
            total.saturating_add(
                (npx as u64)
                    .saturating_mul(npy as u64)
                    .saturating_mul(cod.num_layers as u64),
            )
        });
    if declared_packets / 8 > stream.total {
        return Err(DecodeError::Logic(format!(
            "plan: {declared_packets} declared packets in a tile of {} packet bytes",
            stream.total
        )));
    }

    // --- per-precinct facts every progression volume reuses ---
    // Position (for RPCL/PCRL/CPRL) is the precinct grid line at res r
    // projected onto the reference grid, see prec_position.
    // This walk also collects the block rectangle each band stores: the writer
    // below fills every band of a precinct that passes the any-band window
    // predicate, so a band's rectangle unions its own range over all those
    // precincts, not just the ones whose slice of it is in the window.
    let mut band_rects: Vec<Vec<Vec<Option<BlockRect>>>> = (0..num_comps)
        .map(|c| {
            (0..n_res(c))
                .map(|r| vec![None; geom.components[c].resolutions[r].subbands.len()])
                .collect()
        })
        .collect();
    // (ypos, xpos, in_win) per precinct, in raster index order.
    let mut prec_info: Vec<Vec<Vec<(u64, u64, bool)>>> = (0..num_comps)
        .map(|c| (0..n_res(c)).map(|_| Vec::new()).collect())
        .collect();
    for c in 0..num_comps {
        let dx = siz.components[c].xr_siz as u64;
        let dy = siz.components[c].yr_siz as u64;
        let tc = &geom.components[c];
        let style = &cod.comps[c];
        for r in 0..n_res(c) {
            let (px0, py0, npx, npy) = grids[c][r];
            if npx == 0 {
                continue;
            }
            let res = &tc.resolutions[r];
            let ps = style.precinct_span(r as u8);
            let (block_w, block_h) = style.block_span(r as u8);
            let scale = 1u64 << (style.num_levels as u32 - r as u32);
            for py in py0..py0 + npy {
                for px in px0..px0 + npx {
                    let ypos =
                        prec_position(py, py0, ps.height, res.dims.y0, geom.tile_y0, scale * dy);
                    let xpos =
                        prec_position(px, px0, ps.width, res.dims.x0, geom.tile_x0, scale * dx);
                    let prec_bands = chart_prec_bands(res, px, py, ps, r, block_w, block_h);
                    // every code-block spends at least one header bit, so a
                    // precinct needing more than eight header bits per byte the
                    // tile has cannot be real (classic's PacketParser check)
                    let prec_blocks: u64 = prec_bands
                        .iter()
                        .map(|pb| pb.nbw as u64 * pb.nbh as u64)
                        .sum();
                    if prec_blocks / 8 > stream.total {
                        return Err(DecodeError::Logic(format!(
                            "plan: precinct with {prec_blocks} code-blocks in a tile of {} \
                             packet bytes",
                            stream.total
                        )));
                    }
                    let in_win = wins
                        .map(|ws| precinct_in_window(res, &ws[c].bands[r], px, py, ps, r))
                        .unwrap_or(true);
                    if wins.is_some() && in_win {
                        for (band_idx, pb) in prec_bands.iter().enumerate() {
                            unite_block_rect(&mut band_rects[c][r][band_idx], pb);
                        }
                    }
                    prec_info[c][r].push((ypos, xpos, in_win));
                }
            }
        }
    }

    // --- packet schedule, one progression volume at a time ---
    let volumes = chart_volumes(cod, num_comps, (0..num_comps).map(n_res).max().unwrap_or(0))?;
    // Volumes overlap, so a packet an earlier one already emitted is skipped
    // (T.800 A.6.6); with a single volume nothing can repeat.
    let mut emitted: Vec<Vec<Vec<bool>>> = if volumes.len() > 1 {
        (0..num_comps)
            .map(|c| {
                (0..n_res(c))
                    .map(|r| {
                        let (_, _, npx, npy) = grids[c][r];
                        vec![false; (npx * npy) as usize * cod.num_layers as usize]
                    })
                    .collect()
            })
            .collect()
    } else {
        Vec::new()
    };
    let mut pkts: Vec<Pkt> = Vec::new();
    let mut vol_pkts: Vec<([u64; 5], Pkt)> = Vec::new();
    for vol in &volumes {
        vol_pkts.clear();
        for c in vol.comp_s as usize..vol.comp_e as usize {
            for r in vol.res_s as usize..(vol.res_e as usize).min(n_res(c)) {
                let (px0, py0, npx, _) = grids[c][r];
                if npx == 0 {
                    continue;
                }
                for (prec, &(ypos, xpos, in_win)) in prec_info[c][r].iter().enumerate() {
                    let py = py0 + prec as u32 / npx;
                    let px = px0 + prec as u32 % npx;
                    for l in 0..vol.lay_e {
                        let key = progression_key(
                            vol.order, c as u64, r as u64, l as u64, py as u64, px as u64, ypos,
                            xpos,
                        );
                        vol_pkts.push((
                            key,
                            Pkt {
                                comp: c as u16,
                                res: r as u8,
                                layer: l,
                                prec: prec as u32,
                                in_win,
                            },
                        ));
                    }
                }
            }
        }
        vol_pkts.sort_by_key(|p| p.0);
        for (_, pkt) in vol_pkts.drain(..) {
            if !emitted.is_empty() {
                let slot = &mut emitted[pkt.comp as usize][pkt.res as usize]
                    [pkt.prec as usize * cod.num_layers as usize + pkt.layer as usize];
                if *slot {
                    continue;
                }
                *slot = true;
            }
            pkts.push(pkt);
        }
    }

    // --- band plans, sized to the block rectangle the walk will fill ---
    let mut comps: Vec<Vec<ResPlan>> = Vec::with_capacity(num_comps);
    for c in 0..num_comps {
        let tc = &geom.components[c];
        let q = &quant[c];
        let mut res_plans: Vec<ResPlan> = Vec::with_capacity(n_res(c));
        for (r, res) in tc.resolutions.iter().enumerate().take(n_res(c)) {
            let (block_w, block_h) = cod.comps[c].block_span(r as u8);
            let bands = res
                .subbands
                .iter()
                .enumerate()
                .map(|(i, sb)| {
                    let qcd_idx = if r == 0 { 0 } else { 1 + 3 * (r - 1) + i };
                    let epsilon_b = band_ranging(q, qcd_idx)?;
                    // reduced-away resolutions are still parsed (packet lengths
                    // only come from the headers) but never decoded
                    let rect = if r > target_res(c) {
                        BlockRect::EMPTY
                    } else if wins.is_some() {
                        band_rects[c][r][i].unwrap_or(BlockRect::EMPTY)
                    } else {
                        BlockRect {
                            x0: 0,
                            y0: 0,
                            x1: sb.blocks_wide,
                            y1: sb.blocks_high,
                        }
                    };
                    let wide = rect.x1 - rect.x0;
                    let high = rect.y1 - rect.y0;
                    Ok(BandPlan {
                        band_type: sb.band_type,
                        x0: sb.dims.x0,
                        y0: sb.dims.y0,
                        width: sb.dims.width(),
                        height: sb.dims.height(),
                        block_w,
                        block_h,
                        k_max_prime: q.guard_bits as i32 + epsilon_b - 1,
                        delta: 0.0,
                        stored_block_x0: rect.x0,
                        stored_block_y0: rect.y0,
                        stored_blocks_wide: wide,
                        stored_blocks_high: high,
                        blocks: vec![BlockRec::default(); wide as usize * high as usize],
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
    if !cod.comps[0].reversible {
        let (low, high) = crate::dwt::level_builder::w9x7_gains();
        let q13 = q13_97(cod, siz);
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
            for l in (0..target_res(c)).rev() {
                // Q13: one-level gains only; the graph rescales LL rows by
                // K per non-unit direction as each level consumes them.
                if q13 {
                    norm = 1.0;
                }
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

    // --- parse packet headers in stream order ---
    // Persistent precinct state, keyed [comp][res][precinct raster index].
    let mut prec_states: Vec<Vec<Vec<Option<PrecState>>>> = (0..num_comps)
        .map(|c| {
            (0..n_res(c))
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
        (pkt.res as usize) <= target_res(pkt.comp as usize)
            && !(max_layers != 0 && pkt.layer >= max_layers)
            && pkt.in_win
    };
    // Nothing after the last contributing packet is ever needed, so the walk
    // ends there: no header parse, no PLT hop, no read. This is the uniform
    // form of classic's per-progression window bail-outs.
    let last_needed = pkts.iter().rposition(&contributes);
    let walk = last_needed.map_or(&pkts[..0], |i| &pkts[..=i]);

    let mut vpos: u64 = 0;
    let mut plt_idx: usize = 0;
    for pkt in walk {
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
        let ps = cod.comps[c].precinct_span(r as u8);
        let (block_w, block_h) = cod.comps[c].block_span(r as u8);
        let px = px0 + pkt.prec % npx;
        let py = py0 + pkt.prec / npx;

        let prec_bands = chart_prec_bands(res, px, py, ps, r, block_w, block_h);
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
                // classic abandons the tile at a block claiming more missing
                // bit planes than its band has, so a plan that kept parsing
                // would decode bytes classic never reads
                if contrib.missing_msbs as i32 > band.k_max_prime.max(0) {
                    return Err(DecodeError::Logic(format!(
                        "plan: {} missing bit planes in a band of {}",
                        contrib.missing_msbs, band.k_max_prime
                    )));
                }
                let rec = band.block_mut(pb.bx0 + lx, pb.by0 + ly);
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

/// The tile's progression volume list, clamped and rejected the way classic
/// grok's `finalizePocs` does at the end of the main header. No POC is one
/// volume covering every layer, resolution and component in the COD order.
fn chart_volumes(
    cod: &CodParams,
    num_comps: usize,
    max_res: usize,
) -> Result<Vec<ProgressionVolume>, DecodeError> {
    if cod.pocs.is_empty() {
        return Ok(vec![ProgressionVolume {
            res_s: 0,
            comp_s: 0,
            lay_e: cod.num_layers,
            res_e: max_res as u8,
            comp_e: num_comps as u16,
            order: cod.order,
        }]);
    }
    let mut volumes = Vec::with_capacity(cod.pocs.len());
    for vol in &cod.pocs {
        let mut vol = *vol;
        vol.lay_e = vol.lay_e.min(cod.num_layers);
        vol.res_e = vol.res_e.min(max_res as u8);
        vol.comp_e = vol.comp_e.min(num_comps as u16);
        if vol.res_s >= vol.res_e || vol.comp_s >= vol.comp_e || vol.lay_e == 0 {
            return Err(DecodeError::Logic(format!(
                "plan: empty POC volume, res {}..{} comp {}..{} layers {}",
                vol.res_s, vol.res_e, vol.comp_s, vol.comp_e, vol.lay_e
            )));
        }
        volumes.push(vol);
    }
    Ok(volumes)
}

/// Sort key of one packet under a progression order (T.800 B.12.1.1-5).
#[allow(clippy::too_many_arguments)]
fn progression_key(
    order: ProgressionOrder,
    comp: u64,
    res: u64,
    layer: u64,
    py: u64,
    px: u64,
    ypos: u64,
    xpos: u64,
) -> [u64; 5] {
    match order {
        ProgressionOrder::Lrcp => [layer, res, comp, py, px],
        ProgressionOrder::Rlcp => [res, layer, comp, py, px],
        ProgressionOrder::Rpcl => [res, ypos, xpos, comp, layer],
        ProgressionOrder::Pcrl => [ypos, xpos, comp, res, layer],
        ProgressionOrder::Cprl => [comp, ypos, xpos, res, layer],
    }
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
    if index == first_index && !res_origin.is_multiple_of(prec_span) {
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
        CodingModes, CompCodingStyle, ProgressionOrder, QuantStyle, SizComponent, SizParams,
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

    fn comp_style() -> CompCodingStyle {
        CompCodingStyle {
            num_levels: NUM_LEVELS,
            block_width: 64,
            block_height: 64,
            modes: CodingModes(0),
            reversible: true,
            precincts: vec![],
        }
    }

    fn cod() -> CodParams {
        CodParams {
            order: ProgressionOrder::Lrcp,
            pocs: vec![],
            num_layers: 1,
            use_ycc: false,
            use_sop: false,
            use_eph: false,
            comps: vec![comp_style()],
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
            codestream_off: 0,
            codestream_len: 0,
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
        let n_res = cod.comps[0].num_levels as usize + 1;
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
        let d = cod.comps[0].num_levels as u32;
        (0..=cod.comps[0].num_levels as usize)
            .filter_map(|r| {
                let res = &geom.components[0].resolutions[r];
                if res.dims.is_empty() {
                    return None;
                }
                let ps = cod.comps[0].precinct_span(r as u8);
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
        cod.comps[0].precincts = vec![
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
        cod.comps[0].num_levels = 1;
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
            codestream_off: 0,
            codestream_len: 0,
            first_sot_off: 0,
            tlm: vec![],
        }
    }

    fn coded_bytes(plan: &DecodePlan) -> usize {
        plan.tiles
            .iter()
            .flat_map(|t| t.comps.iter().flatten())
            .flat_map(|r| r.bands.iter())
            .flat_map(|b| b.blocks())
            .map(|blk| blk.bolt_len())
            .sum()
    }

    fn block_offsets(plan: &DecodePlan) -> Vec<u64> {
        plan.tiles
            .iter()
            .flat_map(|t| t.comps.iter().flatten())
            .flat_map(|r| r.bands.iter())
            .flat_map(|b| b.blocks())
            .map(|blk| blk.file_off)
            .collect()
    }

    fn passes(plan: &DecodePlan) -> Vec<u8> {
        plan.tiles
            .iter()
            .flat_map(|t| t.comps.iter().flatten())
            .flat_map(|r| r.bands.iter())
            .flat_map(|b| b.blocks())
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

    /// Layer count high enough that the 8x8 stream's two precincts declare more
    /// packets than eight per byte the tile carries.
    const OVERSIZED_LAYERS: u16 = 10_000;

    /// A packet costs at least one bit of packet data, so a tile declaring more
    /// packets than its bytes can hold is rejected before the packet schedule
    /// is enumerated.
    #[test]
    fn more_declared_packets_than_the_tile_bytes_is_rejected() {
        let stream = synth_stream();
        let mut hdr = synth_header();
        hdr.cod.num_layers = OVERSIZED_LAYERS;
        let Err(DecodeError::Logic(msg)) = draft(&stream, &hdr, 0, 0, true, None) else {
            panic!("a tile declaring {OVERSIZED_LAYERS} layers of packets must be rejected");
        };
        assert!(msg.contains("declared packets"), "got {msg}");
    }

    /// The synthetic stream with its one tile-part's Psot zeroed.
    fn zero_psot_stream() -> Vec<u8> {
        let mut stream = synth_stream();
        stream[6..10].fill(0);
        stream
    }

    /// A.4.2 lets the last tile-part carry Psot=0, meaning it runs to the EOC.
    #[test]
    fn a_zero_psot_tile_part_runs_to_the_eoc() {
        let hdr = synth_header();
        let baseline = draft(&synth_stream(), &hdr, 0, 0, true, None).expect("baseline must build");
        let plan = draft(&zero_psot_stream(), &hdr, 0, 0, true, None).expect("plan must build");
        assert_eq!(block_offsets(&plan), block_offsets(&baseline));
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
        assert_eq!(passes(&plan), passes(&baseline));
    }

    /// Bytes of a box following jp2c, which a Psot=0 tile-part must not reach.
    const TRAILING_BYTES: usize = 128;

    #[test]
    fn a_zero_psot_tile_part_stops_at_the_codestream_end() {
        let baseline =
            draft(&synth_stream(), &synth_header(), 0, 0, true, None).expect("baseline must build");
        let mut stream = zero_psot_stream();
        let mut hdr = synth_header();
        hdr.codestream_len = stream.len() as u64;
        stream.extend([0x5A; TRAILING_BYTES]);

        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("plan must build");
        assert_eq!(block_offsets(&plan), block_offsets(&baseline));
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
        assert_eq!(passes(&plan), passes(&baseline));

        // SOT (12) + SOD (2) ahead of the packets, EOC (2) behind them
        let packet_bytes = hdr.codestream_len as usize - 16;
        // two packets per layer, each needing at least one bit of the tile's
        // packet bytes: one layer too many unless the trailing bytes count
        hdr.cod.num_layers = (4 * packet_bytes + 4) as u16;
        let Err(DecodeError::Logic(msg)) = draft(&stream, &hdr, 0, 0, true, None) else {
            panic!("the trailing bytes must not pad the tile's packet bytes");
        };
        assert!(msg.contains("declared packets"), "got {msg}");
    }

    // issue775.j2k: the last Psot ends 4 bytes short of the codestream end
    #[test]
    fn the_walk_ends_at_bytes_that_are_not_an_sot() {
        let baseline =
            draft(&synth_stream(), &synth_header(), 0, 0, true, None).expect("baseline must build");
        for trailing in [4usize, TRAILING_BYTES] {
            let mut stream = synth_stream();
            stream.truncate(stream.len() - 2); // drop the EOC
            stream.extend(std::iter::repeat_n(0x5A, trailing));
            let mut hdr = synth_header();
            hdr.codestream_len = stream.len() as u64;
            let plan = draft(&stream, &hdr, 0, 0, true, None)
                .unwrap_or_else(|e| panic!("{trailing} trailing bytes: {e:?}"));
            assert_eq!(block_offsets(&plan), block_offsets(&baseline), "{trailing}");
            assert_eq!(coded_bytes(&plan), coded_bytes(&baseline), "{trailing}");
            assert_eq!(passes(&plan), passes(&baseline), "{trailing}");
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
    /// A 12x12 two-component stream whose second component is subsampled 2x2,
    /// one decomposition level, one layer, LRCP: one packet per (resolution,
    /// component), one code-block per band.
    fn subsampled_header() -> MainHeaderIn {
        let mut siz = siz();
        siz.x_siz = 12;
        siz.y_siz = 12;
        siz.x_o_siz = 0;
        siz.y_o_siz = 0;
        siz.xt_siz = 12;
        siz.yt_siz = 12;
        siz.components = vec![
            SizComponent {
                precision: 8,
                is_signed: false,
                xr_siz: 1,
                yr_siz: 1,
            },
            SizComponent {
                precision: 8,
                is_signed: false,
                xr_siz: 2,
                yr_siz: 2,
            },
        ];
        let mut style = comp_style();
        style.num_levels = 1;
        let mut cod = cod();
        cod.num_layers = 1;
        cod.comps = vec![style; 2];
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
            codestream_off: 0,
            codestream_len: 0,
            first_sot_off: 0,
            tlm: vec![],
        }
    }

    fn subsampled_stream() -> Vec<u8> {
        let packets = vec![
            synth_packet(true, 1),
            synth_packet(true, 1),
            synth_packet(true, 3),
            synth_packet(true, 3),
        ];
        assemble_stream(&packets, &[])
    }

    #[test]
    fn a_subsampled_component_gets_its_own_output_dims() {
        let stream = subsampled_stream();
        let hdr = subsampled_header();
        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("plan must build");
        assert_eq!(plan.comp_dims[0], d(0, 0, 12, 12));
        assert_eq!(plan.comp_dims[1], d(0, 0, 6, 6));
        assert_eq!((plan.width, plan.height), (12, 12));

        // an odd window origin rounds up onto the subsampled component
        let windowed =
            draft(&stream, &hdr, 0, 0, true, Some(d(3, 5, 12, 12))).expect("plan must build");
        assert_eq!(windowed.comp_dims[0], d(3, 5, 12, 12));
        assert_eq!(windowed.comp_dims[1], d(2, 3, 6, 6));
    }

    /// Every block decodes to zero coefficients: this test is about row
    /// counts and widths, which the synthesis produces from geometry alone.
    unsafe fn zero_coder(
        blk: &crate::decode::stripe_decoder::MercuryStripeBlockInfo,
    ) -> Option<Vec<i32>> {
        let stripes = (blk.num_rows + 3) >> 2;
        Some(vec![0i32; ((stripes << 2) * blk.num_cols) as usize])
    }

    #[test]
    fn a_subsampled_decode_emits_each_component_at_its_own_size() {
        use crate::decode::graph::{CompRow, CompRowSink, weave_comps_with_coder};
        use std::sync::{Arc, Mutex};

        let stream = subsampled_stream();
        let hdr = subsampled_header();
        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("plan must build");
        let expected: Vec<(u32, u32)> = plan
            .comp_dims
            .iter()
            .map(|dims| (dims.width(), dims.height()))
            .collect();

        let seen: Arc<Mutex<Vec<Vec<usize>>>> = Arc::new(Mutex::new(vec![Vec::new(), Vec::new()]));
        let sink_seen = Arc::clone(&seen);
        let sink: CompRowSink = Box::new(move |c, row, samples| {
            let width = match samples {
                CompRow::I16(s) | CompRow::Q13(s) => s.len(),
                CompRow::I32(s) => s.len(),
                CompRow::F32(s) => s.len(),
            };
            let mut got = sink_seen.lock().unwrap();
            assert_eq!(
                got[c].len() as u32,
                row,
                "component {c} rows arrive in order"
            );
            got[c].push(width);
        });
        let emitted = weave_comps_with_coder(Arc::new(stream.clone()), plan, 2, sink, zero_coder)
            .expect("decode must run");

        assert_eq!(
            emitted,
            expected.iter().map(|&(_, h)| h as u64).collect::<Vec<_>>()
        );
        let got = seen.lock().unwrap();
        for (c, &(width, height)) in expected.iter().enumerate() {
            assert_eq!(got[c].len() as u32, height, "component {c} row count");
            assert!(
                got[c].iter().all(|&w| w as u32 == width),
                "component {c} row widths: {:?}",
                got[c]
            );
        }
    }

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
                .flat_map(|b| b.blocks())
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
    fn empty_out_of_window_packets_are_parsed_without_plt() {
        let (hdr, mut packets) = tall_rlcp();
        for p in &mut packets[1..] {
            *p = synth_empty_packet();
        }
        let stream = assemble_stream(&packets, &[]);
        let plan = draft(&stream, &hdr, 0, 0, false, Some(d(0, 0, 8, 8)))
            .expect("empty out-of-window packets must not block the plan");
        assert!(plan.tiles[0].in_window);
        assert!(passes(&plan).iter().any(|&n| n > 0));
    }

    #[test]
    fn plt_hops_nonempty_out_of_window_packets() {
        let (hdr, packets) = tall_rlcp();
        let plt = plt_encode(&packet_lengths(&packets));
        let stream = assemble_stream(&packets, &[(0, plt)]);
        let plan = draft(&stream, &hdr, 0, 0, true, Some(d(0, 0, 8, 8)))
            .expect("PLT must hop the out-of-window packets");
        assert!(plan.tiles[0].in_window);
        assert!(passes(&plan).iter().any(|&n| n > 0));
    }

    fn synth_empty_packet() -> Vec<u8> {
        let mut w = BitWriter::new();
        w.put(0);
        w.finish()
    }

    fn tall_rlcp() -> (MainHeaderIn, Vec<Vec<u8>>) {
        let mut hdr = synth_header();
        hdr.siz.x_siz = 16;
        hdr.siz.y_siz = 64;
        hdr.siz.xt_siz = 16;
        hdr.siz.yt_siz = 64;
        hdr.cod.order = ProgressionOrder::Rlcp;
        hdr.cod.num_layers = 1;
        hdr.cod.comps[0].block_width = 4;
        hdr.cod.comps[0].block_height = 4;
        hdr.cod.comps[0].precincts = vec![
            crate::codec::params::PrecinctSize {
                width: 4,
                height: 4,
            },
            crate::codec::params::PrecinctSize {
                width: 8,
                height: 8,
            },
        ];
        let mut packets = Vec::new();
        for _ in 0..16 {
            packets.push(synth_packet(true, 1));
        }
        for _ in 0..16 {
            packets.push(synth_packet(true, 3));
        }
        (hdr, packets)
    }

    /// A precinct smaller than the nominal code-block clamps the effective
    /// block (B.7); the 4x4 bands of the synthetic stream still hold one block
    /// each, so the clamped plan parses the same packets as the nominal one.
    #[test]
    fn a_precinct_smaller_than_the_block_is_planned() {
        let stream = synth_stream();
        let mut hdr = synth_header();
        hdr.cod.comps[0].precincts = vec![
            crate::codec::params::PrecinctSize {
                width: 4,
                height: 4,
            },
            crate::codec::params::PrecinctSize {
                width: 8,
                height: 8,
            },
        ];
        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("clamped blocks must plan");
        let baseline =
            draft(&stream, &synth_header(), 0, 0, true, None).expect("baseline must build");
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
        let spans: Vec<(u32, u32)> = plan.tiles[0].comps[0]
            .iter()
            .flat_map(|r| r.bands.iter())
            .map(|b| (b.block_w, b.block_h))
            .collect();
        assert_eq!(spans, vec![(4, 4); 4]);
    }

    /// Two components whose COC gives them different level counts and block
    /// sizes: component 1 carries one more decomposition level and 4x4 blocks.
    /// In LRCP the extra resolution contributes its packets after both
    /// components' shared ones, and each component's band plans follow its own
    /// coding style.
    #[test]
    fn a_component_with_its_own_levels_and_blocks_is_planned() {
        let mut hdr = synth_header();
        hdr.siz.components.push(SizComponent {
            precision: 8,
            is_signed: false,
            xr_siz: 1,
            yr_siz: 1,
        });
        let mut second = hdr.cod.comps[0].clone();
        second.num_levels = 2;
        second.block_width = 4;
        second.block_height = 4;
        hdr.cod.comps.push(second);
        hdr.qcd.ranges = vec![8; 7];

        // LRCP: layer, then resolution, then component. Component 0 stops at
        // resolution 1, so resolution 2 is component 1 alone.
        let mut packets = Vec::new();
        for layer in 0..LAYERS {
            let first = layer == 0;
            packets.push(synth_packet(first, 1)); // res 0, comp 0: LL
            packets.push(synth_packet(first, 1)); // res 0, comp 1: LL
            packets.push(synth_packet(first, 3)); // res 1, comp 0
            packets.push(synth_packet(first, 3)); // res 1, comp 1
            packets.push(synth_packet(first, 3)); // res 2, comp 1
        }
        let stream = assemble_stream(&packets, &[]);
        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("plan must build");

        const BLOCKS: usize = 11; // comp 0: 1 + 3, comp 1: 1 + 3 + 3
        assert_eq!(
            coded_bytes(&plan),
            BLOCKS * LAYERS as usize * BODY_LEN as usize
        );
        assert_eq!(passes(&plan), vec![LAYERS as u8; BLOCKS]);

        let comps = &plan.tiles[0].comps;
        assert_eq!(comps[0].len(), 2);
        assert_eq!(comps[1].len(), 3);
        let spans = |c: usize| -> Vec<(u32, u32)> {
            comps[c]
                .iter()
                .flat_map(|r| r.bands.iter())
                .map(|b| (b.block_w, b.block_h))
                .collect()
        };
        assert_eq!(spans(0), vec![(64, 64); 4]);
        assert_eq!(spans(1), vec![(4, 4); 7]);
    }

    /// The sample path is chosen once for the image, so a 5/3 component beside
    /// a 9/7 one has to be rejected before the graph is built.
    #[test]
    fn mixed_wavelet_kernels_are_rejected() {
        let mut hdr = synth_header();
        hdr.siz.components.push(SizComponent {
            precision: 8,
            is_signed: false,
            xr_siz: 1,
            yr_siz: 1,
        });
        let mut second = hdr.cod.comps[0].clone();
        second.reversible = false;
        hdr.cod.comps.push(second);
        let Err(DecodeError::Logic(msg)) = draft(&synth_stream(), &hdr, 0, 0, true, None) else {
            panic!("mixed kernels must be rejected");
        };
        assert!(msg.contains("mixed wavelet kernels"), "got {msg}");
    }

    /// A tile-part POC reorders that tile's packets, which the single
    /// progression the plan takes from the COD cannot follow.
    #[test]
    fn a_tile_part_poc_is_rejected() {
        let stream = synth_stream();
        const POC: [u8; 11] = [0xFF, 0x5F, 0x00, 0x09, 0, 0, 0, 1, 1, 1, 0];
        let mut spliced = stream[..12].to_vec();
        spliced.extend(POC);
        spliced.extend(&stream[12..]);
        let psot =
            u32::from_be_bytes([spliced[6], spliced[7], spliced[8], spliced[9]]) + POC.len() as u32;
        spliced[6..10].copy_from_slice(&psot.to_be_bytes());
        let Err(DecodeError::Logic(msg)) = draft(&spliced, &synth_header(), 0, 0, true, None)
        else {
            panic!("a tile-part POC must be rejected");
        };
        assert!(msg.contains("0xff5f"), "got {msg}");
    }

    /// Two components, two resolutions, three layers, one precinct each: a POC
    /// whose first volume (resolution 0, layers 0..2, RLCP) is wholly inside
    /// its second (everything, CPRL). The stream is laid out in the order the
    /// two volumes produce, so a schedule that repeats the four shared packets
    /// or orders them differently reads the wrong bytes into the wrong block.
    #[test]
    fn two_overlapping_poc_volumes_schedule_each_packet_once() {
        let mut hdr = synth_header();
        hdr.siz.components.push(SizComponent {
            precision: 8,
            is_signed: false,
            xr_siz: 1,
            yr_siz: 1,
        });
        hdr.cod.comps.push(hdr.cod.comps[0].clone());
        hdr.cod.pocs = vec![
            ProgressionVolume {
                res_s: 0,
                comp_s: 0,
                lay_e: 2,
                res_e: 1,
                comp_e: 2,
                order: ProgressionOrder::Rlcp,
            },
            ProgressionVolume {
                res_s: 0,
                comp_s: 0,
                lay_e: LAYERS,
                res_e: 2,
                comp_e: 2,
                order: ProgressionOrder::Cprl,
            },
        ];

        // volume 0 (RLCP) then volume 1 (CPRL) minus what volume 0 emitted
        let packets = vec![
            synth_packet(true, 1),  // 0: comp 0, res 0, layer 0
            synth_packet(true, 1),  // 1: comp 1, res 0, layer 0
            synth_packet(false, 1), // 2: comp 0, res 0, layer 1
            synth_packet(false, 1), // 3: comp 1, res 0, layer 1
            synth_packet(false, 1), // 4: comp 0, res 0, layer 2
            synth_packet(true, 3),  // 5: comp 0, res 1, layer 0
            synth_packet(false, 3), // 6: comp 0, res 1, layer 1
            synth_packet(false, 3), // 7: comp 0, res 1, layer 2
            synth_packet(false, 1), // 8: comp 1, res 0, layer 2
            synth_packet(true, 3),  // 9: comp 1, res 1, layer 0
            synth_packet(false, 3), // 10: comp 1, res 1, layer 1
            synth_packet(false, 3), // 11: comp 1, res 1, layer 2
        ];
        let stream = assemble_stream(&packets, &[]);
        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("POC plan must build");

        const BLOCKS: usize = 8; // two components of LL + HL + LH + HH
        assert_eq!(
            coded_bytes(&plan),
            BLOCKS * LAYERS as usize * BODY_LEN as usize
        );
        assert_eq!(passes(&plan), vec![LAYERS as u8; BLOCKS]);

        // SOT (12) + SOD (2) precede the first packet
        let starts: Vec<u64> = packets
            .iter()
            .scan(14u64, |off, p| {
                let start = *off;
                *off += p.len() as u64;
                Some(start)
            })
            .collect();
        let body = |packet: usize, header_len: u64| starts[packet] + header_len;
        let (res0_header, res1_header) = (1u64, 3u64);
        let step = BODY_LEN as u64;
        assert_eq!(
            block_offsets(&plan),
            vec![
                body(0, res0_header),
                body(5, res1_header),
                body(5, res1_header) + step,
                body(5, res1_header) + 2 * step,
                body(1, res0_header),
                body(9, res1_header),
                body(9, res1_header) + step,
                body(9, res1_header) + 2 * step,
            ]
        );
    }

    /// A volume reaching past the stream's resolution, component and layer
    /// counts is clamped to them, the way classic's finalizePocs does, so it
    /// just replaces the COD order (the p0_03 / p0_15 shape).
    #[test]
    fn an_oversized_poc_volume_clamps_to_the_stream() {
        let mut hdr = synth_header();
        hdr.cod.order = ProgressionOrder::Pcrl;
        hdr.cod.pocs = vec![ProgressionVolume {
            res_s: 0,
            comp_s: 0,
            lay_e: 8,
            res_e: 33,
            comp_e: 255,
            order: ProgressionOrder::Lrcp,
        }];
        let stream = synth_stream();
        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("clamped POC must plan");
        let mut lrcp = synth_header();
        lrcp.cod.order = ProgressionOrder::Lrcp;
        let baseline = draft(&stream, &lrcp, 0, 0, true, None).expect("baseline must build");
        assert_eq!(block_offsets(&plan), block_offsets(&baseline));
        assert_eq!(passes(&plan), passes(&baseline));
    }

    #[test]
    fn a_poc_volume_covering_no_packet_is_rejected() {
        let empty_volumes = [
            ProgressionVolume {
                res_s: 0,
                comp_s: 0,
                lay_e: 0,
                res_e: 2,
                comp_e: 1,
                order: ProgressionOrder::Lrcp,
            },
            ProgressionVolume {
                res_s: 0,
                comp_s: 1,
                lay_e: LAYERS,
                res_e: 2,
                comp_e: 1,
                order: ProgressionOrder::Lrcp,
            },
        ];
        for vol in empty_volumes {
            let mut hdr = synth_header();
            hdr.cod.pocs = vec![vol];
            let Err(DecodeError::Logic(msg)) = draft(&synth_stream(), &hdr, 0, 0, true, None)
            else {
                panic!("an empty POC volume must be rejected");
            };
            assert!(msg.contains("POC"), "got {msg}");
        }
    }

    /// An RGN in a tile-part header raises that tile's magnitude bit planes,
    /// which the host's main-header ROI check never sees.
    #[test]
    fn a_tile_part_rgn_is_rejected() {
        let stream = synth_stream();
        const RGN: [u8; 7] = [0xFF, 0x5E, 0x00, 0x05, 0, 0, 7];
        let mut spliced = stream[..12].to_vec();
        spliced.extend(RGN);
        spliced.extend(&stream[12..]);
        let psot =
            u32::from_be_bytes([spliced[6], spliced[7], spliced[8], spliced[9]]) + RGN.len() as u32;
        spliced[6..10].copy_from_slice(&psot.to_be_bytes());
        let Err(DecodeError::Logic(msg)) = draft(&spliced, &synth_header(), 0, 0, true, None)
        else {
            panic!("a tile-part RGN must be rejected");
        };
        assert!(msg.contains("0xff5e"), "got {msg}");
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
    /// One tile-part of tile 0: `markers` in its header, then `packets`.
    fn tile_part(tpsot: u8, num_parts: u8, markers: &[Vec<u8>], packets: &[Vec<u8>]) -> Vec<u8> {
        let body: usize = packets.iter().map(|p| p.len()).sum();
        let marker_bytes: usize = markers.iter().map(|m| m.len()).sum();
        const TILE_PART_HEADER: usize = 14; // SOT (12) + SOD (2)
        let psot = (TILE_PART_HEADER + marker_bytes + body) as u32;
        let mut out = vec![0xFF, 0x90, 0x00, 0x0A, 0x00, 0x00];
        out.extend(psot.to_be_bytes());
        out.extend([tpsot, num_parts]);
        for m in markers {
            out.extend(m);
        }
        out.extend([0xFF, 0x93]); // SOD
        for p in packets {
            out.extend(p);
        }
        out
    }

    /// The synthetic stream in one tile-part whose header carries `markers`.
    fn marked_stream(markers: &[Vec<u8>], packets: &[Vec<u8>]) -> Vec<u8> {
        let mut out = tile_part(0, 1, markers, packets);
        out.extend([0xFF, 0xD9]); // EOC
        out
    }

    /// A COD segment: LRCP, `layers` layers, no MCT, `levels` decomposition
    /// levels, 2^(`block_exponent` + 2) code-blocks, reversible.
    fn cod_segment(layers: u16, levels: u8, block_exponent: u8) -> Vec<u8> {
        let mut seg = vec![0xFF, 0x52, 0x00, 0x0C, 0x00, 0x00];
        seg.extend(layers.to_be_bytes());
        seg.extend([0x00, levels, block_exponent, block_exponent, 0x00, 0x01]);
        seg
    }

    /// A COC segment carrying the same SPcod fields as `cod_segment`.
    fn coc_segment(comp: u8, levels: u8, block_exponent: u8) -> Vec<u8> {
        vec![
            0xFF,
            0x53,
            0x00,
            0x09,
            comp,
            0x00,
            levels,
            block_exponent,
            block_exponent,
            0x00,
            0x01,
        ]
    }

    /// A reversible quantization segment: `guard_bits` guard bits and one
    /// `exponent` per band.
    fn quant_segment(code: u8, comp: Option<u8>, guard_bits: u8, exponent: u8) -> Vec<u8> {
        const BANDS: usize = 4;
        let comp_bytes = comp.iter().count();
        let mut seg = vec![0xFF, code];
        seg.extend(((3 + comp_bytes + BANDS) as u16).to_be_bytes());
        seg.extend(comp);
        seg.push(guard_bits << 5);
        seg.extend(std::iter::repeat_n(exponent << 3, BANDS));
        seg
    }

    fn k_max_primes(plan: &DecodePlan, comp: usize) -> Vec<i32> {
        plan.tiles[0].comps[comp]
            .iter()
            .flat_map(|r| r.bands.iter())
            .map(|b| b.k_max_prime)
            .collect()
    }

    /// A tile-part COD replaces the tile's coding style: the plan takes its
    /// block size and stops at its layer count.
    #[test]
    fn a_tile_part_cod_replaces_the_tile_coding_style() {
        const TILE_LAYERS: u16 = 2;
        let hdr = synth_header();
        let stream = marked_stream(&[cod_segment(TILE_LAYERS, 1, 0)], &synth_packets());
        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("plan must build");
        assert_eq!(plan.tiles[0].cod.num_layers, TILE_LAYERS);
        assert_eq!(plan.tiles[0].cod.comps[0].block_width, 4);
        assert_eq!(plan.tiles[0].cod.comps[0].block_height, 4);
        let spans: Vec<(u32, u32)> = plan.tiles[0].comps[0]
            .iter()
            .flat_map(|r| r.bands.iter())
            .map(|b| (b.block_w, b.block_h))
            .collect();
        assert_eq!(spans, vec![(4, 4); 4]);

        // the 4x4 bands hold one code-block either way, so the packets are the
        // main header's stream cut to the tile's layer count
        let baseline =
            draft(&synth_stream(), &hdr, 0, TILE_LAYERS, true, None).expect("baseline must build");
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
        assert_eq!(passes(&plan), passes(&baseline));
    }

    /// Classic caps every tile at the main header's layer count, so a tile-part
    /// COD asking for more decodes the main header's.
    #[test]
    fn a_tile_part_cod_cannot_raise_the_layer_count() {
        let hdr = synth_header();
        let stream = marked_stream(&[cod_segment(LAYERS + 4, 1, 4)], &synth_packets());
        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("plan must build");
        assert_eq!(plan.tiles[0].cod.num_layers, LAYERS);
        let baseline = draft(&synth_stream(), &hdr, 0, 0, true, None).expect("baseline must build");
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
        assert_eq!(passes(&plan), passes(&baseline));
    }

    /// A tile-part QCD replaces every component's quantization, which the band
    /// plans carry as guard bits plus ranging exponent.
    #[test]
    fn a_tile_part_qcd_replaces_the_tile_quantization() {
        let hdr = synth_header();
        let stream = marked_stream(&[quant_segment(0x5C, None, 3, 10)], &synth_packets());
        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("plan must build");
        assert_eq!(k_max_primes(&plan, 0), vec![3 + 10 - 1; 4]);

        let baseline = draft(&synth_stream(), &hdr, 0, 0, true, None).expect("baseline must build");
        assert_eq!(k_max_primes(&baseline, 0), vec![2 + 8 - 1; 4]);
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
    }

    /// Two components, one decomposition level each. The packets follow LRCP:
    /// layer, then resolution, then component.
    fn two_comp_stream() -> (MainHeaderIn, Vec<Vec<u8>>) {
        let mut hdr = synth_header();
        hdr.siz.components.push(SizComponent {
            precision: 8,
            is_signed: false,
            xr_siz: 1,
            yr_siz: 1,
        });
        hdr.cod.comps.push(hdr.cod.comps[0].clone());
        hdr.qcd.ranges = vec![8; 7];
        let mut packets = Vec::new();
        for layer in 0..LAYERS {
            let first = layer == 0;
            packets.push(synth_packet(first, 1)); // res 0, comp 0
            packets.push(synth_packet(first, 1)); // res 0, comp 1
            packets.push(synth_packet(first, 3)); // res 1, comp 0
            packets.push(synth_packet(first, 3)); // res 1, comp 1
        }
        (hdr, packets)
    }

    /// A tile-part COC covers one component only: component 1 takes a second
    /// decomposition level and 4x4 blocks while component 0 keeps the main
    /// header's style.
    #[test]
    fn a_tile_part_coc_replaces_one_component() {
        let (hdr, mut packets) = two_comp_stream();
        // component 1's extra resolution trails both components' shared packets
        for layer in 0..LAYERS {
            packets.insert(
                4 * layer as usize + 4 + layer as usize,
                synth_packet(layer == 0, 3),
            );
        }
        let stream = marked_stream(&[coc_segment(1, 2, 0)], &packets);
        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("plan must build");

        assert_eq!(plan.tiles[0].cod.comps[0].num_levels, 1);
        assert_eq!(plan.tiles[0].cod.comps[0].block_width, 64);
        assert_eq!(plan.tiles[0].cod.comps[1].num_levels, 2);
        assert_eq!(plan.tiles[0].cod.comps[1].block_width, 4);
        assert_eq!(plan.tiles[0].comps[0].len(), 2);
        assert_eq!(plan.tiles[0].comps[1].len(), 3);

        const BLOCKS: usize = 11; // comp 0: 1 + 3, comp 1: 1 + 3 + 3
        assert_eq!(
            coded_bytes(&plan),
            BLOCKS * LAYERS as usize * BODY_LEN as usize
        );
        assert_eq!(passes(&plan), vec![LAYERS as u8; BLOCKS]);
    }

    /// A tile-part QCC outranks the tile-part QCD of the same header for its
    /// own component (T.800 A.6.5).
    #[test]
    fn a_tile_part_qcc_outranks_the_tile_part_qcd() {
        let (hdr, packets) = two_comp_stream();
        let markers = [
            quant_segment(0x5C, None, 3, 10),
            quant_segment(0x5D, Some(1), 1, 5),
        ];
        let stream = marked_stream(&markers, &packets);
        let plan = draft(&stream, &hdr, 0, 0, true, None).expect("plan must build");
        assert_eq!(k_max_primes(&plan, 0), vec![3 + 10 - 1; 4]);
        assert_eq!(k_max_primes(&plan, 1), vec![1 + 5 - 1; 4]);
    }

    /// A tile's coding style has to be known before its first packet, so only
    /// the tile-part that opens the tile may carry it.
    #[test]
    fn a_coding_style_marker_in_a_later_tile_part_is_rejected() {
        let packets = synth_packets();
        let (first, second) = packets.split_at(2);
        let mut stream = tile_part(0, 2, &[], first);
        stream.extend(tile_part(1, 2, &[cod_segment(2, 1, 0)], second));
        stream.extend([0xFF, 0xD9]);
        let Err(DecodeError::Logic(msg)) = draft(&stream, &synth_header(), 0, 0, true, None) else {
            panic!("a later tile-part must not carry a COD");
        };
        assert!(msg.contains("outside the first tile-part"), "got {msg}");
    }

    /// A tile-part COD changing the wavelet would move the sample path the
    /// whole image already chose.
    #[test]
    fn a_tile_part_cod_changing_the_kernel_is_rejected() {
        let mut irreversible = cod_segment(LAYERS, 1, 4);
        *irreversible.last_mut().unwrap() = 0x00;
        let stream = marked_stream(&[irreversible], &synth_packets());
        let Err(DecodeError::Logic(msg)) = draft(&stream, &synth_header(), 0, 0, true, None) else {
            panic!("a tile-part kernel change must be rejected");
        };
        assert!(msg.contains("changes the wavelet kernel"), "got {msg}");
    }
}
