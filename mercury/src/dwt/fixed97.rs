//! int16 fixed-point 9/7 synthesis arithmetic — Q1.15 lifting on Q13 samples.
//!
//! Mirrors grok's classic WaveletReverse97_16 kernels: the same Q1.15
//! coefficients, saturating adds, and ties-to-even shifts, so the two
//! decoders agree within final rounding.
//!
//! Samples are normalized fixed point: stored value = real value × 2^13,
//! where the real value is the normalized sample (sample / 2^prec, signed
//! around 0). 2^13 is the classic path's Q-format at its precision limit
//! (qShift = 13 − prec with prec ≤ 8), which leaves 3 bits of headroom above
//! the sample peak for the lifting intermediates (the classic header's BIBO
//! budget, section 8).
//!
//! Unlike the f32 path, the per-band dequant gain covers ONE level only, and
//! LL rows are rescaled by K per non-unit direction as the level above
//! consumes them (see the level-gain note in decode::plan) — accumulating
//! the gains across levels like the f32 path would multiply intermediates by
//! ~K² per level and eat the int16 headroom.

/// Fractional bits of the sample representation.
pub const Q13_FRACTION_BITS: u32 = 13;

// Q1.15 synthesis coefficients, round(c × 2^15). Step indices follow the
// lifting tables (level_builder::w9x7_i16_draft): step 0 = α applied last.
// Production arithmetic lives in the SIMD kernels (native hwy_dwt.cpp, same
// constants); these back the scalar reference the tests pin the kernels to.
/// δ = 0.443506852 (undo update 2, subtract, sum-first).
#[cfg(test)]
const SYNTH_COEFF_3: i16 = 14533;
/// γ = 0.882911075 (undo predict 2, subtract, sum-first).
#[cfg(test)]
const SYNTH_COEFF_2: i16 = 28931;
/// |β| × 2^3 = 0.052980118 × 8 (undo update 1, add, multiply-first with a ×8
/// precision boost cancelled by a ties-to-even >>3). round(|β| × 2^18).
#[cfg(test)]
const SYNTH_COEFF_1: i16 = 13888;
/// |α| − 1 = 0.586134342 (undo predict 1: |α| > 1, so d += sum + mf15(sum, frac)).
#[cfg(test)]
const SYNTH_COEFF_0_FRAC: i16 = 19206;

/// K − 1 = 0.230174105: x×K = x + mf15(x, this).
pub const SCALE_K_FRAC: i16 = 7542;
/// K² − 1 = 0.513328327: x×K² = x + mf15(x, this).
pub const SCALE_K2_FRAC: i16 = 16821;

/// Q1.15 fixed-point multiply with rounding: (a × b + 2^14) >> 15.
/// Matches Highway's MulFixedPoint15 for all reachable inputs (b is always a
/// positive coefficient, so the −32768 × −32768 saturation case never occurs).
#[inline(always)]
pub fn mf15(a: i16, b: i16) -> i16 {
    ((a as i32 * b as i32 + (1 << 14)) >> 15) as i16
}

/// Right shift rounding to nearest, ties to even, saturating the bias add.
#[inline(always)]
pub fn rshift_even(x: i16, shift: u32) -> i16 {
    if shift == 0 {
        return x;
    }
    let bias = ((1i32 << (shift - 1)) - 1 + ((x as i32 >> shift) & 1)) as i16;
    x.saturating_add(bias) >> shift
}

/// x × K^count via the fp15 additive decomposition (count = non-unit
/// directions of the consuming level, 0..=2).
#[inline(always)]
pub fn scale_k(x: i16, frac: i16) -> i16 {
    x.saturating_add(mf15(x, frac))
}

/// One synthesis lifting update: `target` at parity p, neighbors `n0`/`n1`
/// from the opposite parity. Arithmetic is per classic step (saturation
/// points and term order included, so SIMD kernels can match bit for bit).
#[cfg(test)]
pub fn ply_step(step_idx: u8, target: i16, n0: i16, n1: i16) -> i16 {
    match step_idx {
        // α: |α| > 1 decomposes as d + sum + mf15(sum, |α|−1)
        0 => {
            let sum = n0.saturating_add(n1);
            target
                .saturating_add(sum)
                .saturating_add(mf15(sum, SYNTH_COEFF_0_FRAC))
        }
        // β: multiply-first; the product sum fits i16 (each ≤ 0.424×range),
        // wrapping add matches the classic kernels
        1 => {
            let products = mf15(n0, SYNTH_COEFF_1).wrapping_add(mf15(n1, SYNTH_COEFF_1));
            target.saturating_add(rshift_even(products, 3))
        }
        2 => target.saturating_sub(mf15(n0.saturating_add(n1), SYNTH_COEFF_2)),
        _ => target.saturating_sub(mf15(n0.saturating_add(n1), SYNTH_COEFF_3)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fp15(c: f64) -> i16 {
        (0.5 + c * (1 << 15) as f64) as i16
    }

    #[test]
    fn coefficients_match_their_real_values() {
        assert_eq!(SYNTH_COEFF_3, fp15(0.443506852));
        assert_eq!(SYNTH_COEFF_2, fp15(0.882911075));
        assert_eq!(SYNTH_COEFF_1, (0.5 + 0.052980118 * (1 << 18) as f64) as i16);
        assert_eq!(SYNTH_COEFF_0_FRAC, fp15(1.586134342 - 1.0));
        assert_eq!(SCALE_K_FRAC, fp15(1.230174105 - 1.0));
        assert_eq!(SCALE_K2_FRAC, fp15(1.230174105 * 1.230174105 - 1.0));
    }

    #[test]
    fn rshift_even_splits_ties() {
        // 4/8 rounds to even 0, 12/8 rounds to even 2
        assert_eq!(rshift_even(4, 3), 0);
        assert_eq!(rshift_even(12, 3), 2);
        assert_eq!(rshift_even(-4, 3), 0);
        assert_eq!(rshift_even(-12, 3), -2);
        // non-ties round to nearest
        assert_eq!(rshift_even(5, 3), 1);
        assert_eq!(rshift_even(-5, 3), -1);
    }

    #[test]
    fn steps_track_the_float_lifting_within_q13_rounding() {
        const A: f64 = -1.586134342;
        const B: f64 = -0.052980118;
        const C: f64 = 0.882911075;
        const D: f64 = 0.443506852;
        let factors = [A, B, C, D];
        // sample-peak inputs for an 8-bit image in Q13 (±2^12)
        let vals: [i16; 5] = [4096, -4096, 3000, -1234, 777];
        for s in 0..4u8 {
            for &t in &vals {
                for &n0 in &vals {
                    for &n1 in &vals {
                        let got = ply_step(s, t, n0, n1) as f64;
                        let want = t as f64 - factors[s as usize] * (n0 as f64 + n1 as f64);
                        assert!(
                            (got - want).abs() <= 2.0,
                            "step {s}: t={t} n0={n0} n1={n1} got {got} want {want}"
                        );
                    }
                }
            }
        }
    }
}
