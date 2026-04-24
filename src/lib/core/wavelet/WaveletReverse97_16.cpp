/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/******************************************************************************
 *
 *  16-bit Fixed-Point 9/7 Irreversible Inverse Wavelet Transform
 *
 ******************************************************************************
 *
 *  TABLE OF CONTENTS
 *  -----------------
 *  1.  Overview
 *  2.  References
 *  3.  Lifting scheme (analysis & synthesis)
 *  4.  Gain normalization (K scaling)
 *  5.  Fixed-point representation (Q1.15 / MulFixedPoint15)
 *  6.  Coefficient decomposition — per-step strategies
 *  7.  Neighbor-sum-first flag and BIBO headroom
 *  8.  Headroom scaling — per-level overflow prevention
 *  9.  Eligible images and precision limits
 *  10. Implementation notes (Highway SIMD)
 *
 *
 *  1. OVERVIEW
 *  -----------
 *  This module implements the CDF 9/7 (Cohen–Daubechies–Feauveau biorthogonal
 *  wavelet with 9 analysis / 7 synthesis taps) inverse discrete wavelet
 *  transform (IDWT) using 16-bit fixed-point arithmetic.
 *
 *  Compared to the standard float32 path (WaveletReverse97.cpp), this path:
 *    - Halves memory bandwidth (2 bytes/sample vs 4)
 *    - Improves cache utilization (2× more samples per cache line)
 *    - Uses integer SIMD which is often faster than float SIMD on low-power cores
 *    - Trades ~0.5 dB peak SNR for the above benefits (acceptable for lossy)
 *
 *  The implementation uses Google Highway (HWY) for portable SIMD, targeting
 *  the MulFixedPoint15 operation (Q1.15 fixed-point multiply) that maps to
 *  single-instruction hardware on all major ISAs:
 *    - x86 SSSE3+/AVX2/AVX-512:  VPMULHRSW
 *    - ARM NEON:                  VQRDMULH
 *    - WASM SIMD:                 i16x8.q15mulr_sat_s
 *    - RISC-V V:                  equivalent rounding shift-right multiply
 *
 *
 *  2. REFERENCES
 *  -------------
 *  [T.800]  ITU-T Rec. T.800 | ISO/IEC 15444-1:2019, "JPEG 2000 Part 1:
 *           Core coding system", Annex F — Discrete wavelet transformation
 *
 *  [CDF97]  A. Cohen, I. Daubechies, J.-C. Feauveau, "Biorthogonal bases
 *           of compactly supported wavelets", Comm. Pure Appl. Math. 45,
 *           pp. 485–560, 1992.
 *
 *  [SWE95]  W. Sweldens, "The lifting scheme: A custom-design construction
 *           of biorthogonal wavelets", Appl. Comp. Harm. Anal. 3(2),
 *           pp. 186–200, 1996.
 *
 *  [DAU98]  I. Daubechies, W. Sweldens, "Factoring wavelet transforms into
 *           lifting steps", J. Fourier Anal. Appl. 4(3), pp. 245–267, 1998.
 *
 *  [BIBO]   D. Taubman, M. Marcellin, "JPEG2000: Image Compression
 *           Fundamentals, Standards and Practice", Springer, 2002.
 *           Section 10.4 — BIBO gain analysis for irreversible transforms.
 *
 *
 *  3. LIFTING SCHEME (ANALYSIS & SYNTHESIS)
 *  -----------------------------------------
 *  The 9/7 wavelet is factored into four lifting steps [DAU98, SWE95].
 *  Per [T.800] Annex F, the ANALYSIS (forward) direction applies:
 *
 *    Step 0 (predict 1):  D[n] += α × (S[n] + S[n+1])     α = -1.586134342
 *    Step 1 (update  1):  S[n] += β × (D[n-1] + D[n])     β = -0.052980118
 *    Step 2 (predict 2):  D[n] += γ × (S[n] + S[n+1])     γ =  0.882911075
 *    Step 3 (update  2):  S[n] += δ × (D[n-1] + D[n])     δ =  0.443506852
 *
 *  followed by gain normalization (Section 4 below).
 *
 *  Here S[n] denotes even-indexed (low-pass) samples and D[n] denotes
 *  odd-indexed (high-pass) samples in the polyphase decomposition.
 *
 *  The SYNTHESIS (inverse) direction reverses the step order and negates
 *  each coefficient:
 *
 *    Step 3 (undo update  2):  S[n] -= δ × (D[n-1] + D[n])
 *    Step 2 (undo predict 2):  D[n] -= γ × (S[n] + S[n+1])
 *    Step 1 (undo update  1):  S[n] -= β × (D[n-1] + D[n])
 *    Step 0 (undo predict 1):  D[n] -= α × (S[n] + S[n+1])
 *
 *  Since α is negative, "D[n] -= α × sum" = "D[n] += |α| × sum" = addition.
 *  Similarly β is negative, so "S[n] -= β × sum" = "S[n] += |β| × sum".
 *  Steps 2,3 have positive coefficients, so synthesis subtracts.
 *
 *
 *  4. GAIN NORMALIZATION (K SCALING)
 *  ----------------------------------
 *  After lifting, the analysis filter bank applies gain normalization
 *  [T.800, Equation F-4]:
 *
 *    even (low-pass) samples  *= 1/K
 *    odd  (high-pass) samples *= K/2
 *
 *  where K = 1.230174105 is the 9/7 normalization constant.
 *
 *  Synthesis must undo this BEFORE the lifting steps:
 *
 *    even *= K     = 1.230174105
 *    odd  *= 2/K   = 1.625732422
 *
 *  In this 16-bit path, K/invK scaling is fused into the interleave phase
 *  (when loading subband data into the scratch buffer).  See Section 6 for
 *  the fixed-point decomposition of these factors.
 *
 *
 *  5. FIXED-POINT REPRESENTATION (Q1.15 / MulFixedPoint15)
 *  --------------------------------------------------------
 *  All intermediate lifting computations use signed 16-bit integers.  The
 *  MulFixedPoint15(a, b) operation computes:
 *
 *    result = (a × b + 2^14) >> 15
 *
 *  This is equivalent to multiplying by a Q1.15 fixed-point factor in the
 *  range [-1.0, +1.0).  The "+2^14" provides rounding (vs truncation).
 *
 *  Since MulFixedPoint15 can only represent factors with |f| < 1.0, any
 *  coefficient whose magnitude exceeds 1.0 must be decomposed into an
 *  integer part (handled by addition) and a fractional part (handled by
 *  MulFixedPoint15).  See Section 6 for each step's decomposition.
 *
 *
 *  6. COEFFICIENT DECOMPOSITION — PER-STEP STRATEGIES
 *  ---------------------------------------------------
 *  Each of the four synthesis lifting steps requires a different strategy
 *  to encode its coefficient in Q1.15 format.
 *
 *  ┌──────────┬────────────────┬──────────────────────────────────────────┐
 *  │ Synth    │ Analysis       │ Fixed-point strategy                     │
 *  │ step     │ coefficient    │                                          │
 *  ├──────────┼────────────────┼──────────────────────────────────────────┤
 *  │ 3 (δ)   │ +0.443506852   │ Direct: |δ| < 1.0, fits in Q1.15.       │
 *  │          │                │ fp15 = round(0.443506852 × 2^15) = 14533│
 *  │          │                │ Synthesis subtracts: S[n] -= mf15(sum,c) │
 *  ├──────────┼────────────────┼──────────────────────────────────────────┤
 *  │ 2 (γ)   │ +0.882911075   │ Direct: |γ| < 1.0, fits in Q1.15.       │
 *  │          │                │ fp15 = round(0.882911075 × 2^15) = 28931│
 *  │          │                │ Synthesis subtracts: D[n] -= mf15(sum,c) │
 *  ├──────────┼────────────────┼──────────────────────────────────────────┤
 *  │ 1 (β)   │ -0.052980118   │ Precision boost: |β| is very small, so   │
 *  │          │                │ direct encoding loses too many bits.      │
 *  │          │                │ Instead: multiply each neighbor SEPARATELY│
 *  │          │                │ by (β × 2^3), sum the products with       │
 *  │          │                │ rounding offset 4, then right-shift by 3. │
 *  │          │                │ fp15 = round(|β| × 2^18) = 13890         │
 *  │          │                │ Net: S[n] += (mf15(D[n-1],c) +           │
 *  │          │                │              mf15(D[n],c) + 4) >> 3      │
 *  │          │                │ This gives 3 extra bits of precision.     │
 *  ├──────────┼────────────────┼──────────────────────────────────────────┤
 *  │ 0 (α)   │ -1.586134342   │ Additive decomposition: |α| > 1.0, so    │
 *  │          │                │ decompose as α = -1 + (α + 1):           │
 *  │          │                │   D[n] -= α × sum                         │
 *  │          │                │        = D[n] + sum - (α+1) × sum         │
 *  │          │                │        = D[n] + sum + mf15(sum, frac)     │
 *  │          │                │ where frac = -(α+1) = 0.586134342        │
 *  │          │                │ fp15 = round(0.586134342 × 2^15) = 19205 │
 *  │          │                │ Note: -(α+1) is POSITIVE since α < -1.    │
 *  └──────────┴────────────────┴──────────────────────────────────────────┘
 *
 *  K/invK Scaling Decomposition:
 *
 *    K = 1.230174105 > 1.0 → decompose: x×K = x + mf15(x, K−1)
 *      K−1 = 0.230174105 → fp15 = round(0.230174105 × 2^15) = 7542
 *
 *    2/K = 1.625732422 > 1.0 → decompose: x×(2/K) = x + mf15(x, 2/K−1)
 *      2/K−1 = 0.625732422 → fp15 = round(0.625732422 × 2^15) = 20506
 *
 *
 *  7. NEIGHBOR-SUM-FIRST FLAG AND BIBO HEADROOM
 *  ----------------------------------------------
 *  For each lifting step, the standard formulation computes:
 *
 *    target -= coefficient × (neighbor_prev + neighbor_next)
 *
 *  When both neighbors are stored as int16, their sum can momentarily
 *  require 17 bits, causing overflow.  Two strategies are available:
 *
 *  (a) SUM-FIRST: Form the 16-bit sum (risking overflow on large values),
 *      then multiply once.  This is faster (one multiply vs two) but
 *      requires extra headroom.  The BIBO analysis must account for the
 *      doubled input range at this step.
 *
 *  (b) MULTIPLY-FIRST: Multiply each neighbor separately, then sum the
 *      two products.  This avoids sum overflow but costs an extra multiply.
 *      Used for step 1 (β) since the coefficient is very small and the
 *      products individually fit in int16 with room to spare.
 *
 *  Per-step assignment:
 *    Step 3 (δ = 0.444):  SUM-FIRST  — moderate coefficient, sum risk managed by headroom
 *    Step 2 (γ = 0.883):  SUM-FIRST  — large coefficient but result still bounded
 *    Step 1 (β = 0.053):  MULTIPLY-FIRST — small coefficient, precision boost via ×8/>>3
 *    Step 0 (α = 1.586):  SUM-FIRST  — decomposed, each partial within range
 *
 *  For SUM-FIRST steps, the BIBO analysis (Section 8) doubles the input
 *  gain to account for the momentary 17-bit intermediate.
 *
 *
 *  8. HEADROOM SCALING — PER-LEVEL OVERFLOW PREVENTION
 *  ----------------------------------------------------
 *  The principal challenge of 16-bit fixed-point 9/7 synthesis is overflow.
 *  The synthesis lifting steps amplify the signal — the BIBO gain from
 *  subband coefficients to reconstructed output determines the worst-case
 *  intermediate magnitude at each point in the pipeline.
 *
 *  The 9/7 wavelet has the following cumulative BIBO gains (from
 *  QuantizerOJPH.cpp, `bibo_gains` class):
 *
 *    gain_9x7_l[]:  1.000, 1.380, 1.333, 1.307, 1.303, 1.300, 1.299 ...
 *    gain_9x7_h[]:  1.298, 1.313, 1.276, 1.235, 1.231, 1.229, 1.228 ...
 *
 *  The 2D worst-case gain (product of horizontal and vertical) peaks at
 *  level 1: 1.380² ≈ 1.90.  This is remarkably low — only ~1 bit of growth
 *  — compared to the 5/3 wavelet's 2.867² ≈ 8.22 (~3 bits).
 *
 *  However, within a single synthesis level, the per-STEP intermediate
 *  values can transiently exceed the per-level BIBO gain.  When a
 *  SUM-FIRST step is used, the temporary neighbor sum doubles the input
 *  gain for that step.  The worst-case intermediate across all steps
 *  and all levels determines whether the data fits in int16.
 *
 *  HEADROOM SCALING MECHANISM:
 *
 *  Before synthesis begins, we compute the worst-case intermediate
 *  magnitude `headroom_peak` from the BIBO gains and SUM-FIRST flags.
 *  If `headroom_peak` would exceed ~95% of the int16 range (±32767),
 *  we apply a headroom scale factor:
 *
 *    1. The subband coefficients (input to the DWT) are RIGHT-SHIFTED by
 *       `headroom_downshift` bits during the interleave phase.  This
 *       reduces their magnitude, preventing overflow during lifting.
 *
 *    2. After the final synthesis level's vertical pass produces pixel
 *       values, the output is LEFT-SHIFTED by the accumulated
 *       `headroom_downshift` to restore full magnitude before DC shift
 *       and clamping.
 *
 *  This is equivalent to dequantizing at a smaller normalization factor
 *  and then scaling up the output.  For a lossy transform, the precision
 *  loss from the right-shift is at most 1–2 LSBs per downshift step,
 *  which is negligible compared to quantization noise.
 *
 *  For typical usage:
 *    - 8-bit precision:  headroom_downshift = 0 (no scaling needed)
 *    - 10-bit precision: headroom_downshift = 0
 *    - 12-bit precision: headroom_downshift = 0–1
 *    - 14-bit precision: headroom_downshift = 1–2
 *
 *
 *  9. ELIGIBLE IMAGES
 *  ------------------
 *  This path is selected at runtime when ALL of the following hold:
 *
 *    1. Irreversible wavelet (qmfbid == 0)
 *    2. Whole-tile decoding (no region-of-interest partial decode)
 *    3. Image precision ≤ 12 bits
 *
 *  The 12-bit limit is conservative.  With headroom scaling, precisions
 *  up to ~14 bits are theoretically feasible, but the accumulated
 *  precision loss from multiple downshift levels may degrade quality
 *  beyond acceptable thresholds.  The 12-bit cutoff keeps the worst-case
 *  downshift to 0–1 bits.
 *
 *  For RGB images with ICT (irreversible multi-component transform):
 *  The ICT is applied AFTER the inverse DWT (on each component
 *  independently), so it does not affect DWT overflow.  The ICT output
 *  for 12-bit data (worst-case ~5700 via 1.772× Cb amplification) fits
 *  comfortably in int16.
 *
 *
 *  10. IMPLEMENTATION NOTES (HIGHWAY SIMD)
 *  ----------------------------------------
 *  - All SIMD lifting uses Highway's MulFixedPoint15() which maps to
 *    the fastest available instruction on each architecture.
 *  - The horizontal pass processes rows independently; the vertical pass
 *    processes columns one-at-a-time in the scalar version and will
 *    be extended to multi-column SIMD in a future revision.
 *  - Thread parallelism is achieved by splitting rows (horizontal) or
 *    columns (vertical) across tasks, each with its own scratch buffer
 *    from a pre-allocated pool.
 *
 ******************************************************************************/

