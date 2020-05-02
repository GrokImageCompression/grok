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
 * Copyright (c) 2006-2007, Parvatha Elangovan
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
#include "grk_apps_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include "grok.h"
#include "convert.h"
#include "common.h"

/* Component precision scaling */
void clip_component(grk_image_comp *component, uint32_t precision) {
	size_t len = (size_t) component->w * component->h;
	uint32_t umax = (1U << precision) - 1U;
	assert(precision <= 16);

	if (component->sgnd) {
		auto data = component->data;
		int32_t max = (int32_t) (umax / 2U);
		int32_t min = -max - 1;
		for (size_t i = 0; i < len; ++i) {
			if (data[i] > max)
				data[i] = max;
			else if (data[i] < min)
				data[i] = min;
		}
	} else {
		auto data = (uint32_t*) component->data;
		for (size_t i = 0; i < len; ++i) {
			if (data[i] > umax)
				data[i] = umax;
		}
	}
	component->prec = precision;
}

/* Component precision scaling */
static void scale_component_up(grk_image_comp *component, uint32_t precision) {
	size_t len = (size_t) component->w * component->h;

	if (component->sgnd) {
		int64_t newMax = (int64_t) 1U << (precision - 1);
		int64_t oldMax = (int64_t) 1U << (component->prec - 1);
		auto data = component->data;
		for (size_t i = 0; i < len; ++i)
			data[i] = (int32_t) (((int64_t) data[i] * newMax) / oldMax);
	} else {
		uint64_t newMax = ((uint64_t) 1U << precision) - 1U;
		uint64_t oldMax = ((uint64_t) 1U << component->prec) - 1U;
		auto data = (uint32_t*) component->data;
		for (size_t i = 0; i < len; ++i)
			data[i] = (uint32_t) (((uint64_t) data[i] * newMax) / oldMax);
	}
	component->prec = precision;
}
void scale_component(grk_image_comp *component, uint32_t precision) {
	if (component->prec == precision)
		return;
	if (component->prec < precision) {
		scale_component_up(component, precision);
		return;
	}
	uint32_t shift = (uint32_t) (component->prec - precision);
	size_t len = (size_t) component->w * component->h;
	if (component->sgnd) {
		auto data = component->data;
		for (size_t i = 0; i < len; ++i)
			data[i] >>= shift;
	} else {
		auto data = (uint32_t*) component->data;
		for (size_t i = 0; i < len; ++i)
			data[i] >>= shift;
	}
	component->prec = precision;
}

/*
planar <==> interleaved conversions
used by PNG/TIFF/JPEG
Source and destination are always signed, 32 bit
*/

////////////////////////
//interleaved ==> planar
template<size_t N> void interleavedToPlanar(const int32_t *pSrc, int32_t *const*pDst,
		size_t length){
	size_t src_index = 0;

	for (size_t i = 0; i < length; i++) {
		for (size_t j = 0; j < N; ++j)
			pDst[j][i] = pSrc[src_index++];
	}
}
template<> void interleavedToPlanar<1>(const int32_t *pSrc, int32_t *const*pDst,
		size_t length){
	memcpy(pDst[0], pSrc, length * sizeof(int32_t));
}
const cvtInterleavedToPlanar cvtInterleavedToPlanar_LUT[10] = {
		nullptr,
		interleavedToPlanar<1>,
		interleavedToPlanar<2>,
		interleavedToPlanar<3>,
		interleavedToPlanar<4>,
		interleavedToPlanar<5>,
		interleavedToPlanar<6>,
		interleavedToPlanar<7>,
		interleavedToPlanar<8>,
		interleavedToPlanar<9>
};
////////////////////////
//planar ==> interleaved
template<size_t N> void planarToInterleaved(int32_t const *const*pSrc, int32_t *pDst,
		size_t length, int32_t adjust){
	for (size_t i = 0; i < length; i++) {
		for (size_t j = 0; j < N; ++j)
			pDst[N * i + j] = pSrc[j][i] + adjust;
	}
}
const cvtPlanarToInterleaved cvtPlanarToInterleaved_LUT[10] = {
		nullptr,
		planarToInterleaved<1>,
		planarToInterleaved<2>,
		planarToInterleaved<3>,
		planarToInterleaved<4>,
		planarToInterleaved<5>,
		planarToInterleaved<6>,
		planarToInterleaved<7>,
		planarToInterleaved<8>,
		planarToInterleaved<9>
};


/*
* bit depth conversions for bit depth <= 8 and 16
* used by PNG/TIFF
*
* Note: if source bit depth is < 8, then only unsigned is valid,
* as we don't know how to manage the sign bit for signed data
 *
 */

/**
 * 1 bit unsigned to 32 bit
 */
