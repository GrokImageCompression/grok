//! Full-image T2 parse plan: stream the codestream's packet headers once and
//! build, per tile and subband, a table of each decoded code-block's compressed
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

use std::ops::Range;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex, OnceLock, mpsc};

use crate::decode::ReadAt;
use crate::decode::read_at::{
    FETCH_GAP_BYTES, FetchedRun, FetchedRuns, FillCursor, FilledRuns, fetch_runs, merge_ranges,
};
use crate::weft::rt::{GraphHold, HostedRuntime, NodeId, StillnessFn};

use crate::codec::packet::{
    BandBlocks, BlockContribution, HeaderBytes, PacketBitReader, PacketError, PacketParseResult,
    TagTree, comb_packet_header,
};
use crate::codec::packet_order::{
    PrecBand, chart_prec_bands, chart_precinct_grids, chart_precinct_positions, order_packets,
};
use crate::codec::params::{CodParams, QcdParams, SizParams, step_ranging};
use crate::codec::tile_geom::{Dims, TileGeom, chart_tile_geom};
use crate::decode::DecodeError;
use crate::decode::window::comp_window;
use crate::decode::window::{
    TileCompWindow, chart_tile_comp_window, intersect, intersects, resolution_band_dims,
};

/// One tile-part's position from the host's parsed TLM markers.
pub struct TlmEntry {
    pub tile: u16,
    /// Absolute file offset of the tile-part's SOT marker.
    pub offset: u64,
    /// Tile-part length (Psot).
    pub length: u64,
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

impl MainHeaderIn {
    // QCD unless a QCC overrides it
    pub fn component_quant(&self) -> Vec<QcdParams> {
        (0..self.siz.comp_count())
            .map(|c| {
                self.qcc
                    .iter()
                    .find(|(idx, _)| *idx == c)
                    .map(|(_, q)| q.clone())
                    .unwrap_or_else(|| self.qcd.clone())
            })
            .collect()
    }
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

fn append_segment_lengths(
    block: &mut BlockRec,
    contribution: &BlockContribution,
) -> Result<(), DecodeError> {
    let previous_bytes = u32::try_from(block.bolt_len())
        .map_err(|_| DecodeError::Logic("plan: code-block length exceeds u32".into()))?;
    let mut lengths = block
        .seg_lens
        .take()
        .map_or_else(|| vec![previous_bytes], |lengths| lengths.into_vec());
    let mut new_lengths = contribution.segment_lengths();
    if contribution.first_segment_continues {
        let previous = lengths.last_mut().ok_or_else(|| {
            DecodeError::Logic("plan: continued code-block segment is missing".into())
        })?;
        let added = new_lengths.next().ok_or_else(|| {
            DecodeError::Logic("plan: continued code-block segment is empty".into())
        })?;
        *previous = previous
            .checked_add(*added)
            .ok_or_else(|| DecodeError::Logic("plan: code-block segment length overflow".into()))?;
    }
    lengths.extend(new_lengths.copied());
    block.num_segments = u8::try_from(lengths.len())
        .map_err(|_| DecodeError::Logic("plan: too many code-block segments".into()))?;
    block.seg_lens = (lengths.len() > 1).then(|| lengths.into_boxed_slice());
    Ok(())
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
    /// keeps only the blocks its padded band window needs, so the origin is not
    /// the band's first block.
    stored_block_x0: u32,
    stored_block_y0: u32,
    stored_blocks_wide: u32,
    stored_blocks_high: u32,
    /// Blocks of the stored rectangle in raster order.
    blocks: Vec<BlockRec>,
    published: Option<Arc<PublishedRows>>,
}

pub struct PublishedRows {
    rows: Box<[OnceLock<Box<[BlockRec]>>]>,
    nodes: OnceLock<Box<[NodeId]>>,
}

impl PublishedRows {
    pub fn register_nodes(&self, nodes: Box<[NodeId]>) {
        if self.nodes.set(nodes).is_err() {
            panic!("a band registers its decode nodes once");
        }
    }
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

    fn stored_block_mut(&mut self, block_x: u32, block_y: u32) -> Option<&mut BlockRec> {
        let x = block_x.checked_sub(self.stored_block_x0)?;
        let y = block_y.checked_sub(self.stored_block_y0)?;
        if x >= self.stored_blocks_wide || y >= self.stored_blocks_high {
            return None;
        }
        let i = y as usize * self.stored_blocks_wide as usize + x as usize;
        self.blocks.get_mut(i)
    }

    /// The `count` blocks from band-global block column `block_x` of block row
    /// `block_y`.
    pub fn block_row(&self, block_y: u32, block_x: u32, count: u32) -> &[BlockRec] {
        if let Some(published) = &self.published {
            let row = published.rows[(block_y - self.stored_block_y0) as usize]
                .get()
                .expect("a block row is decoded only after the feed publishes it");
            let x = (block_x - self.stored_block_x0) as usize;
            return &row[x..x + count as usize];
        }
        let first = self.stored_index(block_x, block_y);
        let last = self.stored_index(block_x + count - 1, block_y);
        &self.blocks[first..last + 1]
    }

    pub fn block_row_published(&self, block_y: u32) -> bool {
        self.published.as_ref().is_none_or(|published| {
            published.rows[(block_y - self.stored_block_y0) as usize]
                .get()
                .is_some()
        })
    }

    pub fn published_rows(&self) -> Option<&PublishedRows> {
        self.published.as_deref()
    }

    fn stored_row_bytes(&self, stored_row: usize) -> u64 {
        let wide = self.stored_blocks_wide as usize;
        self.blocks[stored_row * wide..(stored_row + 1) * wide]
            .iter()
            .map(|rec| rec.bolt_len() as u64)
            .sum()
    }

    fn publish_row(&mut self, stored_row: usize) {
        let wide = self.stored_blocks_wide as usize;
        let row = self.blocks[stored_row * wide..(stored_row + 1) * wide]
            .iter_mut()
            .map(std::mem::take)
            .collect();
        let published = self
            .published
            .as_ref()
            .expect("only a fed band publishes rows");
        if published.rows[stored_row].set(row).is_err() {
            panic!("a block row is published once");
        }
    }

    fn shell_for_graph(&mut self) -> BandPlan {
        let published = Arc::new(PublishedRows {
            rows: (0..self.stored_blocks_high)
                .map(|_| OnceLock::new())
                .collect(),
            nodes: OnceLock::new(),
        });
        self.published = Some(Arc::clone(&published));
        BandPlan {
            band_type: self.band_type,
            x0: self.x0,
            y0: self.y0,
            width: self.width,
            height: self.height,
            block_w: self.block_w,
            block_h: self.block_h,
            k_max_prime: self.k_max_prime,
            delta: self.delta,
            stored_block_x0: self.stored_block_x0,
            stored_block_y0: self.stored_block_y0,
            stored_blocks_wide: self.stored_blocks_wide,
            stored_blocks_high: self.stored_blocks_high,
            blocks: Vec::new(),
            published: Some(published),
        }
    }

    fn stored_rows_of(&self, prec_band: &PrecBand) -> Range<usize> {
        let top = prec_band.by0.max(self.stored_block_y0);
        let bottom =
            (prec_band.by0 + prec_band.nbh).min(self.stored_block_y0 + self.stored_blocks_high);
        if top >= bottom {
            return 0..0;
        }
        (top - self.stored_block_y0) as usize..(bottom - self.stored_block_y0) as usize
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
    /// [component][resolution].
    pub comps: Vec<Vec<ResPlan>>,
    /// This tile's coding style: the main header's, with its tile-part
    /// header's COD and COC applied.
    pub cod: Arc<CodParams>,
    pub geom: TileGeom,
    /// Per-component padded decode windows; None on a whole-tile decode.
    pub win: Option<Vec<TileCompWindow>>,
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
    /// Tiles in raster order (index = ty * tiles_across + tx). None when the
    /// decode window misses the tile: nothing of it was parsed.
    pub tiles: Vec<Option<TilePlan>>,
    pub cod: CodParams,
    pub siz: SizParams,
    /// Resolutions skipped from the top (0 = full resolution).
    pub reduce: u8,
    /// Irreversible 9/7 runs on int16 Q13 fixed-point samples instead of f32
    /// (see dwt::fixed97). Band deltas are then one-level gains, not the
    /// accumulated normalization.
    pub q13: bool,
    pub slice_blocks: u32,
    /// Most column strips one tile component's synthesis is split into.
    pub max_strips: u32,
    pub int16_dwt: bool,
    pub feed_link: Option<Arc<FeedLink>>,
}

impl DecodePlan {
    pub fn coded_runs(&self, max_gap: u64) -> Vec<Range<u64>> {
        let mut ranges = Vec::new();
        for tile in self.tiles.iter().flatten() {
            for band in tile.comps.iter().flatten().flat_map(|res| &res.bands) {
                for rec in &band.blocks {
                    ranges.push(rec.file_off..rec.file_off + rec.len as u64);
                    for chunk in rec.extra.iter().flat_map(|chunks| chunks.iter()) {
                        ranges.push(chunk.file_off..chunk.file_off + chunk.len as u64);
                    }
                }
            }
        }
        merge_ranges(ranges, max_gap)
    }
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
    header_bytes_since_refill: usize,
}

const WIN_SIZE: usize = 4 << 20;
const MIN_WINDOW_READ: usize = 64 << 10;

impl<'f> StreamWin<'f> {
    fn warp(file: &'f dyn ReadAt) -> std::io::Result<Self> {
        let file_len = file.extent()?;
        Ok(StreamWin {
            file,
            file_len,
            buf: Vec::new(),
            buf_off: 0,
            valid: 0,
            header_bytes_since_refill: 0,
        })
    }

