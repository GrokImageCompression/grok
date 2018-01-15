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
 * Copyright (c) 2007, Jonathan Ballard <dzonatas@dzonux.net>
 * Copyright (c) 2007, Callum Lerwick <seg@haxxed.com>
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
#include "Barrier.h"
#include "T1Decoder.h"
#include <atomic>
#include "testing.h"

namespace grk {

#define GROK_S(i) a[(i)<<1]
#define GROK_D(i) a[(1+((i)<<1))]
#define GROK_S_(i) ((i)<0?GROK_S(0):((i)>=s_n?GROK_S(s_n-1):GROK_S(i)))
#define GROK_D_(i) ((i)<0?GROK_D(0):((i)>=d_n?GROK_D(d_n-1):GROK_D(i)))
#define GROK_SS_(i) ((i)<0?GROK_S(0):((i)>=d_n?GROK_S(d_n-1):GROK_S(i)))
#define GROK_DD_(i) ((i)<0?GROK_D(0):((i)>=s_n?GROK_D(s_n-1):GROK_D(i)))

int64_t dwt97_t::bufferShiftEven() {
	return -interleaved_offset + odd_top_left_bit;
}
int64_t dwt97_t::bufferShiftOdd() {
	return  -interleaved_offset + (odd_top_left_bit ^ 1);
}

static const float dwt_alpha = 1.586134342f; /*  12994 */
static const float dwt_beta = 0.052980118f; /*    434 */
static const float dwt_gamma = -0.882911075f; /*  -7233 */
static const float dwt_delta = -0.443506852f; /*  -3633 */

static const float dwt_K = 1.230174105f; /*  10078 */
static const float dwt_c13318 = 1.625732422f;


/***************************************************************************************

9/7 Synthesis Wavelet Transform

*****************************************************************************************/

#ifdef __SSE__
static void v4dwt_decode_step1_sse(dwt_v4_t* w, uint32_t count, const __m128 c);

static void v4dwt_decode_step2_sse(dwt_v4_t* l, dwt_v4_t* w, uint32_t k, uint32_t m, __m128 c);

#else
static void v4dwt_decode_step1(dwt_v4_t* w, uint32_t count, const float c);

static void v4dwt_decode_step2(dwt_v4_t* l, dwt_v4_t* w, uint32_t k, uint32_t m, float c);

#endif


/* <summary>                             */
/* Inverse 9-7 wavelet transform in 2-D. */
/* </summary>                            */
bool dwt97::decode(tcd_tilecomp_t* restrict tilec,
	uint32_t numres,
	uint32_t numThreads)
{
	if (numres == 1U) {
		return true;
	}
	if (tile_buf_is_decode_region(tilec->buf))
		return region_decode(tilec, numres, numThreads);

	int rc = 0;
	auto tileBuf = (float*)tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0);
	Barrier decode_dwt_barrier(numThreads);
	Barrier decode_dwt_calling_barrier(numThreads + 1);
	std::vector<std::thread> dwtWorkers;
	for (auto threadId = 0U; threadId < numThreads; threadId++) {
		dwtWorkers.push_back(std::thread([this,
											tilec,
											numres,
											&rc,
											tileBuf,
											&decode_dwt_barrier,
											&decode_dwt_calling_barrier,
											threadId,
											numThreads]()		{
			auto numResolutions = numres;
			v4dwt_t h;
			v4dwt_t v;
			tcd_resolution_t* res = tilec->resolutions;

			uint32_t rw = (res->x1 - res->x0);	/* width of the resolution level computed */
			uint32_t rh = (res->y1 - res->y0);	/* height of the resolution level computed */
			uint32_t w = (tilec->x1 - tilec->x0);

			h.wavelet = (dwt_v4_t*)grok_aligned_malloc((max_resolution(res, numResolutions)) * sizeof(dwt_v4_t));
			if (!h.wavelet) {
				/* FIXME event manager error callback */
				rc++;
				goto cleanup;
			}
			v.wavelet = h.wavelet;
			while (--numResolutions) {
				float * restrict aj = tileBuf + ((w << 2) * threadId);
				uint64_t bufsize = (uint64_t)(tilec->x1 - tilec->x0) * (tilec->y1 - tilec->y0) - (threadId * (w << 2));

				h.s_n = rw;
				v.s_n = rh;

				++res;

				rw = (res->x1 - res->x0);	// width of the resolution level computed 
				rh = (res->y1 - res->y0);	// height of the resolution level computed 

				h.d_n = (uint32_t)(rw - h.s_n);
				h.cas = res->x0 & 1;
				int32_t j;
				for (j = (int32_t)rh - (threadId << 2); j > 3; j -= (numThreads << 2)) {
					v4dwt_interleave_h(&h, aj, w, (uint32_t)bufsize);
					v4dwt_decode(&h);

					for (int32_t k = (int32_t)rw; k-- > 0;) {
						aj[(uint32_t)k] = h.wavelet[k].f[0];
						aj[(uint32_t)k + w] = h.wavelet[k].f[1];
						aj[(uint32_t)k + (w << 1)] = h.wavelet[k].f[2];
						aj[(uint32_t)k + w * 3] = h.wavelet[k].f[3];
					}

					aj += (w << 2) * numThreads;
					bufsize -= (w << 2) * numThreads;
				}
				decode_dwt_barrier.arrive_and_wait();

				if (j > 0) {
					v4dwt_interleave_h(&h, aj, w, (uint32_t)bufsize);
					v4dwt_decode(&h);
					for (int32_t k = (int32_t)rw; k-- > 0;) {
						switch (j) {
						case 3:
							aj[k + (int32_t)(w << 1)] = h.wavelet[k].f[2];
						case 2:
							aj[k + (int32_t)w] = h.wavelet[k].f[1];
						case 1:
							aj[k] = h.wavelet[k].f[0];
						}
					}
				}

				decode_dwt_barrier.arrive_and_wait();

				v.d_n = (int32_t)(rh - v.s_n);
				v.cas = res->y0 & 1;

				decode_dwt_barrier.arrive_and_wait();

				aj = tileBuf + (threadId << 2);
				for (j = (int32_t)rw - (threadId << 2); j > 3; j -= (numThreads << 2)) {
					v4dwt_interleave_v(&v, aj, w, 4);
					v4dwt_decode(&v);

					for (uint32_t k = 0; k < rh; ++k) {
						memcpy(&aj[k*w], &v.wavelet[k], 4 * sizeof(float));
					}
					aj += (numThreads << 2);
				}

				if (j > 0) {
					v4dwt_interleave_v(&v, aj, w, j);
					v4dwt_decode(&v);

					for (uint32_t k = 0; k < rh; ++k) {
						memcpy(&aj[k*w], &v.wavelet[k], (size_t)j * sizeof(float));
					}
				}

				decode_dwt_barrier.arrive_and_wait();
			}
		cleanup:
			if (h.wavelet)
				grok_aligned_free(h.wavelet);
			decode_dwt_calling_barrier.arrive_and_wait();

		}));
	}
	decode_dwt_calling_barrier.arrive_and_wait();
	for (auto& t : dwtWorkers) {
		t.join();
	}
	return rc == 0 ? true : false;
}

