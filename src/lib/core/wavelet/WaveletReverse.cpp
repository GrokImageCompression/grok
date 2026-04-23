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

/***********************************************************************

Inverse Update (even)
F.3, page 118, ITU-T Rec. T.800 final draft
even -= (previous + next + 2) >> 2;

Inverse Predict (odd)
F.3, page 118, ITU-T Rec. T.800 final draft
odd += (previous + next) >> 1;

************************************************************************/
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
#include "FlowComponent.h"
#include "IStream.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "CodeStream.h"
#include "PacketLengthCache.h"
#include "ICoder.h"
#include "CoderPool.h"
#include "BitIO.h"
#include "ImageComponentFlow.h"
#include "TagTree.h"
#include "CodeblockCompress.h"
#include "CodeblockDecompress.h"
#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"
#include "TileFutureManager.h"
#include "FlowComponent.h"
#include "CodecScheduler.h"
#include "TileComponentWindow.h"
#include "WaveletReverse.h"
#include "TileComponent.h"
#include "ITileProcessor.h"
#include "DecompressScheduler.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "wavelet/WaveletReverse.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace grk
{
namespace HWY_NAMESPACE
{
  static uint32_t GetHWY_PLL_COLS_53()
  {
    static const uint32_t value = []() {
      const HWY_FULL(int32_t) di;
      return 2 * (uint32_t)Lanes(di);
    }();
    return value;
  }
  static uint32_t HWY_PLL_COLS_53 = GetHWY_PLL_COLS_53();

  static void hwy_v_final_store_53(const int32_t* scratch, const uint32_t height, int32_t* dest,
                                   const size_t strideDest, int32_t dcShift, int32_t dcMin,
                                   int32_t dcMax)
  {
    const HWY_FULL(int32_t) di;
    if(dcShift != 0)
    {
      const auto vshift = Set(di, dcShift);
      const auto vmin = Set(di, dcMin);
      const auto vmax = Set(di, dcMax);
      for(uint32_t i = 0; i < height; ++i)
      {
        auto v0 = Clamp(Load(di, scratch + HWY_PLL_COLS_53 * i + 0) + vshift, vmin, vmax);
        auto v1 = Clamp(Load(di, scratch + HWY_PLL_COLS_53 * i + Lanes(di)) + vshift, vmin, vmax);
        StoreU(v0, di, &dest[(size_t)i * strideDest + 0]);
        StoreU(v1, di, dest + (size_t)i * strideDest + Lanes(di));
      }
    }
    else
    {
      for(uint32_t i = 0; i < height; ++i)
      {
        StoreU(Load(di, scratch + HWY_PLL_COLS_53 * i + 0), di, &dest[(size_t)i * strideDest + 0]);
        StoreU(Load(di, scratch + HWY_PLL_COLS_53 * i + Lanes(di)), di,
               dest + (size_t)i * strideDest + Lanes(di));
      }
    }
  }
  /** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
   * 16 in AVX2, when top-most pixel is on even coordinate */
  static void hwy_v_p0_53(int32_t* scratch, const uint32_t height, int32_t* bandL,
                          const size_t strideL, int32_t* bandH, const size_t strideH, int32_t* dest,
                          const uint32_t strideDest, int32_t dcShift, int32_t dcMin, int32_t dcMax)
  {
    const HWY_FULL(int32_t) di;
    const auto two = Set(di, 2);
    const auto one = Set(di, 1);

    assert(height > 1);

    /* Note: loads of input even/odd values must be done in an unaligned */
    /* fashion. But stores in tmp can be done with aligned store, since */
    /* the temporary buffer is properly aligned */
    assert((size_t)scratch % (sizeof(int32_t) * Lanes(di)) == 0);

    auto s1n_0 = LoadU(di, bandL + 0);
    auto s1n_1 = LoadU(di, bandL + Lanes(di));
    auto d1n_0 = LoadU(di, bandH);
    auto d1n_1 = LoadU(di, bandH + Lanes(di));

    /* s0n = s1n - ((d1n + 1) >> 1); <==> */
    /* s0n = s1n - ((d1n + d1n + 2) >> 2); */
    auto s0n_0 = s1n_0 - ShiftRight<1>(d1n_0 + one);
    auto s0n_1 = s1n_1 - ShiftRight<1>(d1n_1 + one);

    uint32_t i = 0;
    if(height > 3)
    {
      uint32_t j;
      for(i = 0, j = 1; i < (height - 3); i += 2, j++)
      {
        auto d1c_0 = d1n_0;
        auto s0c_0 = s0n_0;
        auto d1c_1 = d1n_1;
        auto s0c_1 = s0n_1;

        s1n_0 = LoadU(di, bandL + j * strideL);
        s1n_1 = LoadU(di, bandL + j * strideL + Lanes(di));
        d1n_0 = LoadU(di, bandH + j * strideH);
        d1n_1 = LoadU(di, bandH + j * strideH + Lanes(di));

        /*s0n = s1n - ((d1c + d1n + 2) >> 2);*/
        s0n_0 = s1n_0 - ShiftRight<2>(d1c_0 + d1n_0 + two);
        s0n_1 = s1n_1 - ShiftRight<2>(d1c_1 + d1n_1 + two);

        Store(s0c_0, di, scratch + HWY_PLL_COLS_53 * (i + 0));
        Store(s0c_1, di, scratch + HWY_PLL_COLS_53 * (i + 0) + Lanes(di));

        /* d1c + ((s0c + s0n) >> 1) */
        Store(d1c_0 + ShiftRight<1>(s0c_0 + s0n_0), di, scratch + HWY_PLL_COLS_53 * (i + 1) + 0);
        Store(d1c_1 + ShiftRight<1>(s0c_1 + s0n_1), di,
              scratch + HWY_PLL_COLS_53 * (i + 1) + Lanes(di));
      }
    }
    Store(s0n_0, di, scratch + HWY_PLL_COLS_53 * (i + 0) + 0);
    Store(s0n_1, di, scratch + HWY_PLL_COLS_53 * (i + 0) + Lanes(di));

    if(height & 1)
    {
      s1n_0 = LoadU(di, bandL + (size_t)(height >> 1) * strideL);
      /* s0n_len_minus_1 = s1n - ((d1n + 1) >> 1); */
      auto s0n_len_minus_1 = s1n_0 - ShiftRight<2>(d1n_0 + d1n_0 + two);
      Store(s0n_len_minus_1, di, scratch + HWY_PLL_COLS_53 * (height - 1));
      /* d1n + ((s0n + s0n_len_minus_1) >> 1) */
      Store(d1n_0 + ShiftRight<1>(s0n_0 + s0n_len_minus_1), di,
            scratch + HWY_PLL_COLS_53 * (height - 2));

      s1n_1 = LoadU(di, bandL + (size_t)(height >> 1) * strideL + Lanes(di));
      /* s0n_len_minus_1 = s1n - ((d1n + 1) >> 1); */
      s0n_len_minus_1 = s1n_1 - ShiftRight<2>(d1n_1 + d1n_1 + two);
      Store(s0n_len_minus_1, di, scratch + HWY_PLL_COLS_53 * (height - 1) + Lanes(di));
      /* d1n + ((s0n + s0n_len_minus_1) >> 1) */
      Store(d1n_1 + ShiftRight<1>(s0n_1 + s0n_len_minus_1), di,
            scratch + HWY_PLL_COLS_53 * (height - 2) + Lanes(di));
    }
    else
    {
      Store(d1n_0 + s0n_0, di, scratch + HWY_PLL_COLS_53 * (height - 1) + 0);
      Store(d1n_1 + s0n_1, di, scratch + HWY_PLL_COLS_53 * (height - 1) + Lanes(di));
    }
    hwy_v_final_store_53(scratch, height, dest, strideDest, dcShift, dcMin, dcMax);
  }

  /** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
   * 16 in AVX2, when top-most pixel is on odd coordinate */
  static void hwy_v_p1_53(int32_t* scratch, const uint32_t height, int32_t* bandL,
                          const uint32_t strideL, int32_t* bandH, const uint32_t strideH,
                          int32_t* dest, const uint32_t strideDest, int32_t dcShift, int32_t dcMin,
                          int32_t dcMax)
  {
    const HWY_FULL(int32_t) di;
    const auto two = Set(di, 2);

    assert(height > 2);
    /* Note: loads of input even/odd values must be done in a unaligned */
    /* fashion. But stores in buf can be done with aligned store, since */
    /* the temporary buffer is properly aligned */
    assert((size_t)scratch % (sizeof(int32_t) * Lanes(di)) == 0);

    auto d1_0 = LoadU(di, bandH + strideH);

    /* bandL[0] - ((bandH[0] + d1 + 2) >> 2); */
    auto sc_0 = LoadU(di, bandL + 0) - ShiftRight<2>(LoadU(di, bandH + 0) + d1_0 + two);
    Store(LoadU(di, bandH + 0) + sc_0, di, scratch + HWY_PLL_COLS_53 * 0);
    auto d1_1 = LoadU(di, bandH + strideH + Lanes(di));

    /* bandL[0] - ((H[0] + d1 + 2) >> 2); */
    auto sc_1 =
        LoadU(di, bandL + Lanes(di)) - ShiftRight<2>(LoadU(di, bandH + Lanes(di)) + d1_1 + two);
    Store(LoadU(di, bandH + Lanes(di)) + sc_1, di, scratch + HWY_PLL_COLS_53 * 0 + Lanes(di));

    uint32_t i = 1;
    size_t j = 1;
    for(; i < (height - 2 - !(height & 1)); i += 2, j++)
    {
      auto d2_0 = LoadU(di, bandH + (j + 1) * strideH);
      auto d2_1 = LoadU(di, bandH + (j + 1) * strideH + Lanes(di));

      /* sn = bandH[j * stride] - ((d1 + d2 + 2) >> 2); */
      auto sn_0 = LoadU(di, bandL + j * strideL) - ShiftRight<2>(d1_0 + d2_0 + two);
      auto sn_1 = LoadU(di, bandL + j * strideL + Lanes(di)) - ShiftRight<2>(d1_1 + d2_1 + two);

      Store(sc_0, di, scratch + HWY_PLL_COLS_53 * i);
      Store(sc_1, di, scratch + HWY_PLL_COLS_53 * i + Lanes(di));

      /* buf[i + 1] = d1 + ((sn + sc) >> 1); */
      Store(d1_0 + ShiftRight<1>(sn_0 + sc_0), di, scratch + HWY_PLL_COLS_53 * (i + 1) + 0);
      Store(d1_1 + ShiftRight<1>(sn_1 + sc_1), di, scratch + HWY_PLL_COLS_53 * (i + 1) + Lanes(di));

      sc_0 = sn_0;
      sc_1 = sn_1;
      d1_0 = d2_0;
      d1_1 = d2_1;
    }
    Store(sc_0, di, scratch + HWY_PLL_COLS_53 * i);
    Store(sc_1, di, scratch + HWY_PLL_COLS_53 * i + Lanes(di));

    if(!(height & 1))
    {
      /*dn = bandH[(len / 2 - 1) * stride] - ((s1 + 1) >> 1); */
      auto sn_0 =
          LoadU(di, bandL + (size_t)(height / 2 - 1) * strideL) - ShiftRight<2>(d1_0 + d1_0 + two);
      auto sn_1 = LoadU(di, bandL + (size_t)(height / 2 - 1) * strideL + Lanes(di)) -
                  ShiftRight<2>(d1_1 + d1_1 + two);

      /* buf[len - 2] = s1 + ((dn + dc) >> 1); */
      Store(d1_0 + ShiftRight<1>(sn_0 + sc_0), di, scratch + HWY_PLL_COLS_53 * (height - 2) + 0);
      Store(d1_1 + ShiftRight<1>(sn_1 + sc_1), di,
            scratch + HWY_PLL_COLS_53 * (height - 2) + Lanes(di));

      Store(sn_0, di, scratch + HWY_PLL_COLS_53 * (height - 1) + 0);
      Store(sn_1, di, scratch + HWY_PLL_COLS_53 * (height - 1) + Lanes(di));
    }
    else
    {
      Store(d1_0 + sc_0, di, scratch + HWY_PLL_COLS_53 * (height - 1) + 0);
      Store(d1_1 + sc_1, di, scratch + HWY_PLL_COLS_53 * (height - 1) + Lanes(di));
    }
    hwy_v_final_store_53(scratch, height, dest, strideDest, dcShift, dcMin, dcMax);
  }

  // 16-bit 5/3 HWY kernels ///////////////////////////////////////////////////////////////
  //
  // Overview
  // --------
  // These kernels perform the 5/3 reversible (lossless) inverse DWT entirely in int16_t,
  // avoiding the memory and bandwidth cost of int32_t buffers. They are selected at runtime
  // when the image precision plus BIBO headroom fits in 16 bits (see TileProcessor.cpp).
  //
  // Eligibility (decided in TileProcessor::decompressInit):
  //   - Reversible wavelet (qmfbid == 1), whole-tile decoding only.
  //   - MCT components (inverse RCT adds ~1 extra bit): prec + 5 <= 16  →  prec <= 11
  //   - Non-MCT components (DC shift only):              prec + 4 <= 16  →  prec <= 12
  //
  // BIBO (Bounded-Input-Bounded-Output) Gain Analysis
  // --------------------------------------------------
  // The worst-case BIBO gain from any subband coefficient to the reconstructed output
  // is derived from the 5/3 lifting filter coefficients, computed recursively:
  //
  //   Undo-update:  s[n] -= floor((d[n-1] + d[n] + 2) / 4)  →  gain ≤ 3/2 per even sample
  //   Undo-predict: d[n] += floor((s[n] + s[n+1]) / 2)       →  gain ≤ 5/2 per odd sample
  //
  // Multi-level recursion converges because each level's update attenuates by 1/4.
  // Per-subband gains (from QuantizerOJPH.cpp bibo_gains class):
  //   gain_5x3_l[]: 1.000, 1.500, 1.625, 1.688, ... → 1.716
  //   gain_5x3_h[]: 2.000, 2.500, 2.750, 2.805, ... → 2.867
  //
  // 2D worst case: max(gain_h)^2 ≈ 2.867^2 ≈ 8.22 = 2^3.04
  //   - For ≤5 levels: overall gain < 2^3
  //   - For >5 levels: converges to ~2^3.04
  //
  // For 16-bit synthesis, headroom includes ~3 bits DWT gain + safety margin:
  //   - Non-MCT (rct_comp ≤ 1): 4 bits → max prec 12
  //   - MCT chrominance (rct_comp ≥ 2): 5 bits → max prec 11
  // See doc/16BitDWT.md for full first-principles derivation.
  //
  // Overflow-Safe Averaging
  // -----------------------
  // The 5/3 DWT update and predict steps require computing averages of two int16_t values.
  // In SIMD (HWY) code, arithmetic stays within int16 lanes — there is NO implicit
  // promotion to int32 as in scalar C++ code. Therefore (a + b) can overflow int16.
  //
  // Two overflow-safe averaging functions are provided:
  //
  // 1. update_avg_16_53(): Computes floor((a + b + 2) / 4)
  //    - Used in the 5/3 update step: even -= floor((odd_prev + odd_next + 2) / 4)
  //    - Technique: Map signed int16 → unsigned via XOR 0x8000, then use the hardware
  //      unsigned averaging instruction (AverageRound / _mm256_avg_epu16) which computes
  //      floor((a + b + 1) / 2) with a 17-bit intermediate — no overflow.
  //    - Identity chain: floor((a+b+2)/4) = (floor((a+b)/2) + 1) >> 1
  //
  // 2. predict_avg_16_53(): Computes floor((a + b) / 2)
  //    - Used in the 5/3 predict step: odd += floor((even_prev + even_next) / 2)
  //    - Identity: (a + b) >> 1 = (a >> 1) + (b >> 1) + ((a & b) & 1)
  //    - Each term is individually safe: ShiftRight<1> halves the range,
  //      and the correction term (a & b) & 1 is 0 or 1.
  //
  // Note: The scalar (non-SIMD) fallback code does NOT need these techniques because
  // C++ integer promotion automatically widens int16_t operands to int before arithmetic.

  static uint32_t GetHWY_PLL_COLS_16_53()
  {
    static const uint32_t value = []() {
      const HWY_FULL(int16_t) di;
      return 2 * (uint32_t)Lanes(di);
    }();
    return value;
  }
  static uint32_t HWY_PLL_COLS_16_53 = GetHWY_PLL_COLS_16_53();

  /**
   * Compute floor((a + b + 2) / 4) for signed int16 vectors using unsigned
   * averaging, to avoid intermediate overflow in the 5/3 DWT update step.
   *
   * Identity: floor((a + b + 2) / 4) == (floor((a + b) / 2) + 1) >> 1
   *
   * Uses _mm256_avg_epu16 (via HWY AverageRound) which computes
   * floor((a + b + 1) / 2) with a 17-bit intermediate — no overflow.
   * Sign conversion via XOR 0x8000 (signed ↔ unsigned).
   */
  static HWY_INLINE auto update_avg_16_53(const HWY_FULL(int16_t) di,
                                          decltype(Zero(HWY_FULL(int16_t)())) a,
                                          decltype(Zero(HWY_FULL(int16_t)())) b)
      -> decltype(Zero(HWY_FULL(int16_t)()))
  {
    const HWY_FULL(uint16_t) du;
    const auto u_sign = Set(du, (uint16_t)0x8000u);
    const auto u_max = Set(du, (uint16_t)0x7FFFu);

    // a XOR 0x8000 : flip sign bit → unsigned representation
    auto a_u = BitCast(du, a) ^ u_sign;
    // b + 0x7FFF (mod 2^16) == b_unsigned − 1
    auto b_biased = BitCast(du, b) + u_max;
    // AverageRound: floor((a_u + b_biased + 1) / 2) = floor((a_u + b_u) / 2)
    auto avg = AverageRound(a_u, b_biased);
    // avg − 0x7FFF = floor((a+b)/2) + 1  (in signed domain)
    auto step = BitCast(di, avg - u_max);
    // (floor((a+b)/2) + 1) >> 1 = floor((a+b+2)/4)
    return ShiftRight<1>(step);
  }

  /**
   * Compute floor((a + b) / 2) for signed int16 vectors without overflow.
   * Used in the 5/3 DWT predict step: odd += (s_prev + s_next) >> 1.
   *
   * Identity: (a + b) >> 1 = (a >> 1) + (b >> 1) + ((a & b) & 1)
   * This avoids the intermediate sum a + b which can overflow int16.
   */
  static HWY_INLINE auto predict_avg_16_53(const HWY_FULL(int16_t) di,
                                           decltype(Zero(HWY_FULL(int16_t)())) a,
                                           decltype(Zero(HWY_FULL(int16_t)())) b)
      -> decltype(Zero(HWY_FULL(int16_t)()))
  {
    return ShiftRight<1>(a) + ShiftRight<1>(b) + And(And(a, b), Set(di, (int16_t)1));
  }

  static void hwy_v_final_store_16_53(const int16_t* scratch, const uint32_t height, int16_t* dest,
                                      const size_t strideDest, int16_t dcShift, int16_t dcMin,
                                      int16_t dcMax)
  {
    const HWY_FULL(int16_t) di;
    if(dcShift != 0)
    {
      const auto vshift = Set(di, dcShift);
      const auto vmin = Set(di, dcMin);
      const auto vmax = Set(di, dcMax);
      for(uint32_t i = 0; i < height; ++i)
      {
        auto v0 = Clamp(Load(di, scratch + HWY_PLL_COLS_16_53 * i + 0) + vshift, vmin, vmax);
        auto v1 =
            Clamp(Load(di, scratch + HWY_PLL_COLS_16_53 * i + Lanes(di)) + vshift, vmin, vmax);
        StoreU(v0, di, &dest[(size_t)i * strideDest + 0]);
        StoreU(v1, di, dest + (size_t)i * strideDest + Lanes(di));
      }
    }
    else
    {
      for(uint32_t i = 0; i < height; ++i)
      {
        StoreU(Load(di, scratch + HWY_PLL_COLS_16_53 * i + 0), di,
               &dest[(size_t)i * strideDest + 0]);
        StoreU(Load(di, scratch + HWY_PLL_COLS_16_53 * i + Lanes(di)), di,
               dest + (size_t)i * strideDest + Lanes(di));
      }
    }
  }

  /** Vertical inverse 5x3 wavelet transform for int16_t,
   * when top-most pixel is on even coordinate */
  static void hwy_v_p0_16_53(int16_t* scratch, const uint32_t height, int16_t* bandL,
                             const size_t strideL, int16_t* bandH, const size_t strideH,
                             int16_t* dest, const uint32_t strideDest, int16_t dcShift,
                             int16_t dcMin, int16_t dcMax)
  {
    const HWY_FULL(int16_t) di;
    const auto one = Set(di, (int16_t)1);

    assert(height > 1);

    assert((size_t)scratch % (sizeof(int16_t) * Lanes(di)) == 0);

    auto s1n_0 = LoadU(di, bandL + 0);
    auto s1n_1 = LoadU(di, bandL + Lanes(di));
    auto d1n_0 = LoadU(di, bandH);
    auto d1n_1 = LoadU(di, bandH + Lanes(di));

    /* s0n = s1n - ((d1n + 1) >> 1); */
    auto s0n_0 = s1n_0 - ShiftRight<1>(d1n_0 + one);
    auto s0n_1 = s1n_1 - ShiftRight<1>(d1n_1 + one);

    uint32_t i = 0;
    if(height > 3)
    {
      uint32_t j;
      for(i = 0, j = 1; i < (height - 3); i += 2, j++)
      {
        auto d1c_0 = d1n_0;
        auto s0c_0 = s0n_0;
        auto d1c_1 = d1n_1;
        auto s0c_1 = s0n_1;

        s1n_0 = LoadU(di, bandL + j * strideL);
        s1n_1 = LoadU(di, bandL + j * strideL + Lanes(di));
        d1n_0 = LoadU(di, bandH + j * strideH);
        d1n_1 = LoadU(di, bandH + j * strideH + Lanes(di));

        /*s0n = s1n - ((d1c + d1n + 2) >> 2); — use averaging to avoid overflow */
        s0n_0 = s1n_0 - update_avg_16_53(di, d1c_0, d1n_0);
        s0n_1 = s1n_1 - update_avg_16_53(di, d1c_1, d1n_1);

        Store(s0c_0, di, scratch + HWY_PLL_COLS_16_53 * (i + 0));
        Store(s0c_1, di, scratch + HWY_PLL_COLS_16_53 * (i + 0) + Lanes(di));

        /* d1c + ((s0c + s0n) >> 1) — use overflow-safe averaging */
        Store(d1c_0 + predict_avg_16_53(di, s0c_0, s0n_0), di,
              scratch + HWY_PLL_COLS_16_53 * (i + 1) + 0);
        Store(d1c_1 + predict_avg_16_53(di, s0c_1, s0n_1), di,
              scratch + HWY_PLL_COLS_16_53 * (i + 1) + Lanes(di));
      }
    }
    Store(s0n_0, di, scratch + HWY_PLL_COLS_16_53 * (i + 0) + 0);
    Store(s0n_1, di, scratch + HWY_PLL_COLS_16_53 * (i + 0) + Lanes(di));

    if(height & 1)
    {
      s1n_0 = LoadU(di, bandL + (size_t)(height >> 1) * strideL);
      /* s0n_len_minus_1 = s1n - ((d1n + d1n + 2) >> 2); — use averaging */
      auto s0n_len_minus_1 = s1n_0 - update_avg_16_53(di, d1n_0, d1n_0);
      Store(s0n_len_minus_1, di, scratch + HWY_PLL_COLS_16_53 * (height - 1));
      /* d1n + ((s0n + s0n_len_minus_1) >> 1) — use overflow-safe averaging */
      Store(d1n_0 + predict_avg_16_53(di, s0n_0, s0n_len_minus_1), di,
            scratch + HWY_PLL_COLS_16_53 * (height - 2));

      s1n_1 = LoadU(di, bandL + (size_t)(height >> 1) * strideL + Lanes(di));
      /* s0n_len_minus_1 = s1n - ((d1n + d1n + 2) >> 2); — use averaging */
      s0n_len_minus_1 = s1n_1 - update_avg_16_53(di, d1n_1, d1n_1);
      Store(s0n_len_minus_1, di, scratch + HWY_PLL_COLS_16_53 * (height - 1) + Lanes(di));
      /* d1n + ((s0n + s0n_len_minus_1) >> 1) — use overflow-safe averaging */
      Store(d1n_1 + predict_avg_16_53(di, s0n_1, s0n_len_minus_1), di,
            scratch + HWY_PLL_COLS_16_53 * (height - 2) + Lanes(di));
    }
    else
    {
      Store(d1n_0 + s0n_0, di, scratch + HWY_PLL_COLS_16_53 * (height - 1) + 0);
      Store(d1n_1 + s0n_1, di, scratch + HWY_PLL_COLS_16_53 * (height - 1) + Lanes(di));
    }
    hwy_v_final_store_16_53(scratch, height, dest, strideDest, dcShift, dcMin, dcMax);
  }

  /** Vertical inverse 5x3 wavelet transform for int16_t,
   * when top-most pixel is on odd coordinate */
  static void hwy_v_p1_16_53(int16_t* scratch, const uint32_t height, int16_t* bandL,
                             const uint32_t strideL, int16_t* bandH, const uint32_t strideH,
                             int16_t* dest, const uint32_t strideDest, int16_t dcShift,
                             int16_t dcMin, int16_t dcMax)
  {
    const HWY_FULL(int16_t) di;

    assert(height > 2);
    assert((size_t)scratch % (sizeof(int16_t) * Lanes(di)) == 0);

    auto d1_0 = LoadU(di, bandH + strideH);

    /* bandL[0] - ((bandH[0] + d1 + 2) >> 2); — use averaging to avoid overflow */
    auto sc_0 = LoadU(di, bandL + 0) - update_avg_16_53(di, LoadU(di, bandH + 0), d1_0);
    Store(LoadU(di, bandH + 0) + sc_0, di, scratch + HWY_PLL_COLS_16_53 * 0);
    auto d1_1 = LoadU(di, bandH + strideH + Lanes(di));

    /* bandL[0] - ((H[0] + d1 + 2) >> 2); — use averaging */
    auto sc_1 =
        LoadU(di, bandL + Lanes(di)) - update_avg_16_53(di, LoadU(di, bandH + Lanes(di)), d1_1);
    Store(LoadU(di, bandH + Lanes(di)) + sc_1, di, scratch + HWY_PLL_COLS_16_53 * 0 + Lanes(di));

    uint32_t i = 1;
    size_t j = 1;
    for(; i < (height - 2 - !(height & 1)); i += 2, j++)
    {
      auto d2_0 = LoadU(di, bandH + (j + 1) * strideH);
      auto d2_1 = LoadU(di, bandH + (j + 1) * strideH + Lanes(di));

      /* sn = bandH[j * stride] - ((d1 + d2 + 2) >> 2); — use averaging */
      auto sn_0 = LoadU(di, bandL + j * strideL) - update_avg_16_53(di, d1_0, d2_0);
      auto sn_1 = LoadU(di, bandL + j * strideL + Lanes(di)) - update_avg_16_53(di, d1_1, d2_1);

      Store(sc_0, di, scratch + HWY_PLL_COLS_16_53 * i);
      Store(sc_1, di, scratch + HWY_PLL_COLS_16_53 * i + Lanes(di));

      /* buf[i + 1] = d1 + ((sn + sc) >> 1); — use overflow-safe averaging */
      Store(d1_0 + predict_avg_16_53(di, sn_0, sc_0), di,
            scratch + HWY_PLL_COLS_16_53 * (i + 1) + 0);
      Store(d1_1 + predict_avg_16_53(di, sn_1, sc_1), di,
            scratch + HWY_PLL_COLS_16_53 * (i + 1) + Lanes(di));

      sc_0 = sn_0;
      sc_1 = sn_1;
      d1_0 = d2_0;
      d1_1 = d2_1;
    }
    Store(sc_0, di, scratch + HWY_PLL_COLS_16_53 * i);
    Store(sc_1, di, scratch + HWY_PLL_COLS_16_53 * i + Lanes(di));

    if(!(height & 1))
    {
      /*dn = bandH[(len / 2 - 1) * stride] - ((s1 + s1 + 2) >> 2); — use averaging */
      auto sn_0 =
          LoadU(di, bandL + (size_t)(height / 2 - 1) * strideL) - update_avg_16_53(di, d1_0, d1_0);
      auto sn_1 = LoadU(di, bandL + (size_t)(height / 2 - 1) * strideL + Lanes(di)) -
                  update_avg_16_53(di, d1_1, d1_1);

      /* buf[len - 2] = s1 + ((dn + dc) >> 1); — use overflow-safe averaging */
      Store(d1_0 + predict_avg_16_53(di, sn_0, sc_0), di,
            scratch + HWY_PLL_COLS_16_53 * (height - 2) + 0);
      Store(d1_1 + predict_avg_16_53(di, sn_1, sc_1), di,
            scratch + HWY_PLL_COLS_16_53 * (height - 2) + Lanes(di));

      Store(sn_0, di, scratch + HWY_PLL_COLS_16_53 * (height - 1) + 0);
      Store(sn_1, di, scratch + HWY_PLL_COLS_16_53 * (height - 1) + Lanes(di));
    }
    else
    {
      Store(d1_0 + sc_0, di, scratch + HWY_PLL_COLS_16_53 * (height - 1) + 0);
      Store(d1_1 + sc_1, di, scratch + HWY_PLL_COLS_16_53 * (height - 1) + Lanes(di));
    }
    hwy_v_final_store_16_53(scratch, height, dest, strideDest, dcShift, dcMin, dcMax);
  }

} // namespace HWY_NAMESPACE
} // namespace grk
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace grk
{
HWY_EXPORT(hwy_v_p0_53);
HWY_EXPORT(hwy_v_p1_53);
HWY_EXPORT(GetHWY_PLL_COLS_53); // Export GetHWY_PLL_COLS_53
HWY_EXPORT(hwy_v_p0_16_53);
HWY_EXPORT(hwy_v_p1_16_53);
HWY_EXPORT(GetHWY_PLL_COLS_16_53);

uint32_t get_PLL_COLS_53()
{
  static uint32_t value = HWY_DYNAMIC_DISPATCH(GetHWY_PLL_COLS_53)();
  return value;
}

uint32_t get_PLL_COLS_16_53()
{
  static uint32_t value = HWY_DYNAMIC_DISPATCH(GetHWY_PLL_COLS_16_53)();
  return value;
}

WaveletReverse::WaveletReverse(CodecScheduler* scheduler, TileComponent* tilec, uint16_t compno,
                               Rect32 unreducedWindow, uint8_t numres, uint8_t qmfbid,
                               uint32_t maxDim, bool wholeTileDecompress, WaveletPoolData* poolData,
                               DcShiftParam dcShift)
    : poolData_(poolData), scheduler_(scheduler), tilec_(tilec), compno_(compno),
      unreducedWindow_(unreducedWindow), numres_(numres), qmfbid_(qmfbid), maxDim_(maxDim),
      wholeTileDecompress_(wholeTileDecompress), dcShift_(dcShift)
{}
WaveletReverse::~WaveletReverse(void)
{
  for(const auto& t : partialTasks53_)
    delete t;
  for(const auto& t : partialTasks97_)
    delete t;
}

/* Performs lifting in one single iteration. Saves memory */
/* accesses and explicit interleaving. */
void WaveletReverse::load_h_p0_53(int32_t* scratch, const uint32_t width, const int32_t* bandL,
                                  const int32_t* bandH, int32_t* dest)
{
  assert(width > 1);

  int32_t s1n = bandL[0];
  int32_t d1n = bandH[0];
  int32_t s0n = s1n - ((d1n + 1) >> 1);
  uint32_t i = 0;
  if(width > 2)
  {
    for(uint32_t j = 1; i < (width - 3); i += 2, j++)
    {
      int32_t d1c = d1n;
      int32_t s0c = s0n;

      s1n = bandL[j];
      d1n = bandH[j];
      s0n = s1n - ((d1c + d1n + 2) >> 2);
      scratch[i] = s0c;
      scratch[i + 1] = d1c + ((s0c + s0n) >> 1);
    }
  }
  scratch[i] = s0n;
  if(width & 1)
  {
    scratch[width - 1] = bandL[(width - 1) >> 1] - ((d1n + 1) >> 1);
    scratch[width - 2] = d1n + ((s0n + scratch[width - 1]) >> 1);
  }
  else
  {
    scratch[width - 1] = d1n + s0n;
  }
  memcpy(dest, scratch, (size_t)width * sizeof(int32_t));
}

/* Performs lifting in one single iteration. Saves memory
   accesses and explicit interleaving. */
void WaveletReverse::load_h_p1_53(int32_t* scratch, const uint32_t width, const int32_t* bandL,
                                  const int32_t* bandH, int32_t* dest)
{
  assert(width > 2);
  int32_t d1c = bandH[1];
  int32_t s0c = bandL[0] - ((bandH[0] + d1c + 2) >> 2);
  scratch[0] = bandH[0] + s0c; // reflection at boundary
  uint32_t i, j;
  for(i = 1, j = 1; i < (width - 2 - !(width & 1)); i += 2, j++)
  {
    int32_t d1n = bandH[j + 1];
    int32_t s0n = bandL[j] - ((d1c + d1n + 2) >> 2);

    scratch[i] = s0c;
    scratch[i + 1] = d1c + ((s0n + s0c) >> 1);

    s0c = s0n;
    d1c = d1n;
  }
  scratch[i] = s0c;
  if(!(width & 1))
  {
    int32_t sn = bandL[(width >> 1) - 1] - ((d1c + 1) >> 1);

    scratch[width - 2] = d1c + ((sn + s0c) >> 1);
    scratch[width - 1] = sn;
  }
  else
  {
    scratch[width - 1] = d1c + s0c;
  }
  memcpy(dest, scratch, (size_t)width * sizeof(int32_t));
}

/* <summary>                            */
/* Inverse 5-3 wavelet transform in 1-D for one row. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
void WaveletReverse::load_h_53(const dwt_scratch<int32_t>* scratch, int32_t* bandL, int32_t* bandH,
                               int32_t* dest)
{
  const uint32_t width = scratch->sn + scratch->dn;
  assert(width != 0);
  if(scratch->parity == 0)
  {
    if(width > 1)
    {
      load_h_p0_53(scratch->mem, width, bandL, bandH, dest);
    }
    else
    {
      assert(scratch->sn == 1);
      // only L op: only one sample in L band and H band is empty
      dest[0] = bandL[0];
    }
  }
  else
  {
    if(width == 1)
    {
      assert(scratch->dn == 1);
      // only H op: only one sample in H band and L band is empty
      // todo: explain why we use bandOdd i.e. low pass band to calculate this
      dest[0] = bandH[0] >> 1;
    }
    else if(width == 2)
    {
      const int32_t s0 = bandL[0] - ((bandH[0] + 1) >> 1);
      dest[0] = bandH[0] + s0;
      dest[1] = s0;
    }
    else
    {
      load_h_p1_53(scratch->mem, width, bandL, bandH, dest);
    }
  }
}

void WaveletReverse::h_strip_53(const dwt_scratch<int32_t>* scratch, uint32_t hMin, uint32_t hMax,
                                Buffer2dSimple<int32_t> winL, Buffer2dSimple<int32_t> winH,
                                Buffer2dSimple<int32_t> winDest)
{
  for(uint32_t j = hMin; j < hMax; ++j)
  {
    load_h_53(scratch, winL.buf_, winH.buf_, winDest.buf_);
    winL.incY_IN_PLACE(1);
    winH.incY_IN_PLACE(1);
    winDest.incY_IN_PLACE(1);
  }
}
void WaveletReverse::h_53(uint8_t res, TileComponentWindow<int32_t>* tileBuffer, uint32_t resHeight)
{
  uint32_t num_threads = (uint32_t)TFSingleton::num_threads();
  Buffer2dSimple<int32_t> winL, winH, winDest;
  auto imageComponentFlow = ((DecompressScheduler*)scheduler_)->getImageComponentFlow(compno_);
  if(!imageComponentFlow)
    return;
  auto resFlow = imageComponentFlow->getResflow(res - 1);
  if(!resFlow)
    return;
  uint32_t numTasks[2] = {0, 0};
  uint32_t height[2] = {0, 0};

  // top "half" of buffer becomes vertical L orientation, and bottom "half" of buffer
  // becomes vertical H orientation
  for(uint32_t orient = 0; orient < 2; ++orient)
  {
    height[orient] = (orient == 0) ? vert_.sn : resHeight - vert_.sn;
    numTasks[orient] = height[orient] < num_threads ? height[orient] : num_threads;
    height[orient] = (orient == 0) ? vertPool_[0].sn : resHeight - vertPool_[0].sn;
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
      uint32_t sn = horiz_.sn;
      uint32_t dn = horiz_.dn;
      uint32_t parity = horiz_.parity;
      resFlow->waveletHoriz_->nextTask().work(
          [this, sn, dn, parity, winL, winH, winDest, hMin, hMax] {
            horizPool_[TFSingleton::workerId()].sn = sn;
            horizPool_[TFSingleton::workerId()].dn = dn;
            horizPool_[TFSingleton::workerId()].parity = parity;
            h_strip_53(&horizPool_[TFSingleton::workerId()], hMin, hMax, winL, winH, winDest);
          });
      winL.incY_IN_PLACE(heightIncr);
      winH.incY_IN_PLACE(heightIncr);
      winDest.incY_IN_PLACE(heightIncr);
    }
  }
}

/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on even coordinate */
void WaveletReverse::v_p0_53(int32_t* scratch, const uint32_t height, int32_t* bandL,
                             const uint32_t strideL, int32_t* bandH, const uint32_t strideH,
                             int32_t* dest, const uint32_t strideDest)
{
  assert(height > 1);

  /* Performs lifting in one single iteration. Saves memory */
  /* accesses and explicit interleaving. */
  int32_t s1n = bandL[0];
  int32_t d1n = bandH[0];
  int32_t s0n = s1n - ((d1n + 1) >> 1);

  uint32_t i = 0;
  if(height > 2)
  {
    auto bL = bandL + strideL;
    auto bH = bandH + strideH;
    for(uint32_t j = 0; i < (height - 3); i += 2, j++)
    {
      int32_t d1c = d1n;
      int32_t s0c = s0n;
      s1n = *bL;
      bL += strideL;
      d1n = *bH;
      bH += strideH;
      s0n = s1n - ((d1c + d1n + 2) >> 2);
      scratch[i] = s0c;
      scratch[i + 1] = d1c + ((s0c + s0n) >> 1);
    }
  }
  scratch[i] = s0n;
  if(height & 1)
  {
    scratch[height - 1] = bandL[((height - 1) >> 1) * strideL] - ((d1n + 1) >> 1);
    scratch[height - 2] = d1n + ((s0n + scratch[height - 1]) >> 1);
  }
  else
  {
    scratch[height - 1] = d1n + s0n;
  }
  for(i = 0; i < height; ++i)
  {
    *dest = scratch[i];
    dest += strideDest;
  }
}
/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on odd coordinate */
void WaveletReverse::v_p1_53(int32_t* scratch, const uint32_t height, int32_t* bandL,
                             const uint32_t strideL, int32_t* bandH, const uint32_t strideH,
                             int32_t* dest, const uint32_t strideDest)
{
  assert(height > 2);

  /* Performs lifting in one single iteration. Saves memory */
  /* accesses and explicit interleaving. */
  int32_t s1 = bandH[strideH];
  int32_t dc = bandL[0] - ((bandH[0] + s1 + 2) >> 2);
  scratch[0] = bandH[0] + dc;
  auto s2_ptr = bandH + (strideH << 1);
  auto dn_ptr = bandL + strideL;
  uint32_t i, j;
  for(i = 1, j = 1; i < (height - 2 - !(height & 1)); i += 2, j++)
  {
    int32_t s2 = *s2_ptr;
    s2_ptr += strideH;

    int32_t dn = *dn_ptr - ((s1 + s2 + 2) >> 2);
    dn_ptr += strideL;

    scratch[i] = dc;
    scratch[i + 1] = s1 + ((dn + dc) >> 1);
    dc = dn;
    s1 = s2;
  }
  scratch[i] = dc;
  if(!(height & 1))
  {
    int32_t dn = bandL[((height >> 1) - 1) * strideL] - ((s1 + 1) >> 1);
    scratch[height - 2] = s1 + ((dn + dc) >> 1);
    scratch[height - 1] = dn;
  }
  else
  {
    scratch[height - 1] = s1 + dc;
  }
  for(i = 0; i < height; ++i)
  {
    *dest = scratch[i];
    dest += strideDest;
  }
}

/* <summary>                            */
/* Inverse vertical 5-3 wavelet transform in 1-D for several columns. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
/** Number of columns that we can process in parallel in the vertical pass */
void WaveletReverse::v_53(const dwt_scratch<int32_t>* scratch, Buffer2dSimple<int32_t> winL,
                          Buffer2dSimple<int32_t> winH, Buffer2dSimple<int32_t> winDest,
                          uint32_t nb_cols, DcShiftParam dcShift)
{
  const uint32_t height = scratch->sn + scratch->dn;
  assert(height != 0);
  int32_t dc = dcShift.enabled ? dcShift.shift : 0;
  int32_t dcMin = dcShift.min;
  int32_t dcMax = dcShift.max;
  if(scratch->parity == 0)
  {
    if(height == 1)
    {
      if(dcShift.enabled)
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winDest.buf_++)
          winDest.buf_[0] = std::clamp(winL.buf_[0] + dc, dcMin, dcMax);
      }
      else
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winDest.buf_++)
          winDest.buf_[0] = winL.buf_[0];
      }
    }
    else
    {
      if(nb_cols == get_PLL_COLS_53())
      {
        /* Same as below general case, except that thanks to SSE2/AVX2 */
        /* we can efficiently process 8/16 columns in parallel */
        HWY_DYNAMIC_DISPATCH(hwy_v_p0_53)
        (scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winH.stride_, winDest.buf_,
         winDest.stride_, dc, dcMin, dcMax);
      }
      else
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
          v_p0_53(scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winH.stride_,
                  winDest.buf_, winDest.stride_);
        /* Apply DC shift to scalar output (data just written, still in cache) */
        if(dcShift.enabled)
        {
          auto d = winDest.buf_ - nb_cols;
          for(uint32_t c = 0; c < nb_cols; c++, d++)
            for(uint32_t r = 0; r < height; r++)
              d[r * winDest.stride_] = std::clamp(d[r * winDest.stride_] + dc, dcMin, dcMax);
        }
      }
    }
  }
  else
  {
    if(height == 1)
    {
      if(dcShift.enabled)
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winDest.buf_++)
          winDest.buf_[0] = std::clamp((winL.buf_[0] >> 1) + dc, dcMin, dcMax);
      }
      else
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winDest.buf_++)
          winDest.buf_[0] = winL.buf_[0] >> 1;
      }
    }
    else if(height == 2)
    {
      if(dcShift.enabled)
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
        {
          scratch->mem[1] = winL.buf_[0] - ((winH.buf_[0] + 1) >> 1);
          winDest.buf_[0] = std::clamp(winH.buf_[0] + scratch->mem[1] + dc, dcMin, dcMax);
          winDest.buf_[1] = std::clamp(scratch->mem[1] + dc, dcMin, dcMax);
        }
      }
      else
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
        {
          scratch->mem[1] = winL.buf_[0] - ((winH.buf_[0] + 1) >> 1);
          winDest.buf_[0] = winH.buf_[0] + scratch->mem[1];
          winDest.buf_[1] = scratch->mem[1];
        }
      }
    }
    else
    {
      if(nb_cols == get_PLL_COLS_53())
      {
        /* Same as below general case, except that thanks to SSE2/AVX2 */
        /* we can efficiently process 8/16 columns in parallel */
        HWY_DYNAMIC_DISPATCH(hwy_v_p1_53)
        (scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winH.stride_, winDest.buf_,
         winDest.stride_, dc, dcMin, dcMax);
      }
      else
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
          v_p1_53(scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winH.stride_,
                  winDest.buf_, winDest.stride_);
        /* Apply DC shift to scalar output (data just written, still in cache) */
        if(dcShift.enabled)
        {
          auto d = winDest.buf_ - nb_cols;
          for(uint32_t c = 0; c < nb_cols; c++, d++)
            for(uint32_t r = 0; r < height; r++)
              d[r * winDest.stride_] = std::clamp(d[r * winDest.stride_] + dc, dcMin, dcMax);
        }
      }
    }
  }
}

