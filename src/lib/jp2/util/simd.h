/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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

#ifdef _WIN32
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#ifdef __SSE__
#include <xmmintrin.h>
#endif
#ifdef __SSE2__
#include <emmintrin.h>
#endif
#ifdef __SSSE3__
#include <tmmintrin.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif
#endif

#ifdef __clang__
#define _mm_set_pd1 _mm_set1_pd
#define _mm_store_pd1 _mm_store1_pd
#define _mm_cvtsi64x_si128 _mm_cvtsi64_si128
#define _mm_cvtsi128_si64x _mm_cvtsi128_si64
#endif

#if defined(_MSC_VER)
static inline long grk_lrintf(float f)
{
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
}
#else
static inline long grk_lrintf(float f)
{
	return lrintf(f);
}
#endif
/* MSVC x86 is really bad at doing int64 = int32 * int32 on its own. Use intrinsic. */
#if defined(_MSC_VER) && (_MSC_VER >= 1400) && !defined(__INTEL_COMPILER) && defined(_M_IX86)
#pragma intrinsic(__emul)
#endif

#ifdef __AVX2__
/** Number of int32 values in an AVX2 register */
#define VREG_INT_COUNT 8
#else
/** Number of int32 values in an SSE2 register */
#define VREG_INT_COUNT 4
#endif

#if(defined(__SSE2__) || defined(__AVX2__))

/* Convenience macros to improve readability */
#if __AVX2__

#define VREG __m256i
#define LOAD_CST(x) _mm256_set1_epi32(x)
#define LOAD(x) _mm256_load_si256((const VREG*)(x))
#define LOADU(x) _mm256_loadu_si256((const VREG*)(x))
#define STORE(x, y) _mm256_store_si256((VREG*)(x), (y))
#define STOREU(x, y) _mm256_storeu_si256((VREG*)(x), (y))
#define ADD(x, y) _mm256_add_epi32((x), (y))
#define AND(x, y) _mm256_and_si256((x), (y));
#define SUB(x, y) _mm256_sub_epi32((x), (y))
#define VMAX(x, y) _mm256_max_epi32((x), (y))
#define VMIN(x, y) _mm256_min_epi32((x), (y))
#define SAR(x, y) _mm256_srai_epi32((x), (y))
#define MUL(x, y) _mm256_mullo_epi32((x), (y))

#define VREGF __m256
#define LOADF(x) _mm256_load_ps((float const*)(x))
#define LOADUF(x) _mm256_loadu_ps((float const*)(x))
#define LOAD_CST_F(x) _mm256_set1_ps(x)
#define ADDF(x, y) _mm256_add_ps((x), (y))
#define MULF(x, y) _mm256_mul_ps((x), (y))
#define SUBF(x, y) _mm256_sub_ps((x), (y))
#define VMAXF(x, y) _mm256_max_ps((x), (y))
#define VMINF(x, y) _mm256_min_ps((x), (y))
#define STOREF(x, y) _mm256_store_ps((float*)(x), (y))
#define STOREUF(x, y) _mm256_storeu_ps((float*)(x), (y))

#else

#define VREG __m128i
#define LOAD_CST(x) _mm_set1_epi32(x)
#define LOAD(x) _mm_load_si128((const VREG*)(x))
#define LOADU(x) _mm_loadu_si128((const VREG*)(x))
#define STORE(x, y) _mm_store_si128((VREG*)(x), (y))
#define STOREU(x, y) _mm_storeu_si128((VREG*)(x), (y))
#define ADD(x, y) _mm_add_epi32((x), (y))
#define AND(x, y) _mm_and_si128((x), (y));
#define SUB(x, y) _mm_sub_epi32((x), (y))
// !!! supported by SSE 4.1, not SSE 2
#define VMAX(x, y) _mm_max_epi32((x), (y))
// !!! supported by SSE 4.1, not SSE 2
#define VMIN(x, y) _mm_min_epi32((x), (y))

#define VREGF __m128
// !!! MUL is only valid for SSE 4.1, not SSE 2
#define MUL(x, y) _mm_mullo_epi32((x), (y))
#define SAR(x, y) _mm_srai_epi32((x), (y))
#define LOADF(x) _mm_load_ps((float const*)(x))
#define LOADUF(x) _mm_loadu_ps((float const*)(x))
#define LOAD_CST_F(x) _mm_set1_ps(x)
#define ADDF(x, y) _mm_add_ps((x), (y))
#define MULF(x, y) _mm_mul_ps((x), (y))
#define SUBF(x, y) _mm_sub_ps((x), (y))
#define VMAXF(x, y) _mm_max_ps((x), (y))
#define VMINF(x, y) _mm_min_ps((x), (y))
#define STOREF(x, y) _mm_store_ps((float*)(x), (y))
#define STOREUF(x, y) _mm_storeu_ps((float*)(x), (y))

#endif

#define ADD3(x, y, z) ADD(ADD(x, y), z)
// !!! supported by SSE 4.1 or AVX
#define VCLAMP(x, min, max) VMIN(VMAX(x, min), max)

#define VCLAMPF(x, min, max) VMINF(VMAXF(x, min), max)

#endif