void dwt97::v4dwt_interleave_h(v4dwt_t* restrict w, float* restrict a, uint32_t x, uint32_t size)
{
	float* restrict bi = (float*)(w->wavelet + w->cas);
	uint32_t count = w->s_n;
	uint32_t i, k;
	for (k = 0; k < 2; ++k) {
		if (count + 3 * x < size) {
			/* Fast code path */
			for (i = 0; i < count; ++i) {
				uint32_t j = i;
				uint32_t ct = i << 3;
				bi[ct] = a[j];
				j += x;
				ct++;
				bi[ct] = a[j];
				j += x;
				ct++;
				bi[ct] = a[j];
				j += x;
				ct++;
				bi[ct] = a[j];
			}
		}
		else {
			/* Slow code path */
			for (i = 0; i < count; ++i) {
				uint32_t j = i;
				uint32_t ct = i << 3;
				bi[ct] = a[j];
				j += x;
				if (j >= size) 
					continue;
				ct++;
				bi[ct] = a[j];
				j += x;
				if (j >= size) 
					continue;
				ct++;
				bi[ct] = a[j];
				j += x;
				if (j >= size) 
					continue;
				ct++;
				bi[ct] = a[j];
			}
		}

		bi = (float*)(w->wavelet + 1 - w->cas);
		a += w->s_n;
		size -= w->s_n;
		count = w->d_n;
	}
}

void dwt97::v4dwt_interleave_v(v4dwt_t* restrict v, float* restrict a, uint32_t x, uint32_t nb_elts_read)
{
	dwt_v4_t* restrict bi = v->wavelet + v->cas;
	size_t nb_elt_bytes = (size_t)nb_elts_read * sizeof(float);
	for (uint32_t i = 0; i < v->s_n; ++i) {
		memcpy(&bi[i <<1], &a[i*x], nb_elt_bytes);
	}
	a += v->s_n * x;
	bi = v->wavelet + 1 - v->cas;
	for (uint32_t i = 0; i < v->d_n; ++i) {
		memcpy(&bi[i<<1], &a[i*x], nb_elt_bytes);
	}
}

