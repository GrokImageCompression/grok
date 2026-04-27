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

#include "hwy_arm_disable_targets.h"

#include "TFSingleton.h"
#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "ISparseCanvas.h"
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
#include "TileComponentWindow.h"
#include "WaveletCommon.h"
#include "WaveletReverse.h"
#include "WaveletFwd.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "wavelet/WaveletFwd.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>
HWY_BEFORE_NAMESPACE();
namespace grk
{

template<typename T>
struct dwt_line;

template<typename T, typename DWT>
struct encode_info;

namespace HWY_NAMESPACE
{
  static const float alpha = -1.586134342f;
  static const float beta = -0.052980118f;
  static const float gamma = 0.882911075f;
  static const float delta = 0.443506852f;
  static const float K = 1.230174105f;
  static const float invK = (float)(1.0 / 1.230174105);

  template<typename T>
  void deinterleave_h(const T* src, T* dst, const uint32_t dn, const uint32_t sn,
                      const uint32_t stride, const uint32_t parity, const uint32_t numrows)
  {
    const HWY_FULL(T) d;
    const HWY_FULL(int32_t) di;
    const size_t lanes = Lanes(d);

    T* destPtrLow = dst;
    const T* srcPtrLow = src + parity * lanes;

    T* destPtrHigh = dst + sn;
    const T* srcPtrHigh = src + !parity * lanes;
    uint32_t i = 0;
    const auto indices = Iota(di, 0) * Set(di, static_cast<int32_t>(stride));
    for(; i < std::min(dn, sn); ++i)
    {
      ScatterIndexN(Load(d, srcPtrLow), d, destPtrLow, indices, numrows);
      ScatterIndexN(Load(d, srcPtrHigh), d, destPtrHigh, indices, numrows);

      destPtrLow++;
      srcPtrLow += (lanes << 1);
      destPtrHigh++;
      srcPtrHigh += (lanes << 1);
    }

    if(i < sn)
      ScatterIndexN(Load(d, srcPtrLow), d, destPtrLow, indices, numrows);
    else if(i < dn)
      ScatterIndexN(Load(d, srcPtrHigh), d, destPtrHigh, indices, numrows);
  }

  template<typename T>
  void deinterleave_v(const T* src, T* dst, const uint32_t dn, const uint32_t sn,
                      const uint32_t stride, const uint32_t parity, const uint32_t numcols)
  {
    const HWY_FULL(T) d;
    const size_t lanes = Lanes(d);

    T* destPtrLow = dst;
    const T* srcPtrLow = src + parity * lanes;

    T* destPtrHigh = dst + size_t(sn) * stride;
    const T* srcPtrHigh = src + !parity * lanes;
    uint32_t i = 0;
    for(; i < std::min(dn, sn); ++i)
    {
      StoreN(Load(d, srcPtrLow), d, destPtrLow, numcols);
      StoreN(Load(d, srcPtrHigh), d, destPtrHigh, numcols);

      destPtrLow += stride;
      srcPtrLow += (lanes << 1);
      destPtrHigh += stride;
      srcPtrHigh += (lanes << 1);
    }

    if(i < sn)
      StoreN(Load(d, srcPtrLow), d, destPtrLow, numcols);
    else if(i < dn)
      StoreN(Load(d, srcPtrHigh), d, destPtrHigh, numcols);
  }

  HWY_ATTR void encode_53_v(int32_t* resolution, int32_t* scratch, const uint32_t height,
                            const uint8_t parity, const uint32_t stride, const uint32_t numcols,
                            int32_t dcShift)
  {
    const HWY_FULL(int32_t) d;
    if(height <= 1)
    {
      if(parity == 1 && height == 1)
      {
        auto v = Load(d, resolution);
        if(dcShift != 0)
          v = v - Set(d, dcShift);
        auto res = ShiftLeft<1>(v);
        StoreN(res, d, resolution, numcols);
      }
      return;
    }

    const uint32_t sn = (height + !parity) >> 1;
    const uint32_t dn = height - sn;

    // note: d_i is always transformed, while s_i is untransformed and s_ii is transformed
    const size_t lanes = Lanes(d);
    const auto c_d2 = Set(d, 2);
    if(parity == 0)
    {
      // note: dn <= sn
      uint32_t i = 0;
      auto s_0 = Load(d, resolution);
      auto d_0 = Load(d, resolution + stride);
      if(sn > 1)
      {
        //  high pass top
        const auto s_0p1 = Load(d, resolution + 2 * stride);
        d_0 -= ShiftRight<1>(s_0 + s_0p1);
        Store(d_0, d, &scratch[lanes]);
        auto s_i = s_0p1;
        i++;
        // low pass reflection at top
        Store(s_0 + ShiftRight<2>(ShiftLeft<1>(d_0) + c_d2), d, &scratch[0]);

        auto d_im1 = d_0;
        for(; i + 1 < sn; i++)
        {
          // high pass
          const auto s_ip1 = Load(d, resolution + ((i + 1) << 1) * stride);
          const auto d_i =
              Load(d, resolution + ((i << 1) + 1) * stride) - ShiftRight<1>(s_i + s_ip1);
          Store(d_i, d, &scratch[((i << 1) + 1) * lanes]);
          // low pass
          const auto s_ii = s_i + ShiftRight<2>(d_im1 + d_i + c_d2);
          Store(s_ii, d, &scratch[(i << 1) * lanes]);

          s_i = s_ip1;
          d_im1 = d_i;
        }
        if(dn == sn)
        {
          // high pass reflection at bottom
          const auto d_i = Load(d, resolution + ((i << 1) + 1) * stride) - s_i;
          Store(d_i, d, &scratch[((i << 1) + 1) * lanes]);

          // final low-pass step
          const auto s_ii =
              Load(d, resolution + (i << 1) * stride) + ShiftRight<2>(d_im1 + d_i + c_d2);
          Store(s_ii, d, &scratch[(i << 1) * lanes]);
        }
        else
        {
          // low pass reflection at bottom
          const auto s_ii =
              Load(d, resolution + (i << 1) * stride) + ShiftRight<2>(ShiftLeft<1>(d_im1) + c_d2);
          Store(s_ii, d, &scratch[(i << 1) * lanes]);
        }
      }
      else if(sn == dn)
      {
        // sn == dn == 1
        d_0 -= s_0;

        // high pass reflection
        Store(d_0, d, &scratch[((i << 1) + 1) * lanes]);

        // low pass reflection
        Store(s_0 + ShiftRight<2>(ShiftLeft<1>(d_0) + c_d2), d, &scratch[0]);
      }
    }
    else
    {
      // note: sn <= dn

      // high pass reflection at top
      auto s_i = Load(d, resolution + 1 * stride); // s_0
      auto d_0 = Load(d, resolution + 0 * stride) - s_i;
      Store(d_0, d, &scratch[0]);

      auto s_im1 = s_i;
      auto d_im1 = d_0;
      uint32_t i = 1;
      for(; i < sn; i++)
      {
        // high
        s_i = Load(d, resolution + ((i << 1) + 1) * stride);
        const auto d_i = Load(d, resolution + (i << 1) * stride) - ShiftRight<1>(s_i + s_im1);
        Store(d_i, d, &scratch[(i << 1) * lanes]);
        // low
        const auto s_iim1 = s_im1 + ShiftRight<2>(d_im1 + d_i + c_d2);
        Store(s_iim1, d, &scratch[(((i - 1) << 1) + 1) * lanes]);

        d_im1 = d_i;
        s_im1 = s_i;
      }

      if(sn < dn)
      {
        // high pass reflection at bottom (at index sn = dn - 1)
        const auto d_sn = Load(d, resolution + (sn << 1) * stride) - s_im1;
        Store(d_sn, d, &scratch[(sn << 1) * lanes]);

        // regular low pass at bottom
        const auto s_iim1 = Load(d, resolution + (((dn - 2) << 1) + 1) * stride) +
                            ShiftRight<2>(d_im1 + d_sn + c_d2);
        Store(s_iim1, d, &scratch[(((dn - 2) << 1) + 1) * lanes]);
      }
      else
      {
        // low pass reflection at bottom
        const auto s_ii = Load(d, resolution + (((dn - 1) << 1) + 1) * stride) +
                          ShiftRight<2>(ShiftLeft<1>(d_im1) + c_d2);
        Store(s_ii, d, &scratch[(((dn - 1) << 1) + 1) * lanes]);
      }
    }

    // apply DC shift to low-pass band before deinterleaving
    if(dcShift != 0)
    {
      const auto vshift = Set(d, dcShift);
      for(uint32_t k = parity; k < height; k += 2)
        Store(Load(d, &scratch[k * lanes]) - vshift, d, &scratch[k * lanes]);
    }
    deinterleave_v(scratch, resolution, dn, sn, stride, parity, numcols);
  }

  HWY_ATTR void encode_53_h(int32_t* resolution, int32_t* scratch, const uint32_t width,
                            const uint8_t parity, const uint32_t stride, const uint32_t numrows,
                            int32_t dcShift)
  {
    const HWY_FULL(int32_t) d;
    const auto indices = Iota(d, 0) * Set(d, static_cast<int32_t>(stride));
    if(width <= 1)
    {
      if(parity == 1 && width == 1)
      {
        auto v = GatherIndexN(d, resolution, indices, numrows);
        if(dcShift != 0)
          v = v - Set(d, dcShift);
        ScatterIndexN(ShiftLeft<1>(v), d, resolution, indices, numrows);
      }
      return;
    }
    const uint32_t sn = (width + !parity) >> 1;
    const uint32_t dn = width - sn;

    // note: d_i is always transformed, while s_i is untransformed and s_ii is transformed
    const size_t lanes = Lanes(d);
    const auto c_d2 = Set(d, 2);
    if(parity == 0)
    {
      // note: dn <= sn
      uint32_t i = 0;
      const auto s_0 = GatherIndexN(d, resolution, indices, numrows);
      auto d_0 = GatherIndexN(d, resolution, indices + Set(d, static_cast<int32_t>(1)), numrows);
      if(sn > 1)
      {
        //  high pass top
        auto s_i = GatherIndexN(d, resolution, indices + c_d2, numrows); // this is actually s_1
        d_0 -= ShiftRight<1>(s_0 + s_i);
        Store(d_0, d, &scratch[lanes]);
        i++;
        // low pass reflection at top
        Store(s_0 + ShiftRight<2>(ShiftLeft<1>(d_0) + c_d2), d, &scratch[0]);

        auto d_im1 = d_0;
        for(; i + 1 < sn; i++)
        {
          // high pass
          auto s_ip1 = GatherIndexN(
              d, resolution, indices + Set(d, static_cast<int32_t>(((i + 1) << 1))), numrows);
          auto d_i = GatherIndexN(d, resolution,
                                  indices + Set(d, static_cast<int32_t>(((i << 1) + 1))), numrows);
          d_i -= ShiftRight<1>(s_i + s_ip1);
          Store(d_i, d, &scratch[((i << 1) + 1) * lanes]);
          // low pass
          auto s_ii = s_i + ShiftRight<2>(d_im1 + d_i + c_d2);
          Store(s_ii, d, &scratch[(i << 1) * lanes]);

          s_i = s_ip1;
          d_im1 = d_i;
        }
        if(dn == sn)
        {
          // high pass reflection at bottom
          auto d_i = GatherIndexN(d, resolution,
                                  indices + Set(d, static_cast<int32_t>(((i << 1) + 1))), numrows);
          d_i -= s_i;
          Store(d_i, d, &scratch[((i << 1) + 1) * lanes]);

          // final low-pass step
          auto s_ii = GatherIndexN(d, resolution, indices + Set(d, static_cast<int32_t>((i << 1))),
                                   numrows);
          s_ii += ShiftRight<2>(d_im1 + d_i + c_d2);
          Store(s_ii, d, &scratch[(i << 1) * lanes]);
        }
        else
        {
          // low pass reflection at bottom
          auto s_ii = GatherIndexN(d, resolution, indices + Set(d, static_cast<int32_t>((i << 1))),
                                   numrows);
          s_ii += ShiftRight<2>(ShiftLeft<1>(d_im1) + c_d2);
          Store(s_ii, d, &scratch[(i << 1) * lanes]);
        }
      }
      else if(sn == dn)
      {
        // sn == dn == 1
        d_0 -= s_0;

        // high pass reflection
        Store(d_0, d, &scratch[((i << 1) + 1) * lanes]);

        // low pass reflection
        Store(s_0 + ShiftRight<2>(ShiftLeft<1>(d_0) + c_d2), d, &scratch[0]);
      }
    }
    else
    {
      // note: sn <= dn

      // high pass reflection at top
      auto s_i =
          GatherIndexN(d, resolution, indices + Set(d, static_cast<int32_t>(1)), numrows); // s_0
      const auto d_0 = GatherIndexN(d, resolution, indices, numrows) - s_i;
      Store(d_0, d, &scratch[0]);

      auto s_im1 = s_i;
      auto d_im1 = d_0;
      uint32_t i = 1;
      for(; i < sn; i++)
      {
        // high
        s_i = GatherIndexN(d, resolution, indices + Set(d, static_cast<int32_t>((i << 1) + 1)),
                           numrows);
        const auto d_i =
            GatherIndexN(d, resolution, indices + Set(d, static_cast<int32_t>((i << 1))), numrows) -
            ShiftRight<1>(s_i + s_im1);
        Store(d_i, d, &scratch[(i << 1) * lanes]);
        // low
        const auto s_iim1 = s_im1 + ShiftRight<2>(d_im1 + d_i + c_d2);
        Store(s_iim1, d, &scratch[(((i - 1) << 1) + 1) * lanes]);

        d_im1 = d_i;
        s_im1 = s_i;
      }

      if(sn < dn)
      {
        // high pass reflection at bottom (at index sn = dn - 1)
        const auto d_sn = GatherIndexN(d, resolution,
                                       indices + Set(d, static_cast<int32_t>((sn << 1))), numrows) -
                          s_im1;
        Store(d_sn, d, &scratch[(sn << 1) * lanes]);

        // regular low pass at bottom
        const auto s_iim1 =
            GatherIndexN(d, resolution, indices + Set(d, static_cast<int32_t>(((dn - 2) << 1) + 1)),
                         numrows) +
            ShiftRight<2>(d_im1 + d_sn + c_d2);
        Store(s_iim1, d, &scratch[(((dn - 2) << 1) + 1) * lanes]);
      }
      else
      {
        // low pass reflection at bottom
        const auto s_ii =
            GatherIndexN(d, resolution, indices + Set(d, static_cast<int32_t>(((dn - 1) << 1) + 1)),
                         numrows) +
            ShiftRight<2>(ShiftLeft<1>(d_im1) + c_d2);
        Store(s_ii, d, &scratch[(((dn - 1) << 1) + 1) * lanes]);
      }
    }

    // apply DC shift to low-pass band before deinterleaving
    if(dcShift != 0)
    {
      const auto vshift = Set(d, dcShift);
      for(uint32_t k = parity; k < width; k += 2)
        Store(Load(d, &scratch[k * lanes]) - vshift, d, &scratch[k * lanes]);
    }
    deinterleave_h(scratch, resolution, dn, sn, stride, parity, numrows);
  }

