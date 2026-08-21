//! The weft decode graph: SubbandDecode nodes (T1) feeding per-level
//! synthesis nodes (DWT) feeding a tile-row merge sink, connected by bounded
//! SPSC rings and driven by the weft runtime (docs/weft-design.md §2).
//!
//!   [SubbandDecode c0 r0/LL]  ──ring──► [Level 0]
//!   [SubbandDecode c0 r1/HL..HH] ─ring─►    │ LL ring
//!                                        [Level 1] ... [Level top c0] ─┐
//!   [SubbandDecode c1 ...]  ─ ─ ─ ─ ─ ─►  ...  ... [Level top c1] ────┼─► [Merge]
//!
//! Every node is a non-blocking slice; ring capacity is the backpressure.
//! T1 decodes one block-row (up to 64 subband rows) per pass directly into
//! ring slots. Each resolution level is its own node, consuming leaf subband
//! rows (HL/LH/HH, plus LL at level 0) from the T1 rings and LL rows from the
//! level below through a small ring, emitting synthesized rows to the level
//! above (top level emits full-width tile rows). The merge sink joins
//! components (and tile columns), applies the inverse RCT when signalled, and
//! hands rows to a caller callback.
//!
//! Independent level nodes make cross-level scheduling the runtime's job: the
//! tug protocol reschedules a peer whenever a level frees its child's ring
//! space or fills its parent's input — what the old in-node tree sweep did by
//! hand.
//!
//! Multi-tile images decode one tile ROW at a time: all tiles of a row run
//! concurrently (output rows interleave); rows run in sequence, each a fresh
//! graph. Peak memory is one tile row's rings.

#![allow(unsafe_op_in_unsafe_fn)]

use crate::decode::ReadAt;
use std::sync::{Arc, Mutex};

use crate::codec::tile_geom::Dims;
use crate::decode::DecodeError;
use crate::decode::plan::{BandPlan, DecodePlan, TilePlan};
use crate::decode::stripe_decoder::{
    BlockCoder, MercuryStripeBlockInfo, mercury_weave_stripe_f32, mercury_weave_stripe16,
    mercury_weave_stripe32,
};
use crate::decode::window::{TileCompWindow, band_window, intersect};
use crate::dwt::level_builder::{BandDims, LevelSpec, warp_w5x3_prec, warp_w9x7};
use crate::dwt::synthesis::SamplePrec;
use crate::dwt::synthesis::{AlignedVec, PullStatus, RowSource, Synthesis};
use crate::weft::Node;
use crate::weft::ring::{Consumer, Producer, spin};
use crate::weft::rt::{Builder, Ctx, NodeId, Runtime};

/// Ring capacity in rows per subband: two block-row stripes so T1 always has
/// a full stripe of working room ahead of the tree (1.5 stripes saves ~6 MB
/// on ESP but costs ~1 s wall from producer stalls).
fn band_ring_picks(block_h: u32, band_h: u32) -> usize {
    (2 * block_h)
        .min(band_h.next_multiple_of(block_h))
        .max(block_h) as usize
}

/// Rows buffered between each tree and the merge sink: one full T1 block-row
/// stripe (64 rows). At 32, every stripe needed two tree→sink hand-offs, the
/// extra stall/wake cycles costing ~0.25 s on ESP for ~1.8 MB saved.
const OUT_RING_ROWS: usize = 64;

/// Sample representation of the dataflow, chosen per image.
#[derive(Clone, Copy, PartialEq)]
enum Path {
    /// Reversible, band coefficients fit 15 bits + sign.
    I16,
    /// Reversible, high precision.
    I32,
    /// Irreversible 9/7: f32 in normalized units.
    F32,
}

impl Path {
    fn fibre_bytes(self) -> usize {
        match self {
            Path::I16 => 2,
            Path::I32 | Path::F32 => 4,
        }
    }
}

// ============================================================================
// SubbandDecode node
// ============================================================================

/// Decodes a column slice of one subband's code-blocks stripe by stripe
/// (block rows) into its output ring. Block bytes are `pread` on demand.
///
/// Parallelism: a band is split into `[bx0, bx0+nbw)` block-column slices,
/// each an independent node with its own ring of partial rows, gathered by
/// the tree's leaf source. Slices share the `BandPlan` via `Arc` and never
/// write overlapping samples.
///
/// The code-block grid is anchored at the canvas origin: when the band's
/// x0/y0 aren't block-aligned (tiled images), the first block column/row is
/// partial.
pub struct SubbandDecodeNode {
    file: Arc<dyn ReadAt>,
    band: Arc<BandPlan>,
    /// First block column and number of block columns of this slice.
    bx0: u32,
    nbw: u32,
    block_w: u32,
    block_h: u32,
    modes: i32,
    path: Path,
    coder: BlockCoder,
    out: Producer<AlignedVec>,
    consumer: NodeId,
    next_block_row: u32,
    /// One past the last block row to decode (windowed decodes stop early).
    block_row_hi: u32,
    /// Band-local sample row range to publish; whole blocks are decoded, rows
    /// outside the range land in spill buffers and are dropped.
    clip_lo: u32,
    clip_hi: u32,
    /// Spill rows for clipped-out stripe rows, sized like ring slots.
    spill: Vec<AlignedVec>,
    slot_bytes: usize,
    scratch: Vec<u8>,
}

/// Sample rows covered by block-row `row` of `band` (canvas-anchored).
fn block_weft_span(band: &BandPlan, block_h: u32, row: u32) -> (u32, u32) {
    let first_by = band.y0 / block_h;
    let r0 = ((first_by + row) * block_h).max(band.y0);
    let r1 = ((first_by + row + 1) * block_h).min(band.y0 + band.height);
    (r0 - band.y0, r1 - band.y0)
}

