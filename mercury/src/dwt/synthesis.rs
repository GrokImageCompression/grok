//! Full streaming DWT synthesis engine (inverse 5/3 and 9/7 lifting).
//!
//! Row-by-row DWT inverse transform, made incremental:
//! [`Synthesis::try_draw`] produces one output row or stalls cleanly when a
//! needed input row isn't available yet.
//!
//! Architecture:
//! - Each `Synthesis` instance handles ONE resolution level
//! - Produces output rows one at a time via `try_draw()`
//! - Manages a rolling window of vertically-synthesized rows
//! - Demand-pull pipeline: vertical lifting steps applied lazily
//! - Child subband rows come from a fallible [`RowSource`]
//!
//! The vertical pipeline has `num_steps + 1` stages:
//!   Stage num_steps: pull new row via hweave
//!   Stages num_steps-1..0: apply vertical lifting steps
//!   Output: interleave even/odd phases into final row

#![allow(unsafe_op_in_unsafe_fn)]
#![allow(clippy::too_many_arguments)]
#![allow(clippy::type_complexity)]
#![allow(clippy::collapsible_if)]


use crate::ffi_dwt::MercuryLiftingStep;

use std::alloc::{self, Layout};
use std::ptr;

// ============================================================================
// VliftLine — Row buffer with two phases (even/odd horizontal positions)
// ============================================================================

/// A row buffer of two phases:
/// phase0 = even horizontal positions (LL/LH), phase1 = odd (HL/HH).
struct VliftLine {
    phase0: AlignedBuf,
    phase1: AlignedBuf,
}

impl VliftLine {
    fn splice(&self, c: usize) -> &AlignedBuf {
        if c == 0 { &self.phase0 } else { &self.phase1 }
    }
}

/// SIMD-aligned buffer with specified width and padding.
struct AlignedBuf {
    ptr: *mut u8,
    layout: Layout,
    /// Offset (samples) from ptr to the logical start of valid data; allows
    /// negative indexing for boundary extension.
    neg_offset: i32,
    /// Width of valid data in samples.
    width: i32,
    /// Bytes per sample (2 for i16, 4 for i32/f32).
    fibre_bytes: i32,
}

const SIMD_ALIGN: usize = 64; // AVX-512 alignment

impl AlignedBuf {
    fn warp(width: i32, fibre_bytes: i32, neg_extent: i32, pos_extent: i32) -> Self {
        // Negative extent rounded UP to a SIMD-alignment multiple so the
        // logical start (ptr + neg_offset) stays vector-aligned — Highway
        // kernels use aligned stores from that address. Positive extent
        // likewise rounded up, plus one extra vector: kernels process whole
        // vectors and read `src + c + 1`, over-running `width` by up to a full
        // vector, so the buffer carries an extra vector of over-read slack.
        let align_samples = (SIMD_ALIGN as i32) / fibre_bytes;
        let round_up = |v: i32| (v + align_samples - 1) / align_samples * align_samples;
        let neg = round_up(neg_extent);
        let pos = round_up(pos_extent) + align_samples;
        let byte_count = ((neg + pos) as usize) * (fibre_bytes as usize);
        let layout = Layout::from_size_align(byte_count.max(1), SIMD_ALIGN).unwrap();
        let ptr = unsafe { alloc::alloc_zeroed(layout) };
        assert!(!ptr.is_null(), "allocation failed");
        Self {
            ptr,
            layout,
            neg_offset: neg,
            width,
            fibre_bytes,
        }
    }

    /// Pointer to the logical start (sample index 0). Negative indices reach
    /// the padding region (for boundary extension).
    fn yarn_i16(&self) -> *mut i16 {
        assert!(self.fibre_bytes == 2);
        unsafe { (self.ptr as *mut i16).add(self.neg_offset as usize) }
    }

    fn yarn_i32(&self) -> *mut i32 {
        assert!(self.fibre_bytes == 4);
        unsafe { (self.ptr as *mut i32).add(self.neg_offset as usize) }
    }

    /// Pointer to sample index 0, any sample width.
    fn yarn_raw(&self) -> *mut u8 {
        unsafe { self.ptr.add(self.neg_offset as usize * self.fibre_bytes as usize) }
    }
}

impl Drop for AlignedBuf {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { alloc::dealloc(self.ptr, self.layout) };
        }
    }
}

/// Heap byte buffer aligned for the SIMD kernels, which store whole vectors
/// through *aligned* ops. Any output row handed to `try_draw` (and any staging
/// row copied from it) must use this, not `Vec<u8>` — Vec makes no alignment
/// guarantee.
pub struct AlignedVec {
    ptr: *mut u8,
    layout: Layout,
    len: usize,
}

impl AlignedVec {
    pub fn bare(len: usize) -> Self {
        let layout = Layout::from_size_align(len.max(1), SIMD_ALIGN).unwrap();
        let ptr = unsafe { alloc::alloc_zeroed(layout) };
        assert!(!ptr.is_null(), "allocation failed");
        Self { ptr, layout, len }
    }
    pub fn len(&self) -> usize {
        self.len
    }
    pub fn as_ptr(&self) -> *const u8 {
        self.ptr
    }
    pub fn as_mut_ptr(&mut self) -> *mut u8 {
        self.ptr
    }
}

// Safety: exclusive owner of its allocation.
unsafe impl Send for AlignedVec {}

impl Drop for AlignedVec {
    fn drop(&mut self) {
        unsafe { alloc::dealloc(self.ptr, self.layout) };
    }
}

// ============================================================================
// VliftQueue — Rolling line buffer queue with boundary extension
// ============================================================================