  void encode_97_h(float* res, float* scratch, const uint32_t width, const uint8_t parity,
                   const uint32_t stride, const uint32_t numrows, float dcShift)
  {
    if(width == 1)
      return;

    const HWY_FULL(float) d;
    const size_t lanes = Lanes(d);
    const size_t lanesx2 = lanes << 1;
    const uint32_t sn = (width + !parity) >> 1;
    const uint32_t dn = width - sn;
    const uint32_t off_s = parity;
    const uint32_t off_d = !parity;
    const int32_t reflect_d = ((int32_t)off_d - (int32_t)off_s) * (int32_t)lanes;
    const int32_t reflect_s = ((int32_t)off_s - (int32_t)off_d) * (int32_t)lanes;

    // fetch
    auto temp = scratch;
    const HWY_FULL(int32_t) di;
    const auto indices = Iota(di, 0) * Set(di, static_cast<int32_t>(stride));
    if(dcShift != 0.0f)
    {
      const auto vshift = Set(d, dcShift);
      for(auto j = 0U; j < width; ++j)
      {
        auto ind = indices + Set(di, static_cast<int32_t>(j));
        Store(GatherIndexN(d, res, ind, numrows) - vshift, d, temp);
        temp += lanes;
      }
    }
    else
    {
      for(auto j = 0U; j < width; ++j)
      {
        auto ind = indices + Set(di, static_cast<int32_t>(j));
        Store(GatherIndexN(d, res, ind, numrows), d, temp);
        temp += lanes;
      }
    }

    // high pass 1
    auto scratch_d = scratch + off_d * lanes;
    const uint32_t samples_no_end_bdry_d = std::min<uint32_t>(dn, sn - !parity);
    auto s_d = Set(d, alpha);

    auto scratch_s = scratch + off_s * lanes;
    const uint32_t samples_no_end_bdry_s = std::min<uint32_t>(sn, dn - parity);
    auto s_s = Set(d, beta);

    uint32_t i_d = 0;
    uint32_t i_s = 0;
    auto prev_d = s_s;
    auto prev_s = s_s;
    if(samples_no_end_bdry_d != 0)
    {
      // reflection at left
      prev_d = Load(d, &scratch_d[reflect_s]);
      auto curr_d = Load(d, &scratch_d[0]);
      auto next_d = Load(d, &scratch_d[lanes]);
      Store(curr_d + (prev_d + next_d) * s_d, d, &scratch_d[0]);
      scratch_d += lanesx2;
      prev_d = next_d;
      i_d++;
    }
    for(; i_d < samples_no_end_bdry_d; ++i_d)
    {
      auto curr_d = Load(d, &scratch_d[0]);
      auto next_d = Load(d, &scratch_d[lanes]);
      Store(curr_d + (prev_d + next_d) * s_d, d, &scratch_d[0]);
      scratch_d += lanesx2;
      prev_d = next_d;
    }

    if(i_d + 1 == dn)
    {
      // reflection at right
      prev_d = Load(d, &scratch_d[-1 * (int64_t)lanes]);
      auto curr_d = Load(d, &scratch_d[0]);
      Store(curr_d + (prev_d + prev_d) * s_d, d, &scratch_d[0]);
    }
    // low pass 1
    if(samples_no_end_bdry_s != 0)
    {
      // reflection at left
      prev_s = Load(d, &scratch_s[reflect_d]);
      auto curr_s = Load(d, &scratch_s[0]);
      auto next_s = Load(d, &scratch_s[lanes]);
      Store(curr_s + (prev_s + next_s) * s_s, d, &scratch_s[0]);
      scratch_s += lanesx2;
      prev_s = next_s;
      i_s++;
    }
    for(; i_s < samples_no_end_bdry_s; ++i_s)
    {
      auto curr_s = Load(d, &scratch_s[0]);
      auto next_s = Load(d, &scratch_s[lanes]);
      Store(curr_s + (prev_s + next_s) * s_s, d, &scratch_s[0]);
      scratch_s += lanesx2;
      prev_s = next_s;
    }

    if(i_s + 1 == sn)
    {
      // reflection at right
      prev_s = Load(d, &scratch_s[-1 * (int64_t)lanes]);
      auto curr_s = Load(d, &scratch_s[0]);
      Store(curr_s + (prev_s + prev_s) * s_s, d, &scratch_s[0]);
    }

    // high pass 2
    scratch_d = scratch + off_d * lanes;
    s_d = Set(d, gamma);
    const auto sf_d = Set(d, K);

    scratch_s = scratch + off_s * lanes;
    s_s = Set(d, delta);
    const auto sf_s = Set(d, invK);

    i_d = 0;
    if(samples_no_end_bdry_d != 0)
    {
      // reflection at left
      prev_d = Load(d, &scratch_d[reflect_s]);
      auto curr_d = Load(d, &scratch_d[0]);
      auto next_d = Load(d, &scratch_d[lanes]);
      Store((curr_d + (prev_d + next_d) * s_d) * sf_d, d, &scratch_d[0]);
      scratch_d += lanesx2;
      prev_d = next_d;
      i_d++;
    }
    for(; i_d < samples_no_end_bdry_d; ++i_d)
    {
      auto curr_d = Load(d, &scratch_d[0]);
      auto next_d = Load(d, &scratch_d[lanes]);
      Store((curr_d + (prev_d + next_d) * s_d) * sf_d, d, &scratch_d[0]);
      scratch_d += lanesx2;
      prev_d = next_d;
    }

    if(i_d + 1 == dn)
    {
      // reflection at right
      prev_d = Load(d, &scratch_d[-1 * (int64_t)lanes]);
      auto curr_d = Load(d, &scratch_d[0]);
      Store((curr_d + (prev_d + prev_d) * s_d) * sf_d, d, &scratch_d[0]);
    }
    // low pass 2

    i_s = 0;
    if(samples_no_end_bdry_s != 0)
    {
      // reflection at left
      prev_s = Load(d, &scratch_s[reflect_d]);
      auto curr_s = Load(d, &scratch_s[0]);
      auto next_s = Load(d, &scratch_s[lanes]);
      Store((curr_s + (prev_s + next_s) * s_s) * sf_s, d, &scratch_s[0]);
      scratch_s += lanesx2;
      prev_s = next_s;
      i_s++;
    }
    for(; i_s < samples_no_end_bdry_s; ++i_s)
    {
      auto curr_s = Load(d, &scratch_s[0]);
      auto next_s = Load(d, &scratch_s[lanes]);
      Store((curr_s + (prev_s + next_s) * s_s) * sf_s, d, &scratch_s[0]);
      scratch_s += lanesx2;
      prev_s = next_s;
    }

    if(i_s + 1 == sn)
    {
      // reflection at right
      prev_s = Load(d, &scratch_s[-1 * (int64_t)lanes]);
      auto curr_s = Load(d, &scratch_s[0]);
      Store((curr_s + (prev_s + prev_s) * s_s) * sf_s, d, &scratch_s[0]);
    }

    deinterleave_h(scratch, res, dn, sn, stride, parity, numrows);
  }