void WaveletReverse::v_strip_53(const dwt_scratch<int32_t>* scratch, uint32_t wMin, uint32_t wMax,
                                Buffer2dSimple<int32_t> winL, Buffer2dSimple<int32_t> winH,
                                Buffer2dSimple<int32_t> winDest, DcShiftParam dcShift)
{
  uint32_t j;
  for(j = wMin; j + get_PLL_COLS_53() <= wMax; j += get_PLL_COLS_53())
  {
    v_53(scratch, winL, winH, winDest, get_PLL_COLS_53(), dcShift);
    winL.incX_IN_PLACE(get_PLL_COLS_53());
    winH.incX_IN_PLACE(get_PLL_COLS_53());
    winDest.incX_IN_PLACE(get_PLL_COLS_53());
  }
  if(j < wMax)
    v_53(scratch, winL, winH, winDest, wMax - j, dcShift);
}

void WaveletReverse::v_53(uint8_t res, TileComponentWindow<int32_t>* buf, uint32_t resWidth)
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
    uint32_t sn = vert_.sn;
    uint32_t dn = vert_.dn;
    uint32_t parity = vert_.parity;
    resFlow->waveletVert_->nextTask().work(
        [this, sn, dn, parity, wMin, wMax, winL, winH, winDest, dcShift] {
          vertPool_[TFSingleton::workerId()].dn = dn;
          vertPool_[TFSingleton::workerId()].sn = sn;
          vertPool_[TFSingleton::workerId()].parity = parity;

          v_strip_53(&vertPool_[TFSingleton::workerId()], wMin, wMax, winL, winH, winDest, dcShift);
        });
    winL.incX_IN_PLACE(widthIncr);
    winH.incX_IN_PLACE(widthIncr);
    winDest.incX_IN_PLACE(widthIncr);
  }
}

