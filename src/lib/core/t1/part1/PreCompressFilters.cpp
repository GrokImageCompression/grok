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
#include "PreCompressFilters.h"
#include "CodeStreamLimits.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "t1/part1/PreCompressFilters.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>
HWY_BEFORE_NAMESPACE();
namespace grk::t1
{
namespace HWY_NAMESPACE
{
  namespace hn = hwy::HWY_NAMESPACE;

  uint32_t hwy_to_sign_magnitude16(int32_t* dest, const int16_t* src, size_t srcStride,
                                   uint32_t width, uint32_t height)
  {
    const hn::ScalableTag<int32_t> di;
    const hn::Rebind<int16_t, decltype(di)> d16;
    const size_t lanes = hn::Lanes(di);
    const auto signBit = hn::Set(di, INT32_MIN);
    auto maximum = hn::Zero(di);
    auto convert = [&](auto value) {
      auto magnitude = hn::ShiftLeft<T1_NMSEDEC_FRACBITS>(hn::Abs(value));
      maximum = hn::Max(maximum, magnitude);
      return hn::Or(magnitude, hn::And(value, signBit));
    };
    for(uint32_t row = 0; row < height; ++row, src += srcStride, dest += width)
    {
      size_t i = 0;
      for(; i + lanes <= width; i += lanes)
        hn::StoreU(convert(hn::PromoteTo(di, hn::LoadU(d16, src + i))), di, dest + i);
      if(i < width)
        hn::StoreN(convert(hn::PromoteTo(di, hn::LoadN(d16, src + i, width - i))), di, dest + i,
                   width - i);
    }
    return (uint32_t)hn::ReduceMax(di, maximum);
  }
} // namespace HWY_NAMESPACE
} // namespace grk::t1
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace grk::t1
{
HWY_EXPORT(hwy_to_sign_magnitude16);

uint32_t toSignMagnitude16(int32_t* dest, const int16_t* src, size_t srcStride, uint32_t width,
                           uint32_t height)
{
  return HWY_DYNAMIC_DISPATCH(hwy_to_sign_magnitude16)(dest, src, srcStride, width, height);
}
} // namespace grk::t1
#endif