#include <algorithm>
#include <functional>
#include <memory>

#include "hwy_arm_disable_targets.h"

#include "TFSingleton.h"
#include "grk_restrict.h"
#include "simd.h"
#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "ISparseCanvas.h"
#include "TileFutureManager.h"
#include "ImageComponentFlow.h"
#include "IStream.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "CodingParams.h"
#include "ICoder.h"
#include "CoderPool.h"
#include "BitIO.h"

#include "TagTree.h"

#include "CodeblockCompress.h"

#include "CodeblockDecompress.h"
#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"

#include "CodecScheduler.h"
#include "TileComponentWindow.h"
#include "WaveletReverse.h"
#include "TileComponent.h"
#include "DecompressScheduler.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "wavelet/WaveletReverse97_16.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace grk
{
namespace HWY_NAMESPACE
{
  /**************************************************************************
   *                    Fixed-Point Coefficient Constants
   *
   *  Q1.15 ENCODING
   *  --------------
   *  All coefficients are stored as int16_t values for use with the
   *  MulFixedPoint15 operation.  MulFixedPoint15(a, b) computes:
   *
   *    result = (a × b + 16384) >> 15
   *
   *  which is equivalent to multiplying by b/32768.  So to encode a
   *  real-valued coefficient C as a Q1.15 integer:
   *
   *    fp15 = round(C × 32768) = round(C × 2^15)
   *
   *  This works for |C| < 1.0.  Larger coefficients must be decomposed
   *  (see per-step documentation below).
   *
   *  NAMING CONVENTION
   *  -----------------
   *  Coefficients are named with the pattern:
   *    synth_coeff_<step>   — the Q1.15 coefficient for synthesis step N
   *    scale_K_frac         — fractional part of K (for even-sample scaling)
   *    scale_invK_frac      — fractional part of 2/K (for odd-sample scaling)
   **************************************************************************/

  /*
   * Synthesis Step 3 — Undo analysis update δ = +0.443506852
   * ---------------------------------------------------------
   * Analysis added δ × (D[n-1] + D[n]) to each even sample S[n].
   * Synthesis reverses this:  S[n] -= δ × (D[n-1] + D[n])
   *
   * δ = 0.443506852 < 1.0, so it fits directly in Q1.15:
   *   synth_coeff_3 = round(0.443506852 × 2^15) = 14533
   *
   * In code: target -= MulFixedPoint15(neighbor_sum, synth_coeff_3)
   * This step uses SUM-FIRST (neighbors summed before multiply).
   */
  static const int16_t synth_coeff_3 = (int16_t)(0.5 + 0.443506852 * (double)(1 << 15));

  /*
   * Synthesis Step 2 — Undo analysis predict γ = +0.882911075
   * ----------------------------------------------------------
   * Analysis added γ × (S[n] + S[n+1]) to each odd sample D[n].
   * Synthesis reverses this:  D[n] -= γ × (S[n] + S[n+1])
   *
   * γ = 0.882911075 < 1.0, so it fits directly in Q1.15:
   *   synth_coeff_2 = round(0.882911075 × 2^15) = 28931
   *
   * In code: target -= MulFixedPoint15(neighbor_sum, synth_coeff_2)
   * This step uses SUM-FIRST.
   */
  static const int16_t synth_coeff_2 = (int16_t)(0.5 + 0.882911075 * (double)(1 << 15));

  /*
   * Synthesis Step 1 — Undo analysis update β = -0.052980118
   * ---------------------------------------------------------
   * Analysis added β × (D[n-1] + D[n]) to each even sample S[n].
   * Synthesis reverses this:  S[n] -= β × (D[n-1] + D[n])
   *
   * Since β is negative, subtraction becomes addition:
   *   S[n] += |β| × (D[n-1] + D[n])
   *
   * PRECISION BOOST: |β| = 0.052980118 is very small.  In Q1.15, the
   * direct encoding round(0.053 × 32768) = 1736 uses only 11 of the
   * available 15 magnitude bits, wasting 4 bits of precision.
   *
   * To recover precision, we use the MULTIPLY-FIRST strategy with a ×8
   * amplification:
   *
   *   1. Multiply each neighbor SEPARATELY by (|β| × 8) in Q1.15:
   *        synth_coeff_1 = round(|β| × 2^18) = round(0.052980118 × 262144) = 13890
   *      Each product is ≈ 0.424 × neighbor, well within int16 range.
   *
   *   2. Sum the two products with rounding offset 4:
   *        val = MulFixedPoint15(D[n-1], c) + MulFixedPoint15(D[n], c) + 4
   *
   *   3. Right-shift by 3 to cancel the ×8 amplification:
   *        val >>= 3
   *
   *   4. Add to target:  S[n] += val
   *
   * This gives 3 extra bits of precision compared to direct encoding.
   * The MULTIPLY-FIRST approach also avoids the neighbor-sum overflow
   * risk (no SUM-FIRST doubling in BIBO analysis).
   */
  static const int16_t synth_coeff_1 = (int16_t)(0.5 + 0.052980118 * (double)(1 << 18));

  /*
   * Synthesis Step 0 — Undo analysis predict α = -1.586134342
   * ----------------------------------------------------------
   * Analysis added α × (S[n] + S[n+1]) to each odd sample D[n].
   * Synthesis reverses this:  D[n] -= α × (S[n] + S[n+1])
   *
   * Since α is negative, subtraction becomes addition:
   *   D[n] += |α| × (S[n] + S[n+1])       where |α| = 1.586134342
   *
   * ADDITIVE DECOMPOSITION: |α| > 1.0, so it cannot be represented
   * directly in Q1.15.  We decompose using α = -1 + (α + 1):
   *
   *   D[n] -= α × sum = D[n] + |α| × sum
   *         = D[n] + 1 × sum + (|α| - 1) × sum
   *         = D[n] + sum + 0.586134342 × sum
   *
   * The fractional part 0.586134342 = -(α + 1) fits in Q1.15:
   *   synth_coeff_0_frac = round(0.586134342 × 2^15) = 19205
   *
   * In code:
   *   frac = MulFixedPoint15(sum, synth_coeff_0_frac)
   *   D[n] = D[n] + sum + frac
   *
   * This step uses SUM-FIRST (the neighbor sum is formed once, then
   * both the plain addition and the MulFixedPoint15 use it).
   */
  static const int16_t synth_coeff_0_frac = (int16_t)(0.5 + 0.586134342 * (double)(1 << 15));

  /*
   * K Scaling — Even (low-pass) samples
   * ------------------------------------
   * Synthesis un-scales even samples by multiplying by K = 1.230174105.
   * Since K > 1.0, we decompose:  x × K = x + MulFixedPoint15(x, K−1)
   *
   *   K − 1 = 0.230174105
   *   scale_K_frac = round(0.230174105 × 2^15) = 7542
   */
  static const int16_t scale_K_frac = (int16_t)(0.5 + 0.230174105 * (double)(1 << 15));

  /*
   * 2/K Scaling — Odd (high-pass) samples
   * --------------------------------------
   * Synthesis un-scales odd samples by multiplying by 2/K = 1.625732422.
   * Since 2/K > 1.0, we decompose:  x × (2/K) = x + MulFixedPoint15(x, 2/K−1)
   *
   *   2/K − 1 = 0.625732422
   *   scale_invK_frac = round(0.625732422 × 2^15) = 20506
   */
  static const int16_t scale_invK_frac = (int16_t)(0.5 + 0.625732422 * (double)(1 << 15));

  /**************************************************************************
   *  Scalar Helper Functions
   *
   *  These provide the building blocks for the scalar (non-SIMD) synthesis.
   *  Each has a corresponding SIMD implementation using Highway intrinsics.
   **************************************************************************/

  /**
   * @brief Q1.15 fixed-point multiply with rounding (scalar fallback).
   *
   * Computes round(a × b / 2^15) = (a × b + 2^14) >> 15.
   * This matches Highway's MulFixedPoint15 semantics exactly.
   *
   * The intermediate product a×b is computed in 32 bits to avoid overflow.
   * Result is truncated back to int16.
   */
  static inline int16_t mf15(int16_t a, int16_t b)
  {
    return (int16_t)(((int32_t)a * (int32_t)b + (1 << 14)) >> 15);
  }

  /**
   * @brief Apply K scaling to an even (low-pass) sample.
   *
   * Computes x × K = x + round(x × (K−1)) where K = 1.230174105.
   * Uses additive decomposition since K > 1.0 (see Section 6).
   */
  static inline int16_t apply_K_scale(int16_t x)
  {
    return (int16_t)(x + mf15(x, scale_K_frac));
  }

  /**
   * @brief Apply 2/K scaling to an odd (high-pass) sample.
   *
   * Computes x × (2/K) = x + round(x × (2/K−1)) where 2/K = 1.625732422.
   * Uses additive decomposition since 2/K > 1.0 (see Section 6).
   */
  static inline int16_t apply_invK_scale(int16_t x)
  {
    return (int16_t)(x + mf15(x, scale_invK_frac));
  }

  /**************************************************************************
   *  Scalar Vertical 1D 16-bit 9/7 Synthesis (column-at-a-time)
   *
   *  Identical algorithm to the horizontal version, but reads input from
   *  strided column buffers and writes to a strided destination column.
   *  The scratch buffer linearizes the column for cache-friendly lifting.
   *
   *  @param scratch    Scratch buffer (must hold ≥ height samples)
   *  @param height     Output height in samples (= sn + dn)
   *  @param bandL      Low-pass column start pointer
   *  @param strideL    Row stride of the L band buffer (in int16_t units)
   *  @param bandH      High-pass column start pointer
   *  @param strideH    Row stride of the H band buffer
   *  @param dest       Destination column start pointer
   *  @param strideDest Row stride of the destination buffer
   *  @param parity     0 if first output row is even, 1 if odd (= y0 & 1)
   *  @param sn         Number of low-pass rows
   *  @param dn         Number of high-pass rows
   **************************************************************************/
  static void scalar_v_synth_16_97(int16_t* scratch, uint32_t height, const int16_t* bandL,
                                   uint32_t strideL, const int16_t* bandH, uint32_t strideH,
                                   int16_t* dest, uint32_t strideDest, uint32_t parity,
                                   [[maybe_unused]] uint32_t sn, [[maybe_unused]] uint32_t dn)
  {
    if(height == 0)
      return;
    if(height == 1)
    {
      if(parity == 0)
        *dest = apply_K_scale(*bandL);
      else
        *dest = apply_invK_scale(*bandH);
      return;
    }

    // Gather column data into contiguous scratch with K / (2/K) scaling
    uint32_t lIdx = 0, hIdx = 0;
    for(uint32_t i = 0; i < height; ++i)
    {
      if((i + parity) % 2 == 0)
      {
        scratch[i] = apply_K_scale(bandL[lIdx * strideL]);
        lIdx++;
      }
      else
      {
        scratch[i] = apply_invK_scale(bandH[hIdx * strideH]);
        hIdx++;
      }
    }

    // Step 3 — Undo δ: S[n] -= δ × (D[n-1] + D[n])
    for(uint32_t i = parity; i < height; i += 2)
    {
      int16_t d_prev = (i > 0) ? scratch[i - 1] : scratch[i + 1];
      int16_t d_next = (i + 1 < height) ? scratch[i + 1] : scratch[i - 1];
      scratch[i] = (int16_t)(scratch[i] - mf15((int16_t)(d_prev + d_next), synth_coeff_3));
    }

    // Step 2 — Undo γ: D[n] -= γ × (S[n] + S[n+1])
    for(uint32_t i = 1 - parity; i < height; i += 2)
    {
      int16_t s_prev = (i > 0) ? scratch[i - 1] : scratch[i + 1];
      int16_t s_next = (i + 1 < height) ? scratch[i + 1] : scratch[i - 1];
      scratch[i] = (int16_t)(scratch[i] - mf15((int16_t)(s_prev + s_next), synth_coeff_2));
    }

    // Step 1 — Undo β: S[n] += |β| × (D[n-1] + D[n])  [MULTIPLY-FIRST, ×8/>>3]
    for(uint32_t i = parity; i < height; i += 2)
    {
      int16_t d_prev = (i > 0) ? scratch[i - 1] : scratch[i + 1];
      int16_t d_next = (i + 1 < height) ? scratch[i + 1] : scratch[i - 1];
      int16_t prod_sum = (int16_t)(mf15(d_prev, synth_coeff_1) + mf15(d_next, synth_coeff_1) + 4);
      scratch[i] = (int16_t)(scratch[i] + (prod_sum >> 3));
    }

    // Step 0 — Undo α: D[n] += sum + frac×sum  [additive decomposition]
    for(uint32_t i = 1 - parity; i < height; i += 2)
    {
      int16_t s_prev = (i > 0) ? scratch[i - 1] : scratch[i + 1];
      int16_t s_next = (i + 1 < height) ? scratch[i + 1] : scratch[i - 1];
      int16_t even_sum = (int16_t)(s_prev + s_next);
      int16_t frac = mf15(even_sum, synth_coeff_0_frac);
      scratch[i] = (int16_t)(scratch[i] + even_sum + frac);
    }

    // Scatter from contiguous scratch back to strided destination column
    for(uint32_t i = 0; i < height; ++i)
    {
      *dest = scratch[i];
      dest += strideDest;
    }
  }

  /**************************************************************************
   *  Scalar Vertical Synthesis with DC Shift + Clamp
   *
   *  Used only at the FINAL resolution level of the inverse DWT.  After
   *  synthesis produces pixel-domain values, this function applies:
   *
   *    pixel = clamp(synth_value + dc_shift, dc_min, dc_max)
   *
   *  For an N-bit unsigned image, the typical values are:
   *    dc_shift = 2^(N-1)      (e.g. 128 for 8-bit, 2048 for 12-bit)
   *    dc_min   = 0
   *    dc_max   = 2^N - 1      (e.g. 255 for 8-bit, 4095 for 12-bit)
   *
   *  All arithmetic stays in int16 since dc_shift and dc_max fit for ≤12-bit.
   **************************************************************************/
  static void scalar_v_synth_16_97_dc(int16_t* scratch, uint32_t height, const int16_t* bandL,
                                      uint32_t strideL, const int16_t* bandH, uint32_t strideH,
                                      int16_t* dest, uint32_t strideDest, uint32_t parity,
                                      uint32_t sn, uint32_t dn, int16_t dc, int16_t dcMin,
                                      int16_t dcMax)
  {
    // Synthesize into scratch first (using scratch as both workspace and
    // temporary destination with stride=1)
    scalar_v_synth_16_97(scratch, height, bandL, strideL, bandH, strideH, scratch, 1, parity, sn,
                         dn);

    // Apply DC shift and clamp, scatter to strided destination column
    for(uint32_t i = 0; i < height; ++i)
    {
      int16_t val = (int16_t)std::clamp<int32_t>((int32_t)scratch[i] + dc, dcMin, dcMax);
      *dest = val;
      dest += strideDest;
    }
  }

  /**************************************************************************
   *  SIMD Horizontal 1D 16-bit 9/7 Synthesis (one row)
   *
   *  Processes one row using separated even/odd arrays for cache-friendly
   *  SIMD lifting.  Instead of interleaving L/H into scratch (which creates
   *  stride-2 access patterns hostile to SIMD), we keep even-phase (E) and
   *  odd-phase (O) samples in separate contiguous arrays within scratch:
   *
   *    scratch[0..sn-1]    = E[k]  (from L band, scaled by K)
   *    scratch[sn..sn+dn-1] = O[k]  (from H band, scaled by 2/K)
   *
   *  The four lifting steps then operate on contiguous arrays with simple
   *  offset loads (k-1 or k+1), which SIMD handles trivially via LoadU.
   *  After lifting, E and O are interleaved into dest using Highway's
   *  InterleaveLower/InterleaveUpper.
   *
   *  Neighbor relationships by parity:
   *    parity=0: interleave order = E,O,E,O,...
   *      Steps targeting E: left = O[k-1], right = O[k]
   *      Steps targeting O: left = E[k],   right = E[k+1]
   *    parity=1: interleave order = O,E,O,E,...
   *      Steps targeting E: left = O[k],   right = O[k+1]
   *      Steps targeting O: left = E[k-1], right = E[k]
   **************************************************************************/
  static void hwy_h_synth_16_97(int16_t* scratch, uint32_t width, const int16_t* bandL,
                                const int16_t* bandH, int16_t* dest, uint32_t parity, uint32_t sn,
                                uint32_t dn)
  {
    if(width == 0)
      return;
    if(width == 1)
    {
      dest[0] = (parity == 0) ? apply_K_scale(bandL[0]) : apply_invK_scale(bandH[0]);
      return;
    }

    const HWY_FULL(int16_t) di;
    const auto L = (uint32_t)Lanes(di);
    const auto vK_frac = Set(di, scale_K_frac);
    const auto vinvK_frac = Set(di, scale_invK_frac);
    const auto vc3 = Set(di, synth_coeff_3);
    const auto vc2 = Set(di, synth_coeff_2);
    const auto vc1 = Set(di, synth_coeff_1);
    const auto vc0_frac = Set(di, synth_coeff_0_frac);
    const auto v4 = Set(di, (int16_t)4);

    int16_t* E = scratch;
    int16_t* O = scratch + sn;

    /* Phase 1: Scale L → E with K, H → O with 2/K */
    uint32_t k;
    for(k = 0; k + L <= sn; k += L)
    {
      auto v = LoadU(di, bandL + k);
      StoreU(v + MulFixedPoint15(v, vK_frac), di, E + k);
    }
    for(; k < sn; k++)
      E[k] = apply_K_scale(bandL[k]);

    for(k = 0; k + L <= dn; k += L)
    {
      auto v = LoadU(di, bandH + k);
      StoreU(v + MulFixedPoint15(v, vinvK_frac), di, O + k);
    }
    for(; k < dn; k++)
      O[k] = apply_invK_scale(bandH[k]);

    /* Phase 2: Lifting steps on separated E[0..sn-1] and O[0..dn-1] */

    if(parity == 0)
    {
      /* --- Step 3 (δ): E[k] -= δ × (O[k-1] + O[k]) --- */
      /* k=0 boundary: O[-1] mirrors to O[0] */
      E[0] = (int16_t)(E[0] - mf15((int16_t)(O[0] + O[0]), synth_coeff_3));
      /* SIMD bulk: k=1..min(sn,dn)-L, O[k-1] and O[k] both in-bounds */
      {
        uint32_t simd_end = std::min(sn, dn);
        for(k = 1; k + L <= simd_end; k += L)
        {
          auto ol = LoadU(di, O + k - 1);
          auto or_ = LoadU(di, O + k);
          auto e = LoadU(di, E + k);
          StoreU(e - MulFixedPoint15(ol + or_, vc3), di, E + k);
        }
      }
      /* Scalar tail (remaining + boundary if sn > dn) */
      for(; k < sn; k++)
      {
        int16_t ol = O[k - 1];
        int16_t or_ = (k < dn) ? O[k] : O[dn - 1];
        E[k] = (int16_t)(E[k] - mf15((int16_t)(ol + or_), synth_coeff_3));
      }

      /* --- Step 2 (γ): O[k] -= γ × (E[k] + E[k+1]) --- */
      {
        uint32_t simd_end = std::min(dn, sn > 0 ? sn - 1 : 0u);
        for(k = 0; k + L <= simd_end; k += L)
        {
          auto el = LoadU(di, E + k);
          auto er = LoadU(di, E + k + 1);
          auto o = LoadU(di, O + k);
          StoreU(o - MulFixedPoint15(el + er, vc2), di, O + k);
        }
      }
      for(; k < dn; k++)
      {
        int16_t el = E[k];
        int16_t er = (k + 1 < sn) ? E[k + 1] : E[sn - 1];
        O[k] = (int16_t)(O[k] - mf15((int16_t)(el + er), synth_coeff_2));
      }

      /* --- Step 1 (β): E[k] += (mf15(O[k-1],c) + mf15(O[k],c) + 4) >> 3 --- */
      E[0] = (int16_t)(E[0] + ((mf15(O[0], synth_coeff_1) + mf15(O[0], synth_coeff_1) + 4) >> 3));
      {
        uint32_t simd_end = std::min(sn, dn);
        for(k = 1; k + L <= simd_end; k += L)
        {
          auto ol = LoadU(di, O + k - 1);
          auto or_ = LoadU(di, O + k);
          auto e = LoadU(di, E + k);
          auto ps = MulFixedPoint15(ol, vc1) + MulFixedPoint15(or_, vc1) + v4;
          StoreU(e + ShiftRight<3>(ps), di, E + k);
        }
      }
      for(; k < sn; k++)
      {
        int16_t ol = O[k - 1];
        int16_t or_ = (k < dn) ? O[k] : O[dn - 1];
        int16_t ps = (int16_t)(mf15(ol, synth_coeff_1) + mf15(or_, synth_coeff_1) + 4);
        E[k] = (int16_t)(E[k] + (ps >> 3));
      }

      /* --- Step 0 (α): O[k] += sum + mf15(sum, frac) --- */
      {
        uint32_t simd_end = std::min(dn, sn > 0 ? sn - 1 : 0u);
        for(k = 0; k + L <= simd_end; k += L)
        {
          auto el = LoadU(di, E + k);
          auto er = LoadU(di, E + k + 1);
          auto o = LoadU(di, O + k);
          auto sum = el + er;
          StoreU(o + sum + MulFixedPoint15(sum, vc0_frac), di, O + k);
        }
      }
      for(; k < dn; k++)
      {
        int16_t el = E[k];
        int16_t er = (k + 1 < sn) ? E[k + 1] : E[sn - 1];
        int16_t sum = (int16_t)(el + er);
        O[k] = (int16_t)(O[k] + sum + mf15(sum, synth_coeff_0_frac));
      }
    }
    else /* parity == 1 */
    {
      /* --- Step 3 (δ): E[k] -= δ × (O[k] + O[k+1]) --- */
      {
        uint32_t simd_end = std::min(sn, dn > 0 ? dn - 1 : 0u);
        for(k = 0; k + L <= simd_end; k += L)
        {
          auto ol = LoadU(di, O + k);
          auto or_ = LoadU(di, O + k + 1);
          auto e = LoadU(di, E + k);
          StoreU(e - MulFixedPoint15(ol + or_, vc3), di, E + k);
        }
      }
      for(; k < sn; k++)
      {
        int16_t ol = O[k];
        int16_t or_ = (k + 1 < dn) ? O[k + 1] : O[dn - 1];
        E[k] = (int16_t)(E[k] - mf15((int16_t)(ol + or_), synth_coeff_3));
      }

      /* --- Step 2 (γ): O[k] -= γ × (E[k-1] + E[k]) --- */
      O[0] = (int16_t)(O[0] - mf15((int16_t)(E[0] + E[0]), synth_coeff_2));
      {
        uint32_t simd_end = std::min(dn, sn);
        for(k = 1; k + L <= simd_end; k += L)
        {
          auto el = LoadU(di, E + k - 1);
          auto er = LoadU(di, E + k);
          auto o = LoadU(di, O + k);
          StoreU(o - MulFixedPoint15(el + er, vc2), di, O + k);
        }
      }
      for(; k < dn; k++)
      {
        int16_t el = (k > 0) ? E[k - 1] : E[0];
        int16_t er = (k < sn) ? E[k] : E[sn - 1];
        O[k] = (int16_t)(O[k] - mf15((int16_t)(el + er), synth_coeff_2));
      }

      /* --- Step 1 (β): E[k] += (mf15(O[k],c) + mf15(O[k+1],c) + 4) >> 3 --- */
      {
        uint32_t simd_end = std::min(sn, dn > 0 ? dn - 1 : 0u);
        for(k = 0; k + L <= simd_end; k += L)
        {
          auto ol = LoadU(di, O + k);
          auto or_ = LoadU(di, O + k + 1);
          auto e = LoadU(di, E + k);
          auto ps = MulFixedPoint15(ol, vc1) + MulFixedPoint15(or_, vc1) + v4;
          StoreU(e + ShiftRight<3>(ps), di, E + k);
        }
      }
      for(; k < sn; k++)
      {
        int16_t ol = O[k];
        int16_t or_ = (k + 1 < dn) ? O[k + 1] : O[dn - 1];
        int16_t ps = (int16_t)(mf15(ol, synth_coeff_1) + mf15(or_, synth_coeff_1) + 4);
        E[k] = (int16_t)(E[k] + (ps >> 3));
      }

      /* --- Step 0 (α): O[k] += sum + mf15(sum, frac) where sum = E[k-1]+E[k] --- */
      {
        int16_t sum0 = (int16_t)(E[0] + E[0]);
        O[0] = (int16_t)(O[0] + sum0 + mf15(sum0, synth_coeff_0_frac));
      }
      {
        uint32_t simd_end = std::min(dn, sn);
        for(k = 1; k + L <= simd_end; k += L)
        {
          auto el = LoadU(di, E + k - 1);
          auto er = LoadU(di, E + k);
          auto o = LoadU(di, O + k);
          auto sum = el + er;
          StoreU(o + sum + MulFixedPoint15(sum, vc0_frac), di, O + k);
        }
      }
      for(; k < dn; k++)
      {
        int16_t el = (k > 0) ? E[k - 1] : E[0];
        int16_t er = (k < sn) ? E[k] : E[sn - 1];
        int16_t sum = (int16_t)(el + er);
        O[k] = (int16_t)(O[k] + sum + mf15(sum, synth_coeff_0_frac));
      }
    }

    /* Phase 3: Interleave E and O into dest */
    {
      const int16_t* first = (parity == 0) ? E : O;
      const int16_t* second = (parity == 0) ? O : E;
      uint32_t fn = (parity == 0) ? sn : dn;
      uint32_t sn2 = (parity == 0) ? dn : sn;
      uint32_t i = 0, d = 0;
      /* SIMD interleave: L elements of first + L of second → 2L dest */
      for(; i + L <= fn && i + L <= sn2; i += L)
      {
        auto f = LoadU(di, first + i);
        auto s = LoadU(di, second + i);
        StoreU(InterleaveWholeLower(di, f, s), di, dest + d);
        StoreU(InterleaveWholeUpper(di, f, s), di, dest + d + L);
        d += 2 * L;
      }
      /* Scalar interleave for remainder */
      for(; i < fn || i < sn2; i++)
      {
        if(i < fn)
          dest[d++] = first[i];
        if(i < sn2)
          dest[d++] = second[i];
      }
    }
  }

  static uint32_t GetHWY_PLL_COLS_16_97(void)
  {
    const HWY_FULL(int16_t) di16;
    return 2 * (uint32_t)Lanes(di16);
  }

  static const uint32_t HWY_PLL_COLS_16_97 = GetHWY_PLL_COLS_16_97();

  /**************************************************************************
   *  SIMD Vertical 16-bit 9/7 Synthesis (multi-column)
   *
   *  Processes HWY_PLL_COLS_16_97 = 2×Lanes(int16_t) columns at once.
   *  Uses Highway MulFixedPoint15 (maps to VPMULHRSW on x86, VQRDMULH on
   *  ARM NEON) for Q1.15 fixed-point arithmetic.
   *
   *  Algorithm:
   *    Phase 1 — Interleave L/H bands into scratch with K/invK scaling
   *    Phase 2 — Four synthesis lifting steps (δ, γ, β, α)
   *    Phase 3 — Scatter to strided destination with optional DC shift + clamp
   **************************************************************************/
  static void hwy_v_synth_16_97(int16_t* scratch, uint32_t height, const int16_t* bandL,
                                uint32_t strideL, const int16_t* bandH, uint32_t strideH,
                                int16_t* dest, uint32_t strideDest, uint32_t parity,
                                [[maybe_unused]] uint32_t sn, [[maybe_unused]] uint32_t dn,
                                int16_t dc, int16_t dcMin, int16_t dcMax)
  {
    const HWY_FULL(int16_t) di;
    const auto L = (uint32_t)Lanes(di);
    const uint32_t PLL = HWY_PLL_COLS_16_97;

    const auto vcoeff3 = Set(di, synth_coeff_3);
    const auto vcoeff2 = Set(di, synth_coeff_2);
    const auto vcoeff1 = Set(di, synth_coeff_1);
    const auto vcoeff0_frac = Set(di, synth_coeff_0_frac);
    const auto vK_frac = Set(di, scale_K_frac);
    const auto vinvK_frac = Set(di, scale_invK_frac);
    const auto v4 = Set(di, (int16_t)4);

    /* ------------------------------------------------------------------ */
    /*  Phase 1: Interleave L/H into scratch with K / (2/K) scaling       */
    /* ------------------------------------------------------------------ */
    uint32_t lIdx = 0, hIdx = 0;
    for(uint32_t i = 0; i < height; ++i)
    {
      if((i + parity) % 2 == 0)
      {
        auto v0 = LoadU(di, bandL + (size_t)lIdx * strideL);
        auto v1 = LoadU(di, bandL + (size_t)lIdx * strideL + L);
        v0 = v0 + MulFixedPoint15(v0, vK_frac);
        v1 = v1 + MulFixedPoint15(v1, vK_frac);
        Store(v0, di, scratch + (size_t)i * PLL);
        Store(v1, di, scratch + (size_t)i * PLL + L);
        lIdx++;
      }
      else
      {
        auto v0 = LoadU(di, bandH + (size_t)hIdx * strideH);
        auto v1 = LoadU(di, bandH + (size_t)hIdx * strideH + L);
        v0 = v0 + MulFixedPoint15(v0, vinvK_frac);
        v1 = v1 + MulFixedPoint15(v1, vinvK_frac);
        Store(v0, di, scratch + (size_t)i * PLL);
        Store(v1, di, scratch + (size_t)i * PLL + L);
        hIdx++;
      }
    }

    /* ------------------------------------------------------------------ */
    /*  Phase 2: Four synthesis lifting steps                             */
    /* ------------------------------------------------------------------ */

    // Step 3 — δ: S[n] -= δ × (D[n-1] + D[n])
    for(uint32_t i = parity; i < height; i += 2)
    {
      size_t pr = (i > 0) ? (i - 1) : (i + 1);
      size_t nr = (i + 1 < height) ? (i + 1) : (i - 1);
      auto p0 = Load(di, scratch + pr * PLL);
      auto p1 = Load(di, scratch + pr * PLL + L);
      auto n0 = Load(di, scratch + nr * PLL);
      auto n1 = Load(di, scratch + nr * PLL + L);
      auto t0 = Load(di, scratch + (size_t)i * PLL);
      auto t1 = Load(di, scratch + (size_t)i * PLL + L);
      t0 = t0 - MulFixedPoint15(p0 + n0, vcoeff3);
      t1 = t1 - MulFixedPoint15(p1 + n1, vcoeff3);
      Store(t0, di, scratch + (size_t)i * PLL);
      Store(t1, di, scratch + (size_t)i * PLL + L);
    }

    // Step 2 — γ: D[n] -= γ × (S[n] + S[n+1])
    for(uint32_t i = 1 - parity; i < height; i += 2)
    {
      size_t pr = (i > 0) ? (i - 1) : (i + 1);
      size_t nr = (i + 1 < height) ? (i + 1) : (i - 1);
      auto p0 = Load(di, scratch + pr * PLL);
      auto p1 = Load(di, scratch + pr * PLL + L);
      auto n0 = Load(di, scratch + nr * PLL);
      auto n1 = Load(di, scratch + nr * PLL + L);
      auto t0 = Load(di, scratch + (size_t)i * PLL);
      auto t1 = Load(di, scratch + (size_t)i * PLL + L);
      t0 = t0 - MulFixedPoint15(p0 + n0, vcoeff2);
      t1 = t1 - MulFixedPoint15(p1 + n1, vcoeff2);
      Store(t0, di, scratch + (size_t)i * PLL);
      Store(t1, di, scratch + (size_t)i * PLL + L);
    }

    // Step 1 — β: S[n] += |β|×(D[n-1]+D[n])  MULTIPLY-FIRST, ×8/>>3
    for(uint32_t i = parity; i < height; i += 2)
    {
      size_t pr = (i > 0) ? (i - 1) : (i + 1);
      size_t nr = (i + 1 < height) ? (i + 1) : (i - 1);
      auto p0 = Load(di, scratch + pr * PLL);
      auto p1 = Load(di, scratch + pr * PLL + L);
      auto n0 = Load(di, scratch + nr * PLL);
      auto n1 = Load(di, scratch + nr * PLL + L);
      auto t0 = Load(di, scratch + (size_t)i * PLL);
      auto t1 = Load(di, scratch + (size_t)i * PLL + L);
      auto ps0 = MulFixedPoint15(p0, vcoeff1) + MulFixedPoint15(n0, vcoeff1) + v4;
      auto ps1 = MulFixedPoint15(p1, vcoeff1) + MulFixedPoint15(n1, vcoeff1) + v4;
      t0 = t0 + ShiftRight<3>(ps0);
      t1 = t1 + ShiftRight<3>(ps1);
      Store(t0, di, scratch + (size_t)i * PLL);
      Store(t1, di, scratch + (size_t)i * PLL + L);
    }

    // Step 0 — α: D[n] += sum + MulFixedPoint15(sum, frac)  additive decomposition
    for(uint32_t i = 1 - parity; i < height; i += 2)
    {
      size_t pr = (i > 0) ? (i - 1) : (i + 1);
      size_t nr = (i + 1 < height) ? (i + 1) : (i - 1);
      auto p0 = Load(di, scratch + pr * PLL);
      auto p1 = Load(di, scratch + pr * PLL + L);
      auto n0 = Load(di, scratch + nr * PLL);
      auto n1 = Load(di, scratch + nr * PLL + L);
      auto t0 = Load(di, scratch + (size_t)i * PLL);
      auto t1 = Load(di, scratch + (size_t)i * PLL + L);
      auto sum0 = p0 + n0;
      auto sum1 = p1 + n1;
      t0 = t0 + sum0 + MulFixedPoint15(sum0, vcoeff0_frac);
      t1 = t1 + sum1 + MulFixedPoint15(sum1, vcoeff0_frac);
      Store(t0, di, scratch + (size_t)i * PLL);
      Store(t1, di, scratch + (size_t)i * PLL + L);
    }

    /* ------------------------------------------------------------------ */
    /*  Phase 3: Scatter to strided destination with optional DC shift     */
    /* ------------------------------------------------------------------ */
    if(dc != 0)
    {
      const auto vdc = Set(di, dc);
      const auto vmin = Set(di, dcMin);
      const auto vmax = Set(di, dcMax);
      for(uint32_t i = 0; i < height; ++i)
      {
        auto v0 = Clamp(Load(di, scratch + (size_t)i * PLL) + vdc, vmin, vmax);
        auto v1 = Clamp(Load(di, scratch + (size_t)i * PLL + L) + vdc, vmin, vmax);
        StoreU(v0, di, dest + (size_t)i * strideDest);
        StoreU(v1, di, dest + (size_t)i * strideDest + L);
      }
    }
    else
    {
      for(uint32_t i = 0; i < height; ++i)
      {
        StoreU(Load(di, scratch + (size_t)i * PLL), di, dest + (size_t)i * strideDest);
        StoreU(Load(di, scratch + (size_t)i * PLL + L), di, dest + (size_t)i * strideDest + L);
      }
    }
  }

} // namespace HWY_NAMESPACE
} // namespace grk
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace grk
{

HWY_EXPORT(GetHWY_PLL_COLS_16_97);
HWY_EXPORT(hwy_v_synth_16_97);
HWY_EXPORT(hwy_h_synth_16_97);

static uint32_t get_PLL_COLS_16_97(void)
{
  static uint32_t value = HWY_DYNAMIC_DISPATCH(GetHWY_PLL_COLS_16_97)();
  return value;
}

/******************************************************************************
 *  tile_16_97 — Top-level 16-bit 9/7 inverse DWT
 *
 *  Orchestrates the multi-resolution synthesis for one tile component.
 *  For each resolution level (from lowest to highest), performs:
 *    1. Horizontal synthesis: combines LL+HL → L row, LH+HH → H row
 *    2. Vertical synthesis: combines L rows + H rows → output resolution
 *
 *  Thread parallelism: rows (horizontal) and columns (vertical) are split
 *  across tasks.  Each task uses its own scratch buffer from the pool.
 ******************************************************************************/
bool WaveletReverse::tile_16_97(void)
{
  if(numres_ == 1U)
    return true;

  auto bandLL = tilec_->resolutions_;
  auto tileBuffer = tilec_->getWindow16();

  uint32_t num_threads = (uint32_t)TFSingleton::num_threads();
  horizPool16_ = std::make_unique<dwt_scratch<int16_t>[]>(num_threads);
  vertPool16_ = std::make_unique<dwt_scratch<int16_t>[]>(num_threads);

  for(uint8_t res = 1; res < numres_; ++res)
  {
    horiz16_.sn = bandLL->width();
    vert16_.sn = bandLL->height();
    for(uint32_t i = 0; i < num_threads; ++i)
    {
      horizPool16_[i].sn = bandLL->width();
      vertPool16_[i].sn = bandLL->height();
    }
    ++bandLL;
    auto resWidth = bandLL->width();
    auto resHeight = bandLL->height();
    if(resWidth == 0 || resHeight == 0)
      continue;
    horiz16_.dn = resWidth - horiz16_.sn;
    horiz16_.parity = bandLL->x0 & 1;
    vert16_.dn = resHeight - vert16_.sn;
    vert16_.parity = bandLL->y0 & 1;

    for(uint32_t i = 0; i < num_threads; ++i)
    {
      horizPool16_[i].dn = resWidth - horizPool16_[i].sn;
      horizPool16_[i].parity = bandLL->x0 & 1;
      if(!poolData_ || !poolData_->isAllocated())
      {
        grklog.error("tile_16_97: pool data not allocated");
        return false;
      }
      horizPool16_[i].allocatedMem = (int16_t*)poolData_->getHoriz(i);
      horizPool16_[i].mem = (int16_t*)poolData_->getHoriz(i);

      vertPool16_[i].dn = resHeight - vertPool16_[i].sn;
      vertPool16_[i].parity = bandLL->y0 & 1;
      vertPool16_[i].allocatedMem = (int16_t*)poolData_->getVert(i);
      vertPool16_[i].mem = (int16_t*)poolData_->getVert(i);
    }
    h_16_97(res, tileBuffer, resHeight);
    v_16_97(res, tileBuffer, resWidth);
  }

  return true;
}

/******************************************************************************
 *  Horizontal 16-bit 9/7 strip processing
 ******************************************************************************/
void WaveletReverse::h_strip_16_97(const dwt_scratch<int16_t>* scratch, uint32_t hMin,
                                   uint32_t hMax, Buffer2dSimple<int16_t> winL,
                                   Buffer2dSimple<int16_t> winH, Buffer2dSimple<int16_t> winDest)
{
  for(uint32_t j = hMin; j < hMax; ++j)
  {
    HWY_DYNAMIC_DISPATCH(hwy_h_synth_16_97)
    (scratch->mem, scratch->sn + scratch->dn, winL.buf_, winH.buf_, winDest.buf_, scratch->parity,
     scratch->sn, scratch->dn);
    winL.incY_IN_PLACE(1);
    winH.incY_IN_PLACE(1);
    winDest.incY_IN_PLACE(1);
  }
}

void WaveletReverse::h_16_97(uint8_t res, TileComponentWindow<int16_t>* tileBuffer,
                             uint32_t resHeight)
{
  uint32_t num_threads = (uint32_t)TFSingleton::num_threads();
  Buffer2dSimple<int16_t> winL, winH, winDest;
  auto imageComponentFlow = ((DecompressScheduler*)scheduler_)->getImageComponentFlow(compno_);
  if(!imageComponentFlow)
    return;
  auto resFlow = imageComponentFlow->getResflow(res - 1);
  if(!resFlow)
    return;
  uint32_t numTasks[2] = {0, 0};
  uint32_t height[2] = {0, 0};

  for(uint32_t orient = 0; orient < 2; ++orient)
  {
    height[orient] = (orient == 0) ? vert16_.sn : resHeight - vert16_.sn;
    numTasks[orient] = height[orient] < num_threads ? height[orient] : num_threads;
  }
  for(uint32_t orient = 0; orient < 2; ++orient)
  {
    if(height[orient] == 0)
      continue;
    if(orient == 0)
    {
      winL = tileBuffer->getResWindowBufferSimple((uint8_t)(res - 1U));
      winH = tileBuffer->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_HL);
      winDest = tileBuffer->getResWindowBufferSplitSimple(res, SPLIT_L);
    }
    else
    {
      winL = tileBuffer->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_LH);
      winH = tileBuffer->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_HH);
      winDest = tileBuffer->getResWindowBufferSplitSimple(res, SPLIT_H);
    }

    uint32_t heightIncr = height[orient] / numTasks[orient];
    for(uint32_t j = 0; j < numTasks[orient]; ++j)
    {
      auto hMin = j * heightIncr;
      auto hMax = j < (numTasks[orient] - 1U) ? (j + 1U) * heightIncr : height[orient];
      uint32_t sn = horiz16_.sn;
      uint32_t dn = horiz16_.dn;
      uint32_t par = horiz16_.parity;
      resFlow->waveletHoriz_->nextTask().work([this, sn, dn, par, winL, winH, winDest, hMin, hMax] {
        horizPool16_[TFSingleton::workerId()].sn = sn;
        horizPool16_[TFSingleton::workerId()].dn = dn;
        horizPool16_[TFSingleton::workerId()].parity = par;
        h_strip_16_97(&horizPool16_[TFSingleton::workerId()], hMin, hMax, winL, winH, winDest);
      });
      winL.incY_IN_PLACE(heightIncr);
      winH.incY_IN_PLACE(heightIncr);
      winDest.incY_IN_PLACE(heightIncr);
    }
  }
}