/* <summary>                            */
/* Inverse wavelet transform in 2-D.    */
/* </summary>                           */
bool WaveletReverse::tile_53(void)
{
  if(numres_ == 1U)
    return true;

  if(!poolData_ || !poolData_->isAllocated())
    return false;

  // for resolution n, tileCompRes points to LL subband at res n-1
  auto bandLL = tilec_->resolutions_;
  auto tileBuffer = tilec_->getWindow();

  uint32_t num_threads = (uint32_t)TFSingleton::num_threads();
  horizPool_ = std::make_unique<dwt_scratch<int32_t>[]>(num_threads);
  vertPool_ = std::make_unique<dwt_scratch<int32_t>[]>(num_threads);
  for(uint8_t res = 1; res < numres_; ++res)
  {
    horiz_.sn = bandLL->width();
    vert_.sn = bandLL->height();
    for(uint32_t i = 0; i < num_threads; ++i)
    {
      horizPool_[i].sn = bandLL->width();
      vertPool_[i].sn = bandLL->height();
    }
    ++bandLL;
    auto resWidth = bandLL->width();
    auto resHeight = bandLL->height();
    if(resWidth == 0 || resHeight == 0)
      continue;
    horiz_.dn = resWidth - horiz_.sn;
    horiz_.parity = bandLL->x0 & 1;
    vert_.dn = resHeight - vert_.sn;
    vert_.parity = bandLL->y0 & 1;
    for(uint32_t i = 0; i < num_threads; ++i)
    {
      horizPool_[i].dn = resWidth - horizPool_[i].sn;
      horizPool_[i].parity = bandLL->x0 & 1;
      horizPool_[i].allocatedMem = (int32_t*)poolData_->getHoriz(i);
      horizPool_[i].mem = (int32_t*)poolData_->getHoriz(i);

      vertPool_[i].dn = resHeight - vertPool_[i].sn;
      vertPool_[i].parity = bandLL->y0 & 1;
      vertPool_[i].allocatedMem = (int32_t*)poolData_->getVert(i);
      vertPool_[i].mem = (int32_t*)poolData_->getVert(i);
    }
    h_53(res, tileBuffer, resHeight);
    v_53(res, tileBuffer, resWidth);
  }

  return true;
}

