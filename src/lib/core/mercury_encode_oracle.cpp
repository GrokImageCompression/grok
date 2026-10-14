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
 */

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "mercury_encode_oracle.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>
#include <hwy/aligned_allocator.h>

HWY_BEFORE_NAMESPACE();
namespace grk
{
namespace HWY_NAMESPACE
{
  static uint32_t float_lanes()
  {
    return (uint32_t)hwy::HWY_NAMESPACE::Lanes(HWY_FULL(float)());
  }

  // grok's CompressIrrev over aligned rows of whole vectors
  static const float a_r = 0.299f;
  static const float a_g = 0.587f;
  static const float a_b = 0.114f;
  static const float cb = 0.5f / (1.0f - a_b);
  static const float cr = 0.5f / (1.0f - a_r);
  static void classic_forward_ict(int32_t* chan0, int32_t* chan1, int32_t* chan2, int32_t dcShift,
                                  uint64_t chunkSize)
  {
    using namespace hwy::HWY_NAMESPACE;
    int32_t shift[3] = {dcShift, dcShift, dcShift};
    const HWY_FULL(float) df;
    const HWY_FULL(int32_t) di;
    auto va_r = Set(df, a_r);
    auto va_g = Set(df, a_g);
    auto va_b = Set(df, a_b);
    auto vcb = Set(df, cb);
    auto vcr = Set(df, cr);
    auto vdcr = Set(di, shift[0]);
    auto vdcg = Set(di, shift[1]);
    auto vdcb = Set(di, shift[2]);
    size_t begin = 0;
    for(auto j = begin; j < begin + chunkSize; j += Lanes(di))
    {
      auto r = ConvertTo(df, Load(di, chan0 + j) + vdcr);
      auto g = ConvertTo(df, Load(di, chan1 + j) + vdcg);
      auto b = ConvertTo(df, Load(di, chan2 + j) + vdcb);

      auto y = va_r * r + va_g * g + va_b * b;
      auto u = vcb * (b - y);
      auto v = vcr * (r - y);

      Store(y, df, (float*)(chan0 + j));
      Store(u, df, (float*)(chan1 + j));
      Store(v, df, (float*)(chan2 + j));
    }
  }
} // namespace HWY_NAMESPACE
} // namespace grk
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

#include <algorithm>
#include <cstdint>

#include "wavelet/WaveletCommon.h"

#if defined(_WIN32)
#define GRK_MERCURY_ORACLE_API __declspec(dllexport)
#else
#define GRK_MERCURY_ORACLE_API __attribute__((visibility("default")))
#endif

namespace grk
{
HWY_EXPORT(float_lanes);
HWY_EXPORT(classic_forward_ict);

template<typename T, typename DWT>
static void classicForwardDwt(T* data, uint32_t stride, const uint32_t bounds[4], uint32_t levels,
                              T dcShift, bool intInput)
{
  const uint32_t lanes = HWY_DYNAMIC_DISPATCH(float_lanes)();
  auto scaled = [](uint32_t coordinate, uint32_t level) {
    return (uint32_t)(((uint64_t)coordinate + (1ULL << level) - 1) >> level);
  };
  const uint32_t maxDim = std::max(bounds[2] - bounds[0], bounds[3] - bounds[1]);
  auto scratch = hwy::AllocateAligned<T>((size_t)std::max(maxDim, 1U) * lanes);
  DWT dwt;
  for(uint32_t level = 0; level < levels; ++level)
  {
    const uint32_t rx0 = scaled(bounds[0], level);
    const uint32_t ry0 = scaled(bounds[1], level);
    const uint32_t rw = scaled(bounds[2], level) - rx0;
    const uint32_t rh = scaled(bounds[3], level) - ry0;
    const bool firstLevel = level == 0;
    const T levelShift = firstLevel ? dcShift : T(0);
    const bool levelIntInput = firstLevel && intInput;
    for(uint32_t j = 0; j < rw; j += lanes)
      dwt.encode_v(data + j, scratch.get(), rh, (uint8_t)(ry0 & 1), stride, std::min(lanes, rw - j),
                   levelShift, levelIntInput);
    for(uint32_t j = 0; j < rh; j += lanes)
      dwt.encode_h(data + (size_t)j * stride, scratch.get(), rw, (uint8_t)(rx0 & 1), stride,
                   std::min(lanes, rh - j));
  }
}
} // namespace grk

// data must be vector aligned with a stride that is a multiple of 16 samples
extern "C" GRK_MERCURY_ORACLE_API int32_t grk_classic_forward_dwt_tile(
    bool reversible, void* data, uint32_t stride, const uint32_t* bounds, uint32_t levels,
    int32_t dcShift, bool intInput)
{
  if(reversible)
    grk::classicForwardDwt<int32_t, grk::dwt53>((int32_t*)data, stride, bounds, levels, dcShift,
                                                false);
  else
    grk::classicForwardDwt<float, grk::dwt97>((float*)data, stride, bounds, levels, (float)dcShift,
                                              intInput);
  return 0;
}

// planes are vector aligned and padded to whole vectors; transformed in place to floats
extern "C" GRK_MERCURY_ORACLE_API void grk_classic_forward_ict(int32_t* red, int32_t* green,
                                                               int32_t* blue, int32_t dcShift,
                                                               uint64_t samples)
{
  HWY_DYNAMIC_DISPATCH(grk::classic_forward_ict)(red, green, blue, dcShift, samples);
}

#endif