    fn byte_at(&mut self, off: u64) -> std::io::Result<u8> {
        if off >= self.file_len {
            return Err(std::io::Error::new(
                std::io::ErrorKind::UnexpectedEof,
                "packet header read past end of file",
            ));
        }
        let covered = off >= self.buf_off && off < self.buf_off + self.valid as u64;
        if !covered {
            let read_len = (self.header_bytes_since_refill * 2).clamp(MIN_WINDOW_READ, WIN_SIZE);
            let mut n = read_len.min((self.file_len - off) as usize);
            if let Some(run_end) = self.file.fetched_run_end(off) {
                n = n.min((run_end - off) as usize);
            }
            if self.buf.len() < n {
                self.buf.resize(n, 0);
            }
            self.file.draw_at(&mut self.buf[..n], off)?;
            self.buf_off = off;
            self.valid = n;
            self.header_bytes_since_refill = 0;
        }
        self.header_bytes_since_refill += 1;
        Ok(self.buf[(off - self.buf_off) as usize])
    }
}

struct TilePartBytes<'a, 'f> {
    win: &'a mut StreamWin<'f>,
    origin: u64,
    limit: u64,
}

impl HeaderBytes for TilePartBytes<'_, '_> {
    fn byte(&mut self, index: usize) -> Result<u8, PacketError> {
        let index = index as u64;
        if index >= self.limit {
            return Err(PacketError::Truncated);
        }
        self.win
            .byte_at(self.origin + index)
            .map_err(|e| PacketError::Read(e.to_string()))
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
    states: Vec<BandBlocks>,
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

fn window_block_rect(bounds: Dims, window: Dims, block_w: u32, block_h: u32) -> BlockRect {
    let window = intersect(bounds, window);
    if window.is_empty() {
        return BlockRect::EMPTY;
    }
    let first_block_x = bounds.x0 / block_w;
    let first_block_y = bounds.y0 / block_h;
    BlockRect {
        x0: window.x0 / block_w - first_block_x,
        y0: window.y0 / block_h - first_block_y,
        x1: (window.x1 - 1) / block_w - first_block_x + 1,
        y1: (window.y1 - 1) / block_h - first_block_y + 1,
    }
}

fn codestream_end(file: &dyn ReadAt, hdr: &MainHeaderIn) -> Result<u64, DecodeError> {
    let file_len = file.extent().map_err(io_snag)?;
    // for a jp2c box followed by more boxes the codestream ends before the file does
    if hdr.codestream_len == 0 {
        return Ok(file_len);
    }
    Ok(hdr
        .codestream_off
        .saturating_add(hdr.codestream_len)
        .min(file_len))
}

pub struct ChartedPart {
    pub part: TlmEntry,
    pub tile_part: u8,
    pub num_tile_parts: u8,
    pub completes_tile: bool,
}

pub struct SotWalk {
    pub next_sot: u64,
    pub trust_tnsot: bool,
    pub reached_end: bool,
}

pub struct SotChainCursor {
    needed: Vec<bool>,
    codestream_end: u64,
    sot_pos: u64,
    parts_seen: Vec<u8>,
    tiles_pending: usize,
    trust_tnsot: bool,
    first: bool,
    ended: bool,
    reached_end: bool,
    window: Vec<u8>,
    window_start: u64,
    parts_charted: u64,
    bytes_charted: u64,
    cached_through: u64,
}

// SOT windows made TCI cold regions (43 KB tile-parts) 2.5x faster and 650 KB tile-parts slower
const SOT_WINDOW_BYTES: u64 = 256 << 10;

impl SotChainCursor {
    pub fn start(
        file: &dyn ReadAt,
        hdr: &MainHeaderIn,
        needed: Vec<bool>,
    ) -> Result<Self, DecodeError> {
        let tiles_pending = needed.iter().filter(|&&n| n).count();
        Ok(SotChainCursor {
            codestream_end: codestream_end(file, hdr)?,
            sot_pos: hdr.first_sot_off,
            parts_seen: vec![0u8; needed.len()],
            needed,
            tiles_pending,
            trust_tnsot: false,
            first: true,
            ended: false,
            reached_end: false,
            window: Vec::new(),
            window_start: 0,
            parts_charted: 0,
            bytes_charted: 0,
            cached_through: 0,
        })
    }

    pub fn resume(
        file: &dyn ReadAt,
        hdr: &MainHeaderIn,
        needed: Vec<bool>,
        walk: &SotWalk,
        parts_seen: Vec<u8>,
    ) -> Result<Self, DecodeError> {
        if parts_seen.len() != needed.len() {
            return Err(DecodeError::Logic(format!(
                "SOT walk resume has {} part counts for {} tiles",
                parts_seen.len(),
                needed.len()
            )));
        }
        let mut cursor = Self::start(file, hdr, needed)?;
        cursor.sot_pos = walk.next_sot;
        cursor.trust_tnsot = walk.trust_tnsot;
        cursor.first = false;
        cursor.ended = walk.reached_end;
        cursor.reached_end = walk.reached_end;
        cursor.parts_seen = parts_seen;
        Ok(cursor)
    }

    pub fn walk(&self) -> SotWalk {
        SotWalk {
            next_sot: self.sot_pos,
            trust_tnsot: self.trust_tnsot,
            reached_end: self.reached_end,
        }
    }

    fn read_sot(&mut self, file: &dyn ReadAt, sot_pos: u64) -> Result<[u8; 12], DecodeError> {
        let mut sot = [0u8; 12];
        let from = sot_pos.wrapping_sub(self.window_start) as usize;
        if sot_pos >= self.window_start && from + 12 <= self.window.len() {
            sot.copy_from_slice(&self.window[from..from + 12]);
            return Ok(sot);
        }
        let short_parts = self.parts_charted > 0
            && self.bytes_charted / self.parts_charted < SOT_WINDOW_BYTES / 2;
        let window_len = SOT_WINDOW_BYTES.min(self.codestream_end.saturating_sub(sot_pos));
        if !short_parts || window_len < 12 || sot_pos + 12 <= self.cached_through {
            file.draw_at(&mut sot, sot_pos).map_err(io_snag)?;
            return Ok(sot);
        }
        // warm TCI regions ran 1.3x slower copying 35 MB of cached window bytes per region
        let window_end = sot_pos + window_len;
        let mut far_end = [0u8; 12];
        if file
            .draw_at_if_cached(&mut far_end, window_end - 12)
            .map_err(io_snag)?
        {
            self.cached_through = window_end;
            file.draw_at(&mut sot, sot_pos).map_err(io_snag)?;
            return Ok(sot);
        }
        self.window.resize(window_len as usize, 0);
        file.draw_at(&mut self.window, sot_pos).map_err(io_snag)?;
        self.window_start = sot_pos;
        sot.copy_from_slice(&self.window[..12]);
        Ok(sot)
    }

    pub fn next_part(&mut self, file: &dyn ReadAt) -> Result<Option<ChartedPart>, DecodeError> {
        if self.ended {
            return Ok(None);
        }
        let sot_pos = self.sot_pos;
        let first = std::mem::replace(&mut self.first, false);
        // classic ignores whatever follows a tile-part if it is not a whole SOT segment
        if !first && sot_pos + 12 > self.codestream_end {
            self.ended = true;
            self.reached_end = true;
            return Ok(None);
        }
        let sot = self.read_sot(file, sot_pos)?;
        if u16::from_be_bytes([sot[0], sot[1]]) != 0xFF90 {
            if !first {
                self.ended = true;
                self.reached_end = true;
                return Ok(None);
            }
            return Err(DecodeError::Logic(format!("expected SOT at {sot_pos}")));
        }
        let isot = u16::from_be_bytes([sot[4], sot[5]]);
        let psot = u32::from_be_bytes([sot[6], sot[7], sot[8], sot[9]]) as u64;
        let tpsot = sot[10];
        let tnsot = sot[11];
        if isot as usize >= self.needed.len() {
            return Err(DecodeError::Logic(format!(
                "plan: SOT tile index {isot} out of range"
            )));
        }
        // classic trusts TNsot unchecked only when the first tile-part signals one part
        if first {
            self.trust_tnsot = tnsot == 1;
        }
        // A.4.2: only the last tile-part may carry Psot=0, and it runs to the EOC
        let last = psot == 0;
        let psot = if last {
            let mut span = self.codestream_end.saturating_sub(sot_pos);
            if span >= 2 {
                let mut m = [0u8; 2];
                file.draw_at(&mut m, self.codestream_end - 2)
                    .map_err(io_snag)?;
                if u16::from_be_bytes(m) == 0xFFD9 {
                    span -= 2;
                }
            }
            span
        } else {
            psot
        };
        let tile = isot as usize;
        let mut completes_tile = false;
        if self.trust_tnsot {
            self.parts_seen[tile] = self.parts_seen[tile].saturating_add(1);
            completes_tile = self.parts_seen[tile] == tnsot;
            if completes_tile && self.needed[tile] {
                self.tiles_pending -= 1;
            }
        }
        self.reached_end = last;
        self.ended = last || (completes_tile && self.needed[tile] && self.tiles_pending == 0);
        self.sot_pos = sot_pos + psot;
        self.parts_charted += 1;
        self.bytes_charted += psot;
        Ok(Some(ChartedPart {
            part: TlmEntry {
                tile: isot,
                offset: sot_pos,
                length: psot,
            },
            tile_part: tpsot,
            num_tile_parts: tnsot,
            completes_tile,
        }))
    }
}

pub fn chart_tile_parts(
    file: &dyn ReadAt,
    hdr: &MainHeaderIn,
    needed: &[bool],
) -> Result<Vec<TlmEntry>, DecodeError> {
    let mut cursor = SotChainCursor::start(file, hdr, needed.to_vec())?;
    let mut parts = Vec::new();
    while let Some(charted) = cursor.next_part(file)? {
        parts.push(charted.part);
    }
    Ok(parts)
}

const DEFAULT_SLICE_BLOCKS: u32 = 16;

/// How the plan uses PLT markers.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PltUse {
    /// Parse every packet header in stream order, PLT markers unread.
    Ignore,
    /// Hop non-contributing packets by their PLT lengths, in stream order.
    InOrder,
    /// Also parse precincts on this many threads when PLT lengths give
    /// every packet's start.
    Parallel(usize),
    /// As `Parallel`, after reading every contributing packet into memory in
    /// merged runs on `fetch_threads` threads, which the decode then reads its
    /// code-block bytes from.
    FetchedParallel {
        threads: usize,
        fetch_threads: usize,
    },
    Streamed,
    FetchedFeed {
        threads: usize,
        fetch_threads: usize,
    },
}

impl PltUse {
    fn threads(self) -> usize {
        match self {
            PltUse::Parallel(threads)
            | PltUse::FetchedParallel { threads, .. }
            | PltUse::FetchedFeed { threads, .. } => threads,
            _ => 1,
        }
    }
}

pub struct Drafted {
    pub plan: DecodePlan,
    pub fetched: Vec<FetchedRun>,
    pub feed: Option<TileFeed>,
}

pub enum TileFeed {
    Streamed(PacketFeed),
    Fetched(FetchedPacketFeed),
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
    plt_use: PltUse,
    window: Option<Dims>,
    int16_irreversible: bool,
) -> Result<Drafted, DecodeError> {
    // The host codec parsed the main header (SIZ/COD/QCD/QCC) and handed it in;
    // mercury only walks the SOT/tile-part chain and the packet headers below.
    let codestream_end = codestream_end(file, hdr)?;

    check_coding_style(&hdr.cod, hdr.siz.comp_count(), reduce, "")?;
    // int16 fixed point 9/7 meets the T.803 tolerances only up to 8 bits
    let q13 = !hdr.cod.comps[0].reversible
        && (int16_irreversible || hdr.siz.components.iter().all(|c| c.precision <= 8))
        && std::env::var_os("MERCURY_FORCE_F32").is_none();

    let num_tiles = (hdr.siz.tiles_across() * hdr.siz.tiles_down()) as usize;
    let num_comps = hdr.siz.comp_count();

    let quant = hdr.component_quant();

    // Tiles the decode window misses are never parsed: with TLM their
    // tile-part headers are never even read, and the sequential walk only
    // reads their SOT to find the next tile-part.
    let tile_in_window: Vec<bool> = (0..num_tiles as u32)
        .map(|t| {
            let Some(w) = window else { return true };
            intersects(&w, &hdr.siz.tile_rect(t))
        })
        .collect();

    // --- collect each decoded tile's packet-data segments ---
    let mut tile_segs: Vec<Vec<(u64, u64)>> = vec![Vec::new(); num_tiles];
    // Raw PLT payloads per tile: (Zplt, comma-coded length bytes).
    let mut tile_plt: Vec<Vec<(u8, Vec<u8>)>> = vec![Vec::new(); num_tiles];
    // Each tile's first tile-part header's COD/COC/QCD/QCC segments, in header
    // order: (marker code, payload after Lmar).
    let mut tile_styles: Vec<Vec<(u16, Vec<u8>)>> = vec![Vec::new(); num_tiles];
    let mut tile_pocs: Vec<Vec<(u8, Vec<u8>)>> = vec![Vec::new(); num_tiles];
    // A tile's coding style has to be known before its first packet, so only
    // the tile-part that opens a tile may carry those markers. TPsot names it,
    // but streams with a wrong TPsot decode anyway, so stream order decides.
    let mut tile_opened = vec![false; num_tiles];
    let walked_parts;
    let parts = if hdr.tlm.is_empty() {
        walked_parts = chart_tile_parts(file, hdr, &tile_in_window)?;
        &walked_parts
    } else {
        &hdr.tlm
    };
    for e in parts {
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
            e.length,
            sot[10],
            TilePartMarkerSinks {
                packet_lengths: (plt_use != PltUse::Ignore).then(|| &mut tile_plt[t]),
                coding_parameters: (!std::mem::replace(&mut tile_opened[t], true))
                    .then(|| &mut tile_styles[t]),
                progression_changes: &mut tile_pocs[t],
            },
        )?;
        tile_segs[t].push(seg);
    }