static void convert_1u32s_C1R(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val = *pSrc++;
		pDst[i + 0] = INV((int32_t )(val >> 7), 1, invert);
		pDst[i + 1] = INV((int32_t )((val >> 6) & 0x1U), 1, invert);
		pDst[i + 2] = INV((int32_t )((val >> 5) & 0x1U), 1, invert);
		pDst[i + 3] = INV((int32_t )((val >> 4) & 0x1U), 1, invert);
		pDst[i + 4] = INV((int32_t )((val >> 3) & 0x1U), 1, invert);
		pDst[i + 5] = INV((int32_t )((val >> 2) & 0x1U), 1, invert);
		pDst[i + 6] = INV((int32_t )((val >> 1) & 0x1U), 1, invert);
		pDst[i + 7] = INV((int32_t )(val & 0x1U), 1, invert);
	}
	if (length & 7U) {
		uint32_t val = *pSrc++;
		length = length & 7U;
		pDst[i + 0] = INV((int32_t )(val >> 7), 1, invert);

		if (length > 1U) {
			pDst[i + 1] = INV((int32_t )((val >> 6) & 0x1U), 1, invert);
			if (length > 2U) {
				pDst[i + 2] = INV((int32_t )((val >> 5) & 0x1U), 1, invert);
				if (length > 3U) {
					pDst[i + 3] = INV((int32_t )((val >> 4) & 0x1U), 1, invert);
					if (length > 4U) {
						pDst[i + 4] = INV((int32_t )((val >> 3) & 0x1U), 1,
								invert);
						if (length > 5U) {
							pDst[i + 5] = INV((int32_t )((val >> 2) & 0x1U), 1,
									invert);
							if (length > 6U) {
								pDst[i + 6] = INV((int32_t )((val >> 1) & 0x1U),
										1, invert);
							}
						}
					}
				}
			}
		}
	}
}
/**
 * 2 bit unsigned to 32 bit
 */
static void convert_2u32s_C1R(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t val = *pSrc++;
		pDst[i + 0] = INV((int32_t )(val >> 6), 3, invert);
		pDst[i + 1] = INV((int32_t )((val >> 4) & 0x3U), 3, invert);
		pDst[i + 2] = INV((int32_t )((val >> 2) & 0x3U), 3, invert);
		pDst[i + 3] = INV((int32_t )(val & 0x3U), 3, invert);
	}
	if (length & 3U) {
		uint32_t val = *pSrc++;
		length = length & 3U;
		pDst[i + 0] = INV((int32_t )(val >> 6), 3, invert);

		if (length > 1U) {
			pDst[i + 1] = INV((int32_t )((val >> 4) & 0x3U), 3, invert);
			if (length > 2U) {
				pDst[i + 2] = INV((int32_t )((val >> 2) & 0x3U), 3, invert);

			}
		}
	}
}
/**
 * 4 bit unsigned to 32 bit
 */
static void convert_4u32s_C1R(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 1U); i += 2U) {
		uint32_t val = *pSrc++;
		pDst[i + 0] = INV((int32_t )(val >> 4), 15, invert);
		pDst[i + 1] = INV((int32_t )(val & 0xFU), 15, invert);
	}
	if (length & 1U) {
		uint8_t val = *pSrc++;
		pDst[i + 0] = INV((int32_t )(val >> 4), 15, invert);
	}
}
/**
 * 6 bit unsigned to 32 bit
 */
static void convert_6u32s_C1R(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		pDst[i + 0] = INV((int32_t )(val0 >> 2), 63, invert);
		pDst[i + 1] = INV((int32_t )(((val0 & 0x3U) << 4) | (val1 >> 4)), 63,
				invert);
		pDst[i + 2] = INV((int32_t )(((val1 & 0xFU) << 2) | (val2 >> 6)), 63,
				invert);
		pDst[i + 3] = INV((int32_t )(val2 & 0x3FU), 63, invert);

	}
	if (length & 3U) {
		uint32_t val0 = *pSrc++;
		length = length & 3U;
		pDst[i + 0] = INV((int32_t )(val0 >> 2), 63, invert);

		if (length > 1U) {
			uint32_t val1 = *pSrc++;
			pDst[i + 1] = INV((int32_t )(((val0 & 0x3U) << 4) | (val1 >> 4)),
					63, invert);
			if (length > 2U) {
				uint32_t val2 = *pSrc++;
				pDst[i + 2] = INV(
						(int32_t )(((val1 & 0xFU) << 2) | (val2 >> 6)), 63,
						invert);
			}
		}
	}
}
/**
 * 8 bit signed/unsigned to 32 bit
 */
static void convert_8u32s_C1R(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	for (size_t i = 0; i < length; i++)
		pDst[i] = INV(pSrc[i], 0xFF, invert);
}

/**
 * 16 bit signed/unsigned to 32 bit
 */
