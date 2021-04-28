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
#if defined(_MSC_VER) && (_MSC_VER >= 1400) && !defined(__INTEL_COMPILER) && defined(_M_IX86)
#pragma intrinsic(__emul)
#endif