#ifdef __SSE__

static void v4dwt_decode_step1_sse(dwt_v4_t* w, uint32_t count, const __m128 c)
{
	__m128* restrict vw = (__m128*) w;
	uint32_t i;
	/* 4x unrolled loop */
	for (i = 0; i < count >> 2; ++i) {
		*vw = _mm_mul_ps(*vw, c);
		vw += 2;
		*vw = _mm_mul_ps(*vw, c);
		vw += 2;
		*vw = _mm_mul_ps(*vw, c);
		vw += 2;
		*vw = _mm_mul_ps(*vw, c);
		vw += 2;
	}
	count &= 3;
	for (i = 0; i < count; ++i) {
		*vw = _mm_mul_ps(*vw, c);
		vw += 2;
	}
}

static void v4dwt_decode_step2_sse(dwt_v4_t* l, dwt_v4_t* w, uint32_t k, uint32_t m, __m128 c)
{
	__m128* restrict vl = (__m128*) l;
	__m128* restrict vw = (__m128*) w;
	uint32_t i;
	__m128 tmp1, tmp2, tmp3;
	tmp1 = vl[0];
	for (i = 0; i < m; ++i) {
		tmp2 = vw[-1];
		tmp3 = vw[0];
		vw[-1] = _mm_add_ps(tmp2, _mm_mul_ps(_mm_add_ps(tmp1, tmp3), c));
		tmp1 = tmp3;
		vw += 2;
	}
	vl = vw - 2;
	if (m >= k) {
		return;
	}
	c = _mm_add_ps(c, c);
	c = _mm_mul_ps(c, vl[0]);
	for (; m < k; ++m) {
		__m128 tmp = vw[-1];
		vw[-1] = _mm_add_ps(tmp, c);
		vw += 2;
	}
}

#else

static void v4dwt_decode_step1(dwt_v4_t* w, uint32_t count, const float c){
	float* restrict fw = (float*)w;
	for (uint32_t i = 0; i < count; ++i) {
		auto ct = i << 3;
		float tmp1 = fw[ct];
		fw[ct] = tmp1 * c;
		ct++;
		tmp1 = fw[ct];
		fw[ct] = tmp1 * c;
		ct++;
		tmp1 = fw[ct];
		fw[ct] = tmp1 * c;
		ct++;
		tmp1 = fw[ct];
		fw[ct] = tmp1 * c;
	}
}
static void v4dwt_decode_step2(dwt_v4_t* l, dwt_v4_t* w, uint32_t k, uint32_t m, float c){
	float* fl = (float*)l;
	float* fw = (float*)w;
	uint32_t i;
	for (i = 0; i < m; ++i) {
		float tmp1_1 = fl[0];
		float tmp1_2 = fl[1];
		float tmp1_3 = fl[2];
		float tmp1_4 = fl[3];
		float tmp2_1 = fw[-4];
		float tmp2_2 = fw[-3];
		float tmp2_3 = fw[-2];
		float tmp2_4 = fw[-1];
		float tmp3_1 = fw[0];
		float tmp3_2 = fw[1];
		float tmp3_3 = fw[2];
		float tmp3_4 = fw[3];
		fw[-4] = tmp2_1 + ((tmp1_1 + tmp3_1) * c);
		fw[-3] = tmp2_2 + ((tmp1_2 + tmp3_2) * c);
		fw[-2] = tmp2_3 + ((tmp1_3 + tmp3_3) * c);
		fw[-1] = tmp2_4 + ((tmp1_4 + tmp3_4) * c);
		fl = fw;
		fw += 8;
	}
	if (m < k) {
		float c1;
		float c2;
		float c3;
		float c4;
		c += c;
		c1 = fl[0] * c;
		c2 = fl[1] * c;
		c3 = fl[2] * c;
		c4 = fl[3] * c;
		for (; m < k; ++m) {
			float tmp1 = fw[-4];
			float tmp2 = fw[-3];
			float tmp3 = fw[-2];
			float tmp4 = fw[-1];
			fw[-4] = tmp1 + c1;
			fw[-3] = tmp2 + c2;
			fw[-2] = tmp3 + c3;
			fw[-1] = tmp4 + c4;
			fw += 8;
		}
	}
}
#endif

