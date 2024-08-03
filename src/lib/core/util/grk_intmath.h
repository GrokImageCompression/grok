/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

namespace grk
{
/**
 Divide an integer by another integer and round upwards
 @param  a integer of type T
 @param  b integer of type T
 @return a divided by b
 */
template<typename T>
uint32_t ceildiv(T a, T b)
{
   assert(b);
   return (uint32_t)((a + (uint64_t)b - 1) / b);
}

template<typename T>
T ceildivpow2(T a, uint32_t b)
{
   return (T)((a + ((uint64_t)1 << b) - 1) >> b);
}
/**
 Divide an unsigned integer by a power of 2 and round downwards
 @return a divided by 2^b
 */
static inline uint32_t floordivpow2(uint32_t a, uint32_t b)
{
   return a >> b;
}
/**
 Get logarithm of an integer and round downwards
 @param  a 32 bit integer
 @return log2(a)
 */

uint8_t floorlog2(uint32_t a);

/**
 Multiply two fixed-point numbers.
 @param  a N-bit precision fixed point number
 @param  b 13-bit precision fixed point number
 @return a * b in N-bit precision fixed point
 */
static inline int32_t fix_mul(int32_t a, int32_t b)
{
   int64_t temp = (int64_t)a * (int64_t)b;
   temp += 4096; // round by adding "0.5" in 13-bit fixed point
   assert((temp >> 13) <= (int64_t)0x7FFFFFFF);
   assert((temp >> 13) >= (-(int64_t)0x7FFFFFFF - (int64_t)1));

   // return to N-bit precision
   return (int32_t)(temp >> 13);
}
} // namespace grk