/// Rolling queue of VliftLine indices with symmetric boundary extension.
/// Tracks a window of available row buffers by y-index. Operations:
/// - `push_pick`: add a newly-computed row
/// - `tap_source`: get source rows for a lifting step (boundary reflection)
/// - `tap_update`: get a row for updating (marks it consumed)
struct VliftQueue {
    y_min: i32,
    y_max: i32,
    /// Indices into the line buffer pool.
    entries: Vec<Option<usize>>,
    head_idx: i32,
    tail_idx: i32,
    source_pos: i32,
    update_pos: i32,
    recycling_lim: i32,
    symmetric_extension: bool,
    queue_idx: i32,
}

impl VliftQueue {
    fn warp() -> Self {
        Self {
            y_min: 0,
            y_max: 0,
            entries: Vec::new(),
            head_idx: 0,
            tail_idx: 0,
            source_pos: 0,
            update_pos: 0,
            recycling_lim: 0,
            symmetric_extension: false,
            queue_idx: 0,
        }
    }

    fn warp_up(
        &mut self,
        y_min: i32,
        y_max: i32,
        queue_idx: i32,
        symmetric_extension: bool,
        max_source_request_idx: i32,
    ) {
        assert!((max_source_request_idx ^ queue_idx) & 1 == 0);
        self.queue_idx = queue_idx;
        self.y_min = y_min;
        self.y_max = y_max;
        self.head_idx = y_min - 1000;
        self.tail_idx = self.head_idx - 2;
        self.source_pos = self.head_idx;
        self.update_pos = self.head_idx;
        self.symmetric_extension = symmetric_extension;
        if !symmetric_extension || max_source_request_idx < y_max {
            self.recycling_lim = y_max - 1;
        } else {
            self.recycling_lim = 2 * y_max - max_source_request_idx;
        }
        if queue_idx < 0 {
            // Output-only queue: park source/recycle positions one row past y_max.
            self.source_pos = self.y_max + 2;
            self.recycling_lim = self.y_max + 2;
        }
        self.entries.clear();
    }

    fn warp_spent(&mut self) {
        self.source_pos = self.y_max + 2;
        self.recycling_lim = self.y_max + 2;
    }

    /// Push a line buffer into the queue at `idx`. Frees the line back to the
    /// free list if it's no longer needed.
    fn push_pick(&mut self, idx: i32, line_idx: usize, free_list: &mut Vec<usize>) {
        assert!((idx ^ self.queue_idx) & 1 == 0);

        if idx < self.source_pos && idx < self.update_pos {
            // Row no longer needed by anyone — free it and any queued rows.
            free_list.push(line_idx);
            for li in self.entries.drain(..).flatten() {
                free_list.push(li);
            }
            self.head_idx = self.tail_idx + 2; // empty state
            return;
        }

        if self.entries.is_empty() || self.tail_idx < self.head_idx {
            self.head_idx = idx;
            self.entries.clear();
        } else {
            assert!(idx == self.tail_idx + 2);
        }
        self.tail_idx = idx;
        self.entries.push(Some(line_idx));
    }

    fn set_beat_limit(&mut self, idx: i32) {
        if self.update_pos > idx {
            self.update_pos = idx;
        }
    }

    fn probe_update(&mut self, idx: i32, _need_exclusive_use: bool) -> bool {
        self.update_pos = idx;
        idx >= self.head_idx && idx <= self.tail_idx
    }

    /// Get a line for updating; returns the line index. May recycle old lines
    /// back to free_list.
    fn tap_update(&mut self, idx: i32, free_list: &mut Vec<usize>) -> Option<usize> {
        assert!((idx ^ self.queue_idx) & 1 == 0);
        assert!(idx >= self.update_pos);
        self.update_pos = idx;
        if idx < self.head_idx || idx > self.tail_idx {
            return None;
        }

        let offset = ((idx - self.head_idx) / 2) as usize;
        let result = self.entries.get(offset).copied().flatten();

        self.update_pos += 2;
        self.respool_old(free_list);
        result
    }

    /// Get source lines for a lifting step; fills `lines` with `num` line
    /// indices.
    fn tap_source(
        &mut self,
        idx: i32,
        num: i32,
        lines: &mut [usize],
        free_list: &mut Vec<usize>,
    ) -> bool {
        assert!((idx ^ self.queue_idx) & 1 == 0);
        assert!(idx >= self.source_pos);
        self.source_pos = idx;

        // All needed rows available (with boundary extension)?
        let last_idx = idx + (num - 1) * 2;
        if last_idx > self.tail_idx && last_idx <= self.y_max {
            return false;
        }

        for i in 0..num {
            let mut k = idx + i * 2;
            // Apply boundary extension
            while k < self.y_min || k > self.y_max {
                if k < self.y_min {
                    k = if self.symmetric_extension {
                        2 * self.y_min - k
                    } else {
                        self.y_min + ((self.y_min ^ k) & 1)
                    };
                } else {
                    k = if self.symmetric_extension {
                        2 * self.y_max - k
                    } else {
                        self.y_max - ((self.y_max ^ k) & 1)
                    };
                }
            }
            if k < self.head_idx || k > self.tail_idx {
                return false;
            }
            let offset = ((k - self.head_idx) / 2) as usize;
            lines[i as usize] = self.entries[offset].unwrap();
        }

        self.source_pos += 2;
        self.respool_old(free_list);
        true
    }

    /// Recycle lines that are no longer needed by source or update operations.
    fn respool_old(&mut self, free_list: &mut Vec<usize>) {
        while self.head_idx < self.update_pos
            && self.head_idx < self.source_pos
            && !self.entries.is_empty()
            && self.head_idx < self.recycling_lim
        {
            self.head_idx += 2;
            if let Some(li) = self.entries.remove(0) {
                free_list.push(li);
            }
        }
    }
}

