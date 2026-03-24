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

/*************************************
 *
 * Full 9/7 Inverse Wavelet
 *
 ***********************************/

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
#define HWY_TARGET_INCLUDE "wavelet/WaveletReverse97.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace grk
{
namespace HWY_NAMESPACE
{
  static size_t num_lanes(void)
  {
    const HWY_FULL(int32_t) di;
    return Lanes(di);
  }

  static uint32_t GetHWY_PLL_ROWS_97(void)
  {
    const HWY_FULL(float) df;
    return 2 * (uint32_t)Lanes(df);
  }

  /* 9/7 lifting constants (inside HWY_NAMESPACE for multi-target compilation) */
  static const float hwy_K = 1.230174105f;
  static const float hwy_twice_invK = 1.625732422f;
  static const float hwy_dwt_alpha = 1.586134342f;
  static const float hwy_dwt_beta = 0.052980118f;
  static const float hwy_dwt_gamma = -0.882911075f;
  static const float hwy_dwt_delta = -0.443506852f;

  /* step1: multiply each element in a band by constant c.
   * data points to first element; stride between same-band elements = 2*L floats
   * (interleaved L/H layout, each element is L floats wide). */
  static void hwy_step1_97(float* data, uint32_t len, float c)
  {
    const HWY_FULL(float) df;
    const size_t L = Lanes(df);
    const size_t stride = 2 * L;
    auto cv = Set(df, c);

    /* process 4 elements at a time */
    uint32_t i;
    for(i = 0; i + 3 < len; i += 4, data += stride * 4)
    {
      Store(Mul(Load(df, data), cv), df, data);
      Store(Mul(Load(df, data + stride), cv), df, data + stride);
      Store(Mul(Load(df, data + stride * 2), cv), df, data + stride * 2);
      Store(Mul(Load(df, data + stride * 3), cv), df, data + stride * 3);
    }
    for(; i < len; ++i, data += stride)
      Store(Mul(Load(df, data), cv), df, data);
  }

  /* step2: lifting step.
   * data points to first source element; targets are at data[-L].
   * Update: target += (prev_source + source) * c */
  static void hwy_step2_97(float* data, float* dataPrev, uint32_t len, uint32_t lenMax, float c)
  {
    const HWY_FULL(float) df;
    const size_t L = Lanes(df);
    auto cv = Set(df, c);

    uint32_t imax = (std::min<uint32_t>)(len, lenMax);

    /* initial tmp1 value is only necessary when
     * absolute start of line is at 0 */
    auto tmp1 = Load(df, dataPrev);
    uint32_t i = 0;
    for(; i + 3 < imax; i += 4)
    {
      auto tgt0 = Load(df, data - L);
      auto src0 = Load(df, data);
      auto tgt1 = Load(df, data + L);
      auto src1 = Load(df, data + 2 * L);
      auto tgt2 = Load(df, data + 3 * L);
      auto src2 = Load(df, data + 4 * L);
      auto tgt3 = Load(df, data + 5 * L);
      auto src3 = Load(df, data + 6 * L);

      Store(Add(tgt0, Mul(Add(tmp1, src0), cv)), df, data - L);
      Store(Add(tgt1, Mul(Add(src0, src1), cv)), df, data + L);
      Store(Add(tgt2, Mul(Add(src1, src2), cv)), df, data + 3 * L);
      Store(Add(tgt3, Mul(Add(src2, src3), cv)), df, data + 5 * L);

      tmp1 = src3;
      data += 8 * L;
    }

    for(; i < imax; ++i)
    {
      auto tgt = Load(df, data - L);
      auto src = Load(df, data);
      Store(Add(tgt, Mul(Add(tmp1, src), cv)), df, data - L);
      tmp1 = src;
      data += 2 * L;
    }

    if(lenMax < len)
    {
      assert(lenMax + 1 == len);
      auto cv2 = Add(cv, cv);
      auto prev = Load(df, data - 2 * L);
      auto tgt = Load(df, data - L);
      Store(Add(tgt, Mul(prev, cv2)), df, data - L);
    }
  }

  /* step2 for 2*L-wide elements (two SIMD vectors per element).
   * Same lifting step as hwy_step2_97 but each element is 2*Lanes wide,
   * matching the 5/3 pattern of processing 2*Lanes columns at once. */
  static void hwy_step2_97_2x(float* data, float* dataPrev, uint32_t len, uint32_t lenMax, float c)
  {
    const HWY_FULL(float) df;
    const size_t L = Lanes(df);
    const size_t PLL = 2 * L;
    auto cv = Set(df, c);

    uint32_t imax = (std::min<uint32_t>)(len, lenMax);

    auto tmp1_a = Load(df, dataPrev);
    auto tmp1_b = Load(df, dataPrev + L);
    uint32_t i = 0;
    for(; i + 1 < imax; i += 2)
    {
      auto tgt0_a = Load(df, data - PLL);
      auto tgt0_b = Load(df, data - PLL + L);
      auto src0_a = Load(df, data);
      auto src0_b = Load(df, data + L);
      auto tgt1_a = Load(df, data + PLL);
      auto tgt1_b = Load(df, data + PLL + L);
      auto src1_a = Load(df, data + 2 * PLL);
      auto src1_b = Load(df, data + 2 * PLL + L);

      Store(Add(tgt0_a, Mul(Add(tmp1_a, src0_a), cv)), df, data - PLL);
      Store(Add(tgt0_b, Mul(Add(tmp1_b, src0_b), cv)), df, data - PLL + L);
      Store(Add(tgt1_a, Mul(Add(src0_a, src1_a), cv)), df, data + PLL);
      Store(Add(tgt1_b, Mul(Add(src0_b, src1_b), cv)), df, data + PLL + L);

      tmp1_a = src1_a;
      tmp1_b = src1_b;
      data += 4 * PLL;
    }

    for(; i < imax; ++i)
    {
      auto tgt_a = Load(df, data - PLL);
      auto tgt_b = Load(df, data - PLL + L);
      auto src_a = Load(df, data);
      auto src_b = Load(df, data + L);

      Store(Add(tgt_a, Mul(Add(tmp1_a, src_a), cv)), df, data - PLL);
      Store(Add(tgt_b, Mul(Add(tmp1_b, src_b), cv)), df, data - PLL + L);

      tmp1_a = src_a;
      tmp1_b = src_b;
      data += 2 * PLL;
    }

    if(lenMax < len)
    {
      assert(lenMax + 1 == len);
      auto cv2 = Add(cv, cv);
      auto prev_a = Load(df, data - 2 * PLL);
      auto prev_b = Load(df, data - 2 * PLL + L);
      auto tgt_a = Load(df, data - PLL);
      auto tgt_b = Load(df, data - PLL + L);
      Store(Add(tgt_a, Mul(prev_a, cv2)), df, data - PLL);
      Store(Add(tgt_b, Mul(prev_b, cv2)), df, data - PLL + L);
    }
  }

  /* Full 9/7 inverse lifting step on interleaved scratch buffer.
   * Each element is Lanes(float) wide. */
  static void hwy_step_97_full(float* mem, uint32_t sn, uint32_t dn, uint32_t parity, Line32 win_l,
                               Line32 win_h)
  {
    const HWY_FULL(float) df;
    const size_t L = Lanes(df);

    if((!parity && dn == 0 && sn <= 1) || (parity && sn == 0 && dn >= 1))
      return;

    auto make_step1 = [&](bool isBandL) -> std::pair<float*, uint32_t> {
      int64_t parityOff = isBandL ? (int64_t)parity : (int64_t)!parity;
      int64_t band_0 = isBandL ? win_l.x0 : win_h.x0;
      int64_t band_1 = isBandL ? win_l.x1 : win_h.x1;
      float* d = mem + (parityOff + band_0 - win_l.x0) * L;
      return {d, (uint32_t)(band_1 - band_0)};
    };

    auto make_step2 = [&](bool isBandL) -> std::tuple<float*, float*, uint32_t, uint32_t> {
      int64_t parityOff = isBandL ? (int64_t)parity : (int64_t)!parity;
      int64_t band_0 = isBandL ? win_l.x0 : win_h.x0;
      int64_t band_1 = isBandL ? win_l.x1 : win_h.x1;
      int64_t lmax = isBandL ? (std::min<int64_t>)(sn, (int64_t)dn - parityOff)
                             : (std::min<int64_t>)(dn, (int64_t)sn - parityOff);
      if(lmax < 0)
        lmax = 0;
      assert(lmax >= band_0);
      lmax -= band_0;
      float* d = mem + (parityOff + band_0 - win_l.x0) * L;
      d += L;
      float* dPrev = parityOff ? d - 2 * L : d;
      return {d, dPrev, (uint32_t)(band_1 - band_0), (uint32_t)lmax};
    };

    {
      auto [d, len] = make_step1(true);
      hwy_step1_97(d, len, hwy_K);
    }
    {
      auto [d, len] = make_step1(false);
      hwy_step1_97(d, len, hwy_twice_invK);
    }
    {
      auto [d, dPrev, len, lmax] = make_step2(true);
      hwy_step2_97(d, dPrev, len, lmax, hwy_dwt_delta);
    }
    {
      auto [d, dPrev, len, lmax] = make_step2(false);
      hwy_step2_97(d, dPrev, len, lmax, hwy_dwt_gamma);
    }
    {
      auto [d, dPrev, len, lmax] = make_step2(true);
      hwy_step2_97(d, dPrev, len, lmax, hwy_dwt_beta);
    }
    {
      auto [d, dPrev, len, lmax] = make_step2(false);
      hwy_step2_97(d, dPrev, len, lmax, hwy_dwt_alpha);
    }
  }

  /* Lift-only for L-wide elements (no K/invK scaling, for fused-scale path). */
  static void hwy_step_97_lift(float* mem, uint32_t sn, uint32_t dn, uint32_t parity, Line32 win_l,
                               Line32 win_h)
  {
    const HWY_FULL(float) df;
    const size_t L = Lanes(df);

    if((!parity && dn == 0 && sn <= 1) || (parity && sn == 0 && dn >= 1))
      return;

    auto make_step2 = [&](bool isBandL) -> std::tuple<float*, float*, uint32_t, uint32_t> {
      int64_t parityOff = isBandL ? (int64_t)parity : (int64_t)!parity;
      int64_t band_0 = isBandL ? win_l.x0 : win_h.x0;
      int64_t band_1 = isBandL ? win_l.x1 : win_h.x1;
      int64_t lmax = isBandL ? (std::min<int64_t>)(sn, (int64_t)dn - parityOff)
                             : (std::min<int64_t>)(dn, (int64_t)sn - parityOff);
      if(lmax < 0)
        lmax = 0;
      assert(lmax >= band_0);
      lmax -= band_0;
      float* d = mem + (parityOff + band_0 - win_l.x0) * L;
      d += L;
      float* dPrev = parityOff ? d - 2 * L : d;
      return {d, dPrev, (uint32_t)(band_1 - band_0), (uint32_t)lmax};
    };

    /* skip step1 (K/invK scaling already done during interleave) */
    {
      auto [d, dPrev, len, lmax] = make_step2(true);
      hwy_step2_97(d, dPrev, len, lmax, hwy_dwt_delta);
    }
    {
      auto [d, dPrev, len, lmax] = make_step2(false);
      hwy_step2_97(d, dPrev, len, lmax, hwy_dwt_gamma);
    }
    {
      auto [d, dPrev, len, lmax] = make_step2(true);
      hwy_step2_97(d, dPrev, len, lmax, hwy_dwt_beta);
    }
    {
      auto [d, dPrev, len, lmax] = make_step2(false);
      hwy_step2_97(d, dPrev, len, lmax, hwy_dwt_alpha);
    }
  }

  /* Lift-only for 2*L-wide elements (no K/invK scaling, for fused-scale 2x path).
   * Processes 2*Lanes columns at once, matching the 5/3 inverse pattern. */
  static void hwy_step_97_lift_2x(float* mem, uint32_t sn, uint32_t dn, uint32_t parity,
                                  Line32 win_l, Line32 win_h)
  {
    const HWY_FULL(float) df;
    const size_t L = Lanes(df);
    const size_t PLL = 2 * L;

    if((!parity && dn == 0 && sn <= 1) || (parity && sn == 0 && dn >= 1))
      return;

    auto make_step2 = [&](bool isBandL) -> std::tuple<float*, float*, uint32_t, uint32_t> {
      int64_t parityOff = isBandL ? (int64_t)parity : (int64_t)!parity;
      int64_t band_0 = isBandL ? win_l.x0 : win_h.x0;
      int64_t band_1 = isBandL ? win_l.x1 : win_h.x1;
      int64_t lmax = isBandL ? (std::min<int64_t>)(sn, (int64_t)dn - parityOff)
                             : (std::min<int64_t>)(dn, (int64_t)sn - parityOff);
      if(lmax < 0)
        lmax = 0;
      assert(lmax >= band_0);
      lmax -= band_0;
      float* d = mem + (parityOff + band_0 - win_l.x0) * PLL;
      d += PLL;
      float* dPrev = parityOff ? d - 2 * PLL : d;
      return {d, dPrev, (uint32_t)(band_1 - band_0), (uint32_t)lmax};
    };

    /* skip step1 (K/invK scaling already done during interleave) */
    {
      auto [d, dPrev, len, lmax] = make_step2(true);
      hwy_step2_97_2x(d, dPrev, len, lmax, hwy_dwt_delta);
    }
    {
      auto [d, dPrev, len, lmax] = make_step2(false);
      hwy_step2_97_2x(d, dPrev, len, lmax, hwy_dwt_gamma);
    }
    {
      auto [d, dPrev, len, lmax] = make_step2(true);
      hwy_step2_97_2x(d, dPrev, len, lmax, hwy_dwt_beta);
    }
    {
      auto [d, dPrev, len, lmax] = make_step2(false);
      hwy_step2_97_2x(d, dPrev, len, lmax, hwy_dwt_alpha);
    }
  }

  /* Horizontal strip: interleave rows, apply lifting steps, scatter to dest.
   * Uses GatherIndex/ScatterIndex. 3-tier: 2*L main, L fallback, masked remainder.
   * K/invK scaling is fused into the interleave phase when lifting is needed. */
  static void hwy_h_strip_97(float* scratchMem, uint32_t sn, uint32_t dn, uint32_t parity,
                             Line32 win_l, Line32 win_h, const uint32_t resHeight, float* srcL,
                             uint32_t strideL, float* srcH, uint32_t strideH, float* dest,
                             uint32_t strideDest)
  {
    const HWY_FULL(float) df;
    namespace hn = hwy::HWY_NAMESPACE;
    const auto di = hn::RebindToSigned<decltype(df)>();
    const uint32_t L = (uint32_t)Lanes(df);
    const uint32_t PLL = 2 * L;
    const uint32_t x0_l = win_l.x0, x1_l = win_l.x1;
    const uint32_t x0_h = win_h.x0, x1_h = win_h.x1;
    const auto kV = Set(df, hwy_K);
    const auto invKV = Set(df, hwy_twice_invK);
    /* When trivial, step_97_full returns early without scaling — don't fuse */
    const bool trivial = (!parity && dn == 0 && sn <= 1) || (parity && sn == 0 && dn >= 1);
    const auto scaleL = trivial ? Set(df, 1.0f) : kV;
    const auto scaleH = trivial ? Set(df, 1.0f) : invKV;

    uint32_t j = 0;

    /* Main loop: process 2*L rows at once with fused scaling */
    for(; j + PLL <= resHeight; j += PLL)
    {
      {
        float* sd = scratchMem + parity * PLL;
        float* src = srcL;
        uint32_t stride = strideL;
        uint32_t x0 = x0_l, x1 = x1_l;
        auto scaleV = scaleL;
        for(uint32_t k = 0; k < 2; ++k)
        {
          auto gi = Mul(Set(di, (int32_t)stride), Iota(di, 0));
          for(uint32_t i = x0; i < x1; ++i, sd += PLL * 2)
          {
            Store(Mul(GatherIndex(df, src + i, gi), scaleV), df, sd);
            Store(Mul(GatherIndex(df, src + i + L * stride, gi), scaleV), df, sd + L);
          }
          sd = scratchMem + (1 - parity) * PLL;
          src = srcH;
          stride = strideH;
          x0 = x0_h;
          x1 = x1_h;
          scaleV = scaleH;
        }
      }

      if(!trivial)
        hwy_step_97_lift_2x(scratchMem, sn, dn, parity, win_l, win_h);

      /* Scatter: write 2*L rows via two ScatterIndex per column */
      {
        auto si = Mul(Set(di, (int32_t)strideDest), Iota(di, 0));
        for(uint32_t k = 0; k < (uint32_t)(sn + dn); k++)
        {
          ScatterIndex(Load(df, scratchMem + k * PLL), df, dest + k, si);
          ScatterIndex(Load(df, scratchMem + k * PLL + L), df, dest + k + L * strideDest, si);
        }
      }

      srcL += strideL * PLL;
      srcH += strideH * PLL;
      dest += strideDest * PLL;
    }

    /* Fallback: process L rows with fused scaling */
    if(j + L <= resHeight)
    {
      {
        float* sd = scratchMem + parity * L;
        float* src = srcL;
        uint32_t stride = strideL;
        uint32_t x0 = x0_l, x1 = x1_l;
        auto scaleV = scaleL;
        for(uint32_t k = 0; k < 2; ++k)
        {
          auto gi = Mul(Set(di, (int32_t)stride), Iota(di, 0));
          for(uint32_t i = x0; i < x1; ++i, sd += L * 2)
            Store(Mul(GatherIndex(df, src + i, gi), scaleV), df, sd);
          sd = scratchMem + (1 - parity) * L;
          src = srcH;
          stride = strideH;
          x0 = x0_h;
          x1 = x1_h;
          scaleV = scaleH;
        }
      }

      if(!trivial)
        hwy_step_97_lift(scratchMem, sn, dn, parity, win_l, win_h);

      {
        auto si = Mul(Set(di, (int32_t)strideDest), Iota(di, 0));
        for(uint32_t k = 0; k < (uint32_t)(sn + dn); k++)
          ScatterIndex(Load(df, scratchMem + k * L), df, dest + k, si);
      }

      srcL += strideL * L;
      srcH += strideH * L;
      dest += strideDest * L;
      j += L;
    }

    /* Masked remainder: process remaining rows (< L) with fused scaling */
    if(j < resHeight)
    {
      uint32_t remaining = resHeight - j;
      const auto m = hn::FirstN(df, remaining);

      {
        float* sd = scratchMem + parity * L;
        float* src = srcL;
        uint32_t stride = strideL;
        uint32_t x0 = x0_l, x1 = x1_l;
        auto scaleV = scaleL;
        for(uint32_t k = 0; k < 2; ++k)
        {
          auto gi = Mul(Set(di, (int32_t)stride), Iota(di, 0));
          for(uint32_t i = x0; i < x1; ++i, sd += L * 2)
            Store(Mul(MaskedGatherIndex(m, df, src + i, gi), scaleV), df, sd);
          sd = scratchMem + (1 - parity) * L;
          src = srcH;
          stride = strideH;
          x0 = x0_h;
          x1 = x1_h;
          scaleV = scaleH;
        }
      }

      if(!trivial)
        hwy_step_97_lift(scratchMem, sn, dn, parity, win_l, win_h);

      {
        auto si = Mul(Set(di, (int32_t)strideDest), Iota(di, 0));
        for(uint32_t k = 0; k < (uint32_t)(sn + dn); k++)
          MaskedScatterIndex(Load(df, scratchMem + k * L), m, df, dest + k, si);
      }
    }
  }

  /* Vertical strip: interleave columns, apply lifting steps, scatter to dest.
   * Uses Highway Load/Store. 3-tier: 2*L main, L fallback, masked remainder.
   * K/invK scaling is fused into the interleave phase when lifting is needed. */
  static void hwy_v_strip_97(float* scratchMem, uint32_t sn, uint32_t dn, uint32_t parity,
                             Line32 win_l, Line32 win_h, const uint32_t resWidth,
                             const uint32_t resHeight, float* srcL, uint32_t strideL, float* srcH,
                             uint32_t strideH, float* dest, uint32_t strideDest, int32_t dcShift,
                             int32_t dcMin, int32_t dcMax)
  {
    const HWY_FULL(float) df;
    const HWY_FULL(int32_t) di;
    namespace hn = hwy::HWY_NAMESPACE;
    const uint32_t L = (uint32_t)Lanes(df);
    const uint32_t PLL = 2 * L;
    const auto kV = Set(df, hwy_K);
    const auto invKV = Set(df, hwy_twice_invK);
    /* When trivial, step_97_full returns early without scaling — don't fuse */
    const bool trivial = (!parity && dn == 0 && sn <= 1) || (parity && sn == 0 && dn >= 1);
    const auto scaleL = trivial ? Set(df, 1.0f) : kV;
    const auto scaleH = trivial ? Set(df, 1.0f) : invKV;

    const bool hasDcShift = (dcShift != 0) || (dcMin != dcMax);
    const auto vShift = Set(di, dcShift);
    const auto vmin = Set(di, dcMin);
    const auto vmax = Set(di, dcMax);

    uint32_t j = 0;

    /* Main loop: process 2*L columns at once with fused scaling */
    for(; j + PLL <= resWidth; j += PLL)
    {
      {
        auto bi = scratchMem + parity * PLL;
        auto band = srcL + win_l.x0 * strideL;
        for(uint32_t i = win_l.x0; i < win_l.x1; ++i, bi += 2 * PLL)
        {
          Store(Mul(LoadU(df, band), scaleL), df, bi);
          Store(Mul(LoadU(df, band + L), scaleL), df, bi + L);
          band += strideL;
        }
        bi = scratchMem + (1 - parity) * PLL;
        band = srcH + win_h.x0 * strideH;
        for(uint32_t i = win_h.x0; i < win_h.x1; ++i, bi += 2 * PLL)
        {
          Store(Mul(LoadU(df, band), scaleH), df, bi);
          Store(Mul(LoadU(df, band + L), scaleH), df, bi + L);
          band += strideH;
        }
      }

      if(!trivial)
        hwy_step_97_lift_2x(scratchMem, sn, dn, parity, win_l, win_h);

      /* Scatter: store 2*L columns (two vectors per row) */
      if(hasDcShift)
      {
        auto destI = (int32_t*)dest;
        for(uint32_t k = 0; k < resHeight; ++k)
        {
          auto v0 = NearestInt(Load(df, scratchMem + k * PLL));
          auto v1 = NearestInt(Load(df, scratchMem + k * PLL + L));
          StoreU(Clamp(v0 + vShift, vmin, vmax), di, destI);
          StoreU(Clamp(v1 + vShift, vmin, vmax), di, destI + L);
          destI += strideDest;
        }
      }
      else
      {
        auto destPtr = dest;
        for(uint32_t k = 0; k < resHeight; ++k)
        {
          StoreU(Load(df, scratchMem + k * PLL), df, destPtr);
          StoreU(Load(df, scratchMem + k * PLL + L), df, destPtr + L);
          destPtr += strideDest;
        }
      }

      srcL += PLL;
      srcH += PLL;
      dest += PLL;
    }

    /* Fallback: process L columns with fused scaling */
    if(j + L <= resWidth)
    {
      {
        auto bi = scratchMem + parity * L;
        auto band = srcL + win_l.x0 * strideL;
        for(uint32_t i = win_l.x0; i < win_l.x1; ++i, bi += 2 * L)
        {
          Store(Mul(LoadU(df, band), scaleL), df, bi);
          band += strideL;
        }
        bi = scratchMem + (1 - parity) * L;
        band = srcH + win_h.x0 * strideH;
        for(uint32_t i = win_h.x0; i < win_h.x1; ++i, bi += 2 * L)
        {
          Store(Mul(LoadU(df, band), scaleH), df, bi);
          band += strideH;
        }
      }

      if(!trivial)
        hwy_step_97_lift(scratchMem, sn, dn, parity, win_l, win_h);

      if(hasDcShift)
      {
        auto destI = (int32_t*)dest;
        for(uint32_t k = 0; k < resHeight; ++k)
        {
          auto v0 = NearestInt(Load(df, scratchMem + k * L));
          StoreU(Clamp(v0 + vShift, vmin, vmax), di, destI);
          destI += strideDest;
        }
      }
      else
      {
        auto destPtr = dest;
        for(uint32_t k = 0; k < resHeight; ++k)
        {
          StoreU(Load(df, scratchMem + k * L), df, destPtr);
          destPtr += strideDest;
        }
      }

      srcL += L;
      srcH += L;
      dest += L;
      j += L;
    }

    /* Masked remainder: process remaining columns (< L) with fused scaling */
    if(j < resWidth)
    {
      uint32_t remaining = resWidth - j;
      const auto m = hn::FirstN(df, remaining);
      const auto mi = hn::FirstN(di, remaining);

      {
        auto bi = scratchMem + parity * L;
        auto band = srcL + win_l.x0 * strideL;
        for(uint32_t i = win_l.x0; i < win_l.x1; ++i, bi += 2 * L)
        {
          Store(Mul(hn::MaskedLoad(m, df, band), scaleL), df, bi);
          band += strideL;
        }
        bi = scratchMem + (1 - parity) * L;
        band = srcH + win_h.x0 * strideH;
        for(uint32_t i = win_h.x0; i < win_h.x1; ++i, bi += 2 * L)
        {
          Store(Mul(hn::MaskedLoad(m, df, band), scaleH), df, bi);
          band += strideH;
        }
      }

      if(!trivial)
        hwy_step_97_lift(scratchMem, sn, dn, parity, win_l, win_h);

      if(hasDcShift)
      {
        auto destI = (int32_t*)dest;
        for(uint32_t k = 0; k < resHeight; ++k)
        {
          auto v0 = NearestInt(Load(df, scratchMem + k * L));
          hn::BlendedStore(Clamp(v0 + vShift, vmin, vmax), mi, di, destI);
          destI += strideDest;
        }
      }
      else
      {
        auto destPtr = dest;
        for(uint32_t k = 0; k < resHeight; ++k)
        {
          hn::BlendedStore(Load(df, scratchMem + k * L), m, df, destPtr);
          destPtr += strideDest;
        }
      }
    }
  }

} // namespace HWY_NAMESPACE
} // namespace grk
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace grk
{
HWY_EXPORT(num_lanes);
HWY_EXPORT(GetHWY_PLL_ROWS_97);
HWY_EXPORT(hwy_h_strip_97);
HWY_EXPORT(hwy_v_strip_97);

static uint32_t get_PLL_ROWS_97(void)
{
  static uint32_t value = HWY_DYNAMIC_DISPATCH(GetHWY_PLL_ROWS_97)();
  return value;
}

static const float dwt_alpha = 1.586134342f; /*  12994 */
static const float dwt_beta = 0.052980118f; /*    434 */
static const float dwt_gamma = -0.882911075f; /*  -7233 */
static const float dwt_delta = -0.443506852f; /*  -3633 */
static const float K = 1.230174105f; /*  10078 */
static const float twice_invK = 1.625732422f;

struct Params97
{
  Params97(void) : dataPrev(nullptr), data(nullptr), len(0), lenMax(0) {}
  vec4f* dataPrev;
  vec4f* data;
  uint32_t len;
  uint32_t lenMax;
};

// Notes:
// 1. line buffer 0 offset == dwt->win_l.x0
// 2. dwt->memL and dwt->memH are only set for partial decode
static Params97 makeParams97(dwt_scratch<vec4f>* dwt, bool isBandL, bool step1)
{
  Params97 rc;
  // band_0 specifies absolute start of line buffer
  int64_t band_0 = isBandL ? dwt->win_l.x0 : dwt->win_h.x0;
  int64_t band_1 = isBandL ? dwt->win_l.x1 : dwt->win_h.x1;
  auto memPartial = isBandL ? dwt->memL : dwt->memH;
  int64_t parityOffset = isBandL ? dwt->parity : !dwt->parity;
  int64_t lenMax = isBandL ? (std::min<int64_t>)(dwt->sn, (int64_t)dwt->dn - parityOffset)
                           : (std::min<int64_t>)(dwt->dn, (int64_t)dwt->sn - parityOffset);
  if(lenMax < 0)
    lenMax = 0;
  assert(lenMax >= band_0);
  lenMax -= band_0;
  rc.data = memPartial ? memPartial : dwt->mem;

  assert(!memPartial || (dwt->win_l.x1 <= dwt->sn && dwt->win_h.x1 <= dwt->dn));
  assert(band_1 >= band_0);

  rc.data += parityOffset + band_0 - dwt->win_l.x0;
  rc.len = (uint32_t)(band_1 - band_0);
  if(!step1)
  {
    rc.data += 1;
    rc.dataPrev = parityOffset ? rc.data - 2 : rc.data;
    rc.lenMax = (uint32_t)lenMax;
  }
  if(memPartial)
  {
    assert((uint64_t)rc.data >= (uint64_t)dwt->allocatedMem);
    assert((uint64_t)rc.data <= (uint64_t)dwt->allocatedMem + dwt->lenBytes_);
  }

  return rc;
};

#ifdef __SSE__
void step1_sse_97(Params97 d, const __m128 c)
{
  // process 4 floats at a time
  auto mmData = (__m128*)d.data;
  uint32_t i;
  for(i = 0; i + 3 < d.len; i += 4, mmData += 8)
  {
    mmData[0] = _mm_mul_ps(mmData[0], c);
    mmData[2] = _mm_mul_ps(mmData[2], c);
    mmData[4] = _mm_mul_ps(mmData[4], c);
    mmData[6] = _mm_mul_ps(mmData[6], c);
  }
  for(; i < d.len; ++i, mmData += 2)
    mmData[0] = _mm_mul_ps(mmData[0], c);
}
#endif

static void step1_97(const Params97& d, const float c)
{
#ifdef __SSE__
  step1_sse_97(d, _mm_set1_ps(c));
#else
  float* GRK_RESTRICT fw = (float*)d.data;

  for(uint32_t i = 0; i < d.len; ++i, fw += 8)
  {
    fw[0] *= c;
    fw[1] *= c;
    fw[2] *= c;
    fw[3] *= c;
    ;
  }
#endif
}

#ifdef __SSE__
static void step2_sse_97(const Params97& d, __m128 c)
{
  __m128* GRK_RESTRICT vec_data = (__m128*)d.data;

  uint32_t imax = (std::min<uint32_t>)(d.len, d.lenMax);

  // initial tmp1 value is only necessary when
  // absolute start of line is at 0
  auto tmp1 = ((__m128*)d.dataPrev)[0];
  uint32_t i = 0;
  for(; i + 3 < imax; i += 4)
  {
    auto tmp2 = vec_data[-1];
    auto tmp3 = vec_data[0];
    auto tmp4 = vec_data[1];
    auto tmp5 = vec_data[2];
    auto tmp6 = vec_data[3];
    auto tmp7 = vec_data[4];
    auto tmp8 = vec_data[5];
    auto tmp9 = vec_data[6];
    vec_data[-1] = _mm_add_ps(tmp2, _mm_mul_ps(_mm_add_ps(tmp1, tmp3), c));
    vec_data[1] = _mm_add_ps(tmp4, _mm_mul_ps(_mm_add_ps(tmp3, tmp5), c));
    vec_data[3] = _mm_add_ps(tmp6, _mm_mul_ps(_mm_add_ps(tmp5, tmp7), c));
    vec_data[5] = _mm_add_ps(tmp8, _mm_mul_ps(_mm_add_ps(tmp7, tmp9), c));
    tmp1 = tmp9;
    vec_data += 8;
  }

  for(; i < imax; ++i)
  {
    auto tmp2 = vec_data[-1];
    auto tmp3 = vec_data[0];
    vec_data[-1] = _mm_add_ps(tmp2, _mm_mul_ps(_mm_add_ps(tmp1, tmp3), c));
    tmp1 = tmp3;
    vec_data += 2;
  }
  if(d.lenMax < d.len)
  {
    assert(d.lenMax + 1 == d.len);
    c = _mm_add_ps(c, c);
    c = _mm_mul_ps(c, vec_data[-2]);
    vec_data[-1] = _mm_add_ps(vec_data[-1], c);
  }
}
#endif

static void step2_97(const Params97& d, float c)
{
#ifdef __SSE__
  step2_sse_97(d, _mm_set1_ps(c));
#else

  float* dataPrev = (float*)d.dataPrev;
  float* data = (float*)d.data;

  const uint32_t imax = (std::min<uint32_t>)(d.len, d.lenMax);
  for(uint32_t i = 0; i < imax; ++i)
  {
    float tmp1_1 = dataPrev[0];
    float tmp1_2 = dataPrev[1];
    float tmp1_3 = dataPrev[2];
    float tmp1_4 = dataPrev[3];
    float tmp2_1 = data[-4];
    float tmp2_2 = data[-3];
    float tmp2_3 = data[-2];
    float tmp2_4 = data[-1];
    float tmp3_1 = data[0];
    float tmp3_2 = data[1];
    float tmp3_3 = data[2];
    float tmp3_4 = data[3];
    data[-4] = tmp2_1 + ((tmp1_1 + tmp3_1) * c);
    data[-3] = tmp2_2 + ((tmp1_2 + tmp3_2) * c);
    data[-2] = tmp2_3 + ((tmp1_3 + tmp3_3) * c);
    data[-1] = tmp2_4 + ((tmp1_4 + tmp3_4) * c);
    dataPrev = data;
    data += 8;
  }
  if(d.lenMax < d.len)
  {
    assert(d.lenMax + 1 == d.len);
    c += c;
    data[-4] = data[-4] + dataPrev[0] * c;
    data[-3] = data[-3] + dataPrev[1] * c;
    data[-2] = data[-2] + dataPrev[2] * c;
    data[-1] = data[-1] + dataPrev[3] * c;
  }
#endif
}
/* <summary>                             */
/* Inverse 9-7 wavelet transform in 1-D. */
/* </summary>                            */
void WaveletReverse::step_97(dwt_scratch<vec4f>* GRK_RESTRICT scratch)
{
  if((!scratch->parity && scratch->dn == 0 && scratch->sn <= 1) ||
     (scratch->parity && scratch->sn == 0 && scratch->dn >= 1))
    return;

  step1_97(makeParams97(scratch, true, true), K);
  step1_97(makeParams97(scratch, false, true), twice_invK);
  step2_97(makeParams97(scratch, true, false), dwt_delta);
  step2_97(makeParams97(scratch, false, false), dwt_gamma);
  step2_97(makeParams97(scratch, true, false), dwt_beta);
  step2_97(makeParams97(scratch, false, false), dwt_alpha);
}
void WaveletReverse::interleave_h_97(dwt_scratch<vec4f>* GRK_RESTRICT scratch,
                                     Buffer2dSimple<float> winL, Buffer2dSimple<float> winH,
                                     uint32_t remaining_height)
{
  float* GRK_RESTRICT scratchDataL = (float*)(scratch->mem + scratch->parity);
  uint32_t x0 = scratch->win_l.x0;
  uint32_t x1 = scratch->win_l.x1;
  const size_t vec4f_elts = vec4f::NUM_ELTS;
  for(uint32_t k = 0; k < 2; ++k)
  {
    auto src = (k == 0) ? winL.buf_ : winH.buf_;
    uint32_t stride = (k == 0) ? winL.stride_ : winH.stride_;
    if(remaining_height >= vec4f_elts && ((size_t)src & 0x0f) == 0 &&
       ((size_t)scratchDataL & 0x0f) == 0 && (stride & 0x0f) == 0)
    {
      /* Fast code path */
      for(uint32_t i = x0; i < x1; ++i, scratchDataL += vec4f_elts * 2)
      {
        uint32_t j = i;
        scratchDataL[0] = src[j];
        j += stride;
        scratchDataL[1] = src[j];
        j += stride;
        scratchDataL[2] = src[j];
        j += stride;
        scratchDataL[3] = src[j];
      }
    }
    else
    {
      /* Slow code path */
      for(uint32_t i = x0; i < x1; ++i, scratchDataL += vec4f_elts * 2)
      {
        uint32_t j = i;
        scratchDataL[0] = src[j];
        j += stride;
        if(remaining_height == 1)
          continue;
        scratchDataL[1] = src[j];
        j += stride;
        if(remaining_height == 2)
          continue;
        scratchDataL[2] = src[j];
        j += stride;
        if(remaining_height == 3)
          continue;
        scratchDataL[3] = src[j];
      }
    }
    scratchDataL = (float*)(scratch->mem + 1 - scratch->parity);
    x0 = scratch->win_h.x0;
    x1 = scratch->win_h.x1;
  }
}
void WaveletReverse::h_strip_97(dwt_scratch<vec4f>* GRK_RESTRICT scratch, const uint32_t resHeight,
                                Buffer2dSimple<float> winL, Buffer2dSimple<float> winH,
                                Buffer2dSimple<float> winDest)
{
  HWY_DYNAMIC_DISPATCH(hwy_h_strip_97)
  ((float*)scratch->mem, scratch->sn, scratch->dn, scratch->parity, scratch->win_l, scratch->win_h,
   resHeight, winL.buf_, winL.stride_, winH.buf_, winH.stride_, winDest.buf_, winDest.stride_);
}
bool WaveletReverse::h_97(uint8_t res, uint32_t num_threads, size_t dataLength,
                          dwt_scratch<vec4f>& GRK_RESTRICT scratch, const uint32_t resHeight,
                          Buffer2dSimple<float> winL, Buffer2dSimple<float> winH,
                          Buffer2dSimple<float> winDest)
{
  if(resHeight == 0)
    return true;
  uint32_t numTasks = num_threads;
  if(resHeight < numTasks)
    numTasks = resHeight;
  const uint32_t incrPerJob = resHeight / numTasks;
  auto imageComponentFlow = ((DecompressScheduler*)scheduler_)->getImageComponentFlow(compno_);
  // return if no code blocks were decoded for this component
  if(!imageComponentFlow)
    return true;
  auto resFlow = imageComponentFlow->getResflow(res - 1);
  for(uint32_t j = 0; j < numTasks; ++j)
  {
    auto indexMin = j * incrPerJob;
    auto indexMax = (j < (numTasks - 1U) ? (j + 1U) * incrPerJob : resHeight) - indexMin;
    auto myhoriz = std::make_shared<dwt_scratch<vec4f>>(scratch);
    if(!myhoriz->alloc(dataLength * get_PLL_ROWS_97() / vec4f::NUM_ELTS))
    {
      grklog.error("Out of memory");
      return false;
    }
    resFlow->waveletHoriz_->nextTask().work([this, myhoriz, indexMax, winL, winH, winDest] {
      h_strip_97(myhoriz.get(), indexMax, winL, winH, winDest);
    });
    winL.incY_IN_PLACE(incrPerJob);
    winH.incY_IN_PLACE(incrPerJob);
    winDest.incY_IN_PLACE(incrPerJob);
  }
  return true;
}
void WaveletReverse::interleave_v_97(dwt_scratch<vec4f>* GRK_RESTRICT scratch,
                                     Buffer2dSimple<float> winL, Buffer2dSimple<float> winH,
                                     uint32_t nb_elts_read)
{
  auto bi = scratch->mem + scratch->parity;
  auto band = winL.buf_ + scratch->win_l.x0 * winL.stride_;
  for(uint32_t i = scratch->win_l.x0; i < scratch->win_l.x1; ++i, bi += 2)
  {
    memcpy((float*)bi, band, nb_elts_read * sizeof(float));
    band += winL.stride_;
  }
  bi = scratch->mem + 1 - scratch->parity;
  band = winH.buf_ + scratch->win_h.x0 * winH.stride_;
  for(uint32_t i = scratch->win_h.x0; i < scratch->win_h.x1; ++i, bi += 2)
  {
    memcpy((float*)bi, band, nb_elts_read * sizeof(float));
    band += winH.stride_;
  }
}
void WaveletReverse::v_strip_97(dwt_scratch<vec4f>* GRK_RESTRICT scratch, const uint32_t resWidth,
                                const uint32_t resHeight, Buffer2dSimple<float> winL,
                                Buffer2dSimple<float> winH, Buffer2dSimple<float> winDest,
                                DcShiftParam dcShift)
{
  HWY_DYNAMIC_DISPATCH(hwy_v_strip_97)
  ((float*)scratch->mem, scratch->sn, scratch->dn, scratch->parity, scratch->win_l, scratch->win_h,
   resWidth, resHeight, winL.buf_, winL.stride_, winH.buf_, winH.stride_, winDest.buf_,
   winDest.stride_, dcShift.enabled ? dcShift.shift : 0, dcShift.min, dcShift.max);
}
bool WaveletReverse::v_97(uint8_t res, uint32_t num_threads, size_t dataLength,
                          dwt_scratch<vec4f>& GRK_RESTRICT scratch, const uint32_t resWidth,
                          const uint32_t resHeight, Buffer2dSimple<float> winL,
                          Buffer2dSimple<float> winH, Buffer2dSimple<float> winDest)
{
  if(resWidth == 0)
    return true;

  /* Apply DC shift only on the last resolution level */
  DcShiftParam dcShift = (res == numres_ - 1) ? dcShift_ : DcShiftParam{};

  auto numTasks = num_threads;
  if(resWidth < numTasks)
    numTasks = resWidth;
  const auto incrPerJob = resWidth / numTasks;
  auto imageComponentFlow = ((DecompressScheduler*)scheduler_)->getImageComponentFlow(compno_);
  // return if no code blocks were decoded for this component
  if(!imageComponentFlow)
    return true;
  auto resFlow = imageComponentFlow->getResflow(res - 1);
  for(uint32_t j = 0; j < numTasks; j++)
  {
    auto indexMin = j * incrPerJob;
    auto indexMax = (j < (numTasks - 1U) ? (j + 1U) * incrPerJob : resWidth) - indexMin;
    auto myvert = std::make_shared<dwt_scratch<vec4f>>(scratch);
    if(!myvert->alloc(dataLength * get_PLL_ROWS_97() / vec4f::NUM_ELTS))
    {
      grklog.error("Out of memory");
      return false;
    }
    resFlow->waveletVert_->nextTask().work(
        [this, myvert, resHeight, indexMax, winL, winH, winDest, dcShift] {
          v_strip_97(myvert.get(), indexMax, resHeight, winL, winH, winDest, dcShift);
        });
    winL.incX_IN_PLACE(incrPerJob);
    winH.incX_IN_PLACE(incrPerJob);
    winDest.incX_IN_PLACE(incrPerJob);
  }

  return true;
}
/* <summary>                             */
/* Inverse 9-7 wavelet transform in 2-D. */
/* </summary>                            */
bool WaveletReverse::tile_97(void)
{
  if(numres_ == 1U)
    return true;

  auto tr = tilec_->resolutions_;
  auto buf = tilec_->getWindow();
  uint32_t resWidth = tr->width();
  uint32_t resHeight = tr->height();

  size_t dataLength = max_resolution(tr, numres_);
  size_t scaledDataLength = dataLength * get_PLL_ROWS_97() / vec4f::NUM_ELTS;
  if(!horiz97.alloc(scaledDataLength))
  {
    grklog.error("tile_97: out of memory");
    return false;
  }
  vert97.mem = horiz97.mem;
  uint32_t num_threads = (uint32_t)TFSingleton::num_threads();
  for(uint8_t res = 1; res < numres_; ++res)
  {
    horiz97.sn = resWidth;
    vert97.sn = resHeight;
    ++tr;
    resWidth = tr->width();
    resHeight = tr->height();
    if(resWidth == 0 || resHeight == 0)
      continue;
    horiz97.dn = resWidth - horiz97.sn;
    horiz97.parity = tr->x0 & 1;
    horiz97.win_l = Line32(0, horiz97.sn);
    horiz97.win_h = Line32(0, horiz97.dn);
    auto winSplitL = buf->getResWindowBufferSplitSimpleF(res, SPLIT_L);
    auto winSplitH = buf->getResWindowBufferSplitSimpleF(res, SPLIT_H);
    if(!h_97(res, num_threads, dataLength, horiz97, vert97.sn,
             buf->getResWindowBufferSimpleF((uint8_t)(res - 1U)),
             buf->getBandWindowBufferPaddedSimpleF(res, t1::BAND_ORIENT_HL), winSplitL))
      return false;
    if(!h_97(res, num_threads, dataLength, horiz97, resHeight - vert97.sn,
             buf->getBandWindowBufferPaddedSimpleF(res, t1::BAND_ORIENT_LH),
             buf->getBandWindowBufferPaddedSimpleF(res, t1::BAND_ORIENT_HH), winSplitH))
      return false;
    vert97.dn = resHeight - vert97.sn;
    vert97.parity = tr->y0 & 1;
    vert97.win_l = Line32(0, vert97.sn);
    vert97.win_h = Line32(0, vert97.dn);
    if(!v_97(res, num_threads, dataLength, vert97, resWidth, resHeight, winSplitL, winSplitH,
             buf->getResWindowBufferSimpleF(res)))
      return false;
  }

  return true;
}

} // namespace grk
#endif