  void encode_97_v(float* res, float* scratch, const uint32_t height, const uint8_t parity,
                   const uint32_t stride, const uint32_t numcols, float dcShift, bool intInput)
  {
    const HWY_FULL(float) d;
    const size_t lanes = Lanes(d);
    const auto lanesx2 = (lanes << 1);
    const uint32_t sn = (height + !parity) >> 1;
    const uint32_t dn = height - sn;
    if(height <= 1)
      return;

    auto temp = scratch;
    if(intInput)
    {
      // First level: tile buffer contains int32_t — convert to float inline
      const HWY_FULL(int32_t) di;
      auto intIn = (const int32_t*)res;
      if(dcShift != 0.0f)
      {
        const auto vshift = Set(d, dcShift);
        for(auto j = 0U; j < height; ++j)
        {
          Store(ConvertTo(d, Load(di, intIn)) - vshift, d, temp);
          temp += lanes;
          intIn += stride;
        }
      }
      else
      {
        for(auto j = 0U; j < height; ++j)
        {
          Store(ConvertTo(d, Load(di, intIn)), d, temp);
          temp += lanes;
          intIn += stride;
        }
      }
    }
    else
    {
      auto in = res;
      if(dcShift != 0.0f)
      {
        const auto vshift = Set(d, dcShift);
        for(auto j = 0U; j < height; ++j)
        {
          Store(Load(d, in) - vshift, d, temp);
          temp += lanes;
          in += stride;
        }
      }
      else
      {
        for(auto j = 0U; j < height; ++j)
        {
          Store(Load(d, in), d, temp);
          temp += lanes;
          in += stride;
        }
      }
    }

    const uint32_t off_s = parity;
    const uint32_t off_d = !parity;
    const int32_t reflect_d = ((int32_t)off_d - (int32_t)off_s) * (int32_t)lanes;
    const int32_t reflect_s = ((int32_t)off_s - (int32_t)off_d) * (int32_t)lanes;

    // high pass 1
    auto scratch_d = scratch + off_d * lanes;
    const auto samples_no_end_bdry_d = std::min<uint32_t>(dn, sn - !parity);
    auto s_d = Set(d, alpha);

    auto scratch_s = scratch + off_s * lanes;
    const auto samples_no_end_bdry_s = std::min<uint32_t>(sn, dn - parity);
    auto s_s = Set(d, beta);

    uint32_t i_d = 0;
    uint32_t i_s = 0;
    auto prev_d = s_s;
    auto prev_s = s_s;
    if(samples_no_end_bdry_d != 0)
    {
      // reflection at top
      prev_d = Load(d, &scratch_d[reflect_s]);
      auto curr_d = Load(d, &scratch_d[0]);
      auto next_d = Load(d, &scratch_d[lanes]);
      Store(curr_d + (prev_d + next_d) * s_d, d, &scratch_d[0]);
      scratch_d += lanesx2;
      prev_d = next_d;
      i_d++;
    }

    for(; i_d < samples_no_end_bdry_d; ++i_d)
    {
      auto curr_d = Load(d, &scratch_d[0]);
      auto next_d = Load(d, &scratch_d[lanes]);
      Store(curr_d + (prev_d + next_d) * s_d, d, &scratch_d[0]);
      scratch_d += lanesx2;
      prev_d = next_d;
    }

    if(i_d + 1 == dn)
    {
      // reflection at bottom
      prev_d = Load(d, &scratch_d[-1 * static_cast<int64_t>(lanes)]);
      auto curr_d = Load(d, &scratch_d[0]);
      Store(curr_d + (prev_d + prev_d) * s_d, d, &scratch_d[0]);
    }

    // low pass 1
    if(samples_no_end_bdry_s != 0)
    {
      // reflection at top
      prev_s = Load(d, &scratch_s[reflect_d]);
      auto curr_s = Load(d, &scratch_s[0]);
      auto next_s = Load(d, &scratch_s[lanes]);
      Store(curr_s + (prev_s + next_s) * s_s, d, &scratch_s[0]);
      scratch_s += lanesx2;
      prev_s = next_s;
      i_s++;
    }
    for(; i_s < samples_no_end_bdry_s; ++i_s)
    {
      auto curr_s = Load(d, &scratch_s[0]);
      auto next_s = Load(d, &scratch_s[lanes]);
      Store(curr_s + (prev_s + next_s) * s_s, d, &scratch_s[0]);
      scratch_s += lanesx2;
      prev_s = next_s;
    }
    if(i_s + 1 == sn)
    {
      // reflection at bottom
      prev_s = Load(d, &scratch_s[-1 * static_cast<int64_t>(lanes)]);
      auto curr_s = Load(d, &scratch_s[0]);
      Store(curr_s + (prev_s + prev_s) * s_s, d, &scratch_s[0]);
    }

    // high pass 2
    scratch_d = scratch + off_d * lanes;
    s_d = Set(d, gamma);
    const auto sf_d = Set(d, K);

    scratch_s = scratch + off_s * lanes;
    s_s = Set(d, delta);
    const auto sf_s = Set(d, invK);
    i_d = 0;
    if(samples_no_end_bdry_d > 0)
    {
      // reflection at top
      prev_d = Load(d, &scratch_d[reflect_s]);
      auto curr_d = Load(d, &scratch_d[0]);
      auto next_d = Load(d, &scratch_d[lanes]);
      Store((curr_d + (prev_d + next_d) * s_d) * sf_d, d, &scratch_d[0]);
      scratch_d += lanesx2;
      prev_d = next_d;
      i_d++;
    }
    for(; i_d < samples_no_end_bdry_d; ++i_d)
    {
      auto curr_d = Load(d, &scratch_d[0]);
      auto next_d = Load(d, &scratch_d[lanes]);
      Store((curr_d + (prev_d + next_d) * s_d) * sf_d, d, &scratch_d[0]);
      scratch_d += lanesx2;
      prev_d = next_d;
    }

    if(i_d + 1 == dn)
    {
      // reflection at bottom
      prev_d = Load(d, &scratch_d[-1 * static_cast<int64_t>(lanes)]);
      auto curr_d = Load(d, &scratch_d[0]);
      Store((curr_d + (prev_d + prev_d) * s_d) * sf_d, d, &scratch_d[0]);
    }

    // low pass 2
    i_s = 0;
    if(samples_no_end_bdry_s != 0)
    {
      // reflection at top
      prev_s = Load(d, &scratch_s[reflect_d]);
      auto curr_s = Load(d, &scratch_s[0]);
      auto next_s = Load(d, &scratch_s[lanes]);
      Store((curr_s + (prev_s + next_s) * s_s) * sf_s, d, &scratch_s[0]);
      scratch_s += lanesx2;
      prev_s = next_s;
      i_s++;
    }
    for(; i_s < samples_no_end_bdry_s; ++i_s)
    {
      auto curr_s = Load(d, &scratch_s[0]);
      auto next_s = Load(d, &scratch_s[lanes]);
      Store((curr_s + (prev_s + next_s) * s_s) * sf_s, d, &scratch_s[0]);
      scratch_s += lanesx2;
      prev_s = next_s;
    }

    if(i_s + 1 == sn)
    {
      // reflection at bottom
      prev_s = Load(d, &scratch_s[-1 * static_cast<int64_t>(lanes)]);
      auto curr_s = Load(d, &scratch_s[0]);
      Store((curr_s + (prev_s + prev_s) * s_s) * sf_s, d, &scratch_s[0]);
    }

    deinterleave_v(scratch, res, dn, sn, stride, parity, numcols);
  }

  /**************************************************************************
   *  16-bit 5/3 Forward DWT (Analysis)
   *
   *  Uses separated even/odd (E/O) layout in scratch for the lifting steps.
   *  Reads/writes int32_t tile buffer, performs lifting in int16_t.
   *
   *  Eligible when bit-depth + DWT headroom <= 16 (same criteria as inverse).
   *
   *  ITU-T T.800 Annex F.3.4 defines the reversible 5/3 lifting steps:
   *    D[n] -= floor((S[n] + S[n+1]) / 2)         (prediction)
   *    S[n] += floor((D[n-1] + D[n] + 2) / 4)     (update)
   *
   *  The update step uses an overflow-safe unsigned averaging operator
   *  rather than forming the full 17-bit sum D[n-1]+D[n].  The identity
   *
   *    floor((a + b + 2) / 4)  ==  floor(avg_round(a, b') / 2)
   *
   *  where avg_round(x, y) = floor((x + y + 1) / 2) uses the unsigned
   *  averaging instruction (PAVGW/URHADD) that never overflows.  The signed
   *  operands are mapped to unsigned via XOR 0x8000 and ADD 0x7FFF
   *  respectively, then the unsigned average is taken, and the result
   *  converted back to signed.  This technique is well-known in JPEG 2000
   *  implementations and used in OpenJPH's lifting engine for the same
   *  purpose.
   **************************************************************************/

  // Helper: narrow numcols int32 values to int16
  HWY_ATTR static void narrow_row(const int32_t* src, int16_t* dst, uint32_t numcols)
  {
    for(uint32_t j = 0; j < numcols; ++j)
      dst[j] = (int16_t)src[j];
  }

  // Helper: widen numcols int16 values to int32
  HWY_ATTR static void widen_row(const int16_t* src, int32_t* dst, uint32_t numcols)
  {
    for(uint32_t j = 0; j < numcols; ++j)
      dst[j] = (int32_t)src[j];
  }

  HWY_ATTR void encode_53_16_v(int32_t* resolution, int16_t* scratch, const uint32_t height,
                               const uint8_t parity, const uint32_t stride, const uint32_t numcols,
                               int32_t dcShift)
  {
    const HWY_FULL(int16_t) d16;
    const size_t lanes16 = Lanes(d16);

    if(height <= 1)
    {
      if(parity == 1 && height == 1)
      {
        for(uint32_t j = 0; j < numcols; ++j)
        {
          int16_t v = (int16_t)resolution[j];
          if(dcShift != 0)
            v = (int16_t)(v - (int16_t)dcShift);
          resolution[j] = (int32_t)(int16_t)(v << 1);
        }
      }
      return;
    }

    const uint32_t sn = (height + !parity) >> 1;
    const uint32_t dn = height - sn;

    int16_t* E = scratch;
    int16_t* O = scratch + sn * lanes16;

    // Separate even/odd rows into E[] and O[] (narrowing int32 → int16)
    for(uint32_t k = 0; k < sn; ++k)
      narrow_row(resolution + size_t(2 * k + parity) * stride, E + k * lanes16, numcols);
    for(uint32_t k = 0; k < dn; ++k)
      narrow_row(resolution + size_t(2 * k + !parity) * stride, O + k * lanes16, numcols);

    // Type tags for the signed↔unsigned averaging trick
    const HWY_FULL(uint16_t) du16;
    const auto i16_min = Set(d16, (int16_t)0x8000);
    const auto u16_max = Set(du16, (uint16_t)0x7FFF);

    // Prediction: O[k] -= (E_left + E_right) >> 1
    for(uint32_t k = 0; k < dn; ++k)
    {
      uint32_t el_idx, er_idx;
      if(parity == 0)
      {
        el_idx = k;
        er_idx = (k + 1 < sn) ? (k + 1) : k;
      }
      else
      {
        el_idx = (k > 0) ? (k - 1) : 0;
        er_idx = (k < sn) ? k : (sn > 0 ? sn - 1 : 0);
      }
      auto el = Load(d16, E + el_idx * lanes16);
      auto er = Load(d16, E + er_idx * lanes16);
      auto ok = Load(d16, O + k * lanes16);
      if(el_idx == er_idx)
        Store(ok - el, d16, O + k * lanes16);
      else
        Store(ok - ShiftRight<1>(el + er), d16, O + k * lanes16);
    }

    // Update: E[k] += (O_left + O_right + 2) >> 2
    // Uses overflow-safe unsigned averaging: computes floor((a+b+2)/4) as
    // floor(signed_avg_round(a,b) / 2), where signed_avg_round uses the
    // unsigned PAVGW/URHADD instruction on sign-flipped operands.
    //
    // Math: Let A = a XOR 0x8000 (unsigned), B = b + 0x7FFF (unsigned).
    //   avg_u = floor((A + B + 1) / 2)
    //         = floor((a + b + 0x10000) / 2)       (since A+B = a+b+0xFFFF)
    //         = floor((a + b) / 2) + 0x8000         (even a+b)
    //       or  floor((a + b + 1) / 2) + 0x8000     (odd a+b)
    //   result = (int16_t)(avg_u - 0x7FFF)          → (a+b+2)/2 with rounding
    //   result >> 1                                  → (a+b+2)/4  = (a+b+2)>>2
    for(uint32_t k = 0; k < sn; ++k)
    {
      uint32_t ol_idx, or_idx;
      if(parity == 0)
      {
        ol_idx = (k > 0) ? (k - 1) : 0;
        or_idx = (k < dn) ? k : (dn > 0 ? dn - 1 : 0);
      }
      else
      {
        ol_idx = k;
        or_idx = (k + 1 < dn) ? (k + 1) : k;
      }
      auto ol = Load(d16, O + ol_idx * lanes16);
      auto or_ = Load(d16, O + or_idx * lanes16);
      auto ek = Load(d16, E + k * lanes16);
      if(ol_idx == or_idx)
      {
        // Same neighbor: (2*ol + 2) >> 2 = (ol + 1) >> 1
        // = (ol >> 1) + (ol & 1) when ol >= 0, equivalently use avg trick
        auto a_u = BitCast(du16, Xor(ol, i16_min));
        auto avg = AverageRound(a_u, Add(BitCast(du16, ol), u16_max));
        auto update = ShiftRight<1>(BitCast(d16, Sub(avg, u16_max)));
        Store(ek + update, d16, E + k * lanes16);
      }
      else
      {
        // General case: (ol + or_ + 2) >> 2 via unsigned averaging
        auto a_u = BitCast(du16, Xor(ol, i16_min));
        auto b_u = Add(BitCast(du16, or_), u16_max);
        auto avg = AverageRound(a_u, b_u);
        auto update = ShiftRight<1>(BitCast(d16, Sub(avg, u16_max)));
        Store(ek + update, d16, E + k * lanes16);
      }
    }

    // Apply DC shift to low-pass (E) band
    if(dcShift != 0)
    {
      const auto vshift = Set(d16, (int16_t)dcShift);
      for(uint32_t k = 0; k < sn; ++k)
        Store(Load(d16, E + k * lanes16) - vshift, d16, E + k * lanes16);
    }

    // Deinterleave: write E[] → first sn rows, O[] → next dn rows (widening to int32)
    for(uint32_t k = 0; k < sn; ++k)
      widen_row(E + k * lanes16, resolution + k * stride, numcols);
    for(uint32_t k = 0; k < dn; ++k)
      widen_row(O + k * lanes16, resolution + (sn + k) * stride, numcols);
  }