    // --- per-tile plans ---
    let main_cod = Arc::new(hdr.cod.clone());
    let mut win = StreamWin::warp(file).map_err(io_snag)?;
    let mut tiles: Vec<Option<TilePlan>> = Vec::with_capacity(num_tiles);
    let mut fetched: Vec<FetchedRun> = Vec::new();
    let mut feed: Option<TileFeed> = None;
    for (t, segs) in tile_styles.iter().enumerate() {
        if !tile_in_window[t] {
            tiles.push(None);
            continue;
        }
        let tile_quant_override: Vec<QcdParams>;
        let (cod, tile_quant) = if segs.is_empty() && tile_pocs[t].is_empty() {
            (Arc::clone(&main_cod), quant.as_slice())
        } else {
            let (mut cod, tile_quant) = comb_tile_params(&hdr.cod, &quant, segs)?;
            comb_tile_pocs(&mut cod, &tile_pocs[t])?;
            // A tile-part COD cannot raise the main header's layer count.
            cod.num_layers = cod.num_layers.min(hdr.cod.num_layers);
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
            tile_quant_override = tile_quant;
            (Arc::new(cod), tile_quant_override.as_slice())
        };
        let geom = chart_tile_geom(&hdr.siz, &cod, t as u16);
        // Per-component padded windows.
        let mut tile_wins: Option<Vec<TileCompWindow>> = None;
        if let Some(w) = window {
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
                    chart_tile_comp_window(
                        clipped,
                        &resolution_band_dims(tc),
                        style.num_levels,
                        style.reversible,
                    )
                })
                .collect();
            tile_wins = Some(wins);
        }
        let plt = plt_packet_lengths(std::mem::take(&mut tile_plt[t]));
        let combed = comb_tile(
            file,
            &mut win,
            &cod,
            tile_quant,
            &hdr.siz,
            &geom,
            TileStream::warp(std::mem::take(&mut tile_segs[t])),
            reduce,
            max_layers,
            plt.as_deref(),
            tile_wins.as_deref(),
            q13,
            plt_use,
        )?;
        fetched.extend(combed.fetched);
        if let Some(tile_feed) = combed.feed {
            assert!(feed.is_none(), "a streamed decode covers one tile");
            feed = Some(tile_feed);
        }
        tiles.push(Some(TilePlan {
            comps: combed.comps,
            cod,
            geom,
            win: tile_wins,
        }));
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
    let plan = DecodePlan {
        width: ceildivpow2(out.x1, reduce) - ceildivpow2(out.x0, reduce),
        height: ceildivpow2(out.y1, reduce) - ceildivpow2(out.y0, reduce),
        comp_dims,
        tiles,
        cod: hdr.cod.clone(),
        siz: hdr.siz.clone(),
        reduce,
        q13,
        slice_blocks: DEFAULT_SLICE_BLOCKS,
        max_strips: 1,
        int16_dwt: false,
        feed_link: feed.as_ref().map(|feed| match feed {
            TileFeed::Streamed(feed) => Arc::clone(&feed.link),
            TileFeed::Fetched(feed) => Arc::clone(&feed.link),
        }),
    };
    Ok(Drafted {
        plan,
        fetched,
        feed,
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
    // HT and HTMIX use Part 15 packet-length signalling.
    use crate::codec::params::CodingModes;
    let supported = CodingModes::BYPASS
        | CodingModes::RESET
        | CodingModes::TERMALL
        | CodingModes::CAUSAL
        | CodingModes::ERTERM
        | CodingModes::SEGMARK;
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

fn comb_tile_pocs(cod: &mut CodParams, segs: &[(u8, Vec<u8>)]) -> Result<(), DecodeError> {
    use crate::codec::markers;
    let mut by_tile_part = std::collections::BTreeMap::new();
    for (tile_part, payload) in segs {
        let volumes = markers::comb_poc(payload, cod.comps.len())
            .map_err(|why| DecodeError::Logic(format!("plan: tile-part 0xff5f: {why}")))?;
        by_tile_part.insert(*tile_part, volumes);
    }
    if by_tile_part.contains_key(&0) {
        cod.pocs.clear();
    }
    for volumes in by_tile_part.into_values() {
        cod.pocs.extend(volumes);
    }
    Ok(())
}

struct TilePartMarkerSinks<'a> {
    packet_lengths: Option<&'a mut Vec<(u8, Vec<u8>)>>,
    coding_parameters: Option<&'a mut Vec<(u16, Vec<u8>)>>,
    progression_changes: &'a mut Vec<(u8, Vec<u8>)>,
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
    tile_part_index: u8,
    mut sinks: TilePartMarkerSinks,
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
            // PPT means packed headers elsewhere, and RGN upshifts the tile's
            // coefficients.
            0xFF5E | 0xFF61 => {
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
            let Some(sink) = sinks.coding_parameters.as_deref_mut() else {
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
        if code == 0xFF5F {
            if l < 3 {
                return Err(DecodeError::Logic(
                    "plan: tile-part marker 0xff5f carries no payload".into(),
                ));
            }
            let mut payload = vec![0u8; (l - 2) as usize];
            file.draw_at(&mut payload, mp + 4).map_err(io_snag)?;
            sinks.progression_changes.push((tile_part_index, payload));
        }
        // PLT (A.7.1): Zplt byte then comma-coded packet lengths.
        if code == 0xFF58 && l >= 3 {
            if let Some(sink) = sinks.packet_lengths.as_deref_mut() {
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
pub(crate) fn plt_packet_lengths(mut markers: Vec<(u8, Vec<u8>)>) -> Option<Vec<u32>> {
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
    q13: bool,
    plt_use: PltUse,
) -> Result<CombedTile, DecodeError> {
    let num_comps = geom.components.len();
    // Resolution counts and the reduce target are per component (COC).
    let n_res = |c: usize| cod.comps[c].num_levels as usize + 1;
    let target_res = |c: usize| cod.comps[c].num_levels as usize - reduce as usize;

    let grids = chart_precinct_grids(cod, geom);

    // Each packet takes at least one byte (T.800 B.10).
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
    if declared_packets > stream.total {
        return Err(DecodeError::Logic(format!(
            "plan: {declared_packets} declared packets in a tile of {} packet bytes",
            stream.total
        )));
    }

    // --- per-precinct facts every progression volume reuses ---
    // in_win per precinct, in raster index order.
    let mut prec_in_win: Vec<Vec<Vec<bool>>> = (0..num_comps)
        .map(|c| (0..n_res(c)).map(|_| Vec::new()).collect())
        .collect();
    for c in 0..num_comps {
        let tc = &geom.components[c];
        let style = &cod.comps[c];
        for r in 0..n_res(c) {
            let (px0, py0, npx, npy) = grids[c][r];
            if npx == 0 {
                continue;
            }
            let res = &tc.resolutions[r];
            let ps = style.precinct_span(r as u8);
            for py in py0..py0 + npy {
                for px in px0..px0 + npx {
                    let in_win = wins
                        .map(|ws| precinct_in_window(res, &ws[c].bands[r], px, py, ps, r))
                        .unwrap_or(true);
                    prec_in_win[c][r].push(in_win);
                }
            }
        }
    }

    let positions = chart_precinct_positions(cod, siz, geom, &grids);
    let mut pkts: Vec<Pkt> = order_packets(cod, &grids, &positions)?
        .into_iter()
        .map(|slot| Pkt {
            comp: slot.comp,
            res: slot.res,
            layer: slot.layer,
            prec: slot.prec,
            in_win: prec_in_win[slot.comp as usize][slot.res as usize][slot.prec as usize],
        })
        .collect();

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
                    } else if let Some(wins) = wins {
                        window_block_rect(sb.dims, wins[c].bands[r][i], block_w, block_h)
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
                        published: None,
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

    // --- parse packet headers ---
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
    let walk_len = last_needed.map_or(0, |i| i + 1);
    let tile = TileWalk {
        file,
        cod,
        geom,
        grids: &grids,
        stream: &stream,
    };
    let streamed_lengths =
        plt.filter(|lengths| plt_use == PltUse::Streamed && lengths.len() >= walk_len);
    if let Some(lengths) = streamed_lengths {
        let shells = shells_for_graph(&mut comps);
        let charted =
            chart_packet_feed(&tile, &pkts[..walk_len], lengths, &contributes, &mut comps)?;
        pkts.truncate(walk_len);
        let feed = PacketFeed {
            cod: cod.clone(),
            geom: geom.clone(),
            grids,
            stream,
            walk: pkts,
            contributing: charted.contributing,
            starts: charted.starts,
            lengths: lengths[..walk_len].to_vec(),
            comps,
            pending: charted.pending,
            runs: charted.runs,
            link: Arc::new(FeedLink {
                state: Mutex::new(FeedState::Feeding(None)),
            }),
            finished: false,
        };
        return Ok(CombedTile {
            comps: shells,
            fetched: Vec::new(),
            feed: Some(TileFeed::Streamed(feed)),
        });
    }
    let fetched_feed = match plt_use {
        PltUse::FetchedFeed {
            threads,
            fetch_threads,
        } => plt
            .filter(|lengths| lengths.len() >= walk_len)
            .map(|lengths| (lengths, threads, fetch_threads)),
        _ => None,
    };
    if let Some((lengths, threads, fetch_threads)) = fetched_feed {
        let shells = shells_for_graph(&mut comps);
        pkts.truncate(walk_len);
        let starts = packet_starts(lengths, walk_len);
        let contributing: Vec<bool> = pkts.iter().map(&contributes).collect();
        let pending = chart_pending_rows(&tile, &pkts, &contributing, &mut comps);
        let groups = group_precincts(&tile, &pkts, &contributes);
        let runs = chart_packet_runs(
            &stream,
            &starts,
            lengths,
            groups.iter().flatten().map(|&i| i as usize),
        )?;
        let feed = FetchedPacketFeed {
            cod: cod.clone(),
            geom: geom.clone(),
            grids,
            stream,
            walk: pkts,
            starts,
            lengths: lengths[..walk_len].to_vec(),
            groups,
            comps,
            pending,
            runs,
            threads,
            fetch_threads,
            link: Arc::new(FeedLink {
                state: Mutex::new(FeedState::Feeding(None)),
            }),
            finished: false,
        };
        return Ok(CombedTile {
            comps: shells,
            fetched: Vec::new(),
            feed: Some(TileFeed::Fetched(feed)),
        });
    }
    let walk = &pkts[..walk_len];
    // PLT lengths give every packet's start, so precincts parse independently
    let fetched = match plt.filter(|lengths| plt_use.threads() > 1 && lengths.len() >= walk.len()) {
        Some(lengths) => {
            comb_packets_in_parallel(&tile, walk, lengths, &contributes, &mut comps, plt_use)?
        }
        None => {
            comb_packets_in_order(&tile, win, walk, plt, &contributes, &mut comps)?;
            Vec::new()
        }
    };
    Ok(CombedTile {
        comps,
        fetched,
        feed: None,
    })
}

fn shells_for_graph(comps: &mut [Vec<ResPlan>]) -> Vec<Vec<ResPlan>> {
    comps
        .iter_mut()
        .map(|res_plans| {
            res_plans
                .iter_mut()
                .map(|res| ResPlan {
                    bands: res
                        .bands
                        .iter_mut()
                        .map(BandPlan::shell_for_graph)
                        .collect(),
                })
                .collect()
        })
        .collect()
}

struct CombedTile {
    comps: Vec<Vec<ResPlan>>,
    fetched: Vec<FetchedRun>,
    feed: Option<TileFeed>,
}

struct ChartedFeed {
    starts: Vec<u64>,
    contributing: Vec<bool>,
    pending: Vec<Vec<Vec<Vec<u32>>>>,
    runs: Vec<Range<u64>>,
}

fn chart_packet_feed(
    tile: &TileWalk,
    walk: &[Pkt],
    lengths: &[u32],
    contributes: &dyn Fn(&Pkt) -> bool,
    comps: &mut [Vec<ResPlan>],
) -> Result<ChartedFeed, DecodeError> {
    let starts = packet_starts(lengths, walk.len());
    let vpos = starts
        .last()
        .map_or(0, |&last| last + lengths[walk.len() - 1] as u64);
    if vpos > tile.stream.total {
        return Err(DecodeError::Logic(format!(
            "plan: PLT lengths reach {vpos} of {} packet bytes",
            tile.stream.total
        )));
    }
    let contributing: Vec<bool> = walk.iter().map(contributes).collect();
    let pending = chart_pending_rows(tile, walk, &contributing, comps);
    let runs = chart_packet_runs(
        tile.stream,
        &starts,
        lengths,
        (0..walk.len()).filter(|&i| contributing[i]),
    )?;
    Ok(ChartedFeed {
        starts,
        contributing,
        pending,
        runs,
    })
}

fn chart_packet_runs(
    stream: &TileStream,
    starts: &[u64],
    lengths: &[u32],
    packets: impl Iterator<Item = usize>,
) -> Result<Vec<Range<u64>>, DecodeError> {
    let mut ranges = Vec::new();
    for i in packets {
        let (mut vpos, end) = (starts[i], starts[i] + lengths[i] as u64);
        while vpos < end {
            let (file_off, segment_left) = stream.unspool(vpos)?;
            let take = segment_left.min(end - vpos);
            ranges.push(file_off..file_off + take);
            vpos += take;
        }
    }
    Ok(merge_ranges(ranges, FETCH_GAP_BYTES))
}

fn chart_pending_rows(
    tile: &TileWalk,
    walk: &[Pkt],
    contributing: &[bool],
    comps: &mut [Vec<ResPlan>],
) -> Vec<Vec<Vec<Vec<u32>>>> {
    let mut pending: Vec<Vec<Vec<Vec<u32>>>> = comps
        .iter()
        .map(|res_plans| {
            res_plans
                .iter()
                .map(|res| {
                    res.bands
                        .iter()
                        .map(|band| vec![0; band.stored_blocks_high as usize])
                        .collect()
                })
                .collect()
        })
        .collect();
    for (i, pkt) in walk.iter().enumerate() {
        if !contributing[i] {
            continue;
        }
        let (c, r) = (pkt.comp as usize, pkt.res as usize);
        for (b, prec_band) in tile.prec_bands(pkt).iter().enumerate() {
            for row in comps[c][r].bands[b].stored_rows_of(prec_band) {
                pending[c][r][b][row] += 1;
            }
        }
    }
    for (c, res_plans) in comps.iter_mut().enumerate() {
        for (r, res) in res_plans.iter_mut().enumerate() {
            for (b, band) in res.bands.iter_mut().enumerate() {
                for (row, &count) in pending[c][r][b].iter().enumerate() {
                    if count == 0 {
                        band.publish_row(row);
                    }
                }
            }
        }
    }
    pending
}

pub struct PacketFeed {
    cod: CodParams,
    geom: TileGeom,
    grids: Vec<Vec<(u32, u32, u32, u32)>>,
    stream: TileStream,
    walk: Vec<Pkt>,
    contributing: Vec<bool>,
    starts: Vec<u64>,
    lengths: Vec<u32>,
    comps: Vec<Vec<ResPlan>>,
    pending: Vec<Vec<Vec<Vec<u32>>>>,
    runs: Vec<Range<u64>>,
    link: Arc<FeedLink>,
    finished: bool,
}

impl PacketFeed {
    pub fn runs(&self) -> &[Range<u64>] {
        &self.runs
    }

    pub fn run<S: ReadAt>(mut self, bytes: &FilledRuns<S>) {
        let outcome = self.comb_walk(bytes);
        self.link.finish(outcome);
        self.finished = true;
    }

    fn comb_walk<S: ReadAt>(&mut self, bytes: &FilledRuns<S>) -> Result<(), DecodeError> {
        let PacketFeed {
            cod,
            geom,
            grids,
            stream,
            walk,
            contributing,
            starts,
            lengths,
            comps,
            pending,
            link,
            ..
        } = self;
        let tile = TileWalk {
            file: bytes,
            cod,
            geom,
            grids,
            stream,
        };
        let mut win = StreamWin::warp(bytes).map_err(io_snag)?;
        let mut cursor = FillCursor::default();
        let mut sealed = FillCursor::default();
        let mut prec_states: Vec<Vec<Vec<Option<PrecState>>>> = grids
            .iter()
            .map(|comp_grids| {
                comp_grids
                    .iter()
                    .map(|&(_, _, npx, npy)| (0..npx * npy).map(|_| None).collect())
                    .collect()
            })
            .collect();
        for (i, pkt) in walk.iter().enumerate() {
            if !contributing[i] {
                continue;
            }
            let end = starts[i] + lengths[i] as u64;
            let (last_byte, _) = stream.unspool(end - 1)?;
            bytes
                .fill_through(&mut cursor, last_byte + 1, &|| link.graph_failed())
                .map_err(io_snag)?;
            let (c, r) = (pkt.comp as usize, pkt.res as usize);
            let prec_bands = tile.prec_bands(pkt);
            let state_slot = &mut prec_states[c][r][pkt.prec as usize];
            let state = state_slot.get_or_insert_with(|| PrecState::warp(&prec_bands));
            let combed = comb_packet(&tile, &mut win, state, pkt, starts[i])?;
            check_plt_length(lengths[i], starts[i], combed.end)?;
            fill_block_records(comps, pkt, &prec_bands, &combed, &mut |off, len| {
                bytes.owe(off, len as u64)
            })?;
            bytes.seal_through(&mut sealed, last_byte + 1);
            if cod.num_layers == 1 {
                *state_slot = None;
            }
            for (b, prec_band) in prec_bands.iter().enumerate() {
                let band = &mut comps[c][r].bands[b];
                let mut published = false;
                for row in band.stored_rows_of(prec_band) {
                    let count = &mut pending[c][r][b][row];
                    *count -= 1;
                    if *count == 0 {
                        bytes.publish(band.stored_row_bytes(row));
                        band.publish_row(row);
                        published = true;
                    }
                }
                if published {
                    link.tug_band_nodes(
                        band.published_rows()
                            .expect("a fed band has published rows"),
                    );
                }
            }
        }
        bytes.seal_through(&mut sealed, u64::MAX);
        Ok(())
    }
}

impl Drop for PacketFeed {
    fn drop(&mut self) {
        if !self.finished {
            self.link.finish(Err(DecodeError::Logic(
                "packet feed ended before parsing every packet".into(),
            )));
        }
    }
}

pub struct FetchedPacketFeed {
    cod: CodParams,
    geom: TileGeom,
    grids: Vec<Vec<(u32, u32, u32, u32)>>,
    stream: TileStream,
    walk: Vec<Pkt>,
    starts: Vec<u64>,
    lengths: Vec<u32>,
    groups: Vec<Vec<u32>>,
    comps: Vec<Vec<ResPlan>>,
    pending: Vec<Vec<Vec<Vec<u32>>>>,
    runs: Vec<Range<u64>>,
    threads: usize,
    fetch_threads: usize,
    link: Arc<FeedLink>,
    finished: bool,
}

impl FetchedPacketFeed {
    pub fn run(
        mut self,
        source: Arc<dyn ReadAt>,
        fetched: &OnceLock<FetchedRuns<Arc<dyn ReadAt>>>,
    ) {
        let outcome = self.fetch_and_comb(source, fetched);
        self.link.finish(outcome);
        self.finished = true;
    }

    fn fetch_and_comb(
        &mut self,
        source: Arc<dyn ReadAt>,
        fetched: &OnceLock<FetchedRuns<Arc<dyn ReadAt>>>,
    ) -> Result<(), DecodeError> {
        let runs = fetch_runs(&*source, &self.runs, self.fetch_threads).map_err(io_snag)?;
        if fetched.set(FetchedRuns::over(source, runs)).is_err() {
            panic!("a fetched feed runs once");
        }
        let FetchedPacketFeed {
            cod,
            geom,
            grids,
            stream,
            walk,
            starts,
            lengths,
            groups,
            comps,
            pending,
            threads,
            link,
            ..
        } = self;
        let tile = TileWalk {
            file: fetched.get().expect("fetched runs were just set"),
            cod,
            geom,
            grids,
            stream,
        };
        let claims = PrecinctClaim {
            walk,
            starts,
            lengths,
            groups,
        };
        comb_claims_in_parallel(&tile, &claims, *threads, |i, prec_bands, combed| {
            let pkt = &walk[i];
            fill_block_records(comps, pkt, prec_bands, combed, &mut |_, _| {})?;
            let (c, r) = (pkt.comp as usize, pkt.res as usize);
            for (b, prec_band) in prec_bands.iter().enumerate() {
                let band = &mut comps[c][r].bands[b];
                let mut published = false;
                for row in band.stored_rows_of(prec_band) {
                    let count = &mut pending[c][r][b][row];
                    *count -= 1;
                    if *count == 0 {
                        band.publish_row(row);
                        published = true;
                    }
                }
                if published {
                    link.tug_band_nodes(
                        band.published_rows()
                            .expect("a fed band has published rows"),
                    );
                }
            }
            Ok(())
        })
    }
}

impl Drop for FetchedPacketFeed {
    fn drop(&mut self) {
        if !self.finished {
            self.link.finish(Err(DecodeError::Logic(
                "fetched packet feed ended before parsing every packet".into(),
            )));
        }
    }
}

pub struct FeedLink {
    state: Mutex<FeedState>,
}

enum FeedState {
    Feeding(Option<GraphHold>),
    Finished,
    Failed(String),
}

impl FeedLink {
    pub fn launch_graph(&self, runtime: HostedRuntime, kick: &[NodeId], on_still: StillnessFn) {
        let mut state = self.state.lock().unwrap();
        match &mut *state {
            FeedState::Feeding(hold) => *hold = Some(runtime.launch_held(kick, on_still)),
            FeedState::Finished => runtime.launch(kick, on_still),
            FeedState::Failed(msg) => runtime.launch_held(kick, on_still).fail(msg.clone()),
        }
    }

    fn graph_failed(&self) -> bool {
        match &*self.state.lock().unwrap() {
            FeedState::Feeding(hold) => hold.as_ref().is_some_and(GraphHold::failed),
            FeedState::Finished => false,
            FeedState::Failed(_) => true,
        }
    }

    fn tug_band_nodes(&self, published: &PublishedRows) {
        let state = self.state.lock().unwrap();
        let (FeedState::Feeding(Some(hold)), Some(nodes)) = (&*state, published.nodes.get()) else {
            return;
        };
        for &id in nodes.iter() {
            hold.tug(id);
        }
    }

    fn finish(&self, outcome: Result<(), DecodeError>) {
        let next = match &outcome {
            Ok(()) => FeedState::Finished,
            Err(e) => FeedState::Failed(format!("packet feed: {e:?}")),
        };
        let previous = std::mem::replace(&mut *self.state.lock().unwrap(), next);
        let FeedState::Feeding(hold) = previous else {
            panic!("a packet feed finishes once");
        };
        if let (Some(hold), Err(e)) = (&hold, &outcome) {
            hold.fail(format!("packet feed: {e:?}"));
        }
    }
}

/// What every packet parse of one tile reads.
struct TileWalk<'a> {
    file: &'a dyn ReadAt,
    cod: &'a CodParams,
    geom: &'a TileGeom,
    grids: &'a [Vec<(u32, u32, u32, u32)>],
    stream: &'a TileStream,
}

impl TileWalk<'_> {
    fn prec_bands(&self, pkt: &Pkt) -> Vec<PrecBand> {
        let (c, r) = (pkt.comp as usize, pkt.res as usize);
        let (px0, py0, npx, _) = self.grids[c][r];
        let (block_w, block_h) = self.cod.comps[c].block_span(r as u8);
        chart_prec_bands(
            &self.geom.components[c].resolutions[r],
            px0 + pkt.prec % npx,
            py0 + pkt.prec / npx,
            self.cod.comps[c].precinct_span(r as u8),
            r,
            block_w,
            block_h,
        )
    }
}

impl PrecState {
    fn warp(prec_bands: &[PrecBand]) -> Self {
        PrecState {
            trees: prec_bands
                .iter()
                .map(|pb| TagTree::warp(pb.nbw.max(1), pb.nbh.max(1)))
                .collect(),
            states: prec_bands
                .iter()
                .map(|pb| BandBlocks::warp((pb.nbw * pb.nbh) as usize))
                .collect(),
        }
    }
}

/// One parsed packet header and where its packet ends.
struct CombedPacket {
    parsed: PacketParseResult,
    /// Real file offset of the packet body.
    body_real: u64,
    /// Virtual offset just past the packet body.
    end: u64,
}

/// Parse the header of the packet starting at virtual offset `vpos`.
fn comb_packet(
    tile: &TileWalk,
    win: &mut StreamWin<'_>,
    state: &mut PrecState,
    pkt: &Pkt,
    mut vpos: u64,
) -> Result<CombedPacket, DecodeError> {
    let cod = tile.cod;
    let stream = tile.stream;
    // SOP marker segment (6 bytes) before the packet, if signalled.
    if cod.use_sop {
        let (rp, avail) = stream.unspool(vpos)?;
        if avail >= 2 {
            let mut m = [0u8; 2];
            tile.file.draw_at(&mut m, rp).map_err(io_snag)?;
            if u16::from_be_bytes(m) == 0xFF91 {
                vpos += 6;
            }
        }
    }

    let (real_pos, seg_avail) = stream.unspool(vpos)?;
    let mut reader = PacketBitReader::warp(TilePartBytes {
        win,
        origin: real_pos,
        limit: seg_avail,
    });
    let parsed = comb_packet_header(
        &mut reader,
        &mut state.trees,
        &mut state.states,
        pkt.layer,
        0,
        cod.comps[pkt.comp as usize].modes,
    )
    .map_err(|e| DecodeError::Logic(format!("packet parse at vpos {vpos}: {e:?}")))?;
    let mut hdr_len = parsed.header_bytes as u64;

    // EPH marker (2 bytes) terminates the packet header, if signalled.
    if cod.use_eph {
        hdr_len += 2;
    }
    // the last packet of a cut stream has no later packet to overrun
    let end = vpos + hdr_len + parsed.body_bytes;
    if end > stream.total {
        return Err(DecodeError::Logic(format!(
            "packet stream overrun: packet ends at {end} of {}",
            stream.total
        )));
    }
    // Bodies are contiguous in the real file (a packet never spans tile-parts).
    let (body_real, _) = stream.unspool(vpos + hdr_len).unwrap_or((0, 0));
    Ok(CombedPacket {
        parsed,
        body_real,
        end,
    })
}

/// A PLT length that disagrees with the parse means one of them lied, so
/// reject and fall back to classic.
fn check_plt_length(plt_len: u32, start: u64, end: u64) -> Result<(), DecodeError> {
    let parsed_len = end - start;
    if parsed_len != plt_len as u64 {
        return Err(DecodeError::Logic(format!(
            "plan: PLT length {plt_len} != parsed packet length {parsed_len} at vpos {start}"
        )));
    }
    Ok(())
}

/// Record the block contributions of a parsed packet, assigning body offsets
/// in contribution order.
fn fill_block_records(
    comps: &mut [Vec<ResPlan>],
    pkt: &Pkt,
    prec_bands: &[PrecBand],
    combed: &CombedPacket,
    on_recorded: &mut dyn FnMut(u64, u32),
) -> Result<(), DecodeError> {
    let (c, r) = (pkt.comp as usize, pkt.res as usize);
    let mut body = combed.body_real;
    for (band_idx, pb) in prec_bands.iter().enumerate() {
        let contribs = &combed.parsed.contributions[band_idx];
        let band = &mut comps[c][r].bands[band_idx];
        for (li, contrib) in contribs {
            if contrib.new_passes == 0 {
                continue;
            }
            let total: u32 = contrib.segment_lengths().sum();
            let lx = *li % pb.nbw;
            let ly = *li / pb.nbw;
            let block_body = body;
            body += total as u64;
            // classic abandons the tile at a block claiming more missing
            // bit planes than its band has, so a plan that kept parsing
            // would decode bytes classic never reads
            if contrib.missing_msbs as i32 > band.k_max_prime.max(0) {
                return Err(DecodeError::Logic(format!(
                    "plan: {} missing bit planes in a band of {}",
                    contrib.missing_msbs, band.k_max_prime
                )));
            }
            let Some(rec) = band.stored_block_mut(pb.bx0 + lx, pb.by0 + ly) else {
                continue;
            };
            on_recorded(block_body, total);
            if rec.num_passes == 0 {
                // First contribution (inclusion always adds ≥1 pass).
                rec.file_off = block_body;
                rec.len = total;
                rec.num_passes = contrib.new_passes;
                rec.missing_msbs = contrib.missing_msbs;
                rec.num_segments = contrib.num_segments();
                if contrib.num_segments() > 1 {
                    rec.seg_lens = Some(
                        contrib
                            .segment_lengths()
                            .copied()
                            .collect::<Vec<_>>()
                            .into_boxed_slice(),
                    );
                }
            } else {
                append_segment_lengths(rec, contrib)?;
                rec.num_passes = rec.num_passes.saturating_add(contrib.new_passes);
                rec.extra.get_or_insert_with(Default::default).push(Chunk {
                    file_off: block_body,
                    len: total,
                });
            }
        }
    }
    Ok(())
}

/// Parse the walk's packet headers one after another in stream order.
fn comb_packets_in_order(
    tile: &TileWalk,
    win: &mut StreamWin<'_>,
    walk: &[Pkt],
    plt: Option<&[u32]>,
    contributes: &dyn Fn(&Pkt) -> bool,
    comps: &mut [Vec<ResPlan>],
) -> Result<(), DecodeError> {
    let cod = tile.cod;
    // Persistent precinct state, keyed [comp][res][precinct raster index].
    let mut prec_states: Vec<Vec<Vec<Option<PrecState>>>> = tile
        .grids
        .iter()
        .map(|comp_grids| {
            comp_grids
                .iter()
                .map(|&(_, _, npx, npy)| (0..npx * npy).map(|_| None).collect())
                .collect()
        })
        .collect();
    let mut vpos: u64 = 0;
    let mut plt_idx: usize = 0;
    for pkt in walk {
        // A PLT length covers the whole packet, SOP and header included
        // (A.7.1), so a packet that contributes no block records hops by
        // length, header unparsed. Skipped layers are the tail layers of a
        // precinct, skipped resolutions and out-of-window precincts never
        // parse at all, so the tag-tree state the parsed packets need is
        // never behind.
        let contributes = contributes(pkt);
        let plt_len = plt.and_then(|v| v.get(plt_idx)).copied();
        if plt_len.is_some() {
            plt_idx += 1;
        }
        if !contributes {
            if let Some(len) = plt_len {
                vpos += len as u64;
                continue;
            }
        }
        let prec_bands = tile.prec_bands(pkt);
        let state_slot = &mut prec_states[pkt.comp as usize][pkt.res as usize][pkt.prec as usize];
        let state = state_slot.get_or_insert_with(|| PrecState::warp(&prec_bands));
        let combed = comb_packet(tile, win, state, pkt, vpos)?;
        // a non-contributing packet only reaches here with no PLT length to
        // hop by; its header is still parsed but fills no block records
        if contributes {
            fill_block_records(comps, pkt, &prec_bands, &combed, &mut |_, _| {})?;
        }
        // Single-layer streams never revisit a precinct: free its state now
        // rather than holding ~30k tag trees alive for the whole parse.
        if cod.num_layers == 1 {
            *state_slot = None;
        }
        if let Some(len) = plt_len {
            check_plt_length(len, vpos, combed.end)?;
        }
        vpos = combed.end;
    }
    Ok(())
}

/// Precinct groups a worker claims at a time.
const PRECINCTS_PER_CLAIM: usize = 64;

/// One precinct's prec bands and its parsed packets, by walk index.
type GroupResult = (Vec<PrecBand>, Vec<(u32, CombedPacket)>);

/// A run of precinct groups one worker parses.
struct PrecinctClaim<'a> {
    walk: &'a [Pkt],
    starts: &'a [u64],
    lengths: &'a [u32],
    /// Walk indices of each precinct's contributing packets, layers in order.
    groups: &'a [Vec<u32>],
}

fn comb_claim(
    tile: &TileWalk,
    win: &mut StreamWin<'_>,
    claim: &PrecinctClaim,
) -> Result<Vec<GroupResult>, DecodeError> {
    let mut results = Vec::with_capacity(claim.groups.len());
    for group in claim.groups {
        let prec_bands = tile.prec_bands(&claim.walk[group[0] as usize]);
        let mut state = PrecState::warp(&prec_bands);
        let mut packets = Vec::with_capacity(group.len());
        for &i in group {
            let i = i as usize;
            let combed = comb_packet(tile, win, &mut state, &claim.walk[i], claim.starts[i])?;
            check_plt_length(claim.lengths[i], claim.starts[i], combed.end)?;
            packets.push((i as u32, combed));
        }
        results.push((prec_bands, packets));
    }
    Ok(results)
}

fn packet_starts(lengths: &[u32], walk_len: usize) -> Vec<u64> {
    let mut starts = Vec::with_capacity(walk_len);
    let mut vpos = 0u64;
    for &len in &lengths[..walk_len] {
        starts.push(vpos);
        vpos += len as u64;
    }
    starts
}

fn group_precincts(
    tile: &TileWalk,
    walk: &[Pkt],
    contributes: &dyn Fn(&Pkt) -> bool,
) -> Vec<Vec<u32>> {
    let precinct_base: Vec<Vec<usize>> = {
        let mut next = 0usize;
        tile.grids
            .iter()
            .map(|comp_grids| {
                comp_grids
                    .iter()
                    .map(|&(_, _, npx, npy)| {
                        let base = next;
                        next += (npx * npy) as usize;
                        base
                    })
                    .collect()
            })
            .collect()
    };
    let precinct_count = tile
        .grids
        .iter()
        .flatten()
        .map(|&(_, _, npx, npy)| (npx * npy) as usize)
        .sum();
    let mut group_of_precinct: Vec<u32> = vec![u32::MAX; precinct_count];
    let mut groups: Vec<Vec<u32>> = Vec::new();
    for (i, pkt) in walk.iter().enumerate() {
        if !contributes(pkt) {
            continue;
        }
        let precinct = precinct_base[pkt.comp as usize][pkt.res as usize] + pkt.prec as usize;
        if group_of_precinct[precinct] == u32::MAX {
            group_of_precinct[precinct] = groups.len() as u32;
            groups.push(Vec::new());
        }
        groups[group_of_precinct[precinct] as usize].push(i as u32);
    }
    groups
}

/// Parse the walk's packet headers on `threads` threads. Every packet starts
/// at the sum of the PLT lengths before it, and packets of different
/// precincts share no tag-tree state, so each worker parses whole precincts,
/// their layers in order, while the calling thread records the results.
fn comb_packets_in_parallel(
    tile: &TileWalk,
    walk: &[Pkt],
    lengths: &[u32],
    contributes: &dyn Fn(&Pkt) -> bool,
    comps: &mut [Vec<ResPlan>],
    plt_use: PltUse,
) -> Result<Vec<FetchedRun>, DecodeError> {
    let starts = packet_starts(lengths, walk.len());
    let groups = group_precincts(tile, walk, contributes);
    let fetched = match plt_use {
        PltUse::FetchedParallel { fetch_threads, .. } => {
            let runs = chart_packet_runs(
                tile.stream,
                &starts,
                lengths,
                groups.iter().flatten().map(|&i| i as usize),
            )?;
            Some(FetchedRuns::over(
                tile.file,
                fetch_runs(tile.file, &runs, fetch_threads).map_err(io_snag)?,
            ))
        }
        _ => None,
    };
    let fetched_tile;
    let tile = match &fetched {
        Some(fetched) => {
            fetched_tile = TileWalk {
                file: fetched,
                ..*tile
            };
            &fetched_tile
        }
        None => tile,
    };
    let claims = PrecinctClaim {
        walk,
        starts: &starts,
        lengths,
        groups: &groups,
    };
    comb_claims_in_parallel(tile, &claims, plt_use.threads(), |i, prec_bands, combed| {
        fill_block_records(comps, &walk[i], prec_bands, combed, &mut |_, _| {})
    })?;
    Ok(fetched.map_or_else(Vec::new, FetchedRuns::into_runs))
}

fn comb_claims_in_parallel(
    tile: &TileWalk,
    claims: &PrecinctClaim,
    threads: usize,
    mut record: impl FnMut(usize, &[PrecBand], &CombedPacket) -> Result<(), DecodeError>,
) -> Result<(), DecodeError> {
    let groups = claims.groups;
    let next_claim = AtomicUsize::new(0);
    let abort = AtomicBool::new(false);
    let threads = threads
        .min(groups.len().div_ceil(PRECINCTS_PER_CLAIM))
        .max(1);
    let (sender, receiver) = mpsc::channel::<Result<Vec<GroupResult>, DecodeError>>();
    std::thread::scope(|scope| {
        for _ in 0..threads {
            let sender = sender.clone();
            let (next_claim, abort) = (&next_claim, &abort);
            scope.spawn(move || {
                let mut win = match StreamWin::warp(tile.file) {
                    Ok(win) => win,
                    Err(e) => {
                        abort.store(true, Ordering::Relaxed);
                        let _ = sender.send(Err(io_snag(e)));
                        return;
                    }
                };
                loop {
                    let first = next_claim.fetch_add(PRECINCTS_PER_CLAIM, Ordering::Relaxed);
                    if first >= groups.len() || abort.load(Ordering::Relaxed) {
                        return;
                    }
                    let last = (first + PRECINCTS_PER_CLAIM).min(groups.len());
                    let claim = PrecinctClaim {
                        groups: &groups[first..last],
                        ..*claims
                    };
                    let result = comb_claim(tile, &mut win, &claim);
                    let failed = result.is_err();
                    if failed {
                        abort.store(true, Ordering::Relaxed);
                    }
                    if sender.send(result).is_err() || failed {
                        return;
                    }
                }
            });
        }
        drop(sender);
        let mut first_error = None;
        for message in receiver {
            match message {
                Ok(results) if first_error.is_none() => {
                    for (prec_bands, packets) in &results {
                        for (i, combed) in packets {
                            if let Err(e) = record(*i as usize, prec_bands, combed) {
                                abort.store(true, Ordering::Relaxed);
                                first_error.get_or_insert(e);
                            }
                        }
                    }
                }
                Ok(_) => {}
                Err(e) => {
                    first_error.get_or_insert(e);
                }
            }
        }
        first_error.map_or(Ok(()), Err)
    })
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

fn io_snag(e: std::io::Error) -> DecodeError {
    DecodeError::Logic(format!("io: {e}"))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::codec::packet_order::prec_position;
    use crate::codec::params::{
        CodingModes, CompCodingStyle, PrecinctSize, ProgressionOrder, ProgressionVolume,
        QuantStyle, SizComponent, SizParams,
    };

    const NUM_LEVELS: u8 = 5;

    fn draft_plan(
        file: &dyn ReadAt,
        hdr: &MainHeaderIn,
        reduce: u8,
        max_layers: u16,
        plt_use: PltUse,
        window: Option<Dims>,
        int16_irreversible: bool,
    ) -> Result<DecodePlan, DecodeError> {
        draft(
            file,
            hdr,
            reduce,
            max_layers,
            plt_use,
            window,
            int16_irreversible,
        )
        .map(|drafted| drafted.plan)
    }

    #[test]
    fn part_one_modes_are_supported_and_ht_is_rejected() {
        const HT_ONLY: u32 = 1 << 6;
        let mut coding = cod();
        coding.comps[0].modes = CodingModes(
            CodingModes::BYPASS
                | CodingModes::RESET
                | CodingModes::TERMALL
                | CodingModes::CAUSAL
                | CodingModes::ERTERM
                | CodingModes::SEGMARK,
        );
        check_coding_style(&coding, 1, 0, "").expect("Part 1 modes are supported");

        coding.comps[0].modes = CodingModes(HT_ONLY);
        let Err(DecodeError::Logic(message)) = check_coding_style(&coding, 1, 0, "") else {
            panic!("HT must be rejected");
        };
        assert!(message.contains("unsupported code-block modes"));
    }

    #[test]
    fn later_layers_merge_continued_segment_lengths() {
        let mut block = BlockRec {
            file_off: 100,
            len: 5,
            num_passes: 9,
            missing_msbs: 2,
            num_segments: 1,
            ..BlockRec::default()
        };
        let crossing = BlockContribution::with_segment_lengths(3, 2, &[7, 11], true);
        append_segment_lengths(&mut block, &crossing).expect("segment lengths merge");
        assert_eq!(block.seg_lens.as_deref(), Some(&[12, 11][..]));
        assert_eq!(block.num_segments, 2);

        block.extra = Some(Box::new(vec![Chunk {
            file_off: 200,
            len: 18,
        }]));
        let next = BlockContribution::with_segment_lengths(1, 2, &[3], false);
        append_segment_lengths(&mut block, &next).expect("new segment appends");
        assert_eq!(block.seg_lens.as_deref(), Some(&[12, 11, 3][..]));
        assert_eq!(block.num_segments, 3);
    }

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
            let Err(DecodeError::Logic(msg)) =
                draft_plan(&empty, &header(), reduce, 0, PltUse::InOrder, None, false)
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
            let Err(DecodeError::Logic(msg)) =
                draft_plan(&empty, &header(), reduce, 0, PltUse::InOrder, None, false)
            else {
                panic!("there is no SOT to walk");
            };
            assert!(!msg.contains("no decomposition level"), "got {msg}");
        }
    }

    #[test]
    fn window_block_storage_uses_the_window_rectangle() {
        const BAND_WIDTH: u32 = 32_640;
        const BAND_HEIGHT: u32 = 32_576;
        const WINDOW_SIZE: u32 = 1_024;
        const BLOCK_SIZE: u32 = 4;
        const WINDOW_BLOCKS: u32 = WINDOW_SIZE / BLOCK_SIZE;

        let rect = window_block_rect(
            d(0, 0, BAND_WIDTH, BAND_HEIGHT),
            d(0, 0, WINDOW_SIZE, WINDOW_SIZE),
            BLOCK_SIZE,
            BLOCK_SIZE,
        );

        assert_eq!(rect.x0, 0);
        assert_eq!(rect.y0, 0);
        assert_eq!(rect.x1, WINDOW_BLOCKS);
        assert_eq!(rect.y1, WINDOW_BLOCKS);
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
            .flatten()
            .flat_map(|t| t.comps.iter().flatten())
            .flat_map(|r| r.bands.iter())
            .flat_map(|b| b.blocks())
            .map(|blk| blk.bolt_len())
            .sum()
    }

    fn block_offsets(plan: &DecodePlan) -> Vec<u64> {
        plan.tiles
            .iter()
            .flatten()
            .flat_map(|t| t.comps.iter().flatten())
            .flat_map(|r| r.bands.iter())
            .flat_map(|b| b.blocks())
            .map(|blk| blk.file_off)
            .collect()
    }

    fn passes(plan: &DecodePlan) -> Vec<u8> {
        plan.tiles
            .iter()
            .flatten()
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
            let plan = draft_plan(&stream, &hdr, 0, keep, PltUse::InOrder, None, false)
                .expect("plan must build");
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
        let full = coded_bytes(
            &draft_plan(&stream, &hdr, 0, LAYERS, PltUse::InOrder, None, false)
                .expect("plan must build"),
        );
        for max_layers in [0, LAYERS + 1, u16::MAX] {
            let plan = draft_plan(&stream, &hdr, 0, max_layers, PltUse::InOrder, None, false)
                .expect("plan must build");
            assert_eq!(coded_bytes(&plan), full, "max_layers {max_layers}");
        }
    }

    /// Layers past the bytes the 8x8 tile has for its two precincts.
    const OVERSIZED_LAYERS: u16 = 10_000;

    /// One byte per packet.
    #[test]
    fn more_declared_packets_than_the_tile_bytes_is_rejected() {
        let stream = synth_stream();
        let mut hdr = synth_header();
        hdr.cod.num_layers = OVERSIZED_LAYERS;
        let Err(DecodeError::Logic(msg)) =
            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false)
        else {
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
        let baseline = draft_plan(&synth_stream(), &hdr, 0, 0, PltUse::InOrder, None, false)
            .expect("baseline must build");
        let plan = draft_plan(
            &zero_psot_stream(),
            &hdr,
            0,
            0,
            PltUse::InOrder,
            None,
            false,
        )
        .expect("plan must build");
        assert_eq!(block_offsets(&plan), block_offsets(&baseline));
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
        assert_eq!(passes(&plan), passes(&baseline));
    }

    /// Bytes of a box following jp2c, which a Psot=0 tile-part must not reach.
    const TRAILING_BYTES: usize = 128;

    #[test]
    fn a_zero_psot_tile_part_stops_at_the_codestream_end() {
        let baseline = draft_plan(
            &synth_stream(),
            &synth_header(),
            0,
            0,
            PltUse::InOrder,
            None,
            false,
        )
        .expect("baseline must build");
        let mut stream = zero_psot_stream();
        let mut hdr = synth_header();
        hdr.codestream_len = stream.len() as u64;
        stream.extend([0x5A; TRAILING_BYTES]);

        let plan =
            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false).expect("plan must build");
        assert_eq!(block_offsets(&plan), block_offsets(&baseline));
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
        assert_eq!(passes(&plan), passes(&baseline));

        // SOT (12) + SOD (2) ahead of the packets, EOC (2) behind them
        let packet_bytes = hdr.codestream_len as usize - 16;
        // one layer past two packets per layer
        hdr.cod.num_layers = ((packet_bytes / 2) + 1) as u16;
        let Err(DecodeError::Logic(msg)) =
            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false)
        else {
            panic!("the trailing bytes must not pad the tile's packet bytes");
        };
        assert!(msg.contains("declared packets"), "got {msg}");
    }

    // issue775.j2k: the last Psot ends 4 bytes short of the codestream end
    #[test]
    fn the_walk_ends_at_bytes_that_are_not_an_sot() {
        let baseline = draft_plan(
            &synth_stream(),
            &synth_header(),
            0,
            0,
            PltUse::InOrder,
            None,
            false,
        )
        .expect("baseline must build");
        for trailing in [4usize, TRAILING_BYTES] {
            let mut stream = synth_stream();
            stream.truncate(stream.len() - 2); // drop the EOC
            stream.extend(std::iter::repeat_n(0x5A, trailing));
            let mut hdr = synth_header();
            hdr.codestream_len = stream.len() as u64;
            let plan = draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false)
                .unwrap_or_else(|e| panic!("{trailing} trailing bytes: {e:?}"));
            assert_eq!(block_offsets(&plan), block_offsets(&baseline), "{trailing}");
            assert_eq!(coded_bytes(&plan), coded_bytes(&baseline), "{trailing}");
            assert_eq!(passes(&plan), passes(&baseline), "{trailing}");
        }
    }