// 16-bit 5/3 DWT kernels ///////////////////////////////////////////////////////////////

void WaveletReverse::load_h_p0_16_53(int16_t* scratch, const uint32_t width, const int16_t* bandL,
                                     const int16_t* bandH, int16_t* dest)
{
  assert(width > 1);

  int16_t s1n = bandL[0];
  int16_t d1n = bandH[0];
  int16_t s0n = (int16_t)(s1n - ((d1n + 1) >> 1));
  uint32_t i = 0;
  if(width > 2)
  {
    for(uint32_t j = 1; i < (width - 3); i += 2, j++)
    {
      int16_t d1c = d1n;
      int16_t s0c = s0n;

      s1n = bandL[j];
      d1n = bandH[j];
      s0n = (int16_t)(s1n - ((d1c + d1n + 2) >> 2));
      scratch[i] = s0c;
      scratch[i + 1] = (int16_t)(d1c + ((s0c + s0n) >> 1));
    }
  }
  scratch[i] = s0n;
  if(width & 1)
  {
    scratch[width - 1] = (int16_t)(bandL[(width - 1) >> 1] - ((d1n + 1) >> 1));
    scratch[width - 2] = (int16_t)(d1n + ((s0n + scratch[width - 1]) >> 1));
  }
  else
  {
    scratch[width - 1] = d1n + s0n;
  }
  memcpy(dest, scratch, (size_t)width * sizeof(int16_t));
}

