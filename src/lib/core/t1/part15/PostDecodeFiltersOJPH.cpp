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
#include "PostDecodeFiltersOJPH.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "t1/part15/PostDecodeFiltersOJPH.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>
HWY_BEFORE_NAMESPACE();
namespace grk::t1::ojph
{
namespace HWY_NAMESPACE
{
  namespace hn = hwy::HWY_NAMESPACE;

  template<typename DestD, typename Convert>
  void convertBlock(DestD dd, typename hn::TFromD<DestD>* dest, size_t destStride,
                    const int32_t* src, size_t srcStride, uint32_t width, uint32_t height,
                    Convert convert)
  {
    const hn::ScalableTag<int32_t> di;
    const size_t lanes = hn::Lanes(di);
    for(uint32_t row = 0; row < height; ++row, dest += destStride, src += srcStride)
    {
      size_t i = 0;
      for(; i + lanes <= width; i += lanes)
        hn::StoreU(convert(hn::LoadU(di, src + i)), dd, dest + i);
      if(i < width)
        hn::StoreN(convert(hn::LoadN(di, src + i, width - i)), dd, dest + i, width - i);
    }
  }

  // sign-magnitude to two's complement, magnitude shifted right
  template<typename V>
  V shiftSignMagnitude(V v, int shift)
  {
    const hn::DFromV<V> di;
    auto mag = hn::ShiftRightSame(hn::And(v, hn::Set(di, 0x7FFFFFFF)), shift);
    auto sign = hn::ShiftRight<31>(v);
    return hn::Sub(hn::Xor(mag, sign), sign);
  }

  template<typename V>
  auto scaleSignMagnitude(V v, float scale)
  {
    const hn::DFromV<V> di;
    const hn::Rebind<float, decltype(di)> df;
    auto mag = hn::ConvertTo(df, hn::And(v, hn::Set(di, 0x7FFFFFFF)));
    auto signBit = hn::BitCast(df, hn::And(v, hn::Set(di, INT32_MIN)));
    return hn::Or(hn::Mul(mag, hn::Set(df, scale)), signBit);
  }

  void hwy_ht_shift_block(int32_t* dest, size_t destStride, const int32_t* src, size_t srcStride,
                          uint32_t width, uint32_t height, uint32_t shift)
  {
    const hn::ScalableTag<int32_t> di;
    convertBlock(di, dest, destStride, src, srcStride, width, height,
                 [&](auto v) { return shiftSignMagnitude(v, (int)shift); });
  }

  void hwy_ht_scale_block(float* dest, size_t destStride, const int32_t* src, size_t srcStride,
                          uint32_t width, uint32_t height, float scale)
  {
    const hn::ScalableTag<float> df;
    convertBlock(df, dest, destStride, src, srcStride, width, height,
                 [&](auto v) { return scaleSignMagnitude(v, scale); });
  }

  void hwy_ht_shift_block16(int16_t* dest, size_t destStride, const int32_t* src, size_t srcStride,
                            uint32_t width, uint32_t height, uint32_t shift)
  {
    const hn::ScalableTag<int32_t> di;
    const hn::Rebind<int16_t, decltype(di)> d16;
    convertBlock(d16, dest, destStride, src, srcStride, width, height,
                 [&](auto v) { return hn::DemoteTo(d16, shiftSignMagnitude(v, (int)shift)); });
  }

  // rounds half away from zero, then saturates to int16
  void hwy_ht_scale_block16(int16_t* dest, size_t destStride, const int32_t* src, size_t srcStride,
                            uint32_t width, uint32_t height, float scale)
  {
    const hn::ScalableTag<int32_t> di;
    const hn::ScalableTag<float> df;
    const hn::Rebind<int16_t, decltype(di)> d16;
    convertBlock(d16, dest, destStride, src, srcStride, width, height, [&](auto v) {
      auto mag = hn::ConvertTo(df, hn::And(v, hn::Set(di, 0x7FFFFFFF)));
      auto rounded =
          hn::ConvertTo(di, hn::Add(hn::Mul(mag, hn::Set(df, scale)), hn::Set(df, 0.5f)));
      auto sign = hn::ShiftRight<31>(v);
      return hn::DemoteTo(d16, hn::Sub(hn::Xor(rounded, sign), sign));
    });
  }
} // namespace HWY_NAMESPACE
} // namespace grk::t1::ojph
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace grk::t1::ojph
{
HWY_EXPORT(hwy_ht_shift_block);
HWY_EXPORT(hwy_ht_scale_block);
HWY_EXPORT(hwy_ht_shift_block16);
HWY_EXPORT(hwy_ht_scale_block16);

void htShiftBlock(int32_t* dest, size_t destStride, const int32_t* src, size_t srcStride,
                  uint32_t width, uint32_t height, uint32_t shift)
{
  HWY_DYNAMIC_DISPATCH(hwy_ht_shift_block)(dest, destStride, src, srcStride, width, height, shift);
}
void htScaleBlock(float* dest, size_t destStride, const int32_t* src, size_t srcStride,
                  uint32_t width, uint32_t height, float scale)
{
  HWY_DYNAMIC_DISPATCH(hwy_ht_scale_block)(dest, destStride, src, srcStride, width, height, scale);
}
void htShiftBlock16(int16_t* dest, size_t destStride, const int32_t* src, size_t srcStride,
                    uint32_t width, uint32_t height, uint32_t shift)
{
  HWY_DYNAMIC_DISPATCH(hwy_ht_shift_block16)
  (dest, destStride, src, srcStride, width, height, shift);
}
void htScaleBlock16(int16_t* dest, size_t destStride, const int32_t* src, size_t srcStride,
                    uint32_t width, uint32_t height, float scale)
{
  HWY_DYNAMIC_DISPATCH(hwy_ht_scale_block16)
  (dest, destStride, src, srcStride, width, height, scale);
}
} // namespace grk::t1::ojph
#endif
