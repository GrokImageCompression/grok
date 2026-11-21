//! Stripe decoder — decodes a horizontal row of code-blocks into output
//! stripe lines. One generic driver: T1-decode each block (via the embedding
//! codec's [`BlockCoder`]) into the codec's buffer, then transfer with
//! the path's dequantization (i16/i32 reversible, f32 irreversible).

#![allow(unsafe_op_in_unsafe_fn)]

/// One code-block of a stripe row.
pub struct MercuryStripeBlockInfo {
    /// Pointer to coded byte data.
    pub coded_data: *const u8,
    /// Total coded data length.
    pub coded_length: i32,
    /// Number of coding passes.
    pub num_passes: i32,
    /// Missing MSBs (zero bit-planes).
    pub missing_msbs: i32,
    /// guard_bits + epsilon_b - 1.
    pub k_max_prime: i32,
    /// Subband orientation (0=LL, 1=HL, 2=LH, 3=HH).
    pub orientation: i32,
    /// Coding modes bitmask.
    pub modes: i32,
    /// Block width, clipped to subband boundary.
    pub num_cols: i32,
    /// Block height, clipped to subband boundary.
    pub num_rows: i32,
    /// Horizontal offset in the output stripe lines.
    pub dst_offset: i32,
    /// Per-segment lengths (pointer to array of num_segments elements).
    pub segment_lengths: *const i32,
    /// Number of segments.
    pub num_segments: i32,
}

/// Tier-1 code-block decoder — the seam where an embedding codec substitutes
/// its own T1 (e.g. grok's `ICoder`, which also brings HTJ2K to the streaming
/// path).
///
/// Contract: decode `blk`'s coded bytes and return the coder's own buffer —
/// - row-major, `num_cols` wide, at least `num_rows` rows;
/// - two's complement at twice the sample scale (one fraction bit for the
///   half-bit reconstruction), so the reversible sample is `v / 2`, the
///   irreversible one `v as f32 * delta / 2`;
/// - valid until the next call on the same thread;
/// - `num_passes == 0` blocks never arrive (they zero-fill upstream);
/// - multi-segment blocks (bypass/restart) list per-segment byte lengths in
///   `segment_lengths`; otherwise one segment of `coded_length` bytes.
///
/// Returns None on a malformed codeword.
pub type BlockCoder = unsafe fn(&MercuryStripeBlockInfo) -> Option<*const i32>;

/// Decode every block of a stripe row into `dst_lines` using `coder`,
/// converting each double-scale sample with `xfer`. Blocks absent from
/// every packet (`num_passes == 0`) decode to zeros.
unsafe fn weave_stripe<T>(
    blocks: *const MercuryStripeBlockInfo,
    num_blocks: i32,
    dst_lines: *const *mut T,
    stripe_height: i32,
    coder: BlockCoder,
    xfer: impl Fn(i32) -> T,
) -> bool {
    for b in 0..num_blocks.max(0) as usize {
        let blk = &*blocks.add(b);
        let cols = blk.num_cols;
        let rows = blk.num_rows.min(stripe_height);
        let offset = blk.dst_offset;

        if blk.num_passes <= 0 || blk.coded_length <= 0 {
            for row in 0..rows {
                let dst = *dst_lines.add(row as usize);
                std::ptr::write_bytes(dst.add(offset as usize), 0, cols as usize);
            }
            continue;
        }

        let Some(samples) = coder(blk) else {
            return false;
        };
        for row in 0..rows {
            let dst = *dst_lines.add(row as usize);
            let src = samples.add((row * cols) as usize);
            for col in 0..cols {
                *dst.add((offset + col) as usize) = xfer(*src.add(col as usize));
            }
        }
    }
    true
}

/// Decode a stripe row to i16 lines, reversible dequant (`v / 2`).
///
/// # Safety
/// - `blocks`: `num_blocks` valid `MercuryStripeBlockInfo`s with valid
///   `coded_data`/`segment_lengths`.
/// - `dst_lines`: `stripe_height` valid line pointers, each with room for
///   every block's `dst_offset + num_cols` samples.
pub unsafe fn mercury_weave_stripe16(
    blocks: *const MercuryStripeBlockInfo,
    num_blocks: i32,
    dst_lines: *const *mut i16,
    stripe_height: i32,
    coder: BlockCoder,
) -> bool {
    weave_stripe(blocks, num_blocks, dst_lines, stripe_height, coder, |val| {
        (val / 2) as i16
    })
}

/// Decode a stripe row to i32 lines, reversible dequant — for band
/// coefficients exceeding 15 bits (e.g. 16-bit satellite imagery).
///
/// # Safety
/// Same contract as [`mercury_weave_stripe16`], with `*mut i32` lines.
pub unsafe fn mercury_weave_stripe32(
    blocks: *const MercuryStripeBlockInfo,
    num_blocks: i32,
    dst_lines: *const *mut i32,
    stripe_height: i32,
    coder: BlockCoder,
) -> bool {
    weave_stripe(blocks, num_blocks, dst_lines, stripe_height, coder, |val| {
        val / 2
    })
}

/// Decode a stripe row to i16 Q13 lines, irreversible fixed-point dequant:
/// out = sat_i16(round_ties_even(v · δ · 2^13 / 2)).
/// `delta` = band's raw QCD step size × ONE level's 2D gain — unlike the f32
/// path's accumulated normalization; the Q13 path rescales LL rows between
/// levels instead (graph::LevelInputs), keeping every level's intermediates
/// within the int16 headroom budget.
///
/// # Safety
/// Same contract as [`mercury_weave_stripe16`].
pub unsafe fn mercury_weave_stripe16_q13(
    blocks: *const MercuryStripeBlockInfo,
    num_blocks: i32,
    dst_lines: *const *mut i16,
    stripe_height: i32,
    delta: f32,
    coder: BlockCoder,
) -> bool {
    let q13 = (1u32 << crate::dwt::fixed97::Q13_FRACTION_BITS) as f32;
    let fscale = delta * q13 / 2.0;
    weave_stripe(blocks, num_blocks, dst_lines, stripe_height, coder, |val| {
        // `as i32` saturates on overflow; clamp narrows to the i16 range
        ((val as f32 * fscale).round_ties_even() as i32).clamp(-32768, 32767) as i16
    })
}

/// Decode a stripe row to f32 lines, irreversible dequant (normalized):
/// out = v · δ / 2.
/// `delta` = band's raw QCD step size × accumulated synthesis-gain normalization.
///
/// # Safety
/// Same contract as [`mercury_weave_stripe16`], with `*mut f32` lines.
pub unsafe fn mercury_weave_stripe_f32(
    blocks: *const MercuryStripeBlockInfo,
    num_blocks: i32,
    dst_lines: *const *mut f32,
    stripe_height: i32,
    delta: f32,
    coder: BlockCoder,
) -> bool {
    let fscale = delta / 2.0;
    weave_stripe(blocks, num_blocks, dst_lines, stripe_height, coder, |val| {
        val as f32 * fscale
    })
}
