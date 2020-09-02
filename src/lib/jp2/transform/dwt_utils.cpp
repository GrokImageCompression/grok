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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"
#include "T1Decoder.h"
#include <atomic>
#include "testing.h"
#include "HTParams.h"

namespace grk {

/* <summary>                             */
/* Determine maximum computed resolution level for inverse wavelet transform */
/* </summary>                            */
uint32_t dwt_utils::max_resolution(grk_resolution *GRK_RESTRICT r, uint32_t i) {
	uint32_t mr = 0;
	while (--i) {
		++r;
		uint32_t w;
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
		uint32_t stride, int32_t cas) {
	uint32_t i = s_n;
	int32_t *dest = b;
	int32_t *src = a + cas;

	while (i--) {
		*dest = *src;
		dest += stride;
		src += 2;
	}

	dest = b + s_n * stride;
	src = a + 1 - cas;

	i = d_n;
	while (i--) {
		*dest = *src;
		dest += stride;
		src += 2;
	}
}

/* <summary>			                 */
/* Forward lazy transform (horizontal).  */
/* </summary>                            */
void dwt_utils::deinterleave_h(int32_t *a, int32_t *b, uint32_t d_n, uint32_t s_n,
		int32_t cas) {
	int32_t *dest = b;
	int32_t *src = a + cas;

	for (uint32_t i = 0; i < s_n; ++i) {
		*dest++ = *src;
		src += 2;
	}

	dest = b + s_n;
	src = a + 1 - cas;

	for (uint32_t i = 0; i < d_n; ++i) {
		*dest++ = *src;
		src += 2;
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
