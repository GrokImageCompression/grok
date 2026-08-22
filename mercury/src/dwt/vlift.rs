//! Vertical lifting step engine (synthesis direction only — decoder).
//!
//! Dispatches to Highway SIMD kernels for the tables level_builder builds —
//! W5X3 (i16/i32), W9X7 f32 (in i32 lines, 2-tap irreversible), and W9X7
//! int16 Q13 (per-step kernels, see fixed97) — with a scalar fallback as
//! reference for any future kernel.
#![allow(unsafe_op_in_unsafe_fn)]

use crate::ffi_dwt::{self, MercuryLiftingStep};

use super::{KERNEL_W5X3, KERNEL_W9X7, align_samples16, align_samples32};

/// Room for the advanced source-row pointers; every lifting table is 2-tap.
const MAX_SUPPORT_LENGTH: usize = 8;

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
    let alignment = align_samples16();

    // Skip whole aligned blocks below the start. The source rows advance
    // with the destination or the taps read the wrong columns.
    let mut actual_start = start_loc;
    let mut din = dst_in;
    let mut dout = dst_out;
    while actual_start > alignment {
        actual_start -= alignment;
        din = din.add(alignment as usize);
        dout = dout.add(alignment as usize);
    }
    let total_width = width + actual_start;
    let advance = (start_loc - actual_start) as usize;
    let mut advanced_rows = [std::ptr::null_mut::<i16>(); MAX_SUPPORT_LENGTH];
    let src_bufs = if advance > 0 {
        let support = st.support_length as usize;
        assert!(support <= MAX_SUPPORT_LENGTH);
        for k in 0..support {
            advanced_rows[k] = (*src_bufs.add(k)).add(advance);
        }
        advanced_rows.as_mut_ptr()
    } else {
        src_bufs
    };

    // 16-bit kernels level_builder builds: W5X3 reversible, W9X7 Q13.
    if st.kernel_id == KERNEL_W5X3 {
        match st.step_idx {
            0 => ffi_dwt::mercury_hwy_vply_16_5x3_weave_s0(src_bufs, din, dout, total_width),
            _ => ffi_dwt::mercury_hwy_vply_16_5x3_weave_s1(src_bufs, din, dout, total_width),
        }
        return;
    }
    if st.kernel_id == KERNEL_W9X7 {
        match st.step_idx {
            0 => ffi_dwt::mercury_hwy_vply_16_97_weave_s0(src_bufs, din, dout, total_width),
            1 => ffi_dwt::mercury_hwy_vply_16_97_weave_s1(src_bufs, din, dout, total_width),
            2 => ffi_dwt::mercury_hwy_vply_16_97_weave_s2(src_bufs, din, dout, total_width),
            _ => ffi_dwt::mercury_hwy_vply_16_97_weave_s3(src_bufs, din, dout, total_width),
        }
        return;
    }

    tabby_vlift_16(src_bufs, din, dout, total_width, actual_start, st);
}

