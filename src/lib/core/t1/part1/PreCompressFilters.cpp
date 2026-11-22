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

  uint32_t hwy_quantize_to_sign_magnitude(int32_t* dest, const float* src, size_t srcStride,
                                          uint32_t width, uint32_t height, double inverseStep)
  {
    const hn::ScalableTag<double> dd;
    const hn::Rebind<float, decltype(dd)> df;
    const hn::Rebind<int32_t, decltype(dd)> di;
    const size_t lanes = hn::Lanes(dd);
    const auto step = hn::Set(dd, inverseStep);
    const auto fraction = hn::Set(df, (float)(1 << T1_NMSEDEC_FRACBITS));
    const auto signBit = hn::Set(di, INT32_MIN);
    auto maximum = hn::Zero(di);
    // the scalar coder quantizes in double and rounds to float before scaling
    auto convert = [&](auto samples) {
      auto scaled = hn::Mul(hn::DemoteTo(df, hn::Mul(hn::PromoteTo(dd, samples), step)), fraction);
      auto value = hn::NearestInt(scaled);
      auto magnitude = hn::Abs(value);
      maximum = hn::Max(maximum, magnitude);
      return hn::Or(magnitude, hn::And(value, signBit));
    };
    for(uint32_t row = 0; row < height; ++row, src += srcStride, dest += width)
    {
      size_t i = 0;
      for(; i + lanes <= width; i += lanes)
        hn::StoreU(convert(hn::LoadU(df, src + i)), di, dest + i);
      if(i < width)
        hn::StoreN(convert(hn::LoadN(df, src + i, width - i)), di, dest + i, width - i);
    }
    return (uint32_t)hn::ReduceMax(di, maximum);
  }

  uint32_t hwy_quantize16_to_sign_magnitude(int32_t* dest, const int16_t* src, size_t srcStride,
                                            uint32_t width, uint32_t height, double inverseStep)
  {
    const hn::ScalableTag<int32_t> di;
    const hn::Rebind<int16_t, decltype(di)> d16;
    const hn::Rebind<float, decltype(di)> df;
    const size_t lanes = hn::Lanes(di);
    const auto scale = hn::Set(df, (float)(inverseStep * (1 << T1_NMSEDEC_FRACBITS)));
    const auto signBit = hn::Set(di, INT32_MIN);
    auto maximum = hn::Zero(di);
    auto convert = [&](auto samples) {
      auto value = hn::NearestInt(hn::Mul(hn::ConvertTo(df, hn::PromoteTo(di, samples)), scale));
      auto magnitude = hn::Abs(value);
      maximum = hn::Max(maximum, magnitude);
      return hn::Or(magnitude, hn::And(value, signBit));
    };
    for(uint32_t row = 0; row < height; ++row, src += srcStride, dest += width)
    {
      size_t i = 0;
      for(; i + lanes <= width; i += lanes)
        hn::StoreU(convert(hn::LoadU(d16, src + i)), di, dest + i);
      if(i < width)
        hn::StoreN(convert(hn::LoadN(d16, src + i, width - i)), di, dest + i, width - i);
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
HWY_EXPORT(hwy_quantize_to_sign_magnitude);
HWY_EXPORT(hwy_quantize16_to_sign_magnitude);

uint32_t toSignMagnitude16(int32_t* dest, const int16_t* src, size_t srcStride, uint32_t width,
                           uint32_t height)
{
  return HWY_DYNAMIC_DISPATCH(hwy_to_sign_magnitude16)(dest, src, srcStride, width, height);
}

uint32_t quantizeToSignMagnitude(int32_t* dest, const float* src, size_t srcStride, uint32_t width,
                                 uint32_t height, double inverseStep)
{
  return HWY_DYNAMIC_DISPATCH(hwy_quantize_to_sign_magnitude)(dest, src, srcStride, width, height,
                                                              inverseStep);
}

uint32_t quantize16ToSignMagnitude(int32_t* dest, const int16_t* src, size_t srcStride,
                                   uint32_t width, uint32_t height, double inverseStep)
{
  return HWY_DYNAMIC_DISPATCH(hwy_quantize16_to_sign_magnitude)(dest, src, srcStride, width, height,
                                                                inverseStep);
}
} // namespace grk::t1
#endif