/* <summary>                             */
/* Inverse 9-7 wavelet transform in 1-D. */
/* </summary>                            */
void dwt97::v4dwt_decode(v4dwt_t* restrict dwt){
	uint8_t a, b;
	if (dwt->cas == 0) {
		if (!((dwt->d_n > 0) || (dwt->s_n > 1))) {
			return;
		}
		a = 0;
		b = 1;
	}
	else {
		if (!((dwt->s_n > 0) || (dwt->d_n > 1))) {
			return;
		}
		a = 1;
		b = 0;
	}
#ifdef __SSE__
	v4dwt_decode_step1_sse(dwt->wavelet + a, dwt->s_n, _mm_set1_ps(dwt_K));
	v4dwt_decode_step1_sse(dwt->wavelet + b, dwt->d_n, _mm_set1_ps(dwt_c13318));
	v4dwt_decode_step2_sse(dwt->wavelet + b, dwt->wavelet + a + 1, dwt->s_n, std::min<uint32_t>(dwt->s_n, dwt->d_n - a), _mm_set1_ps(dwt_delta));
	v4dwt_decode_step2_sse(dwt->wavelet + a, dwt->wavelet + b + 1, dwt->d_n, std::min<uint32_t>(dwt->d_n, dwt->s_n - b), _mm_set1_ps(dwt_gamma));
	v4dwt_decode_step2_sse(dwt->wavelet + b, dwt->wavelet + a + 1, dwt->s_n, std::min<uint32_t>(dwt->s_n, dwt->d_n - a), _mm_set1_ps(dwt_beta));
	v4dwt_decode_step2_sse(dwt->wavelet + a, dwt->wavelet + b + 1, dwt->d_n, std::min<uint32_t>(dwt->d_n, dwt->s_n - b), _mm_set1_ps(dwt_alpha));
#else
	v4dwt_decode_step1(dwt->wavelet + a, dwt->s_n, dwt_K);
	v4dwt_decode_step1(dwt->wavelet + b, dwt->d_n, dwt_c13318);
	v4dwt_decode_step2(dwt->wavelet + b, dwt->wavelet + a + 1, dwt->s_n, std::min<uint32_t>(dwt->s_n, dwt->d_n - a), dwt_delta);
	v4dwt_decode_step2(dwt->wavelet + a, dwt->wavelet + b + 1, dwt->d_n, std::min<uint32_t>(dwt->d_n, dwt->s_n - b), dwt_gamma);
	v4dwt_decode_step2(dwt->wavelet + b, dwt->wavelet + a + 1, dwt->s_n, std::min<uint32_t>(dwt->s_n, dwt->d_n - a), dwt_beta);
	v4dwt_decode_step2(dwt->wavelet + a, dwt->wavelet + b + 1, dwt->d_n, std::min<uint32_t>(dwt->d_n, dwt->s_n - b), dwt_alpha);
#endif
}


/* <summary>                             */
/* Forward 9-7 wavelet transform in 2-D. */
/* </summary>                            */
bool dwt97::encode(tcd_tilecomp_t * tilec){
	int32_t i;
	int32_t *a = nullptr;
	int32_t *aj = nullptr;
	int32_t *bj = nullptr;
	uint32_t w, l;

	uint32_t rw;			/* width of the resolution level computed   */
	uint32_t rh;			/* height of the resolution level computed  */
	size_t l_data_size;

	tcd_resolution_t * l_cur_res = 0;
	tcd_resolution_t * l_last_res = 0;

	w = tilec->x1 - tilec->x0;
	l = tilec->numresolutions - 1;
	a = tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0);

	l_cur_res = tilec->resolutions + l;
	l_last_res = l_cur_res - 1;

	l_data_size = max_resolution(tilec->resolutions, tilec->numresolutions) * sizeof(int32_t);
	/* overflow check */
	if (l_data_size > SIZE_MAX) {
		/* FIXME event manager error callback */
		return false;
	}
	bj = (int32_t*)grok_malloc(l_data_size);
	/* l_data_size is equal to 0 when numresolutions == 1 but bj is not used */
	/* in that case, so do not error out */
	if (l_data_size != 0 && !bj) {
		return false;
	}
	i = l;

	while (i--) {
		uint32_t rw1;		/* width of the resolution level once lower than computed one                                       */
		uint32_t rh1;		/* height of the resolution level once lower than computed one                                      */
		uint8_t cas_col;	/* 0 = non inversion on horizontal filtering 1 = inversion between low-pass and high-pass filtering */
		uint8_t cas_row;	/* 0 = non inversion on vertical filtering 1 = inversion between low-pass and high-pass filtering   */
		uint32_t d_n, s_n;

		rw = l_cur_res->x1 - l_cur_res->x0;
		rh = l_cur_res->y1 - l_cur_res->y0;
		rw1 = l_last_res->x1 - l_last_res->x0;
		rh1 = l_last_res->y1 - l_last_res->y0;

		cas_row = l_cur_res->x0 & 1;
		cas_col = l_cur_res->y0 & 1;

		s_n = rh1;
		d_n = rh - rh1;
		for (uint32_t j = 0; j < rw; ++j) {
			aj = a + j;
			for (uint32_t k = 0; k < rh; ++k) {
				bj[k] = aj[k*w];
			}
			encode_line(bj, d_n, s_n, cas_col);
			deinterleave_v(bj, aj, d_n, s_n, w, cas_col);
		}
		s_n = rw1;
		d_n = rw - rw1;

		for (uint32_t j = 0; j < rh; j++) {
			aj = a + j * w;
			for (uint32_t k = 0; k < rw; k++)
				bj[k] = aj[k];
			encode_line(bj, d_n, s_n, cas_row);
			deinterleave_h(bj, aj, d_n, s_n, cas_row);
		}
		l_cur_res = l_last_res;
		--l_last_res;
	}
	grok_free(bj);
	return true;
}

