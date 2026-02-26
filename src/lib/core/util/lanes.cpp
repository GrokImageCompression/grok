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

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "lanes.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();

namespace grok
{

namespace HWY_NAMESPACE
{

  uint32_t num_lanes(void)
  {
    const HWY_FULL(int32_t) di;
    return (uint32_t)Lanes(di);
  }

} // namespace HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

HWY_EXPORT(num_lanes);

uint32_t NumLanes(void)
{
  static uint32_t lanes = HWY_DYNAMIC_DISPATCH(num_lanes)();

  return lanes;
}

#endif // HWY_ONCE

} // namespace grok