  HWY_ATTR void encode_53_16_h(int32_t* resolution, int16_t* scratch, const uint32_t width,
                               const uint8_t parity, const uint32_t stride, const uint32_t numrows)
  {
    if(width <= 1)
    {
      if(parity == 1 && width == 1)
      {
        for(uint32_t r = 0; r < numrows; ++r)
          resolution[r * stride] <<= 1;
      }
      return;
    }

    const HWY_FULL(int16_t) d16;
    const size_t lanes16 = Lanes(d16);
    const uint32_t sn = (width + !parity) >> 1;
    const uint32_t dn = width - sn;

    for(uint32_t r = 0; r < numrows; ++r)
    {
      int32_t* row = resolution + r * stride;

      // Load row and narrow to int16
      int16_t* buf = scratch;
      narrow_row(row, buf, width);

      // Separate into E[0..sn-1] and O[0..dn-1]
      int16_t* E = buf + width;
      int16_t* O = E + sn;
      for(uint32_t k = 0; k < sn; ++k)
        E[k] = buf[2 * k + parity];
      for(uint32_t k = 0; k < dn; ++k)
        O[k] = buf[2 * k + !parity];

      // Prediction: O[k] -= (E_left + E_right) >> 1
      // SIMD bulk (all neighbors valid)
      {
        uint32_t no_bdry;
        if(parity == 0)
          no_bdry = std::min(dn, sn > 0 ? sn - 1 : 0u);
        else
          no_bdry = (sn > 0 && dn > 1) ? std::min(dn - 1, sn) : 0;

        if(parity == 0)
        {
          uint32_t k = 0;
          for(; k + lanes16 <= no_bdry; k += (uint32_t)lanes16)
          {
            auto ek = LoadU(d16, E + k);
            auto ek1 = LoadU(d16, E + k + 1);
            auto ok = LoadU(d16, O + k);
            StoreU(ok - ShiftRight<1>(ek + ek1), d16, O + k);
          }
          for(; k < dn; ++k)
          {
            int16_t el = E[k];
            int16_t er = (k + 1 < sn) ? E[k + 1] : E[k];
            O[k] = (int16_t)(O[k] - ((el == er) ? el : (int16_t)((el + er) >> 1)));
          }
        }
        else
        {
          // O[0] boundary
          if(dn > 0)
            O[0] = (int16_t)(O[0] - E[0]);
          uint32_t k = 1;
          for(; k + lanes16 <= no_bdry + 1; k += (uint32_t)lanes16)
          {
            auto ekm1 = LoadU(d16, E + k - 1);
            auto ek = LoadU(d16, E + k);
            auto ok = LoadU(d16, O + k);
            StoreU(ok - ShiftRight<1>(ekm1 + ek), d16, O + k);
          }
          for(; k < dn; ++k)
          {
            int16_t el = E[k - 1];
            int16_t er = (k < sn) ? E[k] : E[sn > 0 ? sn - 1 : 0];
            O[k] = (int16_t)(O[k] - ((el == er) ? el : (int16_t)((el + er) >> 1)));
          }
        }
      }

      // Update: E[k] += (O_left + O_right + 2) >> 2
      // Uses the same overflow-safe unsigned averaging as the vertical path.
      {
        const HWY_FULL(uint16_t) du16;
        const auto i16_min = Set(d16, (int16_t)0x8000);
        const auto u16_max = Set(du16, (uint16_t)0x7FFF);

        if(parity == 0)
        {
          // E[0] boundary: same neighbor → (2*O[0] + 2) >> 2
          if(sn > 0 && dn > 0)
          {
            auto a_u = BitCast(du16, Xor(Set(d16, O[0]), i16_min));
            auto avg_u = AverageRound(a_u, Add(BitCast(du16, Set(d16, O[0])), u16_max));
            int16_t upd = GetLane(ShiftRight<1>(BitCast(d16, Sub(avg_u, u16_max))));
            E[0] = (int16_t)(E[0] + upd);
          }
          uint32_t k = 1;
          uint32_t no_bdry = std::min(sn, dn);
          for(; k + lanes16 <= no_bdry; k += (uint32_t)lanes16)
          {
            auto okm1 = LoadU(d16, O + k - 1);
            auto ok = LoadU(d16, O + k);
            auto ek = LoadU(d16, E + k);
            auto a_u = BitCast(du16, Xor(okm1, i16_min));
            auto b_u = Add(BitCast(du16, ok), u16_max);
            auto avg = AverageRound(a_u, b_u);
            auto update = ShiftRight<1>(BitCast(d16, Sub(avg, u16_max)));
            StoreU(ek + update, d16, E + k);
          }
          for(; k < sn; ++k)
          {
            int16_t ol = O[k - 1];
            int16_t or_ = (k < dn) ? O[k] : O[dn > 0 ? dn - 1 : 0];
            E[k] = (int16_t)(E[k] + ((ol + or_ + 2) >> 2));
          }
        }
        else
        {
          uint32_t no_bdry = (sn > 0 && dn > 1) ? std::min(sn - 1, dn - 1) : 0;
          uint32_t k = 0;
          for(; k + lanes16 <= no_bdry; k += (uint32_t)lanes16)
          {
            auto ok = LoadU(d16, O + k);
            auto ok1 = LoadU(d16, O + k + 1);
            auto ek = LoadU(d16, E + k);
            auto a_u = BitCast(du16, Xor(ok, i16_min));
            auto b_u = Add(BitCast(du16, ok1), u16_max);
            auto avg = AverageRound(a_u, b_u);
            auto update = ShiftRight<1>(BitCast(d16, Sub(avg, u16_max)));
            StoreU(ek + update, d16, E + k);
          }
          for(; k < sn; ++k)
          {
            int16_t ol = O[k];
            int16_t or_ = (k + 1 < dn) ? O[k + 1] : O[k];
            E[k] = (int16_t)(E[k] + ((ol + or_ + 2) >> 2));
          }
        }
      }

      // Write back: E[] → row[0..sn-1], O[] → row[sn..sn+dn-1] (widened to int32)
      for(uint32_t k = 0; k < sn; ++k)
        row[k] = (int32_t)E[k];
      for(uint32_t k = 0; k < dn; ++k)
        row[sn + k] = (int32_t)O[k];
    }
  }

  bool encode_53_16(TileComponent* tilec, int32_t dcShift)
  {
    if(tilec->num_resolutions_ == 1U)
      return true;

    const HWY_FULL(int16_t) d16;
    const uint32_t lanes16 = uint32_t(Lanes(d16));

    uint32_t stride = tilec->getWindow()->getResWindowBufferHighestSimple().stride_;
    int32_t* tiledp = tilec->getWindow()->getResWindowBufferHighestSimple().buf_;

    const uint8_t maxNumResolutions = (uint8_t)(tilec->num_resolutions_ - 1);
    auto currentRes = tilec->resolutions_ + maxNumResolutions;
    auto lastRes = currentRes - 1;

    // Scratch: vertical needs height * lanes16 int16_t, horizontal needs (width + width) int16_t
    // Use the larger of the two
    size_t maxDim = max_resolution(tilec->resolutions_, tilec->num_resolutions_);
    size_t scratchElems = maxDim * lanes16; // vertical scratch
    size_t hScratch = maxDim * 3; // horizontal: buf + E + O
    if(hScratch > scratchElems)
      scratchElems = hScratch;

    const uint32_t num_threads = (uint32_t)TFSingleton::num_threads();
    int16_t* scratch_pool = nullptr;
    if(scratchElems)
    {
      scratch_pool = (int16_t*)grk_aligned_malloc(num_threads * scratchElems * sizeof(int16_t));
      if(!scratch_pool)
        return false;
    }

    int32_t i = maxNumResolutions;
    while(i--)
    {
      bool isFirstLevel = (i == maxNumResolutions - 1);
      int32_t currentDcShift = isFirstLevel ? dcShift : 0;

      const uint32_t rw = (uint32_t)(currentRes->x1 - currentRes->x0);
      const uint32_t rh = (uint32_t)(currentRes->y1 - currentRes->y0);

      const uint8_t parity_row = currentRes->x0 & 1;
      const uint8_t parity_col = currentRes->y0 & 1;

      // Vertical pass: process lanes16 columns at a time
      if(num_threads <= 1 || rw < (lanes16 << 1))
      {
        uint32_t j;
        for(j = 0; j + lanes16 - 1 < rw; j += lanes16)
          encode_53_16_v(tiledp + j, scratch_pool, rh, parity_col, stride, lanes16, currentDcShift);
        if(j < rw)
          encode_53_16_v(tiledp + j, scratch_pool, rh, parity_col, stride, rw - j, currentDcShift);
      }
      else
      {
        uint32_t cols_per_thread = (rw + num_threads - 1) / num_threads;
        uint32_t step_j = ((cols_per_thread + lanes16 - 1) / lanes16) * lanes16;
        uint32_t num_tasks = std::min((rw + step_j - 1) / step_j, num_threads);

        tf::Taskflow taskflow;
        auto nodes = std::make_unique<tf::Task[]>(num_tasks);
        for(uint32_t t = 0; t < num_tasks; t++)
          nodes[t] = taskflow.placeholder();

        for(uint32_t t = 0; t < num_tasks; t++)
        {
          uint32_t min_j = t * step_j;
          uint32_t max_j = (t + 1 == num_tasks) ? rw : (t + 1) * step_j;
          int16_t* scratch = scratch_pool + t * scratchElems;
          int32_t* td = tiledp;
          nodes[t].work([td, scratch, rh, parity_col, stride, lanes16, currentDcShift, min_j,
                         max_j] {
            uint32_t j;
            for(j = min_j; j + lanes16 - 1 < max_j; j += lanes16)
              encode_53_16_v(td + j, scratch, rh, parity_col, stride, lanes16, currentDcShift);
            if(j < max_j)
              encode_53_16_v(td + j, scratch, rh, parity_col, stride, max_j - j, currentDcShift);
          });
        }
        TFSingleton::get().run(taskflow).wait();
      }

      // Horizontal pass: process one row at a time (row-by-row)
      if(num_threads <= 1 || rh < 2)
      {
        for(uint32_t j = 0; j < rh; ++j)
          encode_53_16_h(tiledp + size_t(j) * stride, scratch_pool, rw, parity_row, stride, 1);
      }
      else
      {
        uint32_t num_tasks = std::min(num_threads, rh);
        uint32_t step_j = rh / num_tasks;
        tf::Taskflow taskflow;
        auto nodes = std::make_unique<tf::Task[]>(num_tasks);
        for(uint32_t t = 0; t < num_tasks; t++)
          nodes[t] = taskflow.placeholder();

        for(uint32_t t = 0; t < num_tasks; t++)
        {
          uint32_t min_j = t * step_j;
          uint32_t max_j = (t + 1 == num_tasks) ? rh : (t + 1) * step_j;
          int16_t* scratch = scratch_pool + t * scratchElems;
          int32_t* td = tiledp;
          nodes[t].work([td, scratch, rw, parity_row, stride, min_j, max_j] {
            for(uint32_t j = min_j; j < max_j; ++j)
              encode_53_16_h(td + size_t(j) * stride, scratch, rw, parity_row, stride, 1);
          });
        }
        TFSingleton::get().run(taskflow).wait();
      }

      currentRes = lastRes;
      --lastRes;
    }

    grk_aligned_free(scratch_pool);
    return true;
  }

  /**************************************************************************
   *  16-bit 9/7 Forward DWT (Analysis) — Q1.15 Fixed-Point
   *
   *  ITU-T T.800 Annex F.3.5 defines the irreversible 9/7 CDF analysis
   *  lifting steps:
   *    Step 0 (predict): D[n] += α × (S[n] + S[n+1])   α = -1.586134342
   *    Step 1 (update):  S[n] += β × (D[n-1] + D[n])   β = -0.052980118
   *    Step 2 (predict): D[n] += γ × (S[n] + S[n+1])   γ = +0.882911075
   *    Step 3 (update):  S[n] += δ × (D[n-1] + D[n])   δ = +0.443506852
   *    K scaling:        S[n] *= 1/K,  D[n] *= K/2      K = 1.230174105
   *
   *  Overflow prevention — odd-branch (high-pass) halving:
   *
   *  The large |α| coefficient amplifies the first lifting step output by
   *  up to 1 + 2|α| ≈ 4.17× the input.  Subsequent steps compound this,
   *  creating intermediates that can reach ~7.4× the input — which exceeds
   *  int16 range for precisions above ~8 bits.
   *
   *  The solution is to store the odd (high-pass) branch D at half magnitude
   *  throughout the lifting chain, denoted D' = D/2.  Each coefficient is
   *  adjusted to compensate:
   *
   *  Step 0 (α, output halved):
   *    D'[n] = ((D[n] + round(g × sum)) + 1) >> 1 − sum
   *    where g = 2 + α = 0.413865658 and sum = S[n] + S[n+1].
   *    This derives from:
   *      D'  = (D + α×sum) / 2
   *          = (D + (g−2)×sum) / 2
   *          = (D + g×sum)/2 − sum
   *    Since g < 1, MulFixedPoint15 applies directly.
   *
   *  Step 1 (β, source halved, uses averaging):
   *    S[n] += round(4β × avg(D'[n−1], D'[n]))
   *    The factor 4 compensates: ×2 for halved source, ×2 because avg
   *    replaces the full sum (avg ≈ (a+b)/2).  The unsigned averaging
   *    instruction (PAVGW/URHADD) avoids forming D'+D' which could overflow.
   *
   *  Step 2 (γ, output halved):
   *    D'[n] += round((γ/2) × (S[n] + S[n+1]))
   *    Halved coefficient because the result goes into the halved branch.
   *
   *  Step 3 (δ, source halved):
   *    S[n] += round(2δ × (D'[n−1] + D'[n]))
   *    ×2 compensates for halved source.  The sum of halved D' values is
   *    bounded and fits in int16.
   *
   *  K scaling:
   *    S[n] *= 1/K  (unchanged, 1/K < 1)
   *    D'[n] *= K   (since D = 2D', D×K/2 = D'×K, net result is identical)
   *    K > 1, so implemented as D' + MulFixedPoint15(D', K−1).
   *
   *  The BIBO gain of the 9/7 lowpass filter is ≈1.38 per level (ITU-T T.800
   *  Table F.3).  With odd-branch halving, the maximum intermediate per level
   *  is ~7.4× the input, giving safe headroom for prec + 6 ≤ 16 (prec ≤ 10)
   *  through at least 7 decomposition levels without additional scaling.
   **************************************************************************/