/* <summary>                             */
/* Forward 9-7 wavelet transform in 1-D. */
/* </summary>                            */
void dwt97::encode_line(int32_t *a, int32_t d_n, int32_t s_n, uint8_t cas){
	int32_t i;
	if (!cas) {
		if ((d_n > 0) || (s_n > 1)) {	/* NEW :  CASE ONE ELEMENT */
			for (i = 0; i < d_n; i++)
				GROK_D(i) -= int_fix_mul(GROK_S_(i) + GROK_S_(i + 1), 12994);
			for (i = 0; i < s_n; i++)
				GROK_S(i) -= int_fix_mul(GROK_D_(i - 1) + GROK_D_(i), 434);
			for (i = 0; i < d_n; i++)
				GROK_D(i) += int_fix_mul(GROK_S_(i) + GROK_S_(i + 1), 7233);
			for (i = 0; i < s_n; i++)
				GROK_S(i) += int_fix_mul(GROK_D_(i - 1) + GROK_D_(i), 3633);
			for (i = 0; i < d_n; i++)
				GROK_D(i) = int_fix_mul(GROK_D(i), 5039);	
			for (i = 0; i < s_n; i++)
				GROK_S(i) = int_fix_mul(GROK_S(i), 6659);	
		}
	}
	else {
		if ((s_n > 0) || (d_n > 1)) {	/* NEW :  CASE ONE ELEMENT */
			for (i = 0; i < d_n; i++)
				GROK_S(i) -= int_fix_mul(GROK_DD_(i) + GROK_DD_(i - 1), 12994);
			for (i = 0; i < s_n; i++)
				GROK_D(i) -= int_fix_mul(GROK_SS_(i) + GROK_SS_(i + 1), 434);
			for (i = 0; i < d_n; i++)
				GROK_S(i) += int_fix_mul(GROK_DD_(i) + GROK_DD_(i - 1), 7233);
			for (i = 0; i < s_n; i++)
				GROK_D(i) += int_fix_mul(GROK_SS_(i) + GROK_SS_(i + 1), 3633);
			for (i = 0; i < d_n; i++)
				GROK_S(i) = int_fix_mul(GROK_S(i), 5039);	
			for (i = 0; i < s_n; i++)
				GROK_D(i) = int_fix_mul(GROK_D(i), 6659);	
		}
	}
}

