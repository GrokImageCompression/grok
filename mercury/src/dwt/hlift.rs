//! Horizontal lifting step engine (synthesis direction only — decoder).
//!
//! Dispatches to Highway SIMD kernels for the tables level_builder builds —
//! W5X3 (i16/i32), W9X7 f32 (in i32 lines, 2-tap irreversible), and W9X7
//! int16 Q13 (per-step kernels, see fixed97) — with a scalar fallback as
//! reference for any future kernel.
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

    // 16-bit kernels level_builder builds: W5X3 reversible, W9X7 Q13.
    if st.kernel_id == KERNEL_W5X3 {
        match st.step_idx {
            0 => ffi_dwt::mercury_hwy_hply_16_5x3_weave_s0(src, dst, width),
            _ => ffi_dwt::mercury_hwy_hply_16_5x3_weave_s1(src, dst, width),
        }
        return;
    }
    if st.kernel_id == KERNEL_W9X7 {
        match st.step_idx {
            0 => ffi_dwt::mercury_hwy_hply_16_97_weave_s0(src, dst, width),
            1 => ffi_dwt::mercury_hwy_hply_16_97_weave_s1(src, dst, width),
            2 => ffi_dwt::mercury_hwy_hply_16_97_weave_s2(src, dst, width),
            _ => ffi_dwt::mercury_hwy_hply_16_97_weave_s3(src, dst, width),
        }
        return;
    }

    tabby_hlift_16(src, dst, width, st);
}

/// Scalar reference for the int16 fixed-point 9/7 kernels — the kernel
/// equivalence test pins the SIMD kernels to this, bit for bit.
#[cfg(test)]
pub(crate) unsafe fn q13_hlift_16(src: *mut i16, dst: *mut i16, width: i32, step_idx: u8) {
    for k in 0..width as usize {
        *dst.add(k) =
            crate::dwt::fixed97::ply_step(step_idx, *dst.add(k), *src.add(k), *src.add(k + 1));
    }
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

#[cfg(test)]
mod tests {
    use crate::dwt::synthesis::AlignedVec;
    use crate::dwt::vlift::q13_vlift_16;
    use crate::ffi_dwt;

    const W: usize = 200;
    /// Kernels overrun the width by up to a whole vector.
    const SLACK: usize = 64;

    /// Deterministic values mixing int16 extremes with sample-scale data, so
    /// the saturating paths are exercised.
    fn fill(buf: &mut [i16], seed: &mut u32) {
        for v in buf.iter_mut() {
            *seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
            *v = match *seed % 5 {
                0 => 32767,
                1 => -32768,
                2 => 17545,
                3 => (*seed >> 16) as i16,
                _ => ((*seed >> 20) as i16).wrapping_sub(2048),
            };
        }
    }

    fn pick(samples: usize, seed: &mut u32) -> AlignedVec {
        let mut buf = AlignedVec::bare(samples * 2);
        let s = unsafe { std::slice::from_raw_parts_mut(buf.as_mut_ptr() as *mut i16, samples) };
        fill(s, seed);
        buf
    }

    fn front(buf: &AlignedVec, samples: usize) -> &[i16] {
        unsafe { std::slice::from_raw_parts(buf.as_ptr() as *const i16, samples) }
    }

    /// The Q13 9/7 kernels must match the scalar reference bit for bit —
    /// same MulFixedPoint15 rounding, saturation points, and term order.
    #[test]
    fn q13_kernels_match_the_scalar_reference() {
        let mut seed = 12345u32;
        for step in 0..4u8 {
            // horizontal: taps at src[k] and src[k+1], in-place on dst
            let src = pick(W + 1 + SLACK, &mut seed);
            let mut dst_kernel = pick(W + SLACK, &mut seed);
            let mut dst_scalar = front(&dst_kernel, W + 1).to_vec();
            unsafe {
                let sp = src.as_ptr() as *mut i16;
                let dp = dst_kernel.as_mut_ptr() as *mut i16;
                match step {
                    0 => ffi_dwt::mercury_hwy_hply_16_97_weave_s0(sp, dp, W as i32),
                    1 => ffi_dwt::mercury_hwy_hply_16_97_weave_s1(sp, dp, W as i32),
                    2 => ffi_dwt::mercury_hwy_hply_16_97_weave_s2(sp, dp, W as i32),
                    _ => ffi_dwt::mercury_hwy_hply_16_97_weave_s3(sp, dp, W as i32),
                }
                super::q13_hlift_16(sp, dst_scalar.as_mut_ptr(), W as i32, step);
            }
            assert_eq!(
                &front(&dst_kernel, W)[..],
                &dst_scalar[..W],
                "hlift step {step}"
            );

            // vertical: two source rows, dst_in -> dst_out
            let src0 = pick(W + SLACK, &mut seed);
            let src1 = pick(W + SLACK, &mut seed);
            let dst_in = pick(W + SLACK, &mut seed);
            let mut out_kernel = AlignedVec::bare((W + SLACK) * 2);
            let mut out_scalar = vec![0i16; W + SLACK];
            let mut rows = [src0.as_ptr() as *mut i16, src1.as_ptr() as *mut i16];
            unsafe {
                let din = dst_in.as_ptr() as *mut i16;
                let dk = out_kernel.as_mut_ptr() as *mut i16;
                match step {
                    0 => ffi_dwt::mercury_hwy_vply_16_97_weave_s0(
                        rows.as_mut_ptr(),
                        din,
                        dk,
                        W as i32,
                    ),
                    1 => ffi_dwt::mercury_hwy_vply_16_97_weave_s1(
                        rows.as_mut_ptr(),
                        din,
                        dk,
                        W as i32,
                    ),
                    2 => ffi_dwt::mercury_hwy_vply_16_97_weave_s2(
                        rows.as_mut_ptr(),
                        din,
                        dk,
                        W as i32,
                    ),
                    _ => ffi_dwt::mercury_hwy_vply_16_97_weave_s3(
                        rows.as_mut_ptr(),
                        din,
                        dk,
                        W as i32,
                    ),
                }
                q13_vlift_16(
                    rows.as_mut_ptr(),
                    din,
                    out_scalar.as_mut_ptr(),
                    W as i32,
                    step,
                );
            }
            assert_eq!(
                &front(&out_kernel, W)[..],
                &out_scalar[..W],
                "vlift step {step}"
            );
        }
    }
}