    #[test]
    fn a_stream_cut_inside_the_last_packet_body_is_rejected() {
        let mut stream = synth_stream();
        stream.truncate(stream.len() - 3); // drop the EOC and the last body byte
        let mut hdr = synth_header();
        hdr.codestream_len = stream.len() as u64;
        let Err(DecodeError::Logic(msg)) =
            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false)
        else {
            panic!("a plan must not point a block past the end of the stream");
        };
        assert!(msg.contains("packet stream overrun"), "got {msg}");
    }

    struct CountingFile {
        bytes: Vec<u8>,
        bytes_read: std::sync::atomic::AtomicU64,
        reads: std::sync::atomic::AtomicU64,
    }

    impl ReadAt for CountingFile {
        fn draw_at(&self, buf: &mut [u8], off: u64) -> std::io::Result<()> {
            use std::sync::atomic::Ordering::Relaxed;
            self.bytes_read.fetch_add(buf.len() as u64, Relaxed);
            self.reads.fetch_add(1, Relaxed);
            self.bytes.draw_at(buf, off)
        }

        fn extent(&self) -> std::io::Result<u64> {
            self.bytes.extent()
        }
    }

    fn counting_file(len: usize) -> CountingFile {
        CountingFile {
            bytes: (0..len).map(|i| (i % 251) as u8).collect(),
            bytes_read: Default::default(),
            reads: Default::default(),
        }
    }