/* <summary>                             */
/* Inverse 9-7 data transform in 2-D. */
/* </summary>                            */
bool dwt97::region_decode(tcd_tilecomp_t* restrict tilec,
	uint32_t numres,
	uint32_t numThreads){
	if (numres == 1U) {
		return true;
	}
	int rc = 0;
	auto tileBuf = tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0);

	Barrier decode_dwt_barrier(numThreads);
	Barrier decode_dwt_calling_barrier(numThreads + 1);
	std::vector<std::thread> dwtWorkers;
	bool success = true;
	for (auto threadId = 0U; threadId < numThreads; threadId++) {
		dwtWorkers.push_back(std::thread([this,
										tilec,
										numres,
										&rc,
										tileBuf,
										&decode_dwt_barrier,
										&decode_dwt_calling_barrier,
										threadId,
										numThreads,
										&success]()		{

			auto numResolutions = numres;
			dwt97_t buffer_h;
			dwt97_t buffer_v;

			tcd_resolution_t* res = tilec->resolutions;
			uint32_t resno = 1;

			/* start with lowest resolution */
			uint32_t res_width = (res->x1 - res->x0);	/* width of the resolution level computed */
			uint32_t res_height = (res->y1 - res->y0);	/* height of the resolution level computed */

			uint32_t tile_width = (tilec->x1 - tilec->x0);

			// add 4 for boundary, plus one for parity
			buffer_h.dataSize = (tile_buf_get_interleaved_upper_bound(tilec->buf) + 5) * 4;
			buffer_h.data = (coeff97_t*)grok_aligned_malloc(buffer_h.dataSize * sizeof(float));

			if (!buffer_h.data) {
				/* FIXME event manager error callback */
				success = false;
				return;

			}
			/* share data buffer between vertical and horizontal lifting steps*/
			buffer_v.data = buffer_h.data;
			while (--numResolutions) {

				int64_t j = 0;
				pt_t interleaved_h, interleaved_v;

				/* start with the first resolution, and work upwards*/

				buffer_h.s_n = res_width;
				buffer_v.s_n = res_height;

				buffer_h.range_even = tile_buf_get_uninterleaved_range(tilec->buf, resno, true, true);
				buffer_h.range_odd = tile_buf_get_uninterleaved_range(tilec->buf, resno, false, true);
				buffer_v.range_even = tile_buf_get_uninterleaved_range(tilec->buf, resno, true, false);
				buffer_v.range_odd = tile_buf_get_uninterleaved_range(tilec->buf, resno, false, false);

				interleaved_h = tile_buf_get_interleaved_range(tilec->buf, resno, true);
				interleaved_v = tile_buf_get_interleaved_range(tilec->buf, resno, false);

				++res;

				/* dimensions of next higher resolution */
				res_width = (res->x1 - res->x0);	/* width of the resolution level computed */
				res_height = (res->y1 - res->y0);	/* height of the resolution level computed */

				buffer_h.d_n = (res_width - buffer_h.s_n);
				buffer_h.odd_top_left_bit = res->x0 & 1;
				buffer_h.interleaved_offset = std::max<int64_t>(0, interleaved_h.x - 4);

				//  Step 1.  interleave and lift in horizontal direction 
				float * restrict tile_data = (float*)tileBuf + tile_width * (buffer_v.range_even.x + (threadId << 2));
				auto bufsize = tile_width * (tilec->y1 - tilec->y0 - buffer_v.range_even.x - (threadId << 2));

				for (j = buffer_v.range_even.y - buffer_v.range_even.x - (threadId << 2); j > 3; j -= 4 * numThreads) {
					region_interleave_h(&buffer_h, tile_data, tile_width, bufsize);
					region_decode(&buffer_h);

					for (auto k = interleaved_h.x; k < interleaved_h.y; ++k) {
						auto buffer_index = k - buffer_h.interleaved_offset;
						tile_data[k] = buffer_h.data[buffer_index].f[0];
						tile_data[k + tile_width] = buffer_h.data[buffer_index].f[1];
						tile_data[k + (tile_width << 1)] = buffer_h.data[buffer_index].f[2];
						tile_data[k + tile_width * 3] = buffer_h.data[buffer_index].f[3];
					}

					tile_data += (tile_width << 2) * numThreads;
					bufsize -= (tile_width << 2)*numThreads;
				}

				if (j > 0) {
					region_interleave_h(&buffer_h, tile_data, tile_width, bufsize);
					region_decode(&buffer_h);
					for (auto k = interleaved_h.x; k < interleaved_h.y; ++k) {
						auto buffer_index = k - buffer_h.interleaved_offset;
						switch (j) {
						case 3:
							tile_data[k + (tile_width << 1)] = buffer_h.data[buffer_index].f[2];
						case 2:
							tile_data[k + tile_width] = buffer_h.data[buffer_index].f[1];
						case 1:
							tile_data[k] = buffer_h.data[buffer_index].f[0];
						}
					}
				}

				decode_dwt_barrier.arrive_and_wait();

				tile_data = (float*)tileBuf + tile_width *(buffer_v.s_n + buffer_v.range_odd.x + (threadId << 2));
				bufsize = tile_width *(tilec->y1 - tilec->y0 - buffer_v.s_n - buffer_v.range_odd.x - (threadId << 2));

				for (j = buffer_v.range_odd.y - buffer_v.range_odd.x - (threadId << 2); j > 3; j -= 4 * numThreads) {
					region_interleave_h(&buffer_h, tile_data, tile_width, bufsize);
					region_decode(&buffer_h);

					for (auto k = interleaved_h.x; k < interleaved_h.y; ++k) {
						auto buffer_index = k - buffer_h.interleaved_offset;
						tile_data[k] = buffer_h.data[buffer_index].f[0];
						tile_data[k + tile_width] = buffer_h.data[buffer_index].f[1];
						tile_data[k + (tile_width << 1)] = buffer_h.data[buffer_index].f[2];
						tile_data[k + tile_width * 3] = buffer_h.data[buffer_index].f[3];
					}

					tile_data += (tile_width << 2)*numThreads;
					bufsize -= (tile_width << 2)*numThreads;
				}


				if (j > 0) {
					region_interleave_h(&buffer_h, tile_data, tile_width, bufsize);
					region_decode(&buffer_h);
					for (auto k = interleaved_h.x; k < interleaved_h.y; ++k) {
						auto buffer_index = k - buffer_h.interleaved_offset;
						switch (j) {
						case 3:
							tile_data[k + (tile_width << 1)] = buffer_h.data[buffer_index].f[2];
						case 2:
							tile_data[k + tile_width] = buffer_h.data[buffer_index].f[1];
						case 1:
							tile_data[k] = buffer_h.data[buffer_index].f[0];
						}
					}
				}

				decode_dwt_barrier.arrive_and_wait();

				// Step 2: interleave and lift in vertical direction 

				buffer_v.d_n = (res_height - buffer_v.s_n);
				buffer_v.odd_top_left_bit = res->y0 & 1;
				buffer_v.interleaved_offset = std::max<int64_t>(0, interleaved_v.x - 4);

				tile_data = (float*)tileBuf + interleaved_h.x + (threadId << 2);
				for (j = interleaved_h.y - interleaved_h.x - (threadId << 2); j > 3; j -= 4 * numThreads) {
					region_interleave_v(&buffer_v, tile_data, tile_width, 4);
					region_decode(&buffer_v);
					for (auto k = interleaved_v.x; k < interleaved_v.y; ++k) {
						memcpy(tile_data + k*tile_width, buffer_v.data + k - buffer_v.interleaved_offset, 4 * sizeof(float));
					}
					tile_data += (4 * numThreads);
				}

				if (j > 0) {
					region_interleave_v(&buffer_v, tile_data, tile_width, j);
					region_decode(&buffer_v);
					for (auto k = interleaved_v.x; k < interleaved_v.y; ++k) {
						memcpy(tile_data + k*tile_width, buffer_v.data + k - buffer_v.interleaved_offset, (size_t)j * sizeof(float));
					}
				}

				resno++;
				decode_dwt_barrier.arrive_and_wait();
			}
			grok_aligned_free(buffer_h.data);
			decode_dwt_calling_barrier.arrive_and_wait();
		}));
	}
	decode_dwt_calling_barrier.arrive_and_wait();

	for (auto& t : dwtWorkers) {
		t.join();
	}
	return success;
}

