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

 /*

 =================================================================================
 Synthesis DWT Transform for a region wholly contained inside of a tile component
 =================================================================================

 Notes on DWT tranform:

 The first step in the synthesis transform is interleaving, where sub-bands are transformed
 into resolution space by interleaving even and odd coordinates
 (i.e. low and high pass filtered samples).

 Low-pass filtered samples in sub-bands are mapped to even coordinates in the resolution
 coordinate system, and high-pass filtered samples are mapped to odd coordinates
 in the resolution coordinate system.

 The letter s is used to denote even canvas coordinates (after interleaving),
 while the letter d is used to denote odd coordinates (after interleaving).
 s_n denotes the number of even locations at a given resolution, while d_n denotes the number
 of odd locations.


 5/3 Implementation:

 For each specified resolution, starting with the first resolution, the transform
 proceeds as follows:

 1. For each row region, samples are interleaved in the horizontal axis, and stored in a
 one dimension buffer. Important: the 0th location in the buffer is mapped to the first interleaved
 location in the resolution, which could be either even or odd.  So, based on the parity of the resolution's
 top left hand corner, the even buffer locations are either mapped to low pass or high pass samples
 in the sub-bands. (if even locations are low pass, then odd locations are high pass, and vice versa).

 2. horizontal lifting in buffer

 3. copy data to tile buffer

 4. repeat for vertical axis


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


/**
Forward wavelet transform in 2-D.
Apply a reversible DWT transform to a component of an image.
@param tilec Tile component information (current tile)
*/
bool dwt53::encode(tcd_tilecomp_t * tilec) {
	uint32_t k;
	int32_t *a = nullptr;
	int32_t *aj = nullptr;
	int32_t *bj = nullptr;
	uint32_t w;

	uint32_t rw;			/* width of the resolution level computed   */
	uint32_t rh;			/* height of the resolution level computed  */
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
			for (k = 0; k < rh; ++k) {
				bj[k] = aj[k*w];
			}
			encode_line(bj, d_n, s_n, cas_col);
			deinterleave_v(bj, aj, d_n, s_n, w, cas_col);
		}
		s_n = rw1;
		d_n = rw - rw1;

		for (uint32_t j = 0; j < rh; j++) {
			aj = a + j * w;
			for (k = 0; k < rw; k++) 
				bj[k] = aj[k];
			encode_line(bj, d_n, s_n, cas_row);
			deinterleave_h(bj, aj, d_n, s_n, cas_row);
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
/* <summary>                            */
/* Forward 5-3 wavelet transform in 1-D. */
/* </summary>                           */
void dwt53::encode_line(int32_t *a, int32_t d_n, int32_t s_n, uint8_t cas)
{
	int32_t i;

	if (!cas) {
		if ((d_n > 0) || (s_n > 1)) {	
			for (i = 0; i < d_n; i++) 
				GROK_D(i) -= (GROK_S_(i) + GROK_S_(i + 1)) >> 1;
			for (i = 0; i < s_n; i++)
				GROK_S(i) += (GROK_D_(i - 1) + GROK_D_(i) + 2) >> 2;
		}
	}
	else {
		if (!s_n && d_n == 1)		    /* NEW :  CASE ONE ELEMENT */
			GROK_S(0) *= 2;
		else {
			for (i = 0; i < d_n; i++) 
				GROK_S(i) -= (GROK_DD_(i) + GROK_DD_(i - 1)) >> 1;
			for (i = 0; i < s_n; i++) 
				GROK_D(i) += (GROK_SS_(i) + GROK_SS_(i + 1) + 2) >> 2;
		}
	}
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
		return region_decode(tilec, numres, numThreads);

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
				h.s_n = rw;
				v.s_n = rh;

				rw = (tr->x1 - tr->x0);
				rh = (tr->y1 - tr->y0);

				h.d_n = (int32_t)(rw - h.s_n);
				h.cas = tr->x0 & 1;

				for (uint32_t j = threadId; j < rh; j += numThreads) {
					interleave_h(&h, &tiledp[j*w]);
					decode_line(&h);
					memcpy(&tiledp[j*w], h.mem, rw * sizeof(int32_t));
				}

				v.d_n = (int32_t)(rh - v.s_n);
				v.cas = tr->y0 & 1;

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
	int32_t d_n = (int32_t)v->d_n;
	int32_t s_n = (int32_t)v->s_n;
	uint32_t cas = v->cas;
	int32_t i;

	if (!cas) {
		if ((d_n > 0) || (s_n > 1)) { 
			for (i = 0; i < s_n; i++)
				GROK_S(i) -= (GROK_D_(i - 1) + GROK_D_(i) + 2) >> 2;
			for (i = 0; i < d_n; i++) 
				GROK_D(i) += (GROK_S_(i) + GROK_S_(i + 1)) >> 1;
		}
	}
	else {
		if (!s_n  && d_n == 1)      
			GROK_S(0) /= 2;
		else {
			for (i = 0; i < s_n; i++) 
				GROK_D(i) -= (GROK_SS_(i) + GROK_SS_(i + 1) + 2) >> 2;
			for (i = 0; i < d_n; i++) 
				GROK_S(i) += (GROK_DD_(i) + GROK_DD_(i - 1)) >> 1;
		}
	}

}


/* <summary>                             */
/* Inverse lazy transform (vertical).    */
/* </summary>                            */
void dwt53::interleave_v(dwt_t* v, int32_t *a, int32_t x)
{
	int32_t *ai = a;
	int32_t *bi = v->mem + v->cas;
	int32_t  i = (int32_t)v->s_n;
	while (i--) {
		*bi = *ai;
		bi += 2;
		ai += x;
	}
	ai = a + (v->s_n * x);
	bi = v->mem + 1 - v->cas;
	i = (int32_t)v->d_n;
	while (i--) {
		*bi = *ai;
		bi += 2;
		ai += x;
	}
}


/* <summary>                             */
/* Inverse lazy transform (horizontal).  */
/* </summary>                            */
void dwt53::interleave_h(dwt_t* h, int32_t *a)
{
	int32_t *ai = a;
	int32_t *bi = h->mem + h->cas;
	int32_t  i = (int32_t)h->s_n;
	while (i--) {
		*bi = *(ai++);
		bi += 2;
	}
	ai = a + h->s_n;
	bi = h->mem + 1 - h->cas;
	i = (int32_t)h->d_n;
	while (i--) {
		*bi = *(ai++);
		bi += 2;
	}
}

void dwt53::region_decode_1d(dwt53_t *buffer)
{
	int32_t *a = buffer->data - buffer->interleaved_offset;
	auto d_n = buffer->d_n;
	auto s_n = buffer->s_n;
	if (!buffer->odd_top_left_bit) {
		if ((d_n > 0) || (s_n > 1)) {
			/* inverse update */
			for (auto i = buffer->range_even.x; i < buffer->range_even.y; ++i)
				GROK_S(i) -= (GROK_D_(i - 1) + GROK_D_(i) + 2) >> 2;
			/* inverse predict */
			for (auto i = buffer->range_odd.x; i < buffer->range_odd.y; ++i)
				GROK_D(i) += (GROK_S_(i) + GROK_S_(i + 1)) >> 1;
		}
	}
	else {
		if (!s_n  && d_n == 1)
			GROK_S(0) >>= 1;
		else {
			/* inverse update */
			for (auto i = buffer->range_even.x; i < buffer->range_even.y; ++i)
				GROK_D(i) -= (GROK_SS_(i) + GROK_SS_(i + 1) + 2) >> 2;

			/* inverse predict */
			for (auto i = buffer->range_odd.x; i < buffer->range_odd.y; ++i)
				GROK_S(i) += (GROK_DD_(i) + GROK_DD_(i - 1)) >> 1;
		}
	}
}

/* <summary>                             */
/* Inverse lazy transform (horizontal).  */
/* </summary>                            */
void dwt53::region_interleave_h(dwt53_t* buffer_h, int32_t *tile_data)
{
	int32_t *tile_data_ptr = tile_data;
	int32_t *buffer_data_ptr = buffer_h->data - buffer_h->interleaved_offset + buffer_h->odd_top_left_bit;
	for (auto i = buffer_h->range_even.x; i < buffer_h->range_even.y; ++i) {
		buffer_data_ptr[i << 1] = tile_data_ptr[i];
	}
	tile_data_ptr = tile_data + buffer_h->s_n;
	buffer_data_ptr = buffer_h->data - buffer_h->interleaved_offset + (buffer_h->odd_top_left_bit ^ 1);

	for (auto i = buffer_h->range_odd.x; i < buffer_h->range_odd.y; ++i) {
		buffer_data_ptr[i << 1] = tile_data_ptr[i];
	}
}

/* <summary>                             */
/* Inverse lazy transform (vertical).    */
/* </summary>                            */
void dwt53::region_interleave_v(dwt53_t* buffer_v,
	int32_t *tile_data,
	size_t stride)
{
	int32_t *tile_data_ptr = tile_data;
	int32_t *buffer_data_ptr = buffer_v->data - buffer_v->interleaved_offset + buffer_v->odd_top_left_bit;
	for (auto i = buffer_v->range_even.x; i < buffer_v->range_even.y; ++i) {
		buffer_data_ptr[i << 1] = tile_data_ptr[i*stride];
	}

	tile_data_ptr = tile_data + (buffer_v->s_n * stride);
	buffer_data_ptr = buffer_v->data - buffer_v->interleaved_offset + (buffer_v->odd_top_left_bit ^ 1);

	for (auto i = buffer_v->range_odd.x; i < buffer_v->range_odd.y; ++i) {
		buffer_data_ptr[i << 1] = tile_data_ptr[i*stride];
	}
}


/* <summary>                            */
/* Inverse 5-3 data transform in 2-D. */
/* </summary>                           */
bool dwt53::region_decode(tcd_tilecomp_t* tilec,
	uint32_t numres,
	uint32_t numThreads)
{
	if (numres == 1U) {
		return true;
	}

	int rc = 0;
	auto tileBuf = (int32_t*)tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0);
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
										&success]()
		{
			auto numResolutions = numres;
			dwt53_t buffer_h;
			dwt53_t buffer_v;

			tcd_resolution_t* tr = tilec->resolutions;

			uint32_t res_width = (tr->x1 - tr->x0);	/* width of the resolution level computed */
			uint32_t res_height = (tr->y1 - tr->y0);	/* height of the resolution level computed */

			uint32_t w = (tilec->x1 - tilec->x0);
			int32_t resno = 1;

			// add 2 for boundary, plus one for parity
			auto bufferDataSize = tile_buf_get_interleaved_upper_bound(tilec->buf) + 3;
			buffer_h.data = (int32_t*)grok_aligned_malloc(bufferDataSize * sizeof(int32_t));
			if (!buffer_h.data) {
				success = false;
				return;
			}

			buffer_v.data = buffer_h.data;

			while (--numResolutions) {
				/* start with the first resolution, and work upwards*/
				buffer_h.range_even = tile_buf_get_uninterleaved_range(tilec->buf, resno, true, true);
				buffer_h.range_odd = tile_buf_get_uninterleaved_range(tilec->buf, resno, false, true);
				buffer_v.range_even = tile_buf_get_uninterleaved_range(tilec->buf, resno, true, false);
				buffer_v.range_odd = tile_buf_get_uninterleaved_range(tilec->buf, resno, false, false);

				pt_t interleaved_h = tile_buf_get_interleaved_range(tilec->buf, resno, true);
				pt_t interleaved_v = tile_buf_get_interleaved_range(tilec->buf, resno, false);

				buffer_h.s_n = res_width;
				buffer_v.s_n = res_height;
				buffer_v.interleaved_offset = std::max<int64_t>(0, interleaved_v.x - 2);

				++tr;
				res_width = (tr->x1 - tr->x0);
				res_height = (tr->y1 - tr->y0);

				buffer_h.d_n = (int64_t)(res_width - buffer_h.s_n);
				buffer_h.odd_top_left_bit = tr->x0 & 1;
				buffer_h.interleaved_offset = std::max<int64_t>(0, interleaved_h.x - 2);

				/* first do horizontal interleave */
				int32_t * restrict tiledp = tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0) + (buffer_v.range_even.x + threadId) * w;
				for (auto j = threadId; j < buffer_v.range_even.y - buffer_v.range_even.x; j += numThreads) {
					region_interleave_h(&buffer_h, tiledp);
					region_decode_1d(&buffer_h);
					memcpy(tiledp + interleaved_h.x, buffer_h.data + interleaved_h.x - buffer_h.interleaved_offset, (interleaved_h.y - interleaved_h.x) * sizeof(int32_t));
					tiledp += w*numThreads;
				}
				decode_dwt_barrier.arrive_and_wait();

				tiledp = tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0) + (buffer_v.s_n + buffer_v.range_odd.x + threadId) * w;
				for (auto j = threadId; j < buffer_v.range_odd.y - buffer_v.range_odd.x; j += numThreads) {
					region_interleave_h(&buffer_h, tiledp);
					region_decode_1d(&buffer_h);
					memcpy(tiledp + interleaved_h.x, buffer_h.data + interleaved_h.x - buffer_h.interleaved_offset, (interleaved_h.y - interleaved_h.x) * sizeof(int32_t));
					tiledp += (w*numThreads);
				}
				decode_dwt_barrier.arrive_and_wait();

				buffer_v.d_n = (res_height - buffer_v.s_n);
				buffer_v.odd_top_left_bit = tr->y0 & 1;

				// next do vertical interleave 
				tiledp = tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0) + interleaved_h.x + threadId;
				for (auto j = threadId; j < interleaved_h.y - interleaved_h.x; j += numThreads) {
					int32_t * restrict tiledp_v = tiledp + (interleaved_v.x)*w;
					region_interleave_v(&buffer_v, tiledp, w);
					region_decode_1d(&buffer_v);
					for (auto k = interleaved_v.x; k < interleaved_v.y; k++) {
						*tiledp_v = buffer_v.data[k - buffer_v.interleaved_offset];
						tiledp_v += w;
					}
					tiledp += numThreads;
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

}