void WaveletReverse::load_h_p1_16_53(int16_t* scratch, const uint32_t width, const int16_t* bandL,
                                     const int16_t* bandH, int16_t* dest)
{
  assert(width > 2);
  int16_t d1c = bandH[1];
  int16_t s0c = (int16_t)(bandL[0] - ((bandH[0] + d1c + 2) >> 2));
  scratch[0] = (int16_t)(bandH[0] + s0c);
  uint32_t i, j;
  for(i = 1, j = 1; i < (width - 2 - !(width & 1)); i += 2, j++)
  {
    int16_t d1n = bandH[j + 1];
    int16_t s0n = (int16_t)(bandL[j] - ((d1c + d1n + 2) >> 2));

    scratch[i] = s0c;
    scratch[i + 1] = (int16_t)(d1c + ((s0n + s0c) >> 1));

    s0c = s0n;
    d1c = d1n;
  }
  scratch[i] = s0c;
  if(!(width & 1))
  {
    int16_t sn = (int16_t)(bandL[(width >> 1) - 1] - ((d1c + 1) >> 1));

    scratch[width - 2] = (int16_t)(d1c + ((sn + s0c) >> 1));
    scratch[width - 1] = sn;
  }
  else
  {
    scratch[width - 1] = d1c + s0c;
  }
  memcpy(dest, scratch, (size_t)width * sizeof(int16_t));
}

