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

#include "grk_taskflow.h"
#include "grk_restrict.h"
#include "simd.h"
#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "grk_includes.h"
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

#include "CodecScheduler.h"
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
                            const uint8_t parity, const uint32_t stride, const uint32_t numcols)
  {
    const HWY_FULL(int32_t) d;
    if(height <= 1)
    {
      if(parity == 1 && height == 1)
      {
        auto v = Load(d, resolution);
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

      deinterleave_v(scratch, resolution, dn, sn, stride, parity, numcols);
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

    deinterleave_v(scratch, resolution, dn, sn, stride, parity, numcols);
  }

  HWY_ATTR void encode_53_h(int32_t* resolution, int32_t* scratch, const uint32_t width,
                            const uint8_t parity, const uint32_t stride, const uint32_t numrows)
  {
    const HWY_FULL(int32_t) d;
    const auto indices = Iota(d, 0) * Set(d, static_cast<int32_t>(stride));
    if(width <= 1)
    {
      if(parity == 1 && width == 1)
      {
        const auto v = GatherIndexN(d, resolution, indices, numrows);
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

      deinterleave_h(scratch, resolution, dn, sn, stride, parity, numrows);
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

    deinterleave_h(scratch, resolution, dn, sn, stride, parity, numrows);
  }

  void encode_97_h(float* res, float* scratch, const uint32_t width, const uint8_t parity,
                   const uint32_t stride, const uint32_t numrows)
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
    for(auto j = 0U; j < width; ++j)
    {
      auto ind = indices + Set(di, static_cast<int32_t>(j));
      Store(GatherIndexN(d, res, ind, numrows), d, temp);
      temp += lanes;
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
                   const uint32_t stride, const uint32_t numcols)
  {
    const HWY_FULL(float) d;
    const size_t lanes = Lanes(d);
    const auto lanesx2 = (lanes << 1);
    const uint32_t sn = (height + !parity) >> 1;
    const uint32_t dn = height - sn;
    if(height <= 1)
      return;

    auto temp = scratch;
    auto in = res;
    for(auto j = 0U; j < height; ++j)
    {
      // Store the full SIMD vector, even if numcols < lanes
      Store(Load(d, in), d, temp);
      temp += lanes;
      in += stride;
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
                         task->stride, (uint32_t)lanes);
    if(j < task->max_j)
      task->dwt.encode_h((T*)task->tiledp + ind, (T*)task->line.mem, task->r_dim, task->line.parity,
                         task->stride, task->max_j - j);
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
                         task->stride, (uint32_t)lanes);
    if(j < task->max_j)
      task->dwt.encode_v((T*)task->tiledp + ind, (T*)task->line.mem, task->r_dim, task->line.parity,
                         task->stride, task->max_j - j);
  }

  template<typename T, typename DWT>
  bool encode(TileComponent* tilec)
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
    const uint32_t num_threads = (uint32_t)ExecSingleton::num_threads();
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
          dwt.encode_v((T*)tiledp + j, scratch_pool, rh, parity_col, stride, lanes);
        if(j < rw)
          dwt.encode_v((T*)tiledp + j, scratch_pool, rh, parity_col, stride, rw - j);
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
          ExecSingleton::get().run(taskflow).wait();
      }

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
          if(node)
            node[j].work([info] { encode_h(info); });
          else
            encode_h(info);
        }
        if(node)
          ExecSingleton::get().run(taskflow).wait();
      }
      currentRes = lastRes;
      --lastRes;
    }

    grk_aligned_free(scratch_pool);
    return true;
  }

  bool encode_53(TileComponent* tilec)
  {
    return encode<int32_t, dwt53>(tilec);
  }
  bool encode_97(TileComponent* tilec)
  {
    return encode<float, dwt97>(tilec);
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
HWY_EXPORT(encode_53);
HWY_EXPORT(encode_97);

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
  DWT dwt;
};

bool WaveletFwdImpl::compress(TileComponent* tile_comp, uint8_t qmfbid, uint32_t maxDim)
{
  WaveletReverse::allocPoolData(maxDim);

  return (qmfbid == 1) ? HWY_DYNAMIC_DISPATCH(encode_53)(tile_comp)
                       : HWY_DYNAMIC_DISPATCH(encode_97)(tile_comp);
}
void dwt53::encode_v(int32_t* res, int32_t* scratch, const uint32_t height, const uint8_t parity,
                     const uint32_t stride, const uint32_t numcols)
{
  HWY_DYNAMIC_DISPATCH(encode_53_v)(res, scratch, height, parity, stride, numcols);
}
void dwt53::encode_h(int32_t* res, int32_t* scratch, const uint32_t width, const uint8_t parity,
                     const uint32_t stride, const uint32_t numrows)
{
  HWY_DYNAMIC_DISPATCH(encode_53_h)(res, scratch, width, parity, stride, numrows);
}
void dwt97::encode_v(float* res, float* scratch, const uint32_t height, const uint8_t parity,
                     const uint32_t stride, const uint32_t numcols)
{
  HWY_DYNAMIC_DISPATCH(encode_97_v)(res, scratch, height, parity, stride, numcols);
}
void dwt97::encode_h(float* res, float* scratch, const uint32_t width, const uint8_t parity,
                     const uint32_t stride, const uint32_t numrows)
{
  HWY_DYNAMIC_DISPATCH(encode_97_h)(res, scratch, width, parity, stride, numrows);
}

} // namespace grk
#endif
