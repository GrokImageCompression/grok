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

 Notes on DWT transform:

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
#include <cmath>
#include "dwt53.h"

namespace grk {

#define GROK_S(i) a[(i)<<1]
#define GROK_D(i) a[(1+((i)<<1))]
#define GROK_S_(i) ((i)<0?GROK_S(0):((i)>=s_n?GROK_S(s_n-1):GROK_S(i)))
#define GROK_D_(i) ((i)<0?GROK_D(0):((i)>=d_n?GROK_D(d_n-1):GROK_D(i)))

#define GROK_SS_(i) ((i)<0?GROK_S(0):((i)>=d_n?GROK_S(d_n-1):GROK_S(i)))
#define GROK_DD_(i) ((i)<0?GROK_D(0):((i)>=s_n?GROK_D(s_n-1):GROK_D(i)))

// before DWT
#ifdef DEBUG_LOSSLESS_DWT
	int32_t rw_full = l_cur_res->x1 - l_cur_res->x0;
	int32_t rh_full = l_cur_res->y1 - l_cur_res->y0;
	int32_t* before = new int32_t[rw_full * rh_full];
	memcpy(before, a, rw_full * rh_full * sizeof(int32_t));
	int32_t* after = new int32_t[rw_full * rh_full];

#endif

// after DWT
#ifdef DEBUG_LOSSLESS_DWT
	memcpy(after, a, rw_full * rh_full * sizeof(int32_t));
	dwt53 dwt_utils;
	dwt_utils.decode(tilec, tilec->numresolutions, 8);
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


/* <summary>                            */
/* Forward 5-3 wavelet transform in 1-D. */
/* </summary>                           */
void dwt53::encode_line(int32_t *a, int32_t d_n, int32_t s_n, uint8_t cas) {
	if (!cas) {
		if ((d_n > 0) || (s_n > 1)) {
			for (int32_t i = 0; i < d_n; i++)
				GROK_D(i)-= (GROK_S_(i) + GROK_S_(i + 1)) >> 1;
			for (int32_t i = 0; i < s_n; i++)
				GROK_S(i) += (GROK_D_(i - 1) + GROK_D_(i) + 2) >> 2;
		}
	}
	else {
		if (!s_n && d_n == 1) /* NEW :  CASE ONE ELEMENT */
			GROK_S(0) <<= 1;
		else {
			for (int32_t i = 0; i < d_n; i++)
				GROK_S(i) -= (GROK_DD_(i) + GROK_DD_(i - 1)) >> 1;
			for (int32_t i = 0; i < s_n; i++)
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
typedef int32_t T;
typedef grk_dwt STR;
bool dwt53::decode(TileComponent *tilec, uint32_t numres, uint32_t numThreads) {
	if (numres == 1U) {
		return true;
	}
	if (!tilec->whole_tile_decoding)
		return region_decode(tilec, numres, numThreads);

	std::vector<std::thread> dwtWorkers;
	int rc = 0;
	auto tileBuf = (int32_t*) tilec->buf->get_ptr( 0, 0, 0, 0);
	Barrier decode_dwt_barrier(numThreads);
	Barrier decode_dwt_calling_barrier(numThreads + 1);

	for (auto threadId = 0U; threadId < numThreads; threadId++) {
		dwtWorkers.push_back(
				std::thread(
						[tilec, numres, &rc, tileBuf, &decode_dwt_barrier,
								&decode_dwt_calling_barrier, threadId,
								numThreads, this]() {
							auto numResolutions = numres;
							grk_tcd_resolution *tr = tilec->resolutions;

							uint32_t rw = (tr->x1 - tr->x0); /* width of the resolution level computed */
							uint32_t rh = (tr->y1 - tr->y0); /* height of the resolution level computed */
							uint32_t stride = tilec->width();

							STR h;
							h.mem = (T*) grok_aligned_malloc(
									dwt_utils::max_resolution(tr, numResolutions)
											* sizeof(T));
							if (!h.mem) {
								rc++;
								goto cleanup;
							}
							STR v;
							v.mem = h.mem;

							while (--numResolutions) {
								int32_t *restrict tiledp = tileBuf;

								++tr;
								h.s_n = rw;
								v.s_n = rh;

								rw = (tr->x1 - tr->x0);
								rh = (tr->y1 - tr->y0);

								if (rh) {
									const uint32_t linesPerThreadH = (uint32_t)std::ceil((float)rh / Scheduler::g_tp->num_threads());

									h.d_n = (int32_t) (rw - h.s_n);
									h.cas = tr->x0 & 1;

									for (auto j = threadId * linesPerThreadH;
											j < std::min<uint32_t>((threadId+1)*linesPerThreadH, rh); ++j) {
										interleave_h(&h, &tiledp[j * stride]);
										decode_line(&h);
										memcpy(&tiledp[j * stride], h.mem,
												rw * sizeof(T));
									}
								}
								decode_dwt_barrier.arrive_and_wait();

								if (rw) {
									v.d_n = (int32_t) (rh - v.s_n);
									v.cas = tr->y0 & 1;
									const uint32_t linesPerThreadV = static_cast<uint32_t>((std::ceil((float)rw / Scheduler::g_tp->num_threads())));

									for (auto j = threadId * linesPerThreadV;
											j < std::min<uint32_t>((threadId+1)*linesPerThreadV, rw); ++j) {
										interleave_v(&v, &tiledp[j],
												(int32_t) stride);
										decode_line(&v);
										for (uint32_t k = 0; k < rh; ++k) {
											tiledp[k * stride + j] = v.mem[k];
										}
									}
								}
								decode_dwt_barrier.arrive_and_wait();
							}
							cleanup: if (h.mem)
								grok_aligned_free(h.mem);
							decode_dwt_calling_barrier.arrive_and_wait();

						}));
	}
	decode_dwt_calling_barrier.arrive_and_wait();

	for (auto &t : dwtWorkers) {
		t.join();
	}
	return rc == 0 ? true : false;

}

/* <summary>                            */
/* Inverse 5-3 wavelet transform in 1-D. */
/* </summary>                           */
void dwt53::decode_line(grk_dwt* restrict v) {
	int32_t *a = v->mem;
	int32_t d_n = (int32_t) v->d_n;
	int32_t s_n = (int32_t) v->s_n;
	uint32_t cas = v->cas;
	int32_t i;

	if (!cas) {
		if ((d_n > 0) || (s_n > 1)) {
			for (i = 0; i < s_n; i++)
				GROK_S(i)-= (GROK_D_(i - 1) + GROK_D_(i) + 2) >> 2;
				for (i = 0; i < d_n; i++)
				GROK_D(i) += (GROK_S_(i) + GROK_S_(i + 1)) >> 1;
			}
		}
		else {
			if (!s_n && d_n == 1)
			GROK_S(0) >>= 1;
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
void dwt53::interleave_v(grk_dwt* restrict v, int32_t* restrict a, int32_t x) {
	int32_t *ai = a;
	int32_t *bi = v->mem + v->cas;
	int32_t i = (int32_t) v->s_n;
	while (i--) {
		*bi = *ai;
		bi += 2;
		ai += x;
	}
	ai = a + (v->s_n * x);
	bi = v->mem + 1 - v->cas;
	i = (int32_t) v->d_n;
	while (i--) {
		*bi = *ai;
		bi += 2;
		ai += x;
	}
}

/* <summary>                             */
/* Inverse lazy transform (horizontal).  */
/* </summary>                            */
void dwt53::interleave_h(grk_dwt* restrict h, int32_t* restrict a) {
	int32_t *ai = a;
	int32_t *bi = h->mem + h->cas;
	int32_t i = (int32_t) h->s_n;
	while (i--) {
		*bi = *(ai++);
		bi += 2;
	}
	ai = a + h->s_n;
	bi = h->mem + 1 - h->cas;
	i = (int32_t) h->d_n;
	while (i--) {
		*bi = *(ai++);
		bi += 2;
	}
}

void dwt53::region_decode_1d(grk_dwt53 *buffer) {
	int32_t *a = buffer->data - buffer->interleaved_offset;
	auto d_n = buffer->d_n;
	auto s_n = buffer->s_n;
	if (!buffer->odd_top_left_bit) {
		if ((d_n > 0) || (s_n > 1)) {
			/* inverse update */
			for (auto i = buffer->range_even.x; i < buffer->range_even.y; ++i)
				GROK_S(i)-= (GROK_D_(i - 1) + GROK_D_(i) + 2) >> 2;
				/* inverse predict */
				for (auto i = buffer->range_odd.x; i < buffer->range_odd.y; ++i)
				GROK_D(i) += (GROK_S_(i) + GROK_S_(i + 1)) >> 1;
			}
		}
		else {
			if (!s_n && d_n == 1)
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
void dwt53::region_interleave_h(grk_dwt53 *buffer_h, int32_t *tile_data) {
	int32_t *tile_data_ptr = tile_data;
	int32_t *buffer_data_ptr = buffer_h->data - buffer_h->interleaved_offset
			+ buffer_h->odd_top_left_bit;
	for (auto i = buffer_h->range_even.x; i < buffer_h->range_even.y; ++i) {
		buffer_data_ptr[i << 1] = tile_data_ptr[i];
	}
	tile_data_ptr = tile_data + buffer_h->s_n;
	buffer_data_ptr = buffer_h->data - buffer_h->interleaved_offset
			+ (buffer_h->odd_top_left_bit ^ 1);

	for (auto i = buffer_h->range_odd.x; i < buffer_h->range_odd.y; ++i) {
		buffer_data_ptr[i << 1] = tile_data_ptr[i];
	}
}

/* <summary>                             */
/* Inverse lazy transform (vertical).    */
/* </summary>                            */
void dwt53::region_interleave_v(grk_dwt53 *buffer_v, int32_t *tile_data,
		size_t stride) {
	int32_t *tile_data_ptr = tile_data;
	int32_t *buffer_data_ptr = buffer_v->data - buffer_v->interleaved_offset
			+ buffer_v->odd_top_left_bit;
	for (auto i = buffer_v->range_even.x; i < buffer_v->range_even.y; ++i) {
		buffer_data_ptr[i << 1] = tile_data_ptr[i * stride];
	}

	tile_data_ptr = tile_data + (buffer_v->s_n * stride);
	buffer_data_ptr = buffer_v->data - buffer_v->interleaved_offset
			+ (buffer_v->odd_top_left_bit ^ 1);

	for (auto i = buffer_v->range_odd.x; i < buffer_v->range_odd.y; ++i) {
		buffer_data_ptr[i << 1] = tile_data_ptr[i * stride];
	}
}

/* <summary>                            */
/* Inverse 5-3 data transform in 2-D. */
/* </summary>                           */
bool dwt53::region_decode(TileComponent *tilec, uint32_t numres,
		uint32_t numThreads) {
	if (numres == 1U) {
		return true;
	}

	int rc = 0;
	auto tileBuf = (int32_t*) tilec->buf->get_ptr( 0, 0, 0, 0);
	Barrier decode_dwt_barrier(numThreads);
	Barrier decode_dwt_calling_barrier(numThreads + 1);
	std::vector<std::thread> dwtWorkers;
	bool success = true;
	for (auto threadId = 0U; threadId < numThreads; threadId++) {
		dwtWorkers.push_back(
				std::thread(
						[this, tilec, numres, &rc, tileBuf, &decode_dwt_barrier,
								&decode_dwt_calling_barrier, threadId,
								numThreads, &success]() {
							auto numResolutions = numres;
							grk_dwt53 buffer_h;
							grk_dwt53 buffer_v;

							grk_tcd_resolution *tr = tilec->resolutions;

							uint32_t res_width = (tr->x1 - tr->x0); /* width of the resolution level computed */
							uint32_t res_height = (tr->y1 - tr->y0); /* height of the resolution level computed */

							uint32_t w = tilec->width();
							int32_t resno = 1;

							// add 2 for boundary, plus one for parity
							auto bufferDataSize =
									tilec->buf->get_interleaved_upper_bound() + 3;
							buffer_h.data = (int32_t*) grok_aligned_malloc(
									bufferDataSize * sizeof(int32_t));
							if (!buffer_h.data) {
								success = false;
								return;
							}

							buffer_v.data = buffer_h.data;

							while (--numResolutions) {
								/* start with the first resolution, and work upwards*/
								buffer_h.range_even =
										tilec->buf->get_uninterleaved_range(
												resno, true, true);
								buffer_h.range_odd =
										tilec->buf->get_uninterleaved_range(
												 resno, false, true);
								buffer_v.range_even =
										tilec->buf->get_uninterleaved_range(
												 resno, true, false);
								buffer_v.range_odd =
										tilec->buf->get_uninterleaved_range(
												 resno, false,
												false);

								grk_pt interleaved_h =
										tilec->buf->get_interleaved_range(
												resno, true);
								grk_pt interleaved_v =
										tilec->buf->get_interleaved_range(
												resno, false);

								buffer_h.s_n = res_width;
								buffer_v.s_n = res_height;
								buffer_v.interleaved_offset = std::max<int64_t>(
										0, interleaved_v.x - 2);

								++tr;
								res_width = (tr->x1 - tr->x0);
								res_height = (tr->y1 - tr->y0);

								buffer_h.d_n = (int64_t) (res_width
										- buffer_h.s_n);
								buffer_h.odd_top_left_bit = tr->x0 & 1;
								buffer_h.interleaved_offset = std::max<int64_t>(
										0, interleaved_h.x - 2);

								/* first do horizontal interleave */
								int32_t *restrict tiledp = tilec->buf->get_ptr(
										 0, 0, 0, 0)
										+ (buffer_v.range_even.x + threadId)
												* w;
								for (auto j = threadId;
										j
												< buffer_v.range_even.y
														- buffer_v.range_even.x;
										j += numThreads) {
									region_interleave_h(&buffer_h, tiledp);
									region_decode_1d(&buffer_h);
									memcpy(tiledp + interleaved_h.x,
											buffer_h.data + interleaved_h.x
													- buffer_h.interleaved_offset,
											(interleaved_h.y - interleaved_h.x)
													* sizeof(int32_t));
									tiledp += w * numThreads;
								}
								decode_dwt_barrier.arrive_and_wait();

								tiledp = tilec->buf->get_ptr( 0, 0, 0,
										0)
										+ (buffer_v.s_n + buffer_v.range_odd.x
												+ threadId) * w;
								for (auto j = threadId;
										j
												< buffer_v.range_odd.y
														- buffer_v.range_odd.x;
										j += numThreads) {
									region_interleave_h(&buffer_h, tiledp);
									region_decode_1d(&buffer_h);
									memcpy(tiledp + interleaved_h.x,
											buffer_h.data + interleaved_h.x
													- buffer_h.interleaved_offset,
											(interleaved_h.y - interleaved_h.x)
													* sizeof(int32_t));
									tiledp += (w * numThreads);
								}
								decode_dwt_barrier.arrive_and_wait();

								buffer_v.d_n = (res_height - buffer_v.s_n);
								buffer_v.odd_top_left_bit = tr->y0 & 1;

								// next do vertical interleave
								tiledp = tilec->buf->get_ptr( 0, 0, 0,
										0) + interleaved_h.x + threadId;
								for (auto j = threadId;
										j < interleaved_h.y - interleaved_h.x;
										j += numThreads) {
									int32_t *restrict tiledp_v = tiledp
											+ (interleaved_v.x) * w;
									region_interleave_v(&buffer_v, tiledp, w);
									region_decode_1d(&buffer_v);
									for (auto k = interleaved_v.x;
											k < interleaved_v.y; k++) {
										*tiledp_v = buffer_v.data[k
												- buffer_v.interleaved_offset];
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
	for (auto &t : dwtWorkers) {
		t.join();
	}
	return success;
}

}
