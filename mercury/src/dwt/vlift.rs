//! Vertical lifting step engine (synthesis direction only — decoder).
//!
//! Dispatches to Highway SIMD kernels for the tables level_builder builds —
//! W5X3 (i16/i32) and W9X7 (f32 in i32 lines, 2-tap irreversible) — with a
//! scalar fallback as reference for any future kernel.
#![allow(unsafe_op_in_unsafe_fn)]

use crate::ffi_dwt::{self, MercuryLiftingStep};

use super::{ALIGN_SAMPLES16, ALIGN_SAMPLES32, KERNEL_W5X3, KERNEL_W9X7};

/// Vertical synthesis lifting step on 16-bit samples.
///
/// # Safety
/// - `src_bufs` points to `step.support_length` valid `*mut i16`, each with at
///   least `width + start_loc` samples.
/// - `dst_in`/`dst_out` have at least `width + start_loc` samples.
/// - `step` valid with correct `icoeffs` pointer.
pub unsafe fn mercury_ply_vlift_16(
    src_bufs: *mut *mut i16,
    dst_in: *mut i16,
    dst_out: *mut i16,
    width: i32,
    start_loc: i32,
    step: *mut MercuryLiftingStep,
) {
    if width <= 0 {
        return;
    }

    let st = &*step;
    let alignment = ALIGN_SAMPLES16;

    // Adjust pointers for alignment.
    let mut actual_start = start_loc;
    let mut din = dst_in;
    let mut dout = dst_out;
    while actual_start > alignment {
        actual_start -= alignment;
        din = din.add(alignment as usize);
        dout = dout.add(alignment as usize);
    }
    let total_width = width + actual_start;

    // Only 16-bit kernel level_builder builds is W5X3 reversible.
    if st.kernel_id == KERNEL_W5X3 {
        match st.step_idx {
            0 => ffi_dwt::mercury_hwy_vply_16_5x3_weave_s0(src_bufs, din, dout, total_width),
            _ => ffi_dwt::mercury_hwy_vply_16_5x3_weave_s1(src_bufs, din, dout, total_width),
        }
        return;
    }

    tabby_vlift_16(src_bufs, din, dout, total_width, actual_start, st);
}

/// Scalar fallback, vertical synthesis lifting (16-bit).
unsafe fn tabby_vlift_16(
    src_bufs: *mut *mut i16,
    dst_in: *mut i16,
    dst_out: *mut i16,
    width: i32,
    start_loc: i32,
    step: &MercuryLiftingStep,
) {
    let support = step.support_length as i32;
    let downshift = step.downshift as i32;
    let offset = step.rounding_offset as i32;
    let icoeffs = step.icoeffs;

    for k in start_loc..width {
        let ku = k as usize;
        let mut sum = offset;
        for t in 0..support {
            let sp = *src_bufs.add(t as usize);
            sum += (*icoeffs.add(t as usize)) * (*sp.add(ku) as i32);
        }
        let lifted = (sum >> downshift) as i16;
        *dst_out.add(ku) = (*dst_in.add(ku)).wrapping_sub(lifted);
    }
}

/// Vertical synthesis lifting step on 32-bit samples.
///
/// # Safety
/// Same as `mercury_ply_vlift_16`, for 32-bit buffers.
pub unsafe fn mercury_ply_vlift_32(
    src_bufs: *mut *mut i32,
    dst_in: *mut i32,
    dst_out: *mut i32,
    width: i32,
    start_loc: i32,
    step: *mut MercuryLiftingStep,
) {
    if width <= 0 {
        return;
    }

    let st = &*step;
    let alignment = ALIGN_SAMPLES32;

    let mut actual_start = start_loc;
    let mut din = dst_in;
    let mut dout = dst_out;
    while actual_start > alignment {
        actual_start -= alignment;
        din = din.add(alignment as usize);
        dout = dout.add(alignment as usize);
    }
    let total_width = width + actual_start;

    // W5X3 reversible i32, or W9X7 irreversible (f32 in i32 lines, 2-tap).
    match st.kernel_id {
        KERNEL_W5X3 => {
            match st.step_idx {
                0 => ffi_dwt::mercury_hwy_vply_32_5x3_weave_s0(
                    src_bufs, din, dout, total_width, step,
                ),
                _ => ffi_dwt::mercury_hwy_vply_32_5x3_weave_s1(
                    src_bufs, din, dout, total_width, step,
                ),
            }
            return;
        }
        KERNEL_W9X7 if !st.reversible && st.support_length <= 2 => {
            ffi_dwt::mercury_hwy_vply_32_2tap_irrev(src_bufs, din, dout, total_width, step);
            return;
        }
        _ => {}
    }

    tabby_vlift_32(src_bufs, din, dout, total_width, actual_start, st);
}

/// Scalar fallback, vertical synthesis lifting (32-bit).
unsafe fn tabby_vlift_32(
    src_bufs: *mut *mut i32,
    dst_in: *mut i32,
    dst_out: *mut i32,
    width: i32,
    start_loc: i32,
    step: &MercuryLiftingStep,
) {
    let support = step.support_length as i32;
    let downshift = step.downshift as i32;
    let icoeffs = step.icoeffs;
    let coeffs = step.coeffs;

    if !step.reversible {
        // Irreversible: samples are f32 in the i32 lines.
        let din = dst_in as *mut f32;
        let dout = dst_out as *mut f32;
        for k in start_loc..width {
            let ku = k as usize;
            let mut sum = 0.0f32;
            for t in 0..support {
                let sp = *src_bufs.add(t as usize) as *mut f32;
                sum += *coeffs.add(t as usize) * *sp.add(ku);
            }
            *dout.add(ku) = *din.add(ku) - sum;
        }
    } else {
        let offset = step.rounding_offset as i32;
        for k in start_loc..width {
            let ku = k as usize;
            let mut sum: i64 = offset as i64;
            for t in 0..support {
                let sp = *src_bufs.add(t as usize);
                sum += (*icoeffs.add(t as usize) as i64) * (*sp.add(ku) as i64);
            }
            *dst_out.add(ku) = *dst_in.add(ku) - ((sum >> downshift) as i32);
        }
    }
}