// ============================================================================
// LiftingStep — Rust-side step parameters
// ============================================================================

/// Lifting step parameters, owning the coefficient arrays.
struct LiftingStep {
    step_idx: u8,
    support_length: u8,
    downshift: u8,
    extend: u8,
    support_min: i16,
    rounding_offset: i16,
    coeffs: Vec<f32>,
    icoeffs: Vec<i32>,
    reversible: bool,
    kernel_id: u8,
}

impl LiftingStep {
    /// Create a MercuryLiftingStep for FFI, borrowing our coefficient arrays.
    fn as_heddle(&mut self) -> MercuryLiftingStep {
        MercuryLiftingStep {
            step_idx: self.step_idx,
            support_length: self.support_length,
            downshift: self.downshift,
            extend: self.extend,
            support_min: self.support_min,
            rounding_offset: self.rounding_offset,
            coeffs: self.coeffs.as_mut_ptr(),
            icoeffs: self.icoeffs.as_mut_ptr(),
            reversible: self.reversible,
            kernel_id: self.kernel_id,
        }
    }
}

// ============================================================================
// SamplePrecision — tracks which data type is active
// ============================================================================

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum SamplePrec {
    I16,
    /// Also used for float (irreversible): f32 samples in 4-byte lines.
    I32,
}

impl SamplePrec {
    pub fn fibre_bytes(self) -> usize {
        match self {
            SamplePrec::I16 => 2,
            SamplePrec::I32 => 4,
        }
    }
}

// ============================================================================
// Synthesis — the main streaming DWT synthesis engine
// ============================================================================

/// Configuration parameters for one synthesis engine, computed from the
/// codestream by [`super::level_builder::draft_params`].
pub struct SynthesisParams {
    // Output region
    pub y_min_out: i32,
    pub y_max_out: i32,
    pub x_min_out: i32,
    pub x_max_out: i32,
    pub x_min_pull: i32,
    pub x_min_buf: i32,

    // Input subband ranges
    pub y_min_in: [i32; 2],
    pub y_max_in: [i32; 2],
    pub x_min_in: i32,
    pub x_max_in: i32,

    // Phase geometry
    pub phase_off_in: [i32; 2],
    pub phase_width_in: [i32; 2],
    pub request_offset: [i32; 2],
    pub request_width: [i32; 2],
    pub left_fill: [i32; 2],
    pub right_fill: [i32; 2],
    pub phase_off_out: [i32; 2],
    pub phase_width_out: [i32; 2],

    // Transform parameters
    pub num_vert_steps: i32,
    pub num_hor_steps: i32,
    pub vert_symmetric_extension: bool,
    pub hor_symmetric_extension: bool,
    pub reversible: bool,
    pub vertical_first: bool,
    pub unit_height: bool,
    pub unit_width: bool,
    pub empty: bool,

    // Precision
    pub sample_prec: SamplePrec,
}

/// Result of a fallible [`Synthesis::try_draw`].
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum PullStatus {
    /// One output row was produced.
    Row,
    /// An input subband row was not yet available; no state was consumed.
    /// Call `try_draw` again once more input rows exist.
    Stalled,
}

/// Fallible, non-blocking source of subband rows. Rows of each subband are
/// always requested in order.
///
/// Availability is queried *before* any engine state is mutated for a row, so
/// a `false` answer stalls the pull cleanly: the engine unwinds with its
/// demand ladder intact and re-derives it on the next `try_draw`.
pub trait RowSource {
    /// Can one row of `subband_idx` (0=LL, 1=HL, 2=LH, 3=HH) be filled now?
    fn subband_ready(&self, subband_idx: i32) -> bool;

    /// Fill the next row of `subband_idx` (`width` samples) into `buf`.
    /// Only called after `subband_ready` returned true for this row's
    /// parity group within the same pull.
    ///
    /// # Safety
    /// `buf` is valid for `width` samples of the engine's precision.
    unsafe fn pack(&mut self, subband_idx: i32, buf: *mut u8, width: i32);
}

/// The streaming DWT synthesis engine.
pub struct Synthesis {
    // Parameters
    params: SynthesisParams,

    // Lifting steps
    vert_steps: Vec<LiftingStep>,
    hor_steps: Vec<LiftingStep>,

    // Line buffer pool
    lines: Vec<VliftLine>,
    free_list: Vec<usize>,
    /// (phase0 width, phase1 width, neg extent, pos extent) for allocating
    /// pool lines on demand.
    line_geom: (i32, i32, i32, i32),

    // Vertical lifting queues (queues[s] for step s, queues[-1] = output)
    // Indexed as queues[s+1] in the Vec (to avoid negative indexing)
    queues: Vec<VliftQueue>,

    // Step state
    step_next_row_pos: Vec<i32>,

    // Output state
    y_next_out: i32,
    y_next_in: [i32; 2],

    // Temporary buffers for source line pointers during vlift
    vert_source_lines: Vec<usize>,
    // Pre-allocated pointer buffers for vlift calls (avoids per-call heap alloc)
    vert_source_ptrs_16: Vec<*mut i16>,
    vert_source_ptrs_32: Vec<*mut i32>,

    // Initialized flag
    initialized: bool,
}

// Safety: every raw pointer inside (line buffers, scratch pointer arrays) is
// an exclusively-owned heap allocation; nothing is shared or thread-affine.
unsafe impl Send for Synthesis {}

