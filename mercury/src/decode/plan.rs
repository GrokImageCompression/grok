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
use crate::codec::tile_geom::{TileGeom, chart_tile_geom};
use crate::decode::DecodeError;

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
    pub blocks_high: u32,
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
    /// [component][resolution].
    pub comps: Vec<Vec<ResPlan>>,
    pub geom: TileGeom,
}

pub struct DecodePlan {
    pub width: u32,
    pub height: u32,
    /// Tiles in raster order (index = ty * tiles_across + tx).
    pub tiles: Vec<TilePlan>,
    pub cod: CodParams,
    pub siz: SizParams,
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
        let in_window = off >= self.buf_off && off + want as u64 <= self.buf_off + self.valid as u64;
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

pub fn draft(file: &dyn ReadAt, hdr: &MainHeaderIn) -> Result<DecodePlan, DecodeError> {
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
        let supported = CodingModes::RESET
            | CodingModes::CAUSAL
            | CodingModes::ERTERM
            | CodingModes::SEGMARK;
        let m = hdr.cod.modes.0;
        if m & !supported != 0 {
            return Err(DecodeError::Logic(format!(
                "plan: unsupported code-block modes (Cmodes {m:#x})"
            )));
        }
    }

    let num_tiles = (hdr.siz.tiles_across() * hdr.siz.tiles_down()) as usize;
    let num_comps = hdr.siz.comp_count();
    let n_res = (hdr.cod.num_levels + 1) as usize;
    let d = hdr.cod.num_levels as u32;

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

    // --- walk the SOT chain: collect each tile's packet-data segments ---
    let mut tile_segs: Vec<Vec<(u64, u64)>> = vec![Vec::new(); num_tiles];
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
            return Err(DecodeError::Logic(format!("plan: SOT tile index {isot} out of range")));
        }
        if psot == 0 {
            return Err(DecodeError::Logic("plan: Psot=0 not supported".into()));
        }
        // Scan tile-part header markers until SOD.
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
            mp += 2 + l;
            if mp + 4 > sot_pos + psot {
                return Err(DecodeError::Logic("SOD not found in tile-part".into()));
            }
        }
        tile_segs[isot].push((mp, sot_pos + psot - mp));
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

    // --- per-tile plans ---
    let mut win = StreamWin::warp(file).map_err(io_snag)?;
    let mut tiles: Vec<TilePlan> = Vec::with_capacity(num_tiles);
    for t in 0..num_tiles {
        let geom = chart_tile_geom(&hdr.siz, &hdr.cod, t as u16);
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
        )?;
        tiles.push(TilePlan { comps, geom });
    }

    Ok(DecodePlan {
        width: hdr.siz.width(),
        height: hdr.siz.height(),
        tiles,
        cod: hdr.cod.clone(),
        siz: hdr.siz.clone(),
    })
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
                        blocks_high: sb.blocks_high,
                        k_max_prime: q.guard_bits as i32 + epsilon_b - 1,
                        delta: 0.0,
                        blocks: vec![
                            BlockRec::default();
                            (sb.blocks_wide * sb.blocks_high) as usize
                        ],
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
                steps.get(i).copied().ok_or_else(|| {
                    DecodeError::Logic("QCD: missing irreversible step".into())
                })
            };
            let mut norm = 1.0f32;
            for l in (0..n_res - 1).rev() {
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
    // Position (for RPCL/PCRL/CPRL) is the precinct's projection onto the
    // reference grid: canvas coords at res r, scaled by 2^(D-r) and the
    // component subsampling. The first precinct row/col clamps to the
    // resolution origin (Annex B.12: "y = ty0" case).
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
                    let ypos = (py as u64 * ps.height as u64).max(res.dims.y0 as u64) * scale * dy;
                    let xpos = (px as u64 * ps.width as u64).max(res.dims.x0 as u64) * scale * dx;
                    let prec = (py - py0) * npx + (px - px0);
                    for l in 0..cod.num_layers {
                        let (cu, ru, lu) = (c as u64, r as u64, l as u64);
                        let key = match order {
                            ProgressionOrder::Lrcp => [lu, ru, cu, py as u64, px as u64],
                            ProgressionOrder::Rlcp => [ru, lu, cu, py as u64, px as u64],
                            ProgressionOrder::Rpcl => [ru, ypos, xpos, cu, lu],
                            ProgressionOrder::Pcrl => [ypos, xpos, cu, ru, lu],
                            ProgressionOrder::Cprl => [cu, ypos, xpos, ru, lu],
                        };
                        pkts.push((key, Pkt {
                            comp: c as u16,
                            res: r as u8,
                            layer: l,
                            prec,
                        }));
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

    let mut vpos: u64 = 0;
    for (_, pkt) in &pkts {
        let (c, r) = (pkt.comp as usize, pkt.res as usize);
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
                    return PrecBand { bx0: 0, by0: 0, nbw: 0, nbh: 0 };
                }
                let first_bx = sb.dims.x0 / block_w;
                let first_by = sb.dims.y0 / block_h;
                let bx0 = rx0 / block_w - first_bx;
                let by0 = ry0 / block_h - first_by;
                let bx1 = (rx1 - 1) / block_w - first_bx;
                let by1 = (ry1 - 1) / block_h - first_by;
                PrecBand { bx0, by0, nbw: bx1 - bx0 + 1, nbh: by1 - by0 + 1 }
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
        let slice = win.strand_at(real_pos, seg_avail as usize).map_err(io_snag)?;
        let mut reader = PacketBitReader::warp(slice);
        let parsed =
            comb_packet_header(&mut reader, &mut state.trees, &mut state.states, pkt.layer, 0)
                .map_err(|e| {
                    DecodeError::Logic(format!("packet parse at vpos {vpos}: {e:?}"))
                })?;
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
        for (band_idx, pb) in prec_bands.iter().enumerate() {
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
                        rec.seg_lens = Some(
                            contrib.segment_lengths[..contrib.num_segments as usize].into(),
                        );
                    }
                } else {
                    // Later layer: append bytes, accumulate passes.
                    if rec.num_segments > 1 || contrib.num_segments > 1 {
                        return Err(DecodeError::Logic(
                            "plan: multi-segment multi-layer block not supported".into(),
                        ));
                    }
                    rec.num_passes = rec.num_passes.saturating_add(contrib.new_passes);
                    rec.extra
                        .get_or_insert_with(Default::default)
                        .push(Chunk { file_off: body, len: total });
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
    }

    Ok(comps)
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
        QuantStyle::Derived => {
            Err(DecodeError::Logic("plan: derived quantization not wired yet".into()))
        }
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