void dwt97::region_interleave_h(dwt97_t* restrict buffer,
	float* restrict tile_data,
	size_t stride,
	size_t size){

	int64_t bufferShift = buffer->bufferShiftEven();
	float* restrict buffer_data_ptr = (float*)(buffer->data + bufferShift);
	auto count_low = buffer->range_even.x;
	auto count_high = buffer->range_even.y;

	for (auto k = 0; k < 2; ++k) {
		if (((count_high - 1) + 3 * stride < size)) {
			/* Fast code path */
			for (auto i = count_low; i < count_high; ++i) {
				auto j = i;
				auto bufferIndex = i << 3;
				buffer_data_ptr[bufferIndex] = tile_data[j];
				j += stride;
				bufferIndex++;

				buffer_data_ptr[bufferIndex] = tile_data[j];
				j += stride;
				bufferIndex++;

				buffer_data_ptr[bufferIndex] = tile_data[j];
				j += stride;
				bufferIndex++;

				buffer_data_ptr[bufferIndex] = tile_data[j];
			}
		}
		else {
			/* Slow code path */
			for (auto i = count_low; i < count_high; ++i) {
				size_t j = i;
				auto bufferIndex = i << 3;

				buffer_data_ptr[bufferIndex] = tile_data[j];
				bufferIndex++;
				j += stride;
				if (j >= size)
					continue;

				buffer_data_ptr[bufferIndex] = tile_data[j];
				bufferIndex++;
				j += stride;
				if (j >= size)
					continue;

				buffer_data_ptr[bufferIndex] = tile_data[j];
				bufferIndex++;
				j += stride;
				if (j >= size)
					continue;

				buffer_data_ptr[bufferIndex] = tile_data[j];
			}
		}

		bufferShift = buffer->bufferShiftOdd();
		buffer_data_ptr = (float*)(buffer->data + bufferShift);
		tile_data += buffer->s_n;
		size -= buffer->s_n;
		count_low = buffer->range_odd.x;
		count_high = buffer->range_odd.y;
	}
}