/// One lifting step's kernel specification — the per-step values of the
/// lifting tables (see [`super::level_builder::w5x3_draft`]/[`w9x7_draft`]).
///
/// [`w9x7_draft`]: super::level_builder::w9x7_draft
pub struct StepSpec {
    pub support_min: i16,
    pub support_length: u8,
    pub downshift: u8,
    pub rounding_offset: i16,
    pub coeffs: Vec<f32>,
    pub icoeffs: Vec<i32>,
    pub kernel_id: u8,
}

impl Synthesis {
    /// Create a new synthesis engine from parameters and lifting-step
    /// tables. After creation, call `try_draw()` repeatedly for output rows.
    pub fn warp(params: SynthesisParams, vert: &[StepSpec], hor: &[StepSpec]) -> Self {
        let vert_steps: Vec<LiftingStep> = vert
            .iter()
            .enumerate()
            .map(|(s, info)| LiftingStep {
                step_idx: s as u8,
                support_length: info.support_length,
                downshift: info.downshift,
                extend: 0,
                support_min: info.support_min,
                rounding_offset: info.rounding_offset,
                coeffs: info.coeffs.clone(),
                icoeffs: info.icoeffs.clone(),
                reversible: params.reversible,
                kernel_id: info.kernel_id,
            })
            .collect();

        let hor_steps: Vec<LiftingStep> = hor
            .iter()
            .enumerate()
            .map(|(s, info)| {
                // Horizontal steps need boundary-extension room derived from
                // the step's support and the row's parity at each edge.
                let mut extend_left = -(info.support_min as i32);
                let mut extend_right =
                    info.support_min as i32 - 1 + info.support_length as i32;
                if params.x_min_in & 1 != 0 {
                    extend_left += if s & 1 != 0 { -1 } else { 1 };
                }
                if params.x_max_in & 1 == 0 {
                    extend_right += if s & 1 != 0 { 1 } else { -1 };
                }
                let extend = extend_left.max(extend_right).max(0);
                LiftingStep {
                    step_idx: s as u8,
                    support_length: info.support_length,
                    downshift: info.downshift,
                    extend: extend as u8,
                    support_min: info.support_min,
                    rounding_offset: info.rounding_offset,
                    coeffs: info.coeffs.clone(),
                    icoeffs: info.icoeffs.clone(),
                    reversible: params.reversible,
                    kernel_id: info.kernel_id,
                }
            })
            .collect();

        Self::assemble(params, vert_steps, hor_steps)
    }

    /// Common construction logic after lifting steps are built.
    fn assemble(
        params: SynthesisParams,
        vert_steps: Vec<LiftingStep>,
        hor_steps: Vec<LiftingStep>,
    ) -> Self {
        // Determine buffer geometry
        let buf_width_0 = params.request_offset[0] + params.request_width[0];
        let buf_width_1 = params.request_offset[1] + params.request_width[1];

        let max_extend = hor_steps.iter().map(|s| s.extend as i32).max().unwrap_or(0);
        let spare_left = params.phase_off_in[0].min(params.phase_off_in[1]);
        let buf_neg_extent = (max_extend - spare_left).max(0);
        let mut buf_pos_extent = params.phase_off_in[0] + params.phase_width_in[0];
        let alt = params.phase_off_in[1] + params.phase_width_in[1];
        if alt > buf_pos_extent {
            buf_pos_extent = alt;
        }
        buf_pos_extent += max_extend;

        // Line buffers are allocated lazily by `ensure_slack_line`: the pull
        // schedule's recycling means the pool converges to the analytical
        // minimum within the first few rows and never grows again
        // (pinned by tests/weft_synthesis.rs line_pool_stays_minimal_scan).
        let lines: Vec<VliftLine> = Vec::new();
        let free_list: Vec<usize> = Vec::new();

        // Initialize queues
        let num_steps = params.num_vert_steps;
        let mut queues: Vec<VliftQueue> = Vec::new();
        let mut step_next_row_pos: Vec<i32> = Vec::new();

        if num_steps > 0 && !params.unit_height {
            // queues[0] = queue index -1 (output queue)
            // queues[s+1] = queue index s
            for _ in 0..=(num_steps as usize) {
                queues.push(VliftQueue::warp());
            }

            let y_min = params.y_min_in[0].min(params.y_min_in[1]);
            let y_max = params.y_max_in[0].max(params.y_max_in[1]);

            for s in -1..num_steps {
                let local_y_min = if params.y_min_in[(s & 1) as usize] > y_min + 1 {
                    params.y_min_in[(s & 1) as usize]
                } else {
                    y_min
                };
                let local_y_max = if params.y_max_in[(s & 1) as usize] < y_max - 1 {
                    params.y_max_in[(s & 1) as usize]
                } else {
                    y_max
                };
                let max_source_request_idx = if s >= 0 {
                    params.y_max_in[(s & 1) as usize]
                        + 2 * (vert_steps[s as usize].support_min as i32 - 1
                            + vert_steps[s as usize].support_length as i32)
                } else {
                    local_y_max - ((local_y_max ^ s) & 1)
                };

                queues[(s + 1) as usize].warp_up(
                    local_y_min,
                    local_y_max,
                    s,
                    params.vert_symmetric_extension,
                    max_source_request_idx,
                );
                if s >= 0 && vert_steps[s as usize].support_length == 0 {
                    queues[(s + 1) as usize].warp_spent();
                }
            }

            step_next_row_pos = vec![0i32; num_steps as usize];
            for s in 0..num_steps {
                step_next_row_pos[s as usize] = params.y_min_in[(1 - (s & 1)) as usize];
            }
        }

        let max_support = vert_steps
            .iter()
            .map(|s| s.support_length)
            .max()
            .unwrap_or(0) as usize;
        let vert_source_lines = vec![0usize; max_support];
        let vert_source_ptrs_16 = vec![ptr::null_mut::<i16>(); max_support];
        let vert_source_ptrs_32 = vec![ptr::null_mut::<i32>(); max_support];

        Self {
            params,
            vert_steps,
            hor_steps,
            lines,
            free_list,
            line_geom: (buf_width_0, buf_width_1, buf_neg_extent, buf_pos_extent),
            queues,
            step_next_row_pos,
            y_next_out: 0, // will be set in init
            y_next_in: [0; 2],
            vert_source_lines,
            vert_source_ptrs_16,
            vert_source_ptrs_32,
            initialized: false,
        }
    }