/// Band-local sample columns covered by block columns `[bx0, bx0+nbw)`.
fn block_warp_span(band: &BandPlan, block_w: u32, bx0: u32, nbw: u32) -> (usize, usize) {
    let first_bx = band.x0 / block_w;
    let c0 = ((first_bx + bx0) * block_w).max(band.x0);
    let c1 = ((first_bx + bx0 + nbw) * block_w).min(band.x0 + band.width);
    ((c0 - band.x0) as usize, (c1 - band.x0) as usize)
}

impl SubbandDecodeNode {
    /// Decode the `[bx0, bx0+nbw)` block-column slice of block-row `row`
    /// into `dst_lines` (one pointer per stripe row, each with room for the
    /// slice's samples; sample 0 = the slice's first sample column).
    ///
    /// # Safety
    /// `dst_lines` pointers must be valid for the slice width in samples.
    unsafe fn weave_block_row(
        file: &dyn ReadAt,
        band: &BandPlan,
        block_w: u32,
        block_h: u32,
        modes: i32,
        row: u32,
        bx0: u32,
        nbw: u32,
        path: Path,
        coder: BlockCoder,
        dst_lines: &[*mut u8],
        scratch: &mut Vec<u8>,
    ) -> Result<(), DecodeError> {
        let bw = band.blocks_wide;
        let stripe_rows = dst_lines.len() as i32;
        let recs = &band.blocks[(row * bw + bx0) as usize..(row * bw + bx0 + nbw) as usize];

        // Gather coded bytes for the whole block row. Multi-layer blocks
        // concatenate their per-layer chunks into one stream.
        let total: usize = recs.iter().map(|r| r.bolt_len()).sum();
        scratch.clear();
        scratch.resize(total, 0);
        let mut off = 0usize;
        let mut offsets = Vec::with_capacity(recs.len());
        let rd = |scratch: &mut [u8], off: usize, foff: u64, len: usize| {
            file.draw_at(&mut scratch[off..off + len], foff)
                .map_err(|e| DecodeError::Logic(format!("block read: {e}")))
        };
        for rec in recs {
            offsets.push(off);
            if rec.len > 0 {
                rd(scratch, off, rec.file_off, rec.len as usize)?;
                off += rec.len as usize;
            }
            if let Some(extra) = &rec.extra {
                for ch in extra.iter() {
                    rd(scratch, off, ch.file_off, ch.len as usize)?;
                    off += ch.len as usize;
                }
            }
        }

        let (row_a, row_b) = block_weft_span(band, block_h, row);
        let num_rows = (row_b - row_a) as i32;
        let (slice_c0, _) = block_warp_span(band, block_w, bx0, nbw);

        // Segment-length storage must outlive the FFI call; inner Vec heap
        // buffers are stable even as the outer Vec grows.
        let mut seg_storage: Vec<Vec<i32>> = Vec::new();
        let mut infos: Vec<MercuryStripeBlockInfo> = Vec::with_capacity(recs.len());
        for (i, rec) in recs.iter().enumerate() {
            let bx = bx0 + i as u32; // band-global block column
            let (c0, c1) = block_warp_span(band, block_w, bx, 1);
            let num_cols = (c1 - c0) as i32;
            let seg_ptr = if rec.num_segments > 1 {
                let lens = rec
                    .seg_lens
                    .as_deref()
                    .expect("multi-segment without table");
                seg_storage.push(lens.iter().map(|&l| l as i32).collect());
                seg_storage.last().unwrap().as_ptr()
            } else {
                std::ptr::null()
            };
            infos.push(MercuryStripeBlockInfo {
                coded_data: unsafe { scratch.as_ptr().add(offsets[i]) },
                coded_length: rec.bolt_len() as i32,
                num_passes: rec.num_passes as i32,
                missing_msbs: rec.missing_msbs as i32,
                k_max_prime: band.k_max_prime,
                orientation: band.band_type as i32,
                modes,
                num_cols,
                num_rows,
                dst_offset: (c0 - slice_c0) as i32, // slice-relative
                segment_lengths: seg_ptr,
                num_segments: rec.num_segments.max(1) as i32,
            });
        }

        let ok = unsafe {
            match path {
                Path::F32 => mercury_weave_stripe_f32(
                    infos.as_ptr(),
                    infos.len() as i32,
                    dst_lines.as_ptr() as *const *mut f32,
                    stripe_rows,
                    band.k_max_prime,
                    band.delta,
                    coder,
                ),
                Path::I32 => mercury_weave_stripe32(
                    infos.as_ptr(),
                    infos.len() as i32,
                    dst_lines.as_ptr() as *const *mut i32,
                    stripe_rows,
                    band.k_max_prime,
                    coder,
                ),
                Path::I16 => mercury_weave_stripe16(
                    infos.as_ptr(),
                    infos.len() as i32,
                    dst_lines.as_ptr() as *const *mut i16,
                    stripe_rows,
                    band.k_max_prime,
                    coder,
                ),
            }
        };
        if !ok {
            return Err(DecodeError::Logic(format!(
                "T1 failed in band type {} block row {row}",
                band.band_type
            )));
        }
        Ok(())
    }

    /// Rows of block row `row` that fall inside the publish range.
    fn picks_in_block_row(&self, row: u32) -> usize {
        let (a, b) = block_weft_span(&self.band, self.block_h, row);
        (b.min(self.clip_hi).saturating_sub(a.max(self.clip_lo))) as usize
    }
}