    #[test]
    fn sparse_headers_skip_the_packet_bodies() {
        let packet_len = 1 << 20;
        let packets = 8;
        let file = counting_file(packet_len * packets);
        let mut win = StreamWin::warp(&file).unwrap();
        for packet in 0..packets {
            let off = packet * packet_len;
            assert_eq!(win.byte_at(off as u64).unwrap(), (off % 251) as u8);
        }
        let bytes_read = file.bytes_read.load(std::sync::atomic::Ordering::Relaxed);
        assert_eq!(bytes_read, (packets * MIN_WINDOW_READ) as u64);
    }

    #[test]
    fn dense_headers_grow_the_read() {
        let len = 8 * WIN_SIZE;
        let file = counting_file(len);
        let mut win = StreamWin::warp(&file).unwrap();
        for off in 0..len {
            assert_eq!(win.byte_at(off as u64).unwrap(), (off % 251) as u8);
        }
        let reads = file.reads.load(std::sync::atomic::Ordering::Relaxed);
        assert!(reads < 32, "{reads} reads");
        let bytes_read = file.bytes_read.load(std::sync::atomic::Ordering::Relaxed);
        assert_eq!(bytes_read, len as u64);
    }

    #[test]
    fn a_header_byte_past_the_window_is_read_from_the_file() {
        let past = WIN_SIZE + 3;
        let mut file = vec![0u8; past + 1];
        file[0] = 0x11;
        file[WIN_SIZE - 1] = 0x22;
        file[WIN_SIZE] = 0x33;
        file[past] = 0x44;
        let mut win = StreamWin::warp(&file).unwrap();
        assert_eq!(win.byte_at(0).unwrap(), 0x11);
        assert_eq!(win.byte_at((WIN_SIZE - 1) as u64).unwrap(), 0x22);
        assert_eq!(win.byte_at(WIN_SIZE as u64).unwrap(), 0x33);
        assert_eq!(win.byte_at(past as u64).unwrap(), 0x44);
        // still the file byte after the window slid forward
        assert_eq!(win.byte_at(0).unwrap(), 0x11);
        assert!(win.byte_at(file.len() as u64).is_err());
    }