void dwt97::region_interleave_v(dwt97_t* restrict buffer,
	float* restrict tile_data,
	size_t stride,
	size_t nb_elts_read){
	coeff97_t* restrict buffer_data_ptr = buffer->data - buffer->interleaved_offset + buffer->odd_top_left_bit;
	auto count_low = buffer->range_even.x;
	auto count_high = buffer->range_even.y;

	for (auto i = count_low; i < count_high; ++i) {
		memcpy(buffer_data_ptr + (i << 1), tile_data + i*stride, nb_elts_read * sizeof(float));
	}

	tile_data += buffer->s_n * stride;
	buffer_data_ptr = buffer->data - buffer->interleaved_offset + (buffer->odd_top_left_bit ^ 1);

	count_low = buffer->range_odd.x;
	count_high = buffer->range_odd.y;

	for (auto i = count_low; i < count_high; ++i) {
		memcpy(buffer_data_ptr + (i << 1), tile_data + i*stride, nb_elts_read * sizeof(float));
	}
}

void dwt97::region_decode_scale(coeff97_t* buffer,
	pt_t range,
	const float scale){
	float* restrict fw = ((float*)buffer);
	auto count_low = range.x;
	auto count_high = range.y;

	for (auto i = count_low; i < count_high; ++i) {
		fw[(i << 3)] *= scale;
		fw[(i << 3) + 1] *= scale;
		fw[(i << 3) + 2] *= scale;
		fw[(i << 3) + 3] *= scale;
	}
}

void dwt97::region_decode_lift(coeff97_t* l,
	coeff97_t* w,
	pt_t range,
	int64_t maximum,
	float scale){
	float* fl = (float*)l;
	float* fw = (float*)w;

	auto count_low = range.x;
	auto count_high = range.y;
	auto count_max = std::min<int64_t>(count_high, maximum);

	assert(count_low <= count_high);
	if (count_low > 0) {
		fw += count_low << 3;
		fl = fw - 8;
	}

	for (auto i = count_low; i < count_max; ++i) {
		fw[-4] += ((fl[0] + fw[0]) * scale);
		fw[-3] += ((fl[1] + fw[1]) * scale);
		fw[-2] += ((fl[2] + fw[2]) * scale);
		fw[-1] += ((fl[3] + fw[3]) * scale);
		fl = fw;
		fw += 8;
	}

	/* symmetric boundary extension */
	if (maximum < count_high) {
		scale += scale;
		for (; maximum < count_high; ++maximum) {
			fw[-4] += fl[0] * scale;
			fw[-3] += fl[1] * scale;
			fw[-2] += fl[2] * scale;
			fw[-1] += fl[3] * scale;
			fw += 8;
		}
	}
}

/* <summary>                             */
/* Inverse 9-7 data transform in 1-D. */
/* </summary>                            */
void dwt97::region_decode(dwt97_t* restrict dwt){
	/* either 0 or 1 */
	uint8_t odd_top_left_bit = dwt->odd_top_left_bit;
	uint8_t even_top_left_bit = odd_top_left_bit ^ 1;


	if (!((dwt->d_n > odd_top_left_bit) || (dwt->s_n > even_top_left_bit))) {
		return;
	}

	/* inverse low-pass scale */
	region_decode_scale(dwt->data - dwt->interleaved_offset + odd_top_left_bit,
		dwt->range_even,
		dwt_K);

	/* inverse high-pass scale */
	region_decode_scale(dwt->data - dwt->interleaved_offset + even_top_left_bit,
		dwt->range_odd,
		dwt_c13318);

	/* inverse update */
	region_decode_lift(dwt->data - dwt->interleaved_offset + even_top_left_bit,
		dwt->data - dwt->interleaved_offset + odd_top_left_bit + 1,
		dwt->range_even,
		std::min<int64_t>(dwt->s_n, dwt->d_n - odd_top_left_bit),
		dwt_delta);

	/* inverse predict */
	region_decode_lift(dwt->data - dwt->interleaved_offset + odd_top_left_bit,
		dwt->data - dwt->interleaved_offset + even_top_left_bit + 1,
		dwt->range_odd,
		std::min<int64_t>(dwt->d_n, dwt->s_n - even_top_left_bit),
		dwt_gamma);
	/* inverse update */
	region_decode_lift(dwt->data - dwt->interleaved_offset + even_top_left_bit,
		dwt->data - dwt->interleaved_offset + odd_top_left_bit + 1,
		dwt->range_even,
		std::min<int64_t>(dwt->s_n, dwt->d_n - odd_top_left_bit),
		dwt_beta);

	/* inverse predict */
	region_decode_lift(dwt->data - dwt->interleaved_offset + odd_top_left_bit,
		dwt->data - dwt->interleaved_offset + even_top_left_bit + 1,
		dwt->range_odd,
		std::min<int64_t>(dwt->d_n, dwt->s_n - even_top_left_bit),
		dwt_alpha);

}
}