  // Forward 9/7 Q1.15 coefficients — odd-branch halved formulation (osc)
  // Used for vertical DWT and horizontal DWT on vertical-lowpass rows.
  //
  // Step 0: g = 2 + α = 0.413865658, used to halve the high-pass output.
  static const int16_t fwd97_g = (int16_t)(0.5 + 0.413865658 * (double)(1 << 15)); // g as Q1.15
  // Step 1: 4β = 4 × (−0.052980118) — coefficient for avg-based update
  static const int16_t fwd97_4beta = (int16_t)(-0.5 + 4.0 * (-0.052980118) * (double)(1 << 15));
  // Step 2: γ/2 = 0.441455537 — halved for output to halved branch
  static const int16_t fwd97_half_gamma = (int16_t)(0.5 + 0.882911075 / 2.0 * (double)(1 << 15));
  // Step 3: 2δ = 0.887013704 — doubled to compensate for halved source
  static const int16_t fwd97_2delta = (int16_t)(0.5 + 2.0 * 0.443506852 * (double)(1 << 15));
  // K scaling: 1/K = 0.812893066 (even branch), K−1 = 0.230174105 (odd frac)
  static const int16_t fwd97_invK = (int16_t)(0.5 + (1.0 / 1.230174105) * (double)(1 << 15));
  static const int16_t fwd97_K_frac = (int16_t)(0.5 + (1.230174105 - 1.0) * (double)(1 << 15));

  // Forward 9/7 Q1.15 coefficients — non-scaled formulation (nsc)
  // Used for horizontal DWT on vertical-highpass rows, where the input is
  // already at half magnitude from the vertical halving.  No additional
  // halving is applied to the horizontal odd branch, so all non-LL subbands have uniform ×2
  // compensation in the block coder.
  //
  // Step 0: (1+α) = -0.586134342 — additive decomposition without halving
  static const int16_t fwd97_nsc_0 = (int16_t)(-0.5 + (1.0 + (-1.586134342)) * (double)(1 << 15));
  // Step 1: 2β = 2 × (−0.052980118) — for avg-based update (D at full magnitude)
  static const int16_t fwd97_nsc_2beta = (int16_t)(-0.5 + 2.0 * (-0.052980118) * (double)(1 << 15));
  // Step 2: γ = 0.882911075 — full coefficient (D at full magnitude)
  static const int16_t fwd97_nsc_gamma = (int16_t)(0.5 + 0.882911075 * (double)(1 << 15));
  // Step 3: δ = 0.443506852 — full coefficient (D at full magnitude)
  static const int16_t fwd97_nsc_delta = (int16_t)(0.5 + 0.443506852 * (double)(1 << 15));

  /**
   * @brief Q1.15 fixed-point multiply (scalar fallback for forward 9/7)
   */
  static inline int16_t fwd_mf15(int16_t a, int16_t b)
  {
    return (int16_t)(((int32_t)a * (int32_t)b + (1 << 14)) >> 15);
  }

  HWY_ATTR void encode_97_16_v(int32_t* resolution, int16_t* scratch, const uint32_t height,
                               const uint8_t parity, const uint32_t stride, const uint32_t numcols,
                               float dcShift, bool intInput)
  {
    const HWY_FULL(int16_t) d16;
    const size_t lanes16 = Lanes(d16);

    if(height <= 1)
    {
      if(height == 1)
      {
        // Single row: apply K scaling.  Even parity (lowpass) → 1/K.
        // Odd parity (high-pass): D' = D/2, then D'×K.
        if(parity == 0)
        {
          if(intInput)
          {
            for(uint32_t j = 0; j < numcols; ++j)
            {
              int16_t v = (int16_t)((int32_t)resolution[j] - (int32_t)dcShift);
              resolution[j] = (int32_t)fwd_mf15(v, fwd97_invK);
            }
          }
          else
          {
            auto* fres = (float*)resolution;
            for(uint32_t j = 0; j < numcols; ++j)
            {
              int16_t v = (int16_t)(fres[j] - dcShift);
              resolution[j] = (int32_t)fwd_mf15(v, fwd97_invK);
            }
          }
        }
        else
        {
          // Odd parity: halve then multiply by K → net = K/2
          if(intInput)
          {
            for(uint32_t j = 0; j < numcols; ++j)
            {
              int16_t v = (int16_t)((int32_t)resolution[j] - (int32_t)dcShift);
              int16_t half = (int16_t)((v + 1) >> 1); // halve with rounding
              resolution[j] = (int32_t)(int16_t)(half + fwd_mf15(half, fwd97_K_frac));
            }
          }
          else
          {
            auto* fres = (float*)resolution;
            for(uint32_t j = 0; j < numcols; ++j)
            {
              int16_t v = (int16_t)(fres[j] - dcShift);
              int16_t half = (int16_t)((v + 1) >> 1);
              resolution[j] = (int32_t)(int16_t)(half + fwd_mf15(half, fwd97_K_frac));
            }
          }
        }
      }
      return;
    }

    const uint32_t sn = (height + !parity) >> 1;
    const uint32_t dn = height - sn;

    int16_t* E = scratch;
    int16_t* O = scratch + sn * lanes16;

    // Gather rows into E[]/O[] with int32/float → int16 narrowing + DC shift
    if(intInput)
    {
      int16_t dcShift16 = (int16_t)(int32_t)dcShift;
      for(uint32_t k = 0; k < sn; ++k)
      {
        auto* src = resolution + size_t(2 * k + parity) * stride;
        auto* dst = E + k * lanes16;
        for(uint32_t j = 0; j < numcols; ++j)
          dst[j] = (int16_t)(src[j] - dcShift16);
      }
      for(uint32_t k = 0; k < dn; ++k)
      {
        auto* src = resolution + size_t(2 * k + !parity) * stride;
        auto* dst = O + k * lanes16;
        for(uint32_t j = 0; j < numcols; ++j)
          dst[j] = (int16_t)(src[j] - dcShift16);
      }
    }
    else
    {
      for(uint32_t k = 0; k < sn; ++k)
      {
        auto* src = (float*)resolution + size_t(2 * k + parity) * stride;
        auto* dst = E + k * lanes16;
        for(uint32_t j = 0; j < numcols; ++j)
          dst[j] = (int16_t)(src[j] - dcShift);
      }
      for(uint32_t k = 0; k < dn; ++k)
      {
        auto* src = (float*)resolution + size_t(2 * k + !parity) * stride;
        auto* dst = O + k * lanes16;
        for(uint32_t j = 0; j < numcols; ++j)
          dst[j] = (int16_t)(src[j] - dcShift);
      }
    }

    const auto vg = Set(d16, fwd97_g);
    const auto v4beta = Set(d16, fwd97_4beta);
    const auto vhgamma = Set(d16, fwd97_half_gamma);
    const auto v2delta = Set(d16, fwd97_2delta);
    const auto vone = Set(d16, (int16_t)1);
    // For the unsigned averaging trick (step 1)
    const HWY_FULL(uint16_t) du16;
    const auto i16_min = Set(d16, (int16_t)0x8000);
    const auto u16_max = Set(du16, (uint16_t)0x7FFF);

    // Step 0: D' = ((D + round(g × sum)) + 1) >> 1 − sum
    // Halves the high-pass output.  g = 2+α = 0.414 < 1.
    for(uint32_t k = 0; k < dn; ++k)
    {
      uint32_t el = (parity == 0) ? k : ((k > 0) ? (k - 1) : 0);
      uint32_t er =
          (parity == 0) ? ((k + 1 < sn) ? (k + 1) : k) : ((k < sn) ? k : (sn > 0 ? sn - 1 : 0));
      auto sl = Load(d16, E + el * lanes16);
      auto sr = Load(d16, E + er * lanes16);
      auto ok = Load(d16, O + k * lanes16);
      auto sum = sl + sr;
      // D' = ((D + round(g×sum)) + 1) >> 1 − sum
      auto t = ok + MulFixedPoint15(sum, vg) + vone;
      Store(ShiftRight<1>(t) - sum, d16, O + k * lanes16);
    }

    // Step 1: S += round(4β × avg(D'_l, D'_r))
    // Unsigned averaging prevents overflow in the D'+D' sum.
    for(uint32_t k = 0; k < sn; ++k)
    {
      uint32_t ol = (parity == 0) ? ((k > 0) ? (k - 1) : 0) : k;
      uint32_t or_ =
          (parity == 0) ? ((k < dn) ? k : (dn > 0 ? dn - 1 : 0)) : ((k + 1 < dn) ? (k + 1) : k);
      auto dl = Load(d16, O + ol * lanes16);
      auto dr = Load(d16, O + or_ * lanes16);
      auto ek = Load(d16, E + k * lanes16);
      // Signed average via unsigned trick: XOR 0x8000, avg_epu16, subtract 0x7FFF
      auto a_u = BitCast(du16, Xor(dl, i16_min));
      auto b_u = Add(BitCast(du16, dr), u16_max);
      auto avg = BitCast(d16, Sub(AverageRound(a_u, b_u), u16_max));
      Store(ek + MulFixedPoint15(avg, v4beta), d16, E + k * lanes16);
    }

    // Step 2: D' += round((γ/2) × (S_l + S_r))
    for(uint32_t k = 0; k < dn; ++k)
    {
      uint32_t el = (parity == 0) ? k : ((k > 0) ? (k - 1) : 0);
      uint32_t er =
          (parity == 0) ? ((k + 1 < sn) ? (k + 1) : k) : ((k < sn) ? k : (sn > 0 ? sn - 1 : 0));
      auto sl = Load(d16, E + el * lanes16);
      auto sr = Load(d16, E + er * lanes16);
      auto ok = Load(d16, O + k * lanes16);
      Store(ok + MulFixedPoint15(sl + sr, vhgamma), d16, O + k * lanes16);
    }

    // Step 3: S += round(2δ × (D'_l + D'_r))
    for(uint32_t k = 0; k < sn; ++k)
    {
      uint32_t ol = (parity == 0) ? ((k > 0) ? (k - 1) : 0) : k;
      uint32_t or_ =
          (parity == 0) ? ((k < dn) ? k : (dn > 0 ? dn - 1 : 0)) : ((k + 1 < dn) ? (k + 1) : k);
      auto dl = Load(d16, O + ol * lanes16);
      auto dr = Load(d16, O + or_ * lanes16);
      auto ek = Load(d16, E + k * lanes16);
      Store(ek + MulFixedPoint15(dl + dr, v2delta), d16, E + k * lanes16);
    }

    // K scaling: E *= 1/K, D' *= K (where K > 1: D' + mf15(D', K−1))
    {
      const auto vInvK = Set(d16, fwd97_invK);
      const auto vKfrac = Set(d16, fwd97_K_frac);
      for(uint32_t k = 0; k < sn; ++k)
        Store(MulFixedPoint15(Load(d16, E + k * lanes16), vInvK), d16, E + k * lanes16);
      for(uint32_t k = 0; k < dn; ++k)
      {
        auto dk = Load(d16, O + k * lanes16);
        Store(dk + MulFixedPoint15(dk, vKfrac), d16, O + k * lanes16);
      }
    }

    // Deinterleave: E[] → first sn rows, O[] → next dn rows (widening to int32)
    for(uint32_t k = 0; k < sn; ++k)
      widen_row(E + k * lanes16, resolution + k * stride, numcols);
    for(uint32_t k = 0; k < dn; ++k)
      widen_row(O + k * lanes16, resolution + (sn + k) * stride, numcols);
  }