/******************************************************************************
 *  Vertical 16-bit 9/7 strip processing
 ******************************************************************************/
void WaveletReverse::v_16_97(const dwt_scratch<int16_t>* scratch, Buffer2dSimple<int16_t> winL,
                             Buffer2dSimple<int16_t> winH, Buffer2dSimple<int16_t> winDest,
                             uint32_t nb_cols, DcShiftParam dcShift)
{
  const uint32_t height = scratch->sn + scratch->dn;
  if(height == 0)
    return;

  int16_t dc = dcShift.enabled ? (int16_t)dcShift.shift : 0;
  int16_t dcMin = (int16_t)dcShift.min;
  int16_t dcMax = (int16_t)dcShift.max;

  if(height > 1 && nb_cols == get_PLL_COLS_16_97())
  {
    HWY_DYNAMIC_DISPATCH(hwy_v_synth_16_97)
    (scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winH.stride_, winDest.buf_,
     winDest.stride_, scratch->parity, scratch->sn, scratch->dn, dc, dcMin, dcMax);
    return;
  }

  for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
  {
    if(dcShift.enabled)
    {
      HWY_NAMESPACE::scalar_v_synth_16_97_dc(
          scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winH.stride_, winDest.buf_,
          winDest.stride_, scratch->parity, scratch->sn, scratch->dn, dc, dcMin, dcMax);
    }
    else
    {
      HWY_NAMESPACE::scalar_v_synth_16_97(scratch->mem, height, winL.buf_, winL.stride_, winH.buf_,
                                          winH.stride_, winDest.buf_, winDest.stride_,
                                          scratch->parity, scratch->sn, scratch->dn);
    }
  }
}