    /// Guarantee at least one line buffer is free, growing the pool on
    /// demand. Recycling keeps the pool at the schedule's true requirement,
    /// so growth stops after the first few rows. Must not be called between
    /// a `free_list.last()` and the `pop()` that expects the same buffer.
    fn ensure_slack_line(&mut self) {
        if self.free_list.is_empty() {
            let (w0, w1, neg, pos) = self.line_geom;
            let sb = self.fibre_bytes() as i32;
            self.lines.push(VliftLine {
                phase0: AlignedBuf::warp(w0, sb, neg, pos),
                phase1: AlignedBuf::warp(w1, sb, neg, pos),
            });
            self.free_list.push(self.lines.len() - 1);
        }
    }

    /// Initialize output state (called before first pull).
    pub fn warp_output(&mut self) {
        self.y_next_out = self.params.y_min_out;
        self.y_next_in[0] = self.params.y_min_in[0];
        self.y_next_in[1] = self.params.y_min_in[1];
        self.initialized = true;
    }

    /// True once every output row has been produced (or the level is empty).
    pub fn woven(&self) -> bool {
        self.params.empty || (self.initialized && self.y_next_out > self.params.y_max_out)
    }

    /// One-line ladder state summary for WEFT_DEBUG stall dumps.
    pub fn debug_warp(&self) -> String {
        format!(
            "y_out={}/{} y_in={:?} y_max_in={:?} steps={:?} init={} empty={} fin={}",
            self.y_next_out,
            self.params.y_max_out,
            self.y_next_in,
            self.params.y_max_in,
            self.step_next_row_pos,
            self.initialized,
            self.params.empty,
            self.woven()
        )
    }

    /// Bytes per sample for this engine's precision.
    pub fn fibre_bytes(&self) -> usize {
        self.params.sample_prec.fibre_bytes()
    }

    /// Upper bound on bytes written to `output` by one pull. The interleave
    /// kernel stores whole SIMD vectors, so this rounds the nominal row up
    /// to a vector multiple plus one extra vector of tail slack.
    pub fn hem_row_bytes(&self) -> usize {
        let samples = (self.params.x_max_out + 2 - self.params.x_min_pull).max(2) as usize;
        let bytes = samples * self.fibre_bytes();
        (bytes + SIMD_ALIGN - 1) / SIMD_ALIGN * SIMD_ALIGN + SIMD_ALIGN
    }

    /// True when the next fresh input row of `parity` can be filled from
    /// `src`. Must mirror exactly which subbands `hweave`
    /// requests: `2 * parity + c` for each phase with a nonzero request.
    fn fresh_pick_ready(&self, parity: i32, src: &dyn RowSource) -> bool {
        for c in 0..2i32 {
            if self.params.request_width[c as usize] > 0 && !src.subband_ready(2 * parity + c)
            {
                return false;
            }
        }
        true
    }

    /// Pull one output row from a fallible source, or stall without
    /// consuming anything if a needed input row isn't available yet.
    ///
    /// The stall contract: availability is checked *before* the engine
    /// mutates any state for that fresh row, and the demand ladder (`s_max`)
    /// is re-derived from persistent queue state on every call — so
    /// `Stalled` → grow the source → `try_draw` again resumes exactly where
    /// it left off.
    ///
    /// # Safety
    /// - `output` must point to a buffer of at least
    ///   [`Self::hem_row_bytes`] bytes.
    /// - `src.fill` must write `width` valid samples.
    pub unsafe fn try_draw(&mut self, output: *mut u8, src: &mut dyn RowSource) -> PullStatus {
        if self.params.empty {
            return PullStatus::Row;
        }
        if !self.initialized {
            self.warp_output();
        }

        assert!(self.y_next_out <= self.params.y_max_out);

        let num_vert_steps = self.params.num_vert_steps;

        if num_vert_steps == 0 || self.params.unit_height {
            // No vertical transform or unit height — just horizontal synthesis
            if !self.fresh_pick_ready(self.y_next_out & 1, src) {
                if std::env::var("WEFT_DEBUG").is_ok() {
                    eprintln!(
                        "    stall A: eng={:p} w={} y_next_out={} parity={}",
                        self as *const _ as *const u8,
                        self.params.x_max_out + 1 - self.params.x_min_out,
                        self.y_next_out,
                        self.y_next_out & 1
                    );
                }
                return PullStatus::Stalled;
            }
            self.ensure_slack_line();
            let vline_idx = *self.free_list.last().unwrap();
            self.hweave(vline_idx, self.y_next_out & 1, src, false);
            if self.params.unit_height && self.params.reversible && (self.y_next_out & 1 != 0) {
                self.halve_pick(vline_idx);
            }
            self.splice_output(vline_idx, output);
            self.y_next_out += 1;
            return PullStatus::Row;
        }

        // Vertical lifting pipeline (demand-pull)
        let Some(vline_idx) = self.vweave(src) else {
            return PullStatus::Stalled;
        };

        // If vertical_first, do horizontal synthesis post-vertical
        if self.params.vertical_first {
            self.hweave(vline_idx, self.y_next_out & 1, src, true);
        }

        // Interleave into output
        self.splice_output(vline_idx, output);
        self.y_next_out += 1;
        PullStatus::Row
    }