  HWY_ATTR void encode_97_16_h(int32_t* resolution, int16_t* scratch, const uint32_t width,
                               const uint8_t parity, const uint32_t stride, const uint32_t numrows)
  {
    if(width <= 1)
    {
      if(width == 1)
      {
        // Single sample: even → 1/K, odd → halve then ×K
        if(parity == 0)
        {
          for(uint32_t r = 0; r < numrows; ++r)
            resolution[r * stride] = (int32_t)fwd_mf15((int16_t)resolution[r * stride], fwd97_invK);
        }
        else
        {
          for(uint32_t r = 0; r < numrows; ++r)
          {
            int16_t v = (int16_t)resolution[r * stride];
            int16_t half = (int16_t)((v + 1) >> 1);
            resolution[r * stride] = (int32_t)(int16_t)(half + fwd_mf15(half, fwd97_K_frac));
          }
        }
      }
      return;
    }

    const HWY_FULL(int16_t) d16;
    const size_t lanes16 = Lanes(d16);
    const uint32_t sn = (width + !parity) >> 1;
    const uint32_t dn = width - sn;

    const auto vInvK = Set(d16, fwd97_invK);
    const auto vKfrac = Set(d16, fwd97_K_frac);

    for(uint32_t r = 0; r < numrows; ++r)
    {
      int32_t* row = resolution + r * stride;

      // Load and narrow to int16
      int16_t* buf = scratch;
      for(uint32_t j = 0; j < width; ++j)
        buf[j] = (int16_t)row[j];

      // Separate into E[0..sn-1] and O[0..dn-1]
      int16_t* E = buf + width;
      int16_t* O = E + sn;
      for(uint32_t k = 0; k < sn; ++k)
        E[k] = buf[2 * k + parity];
      for(uint32_t k = 0; k < dn; ++k)
        O[k] = buf[2 * k + !parity];

      // Step 0: D' = ((D + round(g×sum)) + 1) >> 1 − sum
      for(uint32_t k = 0; k < dn; ++k)
      {
        uint32_t el = (parity == 0) ? k : ((k > 0) ? (k - 1) : 0);
        uint32_t er =
            (parity == 0) ? ((k + 1 < sn) ? (k + 1) : k) : ((k < sn) ? k : (sn > 0 ? sn - 1 : 0));
        int32_t sum = (int32_t)E[el] + (int32_t)E[er];
        int16_t g_round = fwd_mf15((int16_t)sum, fwd97_g);
        int32_t t = (int32_t)O[k] + (int32_t)g_round + 1;
        O[k] = (int16_t)((t >> 1) - sum);
      }

      // Step 1: S += round(4β × avg(D'_l, D'_r))
      // Scalar: use int32 to compute signed average safely
      for(uint32_t k = 0; k < sn; ++k)
      {
        uint32_t ol = (parity == 0) ? ((k > 0) ? (k - 1) : 0) : k;
        uint32_t or_ =
            (parity == 0) ? ((k < dn) ? k : (dn > 0 ? dn - 1 : 0)) : ((k + 1 < dn) ? (k + 1) : k);
        // Signed average: (a + b + 1) >> 1 with rounding toward +inf for odd sums
        int32_t avg32 = ((int32_t)O[ol] + (int32_t)O[or_] + 1) >> 1;
        E[k] = (int16_t)((int32_t)E[k] + (int32_t)fwd_mf15((int16_t)avg32, fwd97_4beta));
      }

      // Step 2: D' += round((γ/2) × (S_l + S_r))
      for(uint32_t k = 0; k < dn; ++k)
      {
        uint32_t el = (parity == 0) ? k : ((k > 0) ? (k - 1) : 0);
        uint32_t er =
            (parity == 0) ? ((k + 1 < sn) ? (k + 1) : k) : ((k < sn) ? k : (sn > 0 ? sn - 1 : 0));
        O[k] =
            (int16_t)((int32_t)O[k] + (int32_t)fwd_mf15((int16_t)((int32_t)E[el] + (int32_t)E[er]),
                                                        fwd97_half_gamma));
      }

      // Step 3: S += round(2δ × (D'_l + D'_r))
      for(uint32_t k = 0; k < sn; ++k)
      {
        uint32_t ol = (parity == 0) ? ((k > 0) ? (k - 1) : 0) : k;
        uint32_t or_ =
            (parity == 0) ? ((k < dn) ? k : (dn > 0 ? dn - 1 : 0)) : ((k + 1 < dn) ? (k + 1) : k);
        E[k] =
            (int16_t)((int32_t)E[k] +
                      (int32_t)fwd_mf15((int16_t)((int32_t)O[ol] + (int32_t)O[or_]), fwd97_2delta));
      }

      // K scaling: E *= 1/K, D' *= K (K > 1: D' + mf15(D', K−1))
      {
        uint32_t k = 0;
        for(; k + lanes16 <= sn; k += (uint32_t)lanes16)
          StoreU(MulFixedPoint15(LoadU(d16, E + k), vInvK), d16, E + k);
        for(; k < sn; ++k)
          E[k] = fwd_mf15(E[k], fwd97_invK);

        k = 0;
        for(; k + lanes16 <= dn; k += (uint32_t)lanes16)
        {
          auto dk = LoadU(d16, O + k);
          StoreU(dk + MulFixedPoint15(dk, vKfrac), d16, O + k);
        }
        for(; k < dn; ++k)
          O[k] = (int16_t)((int32_t)O[k] + (int32_t)fwd_mf15(O[k], fwd97_K_frac));
      }

      // Write back: E[] → row[0..sn-1], O[] → row[sn..sn+dn-1]
      for(uint32_t k = 0; k < sn; ++k)
        row[k] = (int32_t)E[k];
      for(uint32_t k = 0; k < dn; ++k)
        row[sn + k] = (int32_t)O[k];
    }
  }

  // Non-halving horizontal 9/7 DWT for vertical highpass rows (nsc variant).
  // The input rows are already at half magnitude from the vertical halving.
  // This variant does NOT halve the horizontal odd branch.  All non-LL subbands then have uniform
  // ×2 compensation.
  HWY_ATTR void encode_97_16_h_nohalf(int32_t* resolution, int16_t* scratch, const uint32_t width,
                                      const uint8_t parity, const uint32_t stride,
                                      const uint32_t numrows)
  {
    if(width <= 1)
    {
      if(width == 1)
      {
        // Single sample: even → 1/K, odd → just ×K (no halving)
        if(parity == 0)
        {
          for(uint32_t r = 0; r < numrows; ++r)
            resolution[r * stride] = (int32_t)fwd_mf15((int16_t)resolution[r * stride], fwd97_invK);
        }
        else
        {
          for(uint32_t r = 0; r < numrows; ++r)
          {
            int16_t v = (int16_t)resolution[r * stride];
            resolution[r * stride] = (int32_t)(int16_t)(v + fwd_mf15(v, fwd97_K_frac));
          }
        }
      }
      return;
    }

    const HWY_FULL(int16_t) d16;
    const size_t lanes16 = Lanes(d16);
    const uint32_t sn = (width + !parity) >> 1;
    const uint32_t dn = width - sn;

    const auto vInvK = Set(d16, fwd97_invK);
    const auto vKfrac = Set(d16, fwd97_K_frac);

    for(uint32_t r = 0; r < numrows; ++r)
    {
      int32_t* row = resolution + r * stride;

      // Load and narrow to int16
      int16_t* buf = scratch;
      for(uint32_t j = 0; j < width; ++j)
        buf[j] = (int16_t)row[j];

      // Separate into E[0..sn-1] and O[0..dn-1]
      int16_t* E = buf + width;
      int16_t* O = E + sn;
      for(uint32_t k = 0; k < sn; ++k)
        E[k] = buf[2 * k + parity];
      for(uint32_t k = 0; k < dn; ++k)
        O[k] = buf[2 * k + !parity];

      // nsc Step 0: D = D - sum + round((1+α) × sum) where (1+α) = -0.586
      for(uint32_t k = 0; k < dn; ++k)
      {
        uint32_t el = (parity == 0) ? k : ((k > 0) ? (k - 1) : 0);
        uint32_t er =
            (parity == 0) ? ((k + 1 < sn) ? (k + 1) : k) : ((k < sn) ? k : (sn > 0 ? sn - 1 : 0));
        int16_t sum = (int16_t)((int32_t)E[el] + (int32_t)E[er]);
        O[k] = (int16_t)((int32_t)O[k] - (int32_t)sum + (int32_t)fwd_mf15(sum, fwd97_nsc_0));
      }

      // nsc Step 1: S += round(2β × avg(D_l, D_r))
      for(uint32_t k = 0; k < sn; ++k)
      {
        uint32_t ol = (parity == 0) ? ((k > 0) ? (k - 1) : 0) : k;
        uint32_t or_ =
            (parity == 0) ? ((k < dn) ? k : (dn > 0 ? dn - 1 : 0)) : ((k + 1 < dn) ? (k + 1) : k);
        int32_t avg32 = ((int32_t)O[ol] + (int32_t)O[or_] + 1) >> 1;
        E[k] = (int16_t)((int32_t)E[k] + (int32_t)fwd_mf15((int16_t)avg32, fwd97_nsc_2beta));
      }

      // nsc Step 2: D += round(γ × (S_l + S_r))
      for(uint32_t k = 0; k < dn; ++k)
      {
        uint32_t el = (parity == 0) ? k : ((k > 0) ? (k - 1) : 0);
        uint32_t er =
            (parity == 0) ? ((k + 1 < sn) ? (k + 1) : k) : ((k < sn) ? k : (sn > 0 ? sn - 1 : 0));
        O[k] =
            (int16_t)((int32_t)O[k] + (int32_t)fwd_mf15((int16_t)((int32_t)E[el] + (int32_t)E[er]),
                                                        fwd97_nsc_gamma));
      }

      // nsc Step 3: S += round(δ × (D_l + D_r))
      for(uint32_t k = 0; k < sn; ++k)
      {
        uint32_t ol = (parity == 0) ? ((k > 0) ? (k - 1) : 0) : k;
        uint32_t or_ =
            (parity == 0) ? ((k < dn) ? k : (dn > 0 ? dn - 1 : 0)) : ((k + 1 < dn) ? (k + 1) : k);
        E[k] =
            (int16_t)((int32_t)E[k] + (int32_t)fwd_mf15((int16_t)((int32_t)O[ol] + (int32_t)O[or_]),
                                                        fwd97_nsc_delta));
      }

      // K scaling: E *= 1/K, D *= K (same as halving variant)
      {
        uint32_t k = 0;
        for(; k + lanes16 <= sn; k += (uint32_t)lanes16)
          StoreU(MulFixedPoint15(LoadU(d16, E + k), vInvK), d16, E + k);
        for(; k < sn; ++k)
          E[k] = fwd_mf15(E[k], fwd97_invK);

        k = 0;
        for(; k + lanes16 <= dn; k += (uint32_t)lanes16)
        {
          auto dk = LoadU(d16, O + k);
          StoreU(dk + MulFixedPoint15(dk, vKfrac), d16, O + k);
        }
        for(; k < dn; ++k)
          O[k] = (int16_t)((int32_t)O[k] + (int32_t)fwd_mf15(O[k], fwd97_K_frac));
      }

      // Write back: E[] → row[0..sn-1], O[] → row[sn..sn+dn-1]
      for(uint32_t k = 0; k < sn; ++k)
        row[k] = (int32_t)E[k];
      for(uint32_t k = 0; k < dn; ++k)
        row[sn + k] = (int32_t)O[k];
    }
  }