void WaveletReverse::v_strip_16_97(const dwt_scratch<int16_t>* scratch, uint32_t wMin,
                                   uint32_t wMax, Buffer2dSimple<int16_t> winL,
                                   Buffer2dSimple<int16_t> winH, Buffer2dSimple<int16_t> winDest,
                                   DcShiftParam dcShift)
{
  uint32_t j;
  const uint32_t pllCols = get_PLL_COLS_16_97();
  for(j = wMin; j + pllCols <= wMax; j += pllCols)
  {
    v_16_97(scratch, winL, winH, winDest, pllCols, dcShift);
    winL.incX_IN_PLACE(pllCols);
    winH.incX_IN_PLACE(pllCols);
    winDest.incX_IN_PLACE(pllCols);
  }
  if(j < wMax)
  {
    v_16_97(scratch, winL, winH, winDest, wMax - j, dcShift);
  }
}

void WaveletReverse::v_16_97(uint8_t res, TileComponentWindow<int16_t>* buf, uint32_t resWidth)
{
  if(resWidth == 0)
    return;
  /* Apply DC shift only on the last resolution level */
  DcShiftParam dcShift = (res == numres_ - 1) ? dcShift_ : DcShiftParam{};
  uint32_t num_threads = (uint32_t)TFSingleton::num_threads();
  auto winL = buf->getResWindowBufferSplitSimple(res, SPLIT_L);
  auto winH = buf->getResWindowBufferSplitSimple(res, SPLIT_H);
  auto winDest = buf->getResWindowBufferSimple(res);
  auto imageComponentFlow = ((DecompressScheduler*)scheduler_)->getImageComponentFlow(compno_);
  if(!imageComponentFlow)
    return;
  auto resFlow = imageComponentFlow->getResflow(res - 1);
  if(!resFlow)
    return;
  const uint32_t numTasks = resWidth < num_threads ? resWidth : num_threads;
  uint32_t widthIncr = resWidth / numTasks;
  for(uint32_t j = 0; j < numTasks; j++)
  {
    auto wMin = j * widthIncr;
    auto wMax = j < (numTasks - 1U) ? (j + 1U) * widthIncr : resWidth;
    uint32_t sn = vert16_.sn;
    uint32_t dn = vert16_.dn;
    uint32_t par = vert16_.parity;
    resFlow->waveletVert_->nextTask().work(
        [this, sn, dn, par, wMin, wMax, winL, winH, winDest, dcShift] {
          vertPool16_[TFSingleton::workerId()].dn = dn;
          vertPool16_[TFSingleton::workerId()].sn = sn;
          vertPool16_[TFSingleton::workerId()].parity = par;

          v_strip_16_97(&vertPool16_[TFSingleton::workerId()], wMin, wMax, winL, winH, winDest,
                        dcShift);
        });
    winL.incX_IN_PLACE(widthIncr);
    winH.incX_IN_PLACE(widthIncr);
    winDest.incX_IN_PLACE(widthIncr);
  }
}

} // namespace grk
#endif
