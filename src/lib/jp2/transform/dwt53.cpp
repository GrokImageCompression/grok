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
#include "grok_includes.h"
#include "T1Decoder.h"
#include <atomic>
#include "testing.h"
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

}