impl Node for SubbandDecodeNode {
    fn shuttle(&mut self, ctx: &mut Ctx<'_>) {
        let mut produced = false;
        while self.next_block_row < self.block_row_hi {
            let (row_a, row_b) = block_weft_span(&self.band, self.block_h, self.next_block_row);
            let picks = self.picks_in_block_row(self.next_block_row);
            if self.out.slack() < picks {
                break;
            }
            // Whole blocks are decoded; rows outside the publish range go to
            // spill buffers the ring never sees.
            let spilled = (row_b - row_a) as usize - picks;
            while self.spill.len() < spilled {
                self.spill.push(AlignedVec::bare(self.slot_bytes));
            }
            let mut ring_i = 0usize;
            let mut spill_i = 0usize;
            let mut dst_lines: Vec<*mut u8> = Vec::with_capacity((row_b - row_a) as usize);
            for r in row_a..row_b {
                if r >= self.clip_lo && r < self.clip_hi {
                    dst_lines.push(self.out.pick(ring_i).as_mut_ptr());
                    ring_i += 1;
                } else {
                    dst_lines.push(self.spill[spill_i].as_mut_ptr());
                    spill_i += 1;
                }
            }
            let decoded = unsafe {
                Self::weave_block_row(
                    &*self.file,
                    &self.band,
                    self.block_w,
                    self.block_h,
                    self.modes,
                    self.next_block_row,
                    self.bx0,
                    self.nbw,
                    self.path,
                    self.coder,
                    &dst_lines,
                    &mut self.scratch,
                )
            };
            if let Err(e) = decoded {
                // rows in this pass are unusable, so don't hand them on
                ctx.fail(format!("subband decode failed: {e:?}"));
                return;
            }
            self.out.beat(picks);
            self.next_block_row += 1;
            produced = true;
        }
        if produced {
            ctx.tug(self.consumer);
        }
        if self.next_block_row >= self.block_row_hi {
            // Done: free the pread scratch and spill rows.
            self.scratch = Vec::new();
            self.spill = Vec::new();
        }
    }
}

// ============================================================================
// Synthesis level node
// ============================================================================

/// Rows buffered between a child level and its parent's LL input. The 9/7
/// engines look several LL rows ahead per output row (four lifting steps),
/// so a shallow ring forces a scheduler round-trip per row; 8 rows lets a
/// child slice run ahead a batch at a time (~4.5% wall on the PHR1B 9/7 gate
/// vs 2 rows, for well under 1 MB per chain — lower-level rows halve in width
/// each step down).
const LL_RING_ROWS: usize = 8;

/// One column slice of a leaf subband: its ring, producing node, and where
/// its samples land within the engine's band row. On windowed decodes a slice
/// holds whole decoded blocks, so `src_off` skips the samples left of the
/// band window and `byte_len` is clipped to it.
struct LeafSlice {
    cons: Consumer<AlignedVec>,
    producer: NodeId,
    byte_off: usize,
    byte_len: usize,
    src_off: usize,
}

/// Input source of one level node: leaf subband rows gathered from the T1
/// slice rings, plus (above level 0) LL rows from the child level's ring.
/// A leaf row is available once EVERY slice has published it; `fill` copies
/// each slice segment, releases the slots, and marks the producers dirty so
/// the node can tug them.
struct LevelInputs {
    /// slices[band_type] — level 0 uses index 0 (LL) plus 1..3; higher
    /// levels use 1..3 only (LL comes from the child ring).
    slices: Vec<Vec<LeafSlice>>,
    fibre_bytes: usize,
    dirty: Vec<NodeId>,
    /// LL rows from the level below (None at level 0).
    ll: Option<Consumer<AlignedVec>>,
    /// Child level node to tug when LL slots are released.
    ll_producer: NodeId,
    ll_released: bool,
    /// Windowed decode: child rows start above this level's LL window, so the
    /// first `ll_skip_rows` are drained unread, and each kept row is read from
    /// `ll_src_off` bytes in (the child's window starts left of this one's).
    ll_skip_rows: u32,
    ll_src_off: usize,
}

impl RowSource for LevelInputs {
    fn subband_ready(&self, sb: i32) -> bool {
        if sb == 0 {
            if let Some(ll) = &self.ll {
                return self.ll_skip_rows == 0 && ll.picks_ready() > 0;
            }
        }
        let slices = &self.slices[sb as usize];
        !slices.is_empty() && slices.iter().all(|s| s.cons.picks_ready() > 0)
    }
    unsafe fn pack(&mut self, sb: i32, buf: *mut u8, width: i32) {
        if sb == 0 {
            if let Some(ll) = self.ll.as_mut() {
                let row = ll.peek(0);
                let bytes = (width as usize) * self.fibre_bytes;
                debug_assert!(self.ll_src_off + bytes <= row.len());
                unsafe {
                    std::ptr::copy_nonoverlapping(row.as_ptr().add(self.ll_src_off), buf, bytes)
                };
                ll.unwind(1);
                self.ll_released = true;
                return;
            }
        }
        let slices = &mut self.slices[sb as usize];
        debug_assert_eq!(
            slices.iter().map(|s| s.byte_len).sum::<usize>(),
            (width as usize) * self.fibre_bytes
        );
        for s in slices.iter_mut() {
            let row = s.cons.peek(0);
            debug_assert!(s.src_off + s.byte_len <= row.len());
            unsafe {
                std::ptr::copy_nonoverlapping(
                    row.as_ptr().add(s.src_off),
                    buf.add(s.byte_off),
                    s.byte_len,
                )
            };
            s.cons.unwind(1);
            if !self.dirty.contains(&s.producer) {
                self.dirty.push(s.producer);
            }
        }
    }
}

/// One resolution level of one component: pulls synthesized rows from its
/// engine into the output ring (the parent level's LL input, or the merge
/// sink for the top level).
pub struct LevelNode {
    engine: Synthesis,
    inputs: LevelInputs,
    out: Producer<AlignedVec>,
    /// Parent level node, or the sink for the top level.
    consumer: NodeId,
    rows_out: u32,
    rows_total: u32,
}