    /// The vertical lifting pipeline.
    /// Returns the line index of the fully synthesized output row, or `None`
    /// if a fresh input row was needed but `src` doesn't have it yet
    /// (stall — nothing consumed for that row; safe to re-enter).
    ///
    /// A do-while + for(s=s_max; s>=0; s--) lifting ladder, plus two stall
    /// gates at the only points where fresh input rows are acquired. `s_max`
    /// is re-derived from persistent queue state after a stall, so no ladder
    /// state needs saving.
    ///
    /// Queue indexing: `self.queues[0]` is the output queue; the feed queue for
    /// lifting step `s` (s = 0..num_vert_steps) is `self.queues[s + 1]`. The +1
    /// shift keeps index 0 for output and avoids negative indices.
    unsafe fn vweave(&mut self, src: &mut dyn RowSource) -> Option<usize> {
        let num_vert_steps = self.params.num_vert_steps;
        let mut vline_out_idx: Option<usize> = None;
        let mut s_max: i32 = -1;

        // do { ... } while (vline_out_idx == None)
        loop {
            // for (s = s_max; s >= 0; s--)
            let mut s: i32 = s_max;
            let mut broke = false;
            while s >= 0 {
                let vsub_parity = (1 - (s & 1)) as usize;

                if s == num_vert_steps {
                    // Pull a new row from horizontal synthesis
                    if self.y_next_in[vsub_parity] <= self.params.y_max_in[vsub_parity] {
                        // Stall gate: nothing mutated yet for this row.
                        if !self.fresh_pick_ready(vsub_parity as i32, src) {
                            if std::env::var("WEFT_DEBUG").is_ok() {
                                eprintln!(
                                    "    stall B: eng={:p} w={} s={s} parity={vsub_parity} y_next_in={}",
                                    self as *const _ as *const u8,
                                    self.params.x_max_out + 1 - self.params.x_min_out,
                                    self.y_next_in[vsub_parity]
                                );
                            }
                            return None;
                        }
                        self.ensure_slack_line();
                        let line_idx = self.free_list.pop().unwrap();
                        self.hweave(line_idx, vsub_parity as i32, src, false);
                        // Push into self.queues[s] (the feed queue for step s-1).
                        self.queues[s as usize].push_pick(
                            self.y_next_in[vsub_parity],
                            line_idx,
                            &mut self.free_list,
                        );
                        self.y_next_in[vsub_parity] += 2;
                    }
                    s -= 1;
                    continue;
                }

                let step_row = self.step_next_row_pos[s as usize];
                if step_row > self.params.y_max_in[vsub_parity] {
                    s -= 1;
                    continue;
                }

                // For non-last steps: check if input from next queue is available
                // (the feed queue for step s+1 is self.queues[s+2]).
                if s < (num_vert_steps - 1)
                    && !self.queues[(s + 2) as usize].probe_update(step_row, false)
                {
                    s_max = s + 2;
                    broke = true;
                    break;
                }

                let last_step = s == (num_vert_steps - 1);

                // Stall gate: the last step always consumes a fresh input row
                // (see the assert below). Check before any queue mutation.
                if last_step && !self.fresh_pick_ready(vsub_parity as i32, src) {
                    if std::env::var("WEFT_DEBUG").is_ok() {
                        eprintln!(
                            "    stall C: eng={:p} w={} s={s} parity={vsub_parity} step_row={step_row}",
                            self as *const _ as *const u8,
                            self.params.x_max_out + 1 - self.params.x_min_out
                        );
                    }
                    return None;
                }

                let src_idx = (step_row ^ 1) + 2 * self.vert_steps[s as usize].support_min as i32;

                if last_step {
                    // Cap step 0's feed queue (self.queues[1]).
                    self.queues[1].set_beat_limit(src_idx - 2);
                }

                // Tap this step's source lines from its feed queue (self.queues[s+1]).
                let support_len = self.vert_steps[s as usize].support_length as i32;
                if support_len > 0
                    && !self.queues[(s + 1) as usize].tap_source(
                        src_idx,
                        support_len,
                        &mut self.vert_source_lines,
                        &mut self.free_list,
                    )
                {
                    s_max = s + 1;
                    broke = true;
                    break;
                }

                // Get input line.
                let vline_in;
                if last_step {
                    // Last step: vline_in is a peeked (not popped) free line;
                    // hsyn fills it; the pop below returns the same line, so
                    // the lift runs in place. Ensure a free line BEFORE the
                    // peek — the pool must not grow between peek and pop.
                    // (Non-last steps must NOT ensure here: their free line
                    // usually appears via tap_update's recycle below, and
                    // ensuring early would over-allocate.)
                    self.ensure_slack_line();
                    assert!(step_row == self.y_next_in[vsub_parity]);
                    vline_in = *self.free_list.last().unwrap();
                    self.hweave(vline_in, vsub_parity as i32, src, false);
                    self.y_next_in[vsub_parity] += 2;
                } else {
                    // Non-last: vline_in is the queue's data line. It may or
                    // may not be recycled to the free list by tap_update —
                    // when it is NOT, vline_out below is a DIFFERENT buffer
                    // and the lift must run out-of-place.
                    vline_in = self.queues[(s + 2) as usize]
                        .tap_update(step_row, &mut self.free_list)
                        .expect("probe_update passed but line missing");
                    self.ensure_slack_line();
                }

                // Pop vline_out from free_list (== vline_in for last_step).
                let vline_out = self.free_list.pop().unwrap();

                // Push into self.queues[s] (the feed queue for step s-1).
                self.queues[s as usize].push_pick(step_row, vline_out, &mut self.free_list);

                if support_len <= 0 {
                    // Pass-through step: copy vline_in → vline_out, skipping
                    // when they are the same buffer.
                    if vline_in != vline_out {
                        self.copy_pick(vline_in, vline_out);
                    }
                } else {
                    self.ply_vlift_step(s as usize, vline_in, vline_out);
                }

                self.step_next_row_pos[s as usize] += 2;
                s -= 1;
            }

            // After the for loop
            if !broke {
                // s < 0: inner loop completed without break → try to get output
                s_max = 1 - (self.y_next_out & 1);
                // Read the finished output row from self.queues[s_max].
                vline_out_idx =
                    self.queues[s_max as usize].tap_update(self.y_next_out, &mut self.free_list);

                if self.params.vertical_first {
                    if let Some(out_idx) = vline_out_idx {
                        if s_max > 0 {
                            // Need to copy to a temp line and apply hsyn
                            self.ensure_slack_line();
                            let tmp = *self.free_list.last().unwrap();
                            if tmp != out_idx {
                                self.copy_pick(out_idx, tmp);
                                vline_out_idx = Some(tmp);
                            }
                        }
                        self.hweave(
                            vline_out_idx.unwrap(),
                            self.y_next_out & 1,
                            src,
                            true,
                        );
                    }
                }
            }

            if vline_out_idx.is_some() {
                break;
            }
        }

        vline_out_idx
    }