void WaveletReverse::load_h_16_53(const dwt_scratch<int16_t>* scratch, int16_t* bandL,
                                  int16_t* bandH, int16_t* dest)
{
  const uint32_t width = scratch->sn + scratch->dn;
  assert(width != 0);
  if(scratch->parity == 0)
  {
    if(width > 1)
    {
      load_h_p0_16_53(scratch->mem, width, bandL, bandH, dest);
    }
    else
    {
      assert(scratch->sn == 1);
      dest[0] = bandL[0];
    }
  }
  else
  {
    if(width == 1)
    {
      assert(scratch->dn == 1);
      dest[0] = bandH[0] >> 1;
    }
    else if(width == 2)
    {
      const int16_t s0 = (int16_t)(bandL[0] - ((bandH[0] + 1) >> 1));
      dest[0] = bandH[0] + s0;
      dest[1] = s0;
    }
    else
    {
      load_h_p1_16_53(scratch->mem, width, bandL, bandH, dest);
    }
  }
}

void WaveletReverse::h_strip_16_53(const dwt_scratch<int16_t>* scratch, uint32_t hMin,
                                   uint32_t hMax, Buffer2dSimple<int16_t> winL,
                                   Buffer2dSimple<int16_t> winH, Buffer2dSimple<int16_t> winDest)
{
  for(uint32_t j = hMin; j < hMax; ++j)
  {
    load_h_16_53(scratch, winL.buf_, winH.buf_, winDest.buf_);
    winL.incY_IN_PLACE(1);
    winH.incY_IN_PLACE(1);
    winDest.incY_IN_PLACE(1);
  }
}

