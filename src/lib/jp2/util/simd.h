/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
 * Copyright (c) 2007, Jonathan Ballard <dzonatas@dzonux.net>
 * Copyright (c) 2007, Callum Lerwick <seg@haxxed.com>
 * Copyright (c) 2017, IntoPIX SA <support@intopix.com>
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

#define GRK_SKIP_POISON
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


#ifdef __AVX2__
/** Number of int32 values in a AVX2 register */
#define VREG_INT_COUNT       8
#else
/** Number of int32 values in a SSE2 register */
#define VREG_INT_COUNT       4
#endif



#if (defined(__SSE2__) || defined(__AVX2__))

/* Convenience macros to improve the readability of the formulas */
#if __AVX2__
#define VREG        __m256i
#define LOAD_CST(x) _mm256_set1_epi32(x)
#define LOAD(x)     _mm256_load_si256((const VREG*)(x))
#define LOADU(x)    _mm256_loadu_si256((const VREG*)(x))
#define STORE(x,y)  _mm256_store_si256((VREG*)(x),(y))
#define STOREU(x,y) _mm256_storeu_si256((VREG*)(x),(y))
#define ADD(x,y)    _mm256_add_epi32((x),(y))
#define SUB(x,y)    _mm256_sub_epi32((x),(y))
#define SAR(x,y)    _mm256_srai_epi32((x),(y))
#define VREGF        __m256
#define LOADF(x)     _mm256_load_ps((float const*)(x))
#define LOAD_CST_F(x)      _mm256_set1_ps(x)
#define ADDF(x,y)    _mm256_add_ps((x),(y))
#define MULF(x,y)    _mm256_mul_ps((x),(y))
#define SUBF(x,y)     _mm256_sub_ps((x),(y))
#define STOREF(x,y)  _mm256_store_ps((float*)(x),(y))
#else
#define VREG        __m128i
#define LOAD_CST(x) _mm_set1_epi32(x)
#define LOAD(x)     _mm_load_si128((const VREG*)(x))
#define LOADU(x)    _mm_loadu_si128((const VREG*)(x))
#define STORE(x,y)  _mm_store_si128((VREG*)(x),(y))
#define STOREU(x,y) _mm_storeu_si128((VREG*)(x),(y))
#define ADD(x,y)    _mm_add_epi32((x),(y))
#define SUB(x,y)    _mm_sub_epi32((x),(y))
#define SAR(x,y)    _mm_srai_epi32((x),(y))
#define VREGF        __m128
#define LOADF(x)     _mm_load_ps((float const*)(x))
#define LOAD_CST_F(x)      _mm_set1_ps(x)
#define ADDF(x,y)    _mm_add_ps((x),(y))
#define MULF(x,y)    _mm_mul_ps((x),(y))
#define SUBF(x,y)    _mm_sub_ps((x),(y))
#define STOREF(x,y)  _mm_store_ps((float*)(x),(y))
#endif

#endif