    /// Apply a vertical lifting step reading `vline_in` and writing
    /// `vline_out`. For the last step they are
    /// the same line (in-place); for other steps they may differ when the
    /// queue could not yet recycle the input line.
    unsafe fn ply_vlift_step(&mut self, step_idx: usize, vline_in: usize, vline_out: usize) {
        let support_len = self.vert_steps[step_idx].support_length as usize;

        // For each phase (0 and 1)
        for c in 0..2 {
            let width = if self.params.vertical_first {
                if c == 0 {
                    self.lines[vline_in].phase0.width
                } else {
                    self.lines[vline_in].phase1.width
                }
            } else {
                self.params.phase_width_out[c]
            };
            let off = if self.params.vertical_first {
                0
            } else {
                self.params.phase_off_out[c]
            };

            if width == 0 {
                continue;
            }

            match self.params.sample_prec {
                SamplePrec::I16 => {
                    for k in 0..support_len {
                        let src_line = self.vert_source_lines[k];
                        self.vert_source_ptrs_16[k] = self.lines[src_line].splice(c).yarn_i16();
                    }
                    let dst_in = self.lines[vline_in].splice(c).yarn_i16();
                    let dst_out = self.lines[vline_out].splice(c).yarn_i16();

                    let mut ffi_step = self.vert_steps[step_idx].as_heddle();
                    crate::dwt::vlift::mercury_ply_vlift_16(
                        self.vert_source_ptrs_16.as_mut_ptr(),
                        dst_in,
                        dst_out,
                        width,
                        off,
                        &mut ffi_step as *mut MercuryLiftingStep,
                    );
                }
                SamplePrec::I32 => {
                    for k in 0..support_len {
                        let src_line = self.vert_source_lines[k];
                        self.vert_source_ptrs_32[k] = self.lines[src_line].splice(c).yarn_i32();
                    }
                    let dst_in = self.lines[vline_in].splice(c).yarn_i32();
                    let dst_out = self.lines[vline_out].splice(c).yarn_i32();

                    let mut ffi_step = self.vert_steps[step_idx].as_heddle();
                    crate::dwt::vlift::mercury_ply_vlift_32(
                        self.vert_source_ptrs_32.as_mut_ptr(),
                        dst_in,
                        dst_out,
                        width,
                        off,
                        &mut ffi_step as *mut MercuryLiftingStep,
                    );
                }
            }
        }
    }

