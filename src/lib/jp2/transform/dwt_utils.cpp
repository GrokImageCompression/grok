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
#include "grok_includes.h"
#include "T1Decoder.h"
#include <atomic>
#include "testing.h"
#include "HTParams.h"

namespace grk {

/* <summary>                             */
/* Determine maximum computed resolution level for inverse wavelet transform */
/* </summary>                            */
uint32_t dwt_utils::max_resolution(grk_tcd_resolution *GRK_RESTRICT r, uint32_t i) {
	uint32_t mr = 0;
	uint32_t w;
	while (--i) {
		++r;
		if (mr < (w = r->x1 - r->x0))
			mr = w;
		if (mr < (w = r->y1 - r->y0))
			mr = w;
	}
	return mr;
}

/* <summary>                             */
/* Forward lazy transform (vertical).    */
/* </summary>                            */
void dwt_utils::deinterleave_v(int32_t *a, int32_t *b, uint32_t d_n, uint32_t s_n,
		int32_t x, int32_t cas) {
	uint32_t i = s_n;
	int32_t *l_dest = b;
	int32_t *l_src = a + cas;

	while (i--) {
		*l_dest = *l_src;
		l_dest += x;
		l_src += 2;
	}

	l_dest = b + s_n * x;
	l_src = a + 1 - cas;

	i = d_n;
	while (i--) {
		*l_dest = *l_src;
		l_dest += x;
		l_src += 2;
	}
}

/* <summary>			                 */
/* Forward lazy transform (horizontal).  */
/* </summary>                            */
void dwt_utils::deinterleave_h(int32_t *a, int32_t *b, uint32_t d_n, uint32_t s_n,
		int32_t cas) {
	int32_t *l_dest = b;
	int32_t *l_src = a + cas;

	for (uint32_t i = 0; i < s_n; ++i) {
		*l_dest++ = *l_src;
		l_src += 2;
	}

	l_dest = b + s_n;
	l_src = a + 1 - cas;

	for (uint32_t i = 0; i < d_n; ++i) {
		*l_dest++ = *l_src;
		l_src += 2;
	}
}

/* <summary>                */
/* Get norm of 5-3 wavelet. */
/* </summary>               */
double dwt_utils::getnorm_53(uint32_t level, uint8_t orient) {
	return getnorm(level,orient,true);
}

/* <summary>                */
/* Get norm of 9-7 wavelet. */
/* </summary>               */
double dwt_utils::getnorm_97(uint32_t level, uint8_t orient) {
	return getnorm(level,orient,false);
}

double dwt_utils::getnorm(uint32_t level, uint8_t orient, bool reversible) {
	assert(orient <= 3);
	switch(orient){
	case 0:
		return sqrt_energy_gains::get_gain_l(level,reversible) *
				sqrt_energy_gains::get_gain_l(level,reversible);
		break;
	case 1:
	case 2:
		return sqrt_energy_gains::get_gain_l(level+1,reversible) *
				sqrt_energy_gains::get_gain_h(level,reversible);
		break;
	case 3:
		return sqrt_energy_gains::get_gain_h(level,reversible) *
				sqrt_energy_gains::get_gain_h(level,reversible);
		break;
	default:
		return 0;
	}
}



}
