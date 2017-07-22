/*
*    Copyright (C) 2016-2017 Grok Image Compression Inc.
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

#ifdef __SSE__
#include <xmmintrin.h>
#endif
#include "grok_includes.h"
#include "Barrier.h"
#include "T1Decoder.h"
#include <atomic>
#include "testing.h"



namespace grk {


#define GROK_S(i) a[(i)*2]
#define GROK_D(i) a[(1+(i)*2)]
#define GROK_S_(i) ((i)<0?GROK_S(0):((i)>=sn?GROK_S(sn-1):GROK_S(i)))
#define GROK_D_(i) ((i)<0?GROK_D(0):((i)>=dn?GROK_D(dn-1):GROK_D(i)))
	/* new */
#define GROK_SS_(i) ((i)<0?GROK_S(0):((i)>=dn?GROK_S(dn-1):GROK_S(i)))
#define GROK_DD_(i) ((i)<0?GROK_D(0):((i)>=sn?GROK_D(sn-1):GROK_D(i)))

/**
Forward wavelet transform in 2-D.
Apply a reversible DWT transform to a component of an image.
@param tilec Tile component information (current tile)
*/
bool dwt53::encode(tcd_tilecomp_t * tilec) {
	int32_t k;
	int32_t *a = nullptr;
	int32_t *aj = nullptr;
	int32_t *bj = nullptr;
	int32_t w;

	int32_t rw;			/* width of the resolution level computed   */
	int32_t rh;			/* height of the resolution level computed  */
	size_t l_data_size;

	tcd_resolution_t * l_cur_res = 0;
	tcd_resolution_t * l_last_res = 0;

	w = tilec->x1 - tilec->x0;
	int32_t num_decomps = (int32_t)tilec->numresolutions - 1;
	a = tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0);

	l_cur_res = tilec->resolutions + num_decomps;
	l_last_res = l_cur_res - 1;



#ifdef DEBUG_LOSSLESS_DWT
	int32_t rw_full = l_cur_res->x1 - l_cur_res->x0;
	int32_t rh_full = l_cur_res->y1 - l_cur_res->y0;
	int32_t* before = new int32_t[rw_full * rh_full];
	memcpy(before, a, rw_full * rh_full * sizeof(int32_t));
	int32_t* after = new int32_t[rw_full * rh_full];

#endif

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

	while (num_decomps--) {
		int32_t rw1;		/* width of the resolution level once lower than computed one                                       */
		int32_t rh1;		/* height of the resolution level once lower than computed one                                      */
		int32_t cas_col;	/* 0 = non inversion on horizontal filtering 1 = inversion between low-pass and high-pass filtering */
		int32_t cas_row;	/* 0 = non inversion on vertical filtering 1 = inversion between low-pass and high-pass filtering   */
		int32_t dn, sn;

		rw = l_cur_res->x1 - l_cur_res->x0;
		rh = l_cur_res->y1 - l_cur_res->y0;
		rw1 = l_last_res->x1 - l_last_res->x0;
		rh1 = l_last_res->y1 - l_last_res->y0;

		cas_row = l_cur_res->x0 & 1;
		cas_col = l_cur_res->y0 & 1;

		sn = rh1;
		dn = rh - rh1;

		for (int32_t j = 0; j < rw; ++j) {
			aj = a + j;
			for (k = 0; k < rh; ++k) {
				bj[k] = aj[k*w];
			}
			encode_line(bj, dn, sn, cas_col);
			deinterleave_v(bj, aj, dn, sn, w, cas_col);
		}
		sn = rw1;
		dn = rw - rw1;

		for (int32_t j = 0; j < rh; j++) {
			aj = a + j * w;
			for (k = 0; k < rw; k++)  bj[k] = aj[k];
			encode_line(bj, dn, sn, cas_row);
			deinterleave_h(bj, aj, dn, sn, cas_row);
		}
		l_cur_res = l_last_res;
		--l_last_res;
	}
	grok_free(bj);
#ifdef DEBUG_LOSSLESS_DWT
	memcpy(after, a, rw_full * rh_full * sizeof(int32_t));
	dwt53 dwt;
	dwt.decode(tilec, tilec->numresolutions, 8);
	for (int m = 0; m < rw_full; ++m) {
		for (int p = 0; p < rh_full; ++p) {
			auto expected = a[m + p * rw_full];
			auto actual = before[m + p * rw_full];
			if (expected != actual) {
				printf("(%d, %d); expected %d, got %d\n", m, p, expected, actual);
			}
		}
	}
	memcpy(a, after, rw_full * rh_full * sizeof(int32_t));
	delete[] before;
	delete[] after;
#endif
	return true;
}

