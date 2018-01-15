/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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
*    This source code incorporates work covered by the following copyright and
*    permission notice:
*
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

namespace grk {

/**
 Get the saturated sum of two unsigned integers
 @param  a integer
 @param  b integer
 @return saturated sum of a+b
 */
static inline uint32_t uint_adds(uint32_t a, uint32_t b){
    uint64_t sum = (uint64_t)a + (uint64_t)b;
    return (uint32_t)(-(int32_t)(sum >> 32)) | (uint32_t)sum;
}
/**
Clamp an integer inside an interval
@param  a integer
@param  b integer
@param  max clamp max
@return a if (min < a < max), max if (a > max) or  min if (a < min)
*/
static inline int32_t int_clamp(int32_t a, int32_t min, int32_t max){
    if (a < min)
        return min;
    if (a > max)
        return max;
    return a;
}
/**
Divide an integer by another integer and round upwards
@param  a integer of type T
@param  b integer of type T
@return a divided by b
*/
template <typename T> uint32_t  ceildiv(T  a, T  b){
    assert(b);
    return (uint32_t)((a + (uint64_t)b - 1) / b);
}
/**
 Divide a 64-bit integer by a power of 2 and round upwards
 @param  a 64-bit integer
 @param  b power of two
 @return a divided by 2^b
 */
static inline int64_t int64_ceildivpow2(int64_t a, uint32_t b){
    return (int64_t)((a + ((int64_t)1 << b) - 1) >> b);
}
/**
Divide a 64-bit integer by a power of 2 and round upwards
@param  a 64-bit integer
@param  b power of two
@return a divided by 2^b
*/
static inline uint32_t uint64_ceildivpow2(uint64_t a, uint32_t b){
	return (uint32_t)((a + ((uint64_t)1 << b) - 1) >> b);
}
/**
 Divide an integer by a power of 2 and round upwards
 @param  a unsigned integer
 @param  b power of two
 @return a divided by 2^b
 */
static inline uint32_t uint_ceildivpow2(uint32_t a, uint32_t b)
{
    return (uint32_t)((a + ((uint64_t)1U << b) - 1U) >> b);
}
/**
Divide an integer by a power of 2 and round downwards
@param  a integer
@param  b power of two
@return a divided by 2^b
*/
static inline int32_t int_floordivpow2(int32_t a, int32_t b)
{
    return a >> b;
}
/**
Divide an unsigned integer by a power of 2 and round downwards
@return a divided by 2^b
*/
static inline int32_t uint_floordivpow2(uint32_t a, uint32_t b){
	return a >> b;
}
/**
Get logarithm of an integer and round downwards
@param  a 32 bit integer
@return log2(a)
*/
static inline int32_t int_floorlog2(int32_t a){
    int32_t l;
    for (l = 0; a > 1; l++) {
        a >>= 1;
    }
    return l;
}
/**
Get logarithm of an integer and round downwards
@param  a 32 bit integer
@return log2(a)
*/
static inline uint32_t  uint_floorlog2(uint32_t  a){
    uint32_t  l;
    for (l = 0; a > 1; ++l) {
        a >>= 1;
    }
    return l;
}
/**
Multiply two fixed-point numbers.
@param  a N-bit precision fixed point number
@param  b 13-bit precision fixed point number
@return a * b
*/
static inline int32_t int_fix_mul(int32_t a, int32_t b){
#if defined(_MSC_VER) && (_MSC_VER >= 1400) && !defined(__INTEL_COMPILER) && defined(_M_IX86)
    int64_t temp = __emul(a, b);
#else
    int64_t temp = (int64_t) a * (int64_t) b ;
#endif
    temp += 4096;	//round by adding "0.5" in 13-bit fixed point
    assert((temp >> 13) <= (int64_t)0x7FFFFFFF);
    assert((temp >> 13) >= (-(int64_t)0x7FFFFFFF - (int64_t)1));

	// return to N-bit precision
    return (int32_t) (temp >> 13);
}
}