impl Node for LevelNode {
    fn shuttle(&mut self, ctx: &mut Ctx<'_>) {
        // Windowed decode: the child level's window starts above this one's,
        // so its leading rows drain unread.
        while self.inputs.ll_skip_rows > 0 {
            let Some(ll) = self.inputs.ll.as_mut() else {
                break;
            };
            if ll.picks_ready() == 0 {
                break;
            }
            ll.unwind(1);
            self.inputs.ll_released = true;
            self.inputs.ll_skip_rows -= 1;
        }
        let mut produced = false;
        while self.rows_out < self.rows_total && self.out.slack() > 0 {
            let status = unsafe {
                let slot = self.out.pick(0).as_mut_ptr();
                self.engine.try_draw(slot, &mut self.inputs)
            };
            match status {
                PullStatus::Row => {
                    self.out.beat(1);
                    self.rows_out += 1;
                    produced = true;
                }
                PullStatus::Stalled => break,
            }
        }
        if !produced && self.rows_out == 0 && std::env::var("WEFT_DEBUG").is_ok() {
            let av: Vec<Vec<usize>> = self
                .inputs
                .slices
                .iter()
                .map(|sl| sl.iter().map(|s| s.cons.picks_ready()).collect())
                .collect();
            eprintln!(
                "  level={:p} stalled at 0 rows: {} leaf avail {av:?} ll avail {:?}",
                self as *const _,
                self.engine.debug_warp(),
                self.inputs.ll.as_ref().map(|c| c.picks_ready())
            );
        }
        // Wake producers whose rings we drained, and the consumer if we produced.
        for id in self.inputs.dirty.drain(..) {
            ctx.tug(id);
        }
        if self.inputs.ll_released {
            self.inputs.ll_released = false;
            ctx.tug(self.inputs.ll_producer);
        }
        if produced {
            ctx.tug(self.consumer);
        }
    }
}

// ============================================================================
// Merge-sink node
// ============================================================================

/// One image row, one slice per component (raw signed samples, before DC
/// level shift). I16 for images whose coefficients fit the short path,
/// I32 for high-precision images.
pub enum Rows<'a> {
    I16(&'a [&'a [i16]]),
    I32(&'a [&'a [i32]]),
    /// Irreversible path: normalized floats (sample / 2^precision,
    /// signed around 0). Convert with v = (fval·2^prec + 2^(prec−1)) as int.
    F32(&'a [&'a [f32]]),
}

