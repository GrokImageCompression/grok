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

#pragma once

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "grok.h"

namespace grk
{
/**
 * Linear per-pixel value remap of one component:
 *   out = round((in - src_min) * (dst_max - dst_min) / (src_max - src_min)) + dst_min,
 * clamped to [min(dst_min, dst_max), max(dst_min, dst_max)].
 *
 * Updates component->prec and component->sgnd to fit the dst range.
 * No-op if src_min == src_max (caller should validate).
 *
 * Returns true on success, false if rescale is ill-formed.
 */
template<typename T>
bool rescale_component(grk_image_comp* component, const grk_rescale& r)
{
  if(component == nullptr || component->data == nullptr)
    return false;
  if(r.src_max == r.src_min)
    return false;

  const double scale = (r.dst_max - r.dst_min) / (r.src_max - r.src_min);
  const double dst_lo = std::min(r.dst_min, r.dst_max);
  const double dst_hi = std::max(r.dst_min, r.dst_max);

  auto data = (T*)component->data;
  const uint32_t stride_diff = component->stride - component->w;
  size_t index = 0;
  for(uint32_t j = 0; j < component->h; ++j)
  {
    for(uint32_t i = 0; i < component->w; ++i)
    {
      double v = (double)data[index] - r.src_min;
      v = v * scale + r.dst_min;
      if(v < dst_lo)
        v = dst_lo;
      else if(v > dst_hi)
        v = dst_hi;
      if constexpr(std::is_floating_point_v<T>)
        data[index] = (T)v;
      else
        data[index] = (T)std::llround(v);
      index++;
    }
    index += stride_diff;
  }

  const bool sgnd = dst_lo < 0.0;
  // Smallest prec p such that the dst range is representable:
  //   unsigned: 2^p - 1 >= dst_hi
  //   signed:   2^(p-1) >= -dst_lo  AND  2^(p-1) - 1 >= dst_hi
  const int64_t neg_mag = sgnd ? (int64_t)std::ceil(-dst_lo) : 0;
  const int64_t pos_mag = (int64_t)std::ceil(std::max(0.0, dst_hi));
  uint8_t prec = 1;
  while(prec < GRK_MAX_SUPPORTED_IMAGE_PRECISION)
  {
    const int64_t max_pos = sgnd ? ((int64_t)1 << (prec - 1)) - 1 : ((int64_t)1 << prec) - 1;
    const int64_t max_neg_capacity = sgnd ? ((int64_t)1 << (prec - 1)) : 0;
    if(max_pos >= pos_mag && max_neg_capacity >= neg_mag)
      break;
    ++prec;
  }
  component->prec = prec;
  component->sgnd = sgnd;
  return true;
}

} // namespace grk
