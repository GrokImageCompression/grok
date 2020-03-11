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
 * Copyright (c) 2008, 2011-2012, Centre National d'Etudes Spatiales (CNES), FR
 * Copyright (c) 2012, CS Systemes d'Information, France
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

#include "CPUArch.h"
#include "grok_includes.h"

namespace grk {

/* <summary> */
/* This table contains the norms of the basis function of the reversible MCT. */
/* </summary> */
static const double mct_norms_rev[3] = { 1.732, .8292, .8292 };

/* <summary> */
/* This table contains the norms of the basis function of the irreversible MCT. */
/* </summary> */
static const double mct_norms_irrev[3] = { 1.732, 1.805, 1.573 };

const double* mct::get_norms_rev() {
	return mct_norms_rev;
}
const double* mct::get_norms_irrev() {
	return mct_norms_irrev;
}


/* <summary> */
/* Forward reversible MCT. */
/* </summary> */
void mct::encode_rev(int32_t *restrict chan0, int32_t *restrict chan1,
		int32_t *restrict chan2, uint64_t n) {
	size_t i = 0;

#ifdef __AVX2__
    size_t chunkSize = n / Scheduler::g_tp->num_threads();
    //ensure it is divisible by 8
    chunkSize = (chunkSize/8) * 8;
	if (chunkSize > 8) {
	    std::vector< std::future<int> > results;
	    for(uint64_t i = 0; i < Scheduler::g_tp->num_threads(); ++i) {
	    	uint64_t index = i;
	        results.emplace_back(
	            Scheduler::g_tp->enqueue([index, chunkSize, chan0,chan1,chan2] {
	        		uint64_t begin = (uint64_t)index * chunkSize;
					for (auto j = begin; j < begin+chunkSize; j+=8 ){
						__m256i y, u, v;
						__m256i r = _mm256_load_si256((const __m256i*) &chan0[j]);
						__m256i g = _mm256_load_si256((const __m256i*) &chan1[j]);
						__m256i b = _mm256_load_si256((const __m256i*) &chan2[j]);
						y = _mm256_add_epi32(g, g);
						y = _mm256_add_epi32(y, b);
						y = _mm256_add_epi32(y, r);
						y = _mm256_srai_epi32(y, 2);
						u = _mm256_sub_epi32(b, g);
						v = _mm256_sub_epi32(r, g);
						_mm256_store_si256((__m256i*) &chan0[j], y);
						_mm256_store_si256((__m256i*) &chan1[j], u);
						_mm256_store_si256((__m256i*) &chan2[j], v);
					}
	                return 0;
	            })
	        );
	    }
	    for(auto && result: results){
	        result.get();
	    }
		i = chunkSize * Scheduler::g_tp->num_threads();
	}
#elif __SSE2__
    size_t chunkSize = n / Scheduler::g_tp->num_threads();
    //ensure it is divisible by 4
    chunkSize = (chunkSize/4) * 4;
	if (chunkSize > 4) {
	    std::vector< std::future<int> > results;
	    for(uint64_t i = 0; i < Scheduler::g_tp->num_threads(); ++i) {
	    	uint64_t index = i;
	        results.emplace_back(
	            Scheduler::g_tp->enqueue([index, chunkSize, chan0,chan1,chan2] {
	        		uint64_t begin = (uint64_t)index * chunkSize;
					for (auto j = begin; j < begin+chunkSize; j+=4 ){
						__m128i y, u, v;
						__m128i r = _mm_load_si128((const __m128i*) &chan0[j]);
						__m128i g = _mm_load_si128((const __m128i*) &chan1[j]);
						__m128i b = _mm_load_si128((const __m128i*) &chan2[j]);
						y = _mm_add_epi32(g, g);
						y = _mm_add_epi32(y, b);
						y = _mm_add_epi32(y, r);
						y = _mm_srai_epi32(y, 2);
						u = _mm_sub_epi32(b, g);
						v = _mm_sub_epi32(r, g);
						_mm_store_si128((__m128i*) &chan0[j], y);
						_mm_store_si128((__m128i*) &chan1[j], u);
						_mm_store_si128((__m128i*) &chan2[j], v);
					}
	                return 0;
	            })
	        );
	    }
	    for(auto && result: results){
	        result.get();
	    }
		i = chunkSize * Scheduler::g_tp->num_threads();
	}
#endif
	for (; i < n; ++i) {
		int32_t r = chan0[i];
		int32_t g = chan1[i];
		int32_t b = chan2[i];
		int32_t y = (r + (g * 2) + b) >> 2;
		int32_t u = b - g;
		int32_t v = r - g;
		chan0[i] = y;
		chan1[i] = u;
		chan2[i] = v;
	}
}

////////////////////////////////////////////////////////////////////////////////

/* <summary> */
/* Inverse reversible MCT. */
/* </summary> */
void mct::decode_rev(int32_t *restrict chan0, int32_t *restrict chan1,
		int32_t *restrict chan2, uint64_t n) {
	size_t i = 0;
#ifdef __AVX2__
    size_t chunkSize = n / Scheduler::g_tp->num_threads();
    //ensure it is divisible by 8
    chunkSize = (chunkSize/8) * 8;
	if (chunkSize > 8) {
	    std::vector< std::future<int> > results;
	    for(uint64_t i = 0; i < Scheduler::g_tp->num_threads(); ++i) {
	    	uint64_t index = i;
	        results.emplace_back(
	            Scheduler::g_tp->enqueue([index, chunkSize,chan0,chan1,chan2] {
					uint64_t begin = (uint64_t)index * chunkSize;
					for (auto j = begin; j < begin+chunkSize; j+=8 ){
						__m256i r, g, b;
						__m256i y = _mm256_load_si256((const __m256i*) &(chan0[j]));
						__m256i u = _mm256_load_si256((const __m256i*) &(chan1[j]));
						__m256i v = _mm256_load_si256((const __m256i*) &(chan2[j]));
						g = y;
						g = _mm256_sub_epi32(g, _mm256_srai_epi32(_mm256_add_epi32(u, v), 2));
						r = _mm256_add_epi32(v, g);
						b = _mm256_add_epi32(u, g);
						_mm256_store_si256((__m256i*) &(chan0[j]), r);
						_mm256_store_si256((__m256i*) &(chan1[j]), g);
						_mm256_store_si256((__m256i*) &(chan2[j]), b);
					}
					return 0;
	            })
	        );
	    }
	    for(auto && result: results){
	        result.get();
	    }
		i = chunkSize * Scheduler::g_tp->num_threads();
	}

#elif __SSE2__
    size_t chunkSize = n / Scheduler::g_tp->num_threads();
    //ensure it is divisible by 4
    chunkSize = (chunkSize/4) * 4;
	if (chunkSize > 4) {
	    std::vector< std::future<int> > results;
	    for(uint64_t i = 0; i < Scheduler::g_tp->num_threads(); ++i) {
	    	uint64_t index = i;
	        results.emplace_back(
	            Scheduler::g_tp->enqueue([index, chunkSize,chan0,chan1,chan2] {
					uint64_t begin = (uint64_t)index * chunkSize;
					for (auto j = begin; j < begin+chunkSize; j+=4 ){
						__m128i r, g, b;
						__m128i y = _mm_load_si128((const __m128i*) &(chan0[j]));
						__m128i u = _mm_load_si128((const __m128i*) &(chan1[j]));
						__m128i v = _mm_load_si128((const __m128i*) &(chan2[j]));
						g = y;
						g = _mm_sub_epi32(g, _mm_srai_epi32(_mm_add_epi32(u, v), 2));
						r = _mm_add_epi32(v, g);
						b = _mm_add_epi32(u, g);
						_mm_store_si128((__m128i*) &(chan0[j]), r);
						_mm_store_si128((__m128i*) &(chan1[j]), g);
						_mm_store_si128((__m128i*) &(chan2[j]), b);
					}
					return 0;
	            })
	        );
	    }
	    for(auto && result: results){
	        result.get();
	    }
		i = chunkSize * Scheduler::g_tp->num_threads();
	}
#endif
	for (; i < n; ++i) {
		int32_t y = chan0[i];
		int32_t u = chan1[i];
		int32_t v = chan2[i];
		int32_t g = y - ((u + v) >> 2);
		int32_t r = v + g;
		int32_t b = u + g;
		chan0[i] = r;
		chan1[i] = g;
		chan2[i] = b;
	}
}
/* <summary> */
/* Forward irreversible MCT. */
/* </summary> */
void mct::encode_irrev( int32_t* restrict chan0,
						int32_t* restrict chan1,
						int32_t* restrict chan2,
						uint64_t n)
{
    size_t i = 0;
#ifdef __SSE4_1__
    const __m128i ry = _mm_set1_epi32(2449);
    const __m128i gy = _mm_set1_epi32(4809);
    const __m128i by = _mm_set1_epi32(934);
    const __m128i ru = _mm_set1_epi32(1382);
    const __m128i gu = _mm_set1_epi32(2714);
    const __m128i gv = _mm_set1_epi32(3430);
    const __m128i bv = _mm_set1_epi32(666);
    const __m128i mulround = _mm_shuffle_epi32(_mm_cvtsi32_si128(4096), _MM_SHUFFLE(1, 0, 1, 0));

    size_t chunkSize = n / Scheduler::g_tp->num_threads();
    //ensure it is divisible by 4
    chunkSize = (chunkSize/4) * 4;
	if (chunkSize > 4) {

		std::vector< std::future<int> > results;
		for(size_t k = 0; k < Scheduler::g_tp->num_threads(); ++k) {
			uint64_t index = k;
			results.emplace_back(
				Scheduler::g_tp->enqueue([index, chunkSize, chan0,chan1,chan2,
											 ry,gy,by,ru,gu,gv,bv,
											 mulround] {

				uint64_t begin = (uint64_t)index * chunkSize;
				for (auto j = begin; j < begin+chunkSize; j+=4 ){
					__m128i lo, hi;
					__m128i y, u, v;
					__m128i r = _mm_load_si128((const __m128i *)&(chan0[j]));
					__m128i g = _mm_load_si128((const __m128i *)&(chan1[j]));
					__m128i b = _mm_load_si128((const __m128i *)&(chan2[j]));

					lo = r;
					hi = _mm_shuffle_epi32(r, _MM_SHUFFLE(3, 3, 1, 1));
					lo = _mm_mul_epi32(lo, ry);
					hi = _mm_mul_epi32(hi, ry);
					lo = _mm_add_epi64(lo, mulround);
					hi = _mm_add_epi64(hi, mulround);
					lo = _mm_srli_epi64(lo, 13);
					hi = _mm_slli_epi64(hi, 32-13);
					y = _mm_blend_epi16(lo, hi, 0xCC);

					lo = g;
					hi = _mm_shuffle_epi32(g, _MM_SHUFFLE(3, 3, 1, 1));
					lo = _mm_mul_epi32(lo, gy);
					hi = _mm_mul_epi32(hi, gy);
					lo = _mm_add_epi64(lo, mulround);
					hi = _mm_add_epi64(hi, mulround);
					lo = _mm_srli_epi64(lo, 13);
					hi = _mm_slli_epi64(hi, 32-13);
					y = _mm_add_epi32(y, _mm_blend_epi16(lo, hi, 0xCC));

					lo = b;
					hi = _mm_shuffle_epi32(b, _MM_SHUFFLE(3, 3, 1, 1));
					lo = _mm_mul_epi32(lo, by);
					hi = _mm_mul_epi32(hi, by);
					lo = _mm_add_epi64(lo, mulround);
					hi = _mm_add_epi64(hi, mulround);
					lo = _mm_srli_epi64(lo, 13);
					hi = _mm_slli_epi64(hi, 32-13);
					y = _mm_add_epi32(y, _mm_blend_epi16(lo, hi, 0xCC));
					_mm_store_si128((__m128i *)&(chan0[j]), y);

					lo = _mm_cvtepi32_epi64(_mm_shuffle_epi32(b, _MM_SHUFFLE(3, 2, 2, 0)));
					hi = _mm_cvtepi32_epi64(_mm_shuffle_epi32(b, _MM_SHUFFLE(3, 2, 3, 1)));
					lo = _mm_slli_epi64(lo, 12);
					hi = _mm_slli_epi64(hi, 12);
					lo = _mm_add_epi64(lo, mulround);
					hi = _mm_add_epi64(hi, mulround);
					lo = _mm_srli_epi64(lo, 13);
					hi = _mm_slli_epi64(hi, 32-13);
					u = _mm_blend_epi16(lo, hi, 0xCC);

					lo = r;
					hi = _mm_shuffle_epi32(r, _MM_SHUFFLE(3, 3, 1, 1));
					lo = _mm_mul_epi32(lo, ru);
					hi = _mm_mul_epi32(hi, ru);
					lo = _mm_add_epi64(lo, mulround);
					hi = _mm_add_epi64(hi, mulround);
					lo = _mm_srli_epi64(lo, 13);
					hi = _mm_slli_epi64(hi, 32-13);
					u = _mm_sub_epi32(u, _mm_blend_epi16(lo, hi, 0xCC));

					lo = g;
					hi = _mm_shuffle_epi32(g, _MM_SHUFFLE(3, 3, 1, 1));
					lo = _mm_mul_epi32(lo, gu);
					hi = _mm_mul_epi32(hi, gu);
					lo = _mm_add_epi64(lo, mulround);
					hi = _mm_add_epi64(hi, mulround);
					lo = _mm_srli_epi64(lo, 13);
					hi = _mm_slli_epi64(hi, 32-13);
					u = _mm_sub_epi32(u, _mm_blend_epi16(lo, hi, 0xCC));
					_mm_store_si128((__m128i *)&(chan1[j]), u);

					lo = _mm_cvtepi32_epi64(_mm_shuffle_epi32(r, _MM_SHUFFLE(3, 2, 2, 0)));
					hi = _mm_cvtepi32_epi64(_mm_shuffle_epi32(r, _MM_SHUFFLE(3, 2, 3, 1)));
					lo = _mm_slli_epi64(lo, 12);
					hi = _mm_slli_epi64(hi, 12);
					lo = _mm_add_epi64(lo, mulround);
					hi = _mm_add_epi64(hi, mulround);
					lo = _mm_srli_epi64(lo, 13);
					hi = _mm_slli_epi64(hi, 32-13);
					v = _mm_blend_epi16(lo, hi, 0xCC);

					lo = g;
					hi = _mm_shuffle_epi32(g, _MM_SHUFFLE(3, 3, 1, 1));
					lo = _mm_mul_epi32(lo, gv);
					hi = _mm_mul_epi32(hi, gv);
					lo = _mm_add_epi64(lo, mulround);
					hi = _mm_add_epi64(hi, mulround);
					lo = _mm_srli_epi64(lo, 13);
					hi = _mm_slli_epi64(hi, 32-13);
					v = _mm_sub_epi32(v, _mm_blend_epi16(lo, hi, 0xCC));

					lo = b;
					hi = _mm_shuffle_epi32(b, _MM_SHUFFLE(3, 3, 1, 1));
					lo = _mm_mul_epi32(lo, bv);
					hi = _mm_mul_epi32(hi, bv);
					lo = _mm_add_epi64(lo, mulround);
					hi = _mm_add_epi64(hi, mulround);
					lo = _mm_srli_epi64(lo, 13);
					hi = _mm_slli_epi64(hi, 32-13);
					v = _mm_sub_epi32(v, _mm_blend_epi16(lo, hi, 0xCC));
					_mm_store_si128((__m128i *)&(chan2[j]), v);

				}
				return 0;
				})
			);
		}
		for(auto && result: results){
			result.get();
		}
		i = Scheduler::g_tp->num_threads() * chunkSize;
	}
#endif
    for(; i < n; ++i) {
        int32_t r = chan0[i];
        int32_t g = chan1[i];
        int32_t b = chan2[i];
        int32_t y =  int_fix_mul(r, 2449) + int_fix_mul(g, 4809) + int_fix_mul(b, 934);
        int32_t u = -int_fix_mul(r, 1382) - int_fix_mul(g, 2714) + int_fix_mul(b, 4096);
        int32_t v =  int_fix_mul(r, 4096) - int_fix_mul(g, 3430) - int_fix_mul(b, 666);
        chan0[i] = y;
        chan1[i] = u;
        chan2[i] = v;
    }
}

/* <summary> */
/* Inverse irreversible MCT. */
/* </summary> */
void mct::decode_irrev(float *restrict c0, float *restrict c1, float *restrict c2,
		uint64_t n) {
	uint64_t i = 0;
#ifdef __AVX2__
	size_t chunkSize = n / Scheduler::g_tp->num_threads();
	//ensure it is divisible by 8
	chunkSize = (chunkSize/8) * 8;
	if (chunkSize > 8) {
		std::vector< std::future<int> > results;
		for(uint64_t i = 0; i < Scheduler::g_tp->num_threads(); ++i) {
			uint64_t index = i;
			results.emplace_back(
				Scheduler::g_tp->enqueue([index, chunkSize, c0,c1,c2] {
				const __m256 vrv = _mm256_set1_ps(1.402f);
				const __m256 vgu = _mm256_set1_ps(0.34413f);
				const __m256 vgv = _mm256_set1_ps(0.71414f);
				const __m256 vbu = _mm256_set1_ps(1.772f);
				uint64_t begin = (uint64_t)index * chunkSize;
				for (auto j = begin; j < begin+chunkSize; j +=8){
					__m256 vy, vu, vv;
					__m256 vr, vg, vb;

					vy = _mm256_load_ps(c0 + j);
					vu = _mm256_load_ps(c1 + j);
					vv = _mm256_load_ps(c2 + j);
					vr = _mm256_add_ps(vy, _mm256_mul_ps(vv, vrv));
					vg = _mm256_sub_ps(_mm256_sub_ps(vy, _mm256_mul_ps(vu, vgu)),
							_mm256_mul_ps(vv, vgv));
					vb = _mm256_add_ps(vy, _mm256_mul_ps(vu, vbu));
					_mm256_store_ps(c0 + j, vr);
					_mm256_store_ps(c1 + j, vg);
					_mm256_store_ps(c2 + j, vb);
				}
				return 0;
				})
			);
		}
		for(auto && result: results){
			result.get();
		}
		i = chunkSize * Scheduler::g_tp->num_threads();
	}
#elif __SSE__
	__m128 vrv, vgu, vgv, vbu;
	vrv = _mm_set1_ps(1.402f);
	vgu = _mm_set1_ps(0.34413f);
	vgv = _mm_set1_ps(0.71414f);
	vbu = _mm_set1_ps(1.772f);

    size_t chunkSize = n / Scheduler::g_tp->num_threads();
    //ensure it is divisible by 8
    chunkSize = (chunkSize/8) * 8;
	if (chunkSize > 8) {
	    std::vector< std::future<int> > results;
	    for(uint64_t i = 0; i < Scheduler::g_tp->num_threads(); ++i) {
	    	uint64_t index = i;
	        results.emplace_back(
	            Scheduler::g_tp->enqueue([index, chunkSize, c0,c1,c2,vrv,vgu,vgv,vbu] {
				uint64_t begin = (uint64_t)index * chunkSize;
				for (auto j = begin; j < begin+chunkSize;){
					__m128 vy, vu, vv;
					__m128 vr, vg, vb;

					vy = _mm_load_ps(c0 + j);
					vu = _mm_load_ps(c1 + j);
					vv = _mm_load_ps(c2 + j);
					vr = _mm_add_ps(vy, _mm_mul_ps(vv, vrv));
					vg = _mm_sub_ps(_mm_sub_ps(vy, _mm_mul_ps(vu, vgu)),
							_mm_mul_ps(vv, vgv));
					vb = _mm_add_ps(vy, _mm_mul_ps(vu, vbu));
					_mm_store_ps(c0 + j, vr);
					_mm_store_ps(c1 + j, vg);
					_mm_store_ps(c2 + j, vb);

					j+=4;

					vy = _mm_load_ps(c0+j);
					vu = _mm_load_ps(c1+j);
					vv = _mm_load_ps(c2+j);
					vr = _mm_add_ps(vy, _mm_mul_ps(vv, vrv));
					vg = _mm_sub_ps(_mm_sub_ps(vy, _mm_mul_ps(vu, vgu)),
							_mm_mul_ps(vv, vgv));
					vb = _mm_add_ps(vy, _mm_mul_ps(vu, vbu));
					_mm_store_ps(c0+j, vr);
					_mm_store_ps(c1+j, vg);
					_mm_store_ps(c2+j, vb);

					j +=4;
				}
                return 0;
	            })
	        );
	    }
	    for(auto && result: results){
	        result.get();
	    }
		i = chunkSize * Scheduler::g_tp->num_threads();
	}
#endif
	for (; i < n; ++i) {
		float y = c0[i];
		float u = c1[i];
		float v = c2[i];
		float r = y + (v * 1.402f);
		float g = y - (u * 0.34413f) - (v * (0.71414f));
		float b = y + (u * 1.772f);
		c0[i] = r;
		c1[i] = g;
		c2[i] = b;
	}
}

//////////////////////////////////////////////////////////////////////////////


void mct::calculate_norms(double *pNorms, uint32_t pNbComps, float *pMatrix) {
	uint32_t i, j, lIndex;
	float lCurrentValue;
	double *lNorms = (double*) pNorms;
	float *lMatrix = (float*) pMatrix;

	for (i = 0; i < pNbComps; ++i) {
		lNorms[i] = 0;
		lIndex = i;

		for (j = 0; j < pNbComps; ++j) {
			lCurrentValue = lMatrix[lIndex];
			lIndex += pNbComps;
			lNorms[i] += lCurrentValue * lCurrentValue;
		}
		lNorms[i] = sqrt(lNorms[i]);
	}
}

bool mct::encode_custom(uint8_t *pCodingdata, uint64_t n, uint8_t **pData,
		uint32_t pNbComp, uint32_t isSigned) {
	float *lMct = (float*) pCodingdata;
	uint64_t i;
	uint32_t j;
	uint32_t k;
	uint32_t lNbMatCoeff = pNbComp * pNbComp;
	int32_t *lCurrentData = nullptr;
	int32_t *lCurrentMatrix = nullptr;
	int32_t **lData = (int32_t**) pData;
	uint32_t lMultiplicator = 1 << 13;
	int32_t *lMctPtr;

	ARG_NOT_USED(isSigned);

	lCurrentData = (int32_t*) grok_malloc(
			(pNbComp + lNbMatCoeff) * sizeof(int32_t));
	if (!lCurrentData) {
		return false;
	}

	lCurrentMatrix = lCurrentData + pNbComp;

	for (i = 0; i < lNbMatCoeff; ++i) {
		lCurrentMatrix[i] = (int32_t) (*(lMct++) * (float) lMultiplicator);
	}

	for (i = 0; i < n; ++i) {
		lMctPtr = lCurrentMatrix;
		for (j = 0; j < pNbComp; ++j) {
			lCurrentData[j] = (*(lData[j]));
		}

		for (j = 0; j < pNbComp; ++j) {
			*(lData[j]) = 0;
			for (k = 0; k < pNbComp; ++k) {
				*(lData[j]) += int_fix_mul(*lMctPtr, lCurrentData[k]);
				++lMctPtr;
			}

			++lData[j];
		}
	}
	grok_free(lCurrentData);

	return true;
}

bool mct::decode_custom(uint8_t *pDecodingData, uint64_t n, uint8_t **pData,
		uint32_t pNbComp, uint32_t isSigned) {
	float *lMct;
	uint64_t i;
	uint32_t j;
	uint32_t k;

	float *lCurrentData = nullptr;
	float *lCurrentResult = nullptr;
	float **lData = (float**) pData;

	ARG_NOT_USED(isSigned);

	lCurrentData = (float*) grok_malloc(2 * pNbComp * sizeof(float));
	if (!lCurrentData) {
		return false;
	}
	lCurrentResult = lCurrentData + pNbComp;

	for (i = 0; i < n; ++i) {
		lMct = (float*) pDecodingData;
		for (j = 0; j < pNbComp; ++j) {
			lCurrentData[j] = (float) (*(lData[j]));
		}
		for (j = 0; j < pNbComp; ++j) {
			lCurrentResult[j] = 0;
			for (k = 0; k < pNbComp; ++k) {
				lCurrentResult[j] += *(lMct++) * lCurrentData[k];
			}
			*(lData[j]++) = (float) (lCurrentResult[j]);
		}
	}
	grok_free(lCurrentData);
	return true;
}

}
