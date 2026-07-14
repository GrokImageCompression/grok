//! Horizontal lifting step engine (synthesis direction only — decoder).
//!
//! Dispatches to Highway SIMD kernels for the tables level_builder builds —
//! W5X3 (i16/i32) and W9X7 (f32 in i32 lines, 2-tap irreversible) — with a
//! scalar fallback as reference for any future kernel.
#![allow(unsafe_op_in_unsafe_fn)]

use crate::ffi_dwt::{self, MercuryLiftingStep};

use super::{KERNEL_W5X3, KERNEL_W9X7};

/// Horizontal synthesis lifting step on 16-bit samples.
///
/// # Safety
/// - `src`/`dst` point to valid sample arrays.
/// - `src` has boundary extension already applied.
/// - `step` valid with correct `icoeffs` pointer.
pub unsafe fn mercury_ply_hlift_16(
    src: *mut i16,
    dst: *mut i16,
    width: i32,
    step: *mut MercuryLiftingStep,
) {
    if width <= 0 {
        return;
    }

    let st = &*step;

    // Only 16-bit kernel level_builder builds is W5X3 reversible.
    if st.kernel_id == KERNEL_W5X3 {
        match st.step_idx {
            0 => ffi_dwt::mercury_hwy_hply_16_5x3_weave_s0(src, dst, width),
            _ => ffi_dwt::mercury_hwy_hply_16_5x3_weave_s1(src, dst, width),
        }
        return;
    }

    tabby_hlift_16(src, dst, width, st);
}

/// Scalar fallback, horizontal synthesis lifting (16-bit).
unsafe fn tabby_hlift_16(src: *mut i16, dst: *mut i16, width: i32, step: &MercuryLiftingStep) {
    let support = step.support_length as i32;
    let downshift = step.downshift as i32;
    let offset = step.rounding_offset as i32;
    let icoeffs = step.icoeffs;

    for k in 0..width {
        let ku = k as usize;
        let mut sum = offset;
        for t in 0..support {
            sum += (*icoeffs.add(t as usize)) * (*src.add(ku + t as usize) as i32);
        }
        let lifted = (sum >> downshift) as i16;
        *dst.add(ku) = (*dst.add(ku)).wrapping_sub(lifted);
    }
}

/// Horizontal synthesis lifting step on 32-bit samples.
///
/// # Safety
/// Same as `mercury_ply_hlift_16`, for 32-bit buffers.
pub unsafe fn mercury_ply_hlift_32(
    src: *mut i32,
    dst: *mut i32,
    width: i32,
    step: *mut MercuryLiftingStep,
) {
    if width <= 0 {
        return;
    }

    let st = &*step;

    // W5X3 reversible i32, or W9X7 irreversible (f32 in i32 lines, 2-tap).
    match st.kernel_id {
        KERNEL_W5X3 => {
            match st.step_idx {
                0 => ffi_dwt::mercury_hwy_hply_32_5x3_weave_s0(src, dst, width, step),
                _ => ffi_dwt::mercury_hwy_hply_32_5x3_weave_s1(src, dst, width, step),
            }
            return;
        }
        KERNEL_W9X7 if !st.reversible && st.support_length <= 2 => {
            ffi_dwt::mercury_hwy_hply_32_2tap_irrev(src, dst, width, step);
            return;
        }
        _ => {}
    }

    tabby_hlift_32(src, dst, width, st);
}

/// Scalar fallback, horizontal synthesis lifting (32-bit).
unsafe fn tabby_hlift_32(src: *mut i32, dst: *mut i32, width: i32, step: &MercuryLiftingStep) {
    let support = step.support_length as i32;
    let downshift = step.downshift as i32;
    let icoeffs = step.icoeffs;
    let coeffs = step.coeffs;

    if !step.reversible {
        // Irreversible: samples are f32 in the i32 lines.
        let src_f = src as *mut f32;
        let dst_f = dst as *mut f32;
        for k in 0..width {
            let ku = k as usize;
            let mut sum = 0.0f32;
            for t in 0..support {
                sum += *coeffs.add(t as usize) * *src_f.add(ku + t as usize);
            }
            *dst_f.add(ku) -= sum;
        }
    } else {
        let offset = step.rounding_offset as i32;
        for k in 0..width {
            let ku = k as usize;
            let mut sum = offset as i64;
            for t in 0..support {
                sum += (*icoeffs.add(t as usize) as i64) * (*src.add(ku + t as usize) as i64);
            }
            *dst.add(ku) -= (sum >> downshift) as i32;
        }
    }
}