void WaveletReverse::h_16_53(uint8_t res, TileComponentWindow<int16_t>* tileBuffer,
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

  // top "half" of buffer becomes vertical L orientation, and bottom "half" of buffer
  // becomes vertical H orientation
  for(uint32_t orient = 0; orient < 2; ++orient)
  {
    height[orient] = (orient == 0) ? vert16_.sn : resHeight - vert16_.sn;
    numTasks[orient] = height[orient] < num_threads ? height[orient] : num_threads;
    height[orient] = (orient == 0) ? vertPool16_[0].sn : resHeight - vertPool16_[0].sn;
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
      uint32_t parity = horiz16_.parity;
      resFlow->waveletHoriz_->nextTask().work(
          [this, sn, dn, parity, winL, winH, winDest, hMin, hMax] {
            horizPool16_[TFSingleton::workerId()].sn = sn;
            horizPool16_[TFSingleton::workerId()].dn = dn;
            horizPool16_[TFSingleton::workerId()].parity = parity;
            h_strip_16_53(&horizPool16_[TFSingleton::workerId()], hMin, hMax, winL, winH, winDest);
          });
      winL.incY_IN_PLACE(heightIncr);
      winH.incY_IN_PLACE(heightIncr);
      winDest.incY_IN_PLACE(heightIncr);
    }
  }
}

void WaveletReverse::v_p0_16_53(int16_t* scratch, const uint32_t height, int16_t* bandL,
                                const uint32_t strideL, int16_t* bandH, const uint32_t strideH,
                                int16_t* dest, const uint32_t strideDest)
{
  assert(height > 1);

  int16_t s1n = bandL[0];
  int16_t d1n = bandH[0];
  int16_t s0n = (int16_t)(s1n - ((d1n + 1) >> 1));

  uint32_t i = 0;
  if(height > 2)
  {
    auto bL = bandL + strideL;
    auto bH = bandH + strideH;
    for(uint32_t j = 0; i < (height - 3); i += 2, j++)
    {
      int16_t d1c = d1n;
      int16_t s0c = s0n;
      s1n = *bL;
      bL += strideL;
      d1n = *bH;
      bH += strideH;
      s0n = (int16_t)(s1n - ((d1c + d1n + 2) >> 2));
      scratch[i] = s0c;
      scratch[i + 1] = (int16_t)(d1c + ((s0c + s0n) >> 1));
    }
  }
  scratch[i] = s0n;
  if(height & 1)
  {
    scratch[height - 1] = (int16_t)(bandL[((height - 1) >> 1) * strideL] - ((d1n + 1) >> 1));
    scratch[height - 2] = (int16_t)(d1n + ((s0n + scratch[height - 1]) >> 1));
  }
  else
  {
    scratch[height - 1] = d1n + s0n;
  }
  for(i = 0; i < height; ++i)
  {
    *dest = scratch[i];
    dest += strideDest;
  }
}