    #[test]
    fn a_packet_header_reads_past_a_quarter_megabyte_and_stops_at_the_tile_part() {
        const AT: usize = (256 << 10) + 7;
        let mut file = vec![0u8; AT + 1];
        file[AT] = 0b1010_0000;
        let mut win = StreamWin::warp(&file).unwrap();
        let mut reader = PacketBitReader::warp(TilePartBytes {
            win: &mut win,
            origin: 0,
            limit: file.len() as u64,
        });
        for _ in 0..AT {
            assert_eq!(reader.pluck_bits(8).unwrap(), 0);
        }
        assert_eq!(reader.pluck_bits(4).unwrap(), 0b1010);

        let mut win = StreamWin::warp(&file).unwrap();
        let mut reader = PacketBitReader::warp(TilePartBytes {
            win: &mut win,
            origin: 0,
            limit: 1,
        });
        assert_eq!(reader.pluck_bits(8).unwrap(), 0);
        assert!(matches!(reader.pluck_bit(), Err(PacketError::Truncated)));
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
        let baseline = draft_plan(&synth_stream(), &hdr, 0, 1, PltUse::InOrder, None, false)
            .expect("baseline must build");
        for use_plt in [false, true] {
            let plt = if use_plt {
                vec![(0, plt_encode(&packet_lengths(&packets)))]
            } else {
                Vec::new()
            };
            let stream = assemble_stream(layer0, &plt);
            let plan = draft_plan(
                &stream,
                &hdr,
                0,
                1,
                if use_plt {
                    PltUse::InOrder
                } else {
                    PltUse::Ignore
                },
                None,
                false,
            )
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
        let baseline = draft_plan(&baseline_stream, &hdr, 0, 1, PltUse::Ignore, None, false)
            .expect("baseline must build");

        let mut packets = clean;
        for garbaged in &mut packets[1..=2] {
            let n = garbaged.len();
            *garbaged = vec![0xFF; n];
        }
        let plt = plt_encode(&packet_lengths(&packets));
        let stream = assemble_stream(&packets, &[(0, plt)]);
        let plan = draft_plan(&stream, &hdr, 0, 1, PltUse::InOrder, None, false)
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

        let baseline = draft_plan(&synth_stream(), &hdr, 0, 0, PltUse::InOrder, None, false)
            .expect("baseline must build");
        let plan =
            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false).expect("plan must build");
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
        assert_eq!(passes(&plan), passes(&baseline));
    }

    /// A window covering the whole image must not change the plan.
    #[test]
    fn full_cover_window_changes_nothing() {
        let stream = synth_stream();
        let hdr = synth_header();
        let full = draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false)
            .expect("baseline must build");
        let windowed = draft_plan(
            &stream,
            &hdr,
            0,
            0,
            PltUse::InOrder,
            Some(d(0, 0, 8, 8)),
            false,
        )
        .expect("windowed plan must build");
        assert_eq!(coded_bytes(&windowed), coded_bytes(&full));
        assert_eq!(passes(&windowed), passes(&full));
        assert_eq!((windowed.width, windowed.height), (full.width, full.height));
        assert!(windowed.tiles[0].is_some());
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
        let plan =
            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false).expect("plan must build");
        assert_eq!(plan.comp_dims[0], d(0, 0, 12, 12));
        assert_eq!(plan.comp_dims[1], d(0, 0, 6, 6));
        assert_eq!((plan.width, plan.height), (12, 12));