/// Scalar reference for the int16 fixed-point 9/7 kernels — the kernel
/// equivalence test pins the SIMD kernels to this, bit for bit.
#[cfg(test)]
pub(crate) unsafe fn q13_vlift_16(
    src_bufs: *mut *mut i16,
    dst_in: *mut i16,
    dst_out: *mut i16,
    width: i32,
    step_idx: u8,
) {
    let src0 = *src_bufs;
    let src1 = *src_bufs.add(1);
    for k in 0..width as usize {
        *dst_out.add(k) =
            crate::dwt::fixed97::ply_step(step_idx, *dst_in.add(k), *src0.add(k), *src1.add(k));
    }
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
    let alignment = align_samples32();

    // Same block skip as the 16-bit path: source rows advance with the
    // destination.
    let mut actual_start = start_loc;
    let mut din = dst_in;
    let mut dout = dst_out;
    while actual_start > alignment {
        actual_start -= alignment;
        din = din.add(alignment as usize);
        dout = dout.add(alignment as usize);
    }
    let total_width = width + actual_start;
    let advance = (start_loc - actual_start) as usize;
    let mut advanced_rows = [std::ptr::null_mut::<i32>(); MAX_SUPPORT_LENGTH];
    let src_bufs = if advance > 0 {
        let support = st.support_length as usize;
        assert!(support <= MAX_SUPPORT_LENGTH);
        for k in 0..support {
            advanced_rows[k] = (*src_bufs.add(k)).add(advance);
        }
        advanced_rows.as_mut_ptr()
    } else {
        src_bufs
    };

    // W5X3 reversible i32, or W9X7 irreversible (f32 in i32 lines, 2-tap).
    match st.kernel_id {
        KERNEL_W5X3 => {
            match st.step_idx {
                0 => ffi_dwt::mercury_hwy_vply_32_5x3_weave_s0(
                    src_bufs,
                    din,
                    dout,
                    total_width,
                    step,
                ),
                _ => ffi_dwt::mercury_hwy_vply_32_5x3_weave_s1(
                    src_bufs,
                    din,
                    dout,
                    total_width,
                    step,
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::dwt::fixed97;
    use crate::dwt::synthesis::AlignedVec;

    fn heddle(step_idx: u8, coeffs: &mut [f32; 2], icoeffs: &mut [i32; 2]) -> MercuryLiftingStep {
        MercuryLiftingStep {
            step_idx,
            support_length: 2,
            downshift: 0,
            extend: 0,
            support_min: -1,
            rounding_offset: 0,
            coeffs: coeffs.as_mut_ptr(),
            icoeffs: icoeffs.as_mut_ptr(),
            reversible: false,
            kernel_id: KERNEL_W9X7,
        }
    }

    /// A start offset beyond one aligned block advances the destination
    /// pointers by whole blocks; the source rows must advance with them.
    /// Before the fix the sources stayed at column 0, so every tap read the
    /// wrong column once the block skip ran.
    #[test]
    fn block_skip_advances_source_rows_with_the_destination() {
        // 16-bit Q13 path (step 3 = δ)
        let alignment = crate::dwt::align_samples16() as usize;
        let start = 2 * alignment + 3;
        let width = 50usize;
        let n = start + width + 64;
        let mut coeffs = [0.0f32; 2];
        let mut icoeffs = [0i32; 2];
        let mut step = heddle(3, &mut coeffs, &mut icoeffs);
        let mut src0 = AlignedVec::bare(n * 2);
        let mut src1 = AlignedVec::bare(n * 2);
        let mut din = AlignedVec::bare(n * 2);
        let mut dout = AlignedVec::bare(n * 2);
        let as_i16 =
            |b: &AlignedVec| unsafe { std::slice::from_raw_parts(b.as_ptr() as *const i16, n) };
        unsafe {
            for (i, b) in [&mut src0, &mut src1, &mut din].iter_mut().enumerate() {
                let s = std::slice::from_raw_parts_mut(b.as_mut_ptr() as *mut i16, n);
                for (k, v) in s.iter_mut().enumerate() {
                    *v = ((k * 31 + i * 17) % 4001) as i16 - 2000;
                }
            }
            let mut rows = [src0.as_ptr() as *mut i16, src1.as_ptr() as *mut i16];
            mercury_ply_vlift_16(
                rows.as_mut_ptr(),
                din.as_ptr() as *mut i16,
                dout.as_mut_ptr() as *mut i16,
                width as i32,
                start as i32,
                &mut step,
            );
        }
        for k in start..start + width {
            let want = fixed97::ply_step(3, as_i16(&din)[k], as_i16(&src0)[k], as_i16(&src1)[k]);
            assert_eq!(as_i16(&dout)[k], want, "i16 column {k}");
        }

        // 32-bit f32 path (2-tap irreversible)
        let alignment = crate::dwt::align_samples32() as usize;
        let start = 2 * alignment + 3;
        let n = start + width + 64;
        let mut coeffs = [0.443506852f32, 0.443506852];
        let mut icoeffs = [0i32; 2];
        let mut step = heddle(3, &mut coeffs, &mut icoeffs);
        let mut src0 = AlignedVec::bare(n * 4);
        let mut src1 = AlignedVec::bare(n * 4);
        let mut din = AlignedVec::bare(n * 4);
        let mut dout = AlignedVec::bare(n * 4);
        let as_f32 =
            |b: &AlignedVec| unsafe { std::slice::from_raw_parts(b.as_ptr() as *const f32, n) };
        unsafe {
            for (i, b) in [&mut src0, &mut src1, &mut din].iter_mut().enumerate() {
                let s = std::slice::from_raw_parts_mut(b.as_mut_ptr() as *mut f32, n);
                for (k, v) in s.iter_mut().enumerate() {
                    *v = ((k * 31 + i * 17) % 4001) as f32 / 8192.0 - 0.25;
                }
            }
            let mut rows = [src0.as_ptr() as *mut i32, src1.as_ptr() as *mut i32];
            mercury_ply_vlift_32(
                rows.as_mut_ptr(),
                din.as_ptr() as *mut i32,
                dout.as_mut_ptr() as *mut i32,
                width as i32,
                start as i32,
                &mut step,
            );
        }
        for k in start..start + width {
            let want = as_f32(&din)[k] - 0.443506852 * (as_f32(&src0)[k] + as_f32(&src1)[k]);
            let got = as_f32(&dout)[k];
            assert!(
                (got - want).abs() < 1e-4,
                "f32 column {k}: got {got} want {want}"
            );
        }
    }
}
