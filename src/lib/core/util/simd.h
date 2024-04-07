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
 */
#pragma once

#include <cmath>
#include <cstdint>

#if defined(__arm64__) || defined(__arm__)
#include <arm_acle.h>
#if defined(__ARM_NEON__)
#include <arm_neon.h>
#endif
#elif defined(_WIN32)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#ifdef __SSE__
#include <xmmintrin.h>
#endif
#ifdef __SSE2__
#include <emmintrin.h>
#endif
#endif

static inline long grk_lrintf(float f)
{
#if defined(_MSC_VER)
#ifdef _M_X64
   return _mm_cvt_ss2si(_mm_load_ss(&f));
#elif defined(_M_IX86)
   int i;
   _asm {
        fld f
        fistp i
   }
   ;
   return i;
#else
   return (long)((f > 0.0f) ? (f + 0.5f) : (f - 0.5f));
#endif
#else
   return lrintf(f);
#endif
}

#if defined(_MSC_VER) && (_MSC_VER >= 1400) && !defined(__INTEL_COMPILER) && defined(_M_IX86)
#pragma intrinsic(__emul)
#endif

static inline uint32_t grk_population_count(uint32_t val)
{
#ifdef _MSC_VER
   return (uint32_t)__popcnt(val);
#else
   return (uint32_t)__builtin_popcount(val);
#endif
}