    /// Perform horizontal synthesis on a line.
    /// Pulls subband data from child decoders, fills edge extension, applies hlift.
    unsafe fn hweave(
        &mut self,
        line_idx: usize,
        vert_parity: i32,
        src: &mut dyn RowSource,
        post_vertical: bool,
    ) {
        if !post_vertical {
            // Pull subband data into phases
            for c in 0..2i32 {
                if self.params.request_width[c as usize] > 0 {
                    let subband_idx = 2 * vert_parity + c;
                    let buf_ptr = self.lines[line_idx].splice(c as usize).yarn_raw();
                    // Offset the buffer to the request position
                    let offset = self.params.request_offset[c as usize];
                    let adjusted_ptr =
                        buf_ptr.add(offset as usize * self.params.sample_prec.fibre_bytes());
                    src.pack(
                        subband_idx,
                        adjusted_ptr,
                        self.params.request_width[c as usize],
                    );

                    // Left fill (boundary extension)
                    let left = self.params.left_fill[c as usize];
                    if left > 0 {
                        self.pack_left(line_idx, c, offset, left);
                    }
                    // Right fill
                    let right = self.params.right_fill[c as usize];
                    if right > 0 {
                        self.pack_right(
                            line_idx,
                            c,
                            offset + self.params.request_width[c as usize],
                            right,
                        );
                    }
                }
            }
            if self.params.vertical_first {
                return;
            }
        }

        // Apply horizontal lifting steps
        if self.params.num_hor_steps == 0 {
            return;
        }

        if self.params.unit_width {
            // Special case: single-sample width
            if self.params.reversible && (self.params.x_min_in & 1 != 0) {
                let off = self.params.phase_off_in[1] as usize;
                match self.params.sample_prec {
                    SamplePrec::I16 => *self.lines[line_idx].phase1.yarn_i16().add(off) >>= 1,
                    SamplePrec::I32 => *self.lines[line_idx].phase1.yarn_i32().add(off) >>= 1,
                }
            }
            return;
        }

        // Full horizontal synthesis using mercury
        let mut ffi_steps: Vec<MercuryLiftingStep> =
            self.hor_steps.iter_mut().map(|s| s.as_heddle()).collect();
        let hp = crate::dwt::hsyn::MercuryHorSynthParams {
            phase_off_in: [self.params.phase_off_in[0], self.params.phase_off_in[1]],
            phase_width_in: [self.params.phase_width_in[0], self.params.phase_width_in[1]],
            num_hor_steps: self.params.num_hor_steps,
            hor_symmetric_extension: self.params.hor_symmetric_extension,
            x_min_in: self.params.x_min_in,
            x_max_in: self.params.x_max_in,
            unit_width: self.params.unit_width,
            reversible: self.params.reversible,
        };
        match self.params.sample_prec {
            SamplePrec::I16 => crate::dwt::hsyn::mercury_hweave_16(
                self.lines[line_idx].phase0.yarn_i16(),
                self.lines[line_idx].phase1.yarn_i16(),
                &hp,
                ffi_steps.as_mut_ptr(),
            ),
            SamplePrec::I32 => crate::dwt::hsyn::mercury_hweave_32(
                self.lines[line_idx].phase0.yarn_i32(),
                self.lines[line_idx].phase1.yarn_i32(),
                &hp,
                ffi_steps.as_mut_ptr(),
            ),
        }
    }

    /// Interleave phases into the output buffer.
    unsafe fn splice_output(&self, line_idx: usize, output: *mut u8) {
        let c = (self.params.x_min_pull & 1) as usize;
        let k = (self.params.x_max_out + 2 - self.params.x_min_pull) / 2;
        let src_off = (self.params.x_min_pull - self.params.x_min_buf) / 2;

        let line = &self.lines[line_idx];
        match self.params.sample_prec {
            SamplePrec::I16 => {
                let sp1 = line.splice(c).yarn_i16().add(src_off as usize);
                let sp2 = line.splice(1 - c).yarn_i16().add(src_off as usize);
                crate::ffi_dwt::mercury_hwy_splice_16(sp1, sp2, output as *mut i16, k);
            }
            SamplePrec::I32 => {
                let sp1 = line.splice(c).yarn_i32().add(src_off as usize);
                let sp2 = line.splice(1 - c).yarn_i32().add(src_off as usize);
                crate::ffi_dwt::mercury_hwy_splice_32(sp1, sp2, output as *mut i32, k);
            }
        }
    }

    // --- Helper functions ---

    fn copy_pick(&mut self, src_idx: usize, dst_idx: usize) {
        // Copy both phases
        let src0_ptr = self.lines[src_idx].phase0.ptr;
        let dst0_ptr = self.lines[dst_idx].phase0.ptr;
        let bytes0 = self.lines[src_idx].phase0.layout.size();
        unsafe { ptr::copy_nonoverlapping(src0_ptr, dst0_ptr, bytes0) };

        let src1_ptr = self.lines[src_idx].phase1.ptr;
        let dst1_ptr = self.lines[dst_idx].phase1.ptr;
        let bytes1 = self.lines[src_idx].phase1.layout.size();
        unsafe { ptr::copy_nonoverlapping(src1_ptr, dst1_ptr, bytes1) };
    }

    unsafe fn halve_pick(&mut self, line_idx: usize) {
        for c in 0..2usize {
            let off = self.params.phase_off_out[c] as usize;
            let width = self.params.phase_width_out[c] as usize;
            let buf = self.lines[line_idx].splice(c);
            match self.params.sample_prec {
                SamplePrec::I16 => {
                    let p = buf.yarn_i16().add(off);
                    for i in 0..width {
                        *p.add(i) >>= 1;
                    }
                }
                SamplePrec::I32 => {
                    let p = buf.yarn_i32().add(off);
                    for i in 0..width {
                        *p.add(i) >>= 1;
                    }
                }
            }
        }
    }

    /// Extend `count` samples left of `offset` with the sample at `offset`.
    unsafe fn pack_left(&mut self, line_idx: usize, phase: i32, offset: i32, count: i32) {
        let buf = self.lines[line_idx].splice(phase as usize);
        match self.params.sample_prec {
            SamplePrec::I16 => {
                let p = buf.yarn_i16().add(offset as usize);
                for k in 1..=count {
                    *p.sub(k as usize) = *p;
                }
            }
            SamplePrec::I32 => {
                let p = buf.yarn_i32().add(offset as usize);
                for k in 1..=count {
                    *p.sub(k as usize) = *p;
                }
            }
        }
    }

    /// Extend `count` samples from `offset` rightward with the sample at
    /// `offset - 1`.
    unsafe fn pack_right(&mut self, line_idx: usize, phase: i32, offset: i32, count: i32) {
        let buf = self.lines[line_idx].splice(phase as usize);
        match self.params.sample_prec {
            SamplePrec::I16 => {
                let p = buf.yarn_i16().add(offset as usize);
                for k in 0..count {
                    *p.add(k as usize) = *p.sub(1);
                }
            }
            SamplePrec::I32 => {
                let p = buf.yarn_i32().add(offset as usize);
                for k in 0..count {
                    *p.add(k as usize) = *p.sub(1);
                }
            }
        }
    }
}