void WaveletReverse::v_p1_16_53(int16_t* scratch, const uint32_t height, int16_t* bandL,
                                const uint32_t strideL, int16_t* bandH, const uint32_t strideH,
                                int16_t* dest, const uint32_t strideDest)
{
  assert(height > 2);

  int16_t s1 = bandH[strideH];
  int16_t dc = (int16_t)(bandL[0] - ((bandH[0] + s1 + 2) >> 2));
  scratch[0] = (int16_t)(bandH[0] + dc);
  auto s2_ptr = bandH + (strideH << 1);
  auto dn_ptr = bandL + strideL;
  uint32_t i, j;
  for(i = 1, j = 1; i < (height - 2 - !(height & 1)); i += 2, j++)
  {
    int16_t s2 = *s2_ptr;
    s2_ptr += strideH;

    int16_t dn = (int16_t)(*dn_ptr - ((s1 + s2 + 2) >> 2));
    dn_ptr += strideL;

    scratch[i] = dc;
    scratch[i + 1] = (int16_t)(s1 + ((dn + dc) >> 1));
    dc = dn;
    s1 = s2;
  }
  scratch[i] = dc;
  if(!(height & 1))
  {
    int16_t dn = (int16_t)(bandL[((height >> 1) - 1) * strideL] - ((s1 + 1) >> 1));
    scratch[height - 2] = (int16_t)(s1 + ((dn + dc) >> 1));
    scratch[height - 1] = dn;
  }
  else
  {
    scratch[height - 1] = s1 + dc;
  }
  for(i = 0; i < height; ++i)
  {
    *dest = scratch[i];
    dest += strideDest;
  }
}

void WaveletReverse::v_16_53(const dwt_scratch<int16_t>* scratch, Buffer2dSimple<int16_t> winL,
                             Buffer2dSimple<int16_t> winH, Buffer2dSimple<int16_t> winDest,
                             uint32_t nb_cols, DcShiftParam dcShift)
{
  const uint32_t height = scratch->sn + scratch->dn;
  assert(height != 0);
  int16_t dc = dcShift.enabled ? (int16_t)dcShift.shift : 0;
  int16_t dcMin = (int16_t)dcShift.min;
  int16_t dcMax = (int16_t)dcShift.max;
  if(scratch->parity == 0)
  {
    if(height == 1)
    {
      if(dcShift.enabled)
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winDest.buf_++)
          winDest.buf_[0] = std::clamp<int16_t>(winL.buf_[0] + dc, dcMin, dcMax);
      }
      else
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winDest.buf_++)
          winDest.buf_[0] = winL.buf_[0];
      }
    }
    else
    {
      if(nb_cols == get_PLL_COLS_16_53())
      {
        HWY_DYNAMIC_DISPATCH(hwy_v_p0_16_53)
        (scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winH.stride_, winDest.buf_,
         winDest.stride_, dc, dcMin, dcMax);
      }
      else
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
          v_p0_16_53(scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winH.stride_,
                     winDest.buf_, winDest.stride_);
        if(dcShift.enabled)
        {
          auto d = winDest.buf_ - nb_cols;
          for(uint32_t c = 0; c < nb_cols; c++, d++)
            for(uint32_t r = 0; r < height; r++)
              d[r * winDest.stride_] =
                  std::clamp<int16_t>(d[r * winDest.stride_] + dc, dcMin, dcMax);
        }
      }
    }
  }
  else
  {
    if(height == 1)
    {
      if(dcShift.enabled)
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winDest.buf_++)
          winDest.buf_[0] = std::clamp<int16_t>((winL.buf_[0] >> 1) + dc, dcMin, dcMax);
      }
      else
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winDest.buf_++)
          winDest.buf_[0] = winL.buf_[0] >> 1;
      }
    }
    else if(height == 2)
    {
      if(dcShift.enabled)
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
        {
          scratch->mem[1] = (int16_t)(winL.buf_[0] - ((winH.buf_[0] + 1) >> 1));
          winDest.buf_[0] = std::clamp<int16_t>(winH.buf_[0] + scratch->mem[1] + dc, dcMin, dcMax);
          winDest.buf_[1] = std::clamp<int16_t>(scratch->mem[1] + dc, dcMin, dcMax);
        }
      }
      else
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
        {
          scratch->mem[1] = (int16_t)(winL.buf_[0] - ((winH.buf_[0] + 1) >> 1));
          winDest.buf_[0] = winH.buf_[0] + scratch->mem[1];
          winDest.buf_[1] = scratch->mem[1];
        }
      }
    }
    else
    {
      if(nb_cols == get_PLL_COLS_16_53())
      {
        HWY_DYNAMIC_DISPATCH(hwy_v_p1_16_53)
        (scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winH.stride_, winDest.buf_,
         winDest.stride_, dc, dcMin, dcMax);
      }
      else
      {
        for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
          v_p1_16_53(scratch->mem, height, winL.buf_, winL.stride_, winH.buf_, winH.stride_,
                     winDest.buf_, winDest.stride_);
        if(dcShift.enabled)
        {
          auto d = winDest.buf_ - nb_cols;
          for(uint32_t c = 0; c < nb_cols; c++, d++)
            for(uint32_t r = 0; r < height; r++)
              d[r * winDest.stride_] =
                  std::clamp<int16_t>(d[r * winDest.stride_] + dc, dcMin, dcMax);
        }
      }
    }
  }
}

void WaveletReverse::v_strip_16_53(const dwt_scratch<int16_t>* scratch, uint32_t wMin,
                                   uint32_t wMax, Buffer2dSimple<int16_t> winL,
                                   Buffer2dSimple<int16_t> winH, Buffer2dSimple<int16_t> winDest,
                                   DcShiftParam dcShift)
{
  uint32_t j;
  for(j = wMin; j + get_PLL_COLS_16_53() <= wMax; j += get_PLL_COLS_16_53())
  {
    v_16_53(scratch, winL, winH, winDest, get_PLL_COLS_16_53(), dcShift);
    winL.incX_IN_PLACE(get_PLL_COLS_16_53());
    winH.incX_IN_PLACE(get_PLL_COLS_16_53());
    winDest.incX_IN_PLACE(get_PLL_COLS_16_53());
  }
  if(j < wMax)
    v_16_53(scratch, winL, winH, winDest, wMax - j, dcShift);
}

void WaveletReverse::v_16_53(uint8_t res, TileComponentWindow<int16_t>* buf, uint32_t resWidth)
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
    uint32_t parity = vert16_.parity;
    resFlow->waveletVert_->nextTask().work(
        [this, sn, dn, parity, wMin, wMax, winL, winH, winDest, dcShift] {
          vertPool16_[TFSingleton::workerId()].dn = dn;
          vertPool16_[TFSingleton::workerId()].sn = sn;
          vertPool16_[TFSingleton::workerId()].parity = parity;

          v_strip_16_53(&vertPool16_[TFSingleton::workerId()], wMin, wMax, winL, winH, winDest,
                        dcShift);
        });
    winL.incX_IN_PLACE(widthIncr);
    winH.incX_IN_PLACE(widthIncr);
    winDest.incX_IN_PLACE(widthIncr);
  }
}

bool WaveletReverse::tile_16_53(void)
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
      horizPool16_[i].allocatedMem = (int16_t*)poolData_->getHoriz(i);
      horizPool16_[i].mem = (int16_t*)poolData_->getHoriz(i);

      vertPool16_[i].dn = resHeight - vertPool16_[i].sn;
      vertPool16_[i].parity = bandLL->y0 & 1;
      vertPool16_[i].allocatedMem = (int16_t*)poolData_->getVert(i);
      vertPool16_[i].mem = (int16_t*)poolData_->getVert(i);
    }
    h_16_53(res, tileBuffer, resHeight);
    v_16_53(res, tileBuffer, resWidth);
  }

  return true;
}

bool WaveletReverse::decompress(void)
{
  if(poolData_)
    poolData_->alloc(maxDim_);

  if(!wholeTileDecompress_)
    return decompressPartial();

  if(qmfbid_ == 1)
  {
    if(tilec_->is16BitDwt())
      return tile_16_53();
    return tile_53();
  }
  else
    return tile_97();
}

} // namespace grk
#endif
