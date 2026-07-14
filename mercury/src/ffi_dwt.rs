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

    // Horizontal synthesis lifting, W5X3 i16
    pub fn mercury_hwy_hply_16_5x3_weave_s0(src: *mut i16, dst: *mut i16, samples: i32);
    pub fn mercury_hwy_hply_16_5x3_weave_s1(src: *mut i16, dst: *mut i16, samples: i32);

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
}
