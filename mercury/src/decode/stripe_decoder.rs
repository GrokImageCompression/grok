//! Stripe decoder — decodes a horizontal row of code-blocks into output
//! stripe lines. One generic driver: T1-decode each block (via the embedding
//! codec's [`BlockCoder`]) into a sign-magnitude buffer, then transfer with
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
/// Contract: decode `blk`'s coded bytes into a fresh sign-magnitude buffer —
/// - row-major, `num_cols` wide, `4 * ceil(num_rows / 4)` rows (EBCOT stripes
///   are 4 rows; only the first `num_rows` are read);
/// - bit 31 = sign, bits [30..0] = magnitude, left-aligned so the reversible
///   sample is `mag >> (31 - k_max)`, the irreversible one
///   `mag as f32 * delta / 2^(31 - k_max)`;
/// - `num_passes == 0` blocks never arrive (they zero-fill upstream);
/// - multi-segment blocks (bypass/restart) list per-segment byte lengths in
///   `segment_lengths`; otherwise one segment of `coded_length` bytes.
///
/// Returns None on a malformed codeword.
pub type BlockCoder = unsafe fn(&MercuryStripeBlockInfo) -> Option<Vec<i32>>;

/// Decode every block of a stripe row into `dst_lines` using `coder`,
/// converting each sign-magnitude sample with `xfer`. Blocks absent from
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
            let src = samples.as_ptr().add((row * cols) as usize);
            for col in 0..cols {
                *dst.add((offset + col) as usize) = xfer(*src.add(col as usize));
            }
        }
    }
    true
}

/// Decode a stripe row to i16 lines, reversible dequant (sign-magnitude →
/// two's complement, `>> (31 - k_max)`).
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
    k_max: i32,
    coder: BlockCoder,
) -> bool {
    let downshift = 31 - k_max;
    weave_stripe(blocks, num_blocks, dst_lines, stripe_height, coder, |val| {
        if val < 0 {
            -(((val & 0x7FFF_FFFF) >> downshift) as i16)
        } else {
            (val >> downshift) as i16
        }
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
    k_max: i32,
    coder: BlockCoder,
) -> bool {
    let downshift = 31 - k_max;
    weave_stripe(blocks, num_blocks, dst_lines, stripe_height, coder, |val| {
        if val < 0 {
            -((val & 0x7FFF_FFFF) >> downshift)
        } else {
            val >> downshift
        }
    })
}

/// Decode a stripe row to f32 lines, irreversible dequant (normalized):
/// out = ±(|mag| · δ / 2^(31−K_max)), sign bit OR'd back onto the float.
/// `delta` = band's raw QCD step size × accumulated synthesis-gain normalization.
///
/// # Safety
/// Same contract as [`mercury_weave_stripe16`], with `*mut f32` lines.
pub unsafe fn mercury_weave_stripe_f32(
    blocks: *const MercuryStripeBlockInfo,
    num_blocks: i32,
    dst_lines: *const *mut f32,
    stripe_height: i32,
    k_max: i32,
    delta: f32,
    coder: BlockCoder,
) -> bool {
    let fscale = if k_max <= 31 {
        delta / (1u64 << (31 - k_max)) as f32
    } else {
        delta * (1u64 << (k_max - 31)) as f32
    };
    weave_stripe(blocks, num_blocks, dst_lines, stripe_height, coder, |val| {
        let val = val as u32;
        let mag = (val & 0x7FFF_FFFF) as f32 * fscale;
        f32::from_bits(mag.to_bits() | (val & 0x8000_0000))
    })
}