        // an odd window origin rounds up onto the subsampled component
        let windowed = draft_plan(
            &stream,
            &hdr,
            0,
            0,
            PltUse::InOrder,
            Some(d(3, 5, 12, 12)),
            false,
        )
        .expect("plan must build");
        assert_eq!(windowed.comp_dims[0], d(3, 5, 12, 12));
        assert_eq!(windowed.comp_dims[1], d(2, 3, 6, 6));
    }

    /// Every block decodes to zero coefficients: this test is about row
    /// counts and widths, which the synthesis produces from geometry alone.
    unsafe fn zero_coder(
        blk: &crate::decode::stripe_decoder::MercuryStripeBlockInfo,
    ) -> Option<*const i32> {
        // JPEG 2000 code-blocks hold at most 4096 samples
        static ZEROS: [i32; 4096] = [0; 4096];
        assert!((blk.num_cols * blk.num_rows) as usize <= ZEROS.len());
        Some(ZEROS.as_ptr())
    }

    #[test]
    fn a_subsampled_decode_emits_each_component_at_its_own_size() {
        use crate::decode::graph::{CompRow, CompRowSink, weave_comps_with_coder};
        use std::sync::{Arc, Mutex};

        let stream = subsampled_stream();
        let hdr = subsampled_header();
        let plan =
            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false).expect("plan must build");
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
        let baseline = draft_plan(&whole, &hdr, 0, 0, PltUse::InOrder, None, false)
            .expect("baseline must build");

        let mut cut = tile_part(0, true);
        cut.extend(tile_part(1, false));
        cut.extend([0xFF, 0xD9]); // EOC where tile 1's packet bytes should be
        let win = Some(d(0, 0, 8, 8));
        let plan = draft_plan(&cut, &hdr, 0, 0, PltUse::InOrder, win, false)
            .expect("tile 1 must never be touched");
        assert!(plan.tiles[1].is_none());
        assert_eq!((plan.width, plan.height), (8, 8));
        // tile 0's records match the whole-image parse
        let tile0_passes = |p: &DecodePlan| -> Vec<u8> {
            p.tiles[0]
                .as_ref()
                .unwrap()
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
            draft_plan(&cut, &hdr, 0, 0, PltUse::InOrder, None, false).is_err(),
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
        let plan = draft_plan(
            &stream,
            &hdr,
            0,
            0,
            PltUse::Ignore,
            Some(d(0, 0, 8, 8)),
            false,
        )
        .expect("empty out-of-window packets must not block the plan");
        assert!(plan.tiles[0].is_some());
        assert!(passes(&plan).iter().any(|&n| n > 0));
    }

    #[test]
    fn plt_hops_nonempty_out_of_window_packets() {
        let (hdr, packets) = tall_rlcp();
        let plt = plt_encode(&packet_lengths(&packets));
        let stream = assemble_stream(&packets, &[(0, plt)]);
        let plan = draft_plan(
            &stream,
            &hdr,
            0,
            0,
            PltUse::InOrder,
            Some(d(0, 0, 8, 8)),
            false,
        )
        .expect("PLT must hop the out-of-window packets");
        assert!(plan.tiles[0].is_some());
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
        let plan = draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false)
            .expect("clamped blocks must plan");
        let baseline = draft_plan(&stream, &synth_header(), 0, 0, PltUse::InOrder, None, false)
            .expect("baseline must build");
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
        let spans: Vec<(u32, u32)> = plan.tiles[0].as_ref().unwrap().comps[0]
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
        let plan =
            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false).expect("plan must build");

        const BLOCKS: usize = 11; // comp 0: 1 + 3, comp 1: 1 + 3 + 3
        assert_eq!(
            coded_bytes(&plan),
            BLOCKS * LAYERS as usize * BODY_LEN as usize
        );
        assert_eq!(passes(&plan), vec![LAYERS as u8; BLOCKS]);

        let comps = &plan.tiles[0].as_ref().unwrap().comps;
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
        let Err(DecodeError::Logic(msg)) =
            draft_plan(&synth_stream(), &hdr, 0, 0, PltUse::InOrder, None, false)
        else {
            panic!("mixed kernels must be rejected");
        };
        assert!(msg.contains("mixed wavelet kernels"), "got {msg}");
    }

    #[test]
    fn a_first_tile_part_poc_replaces_the_main_header_poc() {
        let volume = ProgressionVolume {
            res_s: 0,
            comp_s: 0,
            lay_e: LAYERS,
            res_e: 2,
            comp_e: 1,
            order: ProgressionOrder::Rlcp,
        };
        let packets = vec![
            synth_packet(true, 1),
            synth_packet(false, 1),
            synth_packet(false, 1),
            synth_packet(true, 3),
            synth_packet(false, 3),
            synth_packet(false, 3),
        ];
        let mut hdr = synth_header();
        hdr.cod.pocs.push(ProgressionVolume {
            lay_e: 1,
            order: ProgressionOrder::Lrcp,
            ..volume
        });
        let stream = marked_stream(&[poc_segment(&[volume])], &packets);
        let plan = draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false)
            .expect("tile-part POC must plan");
        let tile = plan.tiles[0].as_ref().expect("tile must be present");

        assert_eq!(tile.cod.pocs.len(), 1);
        assert_eq!(tile.cod.pocs[0].order, ProgressionOrder::Rlcp);
        assert_eq!(tile.cod.pocs[0].lay_e, LAYERS);
        assert_eq!(passes(&plan), vec![LAYERS as u8; BLOCKS_PER_LAYER as usize]);
    }

    #[test]
    fn a_later_tile_part_poc_appends_to_the_first() {
        let first = ProgressionVolume {
            res_s: 0,
            comp_s: 0,
            lay_e: LAYERS,
            res_e: 1,
            comp_e: 1,
            order: ProgressionOrder::Rlcp,
        };
        let second = ProgressionVolume {
            res_s: 1,
            res_e: 2,
            ..first
        };
        let first_packets = vec![
            synth_packet(true, 1),
            synth_packet(false, 1),
            synth_packet(false, 1),
        ];
        let second_packets = vec![
            synth_packet(true, 3),
            synth_packet(false, 3),
            synth_packet(false, 3),
        ];
        let mut stream = tile_part(0, 2, &[poc_segment(&[first])], &first_packets);
        stream.extend(tile_part(1, 2, &[poc_segment(&[second])], &second_packets));
        stream.extend([0xFF, 0xD9]);

        let plan = draft_plan(&stream, &synth_header(), 0, 0, PltUse::InOrder, None, false)
            .expect("later tile-part POC must plan");
        let tile = plan.tiles[0].as_ref().expect("tile must be present");
        assert_eq!(tile.cod.pocs.len(), 2);
        assert_eq!(tile.cod.pocs[0].res_s, 0);
        assert_eq!(tile.cod.pocs[1].res_s, 1);
        assert_eq!(passes(&plan), vec![LAYERS as u8; BLOCKS_PER_LAYER as usize]);

        let limited = draft_plan(&stream, &synth_header(), 0, 1, PltUse::InOrder, None, false)
            .expect("layer-limited tile-part POC must plan");
        assert_eq!(passes(&limited), vec![1; BLOCKS_PER_LAYER as usize]);
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
        let plan = draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false)
            .expect("POC plan must build");

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
        let plan = draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false)
            .expect("clamped POC must plan");
        let mut lrcp = synth_header();
        lrcp.cod.order = ProgressionOrder::Lrcp;
        let baseline = draft_plan(&stream, &lrcp, 0, 0, PltUse::InOrder, None, false)
            .expect("baseline must build");
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
            let Err(DecodeError::Logic(msg)) =
                draft_plan(&synth_stream(), &hdr, 0, 0, PltUse::InOrder, None, false)
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
        let Err(DecodeError::Logic(msg)) = draft_plan(
            &spliced,
            &synth_header(),
            0,
            0,
            PltUse::InOrder,
            None,
            false,
        ) else {
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
        let Err(DecodeError::Logic(msg)) =
            draft_plan(&stream, &synth_header(), 0, 0, PltUse::InOrder, None, false)
        else {
            panic!("a lying PLT must be rejected");
        };
        assert!(msg.contains("PLT length"), "got {msg}");
    }

    /// 64x48 image, 4x4 code blocks, 4x4 LL precincts and 8x8 precincts at
    /// resolution 1: one block per band in each of 96 precincts, more than one
    /// claim, two layers in LRCP order so each precinct's packets are apart.
    fn many_precinct_header() -> MainHeaderIn {
        let mut hdr = synth_header();
        hdr.siz.x_siz = 64;
        hdr.siz.y_siz = 48;
        hdr.siz.xt_siz = 64;
        hdr.siz.yt_siz = 48;
        hdr.cod.num_layers = 2;
        let style = &mut hdr.cod.comps[0];
        style.block_width = 4;
        style.block_height = 4;
        style.precincts = vec![
            PrecinctSize {
                width: 4,
                height: 4,
            },
            PrecinctSize {
                width: 8,
                height: 8,
            },
        ];
        hdr
    }

    const PRECINCTS_PER_RESOLUTION: usize = 48;

    fn many_precinct_packets() -> Vec<Vec<u8>> {
        let mut packets = Vec::new();
        for layer in 0..2 {
            for _ in 0..PRECINCTS_PER_RESOLUTION {
                packets.push(synth_packet(layer == 0, 1));
            }
            for _ in 0..PRECINCTS_PER_RESOLUTION {
                packets.push(synth_packet(layer == 0, 3));
            }
        }
        packets
    }

    fn later_chunks(plan: &DecodePlan) -> Vec<(u64, u32)> {
        plan.tiles
            .iter()
            .flatten()
            .flat_map(|t| t.comps.iter().flatten())
            .flat_map(|r| r.bands.iter())
            .flat_map(|b| b.blocks())
            .flat_map(|blk| blk.extra.iter().flat_map(|chunks| chunks.iter()))
            .map(|chunk| (chunk.file_off, chunk.len))
            .collect()
    }

    #[test]
    fn parallel_plt_parse_matches_the_in_order_parse() {
        let packets = many_precinct_packets();
        let stream = assemble_stream(&packets, &[(0, plt_encode(&packet_lengths(&packets)))]);
        let hdr = many_precinct_header();
        let in_order = draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false)
            .expect("in-order plan must build");
        let parallel = draft_plan(&stream, &hdr, 0, 0, PltUse::Parallel(4), None, false)
            .expect("parallel plan must build");
        let blocks = PRECINCTS_PER_RESOLUTION * 4;
        assert_eq!(coded_bytes(&in_order), 2 * blocks * BODY_LEN as usize);
        assert_eq!(block_offsets(&parallel), block_offsets(&in_order));
        assert_eq!(passes(&parallel), passes(&in_order));
        assert_eq!(later_chunks(&parallel), later_chunks(&in_order));
        assert_eq!(later_chunks(&parallel).len(), blocks);
    }

    #[test]
    fn fetched_plt_parse_matches_the_parallel_parse_from_fetched_bytes_alone() {
        let packets = many_precinct_packets();
        let stream = assemble_stream(&packets, &[(0, plt_encode(&packet_lengths(&packets)))]);
        let hdr = many_precinct_header();
        let window = Some(Dims {
            x0: 0,
            y0: 0,
            x1: 24,
            y1: 16,
        });
        let parallel = draft_plan(&stream, &hdr, 0, 0, PltUse::Parallel(4), window, false)
            .expect("parallel plan must build");
        let Drafted { plan, fetched, .. } = draft(
            &stream,
            &hdr,
            0,
            0,
            PltUse::FetchedParallel {
                threads: 4,
                fetch_threads: 4,
            },
            window,
            false,
        )
        .expect("fetched plan must build");
        assert_eq!(block_offsets(&plan), block_offsets(&parallel));
        assert_eq!(passes(&plan), passes(&parallel));
        assert_eq!(later_chunks(&plan), later_chunks(&parallel));
        let fetched_bytes: usize = fetched.iter().map(|(_, run)| run.len()).sum();
        assert!(
            fetched_bytes < stream.len(),
            "the window skips some packets"
        );
        let fetched_only = FetchedRuns::over(Vec::<u8>::new(), fetched);
        let block_ranges = plan.coded_runs(0);
        assert!(!block_ranges.is_empty());
        for range in block_ranges {
            let mut from_runs = vec![0u8; (range.end - range.start) as usize];
            fetched_only
                .draw_at(&mut from_runs, range.start)
                .expect("block bytes lie in the fetched runs");
            let mut from_stream = vec![0u8; from_runs.len()];
            stream.draw_at(&mut from_stream, range.start).unwrap();
            assert_eq!(from_runs, from_stream);
        }
    }

    fn packets_in_stream_order(hdr: &MainHeaderIn) -> Vec<Vec<u8>> {
        let geom = chart_tile_geom(&hdr.siz, &hdr.cod, 0);
        let grids = chart_precinct_grids(&hdr.cod, &geom);
        let positions = chart_precinct_positions(&hdr.cod, &hdr.siz, &geom, &grids);
        order_packets(&hdr.cod, &grids, &positions)
            .expect("the synthetic header orders its packets")
            .into_iter()
            .map(|slot| synth_packet(slot.layer == 0, if slot.res == 0 { 1 } else { 3 }))
            .collect()
    }

    fn record_fields(rec: &BlockRec) -> (u64, u32, u8, u8, Vec<(u64, u32)>) {
        let chunks = rec
            .extra
            .iter()
            .flat_map(|chunks| chunks.iter())
            .map(|chunk| (chunk.file_off, chunk.len))
            .collect();
        (
            rec.file_off,
            rec.len,
            rec.num_passes,
            rec.missing_msbs,
            chunks,
        )
    }

    #[test]
    fn packet_feeds_publish_the_in_order_records() {
        let orders = [
            ProgressionOrder::Lrcp,
            ProgressionOrder::Rlcp,
            ProgressionOrder::Rpcl,
            ProgressionOrder::Pcrl,
            ProgressionOrder::Cprl,
        ];
        let windows = [None, Some(d(4, 8, 40, 36))];
        let feeds = [
            PltUse::Streamed,
            PltUse::FetchedFeed {
                threads: 4,
                fetch_threads: 2,
            },
        ];
        for order in orders {
            for layers in [1u16, 3] {
                for window in windows {
                    for plt_use in feeds {
                        let mut hdr = many_precinct_header();
                        hdr.cod.order = order;
                        hdr.cod.num_layers = layers;
                        let packets = packets_in_stream_order(&hdr);
                        let stream = assemble_stream(
                            &packets,
                            &[(0, plt_encode(&packet_lengths(&packets)))],
                        );
                        let in_order =
                            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, window, false)
                                .expect("in-order plan must build");
                        let Drafted { plan, feed, .. } =
                            draft(&stream, &hdr, 0, 0, plt_use, window, false)
                                .expect("fed plan must build");
                        match feed.expect("PLT lengths give a fed plan a packet feed") {
                            TileFeed::Streamed(feed) => {
                                let bytes = FilledRuns::over(stream.clone(), feed.runs());
                                feed.run(&bytes);
                            }
                            TileFeed::Fetched(feed) => {
                                let source: Arc<dyn ReadAt> = Arc::new(stream.clone());
                                feed.run(source, &OnceLock::new());
                            }
                        }
                        let case =
                            format!("{order:?}, {layers} layers, window {window:?}, {plt_use:?}");
                        let streamed_bands = plan
                            .tiles
                            .iter()
                            .flatten()
                            .flat_map(|t| t.comps.iter().flatten())
                            .flat_map(|r| r.bands.iter());
                        let in_order_bands = in_order
                            .tiles
                            .iter()
                            .flatten()
                            .flat_map(|t| t.comps.iter().flatten())
                            .flat_map(|r| r.bands.iter());
                        let mut compared = 0usize;
                        for (streamed, expected) in streamed_bands.zip(in_order_bands) {
                            let published = streamed
                                .published_rows()
                                .expect("a fed band has published rows");
                            let wide = expected.stored_blocks_wide as usize;
                            for (row, slot) in published.rows.iter().enumerate() {
                                let records = slot
                                    .get()
                                    .unwrap_or_else(|| panic!("{case}: row {row} unpublished"));
                                let expected_row = &expected.blocks[row * wide..(row + 1) * wide];
                                let got: Vec<_> = records.iter().map(record_fields).collect();
                                let want: Vec<_> = expected_row.iter().map(record_fields).collect();
                                assert_eq!(got, want, "{case}: row {row}");
                                compared += records.iter().filter(|rec| rec.num_passes > 0).count();
                            }
                        }
                        assert!(compared > 0, "{case}: no coded blocks compared");
                    }
                }
            }
        }
    }

    #[test]
    fn packet_feed_rejects_a_stream_cut_before_its_plt_lengths_end() {
        let mut hdr = many_precinct_header();
        let packets = packets_in_stream_order(&hdr);
        let mut stream = assemble_stream(&packets, &[(0, plt_encode(&packet_lengths(&packets)))]);
        stream.truncate(stream.len() / 2);
        hdr.codestream_len = stream.len() as u64;
        let Err(DecodeError::Logic(msg)) =
            draft(&stream, &hdr, 0, 0, PltUse::Streamed, None, false)
        else {
            panic!("PLT lengths past the tile-part data must reject the plan");
        };
        assert!(msg.contains("PLT lengths reach"), "got {msg}");
    }

    #[test]
    fn parallel_plt_parse_rejects_a_lying_plt_length() {
        let packets = many_precinct_packets();
        let mut lengths = packet_lengths(&packets);
        lengths[150] += 1;
        let stream = assemble_stream(&packets, &[(0, plt_encode(&lengths))]);
        let Err(DecodeError::Logic(_)) = draft_plan(
            &stream,
            &many_precinct_header(),
            0,
            0,
            PltUse::Parallel(4),
            None,
            false,
        ) else {
            panic!("a lying PLT must be rejected");
        };
    }

    #[test]
    fn corrupt_plt_is_ignored() {
        let packets = synth_packets();
        let mut data = plt_encode(&packet_lengths(&packets));
        *data.last_mut().unwrap() |= 0x80; // dangling continuation
        let stream = assemble_stream(&packets, &[(0, data)]);
        let hdr = synth_header();

        let baseline = draft_plan(&synth_stream(), &hdr, 0, 0, PltUse::InOrder, None, false)
            .expect("baseline must build");
        let plan = draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false)
            .expect("plan must parse without PLT");
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

    fn poc_segment(volumes: &[ProgressionVolume]) -> Vec<u8> {
        const RECORD_BYTES: u16 = 7;
        let mut segment = vec![0xFF, 0x5F];
        segment.extend((2 + RECORD_BYTES * volumes.len() as u16).to_be_bytes());
        for volume in volumes {
            segment.push(volume.res_s);
            segment.push(volume.comp_s as u8);
            segment.extend(volume.lay_e.to_be_bytes());
            segment.push(volume.res_e);
            segment.push(volume.comp_e as u8);
            segment.push(volume.order as u8);
        }
        segment
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
        plan.tiles[0].as_ref().unwrap().comps[comp]
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
        let plan =
            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false).expect("plan must build");
        assert_eq!(plan.tiles[0].as_ref().unwrap().cod.num_layers, TILE_LAYERS);
        assert_eq!(plan.tiles[0].as_ref().unwrap().cod.comps[0].block_width, 4);
        assert_eq!(plan.tiles[0].as_ref().unwrap().cod.comps[0].block_height, 4);
        let spans: Vec<(u32, u32)> = plan.tiles[0].as_ref().unwrap().comps[0]
            .iter()
            .flat_map(|r| r.bands.iter())
            .map(|b| (b.block_w, b.block_h))
            .collect();
        assert_eq!(spans, vec![(4, 4); 4]);

        // the 4x4 bands hold one code-block either way, so the packets are the
        // main header's stream cut to the tile's layer count
        let baseline = draft_plan(
            &synth_stream(),
            &hdr,
            0,
            TILE_LAYERS,
            PltUse::InOrder,
            None,
            false,
        )
        .expect("baseline must build");
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
        assert_eq!(passes(&plan), passes(&baseline));
    }

    /// Classic caps every tile at the main header's layer count, so a tile-part
    /// COD asking for more decodes the main header's.
    #[test]
    fn a_tile_part_cod_cannot_raise_the_layer_count() {
        let hdr = synth_header();
        let stream = marked_stream(&[cod_segment(LAYERS + 4, 1, 4)], &synth_packets());
        let plan =
            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false).expect("plan must build");
        assert_eq!(plan.tiles[0].as_ref().unwrap().cod.num_layers, LAYERS);
        let baseline = draft_plan(&synth_stream(), &hdr, 0, 0, PltUse::InOrder, None, false)
            .expect("baseline must build");
        assert_eq!(coded_bytes(&plan), coded_bytes(&baseline));
        assert_eq!(passes(&plan), passes(&baseline));
    }

    /// A tile-part QCD replaces every component's quantization, which the band
    /// plans carry as guard bits plus ranging exponent.
    #[test]
    fn a_tile_part_qcd_replaces_the_tile_quantization() {
        let hdr = synth_header();
        let stream = marked_stream(&[quant_segment(0x5C, None, 3, 10)], &synth_packets());
        let plan =
            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false).expect("plan must build");
        assert_eq!(k_max_primes(&plan, 0), vec![3 + 10 - 1; 4]);

        let baseline = draft_plan(&synth_stream(), &hdr, 0, 0, PltUse::InOrder, None, false)
            .expect("baseline must build");
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
        let plan =
            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false).expect("plan must build");

        assert_eq!(plan.tiles[0].as_ref().unwrap().cod.comps[0].num_levels, 1);
        assert_eq!(plan.tiles[0].as_ref().unwrap().cod.comps[0].block_width, 64);
        assert_eq!(plan.tiles[0].as_ref().unwrap().cod.comps[1].num_levels, 2);
        assert_eq!(plan.tiles[0].as_ref().unwrap().cod.comps[1].block_width, 4);
        assert_eq!(plan.tiles[0].as_ref().unwrap().comps[0].len(), 2);
        assert_eq!(plan.tiles[0].as_ref().unwrap().comps[1].len(), 3);

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
        let plan =
            draft_plan(&stream, &hdr, 0, 0, PltUse::InOrder, None, false).expect("plan must build");
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
        let Err(DecodeError::Logic(msg)) =
            draft_plan(&stream, &synth_header(), 0, 0, PltUse::InOrder, None, false)
        else {
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
        let Err(DecodeError::Logic(msg)) =
            draft_plan(&stream, &synth_header(), 0, 0, PltUse::InOrder, None, false)
        else {
            panic!("a tile-part kernel change must be rejected");
        };
        assert!(msg.contains("changes the wavelet kernel"), "got {msg}");
    }

    struct CountingReader {
        bytes: Vec<u8>,
        reads: std::sync::atomic::AtomicU32,
        cached: bool,
    }

    impl ReadAt for CountingReader {
        fn draw_at(&self, buf: &mut [u8], off: u64) -> std::io::Result<()> {
            self.reads
                .fetch_add(1, std::sync::atomic::Ordering::Relaxed);
            self.bytes.draw_at(buf, off)
        }

        fn extent(&self) -> std::io::Result<u64> {
            Ok(self.bytes.len() as u64)
        }

        fn draw_at_if_cached(&self, buf: &mut [u8], off: u64) -> std::io::Result<bool> {
            if !self.cached {
                return Ok(false);
            }
            self.draw_at(buf, off)?;
            Ok(true)
        }
    }

    const SHORT_PART_BYTES: u32 = 40;

    fn sot_chain(tnsot: u8, psot: u32) -> CountingReader {
        let num_tiles = siz().tiles_across() * siz().tiles_down();
        let mut bytes = Vec::new();
        for tile in 0..num_tiles as u16 {
            bytes.extend_from_slice(&[0xFF, 0x90, 0, 10]);
            bytes.extend_from_slice(&tile.to_be_bytes());
            bytes.extend_from_slice(&psot.to_be_bytes());
            bytes.extend_from_slice(&[0, tnsot]);
            bytes.resize(bytes.len() + psot as usize - 12, 0);
        }
        CountingReader {
            bytes,
            reads: std::sync::atomic::AtomicU32::new(0),
            cached: false,
        }
    }

    fn only_tile(tile: usize) -> Vec<bool> {
        let mut needed = vec![false; (siz().tiles_across() * siz().tiles_down()) as usize];
        needed[tile] = true;
        needed
    }

    #[test]
    fn chart_stops_after_the_last_needed_tile() {
        let file = sot_chain(1, SOT_WINDOW_BYTES as u32);
        let parts = chart_tile_parts(&file, &header(), &only_tile(5)).expect("chain charts");
        assert_eq!(
            parts.iter().map(|p| p.tile).collect::<Vec<_>>(),
            (0..=5).collect::<Vec<_>>()
        );
        assert_eq!(file.reads.into_inner(), 6);
    }

    #[test]
    fn chart_reads_short_tile_parts_through_one_window() {
        let file = sot_chain(1, SHORT_PART_BYTES);
        let parts = chart_tile_parts(&file, &header(), &only_tile(5)).expect("chain charts");
        assert_eq!(
            parts.iter().map(|p| p.tile).collect::<Vec<_>>(),
            (0..=5).collect::<Vec<_>>()
        );
        assert_eq!(file.reads.into_inner(), 2);
    }

    #[test]
    fn chart_reads_cached_short_tile_parts_one_sot_at_a_time() {
        let file = CountingReader {
            cached: true,
            ..sot_chain(1, SHORT_PART_BYTES)
        };
        let parts = chart_tile_parts(&file, &header(), &only_tile(5)).expect("chain charts");
        assert_eq!(
            parts.iter().map(|p| p.tile).collect::<Vec<_>>(),
            (0..=5).collect::<Vec<_>>()
        );
        assert_eq!(file.reads.into_inner(), 7);
    }

    #[test]
    fn cursor_marks_the_part_that_completes_each_tile() {
        let completing = |tnsot: u8| {
            let file = sot_chain(tnsot, SHORT_PART_BYTES);
            let mut cursor =
                SotChainCursor::start(&file, &header(), only_tile(5)).expect("chain opens");
            let mut tiles = Vec::new();
            while let Some(charted) = cursor.next_part(&file).expect("chain steps") {
                if charted.completes_tile {
                    tiles.push(charted.part.tile);
                }
            }
            tiles
        };
        assert_eq!(completing(1), (0..=5).collect::<Vec<_>>());
        assert!(completing(0).is_empty());
    }

    #[test]
    fn resumed_cursor_continues_after_the_last_charted_part() {
        let file = sot_chain(1, SOT_WINDOW_BYTES as u32);
        let mut first = SotChainCursor::start(&file, &header(), only_tile(5)).expect("chain opens");
        let mut parts_seen = vec![0u8; only_tile(5).len()];
        while let Some(charted) = first.next_part(&file).expect("chain steps") {
            parts_seen[charted.part.tile as usize] += 1;
        }
        let walk = first.walk();
        assert!(!walk.reached_end);
        let mut resumed = SotChainCursor::resume(&file, &header(), only_tile(9), &walk, parts_seen)
            .expect("chain resumes");
        let mut tiles = Vec::new();
        while let Some(charted) = resumed.next_part(&file).expect("chain steps") {
            tiles.push(charted.part.tile);
        }
        assert_eq!(tiles, (6..=9).collect::<Vec<_>>());
        assert_eq!(file.reads.into_inner(), 10);
    }

    #[test]
    fn chart_walks_every_tile_part_when_tnsot_is_unknown() {
        let file = sot_chain(0, SHORT_PART_BYTES);
        let parts = chart_tile_parts(&file, &header(), &only_tile(5)).expect("chain charts");
        assert_eq!(
            parts.len(),
            (siz().tiles_across() * siz().tiles_down()) as usize
        );
    }

    static PATTERNED_BLOCKS_DECODED: std::sync::atomic::AtomicUsize =
        std::sync::atomic::AtomicUsize::new(0);

    unsafe fn patterned_coder(
        blk: &crate::decode::stripe_decoder::MercuryStripeBlockInfo,
    ) -> Option<*const i32> {
        thread_local! {
            static SAMPLES: std::cell::UnsafeCell<[i32; 4096]> =
                const { std::cell::UnsafeCell::new([0; 4096]) };
        }
        PATTERNED_BLOCKS_DECODED.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        let (cols, rows) = (blk.num_cols as usize, blk.num_rows as usize);
        SAMPLES.with(|cell| {
            let samples = unsafe { &mut *cell.get() };
            for r in 0..rows {
                for c in 0..cols {
                    let x = blk.dst_offset as usize + c;
                    let value = (x * 7 + r * 13 + blk.orientation as usize * 5) % 31;
                    samples[r * cols + c] = 2 * (value as i32 - 15);
                }
            }
            Some(cell.get() as *const i32)
        })
    }

    fn strip_test_header() -> MainHeaderIn {
        let mut hdr = synth_header();
        hdr.siz.x_siz = 4096;
        hdr.siz.y_siz = 64;
        hdr.siz.xt_siz = 4096;
        hdr.siz.yt_siz = 64;
        hdr.cod.num_layers = 1;
        let style = &mut hdr.cod.comps[0];
        style.num_levels = 2;
        style.block_width = 16;
        style.block_height = 16;
        let precinct = |side| crate::codec::params::PrecinctSize {
            width: side,
            height: side,
        };
        // one code-block per band per precinct, so every packet is a synth_packet
        style.precincts = vec![precinct(16), precinct(32), precinct(32)];
        hdr.qcd.ranges = vec![8; 7];
        hdr
    }

    fn strip_test_stream() -> Vec<u8> {
        let mut packets = Vec::new();
        packets.extend((0..64).map(|_| synth_packet(true, 1)));
        packets.extend((0..64).map(|_| synth_packet(true, 3)));
        packets.extend((0..256).map(|_| synth_packet(true, 3)));
        assemble_stream(&packets, &[])
    }

    fn decode_rows(stream: &[u8], plan: DecodePlan) -> Vec<Vec<i64>> {
        use crate::decode::graph::{RowSink, Rows, weave_with_coder};
        use std::sync::{Arc, Mutex};

        let rows: Arc<Mutex<Vec<Vec<i64>>>> = Arc::new(Mutex::new(Vec::new()));
        let sink_rows = Arc::clone(&rows);
        let sink: RowSink = Box::new(move |row, samples| {
            let values: Vec<i64> = match samples {
                Rows::I16(r) | Rows::Q13(r) => r[0].iter().map(|&v| v.into()).collect(),
                Rows::I32(r) => r[0].iter().map(|&v| v.into()).collect(),
                Rows::F32(r) => r[0].iter().map(|&v| v.to_bits().into()).collect(),
            };
            let mut got = sink_rows.lock().unwrap();
            assert_eq!(got.len() as u32, row, "rows arrive in order");
            got.push(values);
        });
        weave_with_coder(Arc::new(stream.to_vec()), plan, 4, sink, patterned_coder)
            .expect("decode must run");
        Arc::try_unwrap(rows).unwrap().into_inner().unwrap()
    }

    fn decode_segment_rows(stream: &[u8], plan: DecodePlan) -> Vec<Vec<i64>> {
        use crate::decode::graph::{Rows, SegmentSink, weave_segments_with_coder};
        use std::sync::{Arc, Mutex};

        let width = plan.comp_dims[0].width() as usize;
        let height = plan.comp_dims[0].height() as usize;
        let rows = Arc::new(Mutex::new(vec![vec![None::<i64>; width]; height]));
        let reported = Arc::new(Mutex::new(Vec::new()));
        let written_rows = Arc::clone(&rows);
        let done_rows = Arc::clone(&rows);
        let done_reported = Arc::clone(&reported);
        let sink = SegmentSink {
            write: Box::new(move |row, x0, samples| {
                let values: Vec<i64> = match samples {
                    Rows::I16(rows) | Rows::Q13(rows) => {
                        rows[0].iter().map(|&value| value.into()).collect()
                    }
                    Rows::I32(rows) => rows[0].iter().map(|&value| value.into()).collect(),
                    Rows::F32(rows) => rows[0]
                        .iter()
                        .map(|&value| value.to_bits().into())
                        .collect(),
                };
                let mut got = written_rows.lock().unwrap();
                for (column, value) in values.into_iter().enumerate() {
                    let previous = got[row as usize][x0 + column].replace(value);
                    assert!(
                        previous.is_none(),
                        "row {row} column {} arrives once",
                        x0 + column
                    );
                }
            }),
            rows_done: Box::new(move |done| {
                let got = done_rows.lock().unwrap();
                assert!(
                    got[..done as usize].iter().flatten().all(Option::is_some),
                    "rows below {done} are written"
                );
                done_reported.lock().unwrap().push(done);
            }),
            admit: None,
        };
        weave_segments_with_coder(Arc::new(stream.to_vec()), plan, 4, sink, patterned_coder)
            .expect("decode must run");
        let reported = reported.lock().unwrap();
        assert!(reported.windows(2).all(|pair| pair[0] < pair[1]));
        assert_eq!(reported.last().copied(), Some(height as u32));
        let rows = rows.lock().unwrap();
        rows.iter()
            .map(|row| {
                row.iter()
                    .map(|v| v.expect("every sample arrives"))
                    .collect()
            })
            .collect()
    }

    #[test]
    fn column_strips_decode_each_block_once_and_emit_the_same_rows() {
        use std::sync::atomic::Ordering;

        let stream = strip_test_stream();
        let hdr = strip_test_header();
        let cases = [
            (0u8, None, Some(1024)),
            (1, None, Some(256)),
            (0, Some(d(301, 5, 3901, 61)), None),
        ];
        for (reduce, window, expected_blocks) in cases {
            let decode = |max_strips: u32| {
                let mut plan = draft_plan(&stream, &hdr, reduce, 0, PltUse::InOrder, window, false)
                    .expect("plan must build");
                plan.max_strips = max_strips;
                PATTERNED_BLOCKS_DECODED.store(0, Ordering::Relaxed);
                let rows = decode_rows(&stream, plan);
                (PATTERNED_BLOCKS_DECODED.load(Ordering::Relaxed), rows)
            };
            let (baseline_blocks, baseline_rows) = decode(1);
            if let Some(expected) = expected_blocks {
                assert_eq!(baseline_blocks, expected, "reduce {reduce}");
            }
            assert!(
                baseline_rows
                    .iter()
                    .flatten()
                    .any(|&v| v != baseline_rows[0][0])
            );
            for max_strips in [3, 4] {
                let (blocks, rows) = decode(max_strips);
                assert_eq!(
                    blocks, baseline_blocks,
                    "reduce {reduce} window {window:?} strips {max_strips}"
                );
                assert!(
                    rows == baseline_rows,
                    "reduce {reduce} window {window:?} strips {max_strips}"
                );
            }
            for max_strips in [1, 3, 4] {
                let mut plan = draft_plan(&stream, &hdr, reduce, 0, PltUse::InOrder, window, false)
                    .expect("plan must build");
                plan.max_strips = max_strips;
                assert!(
                    decode_segment_rows(&stream, plan) == baseline_rows,
                    "segments reduce {reduce} window {window:?} strips {max_strips}"
                );
            }
        }
    }
}