/**
Inverse wavelet transform in 2-D.
Apply a reversible inverse DWT transform to a component of an image.
@param tilec Tile component information (current tile)
@param numres Number of resolution levels to decode
*/
bool dwt53::decode(tcd_tilecomp_t* tilec,
	uint32_t numres,
	uint32_t numThreads) {

	if (numres == 1U) {
		return true;
	}
	if (tile_buf_is_decode_region(tilec->buf))
		return dwt_region_decode53(tilec, numres, numThreads);

	std::vector<std::thread> dwtWorkers;
	int rc = 0;
	auto tileBuf = (int32_t*)tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0);
	Barrier decode_dwt_barrier(numThreads);
	Barrier decode_dwt_calling_barrier(numThreads + 1);

	for (auto threadId = 0U; threadId < numThreads; threadId++) {
		dwtWorkers.push_back(std::thread([tilec,
			numres,
			&rc,
			tileBuf,
			&decode_dwt_barrier,
			&decode_dwt_calling_barrier,
			threadId,
			numThreads, this]()
		{
			auto numResolutions = numres;
			dwt_t h;
			dwt_t v;

			tcd_resolution_t* tr = tilec->resolutions;

			uint32_t rw = (tr->x1 - tr->x0);	/* width of the resolution level computed */
			uint32_t rh = (tr->y1 - tr->y0);	/* height of the resolution level computed */

			uint32_t w = (tilec->x1 - tilec->x0);
			h.mem = (int32_t*)grok_aligned_malloc(max_resolution(tr, numResolutions) * sizeof(int32_t));
			if (!h.mem) {
				rc++;
				goto cleanup;
			}

			v.mem = h.mem;

			while (--numResolutions) {
				int32_t * restrict tiledp = tileBuf;

				++tr;
				h.sn = (int32_t)rw;
				v.sn = (int32_t)rh;

				rw = (tr->x1 - tr->x0);
				rh = (tr->y1 - tr->y0);

				h.dn = (int32_t)(rw - (uint32_t)h.sn);
				h.cas = tr->x0 % 2;

				for (uint32_t j = threadId; j < rh; j += numThreads) {
					interleave_h(&h, &tiledp[j*w]);
					decode_line(&h);
					memcpy(&tiledp[j*w], h.mem, rw * sizeof(int32_t));
				}

				v.dn = (int32_t)(rh - (uint32_t)v.sn);
				v.cas = tr->y0 % 2;

				decode_dwt_barrier.arrive_and_wait();

				for (uint32_t j = threadId; j < rw; j += numThreads) {
					interleave_v(&v, &tiledp[j], (int32_t)w);
					decode_line(&v);
					for (uint32_t k = 0; k < rh; ++k) {
						tiledp[k * w + j] = v.mem[k];
					}
				}
				decode_dwt_barrier.arrive_and_wait();
			}
		cleanup:
			if (h.mem)
				grok_aligned_free(h.mem);
			decode_dwt_calling_barrier.arrive_and_wait();

		}));
	}
	decode_dwt_calling_barrier.arrive_and_wait();

	for (auto& t : dwtWorkers) {
		t.join();
	}
	return rc == 0 ? true : false;

}

/* <summary>                            */
/* Inverse 5-3 wavelet transform in 1-D. */
/* </summary>                           */
void dwt53::decode_line(dwt_t *v)
{
	int32_t *a = v->mem;
	int32_t dn = v->dn;
	int32_t sn = v->sn;
	int32_t cas = v->cas;
	int32_t i;

	if (!cas) {
		if ((dn > 0) || (sn > 1)) { /* NEW :  CASE ONE ELEMENT */
			for (i = 0; i < sn; i++) GROK_S(i) -= (GROK_D_(i - 1) + GROK_D_(i) + 2) >> 2;
			for (i = 0; i < dn; i++) GROK_D(i) += (GROK_S_(i) + GROK_S_(i + 1)) >> 1;
		}
	}
	else {
		if (!sn  && dn == 1)          /* NEW :  CASE ONE ELEMENT */
			GROK_S(0) /= 2;
		else {
			for (i = 0; i < sn; i++) GROK_D(i) -= (GROK_SS_(i) + GROK_SS_(i + 1) + 2) >> 2;
			for (i = 0; i < dn; i++) GROK_S(i) += (GROK_DD_(i) + GROK_DD_(i - 1)) >> 1;
		}
	}

}



/* <summary>                            */
/* Forward 5-3 wavelet transform in 1-D. */
/* </summary>                           */
void dwt53::encode_line(int32_t *a, int32_t dn, int32_t sn, int32_t cas)
{
	int32_t i;

	if (!cas) {
		if ((dn > 0) || (sn > 1)) {	/* NEW :  CASE ONE ELEMENT */
			for (i = 0; i < dn; i++) GROK_D(i) -= (GROK_S_(i) + GROK_S_(i + 1)) >> 1;
			for (i = 0; i < sn; i++) GROK_S(i) += (GROK_D_(i - 1) + GROK_D_(i) + 2) >> 2;
		}
	}
	else {
		if (!sn && dn == 1)		    /* NEW :  CASE ONE ELEMENT */
			GROK_S(0) *= 2;
		else {
			for (i = 0; i < dn; i++) GROK_S(i) -= (GROK_DD_(i) + GROK_DD_(i - 1)) >> 1;
			for (i = 0; i < sn; i++) GROK_D(i) += (GROK_SS_(i) + GROK_SS_(i + 1) + 2) >> 2;
		}
	}
}



}