/// Receives each full-image-width row exactly once, in order. When the
/// codestream signals the reversible color transform, components 0-2
/// arrive as R,G,B.
pub type RowSink = Box<dyn FnMut(u32, Rows<'_>) + Send>;

/// One tile column's tree outputs feeding the merge sink.
struct ColInput {
    /// Per-component consumers (tree output rings).
    inputs: Vec<Consumer<AlignedVec>>,
    /// Per-component tree node ids.
    producers: Vec<NodeId>,
    /// Output-relative sample column of this tile's first emitted sample.
    x0: usize,
    width: usize,
    /// Windowed decode: the tree's rows start left of the emitted window;
    /// samples are read from this offset in.
    src_off: usize,
}

/// Joins the per-component (and per-tile-column) tree rings row by row,
/// applies the inverse reversible color transform (RCT) to components 0-2
/// when signalled, and hands the assembled component rows to the caller.
struct MergeSinkNode<T> {
    cols: Vec<ColInput>,
    /// Full output width in samples (the window's when one is set).
    width: usize,
    ycc: bool,
    /// Output row of this tile row's first emitted row.
    row_base: u32,
    rows_seen: u32,
    /// Windowed decode: leading tree rows above the output window, drained
    /// without emitting.
    discard: u32,
    /// Assembled rows, [component][output width].
    assembled: Vec<Vec<T>>,
    cb: Arc<Mutex<RowSink>>,
}

/// Sample type of the weft dataflow: i16/i32 (reversible) or f32 (9/7).
trait Sample: Copy + Send + Default + 'static {
    fn skein<'a>(refs: &'a [&'a [Self]]) -> Rows<'a>;
    /// In-place inverse color transform on components 0-2; leaves R,G,B in
    /// (c0, c1, c2). RCT for the integer paths, ICT for the float path.
    fn untangle_color(c0: &mut [Self], c1: &mut [Self], c2: &mut [Self]);
}

macro_rules! impl_rct_sample {
    ($t:ty, $wide:ty, $wrap:path) => {
        impl Sample for $t {
            fn skein<'a>(refs: &'a [&'a [$t]]) -> Rows<'a> {
                $wrap(refs)
            }
            fn untangle_color(y: &mut [$t], u: &mut [$t], v: &mut [$t]) {
                for x in 0..y.len() {
                    let g = y[x] as $wide - ((u[x] as $wide + v[x] as $wide) >> 2);
                    let r = (v[x] as $wide + g) as $t;
                    let b = (u[x] as $wide + g) as $t;
                    y[x] = r;
                    u[x] = g as $t;
                    v[x] = b;
                }
            }
        }
    };
}
impl_rct_sample!(i16, i32, Rows::I16);
impl_rct_sample!(i32, i64, Rows::I32);

impl Sample for f32 {
    fn skein<'a>(refs: &'a [&'a [f32]]) -> Rows<'a> {
        Rows::F32(refs)
    }
    /// Inverse ICT (irreversible YCbCr→RGB): f32 FMA, G from Cr then Cb.
    fn untangle_color(y: &mut [f32], cb: &mut [f32], cr: &mut [f32]) {
        const CR_FACT_R: f32 = (2.0 * (1.0 - 0.299)) as f32;
        const CB_FACT_B: f32 = (2.0 * (1.0 - 0.114)) as f32;
        const CR_FACT_G: f32 = (2.0 * 0.299 * (1.0 - 0.299) / (1.0 - (0.299 + 0.114))) as f32;
        const CB_FACT_G: f32 = (2.0 * 0.114 * (1.0 - 0.114) / (1.0 - (0.299 + 0.114))) as f32;
        for x in 0..y.len() {
            let (yv, cbv, crv) = (y[x], cb[x], cr[x]);
            let green = crv.mul_add(-CR_FACT_G, yv);
            y[x] = crv.mul_add(CR_FACT_R, yv);
            cb[x] = cbv.mul_add(-CB_FACT_G, green);
            cr[x] = cbv.mul_add(CB_FACT_B, yv);
        }
    }
}

impl<T: Sample> Node for MergeSinkNode<T> {
    fn shuttle(&mut self, ctx: &mut Ctx<'_>) {
        let mut n = self
            .cols
            .iter()
            .flat_map(|col| col.inputs.iter().map(|c| c.picks_ready()))
            .min()
            .unwrap_or(0);
        if n == 0 {
            return;
        }
        // Windowed decode: rows above the output window drain unemitted.
        if self.discard > 0 {
            let d = (self.discard as usize).min(n);
            for col in &mut self.cols {
                for c in &mut col.inputs {
                    c.unwind(d);
                }
            }
            self.discard -= d as u32;
            n -= d;
            if n == 0 {
                for col in &self.cols {
                    for &p in &col.producers {
                        ctx.tug(p);
                    }
                }
                return;
            }
        }
        // Zero-copy fast path: one tile column, no color transform — hand
        // the ring rows straight to the callback (the ESP 10 s gate rides
        // on not copying 3 GB of rows here). A window crop is a subslice,
        // still zero-copy.
        if self.cols.len() == 1 && !self.ycc {
            let col = &self.cols[0];
            debug_assert_eq!(col.width, self.width);
            let mut refs: Vec<&[T]> = Vec::with_capacity(col.inputs.len());
            let mut cb = self.cb.lock().unwrap();
            for i in 0..n {
                refs.clear();
                refs.extend(col.inputs.iter().map(|c| {
                    let row = c.peek(i);
                    unsafe {
                        std::slice::from_raw_parts(
                            (row.as_ptr() as *const T).add(col.src_off),
                            col.width,
                        )
                    }
                }));
                (cb)(self.row_base + self.rows_seen + i as u32, T::skein(&refs));
            }
            drop(cb);
            self.rows_seen += n as u32;
            for col in &mut self.cols {
                for c in &mut col.inputs {
                    c.unwind(n);
                }
            }
            for col in &self.cols {
                for &p in &col.producers {
                    ctx.tug(p);
                }
            }
            return;
        }
        for i in 0..n {
            for (ci, dst) in self.assembled.iter_mut().enumerate() {
                for col in &self.cols {
                    let row = col.inputs[ci].peek(i);
                    let src = unsafe {
                        std::slice::from_raw_parts(
                            (row.as_ptr() as *const T).add(col.src_off),
                            col.width,
                        )
                    };
                    dst[col.x0..col.x0 + col.width].copy_from_slice(src);
                }
            }
            if self.ycc {
                let (a, rest) = self.assembled.split_at_mut(1);
                let (b, c) = rest.split_at_mut(1);
                T::untangle_color(&mut a[0], &mut b[0], &mut c[0]);
                // assembled[0..3] now hold R, G, B.
            }
            let refs: Vec<&[T]> = self.assembled.iter().map(|v| v.as_slice()).collect();
            let mut cb = self.cb.lock().unwrap();
            (cb)(self.row_base + self.rows_seen + i as u32, T::skein(&refs));
        }
        self.rows_seen += n as u32;
        for col in &mut self.cols {
            for c in &mut col.inputs {
                c.unwind(n);
            }
        }
        for col in &self.cols {
            for &p in &col.producers {
                ctx.tug(p);
            }
        }
    }
}

// ============================================================================
// Graph builder + driver
// ============================================================================

pub struct WeftDecoder {
    rt: Runtime,
    kick: Vec<NodeId>,
    pub rows_total: u32,
}

impl WeftDecoder {
    /// Run the decode to completion (blocks the calling thread).
    pub fn weave(&self) -> Result<(), DecodeError> {
        for &id in &self.kick {
            self.rt.tug(id);
        }
        self.rt.await_stillness().map_err(DecodeError::Logic)
    }
}

/// Decode the whole image, streaming rows to `sink` in order, using the
/// embedding codec's tier-1 block decoder (`coder`, see [`BlockCoder`]).
/// Multi-tile images run one tile row at a time (a fresh graph each; workers
/// rejoin between tile rows). Returns total rows emitted.
pub fn weave_with_coder(
    file: Arc<dyn ReadAt>,
    plan: DecodePlan,
    workers: usize,
    sink: RowSink,
    coder: BlockCoder,
) -> Result<u64, DecodeError> {
    let mut plan = plan;
    let ntx = plan.siz.tiles_across() as usize;
    let nty = plan.siz.tiles_down() as usize;
    // Sample-width selection mirrors grok's grk_get_data_type() BIBO
    // (bounded-input-bounded-output) rule, NOT a coefficient-magnitude
    // (k_max') test: the int16 lifting buffers must hold `prec` bits PLUS the
    // inverse transform's dynamic-range growth, or the transform overflows
    // int16 even though the coefficients themselves fit. Reversible 5/3 needs
    // 4 bits of headroom (non-MCT) or 5 (MCT — RCT adds a bit of range), so
    // int16 is exact only while `prec + headroom <= 16` (non-MCT prec <= 12,
    // MCT prec <= 11). Grok decides this per component and forces one output
    // type image-wide, so a single under-headroom component pulls everything
    // to i32. 5/3 is exact integer arithmetic either way, so a conservative
    // i32 choice still matches grok bit-for-bit. Irreversible 9/7 always takes
    // the float path: it never overflows and is at least as accurate as grok's
    // int16 fixed-point 9/7.
    let path = if !plan.cod.reversible {
        Path::F32
    } else {
        let num_comps = plan.siz.comp_count();
        let mct = plan.cod.use_ycc && num_comps >= 3;
        let all_i16 = plan.siz.components.iter().enumerate().all(|(c, comp)| {
            let headroom = if mct && c < 3 { 5 } else { 4 };
            comp.precision as u32 + headroom <= 16
        });
        if std::env::var_os("MERCURY_FORCE_I16").is_some() {
            Path::I16
        } else if all_i16 && std::env::var_os("MERCURY_FORCE_I32").is_none() {
            Path::I16
        } else {
            Path::I32
        }
    };
    let cb = Arc::new(Mutex::new(sink));
    let mut tiles = std::collections::VecDeque::from(std::mem::take(&mut plan.tiles));
    let mut emitted = 0u64;
    for _ty in 0..nty {
        let row_tiles: Vec<TilePlan> = tiles.drain(..ntx).collect();
        // A tile row the decode window misses entirely was never parsed and
        // is never decoded.
        if row_tiles.iter().all(|t| !t.in_window) {
            continue;
        }
        let dec = dress_tile_loom(
            Arc::clone(&file),
            &plan,
            row_tiles,
            workers,
            path,
            coder,
            Arc::clone(&cb),
        )?;
        let Some(dec) = dec else {
            continue;
        };
        if std::env::var("WEFT_DEBUG").is_ok() {
            eprintln!(
                "weft debug: tile row kick={} rows_total={}",
                dec.kick.len(),
                dec.rows_total
            );
        }
        dec.weave()?;
        emitted += dec.rows_total as u64;
    }
    Ok(emitted)
}

/// Build the decode graph for one tile row (all components of all tiles in
/// the row). Returns None when the decode window leaves nothing of the row
/// to emit.
fn dress_tile_loom(
    file: Arc<dyn ReadAt>,
    plan: &DecodePlan,
    row_tiles: Vec<TilePlan>,
    workers: usize,
    path: Path,
    coder: BlockCoder,
    cb: Arc<Mutex<RowSink>>,
) -> Result<Option<WeftDecoder>, DecodeError> {
    let n_res = (plan.cod.num_levels + 1) as usize;
    let target_res = n_res - 1 - plan.reduce as usize;
    // one synthesis level per resolution step from 0 up to the target
    let levels = target_res;
    let num_comps = plan.siz.comp_count();
    let block_w = plan.cod.block_width;
    let block_h = plan.cod.block_height;
    let modes = plan.cod.modes.0 as i32;
    for c in 0..num_comps {
        let comp = &plan.siz.components[c];
        if comp.xr_siz != 1 || comp.yr_siz != 1 {
            return Err(DecodeError::Logic(
                "weft graph: subsampled components not wired yet".into(),
            ));
        }
    }
    // Output rectangle on the reduced plane: the window's when one is set,
    // the image's otherwise. Tile resolution dims at target_res live on the
    // same plane, so emitted rows and columns are relative to this rect.
    let out_rect = band_window(
        plan.window.unwrap_or(Dims {
            x0: plan.siz.x_o_siz,
            y0: plan.siz.y_o_siz,
            x1: plan.siz.x_siz,
            y1: plan.siz.y_siz,
        }),
        plan.reduce,
        0,
    );
    let width = plan.width as usize;
    let sb: usize = path.fibre_bytes();
    let prec = if path == Path::I16 {
        SamplePrec::I16
    } else {
        SamplePrec::I32
    };

    let bd = |d: &crate::codec::tile_geom::Dims| BandDims {
        x0: d.x0 as i32,
        y0: d.y0 as i32,
        w: d.width() as i32,
        h: d.height() as i32,
    };

    // Column slices per band: enough that the big full-res bands fan out
    // across all workers; small bands stay whole. Work-stealing balances.
    let slice_count = |blocks_wide: u32| -> u32 { blocks_wide.div_ceil(16).clamp(1, 16) };

    struct ChainSpec {
        /// Per-level engines, bottom first, with each level's output height.
        engines: Vec<Synthesis>,
        level_rows: Vec<u32>,
        /// slices[level][band_type] leaf wiring.
        slices: Vec<Vec<Vec<LeafSlice>>>,
        /// (band_nodes index, level) — for patching consumers once level
        /// node ids are known.
        band_node_levels: Vec<(usize, usize)>,
        /// Per level: (leading child LL rows to drain, byte offset into each
        /// kept child row). All zero on a whole-tile decode.
        ll_feed: Vec<(u32, usize)>,
    }
    struct ColSpec {
        chains: Vec<ChainSpec>,
        x0: usize,
        width: usize,
        src_off: usize,
    }

    let mut band_nodes: Vec<SubbandDecodeNode> = Vec::new();
    let mut col_specs: Vec<ColSpec> = Vec::with_capacity(row_tiles.len());
    let mut row_base: u32 = 0;
    let mut rows_total: u32 = 0;
    let mut sink_discard: u32 = 0;
    let mut first_tile = true;

    for tile in row_tiles.into_iter() {
        if !tile.in_window {
            continue;
        }
        let full = &tile.geom.components[0].resolutions[target_res].dims;
        if full.is_empty() {
            continue;
        }
        // Effective decode windows: the plan's padded ones, or the whole tile
        // expressed in the same shape so everything below is one code path.
        let eff: Vec<TileCompWindow> = tile.win.clone().unwrap_or_else(|| {
            tile.geom
                .components
                .iter()
                .map(|tc| TileCompWindow {
                    res: tc.resolutions.iter().map(|r| r.dims).collect(),
                    bands: tc
                        .resolutions
                        .iter()
                        .map(|r| r.subbands.iter().map(|s| s.dims).collect())
                        .collect(),
                })
                .collect()
        });
        // This tile's emitted rectangle on the output plane.
        let rw = intersect(out_rect, *full);
        if rw.is_empty() {
            continue;
        }
        let node_top = eff[0].res[target_res];
        if first_tile {
            first_tile = false;
            row_base = rw.y0 - out_rect.y0;
            rows_total = rw.height();
            // rows the top engines emit above the output window
            sink_discard = rw.y0 - node_top.y0;
        }
        let tile_x0 = (rw.x0 - out_rect.x0) as usize;
        let tile_w = rw.width() as usize;
        let col_src_off = (rw.x0 - node_top.x0) as usize;
        let mut chains: Vec<ChainSpec> = Vec::with_capacity(num_comps);
        let mut plan_comps = tile.comps;
        for c in 0..num_comps {
            let tc = &tile.geom.components[c];
            let w = &eff[c];
            // Build synthesis engines from the (windowed) geometry, bottom
            // first: each level's spec is its resolution's padded window with
            // that window's band splits — the engine synthesizes only the
            // window, treating its edges as band boundaries; the padding
            // absorbs the fabricated extension values.
            let mut engines = Vec::with_capacity(levels);
            let mut ll_rects = Vec::with_capacity(levels);
            for l in 0..levels {
                let node = w.res[l + 1];
                let ll = intersect(band_window(node, 1, 0), tc.resolutions[l].dims);
                let spec = LevelSpec {
                    node: bd(&node),
                    ll: bd(&ll),
                    hl: bd(&w.bands[l + 1][0]),
                    lh: bd(&w.bands[l + 1][1]),
                };
                engines.push(if path == Path::F32 {
                    warp_w9x7(&spec)?
                } else {
                    warp_w5x3_prec(&spec, prec)?
                });
                ll_rects.push(ll);
            }
            // Rows each level must emit: up to the last row its consumer
            // needs (the next level's LL rect, or the sink window at the
            // top) — everything below that is never synthesized.
            let mut level_rows = Vec::with_capacity(levels);
            for l in 0..levels {
                let node = w.res[l + 1];
                let need_y1 = if l + 1 < levels {
                    ll_rects[l + 1].y1
                } else {
                    rw.y1
                };
                level_rows.push(need_y1 - node.y0);
            }
            // How each level consumes its child's LL rows: the child emits
            // from its own window origin, which starts above and left of the
            // parent's LL rect.
            let mut ll_feed = Vec::with_capacity(levels);
            ll_feed.push((0u32, 0usize)); // level 0 reads leaves, not a ring
            for l in 1..levels {
                let child_node = w.res[l];
                let ll = ll_rects[l];
                debug_assert!(
                    child_node.y0 <= ll.y0 && child_node.x0 <= ll.x0,
                    "LL rect must sit inside the child's padded window"
                );
                ll_feed.push((ll.y0 - child_node.y0, (ll.x0 - child_node.x0) as usize * sb));
            }

            let mut slices_wiring: Vec<Vec<Vec<LeafSlice>>> = (0..levels)
                .map(|_| (0..4).map(|_| Vec::new()).collect())
                .collect();
            let mut band_node_levels: Vec<(usize, usize)> = Vec::new();

            let res_plans = std::mem::take(&mut plan_comps[c]);
            for (r, res_plan) in res_plans.into_iter().enumerate() {
                if r > target_res {
                    break;
                }
                for band in res_plan.bands {
                    if band.width == 0 || band.height == 0 {
                        continue;
                    }
                    let (level, slot) = if r == 0 {
                        (0usize, 0usize)
                    } else {
                        (r - 1, band.band_type as usize)
                    };
                    // The band's decode window: blocks it misses are never
                    // decoded, rows it excludes never enter the ring.
                    let bw = if r == 0 {
                        w.bands[0][0]
                    } else {
                        w.bands[r][band.band_type as usize - 1]
                    };
                    if bw.is_empty() {
                        continue;
                    }
                    // band-local sample and block-grid ranges of the window
                    let bwl_x0 = (bw.x0 - band.x0) as usize;
                    let bwl_x1 = (bw.x1 - band.x0) as usize;
                    let clip_lo = bw.y0 - band.y0;
                    let clip_hi = bw.y1 - band.y0;
                    let first_bx = band.x0 / block_w;
                    let first_by = band.y0 / block_h;
                    let bx_lo = bw.x0 / block_w - first_bx;
                    let bx_hi = (bw.x1 - 1) / block_w - first_bx + 1;
                    let by_lo = bw.y0 / block_h - first_by;
                    let by_hi = (bw.y1 - 1) / block_h - first_by + 1;
                    let cap_rows = band_ring_picks(block_h, bw.y1 - bw.y0);
                    let k = slice_count(bx_hi - bx_lo);
                    let per = (bx_hi - bx_lo).div_ceil(k);
                    let band = Arc::new(band);
                    let mut bx0 = bx_lo;
                    while bx0 < bx_hi {
                        let nbw = per.min(bx_hi - bx0);
                        let (col0, col1) = block_warp_span(&band, block_w, bx0, nbw);
                        let slice_samples = col1 - col0;
                        // whole blocks are decoded into the slot; the engine
                        // reads only the window's sample span
                        let c0 = col0.max(bwl_x0);
                        let c1 = col1.min(bwl_x1);
                        let slot_bytes = slice_samples * sb + 64;
                        let slots: Vec<AlignedVec> = (0..cap_rows)
                            .map(|_| AlignedVec::bare(slot_bytes))
                            .collect();
                        let (prod, con) = spin(slots);
                        // Node id assigned later == index in band_nodes.
                        let node_id = NodeId::heddle(band_nodes.len() as u32);
                        slices_wiring[level][slot].push(LeafSlice {
                            cons: con,
                            producer: node_id,
                            byte_off: (c0 - bwl_x0) * sb,
                            byte_len: (c1 - c0) * sb,
                            src_off: (c0 - col0) * sb,
                        });
                        band_node_levels.push((band_nodes.len(), level));
                        band_nodes.push(SubbandDecodeNode {
                            file: Arc::clone(&file),
                            band: Arc::clone(&band),
                            bx0,
                            nbw,
                            block_w,
                            block_h,
                            modes,
                            path,
                            coder,
                            out: prod,
                            consumer: NodeId::heddle(0), // patched below
                            next_block_row: by_lo,
                            block_row_hi: by_hi,
                            clip_lo,
                            clip_hi,
                            spill: Vec::new(),
                            slot_bytes,
                            scratch: Vec::new(),
                        });
                        bx0 += nbw;
                    }
                }
            }
            chains.push(ChainSpec {
                engines,
                level_rows,
                slices: slices_wiring,
                band_node_levels,
                ll_feed,
            });
        }
        col_specs.push(ColSpec {
            chains,
            x0: tile_x0,
            width: tile_w,
            src_off: col_src_off,
        });
    }
    if col_specs.is_empty() {
        return Ok(None);
    }

    // Assign node ids: band nodes, then level nodes (col-major, chain-major,
    // levels bottom-up), then the sink.
    let num_band_nodes = band_nodes.len() as u32;
    let num_level_nodes: u32 = col_specs
        .iter()
        .map(|cs| {
            cs.chains
                .iter()
                .map(|ch| ch.engines.len() as u32)
                .sum::<u32>()
        })
        .sum();
    let sink_id = NodeId::heddle(num_band_nodes + num_level_nodes);
    let mut next_level = num_band_nodes;
    let mut level_nodes: Vec<LevelNode> = Vec::with_capacity(num_level_nodes as usize);
    let mut cols: Vec<ColInput> = Vec::with_capacity(col_specs.len());
    for cs in col_specs {
        let mut inputs = Vec::with_capacity(cs.chains.len());
        let mut producers = Vec::with_capacity(cs.chains.len());
        for ch in cs.chains {
            let n_lv = ch.engines.len() as u32;
            let base = next_level;
            next_level += n_lv;
            for &(bidx, level) in &ch.band_node_levels {
                band_nodes[bidx].consumer = NodeId::heddle(base + level as u32);
            }
            let top = n_lv as usize - 1;
            // LL consumer handed down the chain as levels are built.
            let mut ll_from_child: Option<Consumer<AlignedVec>> = None;
            for (l, (engine, slices)) in ch
                .engines
                .into_iter()
                .zip(ch.slices.into_iter())
                .enumerate()
            {
                let is_top = l == top;
                let (out_prod, out_con) = if is_top {
                    // Top level: full-width tile rows to the merge sink. Sized
                    // by the engine so the interleave's tail stores fit.
                    let slots: Vec<AlignedVec> = (0..OUT_RING_ROWS)
                        .map(|_| AlignedVec::bare(engine.hem_row_bytes()))
                        .collect();
                    spin(slots)
                } else {
                    // Intermediate: LL rows for the level above.
                    let slots: Vec<AlignedVec> = (0..LL_RING_ROWS)
                        .map(|_| AlignedVec::bare(engine.hem_row_bytes()))
                        .collect();
                    spin(slots)
                };
                level_nodes.push(LevelNode {
                    engine,
                    inputs: LevelInputs {
                        slices,
                        fibre_bytes: sb,
                        dirty: Vec::new(),
                        ll: ll_from_child.take(),
                        ll_producer: NodeId::heddle(base + (l as u32).saturating_sub(1)),
                        ll_released: false,
                        ll_skip_rows: ch.ll_feed[l].0,
                        ll_src_off: ch.ll_feed[l].1,
                    },
                    out: out_prod,
                    consumer: if is_top {
                        sink_id
                    } else {
                        NodeId::heddle(base + l as u32 + 1)
                    },
                    rows_out: 0,
                    rows_total: ch.level_rows[l],
                });
                if is_top {
                    inputs.push(out_con);
                    producers.push(NodeId::heddle(base + top as u32));
                } else {
                    ll_from_child = Some(out_con);
                }
            }
        }
        cols.push(ColInput {
            inputs,
            producers,
            x0: cs.x0,
            width: cs.width,
            src_off: cs.src_off,
        });
    }

    let ycc = plan.cod.use_ycc && num_comps >= 3;
    let mut b = Builder::warp();
    let mut kick = Vec::new();
    for n in band_nodes {
        kick.push(b.mount(Box::new(n)));
    }
    for ln in level_nodes {
        b.mount(Box::new(ln));
    }
    struct HemArgs {
        cols: Vec<ColInput>,
        width: usize,
        ycc: bool,
        row_base: u32,
        discard: u32,
        num_comps: usize,
        cb: Arc<Mutex<RowSink>>,
    }
    fn make_hem<T: Sample>(a: HemArgs) -> Box<MergeSinkNode<T>> {
        Box::new(MergeSinkNode::<T> {
            cols: a.cols,
            width: a.width,
            ycc: a.ycc,
            row_base: a.row_base,
            rows_seen: 0,
            discard: a.discard,
            assembled: (0..a.num_comps)
                .map(|_| vec![T::default(); a.width])
                .collect(),
            cb: a.cb,
        })
    }
    let hem_args = HemArgs {
        cols,
        width,
        ycc,
        row_base,
        discard: sink_discard,
        num_comps,
        cb,
    };
    let got_sink = match path {
        Path::I16 => b.mount(make_hem::<i16>(hem_args)),
        Path::I32 => b.mount(make_hem::<i32>(hem_args)),
        Path::F32 => b.mount(make_hem::<f32>(hem_args)),
    };
    assert_eq!(got_sink, sink_id);

    Ok(Some(WeftDecoder {
        rt: b.dress(workers),
        kick,
        rows_total,
    }))
}