  bool encode_97_16(TileComponent* tilec, float dcShift, bool intInput)
  {
    if(tilec->num_resolutions_ == 1U)
      return true;

    const HWY_FULL(int16_t) d16;
    const uint32_t lanes16 = uint32_t(Lanes(d16));

    uint32_t stride = tilec->getWindow()->getResWindowBufferHighestSimple().stride_;
    int32_t* tiledp = tilec->getWindow()->getResWindowBufferHighestSimple().buf_;

    const uint8_t maxNumResolutions = (uint8_t)(tilec->num_resolutions_ - 1);
    auto currentRes = tilec->resolutions_ + maxNumResolutions;
    auto lastRes = currentRes - 1;

    size_t maxDim = max_resolution(tilec->resolutions_, tilec->num_resolutions_);
    size_t scratchElems = maxDim * lanes16; // vertical scratch
    size_t hScratch = maxDim * 3; // horizontal: buf + E + O
    if(hScratch > scratchElems)
      scratchElems = hScratch;

    const uint32_t num_threads = (uint32_t)TFSingleton::num_threads();
    int16_t* scratch_pool = nullptr;
    if(scratchElems)
    {
      scratch_pool = (int16_t*)grk_aligned_malloc(num_threads * scratchElems * sizeof(int16_t));
      if(!scratch_pool)
        return false;
    }

    int32_t i = maxNumResolutions;
    while(i--)
    {
      bool isFirstLevel = (i == maxNumResolutions - 1);
      float currentDcShift = isFirstLevel ? dcShift : 0.0f;
      // After the first level, data is always int32_t (widened from int16_t),
      // so intInput must be true to avoid reading as float.
      bool currentIntInput = isFirstLevel ? intInput : true;

      const uint32_t rw = (uint32_t)(currentRes->x1 - currentRes->x0);
      const uint32_t rh = (uint32_t)(currentRes->y1 - currentRes->y0);

      const uint8_t parity_row = currentRes->x0 & 1;
      const uint8_t parity_col = currentRes->y0 & 1;

      // Vertical pass
      if(num_threads <= 1 || rw < (lanes16 << 1))
      {
        uint32_t j;
        for(j = 0; j + lanes16 - 1 < rw; j += lanes16)
          encode_97_16_v(tiledp + j, scratch_pool, rh, parity_col, stride, lanes16, currentDcShift,
                         currentIntInput);
        if(j < rw)
          encode_97_16_v(tiledp + j, scratch_pool, rh, parity_col, stride, rw - j, currentDcShift,
                         currentIntInput);
      }
      else
      {
        uint32_t cols_per_thread = (rw + num_threads - 1) / num_threads;
        uint32_t step_j = ((cols_per_thread + lanes16 - 1) / lanes16) * lanes16;
        uint32_t num_tasks = std::min((rw + step_j - 1) / step_j, num_threads);

        tf::Taskflow taskflow;
        auto nodes = std::make_unique<tf::Task[]>(num_tasks);
        for(uint32_t t = 0; t < num_tasks; t++)
          nodes[t] = taskflow.placeholder();

        for(uint32_t t = 0; t < num_tasks; t++)
        {
          uint32_t min_j = t * step_j;
          uint32_t max_j = (t + 1 == num_tasks) ? rw : (t + 1) * step_j;
          int16_t* scratch = scratch_pool + t * scratchElems;
          int32_t* td = tiledp;
          nodes[t].work([td, scratch, rh, parity_col, stride, lanes16, currentDcShift,
                         currentIntInput, min_j, max_j] {
            uint32_t j;
            for(j = min_j; j + lanes16 - 1 < max_j; j += lanes16)
              encode_97_16_v(td + j, scratch, rh, parity_col, stride, lanes16, currentDcShift,
                             currentIntInput);
            if(j < max_j)
              encode_97_16_v(td + j, scratch, rh, parity_col, stride, max_j - j, currentDcShift,
                             currentIntInput);
          });
        }
        TFSingleton::get().run(taskflow).wait();
      }

      // Horizontal pass (DC shift already applied in vertical)
      // After vertical deinterleave: rows [0, sn_v) are lowpass, rows [sn_v, rh) are highpass.
      // Use halving (osc) for lowpass rows → produces LL + HL (halved).
      // Use non-halving (nsc) for highpass rows → produces LH + HH (not additionally halved).
      const uint32_t sn_v = (rh + !parity_col) >> 1;
      if(num_threads <= 1 || rh < 2)
      {
        for(uint32_t j = 0; j < sn_v; ++j)
          encode_97_16_h(tiledp + size_t(j) * stride, scratch_pool, rw, parity_row, stride, 1);
        for(uint32_t j = sn_v; j < rh; ++j)
          encode_97_16_h_nohalf(tiledp + size_t(j) * stride, scratch_pool, rw, parity_row, stride,
                                1);
      }
      else
      {
        uint32_t num_tasks = std::min(num_threads, rh);
        uint32_t step_j = rh / num_tasks;
        tf::Taskflow taskflow;
        auto nodes = std::make_unique<tf::Task[]>(num_tasks);
        for(uint32_t t = 0; t < num_tasks; t++)
          nodes[t] = taskflow.placeholder();

        for(uint32_t t = 0; t < num_tasks; t++)
        {
          uint32_t min_j = t * step_j;
          uint32_t max_j = (t + 1 == num_tasks) ? rh : (t + 1) * step_j;
          int16_t* scratch = scratch_pool + t * scratchElems;
          int32_t* td = tiledp;
          nodes[t].work([td, scratch, rw, parity_row, stride, min_j, max_j, sn_v] {
            for(uint32_t j = min_j; j < max_j; ++j)
            {
              if(j < sn_v)
                encode_97_16_h(td + size_t(j) * stride, scratch, rw, parity_row, stride, 1);
              else
                encode_97_16_h_nohalf(td + size_t(j) * stride, scratch, rw, parity_row, stride, 1);
            }
          });
        }
        TFSingleton::get().run(taskflow).wait();
      }

      currentRes = lastRes;
      --lastRes;
    }

    grk_aligned_free(scratch_pool);
    return true;
  }

  template<typename T, typename DWT>
  void encode_h(encode_info<T, DWT>* task)
  {
    const HWY_FULL(T) d;
    const size_t lanes = Lanes(d);
    uint32_t j;
    size_t ind = task->min_j * task->stride;
    size_t inc = lanes * task->stride;
    for(j = task->min_j; j + lanes - 1 < task->max_j; j += (uint32_t)lanes, ind += inc)
      task->dwt.encode_h((T*)task->tiledp + ind, (T*)task->line.mem, task->r_dim, task->line.parity,
                         task->stride, (uint32_t)lanes, task->dcShift);
    if(j < task->max_j)
      task->dwt.encode_h((T*)task->tiledp + ind, (T*)task->line.mem, task->r_dim, task->line.parity,
                         task->stride, task->max_j - j, task->dcShift);
  }

  template<typename T, typename DWT>
  void encode_v(encode_info<T, DWT>* task)
  {
    const HWY_FULL(T) d;
    const size_t lanes = Lanes(d);
    uint32_t j;
    size_t ind = task->min_j;
    size_t inc = lanes;
    for(j = task->min_j; j + lanes - 1 < task->max_j; j += (uint32_t)lanes, ind += inc)
      task->dwt.encode_v((T*)task->tiledp + ind, (T*)task->line.mem, task->r_dim, task->line.parity,
                         task->stride, (uint32_t)lanes, task->dcShift, task->intInput);
    if(j < task->max_j)
      task->dwt.encode_v((T*)task->tiledp + ind, (T*)task->line.mem, task->r_dim, task->line.parity,
                         task->stride, task->max_j - j, task->dcShift, task->intInput);
  }

  template<typename T, typename DWT>
  bool encode(TileComponent* tilec, T dcShiftVal, bool intInput = false)
  {
    if(tilec->num_resolutions_ == 1U)
      return true;
    const HWY_FULL(float) d;
    const uint32_t lanes = uint32_t(Lanes(d));

    uint32_t stride = tilec->getWindow()->getResWindowBufferHighestSimple().stride_;
    T* tiledp = (T*)tilec->getWindow()->getResWindowBufferHighestSimple().buf_;

    const uint8_t maxNumResolutions = (uint8_t)(tilec->num_resolutions_ - 1);
    auto currentRes = tilec->resolutions_ + maxNumResolutions;
    auto lastRes = currentRes - 1;

    size_t dataSize = max_resolution(tilec->resolutions_, tilec->num_resolutions_);
    const size_t thick = lanes;
    dataSize *= thick * sizeof(int32_t);
    int32_t i = maxNumResolutions;
    const uint32_t num_threads = (uint32_t)TFSingleton::num_threads();
    T* scratch_pool = nullptr;
    if(dataSize)
    {
      scratch_pool = (T*)grk_aligned_malloc(num_threads * dataSize);
      // dataSize is equal to 0 when numresolutions == 1 but scratch is not used
      // in that case, so do not error out
      if(!scratch_pool)
        return false;
    }
    DWT dwt;
    auto infoArray = std::make_unique<encode_info<T, DWT>[]>(num_threads);
    for(auto j = 0U; j < num_threads; ++j)
    {
      auto info = &infoArray[j];
      info->line.mem = scratch_pool + (j * dataSize) / sizeof(T);
      info->stride = stride;
      info->tiledp = tiledp;
    }
    while(i--)
    {
      // DC shift only on first (finest) resolution level
      bool isFirstLevel = (i == maxNumResolutions - 1);
      T currentDcShift = isFirstLevel ? dcShiftVal : T(0);
      // For integer types (e.g. dwt97_16), after the first level the data is
      // always int32_t (widened from int16_t), so intInput must stay true.
      bool currentIntInput;
      if constexpr(std::is_floating_point_v<T>)
        currentIntInput = isFirstLevel && intInput;
      else
        currentIntInput = isFirstLevel ? intInput : true;

      // width of the resolution level computed
      const uint32_t rw = (uint32_t)(currentRes->x1 - currentRes->x0);
      // height of the resolution level computed
      const uint32_t rh = (uint32_t)(currentRes->y1 - currentRes->y0);
      // width of the resolution level once lower than computed one
      const uint32_t rw_lower = (uint32_t)(lastRes->x1 - lastRes->x0);
      // height of the resolution level once lower than computed one
      const uint32_t rh_lower = (uint32_t)(lastRes->y1 - lastRes->y0);

      const uint8_t parity_row = currentRes->x0 & 1;
      const uint8_t parity_col = currentRes->y0 & 1;

      uint32_t sn = rh_lower;
      uint32_t dn = rh - rh_lower;
      if(num_threads <= 1 || rw < (lanes << 1))
      {
        uint32_t j;
        for(j = 0; j + lanes - 1 < rw; j += lanes)
          dwt.encode_v((T*)tiledp + j, scratch_pool, rh, parity_col, stride, lanes, currentDcShift,
                       currentIntInput);
        if(j < rw)
          dwt.encode_v((T*)tiledp + j, scratch_pool, rh, parity_col, stride, rw - j, currentDcShift,
                       currentIntInput);
      }
      else
      {
        uint32_t num_tasks = num_threads;
        if(rw < num_tasks)
          num_tasks = rw;
        uint32_t step_j = ((rw / num_tasks) / lanes) * lanes;
        tf::Taskflow taskflow;
        std::unique_ptr<tf::Task[]> node;
        if(num_tasks > 1)
        {
          node = std::make_unique<tf::Task[]>(num_tasks);
          for(uint64_t j = 0; j < num_tasks; j++)
            node[j] = taskflow.placeholder();
        }
        for(auto j = 0U; j < num_tasks; j++)
        {
          auto info = &infoArray[j];
          info->line.dn = dn;
          info->line.sn = sn;
          info->line.parity = parity_col;
          info->r_dim = rh;
          info->min_j = j * step_j;
          info->max_j = (j + 1 == num_tasks) ? rw : (j + 1) * step_j;
          info->dcShift = currentDcShift;
          info->intInput = currentIntInput;
          if(node)
          {
            node[j].work([info] {
              encode_v(info);
              return 0;
            });
          }
          else
          {
            encode_v(info);
          }
        }
        if(node)
          TFSingleton::get().run(taskflow).wait();
      }

      // DC shift applied only during vertical pass for first level;
      // horizontal pass processes already-shifted data
      sn = rw_lower;
      dn = (uint32_t)(rw - rw_lower);
      if(num_threads <= 1 || rh < (lanes << 1))
      {
        uint32_t j;
        for(j = 0; j + lanes - 1 < rh; j += lanes)
          dwt.encode_h((T*)tiledp + size_t(j) * stride, scratch_pool, rw, parity_row, stride,
                       lanes);
        if(j < rh)
          dwt.encode_h((T*)tiledp + size_t(j) * stride, scratch_pool, rw, parity_row, stride,
                       rh - j);
      }
      else
      {
        uint32_t num_tasks = num_threads;
        if(rh < num_tasks)
          num_tasks = rh;
        uint32_t step_j = ((rh / num_tasks) / lanes) * lanes;
        tf::Taskflow taskflow;
        std::unique_ptr<tf::Task[]> node;
        if(num_tasks > 1)
        {
          node = std::make_unique<tf::Task[]>(num_tasks);
          for(uint64_t j = 0; j < num_tasks; j++)
            node[j] = taskflow.placeholder();
        }
        for(auto j = 0U; j < num_tasks; j++)
        {
          auto info = &infoArray[j];
          info->line.dn = dn;
          info->line.sn = sn;
          info->line.parity = parity_row;
          info->r_dim = rw;
          info->min_j = j * step_j;
          info->max_j = (j + 1 == num_tasks) ? rh : (j + 1U) * step_j;
          info->dcShift = T(0);
          if(node)
            node[j].work([info] { encode_h(info); });
          else
            encode_h(info);
        }
        if(node)
          TFSingleton::get().run(taskflow).wait();
      }
      currentRes = lastRes;
      --lastRes;
    }

    grk_aligned_free(scratch_pool);
    return true;
  }

  bool encode_53(TileComponent* tilec, int32_t dcShift)
  {
    return encode<int32_t, dwt53>(tilec, dcShift, false);
  }
  bool encode_97(TileComponent* tilec, float dcShift, bool intInput)
  {
    return encode<float, dwt97>(tilec, dcShift, intInput);
  }

  // Holds per-thread encode_info arrays for each (vert, horiz) pass at each level,
  // plus the shared scratch pool. Must outlive the DAG execution.
  template<typename T, typename DWT>
  struct WaveletFwdScheduleDataImpl : public WaveletFwdScheduleData
  {
    T* scratch_pool = nullptr;
    struct LevelPass
    {
      std::unique_ptr<encode_info<T, DWT>[]> infos;
    };
    std::vector<LevelPass> passes;
    ~WaveletFwdScheduleDataImpl()
    {
      if(scratch_pool)
        grk_aligned_free(scratch_pool);
    }
  };