void convert_16u32s_C1R(const uint8_t *pSrc, int32_t *pDst,
		size_t length, bool invert) {
	size_t i;
	for (i = 0; i < length; i++) {
		int32_t val0 = *pSrc++;
		int32_t val1 = *pSrc++;
		pDst[i] = INV(val0 << 8 | val1, 0xFFFF, invert);
	}
}
const cvtTo32 cvtTo32_LUT[9] = {
		nullptr,
		convert_1u32s_C1R,
		convert_2u32s_C1R,
		nullptr,
		convert_4u32s_C1R,
		nullptr,
		convert_6u32s_C1R,
		nullptr,
		convert_8u32s_C1R
};

/**
 * 32 bit to 1 bit
 */
static void convert_32s1u_C1R(const int32_t *pSrc, uint8_t *pDst,
		size_t length) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];
		uint32_t src4 = (uint32_t) pSrc[i + 4];
		uint32_t src5 = (uint32_t) pSrc[i + 5];
		uint32_t src6 = (uint32_t) pSrc[i + 6];
		uint32_t src7 = (uint32_t) pSrc[i + 7];

		*pDst++ = (uint8_t) ((src0 << 7) | (src1 << 6) | (src2 << 5)
				| (src3 << 4) | (src4 << 3) | (src5 << 2) | (src6 << 1) | src7);
	}

	if (length & 7U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;
		uint32_t src3 = 0U;
		uint32_t src4 = 0U;
		uint32_t src5 = 0U;
		uint32_t src6 = 0U;
		length = length & 7U;

		if (length > 1U) {
			src1 = (uint32_t) pSrc[i + 1];
			if (length > 2U) {
				src2 = (uint32_t) pSrc[i + 2];
				if (length > 3U) {
					src3 = (uint32_t) pSrc[i + 3];
					if (length > 4U) {
						src4 = (uint32_t) pSrc[i + 4];
						if (length > 5U) {
							src5 = (uint32_t) pSrc[i + 5];
							if (length > 6U) {
								src6 = (uint32_t) pSrc[i + 6];
							}
						}
					}
				}
			}
		}
		*pDst++ = (uint8_t) ((src0 << 7) | (src1 << 6) | (src2 << 5)
				| (src3 << 4) | (src4 << 3) | (src5 << 2) | (src6 << 1));
	}
}

/**
 * 32 bit to 2 bit
 */
static void convert_32s2u_C1R(const int32_t *pSrc, uint8_t *pDst,
		size_t length) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];

		*pDst++ = (uint8_t) ((src0 << 6) | (src1 << 4) | (src2 << 2) | src3);
	}

	if (length & 3U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;
		length = length & 3U;

		if (length > 1U) {
			src1 = (uint32_t) pSrc[i + 1];
			if (length > 2U) {
				src2 = (uint32_t) pSrc[i + 2];
			}
		}
		*pDst++ = (uint8_t) ((src0 << 6) | (src1 << 4) | (src2 << 2));
	}
}

/**
 * 32 bit to 4 bit
 */
static void convert_32s4u_C1R(const int32_t *pSrc, uint8_t *pDst,
		size_t length) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 1U); i += 2U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];

		*pDst++ = (uint8_t) ((src0 << 4) | src1);
	}

	if (length & 1U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		*pDst++ = (uint8_t) ((src0 << 4));
	}
}

/**
 * 32 bit to 6 bit
 */
static void convert_32s6u_C1R(const int32_t *pSrc, uint8_t *pDst,
		size_t length) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];

		*pDst++ = (uint8_t) ((src0 << 2) | (src1 >> 4));
		*pDst++ = (uint8_t) (((src1 & 0xFU) << 4) | (src2 >> 2));
		*pDst++ = (uint8_t) (((src2 & 0x3U) << 6) | src3);
	}

	if (length & 3U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;
		length = length & 3U;

		if (length > 1U) {
			src1 = (uint32_t) pSrc[i + 1];
			if (length > 2U) {
				src2 = (uint32_t) pSrc[i + 2];
			}
		}
		*pDst++ = (uint8_t) ((src0 << 2) | (src1 >> 4));
		if (length > 1U) {
			*pDst++ = (uint8_t) (((src1 & 0xFU) << 4) | (src2 >> 2));
			if (length > 2U) {
				*pDst++ = (uint8_t) (((src2 & 0x3U) << 6));
			}
		}
	}
}
/**
 * 32 bit to 8 bit
 */
static void convert_32s8u_C1R(const int32_t *pSrc, uint8_t *pDst,
		size_t length) {
	for (size_t i = 0; i < length; ++i)
		pDst[i] = (uint8_t) pSrc[i];
}
const cvtFrom32 cvtFrom32_LUT[9] = {
		nullptr,
		convert_32s1u_C1R,
		convert_32s2u_C1R,
		nullptr,
		convert_32s4u_C1R,
		nullptr,
		convert_32s6u_C1R,
		nullptr,
		convert_32s8u_C1R
};
