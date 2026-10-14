//! FFI bindings for the vendored Highway SIMD DWT kernels (native/ — see
//! native/README.md), the crate's only FFI boundary.
//!
//! Only the kernels the decoder dispatches to are declared: W5X3 synthesis
//! lifting (i16 and i32), the 2-tap irreversible lifting for the f32 9/7
//! path, and the phase interleaves. Control flow lives in the Rust
//! orchestrators `dwt::{vlift, hlift, hsyn}`.

/// C-compatible lifting step. Layout matches the `merc_lifting_step` struct the
/// native bridge (`mercury_dwt_bridge.cpp`) casts to — do not reorder fields.
#[repr(C)]
pub struct MercuryLiftingStep {
    pub step_idx: u8,
    pub support_length: u8,
    pub downshift: u8,
    pub extend: u8,
    pub support_min: i16,
    pub rounding_offset: i16,
    pub coeffs: *mut f32,
    pub icoeffs: *mut i32,
    pub reversible: bool,
    pub kernel_id: u8,
}

unsafe extern "C" {
    // i32 lane count of the dispatched kernels' vectors
    pub fn mercury_hwy_lanes_32() -> i32;

    // Phase interleave (synthesis output assembly)
    pub fn mercury_hwy_splice_16(src1: *mut i16, src2: *mut i16, dst: *mut i16, pairs: i32);
    pub fn mercury_hwy_splice_32(src1: *mut i32, src2: *mut i32, dst: *mut i32, pairs: i32);

    // Vertical synthesis lifting, W5X3 i16
    pub fn mercury_hwy_vply_16_5x3_weave_s0(
        src: *mut *mut i16,
        dst_in: *mut i16,
        dst_out: *mut i16,
        samples: i32,
    );
    pub fn mercury_hwy_vply_16_5x3_weave_s1(
        src: *mut *mut i16,
        dst_in: *mut i16,
        dst_out: *mut i16,
        samples: i32,
    );

    // Vertical synthesis lifting, W5X3 i32
    pub fn mercury_hwy_vply_32_5x3_weave_s0(
        src: *mut *mut i32,
        dst_in: *mut i32,
        dst_out: *mut i32,
        samples: i32,
        step: *mut MercuryLiftingStep,
    );
    pub fn mercury_hwy_vply_32_5x3_weave_s1(
        src: *mut *mut i32,
        dst_in: *mut i32,
        dst_out: *mut i32,
        samples: i32,
        step: *mut MercuryLiftingStep,
    );

    // Vertical 2-tap irreversible lifting (f32 9/7; samples are f32 in the
    // i32 lines, synthesis direction)
    pub fn mercury_hwy_vply_32_2tap_irrev(
        src: *mut *mut i32,
        dst_in: *mut i32,
        dst_out: *mut i32,
        samples: i32,
        step: *mut MercuryLiftingStep,
    );

    // Vertical synthesis lifting, int16 fixed-point 9/7 (Q13), one per step
    pub fn mercury_hwy_vply_16_97_weave_s0(
        src: *mut *mut i16,
        dst_in: *mut i16,
        dst_out: *mut i16,
        samples: i32,
    );
    pub fn mercury_hwy_vply_16_97_weave_s1(
        src: *mut *mut i16,
        dst_in: *mut i16,
        dst_out: *mut i16,
        samples: i32,
    );
    pub fn mercury_hwy_vply_16_97_weave_s2(
        src: *mut *mut i16,
        dst_in: *mut i16,
        dst_out: *mut i16,
        samples: i32,
    );
    pub fn mercury_hwy_vply_16_97_weave_s3(
        src: *mut *mut i16,
        dst_in: *mut i16,
        dst_out: *mut i16,
        samples: i32,
    );

    // Horizontal synthesis lifting, W5X3 i16
    pub fn mercury_hwy_hply_16_5x3_weave_s0(src: *mut i16, dst: *mut i16, samples: i32);
    pub fn mercury_hwy_hply_16_5x3_weave_s1(src: *mut i16, dst: *mut i16, samples: i32);

    // Horizontal synthesis lifting, int16 fixed-point 9/7 (Q13), one per step
    pub fn mercury_hwy_hply_16_97_weave_s0(src: *mut i16, dst: *mut i16, samples: i32);
    pub fn mercury_hwy_hply_16_97_weave_s1(src: *mut i16, dst: *mut i16, samples: i32);
    pub fn mercury_hwy_hply_16_97_weave_s2(src: *mut i16, dst: *mut i16, samples: i32);
    pub fn mercury_hwy_hply_16_97_weave_s3(src: *mut i16, dst: *mut i16, samples: i32);

    // Horizontal synthesis lifting, W5X3 i32
    pub fn mercury_hwy_hply_32_5x3_weave_s0(
        src: *mut i32,
        dst: *mut i32,
        samples: i32,
        step: *mut MercuryLiftingStep,
    );
    pub fn mercury_hwy_hply_32_5x3_weave_s1(
        src: *mut i32,
        dst: *mut i32,
        samples: i32,
        step: *mut MercuryLiftingStep,
    );

    // Horizontal 2-tap irreversible lifting (f32 9/7)
    pub fn mercury_hwy_hply_32_2tap_irrev(
        src: *mut i32,
        dst: *mut i32,
        samples: i32,
        step: *mut MercuryLiftingStep,
    );

    // Forward 9/7 lifting step 0 to 3 over one row, matching grok's WaveletFwd
    pub fn mercury_hwy_analysis_lift_97(
        step: i32,
        target: *mut f32,
        left: *const f32,
        right: *const f32,
        samples: i32,
    );

    // grok's Coder::preCompress for one irreversible code-block
    pub fn mercury_hwy_quantize_97(
        stripe: *const f32,
        stride: i32,
        columns: i32,
        rows: i32,
        quant: f64,
        block: *mut i32,
    ) -> u32;

    pub fn mercury_t1_encode_block(
        samples: *const i32,
        num_cols: i32,
        num_rows: i32,
        max_magnitude: u32,
        orientation: i32,
        compno: i32,
        level: i32,
        stepsize: f64,
        mct: i32,
        coded_data: *mut *const u8,
        pass_rates: *mut u16,
        pass_distortions: *mut f64,
        pass_terminated: *mut u8,
        numbps: *mut i32,
    ) -> i32;

    pub fn mercury_hwy_forward_ict(
        red: *const i32,
        green: *const i32,
        blue: *const i32,
        dc_shift: i32,
        luma: *mut f32,
        chroma_blue: *mut f32,
        chroma_red: *mut f32,
        samples: i32,
    );

    pub fn mercury_t1_band_step(
        numresolutions: u8,
        max_bit_depth: u8,
        bandno: u8,
        step_scale: f64,
        exponent: *mut u8,
        mantissa: *mut u16,
    );

    pub fn mercury_rate_convex_hull(
        lens: *const u16,
        distortions: *const f64,
        slopes: *mut u16,
        num_passes: u32,
    );

    pub fn mercury_hwy_analysis_deinterleave_32(
        row: *const u32,
        even: *mut u32,
        odd: *mut u32,
        pairs: i32,
    );
}
