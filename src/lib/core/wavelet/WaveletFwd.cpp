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

#include <array>
#include <cmath>

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
#include "WaveletInt16Rules.h"
#include "Forward97Constants.h"

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
    // AMD runs masked stores as microcode, so full strips use plain stores
    const bool fullStrip = numcols == lanes;
    auto storeRow = [&](auto v, T* row) {
      if(fullStrip)
        StoreU(v, d, row);
      else
        StoreN(v, d, row, numcols);
    };

    T* destPtrLow = dst;
    const T* srcPtrLow = src + parity * lanes;

    T* destPtrHigh = dst + size_t(sn) * stride;
    const T* srcPtrHigh = src + !parity * lanes;
    uint32_t i = 0;
    for(; i < std::min(dn, sn); ++i)
    {
      storeRow(Load(d, srcPtrLow), destPtrLow);
      storeRow(Load(d, srcPtrHigh), destPtrHigh);

      destPtrLow += stride;
      srcPtrLow += (lanes << 1);
      destPtrHigh += stride;
      srcPtrHigh += (lanes << 1);
    }

    if(i < sn)
      storeRow(Load(d, srcPtrLow), destPtrLow);
    else if(i < dn)
      storeRow(Load(d, srcPtrHigh), destPtrHigh);
  }

  HWY_ATTR void encode_53_v(int32_t* resolution, int32_t* scratch, const uint32_t height,
                            const uint8_t parity, const uint32_t stride, const uint32_t numcols,
                            int32_t dcShift)
  {
    const HWY_FULL(int32_t) d;
    if(height <= 1)
    {
      // a lone row still carries the dc shift, and doubles only when it sits
      // on an odd row and so is a high-pass coefficient
      if(height == 1 && (parity == 1 || dcShift != 0))
      {
        auto v = LoadN(d, resolution, numcols);
        if(dcShift != 0)
          v = v - Set(d, dcShift);
        auto res = parity == 1 ? ShiftLeft<1>(v) : v;
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
      auto s_0 = LoadN(d, resolution, numcols);
      auto d_0 = LoadN(d, resolution + stride, numcols);
      if(sn > 1)
      {
        //  high pass top
        const auto s_0p1 = LoadN(d, resolution + 2 * stride, numcols);
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
          const auto s_ip1 = LoadN(d, resolution + ((i + 1) << 1) * stride, numcols);
          const auto d_i =
              LoadN(d, resolution + ((i << 1) + 1) * stride, numcols) - ShiftRight<1>(s_i + s_ip1);
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
          const auto d_i = LoadN(d, resolution + ((i << 1) + 1) * stride, numcols) - s_i;
          Store(d_i, d, &scratch[((i << 1) + 1) * lanes]);

          // final low-pass step
          const auto s_ii =
              LoadN(d, resolution + (i << 1) * stride, numcols) + ShiftRight<2>(d_im1 + d_i + c_d2);
          Store(s_ii, d, &scratch[(i << 1) * lanes]);
        }
        else
        {
          // low pass reflection at bottom
          const auto s_ii = LoadN(d, resolution + (i << 1) * stride, numcols) +
                            ShiftRight<2>(ShiftLeft<1>(d_im1) + c_d2);
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
      auto s_i = LoadN(d, resolution + 1 * stride, numcols); // s_0
      auto d_0 = LoadN(d, resolution + 0 * stride, numcols) - s_i;
      Store(d_0, d, &scratch[0]);

      auto s_im1 = s_i;
      auto d_im1 = d_0;
      uint32_t i = 1;
      for(; i < sn; i++)
      {
        // high
        s_i = LoadN(d, resolution + ((i << 1) + 1) * stride, numcols);
        const auto d_i =
            LoadN(d, resolution + (i << 1) * stride, numcols) - ShiftRight<1>(s_i + s_im1);
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
        const auto d_sn = LoadN(d, resolution + (sn << 1) * stride, numcols) - s_im1;
        Store(d_sn, d, &scratch[(sn << 1) * lanes]);

        // regular low pass at bottom
        const auto s_iim1 = LoadN(d, resolution + (((dn - 2) << 1) + 1) * stride, numcols) +
                            ShiftRight<2>(d_im1 + d_sn + c_d2);
        Store(s_iim1, d, &scratch[(((dn - 2) << 1) + 1) * lanes]);
      }
      else
      {
        // low pass reflection at bottom
        const auto s_ii = LoadN(d, resolution + (((dn - 1) << 1) + 1) * stride, numcols) +
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

  // target[k] += coefficient * (neighbours[k + offset] + neighbours[k + offset + 1]), with the
  // neighbours mirrored at both ends of the row
  template<bool Scaled>
  HWY_ATTR static void lift97_row(float* target, uint32_t targetCount, const float* neighbours,
                                  uint32_t neighbourCount, int32_t offset, float coefficient,
                                  float scale)
  {
    const HWY_FULL(float) d;
    const size_t lanes = Lanes(d);
    const auto vcoefficient = Set(d, coefficient);
    const auto vscale = Set(d, scale);
    auto lift = [&](auto curr, auto left, auto right) {
      auto lifted = curr + (left + right) * vcoefficient;
      if constexpr(Scaled)
        lifted = lifted * vscale;
      return lifted;
    };
    auto liftOne = [&](uint32_t k) {
      int64_t left = (int64_t)k + offset;
      int64_t right = left + 1;
      if(left < 0)
        left = right;
      if(right >= (int64_t)neighbourCount)
        right = left;
      target[k] =
          GetLane(lift(Set(d, target[k]), Set(d, neighbours[left]), Set(d, neighbours[right])));
    };
    // both neighbours exist for k >= -offset and k + offset + 1 < neighbourCount
    uint32_t begin = (uint32_t)std::min<int64_t>(-offset, targetCount);
    int64_t lastInterior = (int64_t)neighbourCount - offset - 1;
    uint32_t end = (uint32_t)std::clamp<int64_t>(lastInterior, begin, targetCount);
    for(uint32_t k = 0; k < begin; ++k)
      liftOne(k);
    uint32_t k = begin;
    for(; k + lanes <= end; k += (uint32_t)lanes)
    {
      auto left = LoadU(d, neighbours + (int64_t)k + offset);
      auto right = LoadU(d, neighbours + (int64_t)k + offset + 1);
      StoreU(lift(LoadU(d, target + k), left, right), d, target + k);
    }
    for(; k < targetCount; ++k)
      liftOne(k);
  }

  void encode_97_h(float* res, float* scratch, const uint32_t width, const uint8_t parity,
                   const uint32_t stride, const uint32_t numrows, float dcShift)
  {
    if(width == 1)
    {
      // a lone column is passed through unlifted, doubled on an odd column
      // where the decompressor's step size drops the high-pass gain
      if(parity == 1)
      {
        for(uint32_t j = 0; j < numrows; ++j)
          res[(size_t)j * stride] *= 2.0f;
      }
      return;
    }

    const HWY_FULL(float) d;
    const size_t lanes = Lanes(d);
    const uint32_t sn = (width + !parity) >> 1;
    const uint32_t dn = width - sn;
    const auto vshift = Set(d, dcShift);
    float* low = scratch;
    float* high = scratch + sn;
    float* evenSamples = parity ? high : low;
    float* oddSamples = parity ? low : high;
    // a high-pass sample's left neighbour is low[k] for parity 0 and low[k - 1] for parity 1
    const int32_t highOffset = parity ? -1 : 0;
    const int32_t lowOffset = parity ? 0 : -1;
    for(uint32_t r = 0; r < numrows; ++r)
    {
      float* row = res + (size_t)r * stride;
      uint32_t pair = 0;
      for(; 2 * pair + 2 * lanes <= width; pair += (uint32_t)lanes)
      {
        auto first = LoadU(d, row + 2 * pair);
        auto second = LoadU(d, row + 2 * pair + lanes);
        StoreU(ConcatEven(d, second, first) - vshift, d, evenSamples + pair);
        StoreU(ConcatOdd(d, second, first) - vshift, d, oddSamples + pair);
      }
      for(; 2 * pair < width; ++pair)
      {
        evenSamples[pair] = GetLane(Set(d, row[2 * pair]) - vshift);
        if(2 * pair + 1 < width)
          oddSamples[pair] = GetLane(Set(d, row[2 * pair + 1]) - vshift);
      }
      lift97_row<false>(high, dn, low, sn, highOffset, alpha, 1.0f);
      lift97_row<false>(low, sn, high, dn, lowOffset, beta, 1.0f);
      lift97_row<true>(high, dn, low, sn, highOffset, gamma, K);
      // the high pass neighbours were stored already scaled by K
      lift97_row<true>(low, sn, high, dn, lowOffset, delta * invK, invK);
      memcpy(row, scratch, (size_t)width * sizeof(float));
    }
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
    {
      // a lone row still carries the dc shift and the int to float conversion,
      // and doubles on an odd row where the step size drops the high-pass gain
      if(height == 1)
      {
        const HWY_FULL(int32_t) di;
        auto v = intInput ? ConvertTo(d, LoadN(di, (const int32_t*)res, numcols))
                          : LoadN(d, res, numcols);
        if(dcShift != 0.0f)
          v = v - Set(d, dcShift);
        if(parity == 1)
          v = v + v;
        StoreN(v, d, res, numcols);
      }
      return;
    }

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
          Store(ConvertTo(d, LoadN(di, intIn, numcols)) - vshift, d, temp);
          temp += lanes;
          intIn += stride;
        }
      }
      else
      {
        for(auto j = 0U; j < height; ++j)
        {
          Store(ConvertTo(d, LoadN(di, intIn, numcols)), d, temp);
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
          Store(LoadN(d, in, numcols) - vshift, d, temp);
          temp += lanes;
          in += stride;
        }
      }
      else
      {
        for(auto j = 0U; j < height; ++j)
        {
          Store(LoadN(d, in, numcols), d, temp);
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
    // the high pass neighbours were stored already scaled by K
    s_s = Set(d, delta * invK);
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
   *  Performs lifting in int16_t and preserves the tile buffer type.
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

  template<typename T>
  HWY_ATTR static void copy_to_int16(const T* src, int16_t* dst, uint32_t numcols)
  {
    for(uint32_t j = 0; j < numcols; ++j)
      dst[j] = (int16_t)src[j];
  }

  template<typename T>
  HWY_ATTR static void copy_from_int16(const int16_t* src, T* dst, uint32_t numcols)
  {
    for(uint32_t j = 0; j < numcols; ++j)
      dst[j] = (T)src[j];
  }

  template<typename T>
  HWY_ATTR void encode_53_16_v(T* resolution, int16_t* scratch, const uint32_t height,
                               const uint8_t parity, const uint32_t stride, const uint32_t numcols,
                               int32_t dcShift)
  {
    const HWY_FULL(int16_t) d16;
    const size_t lanes16 = Lanes(d16);

    if(height <= 1)
    {
      // a lone row still carries the dc shift, and doubles only when it sits
      // on an odd row and so is a high-pass coefficient
      if(height == 1 && (parity == 1 || dcShift != 0))
      {
        for(uint32_t j = 0; j < numcols; ++j)
        {
          int16_t v = (int16_t)resolution[j];
          if(dcShift != 0)
            v = (int16_t)(v - (int16_t)dcShift);
          resolution[j] = (T)(int16_t)(parity == 1 ? (int16_t)(v << 1) : v);
        }
      }
      return;
    }

    const uint32_t sn = (height + !parity) >> 1;
    const uint32_t dn = height - sn;

    int16_t* E = scratch;
    int16_t* O = scratch + sn * lanes16;

    for(uint32_t k = 0; k < sn; ++k)
      copy_to_int16(resolution + size_t(2 * k + parity) * stride, E + k * lanes16, numcols);
    for(uint32_t k = 0; k < dn; ++k)
      copy_to_int16(resolution + size_t(2 * k + !parity) * stride, O + k * lanes16, numcols);

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
      copy_from_int16(E + k * lanes16, resolution + k * stride, numcols);
    for(uint32_t k = 0; k < dn; ++k)
      copy_from_int16(O + k * lanes16, resolution + (sn + k) * stride, numcols);
  }

  template<typename T>
  HWY_ATTR void encode_53_16_h(T* resolution, int16_t* scratch, const uint32_t width,
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
      T* row = resolution + r * stride;

      int16_t* buf = scratch;
      const int16_t* source;
      if constexpr(std::is_same_v<T, int16_t>)
      {
        source = row;
      }
      else
      {
        copy_to_int16(row, buf, width);
        source = buf;
      }

      // Separate into E[0..sn-1] and O[0..dn-1]
      int16_t* E = buf + width;
      int16_t* O = E + sn;
      int16_t* evenSamples = parity ? O : E;
      int16_t* oddSamples = parity ? E : O;
      uint32_t pair = 0;
      for(; 2 * pair + 2 * lanes16 <= width; pair += (uint32_t)lanes16)
      {
        auto low = LoadU(d16, source + 2 * pair);
        auto high = LoadU(d16, source + 2 * pair + lanes16);
        StoreU(ConcatEven(d16, high, low), d16, evenSamples + pair);
        StoreU(ConcatOdd(d16, high, low), d16, oddSamples + pair);
      }
      for(; 2 * pair < width; ++pair)
      {
        evenSamples[pair] = source[2 * pair];
        if(2 * pair + 1 < width)
          oddSamples[pair] = source[2 * pair + 1];
      }

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

      if constexpr(std::is_same_v<T, int16_t>)
      {
        memcpy(row, E, (size_t)width * sizeof(int16_t));
      }
      else
      {
        for(uint32_t k = 0; k < sn; ++k)
          row[k] = (T)E[k];
        for(uint32_t k = 0; k < dn; ++k)
          row[sn + k] = (T)O[k];
      }
    }
  }

  void encode_53_16_v_in_place(int16_t* resolution, int16_t* scratch, uint32_t height,
                               uint8_t parity, uint32_t stride, uint32_t numcols, int16_t dcShift,
                               bool)
  {
    encode_53_16_v(resolution, scratch, height, parity, stride, numcols, dcShift);
  }

  void encode_53_16_h_in_place(int16_t* resolution, int16_t* scratch, uint32_t width,
                               uint8_t parity, uint32_t stride, uint32_t numrows, int16_t dcShift)
  {
    encode_53_16_h(resolution, scratch, width, parity, stride, numrows);
    (void)dcShift;
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
          // Lanes() is a runtime value on SVE and RVV
          nodes[t].work([td, scratch, rh, parity_col, stride, lanes16 = lanes16, currentDcShift,
                         min_j, max_j] {
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

  template<class D, class V>
  HWY_INLINE V halveTiesToEven(D d, V x)
  {
    // rounding ties up biases the low pass upward, and the bias adds up across levels
    return ShiftRight<1>(x + And(ShiftRight<1>(x), Set(d, (int16_t)1)));
  }

  template<class D, class V>
  HWY_INLINE V average16(D d, V a, V b)
  {
    const hwy::HWY_NAMESPACE::RebindToUnsigned<D> du;
    const auto sign = Set(du, (uint16_t)0x8000);
    auto roundedUp =
        BitCast(d, Xor(AverageRound(Xor(BitCast(du, a), sign), Xor(BitCast(du, b), sign)), sign));
    auto oddTie = And(And(Xor(a, b), roundedUp), Set(d, (int16_t)1));
    return roundedUp - oddTie;
  }

  template<class D, class V>
  HWY_INLINE V halvedPredict0(D d, V odd, V left, V right)
  {
    auto sum = left + right;
    return halveTiesToEven(d, odd + MulFixedPoint15(sum, Set(d, fwd97_g))) - sum;
  }
  template<class D, class V>
  HWY_INLINE V halvedUpdate1(D d, V even, V left, V right)
  {
    return even + MulFixedPoint15(average16(d, left, right), Set(d, fwd97_4beta));
  }
  template<class D, class V>
  HWY_INLINE V halvedPredict2(D d, V odd, V left, V right)
  {
    return odd + MulFixedPoint15(left + right, Set(d, fwd97_half_gamma));
  }
  template<class D, class V>
  HWY_INLINE V halvedUpdate3(D d, V even, V left, V right)
  {
    return even + MulFixedPoint15(left + right, Set(d, fwd97_2delta));
  }
  template<class D, class V>
  HWY_INLINE V unhalvedPredict0(D d, V odd, V left, V right)
  {
    auto sum = left + right;
    return odd - sum + MulFixedPoint15(sum, Set(d, fwd97_unhalved_0));
  }
  template<class D, class V>
  HWY_INLINE V unhalvedUpdate1(D d, V even, V left, V right)
  {
    return even + MulFixedPoint15(average16(d, left, right), Set(d, fwd97_unhalved_2beta));
  }
  template<class D, class V>
  HWY_INLINE V unhalvedPredict2(D d, V odd, V left, V right)
  {
    return odd + MulFixedPoint15(left + right, Set(d, fwd97_unhalved_gamma));
  }
  template<class D, class V>
  HWY_INLINE V unhalvedUpdate3(D d, V even, V left, V right)
  {
    return even + MulFixedPoint15(left + right, Set(d, fwd97_unhalved_delta));
  }

  static int16_t scaleLoneSample(int16_t value, double factor)
  {
    return (int16_t)std::lround(value * factor);
  }

  void encode_97_16_v(int16_t* resolution, int16_t* scratch, uint32_t height, uint8_t parity,
                      uint32_t stride, uint32_t numcols, int16_t dcShift, uint8_t sampleShift,
                      int32_t factorQ15)
  {
    const HWY_FULL(int16_t) d;
    const size_t lanes = Lanes(d);
    // AMD runs masked stores as microcode
    const bool fullStrip = numcols == lanes;
    auto loadRow = [&](const int16_t* row) {
      auto samples = fullStrip ? LoadU(d, row) : LoadN(d, row, numcols);
      return ShiftLeftSame(samples - Set(d, dcShift), sampleShift);
    };
    auto storeRow = [&](auto v, int16_t* row) {
      if(fullStrip)
        StoreU(v, d, row);
      else
        StoreN(v, d, row, numcols);
    };
    const double factor = factorQ15 / 32768.0;
    if(height <= 1)
    {
      if(height == 1)
      {
        storeRow(loadRow(resolution), resolution);
        double loneFactor = parity ? factor / K : factor * K;
        for(uint32_t j = 0; j < numcols; ++j)
          resolution[j] = scaleLoneSample(resolution[j], loneFactor);
      }
      return;
    }

    const uint32_t sn = (height + !parity) >> 1;
    const uint32_t dn = height - sn;
    HWY_ALIGN int16_t lowRing[4 * MaxLanes(d)];
    auto isLow = [parity](uint32_t position) { return ((position + parity) & 1) == 0; };
    auto rowAt = [&](uint32_t position) {
      size_t band = position >> 1;
      return isLow(position) ? lowRing + (band & 3) * lanes : scratch + band * lanes;
    };

    const bool unitFactor = factorQ15 == 32768;
    const auto halfFactor = Set(d, (int16_t)((factorQ15 + 1) >> 1));
    const auto halfFactorAlpha = Set(d, (int16_t)std::lround((double)factorQ15 * alpha / 2.0));
    const auto levelFactor = Set(d, (int16_t)factorQ15);
    for(uint32_t newest = 0; newest < height + 4; ++newest)
    {
      if(newest < height)
        Store(loadRow(resolution + size_t(newest) * stride), d, rowAt(newest));
      for(uint32_t step = 0; step < 4; ++step)
      {
        if(newest < step + 1)
          break;
        uint32_t target = newest - 1 - step;
        bool updatesLow = step & 1;
        if(target >= height || isLow(target) != updatesLow)
          continue;
        auto left = Load(d, rowAt(target == 0 ? 1 : target - 1));
        auto right = Load(d, rowAt(target + 1 < height ? target + 1 : target - 1));
        int16_t* row = rowAt(target);
        auto value = Load(d, row);
        switch(step)
        {
          case 0:
            value = unitFactor ? halvedPredict0(d, value, left, right)
                               : MulFixedPoint15(value, halfFactor) +
                                     MulFixedPoint15(left + right, halfFactorAlpha);
            break;
          case 1:
            value = halvedUpdate1(d, unitFactor ? value : MulFixedPoint15(value, levelFactor), left,
                                  right);
            break;
          case 2:
            value = halvedPredict2(d, value, left, right);
            break;
          default:
            value = halvedUpdate3(d, value, left, right);
            break;
        }
        if(step == 3)
          storeRow(value, resolution + size_t(target >> 1) * stride);
        else
          Store(value, d, row);
      }
    }
    for(uint32_t k = 0; k < dn; ++k)
      storeRow(Load(d, scratch + k * lanes), resolution + size_t(sn + k) * stride);
  }

  template<class Op>
  HWY_ATTR static void lift97_16_row(int16_t* target, uint32_t targetCount,
                                     const int16_t* neighbours, uint32_t neighbourCount,
                                     int32_t offset, Op op)
  {
    const HWY_FULL(int16_t) d;
    const size_t lanes = Lanes(d);
    auto liftOne = [&](uint32_t k) {
      int64_t left = (int64_t)k + offset;
      int64_t right = left + 1;
      if(left < 0)
        left = right;
      if(right >= (int64_t)neighbourCount)
        right = left;
      target[k] =
          GetLane(op(d, Set(d, target[k]), Set(d, neighbours[left]), Set(d, neighbours[right])));
    };
    uint32_t begin = (uint32_t)std::min<int64_t>(-offset, targetCount);
    int64_t lastInterior = (int64_t)neighbourCount - offset - 1;
    uint32_t end = (uint32_t)std::clamp<int64_t>(lastInterior, begin, targetCount);
    for(uint32_t k = 0; k < begin; ++k)
      liftOne(k);
    uint32_t k = begin;
    for(; k + lanes <= end; k += (uint32_t)lanes)
    {
      auto left = LoadU(d, neighbours + (int64_t)k + offset);
      auto right = LoadU(d, neighbours + (int64_t)k + offset + 1);
      StoreU(op(d, LoadU(d, target + k), left, right), d, target + k);
    }
    for(; k < targetCount; ++k)
      liftOne(k);
  }

  void encode_97_16_h(int16_t* rows, int16_t* scratch, uint32_t width, uint8_t parity,
                      uint32_t stride, uint32_t numRows, uint32_t firstRow,
                      uint32_t verticalLowRows)
  {
    const HWY_FULL(int16_t) d;
    const size_t lanes = Lanes(d);
    const uint32_t sn = (width + !parity) >> 1;
    const uint32_t dn = width - sn;
    int16_t* low = scratch;
    int16_t* high = scratch + sn;
    int16_t* evenSamples = parity ? high : low;
    int16_t* oddSamples = parity ? low : high;
    const int32_t highOffset = parity ? -1 : 0;
    const int32_t lowOffset = parity ? 0 : -1;
    for(uint32_t r = 0; r < numRows; ++r)
    {
      int16_t* row = rows + (size_t)r * stride;
      bool halvedOdd = firstRow + r < verticalLowRows;
      if(width == 1)
      {
        double loneFactor = parity ? (halvedOdd ? 1.0 / K : 2.0 / K) : K;
        row[0] = scaleLoneSample(row[0], loneFactor);
        continue;
      }
      uint32_t pair = 0;
      for(; 2 * pair + 2 * lanes <= width; pair += (uint32_t)lanes)
      {
        auto first = LoadU(d, row + 2 * pair);
        auto second = LoadU(d, row + 2 * pair + lanes);
        StoreU(ConcatEven(d, second, first), d, evenSamples + pair);
        StoreU(ConcatOdd(d, second, first), d, oddSamples + pair);
      }
      for(; 2 * pair < width; ++pair)
      {
        evenSamples[pair] = row[2 * pair];
        if(2 * pair + 1 < width)
          oddSamples[pair] = row[2 * pair + 1];
      }
      if(halvedOdd)
      {
        lift97_16_row(high, dn, low, sn, highOffset, [](auto dd, auto t, auto l, auto rr) {
          return halvedPredict0(dd, t, l, rr);
        });
        lift97_16_row(low, sn, high, dn, lowOffset,
                      [](auto dd, auto t, auto l, auto rr) { return halvedUpdate1(dd, t, l, rr); });
        lift97_16_row(high, dn, low, sn, highOffset, [](auto dd, auto t, auto l, auto rr) {
          return halvedPredict2(dd, t, l, rr);
        });
        lift97_16_row(low, sn, high, dn, lowOffset,
                      [](auto dd, auto t, auto l, auto rr) { return halvedUpdate3(dd, t, l, rr); });
      }
      else
      {
        lift97_16_row(high, dn, low, sn, highOffset, [](auto dd, auto t, auto l, auto rr) {
          return unhalvedPredict0(dd, t, l, rr);
        });
        lift97_16_row(low, sn, high, dn, lowOffset, [](auto dd, auto t, auto l, auto rr) {
          return unhalvedUpdate1(dd, t, l, rr);
        });
        lift97_16_row(high, dn, low, sn, highOffset, [](auto dd, auto t, auto l, auto rr) {
          return unhalvedPredict2(dd, t, l, rr);
        });
        lift97_16_row(low, sn, high, dn, lowOffset, [](auto dd, auto t, auto l, auto rr) {
          return unhalvedUpdate3(dd, t, l, rr);
        });
      }
      memcpy(row, scratch, (size_t)width * sizeof(int16_t));
    }
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
                         task->stride, (uint32_t)lanes, j, task->level);
    if(j < task->max_j)
      task->dwt.encode_h((T*)task->tiledp + ind, (T*)task->line.mem, task->r_dim, task->line.parity,
                         task->stride, task->max_j - j, j, task->level);
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
                         task->stride, (uint32_t)lanes, task->level);
    if(j < task->max_j)
      task->dwt.encode_v((T*)task->tiledp + ind, (T*)task->line.mem, task->r_dim, task->line.parity,
                         task->stride, task->max_j - j, task->level);
  }

  template<typename T, typename DWT>
  bool encode(TileComponent* tilec, const DWT& dwt)
  {
    if(tilec->num_resolutions_ == 1U)
      return true;
    const HWY_FULL(T) d;
    const uint32_t lanes = uint32_t(Lanes(d));

    uint32_t stride;
    T* tiledp;
    if constexpr(std::is_same_v<T, int16_t>)
    {
      auto highest = tilec->getWindow16()->getResWindowBufferHighestSimple();
      stride = highest.stride_;
      tiledp = highest.buf_;
    }
    else
    {
      auto highest = tilec->getWindow()->getResWindowBufferHighestSimple();
      stride = highest.stride_;
      tiledp = (T*)highest.buf_;
    }

    const uint8_t maxNumResolutions = (uint8_t)(tilec->num_resolutions_ - 1);
    auto currentRes = tilec->resolutions_ + maxNumResolutions;
    auto lastRes = currentRes - 1;

    size_t dataSize = max_resolution(tilec->resolutions_, tilec->num_resolutions_);
    const size_t thick = lanes;
    dataSize *= thick * sizeof(T);
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
    auto infoArray = std::make_unique<encode_info<T, DWT>[]>(num_threads);
    for(auto j = 0U; j < num_threads; ++j)
    {
      auto info = &infoArray[j];
      info->line.mem = scratch_pool + (j * dataSize) / sizeof(T);
      info->stride = stride;
      info->tiledp = tiledp;
      info->dwt = dwt;
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

      const ForwardDwtLevel level{(uint8_t)(maxNumResolutions - 1 - i), rh_lower};
      uint32_t sn = rh_lower;
      uint32_t dn = rh - rh_lower;
      if(num_threads <= 1 || rw < (lanes << 1))
      {
        uint32_t j;
        for(j = 0; j + lanes - 1 < rw; j += lanes)
          dwt.encode_v((T*)tiledp + j, scratch_pool, rh, parity_col, stride, lanes, level);
        if(j < rw)
          dwt.encode_v((T*)tiledp + j, scratch_pool, rh, parity_col, stride, rw - j, level);
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
          info->level = level;
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

      sn = rw_lower;
      dn = (uint32_t)(rw - rw_lower);
      if(num_threads <= 1 || rh < (lanes << 1))
      {
        uint32_t j;
        for(j = 0; j + lanes - 1 < rh; j += lanes)
          dwt.encode_h((T*)tiledp + size_t(j) * stride, scratch_pool, rw, parity_row, stride, lanes,
                       j, level);
        if(j < rh)
          dwt.encode_h((T*)tiledp + size_t(j) * stride, scratch_pool, rw, parity_row, stride,
                       rh - j, j, level);
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
          info->level = level;
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

  bool encode_53(TileComponent* tilec, dwt53 dwt)
  {
    return encode<int32_t, dwt53>(tilec, dwt);
  }
  bool encode_53_16_in_place(TileComponent* tilec, dwt53_16 dwt)
  {
    return encode<int16_t, dwt53_16>(tilec, dwt);
  }
  bool encode_97(TileComponent* tilec, dwt97 dwt)
  {
    return encode<float, dwt97>(tilec, dwt);
  }
  bool encode_97_16(TileComponent* tilec, dwt97_16 dwt)
  {
    return encode<int16_t, dwt97_16>(tilec, dwt);
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
      schedule_encode(TileComponent* tilec, const DWT& dwt,
                      std::vector<std::pair<FlowComponent*, FlowComponent*>>& levelFlows)
  {
    if(tilec->num_resolutions_ == 1U)
      return nullptr;
    const HWY_FULL(T) d;
    const uint32_t lanes = uint32_t(Lanes(d));

    uint32_t stride;
    T* tiledp;
    if constexpr(std::is_same_v<T, int16_t>)
    {
      auto highest = tilec->getWindow16()->getResWindowBufferHighestSimple();
      stride = highest.stride_;
      tiledp = highest.buf_;
    }
    else
    {
      auto highest = tilec->getWindow()->getResWindowBufferHighestSimple();
      stride = highest.stride_;
      tiledp = (T*)highest.buf_;
    }

    const uint8_t maxNumResolutions = (uint8_t)(tilec->num_resolutions_ - 1);
    auto currentRes = tilec->resolutions_ + maxNumResolutions;
    auto lastRes = currentRes - 1;

    size_t dataSize = max_resolution(tilec->resolutions_, tilec->num_resolutions_);
    const size_t thick = lanes;
    dataSize *= thick * sizeof(T);
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
      const ForwardDwtLevel level{levelIdx, rh_lower};

      // --- Vertical pass ---
      {
        uint32_t sn = rh_lower;
        uint32_t dn = rh - rh_lower;
        if(num_threads <= 1 || rw < (lanes << 1))
        {
          T* scratch = data->scratch_pool;
          // Lanes() is a runtime value on SVE and RVV
          vertFlow->nextTask().work(
              [tiledp, scratch, rw, rh, parity_col, stride, dwt, level, lanes = lanes] {
                uint32_t j;
                for(j = 0; j + lanes - 1 < rw; j += lanes)
                  dwt.encode_v((T*)tiledp + j, scratch, rh, parity_col, stride, lanes, level);
                if(j < rw)
                  dwt.encode_v((T*)tiledp + j, scratch, rh, parity_col, stride, rw - j, level);
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
            info->level = level;
            info->dwt = dwt;
            vertFlow->nextTask().work([info] { encode_v(info); });
          }
          data->passes.push_back(std::move(pass));
        }
      }

      // --- Horizontal pass ---
      {
        uint32_t sn = rw_lower;
        uint32_t dn = (uint32_t)(rw - rw_lower);
        if(num_threads <= 1 || rh < (lanes << 1))
        {
          T* scratch = data->scratch_pool;
          // Lanes() is a runtime value on SVE and RVV
          horizFlow->nextTask().work(
              [tiledp, scratch, rw, rh, parity_row, stride, dwt, level, lanes = lanes] {
                uint32_t j;
                for(j = 0; j + lanes - 1 < rh; j += lanes)
                  dwt.encode_h((T*)tiledp + size_t(j) * stride, scratch, rw, parity_row, stride,
                               lanes, j, level);
                if(j < rh)
                  dwt.encode_h((T*)tiledp + size_t(j) * stride, scratch, rw, parity_row, stride,
                               rh - j, j, level);
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
            info->level = level;
            info->dwt = dwt;
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
      schedule_encode_53(TileComponent* tilec, dwt53 dwt,
                         std::vector<std::pair<FlowComponent*, FlowComponent*>>& levelFlows)
  {
    return schedule_encode<int32_t, dwt53>(tilec, dwt, levelFlows);
  }
  std::unique_ptr<WaveletFwdScheduleData> schedule_encode_53_16_in_place(
      TileComponent* tilec, dwt53_16 dwt,
      std::vector<std::pair<FlowComponent*, FlowComponent*>>& levelFlows)
  {
    return schedule_encode<int16_t, dwt53_16>(tilec, dwt, levelFlows);
  }
  std::unique_ptr<WaveletFwdScheduleData>
      schedule_encode_97(TileComponent* tilec, dwt97 dwt,
                         std::vector<std::pair<FlowComponent*, FlowComponent*>>& levelFlows)
  {
    return schedule_encode<float, dwt97>(tilec, dwt, levelFlows);
  }
  std::unique_ptr<WaveletFwdScheduleData>
      schedule_encode_97_16(TileComponent* tilec, dwt97_16 dwt,
                            std::vector<std::pair<FlowComponent*, FlowComponent*>>& levelFlows)
  {
    return schedule_encode<int16_t, dwt97_16>(tilec, dwt, levelFlows);
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
HWY_EXPORT(encode_53_16_v_in_place);
HWY_EXPORT(encode_53_16_h_in_place);
HWY_EXPORT(encode_97_v);
HWY_EXPORT(encode_97_h);
HWY_EXPORT(encode_53);
HWY_EXPORT(encode_97);
HWY_EXPORT(encode_97_16);
HWY_EXPORT(encode_97_16_v);
HWY_EXPORT(encode_97_16_h);
HWY_EXPORT(encode_53_16);
HWY_EXPORT(encode_53_16_in_place);
HWY_EXPORT(schedule_encode_53);
HWY_EXPORT(schedule_encode_53_16_in_place);
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
  ForwardDwtLevel level;
  DWT dwt;
};

namespace
{
  struct Filter
  {
    int64_t origin = 0;
    std::vector<double> taps;
  };

  Filter shifted(const Filter& filter, int64_t offset)
  {
    return {filter.origin + offset, filter.taps};
  }

  Filter plusScaled(const Filter& first, const Filter& second, double coefficient)
  {
    int64_t begin = std::min(first.origin, second.origin);
    int64_t end = std::max(first.origin + (int64_t)first.taps.size(),
                           second.origin + (int64_t)second.taps.size());
    Filter sum{begin, std::vector<double>((size_t)(end - begin), 0.0)};
    for(size_t i = 0; i < first.taps.size(); ++i)
      sum.taps[(size_t)(first.origin - begin) + i] += first.taps[i];
    for(size_t i = 0; i < second.taps.size(); ++i)
      sum.taps[(size_t)(second.origin - begin) + i] += coefficient * second.taps[i];
    return sum;
  }

  double absoluteSum(const Filter& filter)
  {
    double sum = 0;
    for(double tap : filter.taps)
      sum += std::fabs(tap);
    return sum;
  }

  struct LiftingGains
  {
    double step0Sum;
    double step0Unhalved;
    double afterStep0;
    Filter low;
    Filter high;
  };

  LiftingGains liftingGains(const Filter& input, int64_t stride, bool halvedOdd)
  {
    const double alpha = -1.586134342, beta = -0.052980118, gamma = 0.882911075,
                 delta = 0.443506852;
    const double oddScale = halvedOdd ? 0.5 : 1.0;
    LiftingGains gains;
    Filter even = input;
    Filter odd = shifted(input, stride);
    Filter sum = plusScaled(even, shifted(even, 2 * stride), 1.0);
    gains.step0Sum = absoluteSum(sum);
    gains.step0Unhalved = absoluteSum(plusScaled(odd, sum, 2.0 + alpha));
    odd = plusScaled(odd, sum, alpha);
    for(double& tap : odd.taps)
      tap *= oddScale;
    double peak = absoluteSum(odd);
    even = plusScaled(even, plusScaled(shifted(odd, -2 * stride), odd, 1.0), beta / oddScale);
    peak = std::max(peak, absoluteSum(even));
    sum = plusScaled(even, shifted(even, 2 * stride), 1.0);
    peak = std::max(peak, absoluteSum(sum));
    odd = plusScaled(odd, sum, gamma * oddScale);
    peak = std::max(peak, absoluteSum(odd));
    sum = plusScaled(shifted(odd, -2 * stride), odd, 1.0);
    peak = std::max(peak, absoluteSum(sum));
    even = plusScaled(even, sum, delta / oddScale);
    peak = std::max(peak, absoluteSum(even));
    gains.afterStep0 = peak;
    gains.low = std::move(even);
    gains.high = std::move(odd);
    return gains;
  }

  struct Forward97Int16Plan
  {
    std::array<int32_t, GRK_MAX_DECOMP_LVLS> levelFactors;
    std::array<std::array<double, t1::BAND_NUM_ORIENTATIONS>, GRK_MAX_DECOMP_LVLS> bandScales;
  };

  Forward97Int16Plan makeForward97Int16Plan()
  {
    const double K = 1.230174105;
    // samples peak at 2^12, the margin covers rounding in the lifting
    const double limit = 0.995 * 32768.0 / 4096.0;
    // impulse responses double in length every level, and their gains agree to six digits after 11
    const uint8_t exactLevels = 12;
    Forward97Int16Plan plan{};
    Filter rowInput{0, {1.0}};
    LiftingGains halved, unhalved;
    double cumulativeFactor = 1.0;
    for(uint8_t level = 0; level < GRK_MAX_DECOMP_LVLS; ++level)
    {
      if(level < exactLevels)
      {
        int64_t stride = (int64_t)1 << level;
        halved = liftingGains(rowInput, stride, true);
        unhalved = liftingGains(rowInput, stride, false);
      }
      double rowGain = absoluteSum(rowInput);
      double scale = cumulativeFactor * std::pow(K, 2.0 * level);
      double horizontal =
          std::max(absoluteSum(halved.low) *
                       std::max({halved.step0Sum, halved.step0Unhalved, halved.afterStep0}),
                   absoluteSum(halved.high) * std::max(unhalved.step0Sum, unhalved.afterStep0));
      auto fits = [&](int32_t factorQ15) {
        double factor = factorQ15 / 32768.0;
        double unscaled = halved.step0Sum;
        if(factorQ15 == 32768)
          unscaled = std::max(unscaled, halved.step0Unhalved);
        double scaled = std::max(halved.afterStep0 * rowGain, horizontal);
        return std::max(unscaled * rowGain, scaled * factor) * scale <= limit;
      };
      int32_t factorQ15 = 32768;
      while(factorQ15 > 1024 && !fits(factorQ15))
        factorQ15 -= 1024;
      plan.levelFactors[level] = factorQ15;
      cumulativeFactor *= factorQ15 / 32768.0;
      double bandScale = cumulativeFactor * std::pow(K, 2.0 * level);
      plan.bandScales[level][t1::BAND_ORIENT_LL] = bandScale * K * K;
      plan.bandScales[level][t1::BAND_ORIENT_HL] = bandScale * 0.5;
      plan.bandScales[level][t1::BAND_ORIENT_LH] = bandScale * 0.5;
      plan.bandScales[level][t1::BAND_ORIENT_HH] = bandScale * 0.5 / (K * K);
      if(level + 1 < exactLevels)
      {
        rowInput = halved.low;
        for(double& tap : rowInput.taps)
          tap /= K;
      }
    }
    return plan;
  }

  const Forward97Int16Plan& forward97Int16Plan()
  {
    static const Forward97Int16Plan plan = makeForward97Int16Plan();
    return plan;
  }
} // namespace

int32_t forward97Int16LevelFactor(uint8_t level)
{
  return forward97Int16Plan().levelFactors[level];
}

double forward97Int16BandScale(uint8_t level, uint8_t orientation)
{
  return forward97Int16Plan().bandScales[level][orientation];
}

static dwt97_16 makeDwt97_16(const TileComponent* tile_comp, DcShiftParam dcShift, bool intInput)
{
  return {(int16_t)(dcShift.enabled ? dcShift.shift : 0),
          (uint8_t)(intInput ? tile_comp->qShift() : 0)};
}

bool WaveletFwdImpl::compress(TileComponent* tile_comp, uint8_t qmfbid, DcShiftParam dcShift,
                              bool intInput)
{
  int32_t shift = dcShift.enabled ? dcShift.shift : 0;
  if(qmfbid == 1)
  {
    if(tile_comp->is16BitDwt())
    {
      if(tile_comp->uses16BitWindow())
        return HWY_DYNAMIC_DISPATCH(encode_53_16_in_place)(tile_comp, dwt53_16{(int16_t)shift});
      return HWY_DYNAMIC_DISPATCH(encode_53_16)(tile_comp, shift);
    }
    return HWY_DYNAMIC_DISPATCH(encode_53)(tile_comp, dwt53{shift});
  }
  if(tile_comp->is16BitDwt())
    return HWY_DYNAMIC_DISPATCH(encode_97_16)(tile_comp,
                                              makeDwt97_16(tile_comp, dcShift, intInput));
  return HWY_DYNAMIC_DISPATCH(encode_97)(tile_comp, dwt97{(float)shift, intInput});
}
std::unique_ptr<WaveletFwdScheduleData> WaveletFwdImpl::scheduleCompress(
    TileComponent* tile_comp, uint8_t qmfbid, DcShiftParam dcShift,
    std::vector<std::pair<FlowComponent*, FlowComponent*>>& levelFlows, bool intInput)
{
  int32_t shift = dcShift.enabled ? dcShift.shift : 0;
  if(qmfbid == 1)
  {
    if(tile_comp->uses16BitWindow())
      return HWY_DYNAMIC_DISPATCH(schedule_encode_53_16_in_place)(
          tile_comp, dwt53_16{(int16_t)shift}, levelFlows);
    return HWY_DYNAMIC_DISPATCH(schedule_encode_53)(tile_comp, dwt53{shift}, levelFlows);
  }
  if(tile_comp->is16BitDwt())
    return HWY_DYNAMIC_DISPATCH(schedule_encode_97_16)(
        tile_comp, makeDwt97_16(tile_comp, dcShift, intInput), levelFlows);
  return HWY_DYNAMIC_DISPATCH(schedule_encode_97)(tile_comp, dwt97{(float)shift, intInput},
                                                  levelFlows);
}
void dwt53::encode_v(int32_t* res, int32_t* scratch, uint32_t height, uint8_t parity,
                     uint32_t stride, uint32_t numcols, ForwardDwtLevel level) const
{
  HWY_DYNAMIC_DISPATCH(encode_53_v)(res, scratch, height, parity, stride, numcols,
                                    level.index == 0 ? dcShift : 0);
}
void dwt53::encode_h(int32_t* rows, int32_t* scratch, uint32_t width, uint8_t parity,
                     uint32_t stride, uint32_t numRows, uint32_t, ForwardDwtLevel) const
{
  HWY_DYNAMIC_DISPATCH(encode_53_h)(rows, scratch, width, parity, stride, numRows, 0);
}
void dwt97::encode_v(float* res, float* scratch, uint32_t height, uint8_t parity, uint32_t stride,
                     uint32_t numcols, ForwardDwtLevel level) const
{
  bool finest = level.index == 0;
  HWY_DYNAMIC_DISPATCH(encode_97_v)(res, scratch, height, parity, stride, numcols,
                                    finest ? dcShift : 0.0f, finest && intInput);
}
void dwt97::encode_h(float* rows, float* scratch, uint32_t width, uint8_t parity, uint32_t stride,
                     uint32_t numRows, uint32_t, ForwardDwtLevel) const
{
  HWY_DYNAMIC_DISPATCH(encode_97_h)(rows, scratch, width, parity, stride, numRows, 0.0f);
}
void dwt53_16::encode_v(int16_t* res, int16_t* scratch, uint32_t height, uint8_t parity,
                        uint32_t stride, uint32_t columns, ForwardDwtLevel level) const
{
  HWY_DYNAMIC_DISPATCH(encode_53_16_v_in_place)(res, scratch, height, parity, stride, columns,
                                                level.index == 0 ? dcShift : (int16_t)0, false);
}
void dwt53_16::encode_h(int16_t* rows, int16_t* scratch, uint32_t width, uint8_t parity,
                        uint32_t stride, uint32_t numRows, uint32_t, ForwardDwtLevel) const
{
  HWY_DYNAMIC_DISPATCH(encode_53_16_h_in_place)(rows, scratch, width, parity, stride, numRows, 0);
}
void dwt97_16::encode_v(int16_t* res, int16_t* scratch, uint32_t height, uint8_t parity,
                        uint32_t stride, uint32_t columns, ForwardDwtLevel level) const
{
  bool finest = level.index == 0;
  HWY_DYNAMIC_DISPATCH(encode_97_16_v)(
      res, scratch, height, parity, stride, columns, finest ? dcShift : (int16_t)0,
      finest ? sampleShift : (uint8_t)0, forward97Int16LevelFactor(level.index));
}
void dwt97_16::encode_h(int16_t* rows, int16_t* scratch, uint32_t width, uint8_t parity,
                        uint32_t stride, uint32_t numRows, uint32_t firstRow,
                        ForwardDwtLevel level) const
{
  HWY_DYNAMIC_DISPATCH(encode_97_16_h)(rows, scratch, width, parity, stride, numRows, firstRow,
                                       level.verticalLowRows);
}

} // namespace grk
#endif