  // Schedule forward DWT into FlowComponent pairs (vert, horiz) per level.
  // Instead of run().wait(), tasks are added to the FlowComponents.
  template<typename T, typename DWT>
  std::unique_ptr<WaveletFwdScheduleData>
      schedule_encode(TileComponent* tilec, T dcShiftVal,
                      std::vector<std::pair<FlowComponent*, FlowComponent*>>& levelFlows,
                      bool intInput = false)
  {
    if(tilec->num_resolutions_ == 1U)
      return nullptr;
    const HWY_FULL(float) d;
    const uint32_t lanes = uint32_t(Lanes(d));

    uint32_t stride = tilec->getWindow()->getResWindowBufferHighestSimple().stride_;
    T* tiledp = (T*)tilec->getWindow()->getResWindowBufferHighestSimple().buf_;

    const uint8_t maxNumResolutions = (uint8_t)(tilec->num_resolutions_ - 1);
    auto currentRes = tilec->resolutions_ + maxNumResolutions;
    auto lastRes = currentRes - 1;

    size_t dataSize = max_resolution(tilec->resolutions_, tilec->num_resolutions_);
    const size_t thick = lanes;
    dataSize *= thick * sizeof(int32_t);
    int32_t i = maxNumResolutions;
    const uint32_t num_threads = (uint32_t)TFSingleton::num_threads();

    auto data = std::make_unique<WaveletFwdScheduleDataImpl<T, DWT>>();

    if(dataSize)
    {
      data->scratch_pool = (T*)grk_aligned_malloc(num_threads * dataSize);
      if(!data->scratch_pool)
        return nullptr;
    }

    uint8_t levelIdx = 0;
    while(i--)
    {
      // DC shift only on first (finest) resolution level
      bool isFirstLevel = (i == maxNumResolutions - 1);
      T currentDcShift = isFirstLevel ? dcShiftVal : T(0);
      // For integer types (e.g. dwt97_16), after the first level the data is
      // always int32_t (widened from int16_t), so intInput must stay true.
      bool currentIntInput;
      if constexpr(std::is_floating_point_v<T>)
        currentIntInput = isFirstLevel && intInput;
      else
        currentIntInput = isFirstLevel ? intInput : true;

      // width of the resolution level computed
      const uint32_t rw = (uint32_t)(currentRes->x1 - currentRes->x0);
      // height of the resolution level computed
      const uint32_t rh = (uint32_t)(currentRes->y1 - currentRes->y0);
      // width of the resolution level once lower than computed one
      const uint32_t rw_lower = (uint32_t)(lastRes->x1 - lastRes->x0);
      // height of the resolution level once lower than computed one
      const uint32_t rh_lower = (uint32_t)(lastRes->y1 - lastRes->y0);

      const uint8_t parity_row = currentRes->x0 & 1;
      const uint8_t parity_col = currentRes->y0 & 1;

      auto vertFlow = levelFlows[levelIdx].first;
      auto horizFlow = levelFlows[levelIdx].second;

      // --- Vertical pass ---
      {
        uint32_t sn = rh_lower;
        uint32_t dn = rh - rh_lower;
        if(num_threads <= 1 || rw < (lanes << 1))
        {
          T* scratch = data->scratch_pool;
          vertFlow->nextTask().work([tiledp, scratch, rw, rh, parity_col, stride, currentDcShift,
                                     currentIntInput, lanes] {
            DWT dwt;
            uint32_t j;
            for(j = 0; j + lanes - 1 < rw; j += lanes)
              dwt.encode_v((T*)tiledp + j, scratch, rh, parity_col, stride, lanes, currentDcShift,
                           currentIntInput);
            if(j < rw)
              dwt.encode_v((T*)tiledp + j, scratch, rh, parity_col, stride, rw - j, currentDcShift,
                           currentIntInput);
          });
        }
        else
        {
          uint32_t num_tasks = num_threads;
          if(rw < num_tasks)
            num_tasks = rw;
          uint32_t step_j = ((rw / num_tasks) / lanes) * lanes;

          typename WaveletFwdScheduleDataImpl<T, DWT>::LevelPass pass;
          pass.infos = std::make_unique<encode_info<T, DWT>[]>(num_tasks);
          for(auto j = 0U; j < num_tasks; j++)
          {
            auto info = &pass.infos[j];
            info->line.mem = data->scratch_pool + (j * dataSize) / sizeof(T);
            info->stride = stride;
            info->tiledp = tiledp;
            info->line.dn = dn;
            info->line.sn = sn;
            info->line.parity = parity_col;
            info->r_dim = rh;
            info->min_j = j * step_j;
            info->max_j = (j + 1 == num_tasks) ? rw : (j + 1) * step_j;
            info->dcShift = currentDcShift;
            info->intInput = currentIntInput;
            vertFlow->nextTask().work([info] { encode_v(info); });
          }
          data->passes.push_back(std::move(pass));
        }
      }

      // --- Horizontal pass ---
      // DC shift applied only during vertical pass for first level;
      // horizontal pass processes already-shifted data
      {
        uint32_t sn = rw_lower;
        uint32_t dn = (uint32_t)(rw - rw_lower);
        if(num_threads <= 1 || rh < (lanes << 1))
        {
          T* scratch = data->scratch_pool;
          horizFlow->nextTask().work([tiledp, scratch, rw, rh, parity_row, stride, lanes] {
            DWT dwt;
            uint32_t j;
            for(j = 0; j + lanes - 1 < rh; j += lanes)
              dwt.encode_h((T*)tiledp + size_t(j) * stride, scratch, rw, parity_row, stride, lanes);
            if(j < rh)
              dwt.encode_h((T*)tiledp + size_t(j) * stride, scratch, rw, parity_row, stride,
                           rh - j);
          });
        }
        else
        {
          uint32_t num_tasks = num_threads;
          if(rh < num_tasks)
            num_tasks = rh;
          uint32_t step_j = ((rh / num_tasks) / lanes) * lanes;

          typename WaveletFwdScheduleDataImpl<T, DWT>::LevelPass pass;
          pass.infos = std::make_unique<encode_info<T, DWT>[]>(num_tasks);
          for(auto j = 0U; j < num_tasks; j++)
          {
            auto info = &pass.infos[j];
            info->line.mem = data->scratch_pool + (j * dataSize) / sizeof(T);
            info->stride = stride;
            info->tiledp = tiledp;
            info->line.dn = dn;
            info->line.sn = sn;
            info->line.parity = parity_row;
            info->r_dim = rw;
            info->min_j = j * step_j;
            info->max_j = (j + 1 == num_tasks) ? rh : (j + 1U) * step_j;
            info->dcShift = T(0);
            horizFlow->nextTask().work([info] { encode_h(info); });
          }
          data->passes.push_back(std::move(pass));
        }
      }

      currentRes = lastRes;
      --lastRes;
      levelIdx++;
    }

    return data;
  }

  std::unique_ptr<WaveletFwdScheduleData>
      schedule_encode_53(TileComponent* tilec, int32_t dcShift,
                         std::vector<std::pair<FlowComponent*, FlowComponent*>>& levelFlows)
  {
    return schedule_encode<int32_t, dwt53>(tilec, dcShift, levelFlows, false);
  }
  std::unique_ptr<WaveletFwdScheduleData>
      schedule_encode_97(TileComponent* tilec, float dcShift,
                         std::vector<std::pair<FlowComponent*, FlowComponent*>>& levelFlows,
                         bool intInput)
  {
    return schedule_encode<float, dwt97>(tilec, dcShift, levelFlows, intInput);
  }
  std::unique_ptr<WaveletFwdScheduleData>
      schedule_encode_97_16(TileComponent* tilec, int32_t dcShift,
                            std::vector<std::pair<FlowComponent*, FlowComponent*>>& levelFlows,
                            bool intInput)
  {
    return schedule_encode<int32_t, dwt97_16>(tilec, dcShift, levelFlows, intInput);
  }

} // namespace HWY_NAMESPACE
} // namespace grk
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

#include "WaveletFwd.h"

namespace grk
{
HWY_EXPORT(encode_53_v);
HWY_EXPORT(encode_53_h);
HWY_EXPORT(encode_97_v);
HWY_EXPORT(encode_97_h);
HWY_EXPORT(encode_97_16_v);
HWY_EXPORT(encode_97_16_h);
HWY_EXPORT(encode_53);
HWY_EXPORT(encode_97);
HWY_EXPORT(encode_53_16);
HWY_EXPORT(encode_97_16);
HWY_EXPORT(schedule_encode_53);
HWY_EXPORT(schedule_encode_97);
HWY_EXPORT(schedule_encode_97_16);

template<typename T>
struct dwt_line
{
  T* mem;
  uint32_t dn; /* number of elements in high pass band */
  uint32_t sn; /* number of elements in low pass band */
  uint8_t parity; /* 0 = start on even coord, 1 = start on odd coord */
};

template<typename T, typename DWT>
struct encode_info
{
  dwt_line<T> line;
  uint32_t r_dim; // width or height of the resolution to process
  uint32_t stride; // stride  of tiledp
  T* tiledp;
  uint32_t min_j;
  uint32_t max_j;
  T dcShift;
  bool intInput = false; // true when tile buffer contains int32_t (9/7 first level)
  DWT dwt;
};

bool WaveletFwdImpl::compress(TileComponent* tile_comp, uint8_t qmfbid, DcShiftParam dcShift,
                              bool intInput)
{
  if(qmfbid == 1)
  {
    if(tile_comp->is16BitDwt())
      return HWY_DYNAMIC_DISPATCH(encode_53_16)(tile_comp, dcShift.enabled ? dcShift.shift : 0);
    return HWY_DYNAMIC_DISPATCH(encode_53)(tile_comp, dcShift.enabled ? dcShift.shift : 0);
  }
  else
  {
    if(tile_comp->is16BitDwt())
      return HWY_DYNAMIC_DISPATCH(encode_97_16)(
          tile_comp, dcShift.enabled ? (float)dcShift.shift : 0.0f, intInput);
    return HWY_DYNAMIC_DISPATCH(encode_97)(tile_comp, dcShift.enabled ? (float)dcShift.shift : 0.0f,
                                           intInput);
  }
}
std::unique_ptr<WaveletFwdScheduleData> WaveletFwdImpl::scheduleCompress(
    TileComponent* tile_comp, uint8_t qmfbid, DcShiftParam dcShift,
    std::vector<std::pair<FlowComponent*, FlowComponent*>>& levelFlows, bool intInput)
{
  if(qmfbid == 1)
    return HWY_DYNAMIC_DISPATCH(schedule_encode_53)(tile_comp, dcShift.enabled ? dcShift.shift : 0,
                                                    levelFlows);
  else if(tile_comp->is16BitDwt())
    return HWY_DYNAMIC_DISPATCH(schedule_encode_97_16)(
        tile_comp, dcShift.enabled ? dcShift.shift : 0, levelFlows, intInput);
  else
    return HWY_DYNAMIC_DISPATCH(schedule_encode_97)(
        tile_comp, dcShift.enabled ? (float)dcShift.shift : 0.0f, levelFlows, intInput);
}
void dwt53::encode_v(int32_t* res, int32_t* scratch, const uint32_t height, const uint8_t parity,
                     const uint32_t stride, const uint32_t numcols, int32_t dcShift,
                     [[maybe_unused]] bool intInput)
{
  HWY_DYNAMIC_DISPATCH(encode_53_v)(res, scratch, height, parity, stride, numcols, dcShift);
}
void dwt53::encode_h(int32_t* res, int32_t* scratch, const uint32_t width, const uint8_t parity,
                     const uint32_t stride, const uint32_t numrows, int32_t dcShift)
{
  HWY_DYNAMIC_DISPATCH(encode_53_h)(res, scratch, width, parity, stride, numrows, dcShift);
}
void dwt97::encode_v(float* res, float* scratch, const uint32_t height, const uint8_t parity,
                     const uint32_t stride, const uint32_t numcols, float dcShift, bool intInput)
{
  HWY_DYNAMIC_DISPATCH(encode_97_v)(res, scratch, height, parity, stride, numcols, dcShift,
                                    intInput);
}
void dwt97::encode_h(float* res, float* scratch, const uint32_t width, const uint8_t parity,
                     const uint32_t stride, const uint32_t numrows, float dcShift)
{
  HWY_DYNAMIC_DISPATCH(encode_97_h)(res, scratch, width, parity, stride, numrows, dcShift);
}
void dwt53_16::encode_v(int16_t*, int16_t*, uint32_t, uint8_t, uint32_t, uint32_t, int16_t, bool)
{
  // 16-bit forward DWT uses encode_53_16() which operates directly on int32 tile buffer.
  // These stubs exist only to satisfy the class declaration.
}
void dwt53_16::encode_h(int16_t*, int16_t*, uint32_t, uint8_t, uint32_t, uint32_t, int16_t)
{
  // 16-bit forward DWT uses encode_53_16() which operates directly on int32 tile buffer.
  // These stubs exist only to satisfy the class declaration.
}
void dwt97_16::encode_v(int32_t* res, int32_t* scratch, const uint32_t height, const uint8_t parity,
                        const uint32_t stride, const uint32_t numcols, int32_t dcShift,
                        bool intInput)
{
  HWY_DYNAMIC_DISPATCH(encode_97_16_v)(res, (int16_t*)scratch, height, parity, stride, numcols,
                                       (float)dcShift, intInput);
}
void dwt97_16::encode_h(int32_t* res, int32_t* scratch, const uint32_t width, const uint8_t parity,
                        const uint32_t stride, const uint32_t numrows, int32_t)
{
  // DC shift is only applied in the vertical pass (first level), so the dcShift parameter is unused
  HWY_DYNAMIC_DISPATCH(encode_97_16_h)(res, (int16_t*)scratch, width, parity, stride, numrows);
}

} // namespace grk
#endif
