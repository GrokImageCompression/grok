/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
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
#include <algorithm>
#include <limits>

#ifdef _WIN32
#include <intrin.h>
#endif

/*
planar <==> interleaved conversions
used by PNG/TIFF/JPEG
Source and destination are always signed, 32 bit
*/

////////////////////////
// interleaved ==> planar
template<size_t N>
void interleavedToPlanar(const int32_t* pSrc, int32_t* const* pDst, size_t length)
{
	size_t src_index = 0;

	for(size_t i = 0; i < length; i++)
	{
		for(size_t j = 0; j < N; ++j)
			pDst[j][i] = pSrc[src_index++];
	}
}
template<>
void interleavedToPlanar<1>(const int32_t* pSrc, int32_t* const* pDst, size_t length)
{
	memcpy(pDst[0], pSrc, length * sizeof(int32_t));
}
const cvtInterleavedToPlanar cvtInterleavedToPlanar_LUT[10] = {nullptr,
															   interleavedToPlanar<1>,
															   interleavedToPlanar<2>,
															   interleavedToPlanar<3>,
															   interleavedToPlanar<4>,
															   interleavedToPlanar<5>,
															   interleavedToPlanar<6>,
															   interleavedToPlanar<7>,
															   interleavedToPlanar<8>,
															   interleavedToPlanar<9>};
////////////////////////
// planar ==> interleaved
template<size_t N>
void planarToInterleaved(int32_t const* const* pSrc, int32_t* pDst, size_t length, int32_t adjust)
{
	for(size_t i = 0; i < length; i++)
	{
		for(size_t j = 0; j < N; ++j)
			pDst[N * i + j] = pSrc[j][i] + adjust;
	}
}
const cvtPlanarToInterleaved cvtPlanarToInterleaved_LUT[10] = {nullptr,
															   planarToInterleaved<1>,
															   planarToInterleaved<2>,
															   planarToInterleaved<3>,
															   planarToInterleaved<4>,
															   planarToInterleaved<5>,
															   planarToInterleaved<6>,
															   planarToInterleaved<7>,
															   planarToInterleaved<8>,
															   planarToInterleaved<9>};

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
static void convert_1u32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t val = *pSrc++;
		pDst[i + 0] = INV((int32_t)(val >> 7), 1, invert);
		pDst[i + 1] = INV((int32_t)((val >> 6) & 0x1U), 1, invert);
		pDst[i + 2] = INV((int32_t)((val >> 5) & 0x1U), 1, invert);
		pDst[i + 3] = INV((int32_t)((val >> 4) & 0x1U), 1, invert);
		pDst[i + 4] = INV((int32_t)((val >> 3) & 0x1U), 1, invert);
		pDst[i + 5] = INV((int32_t)((val >> 2) & 0x1U), 1, invert);
		pDst[i + 6] = INV((int32_t)((val >> 1) & 0x1U), 1, invert);
		pDst[i + 7] = INV((int32_t)(val & 0x1U), 1, invert);
	}

	length = length & 7U;
	if(length)
	{
		uint32_t val = *pSrc++;
		for(size_t j = 0; j < length; ++j)
			pDst[i + j] = INV((int32_t)(val >> (7 - j)), 1, invert);
	}
}
/**
 * 2 bit unsigned to 32 bit
 */
static void convert_2u32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)3U); i += 4U)
	{
		uint32_t val = *pSrc++;
		pDst[i + 0] = INV((int32_t)(val >> 6), 3, invert);
		pDst[i + 1] = INV((int32_t)((val >> 4) & 0x3U), 3, invert);
		pDst[i + 2] = INV((int32_t)((val >> 2) & 0x3U), 3, invert);
		pDst[i + 3] = INV((int32_t)(val & 0x3U), 3, invert);
	}
	if(length & 3U)
	{
		uint32_t val = *pSrc++;
		length = length & 3U;
		pDst[i + 0] = INV((int32_t)(val >> 6), 3, invert);

		if(length > 1U)
		{
			pDst[i + 1] = INV((int32_t)((val >> 4) & 0x3U), 3, invert);
			if(length > 2U)
			{
				pDst[i + 2] = INV((int32_t)((val >> 2) & 0x3U), 3, invert);
			}
		}
	}
}
/**
 * 4 bit unsigned to 32 bit
 */
static void convert_4u32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)1U); i += 2U)
	{
		uint32_t val = *pSrc++;
		pDst[i + 0] = INV((int32_t)(val >> 4), 15, invert);
		pDst[i + 1] = INV((int32_t)(val & 0xFU), 15, invert);
	}
	if(length & 1U)
	{
		uint8_t val = *pSrc++;
		pDst[i + 0] = INV((int32_t)(val >> 4), 15, invert);
	}
}

int32_t sign_extend(int32_t val, uint8_t shift)
{
	val <<= shift;
	val >>= shift;

	return val;
}

/**
 * 4 bit signed to 32 bit
 */
static void convert_4s32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)1U); i += 2U)
	{
		uint8_t val = *pSrc++;
		pDst[i + 0] = INV(sign_extend(val >> 4, 32 - 4), 0xF, invert);
		pDst[i + 1] = INV(sign_extend(val & 0xF, 32 - 4), 0xF, invert);
	}
	if(length & 1U)
	{
		uint8_t val = *pSrc++;
		pDst[i + 0] = INV(sign_extend(val >> 4, 32 - 4), 15, invert);
	}
}

/**
 * 6 bit unsigned to 32 bit
 */
static void convert_6u32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)3U); i += 4U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		pDst[i + 0] = INV((int32_t)(val0 >> 2), 63, invert);
		pDst[i + 1] = INV((int32_t)(((val0 & 0x3U) << 4) | (val1 >> 4)), 63, invert);
		pDst[i + 2] = INV((int32_t)(((val1 & 0xFU) << 2) | (val2 >> 6)), 63, invert);
		pDst[i + 3] = INV((int32_t)(val2 & 0x3FU), 63, invert);
	}
	if(length & 3U)
	{
		uint32_t val0 = *pSrc++;
		length = length & 3U;
		pDst[i + 0] = INV((int32_t)(val0 >> 2), 63, invert);

		if(length > 1U)
		{
			uint32_t val1 = *pSrc++;
			pDst[i + 1] = INV((int32_t)(((val0 & 0x3U) << 4) | (val1 >> 4)), 63, invert);
			if(length > 2U)
			{
				uint32_t val2 = *pSrc++;
				pDst[i + 2] = INV((int32_t)(((val1 & 0xFU) << 2) | (val2 >> 6)), 63, invert);
			}
		}
	}
}
/**
 * 8 bit signed/unsigned to 32 bit
 */
static void convert_8u32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	for(size_t i = 0; i < length; i++)
		pDst[i] = INV(pSrc[i], 0xFF, invert);
}

/**
 * 16 bit signed/unsigned to 32 bit
 */
void convert_16u32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < length; i++)
	{
		int32_t val0 = *pSrc++;
		int32_t val1 = *pSrc++;
		pDst[i] = INV(val0 << 8 | val1, 0xFFFF, invert);
	}
}
const cvtTo32 cvtTo32_LUT[9] = {nullptr,		   convert_1u32s_C1R, convert_2u32s_C1R,
								nullptr,		   convert_4u32s_C1R, nullptr,
								convert_6u32s_C1R, nullptr,			  convert_8u32s_C1R};

const cvtTo32 cvtsTo32_LUT[9] = {nullptr,			convert_1u32s_C1R, convert_2u32s_C1R,
								 nullptr,			convert_4s32s_C1R, nullptr,
								 convert_6u32s_C1R, nullptr,		   convert_8u32s_C1R};

/**
 * convert 1 bpp to 8 bit
 */
static void convert_32s1u_C1R(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];
		uint32_t src2 = (uint32_t)pSrc[i + 2];
		uint32_t src3 = (uint32_t)pSrc[i + 3];
		uint32_t src4 = (uint32_t)pSrc[i + 4];
		uint32_t src5 = (uint32_t)pSrc[i + 5];
		uint32_t src6 = (uint32_t)pSrc[i + 6];
		uint32_t src7 = (uint32_t)pSrc[i + 7];

		*pDst++ = (uint8_t)((src0 << 7) | (src1 << 6) | (src2 << 5) | (src3 << 4) | (src4 << 3) |
							(src5 << 2) | (src6 << 1) | src7);
	}

	if(length & 7U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;
		uint32_t src3 = 0U;
		uint32_t src4 = 0U;
		uint32_t src5 = 0U;
		uint32_t src6 = 0U;
		length = length & 7U;

		if(length > 1U)
		{
			src1 = (uint32_t)pSrc[i + 1];
			if(length > 2U)
			{
				src2 = (uint32_t)pSrc[i + 2];
				if(length > 3U)
				{
					src3 = (uint32_t)pSrc[i + 3];
					if(length > 4U)
					{
						src4 = (uint32_t)pSrc[i + 4];
						if(length > 5U)
						{
							src5 = (uint32_t)pSrc[i + 5];
							if(length > 6U)
							{
								src6 = (uint32_t)pSrc[i + 6];
							}
						}
					}
				}
			}
		}
		*pDst++ = (uint8_t)((src0 << 7) | (src1 << 6) | (src2 << 5) | (src3 << 4) | (src4 << 3) |
							(src5 << 2) | (src6 << 1));
	}
}

/**
 * convert 2 bpp to 8 bit
 */
static void convert_32s2u_C1R(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)3U); i += 4U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];
		uint32_t src2 = (uint32_t)pSrc[i + 2];
		uint32_t src3 = (uint32_t)pSrc[i + 3];

		*pDst++ = (uint8_t)((src0 << 6) | (src1 << 4) | (src2 << 2) | src3);
	}

	if(length & 3U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;
		length = length & 3U;

		if(length > 1U)
		{
			src1 = (uint32_t)pSrc[i + 1];
			if(length > 2U)
			{
				src2 = (uint32_t)pSrc[i + 2];
			}
		}
		*pDst++ = (uint8_t)((src0 << 6) | (src1 << 4) | (src2 << 2));
	}
}

/**
 * convert 4 bpp to 8 bit
 */
static void convert_32s4u_C1R(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)1U); i += 2U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];
		// IMPORTANT NOTE: we need to mask src1 to 4 bits,
		// to prevent sign extension bits of negative src1,
		// extending beyond 4 bits,
		// from contributing to destination value
		*pDst++ = (uint8_t)((src0 << 4) | (src1 & 0xF));
	}

	if(length & 1U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		*pDst++ = (uint8_t)((src0 << 4));
	}
}

/**
 * convert 6 bpp to 8 bit
 */
static void convert_32s6u_C1R(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)3U); i += 4U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];
		uint32_t src2 = (uint32_t)pSrc[i + 2];
		uint32_t src3 = (uint32_t)pSrc[i + 3];

		*pDst++ = (uint8_t)((src0 << 2) | (src1 >> 4));
		*pDst++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 2));
		*pDst++ = (uint8_t)(((src2 & 0x3U) << 6) | src3);
	}

	if(length & 3U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;
		length = length & 3U;

		if(length > 1U)
		{
			src1 = (uint32_t)pSrc[i + 1];
			if(length > 2U)
			{
				src2 = (uint32_t)pSrc[i + 2];
			}
		}
		*pDst++ = (uint8_t)((src0 << 2) | (src1 >> 4));
		if(length > 1U)
		{
			*pDst++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 2));
			if(length > 2U)
			{
				*pDst++ = (uint8_t)(((src2 & 0x3U) << 6));
			}
		}
	}
}
/**
 * convert 8 bpp to 8 bit
 */
static void convert_32s8u_C1R(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	for(size_t i = 0; i < length; ++i)
		pDst[i] = (uint8_t)pSrc[i];
}
const cvtFrom32 cvtFrom32_LUT[9] = {nullptr,		   convert_32s1u_C1R, convert_32s2u_C1R,
									nullptr,		   convert_32s4u_C1R, nullptr,
									convert_32s6u_C1R, nullptr,			  convert_32s8u_C1R};

/////////////////////////////////////////////////////////////////////////
// routines to convert image pixels => TIFF pixels for various precisions

#define PUTBITS2(s, nb)                                              \
	trailing <<= remaining;                                          \
	trailing |= (uint32_t)((s) >> (nb - remaining));                 \
	*pDst++ = (uint8_t)trailing;                                     \
	trailing = (uint32_t)((s) & ((1U << (nb - remaining)) - 1U));    \
	if(nb >= (remaining + 8))                                        \
	{                                                                \
		*pDst++ = (uint8_t)(trailing >> (nb - (remaining + 8)));     \
		trailing &= (uint32_t)((1U << (nb - (remaining + 8))) - 1U); \
		remaining += 16 - nb;                                        \
	}                                                                \
	else                                                             \
	{                                                                \
		remaining += 8 - nb;                                         \
	}

#define PUTBITS(s, nb)             \
	if(nb >= remaining)            \
	{                              \
		PUTBITS2(s, nb)            \
	}                              \
	else                           \
	{                              \
		trailing <<= nb;           \
		trailing |= (uint32_t)(s); \
		remaining -= nb;           \
	}
#define FLUSHBITS()                  \
	if(remaining != 8)               \
	{                                \
		trailing <<= remaining;      \
		*pDst++ = (uint8_t)trailing; \
	}

void convert_tif_32sto3u(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;

	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];
		uint32_t src2 = (uint32_t)pSrc[i + 2];
		uint32_t src3 = (uint32_t)pSrc[i + 3];
		uint32_t src4 = (uint32_t)pSrc[i + 4];
		uint32_t src5 = (uint32_t)pSrc[i + 5];
		uint32_t src6 = (uint32_t)pSrc[i + 6];
		uint32_t src7 = (uint32_t)pSrc[i + 7];

		*pDst++ = (uint8_t)((src0 << 5) | (src1 << 2) | (src2 >> 1));
		*pDst++ = (uint8_t)((src2 << 7) | (src3 << 4) | (src4 << 1) | (src5 >> 2));
		*pDst++ = (uint8_t)((src5 << 6) | (src6 << 3) | (src7));
	}

	length &= 7U;
	if(length)
	{
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < length; ++j)
		{
			PUTBITS((uint32_t)pSrc[i + j], 3);
		}
		FLUSHBITS()
	}
}

void convert_tif_32sto5u(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;

	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];
		uint32_t src2 = (uint32_t)pSrc[i + 2];
		uint32_t src3 = (uint32_t)pSrc[i + 3];
		uint32_t src4 = (uint32_t)pSrc[i + 4];
		uint32_t src5 = (uint32_t)pSrc[i + 5];
		uint32_t src6 = (uint32_t)pSrc[i + 6];
		uint32_t src7 = (uint32_t)pSrc[i + 7];

		*pDst++ = (uint8_t)((src0 << 3) | (src1 >> 2));
		*pDst++ = (uint8_t)((src1 << 6) | (src2 << 1) | (src3 >> 4));
		*pDst++ = (uint8_t)((src3 << 4) | (src4 >> 1));
		*pDst++ = (uint8_t)((src4 << 7) | (src5 << 2) | (src6 >> 3));
		*pDst++ = (uint8_t)((src6 << 5) | (src7));
	}

	length &= 7U;
	if(length)
	{
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < length; ++j)
		{
			PUTBITS((uint32_t)pSrc[i + j], 5);
		}
		FLUSHBITS()
	}
}

void convert_tif_32sto7u(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;

	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];
		uint32_t src2 = (uint32_t)pSrc[i + 2];
		uint32_t src3 = (uint32_t)pSrc[i + 3];
		uint32_t src4 = (uint32_t)pSrc[i + 4];
		uint32_t src5 = (uint32_t)pSrc[i + 5];
		uint32_t src6 = (uint32_t)pSrc[i + 6];
		uint32_t src7 = (uint32_t)pSrc[i + 7];

		*pDst++ = (uint8_t)((src0 << 1) | (src1 >> 6));
		*pDst++ = (uint8_t)((src1 << 2) | (src2 >> 5));
		*pDst++ = (uint8_t)((src2 << 3) | (src3 >> 4));
		*pDst++ = (uint8_t)((src3 << 4) | (src4 >> 3));
		*pDst++ = (uint8_t)((src4 << 5) | (src5 >> 2));
		*pDst++ = (uint8_t)((src5 << 6) | (src6 >> 1));
		*pDst++ = (uint8_t)((src6 << 7) | (src7));
	}

	length &= 7U;
	if(length)
	{
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < length; ++j)
		{
			PUTBITS((uint32_t)pSrc[i + j], 7);
		}
		FLUSHBITS()
	}
}

void convert_tif_32sto9u(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;

	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];
		uint32_t src2 = (uint32_t)pSrc[i + 2];
		uint32_t src3 = (uint32_t)pSrc[i + 3];
		uint32_t src4 = (uint32_t)pSrc[i + 4];
		uint32_t src5 = (uint32_t)pSrc[i + 5];
		uint32_t src6 = (uint32_t)pSrc[i + 6];
		uint32_t src7 = (uint32_t)pSrc[i + 7];

		*pDst++ = (uint8_t)((src0 >> 1));
		*pDst++ = (uint8_t)((src0 << 7) | (src1 >> 2));
		*pDst++ = (uint8_t)((src1 << 6) | (src2 >> 3));
		*pDst++ = (uint8_t)((src2 << 5) | (src3 >> 4));
		*pDst++ = (uint8_t)((src3 << 4) | (src4 >> 5));
		*pDst++ = (uint8_t)((src4 << 3) | (src5 >> 6));
		*pDst++ = (uint8_t)((src5 << 2) | (src6 >> 7));
		*pDst++ = (uint8_t)((src6 << 1) | (src7 >> 8));
		*pDst++ = (uint8_t)(src7);
	}

	length &= 7U;
	if(length)
	{
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < length; ++j)
		{
			PUTBITS2((uint32_t)pSrc[i + j], 9);
		}
		FLUSHBITS()
	}
}

void convert_tif_32sto10u(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)3U); i += 4U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];
		uint32_t src2 = (uint32_t)pSrc[i + 2];
		uint32_t src3 = (uint32_t)pSrc[i + 3];

		*pDst++ = (uint8_t)(src0 >> 2);
		*pDst++ = (uint8_t)(((src0 & 0x3U) << 6) | (src1 >> 4));
		*pDst++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 6));
		*pDst++ = (uint8_t)(((src2 & 0x3FU) << 2) | (src3 >> 8));
		*pDst++ = (uint8_t)(src3);
	}

	if(length & 3U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;
		length = length & 3U;

		if(length > 1U)
		{
			src1 = (uint32_t)pSrc[i + 1];
			if(length > 2U)
			{
				src2 = (uint32_t)pSrc[i + 2];
			}
		}
		*pDst++ = (uint8_t)(src0 >> 2);
		*pDst++ = (uint8_t)(((src0 & 0x3U) << 6) | (src1 >> 4));
		if(length > 1U)
		{
			*pDst++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 6));
			if(length > 2U)
			{
				*pDst++ = (uint8_t)(((src2 & 0x3FU) << 2));
			}
		}
	}
}

void convert_tif_32sto11u(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;

	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];
		uint32_t src2 = (uint32_t)pSrc[i + 2];
		uint32_t src3 = (uint32_t)pSrc[i + 3];
		uint32_t src4 = (uint32_t)pSrc[i + 4];
		uint32_t src5 = (uint32_t)pSrc[i + 5];
		uint32_t src6 = (uint32_t)pSrc[i + 6];
		uint32_t src7 = (uint32_t)pSrc[i + 7];

		*pDst++ = (uint8_t)((src0 >> 3));
		*pDst++ = (uint8_t)((src0 << 5) | (src1 >> 6));
		*pDst++ = (uint8_t)((src1 << 2) | (src2 >> 9));
		*pDst++ = (uint8_t)((src2 >> 1));
		*pDst++ = (uint8_t)((src2 << 7) | (src3 >> 4));
		*pDst++ = (uint8_t)((src3 << 4) | (src4 >> 7));
		*pDst++ = (uint8_t)((src4 << 1) | (src5 >> 10));
		*pDst++ = (uint8_t)((src5 >> 2));
		*pDst++ = (uint8_t)((src5 << 6) | (src6 >> 5));
		*pDst++ = (uint8_t)((src6 << 3) | (src7 >> 8));
		*pDst++ = (uint8_t)(src7);
	}

	length &= 7U;
	if(length)
	{
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < length; ++j)
		{
			PUTBITS2((uint32_t)pSrc[i + j], 11);
		}
		FLUSHBITS()
	}
}
void convert_tif_32sto12u(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)1U); i += 2U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];

		*pDst++ = (uint8_t)(src0 >> 4);
		*pDst++ = (uint8_t)(((src0 & 0xFU) << 4) | (src1 >> 8));
		*pDst++ = (uint8_t)(src1);
	}

	if(length & 1U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		*pDst++ = (uint8_t)(src0 >> 4);
		*pDst++ = (uint8_t)(((src0 & 0xFU) << 4));
	}
}

void convert_tif_32sto13u(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;

	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];
		uint32_t src2 = (uint32_t)pSrc[i + 2];
		uint32_t src3 = (uint32_t)pSrc[i + 3];
		uint32_t src4 = (uint32_t)pSrc[i + 4];
		uint32_t src5 = (uint32_t)pSrc[i + 5];
		uint32_t src6 = (uint32_t)pSrc[i + 6];
		uint32_t src7 = (uint32_t)pSrc[i + 7];

		*pDst++ = (uint8_t)((src0 >> 5));
		*pDst++ = (uint8_t)((src0 << 3) | (src1 >> 10));
		*pDst++ = (uint8_t)((src1 >> 2));
		*pDst++ = (uint8_t)((src1 << 6) | (src2 >> 7));
		*pDst++ = (uint8_t)((src2 << 1) | (src3 >> 12));
		*pDst++ = (uint8_t)((src3 >> 4));
		*pDst++ = (uint8_t)((src3 << 4) | (src4 >> 9));
		*pDst++ = (uint8_t)((src4 >> 1));
		*pDst++ = (uint8_t)((src4 << 7) | (src5 >> 6));
		*pDst++ = (uint8_t)((src5 << 2) | (src6 >> 11));
		*pDst++ = (uint8_t)((src6 >> 3));
		*pDst++ = (uint8_t)((src6 << 5) | (src7 >> 8));
		*pDst++ = (uint8_t)(src7);
	}

	length &= 7U;
	if(length)
	{
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < length; ++j)
		{
			PUTBITS2((uint32_t)pSrc[i + j], 13);
		}
		FLUSHBITS()
	}
}

void convert_tif_32sto14u(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)3U); i += 4U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];
		uint32_t src2 = (uint32_t)pSrc[i + 2];
		uint32_t src3 = (uint32_t)pSrc[i + 3];

		*pDst++ = (uint8_t)(src0 >> 6);
		*pDst++ = (uint8_t)(((src0 & 0x3FU) << 2) | (src1 >> 12));
		*pDst++ = (uint8_t)(src1 >> 4);
		*pDst++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 10));
		*pDst++ = (uint8_t)(src2 >> 2);
		*pDst++ = (uint8_t)(((src2 & 0x3U) << 6) | (src3 >> 8));
		*pDst++ = (uint8_t)(src3);
	}

	if(length & 3U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = 0U;
		uint32_t src2 = 0U;
		length = length & 3U;

		if(length > 1U)
		{
			src1 = (uint32_t)pSrc[i + 1];
			if(length > 2U)
			{
				src2 = (uint32_t)pSrc[i + 2];
			}
		}
		*pDst++ = (uint8_t)(src0 >> 6);
		*pDst++ = (uint8_t)(((src0 & 0x3FU) << 2) | (src1 >> 12));
		if(length > 1U)
		{
			*pDst++ = (uint8_t)(src1 >> 4);
			*pDst++ = (uint8_t)(((src1 & 0xFU) << 4) | (src2 >> 10));
			if(length > 2U)
			{
				*pDst++ = (uint8_t)(src2 >> 2);
				*pDst++ = (uint8_t)(((src2 & 0x3U) << 6));
			}
		}
	}
}

void convert_tif_32sto15u(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;

	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t src0 = (uint32_t)pSrc[i + 0];
		uint32_t src1 = (uint32_t)pSrc[i + 1];
		uint32_t src2 = (uint32_t)pSrc[i + 2];
		uint32_t src3 = (uint32_t)pSrc[i + 3];
		uint32_t src4 = (uint32_t)pSrc[i + 4];
		uint32_t src5 = (uint32_t)pSrc[i + 5];
		uint32_t src6 = (uint32_t)pSrc[i + 6];
		uint32_t src7 = (uint32_t)pSrc[i + 7];

		*pDst++ = (uint8_t)((src0 >> 7));
		*pDst++ = (uint8_t)((src0 << 1) | (src1 >> 14));
		*pDst++ = (uint8_t)((src1 >> 6));
		*pDst++ = (uint8_t)((src1 << 2) | (src2 >> 13));
		*pDst++ = (uint8_t)((src2 >> 5));
		*pDst++ = (uint8_t)((src2 << 3) | (src3 >> 12));
		*pDst++ = (uint8_t)((src3 >> 4));
		*pDst++ = (uint8_t)((src3 << 4) | (src4 >> 11));
		*pDst++ = (uint8_t)((src4 >> 3));
		*pDst++ = (uint8_t)((src4 << 5) | (src5 >> 10));
		*pDst++ = (uint8_t)((src5 >> 2));
		*pDst++ = (uint8_t)((src5 << 6) | (src6 >> 9));
		*pDst++ = (uint8_t)((src6 >> 1));
		*pDst++ = (uint8_t)((src6 << 7) | (src7 >> 8));
		*pDst++ = (uint8_t)(src7);
	}

	length &= 7U;
	if(length)
	{
		uint32_t trailing = 0U;
		int remaining = 8U;
		for(size_t j = 0; j < length; ++j)
		{
			PUTBITS2((uint32_t)pSrc[i + j], 15);
		}
		FLUSHBITS()
	}
}

void convert_tif_32sto16u(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	auto dest = (uint16_t*)pDst;
	for(size_t i = 0; i < length; ++i)
		dest[i] = (uint16_t)pSrc[i];
}


#define INV_MASK_16 0xFFFF
#define INV_MASK_15 ((1 << 15) - 1)
#define INV_MASK_14 ((1 << 14) - 1)
#define INV_MASK_13 ((1 << 13) - 1)
#define INV_MASK_12 ((1 << 12) - 1)
#define INV_MASK_11 ((1 << 11) - 1)
#define INV_MASK_10 ((1 << 10) - 1)
#define INV_MASK_9 ((1 << 9) - 1)
#define INV_MASK_8 0xFF
#define INV_MASK_7 ((1 << 7) - 1)
#define INV_MASK_6 ((1 << 6) - 1)
#define INV_MASK_5 ((1 << 5) - 1)
#define INV_MASK_4 ((1 << 4) - 1)
#define INV_MASK_3 ((1 << 3) - 1)
#define INV_MASK_2 ((1 << 2) - 1)

#define GETBITS(dest, nb, mask, invert)                               \
	{                                                                 \
		int needed = (nb);                                            \
		uint32_t dst = 0U;                                            \
		if(available == 0)                                            \
		{                                                             \
			val = *pSrc++;                                            \
			available = 8;                                            \
		}                                                             \
		while(needed > available)                                     \
		{                                                             \
			dst |= val & ((1U << available) - 1U);                    \
			needed -= available;                                      \
			dst <<= needed;                                           \
			val = *pSrc++;                                            \
			available = 8;                                            \
		}                                                             \
		dst |= (val >> (available - needed)) & ((1U << needed) - 1U); \
		available -= needed;                                          \
		dest = INV((int32_t)dst, mask, invert);                       \
	}

void convert_tif_3uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;

		pDst[i + 0] = INV((int32_t)((val0 >> 5)), INV_MASK_3, invert);
		pDst[i + 1] = INV((int32_t)(((val0 & 0x1FU) >> 2)), INV_MASK_3, invert);
		pDst[i + 2] = INV((int32_t)(((val0 & 0x3U) << 1) | (val1 >> 7)), INV_MASK_3, invert);
		pDst[i + 3] = INV((int32_t)(((val1 & 0x7FU) >> 4)), INV_MASK_3, invert);
		pDst[i + 4] = INV((int32_t)(((val1 & 0xFU) >> 1)), INV_MASK_3, invert);
		pDst[i + 5] = INV((int32_t)(((val1 & 0x1U) << 2) | (val2 >> 6)), INV_MASK_3, invert);
		pDst[i + 6] = INV((int32_t)(((val2 & 0x3FU) >> 3)), INV_MASK_3, invert);
		pDst[i + 7] = INV((int32_t)(((val2 & 0x7U))), INV_MASK_3, invert);
	}

	length = length & 7U;
	if(length)
	{
		uint32_t val;
		int available = 0;
		for(size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 3, INV_MASK_3, invert)
	}
}
void convert_tif_5uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;

		pDst[i + 0] = INV((int32_t)((val0 >> 3)), INV_MASK_5, invert);
		pDst[i + 1] = INV((int32_t)(((val0 & 0x7U) << 2) | (val1 >> 6)), INV_MASK_5, invert);
		pDst[i + 2] = INV((int32_t)(((val1 & 0x3FU) >> 1)), INV_MASK_5, invert);
		pDst[i + 3] = INV((int32_t)(((val1 & 0x1U) << 4) | (val2 >> 4)), INV_MASK_5, invert);
		pDst[i + 4] = INV((int32_t)(((val2 & 0xFU) << 1) | (val3 >> 7)), INV_MASK_5, invert);
		pDst[i + 5] = INV((int32_t)(((val3 & 0x7FU) >> 2)), INV_MASK_5, invert);
		pDst[i + 6] = INV((int32_t)(((val3 & 0x3U) << 3) | (val4 >> 5)), INV_MASK_5, invert);
		pDst[i + 7] = INV((int32_t)(((val4 & 0x1FU))), INV_MASK_5, invert);
	}

	length = length & 7U;
	if(length)
	{
		uint32_t val;
		int available = 0;
		for(size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 5, INV_MASK_5, invert)
	}
}
void convert_tif_7uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;

		pDst[i + 0] = INV((int32_t)((val0 >> 1)), INV_MASK_7, invert);
		pDst[i + 1] = INV((int32_t)(((val0 & 0x1U) << 6) | (val1 >> 2)), INV_MASK_7, invert);
		pDst[i + 2] = INV((int32_t)(((val1 & 0x3U) << 5) | (val2 >> 3)), INV_MASK_7, invert);
		pDst[i + 3] = INV((int32_t)(((val2 & 0x7U) << 4) | (val3 >> 4)), INV_MASK_7, invert);
		pDst[i + 4] = INV((int32_t)(((val3 & 0xFU) << 3) | (val4 >> 5)), INV_MASK_7, invert);
		pDst[i + 5] = INV((int32_t)(((val4 & 0x1FU) << 2) | (val5 >> 6)), INV_MASK_7, invert);
		pDst[i + 6] = INV((int32_t)(((val5 & 0x3FU) << 1) | (val6 >> 7)), INV_MASK_7, invert);
		pDst[i + 7] = INV((int32_t)(((val6 & 0x7FU))), INV_MASK_7, invert);
	}

	length = length & 7U;
	if(length)
	{
		uint32_t val;
		int available = 0;
		for(size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 7, INV_MASK_7, invert)
	}
}
void convert_tif_9uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;
		uint32_t val7 = *pSrc++;
		uint32_t val8 = *pSrc++;

		pDst[i + 0] = INV((int32_t)((val0 << 1) | (val1 >> 7)), INV_MASK_9, invert);
		pDst[i + 1] = INV((int32_t)(((val1 & 0x7FU) << 2) | (val2 >> 6)), INV_MASK_9, invert);
		pDst[i + 2] = INV((int32_t)(((val2 & 0x3FU) << 3) | (val3 >> 5)), INV_MASK_9, invert);
		pDst[i + 3] = INV((int32_t)(((val3 & 0x1FU) << 4) | (val4 >> 4)), INV_MASK_9, invert);
		pDst[i + 4] = INV((int32_t)(((val4 & 0xFU) << 5) | (val5 >> 3)), INV_MASK_9, invert);
		pDst[i + 5] = INV((int32_t)(((val5 & 0x7U) << 6) | (val6 >> 2)), INV_MASK_9, invert);
		pDst[i + 6] = INV((int32_t)(((val6 & 0x3U) << 7) | (val7 >> 1)), INV_MASK_9, invert);
		pDst[i + 7] = INV((int32_t)(((val7 & 0x1U) << 8) | (val8)), INV_MASK_9, invert);
	}
	length = length & 7U;
	if(length)
	{
		uint32_t val;
		int available = 0;
		for(size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 9, INV_MASK_9, invert)
	}
}

void convert_tif_10sto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)3U); i += 4U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;

		pDst[i + 0] =
			sign_extend(INV((int32_t)((val0 << 2) | (val1 >> 6)), INV_MASK_10, invert), 32 - 10);
		pDst[i + 1] = sign_extend(
			INV((int32_t)(((val1 & 0x3FU) << 4) | (val2 >> 4)), INV_MASK_10, invert), 32 - 10);
		pDst[i + 2] = sign_extend(
			INV((int32_t)(((val2 & 0xFU) << 6) | (val3 >> 2)), INV_MASK_10, invert), 32 - 10);
		pDst[i + 3] =
			sign_extend(INV((int32_t)(((val3 & 0x3U) << 8) | val4), INV_MASK_10, invert), 32 - 10);
	}
	if(length & 3U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		length = length & 3U;
		pDst[i + 0] =
			sign_extend(INV((int32_t)((val0 << 2) | (val1 >> 6)), INV_MASK_10, invert), 32 - 10);

		if(length > 1U)
		{
			uint32_t val2 = *pSrc++;
			pDst[i + 1] = sign_extend(
				INV((int32_t)(((val1 & 0x3FU) << 4) | (val2 >> 4)), INV_MASK_10, invert), 32 - 10);
			if(length > 2U)
			{
				uint32_t val3 = *pSrc++;
				pDst[i + 2] = sign_extend(
					INV((int32_t)(((val2 & 0xFU) << 6) | (val3 >> 2)), INV_MASK_10, invert),
					32 - 10);
			}
		}
	}
}

void convert_tif_10uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)3U); i += 4U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;

		pDst[i + 0] = INV((int32_t)((val0 << 2) | (val1 >> 6)), INV_MASK_10, invert);
		pDst[i + 1] = INV((int32_t)(((val1 & 0x3FU) << 4) | (val2 >> 4)), INV_MASK_10, invert);
		pDst[i + 2] = INV((int32_t)(((val2 & 0xFU) << 6) | (val3 >> 2)), INV_MASK_10, invert);
		pDst[i + 3] = INV((int32_t)(((val3 & 0x3U) << 8) | val4), INV_MASK_10, invert);
	}
	if(length & 3U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		length = length & 3U;
		pDst[i + 0] = INV((int32_t)((val0 << 2) | (val1 >> 6)), INV_MASK_10, invert);

		if(length > 1U)
		{
			uint32_t val2 = *pSrc++;
			pDst[i + 1] = INV((int32_t)(((val1 & 0x3FU) << 4) | (val2 >> 4)), INV_MASK_10, invert);
			if(length > 2U)
			{
				uint32_t val3 = *pSrc++;
				pDst[i + 2] =
					INV((int32_t)(((val2 & 0xFU) << 6) | (val3 >> 2)), INV_MASK_10, invert);
			}
		}
	}
}

void convert_tif_11uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;
		uint32_t val7 = *pSrc++;
		uint32_t val8 = *pSrc++;
		uint32_t val9 = *pSrc++;
		uint32_t val10 = *pSrc++;

		pDst[i + 0] = INV((int32_t)((val0 << 3) | (val1 >> 5)), INV_MASK_11, invert);
		pDst[i + 1] = INV((int32_t)(((val1 & 0x1FU) << 6) | (val2 >> 2)), INV_MASK_11, invert);
		pDst[i + 2] =
			INV((int32_t)(((val2 & 0x3U) << 9) | (val3 << 1) | (val4 >> 7)), INV_MASK_11, invert);
		pDst[i + 3] = INV((int32_t)(((val4 & 0x7FU) << 4) | (val5 >> 4)), INV_MASK_11, invert);
		pDst[i + 4] = INV((int32_t)(((val5 & 0xFU) << 7) | (val6 >> 1)), INV_MASK_11, invert);
		pDst[i + 5] =
			INV((int32_t)(((val6 & 0x1U) << 10) | (val7 << 2) | (val8 >> 6)), INV_MASK_11, invert);
		pDst[i + 6] = INV((int32_t)(((val8 & 0x3FU) << 5) | (val9 >> 3)), INV_MASK_11, invert);
		pDst[i + 7] = INV((int32_t)(((val9 & 0x7U) << 8) | (val10)), INV_MASK_11, invert);
	}
	length = length & 7U;
	if(length)
	{
		uint32_t val;
		int available = 0;
		for(size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 11, INV_MASK_11, invert)
	}
}
void convert_tif_12sto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)1U); i += 2U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;

		pDst[i + 0] =
			sign_extend(INV((int32_t)((val0 << 4) | (val1 >> 4)), INV_MASK_12, invert), 32 - 12);
		pDst[i + 1] =
			sign_extend(INV((int32_t)(((val1 & 0xFU) << 8) | val2), INV_MASK_12, invert), 32 - 12);
	}
	if(length & 1U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		pDst[i + 0] =
			sign_extend(INV((int32_t)((val0 << 4) | (val1 >> 4)), INV_MASK_12, invert), 32 - 12);
	}
}
void convert_tif_12uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)1U); i += 2U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;

		pDst[i + 0] = INV((int32_t)((val0 << 4) | (val1 >> 4)), INV_MASK_12, invert);
		pDst[i + 1] = INV((int32_t)(((val1 & 0xFU) << 8) | val2), INV_MASK_12, invert);
	}
	if(length & 1U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		pDst[i + 0] = INV((int32_t)((val0 << 4) | (val1 >> 4)), INV_MASK_12, invert);
	}
}

void convert_tif_13uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;
		uint32_t val7 = *pSrc++;
		uint32_t val8 = *pSrc++;
		uint32_t val9 = *pSrc++;
		uint32_t val10 = *pSrc++;
		uint32_t val11 = *pSrc++;
		uint32_t val12 = *pSrc++;

		pDst[i + 0] = INV((int32_t)((val0 << 5) | (val1 >> 3)), INV_MASK_13, invert);
		pDst[i + 1] =
			INV((int32_t)(((val1 & 0x7U) << 10) | (val2 << 2) | (val3 >> 6)), INV_MASK_13, invert);
		pDst[i + 2] = INV((int32_t)(((val3 & 0x3FU) << 7) | (val4 >> 1)), INV_MASK_13, invert);
		pDst[i + 3] =
			INV((int32_t)(((val4 & 0x1U) << 12) | (val5 << 4) | (val6 >> 4)), INV_MASK_13, invert);
		pDst[i + 4] =
			INV((int32_t)(((val6 & 0xFU) << 9) | (val7 << 1) | (val8 >> 7)), INV_MASK_13, invert);
		pDst[i + 5] = INV((int32_t)(((val8 & 0x7FU) << 6) | (val9 >> 2)), INV_MASK_13, invert);
		pDst[i + 6] = INV((int32_t)(((val9 & 0x3U) << 11) | (val10 << 3) | (val11 >> 5)),
						  INV_MASK_13, invert);
		pDst[i + 7] = INV((int32_t)(((val11 & 0x1FU) << 8) | (val12)), INV_MASK_13, invert);
	}
	length = length & 7U;
	if(length)
	{
		uint32_t val;
		int available = 0;
		for(size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 13, INV_MASK_13, invert)
	}
}

void convert_tif_14uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)3U); i += 4U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;

		pDst[i + 0] = INV((int32_t)((val0 << 6) | (val1 >> 2)), INV_MASK_14, invert);
		pDst[i + 1] =
			INV((int32_t)(((val1 & 0x3U) << 12) | (val2 << 4) | (val3 >> 4)), INV_MASK_14, invert);
		pDst[i + 2] =
			INV((int32_t)(((val3 & 0xFU) << 10) | (val4 << 2) | (val5 >> 6)), INV_MASK_14, invert);
		pDst[i + 3] = INV((int32_t)(((val5 & 0x3FU) << 8) | val6), INV_MASK_14, invert);
	}
	if(length & 3U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		length = length & 3U;
		pDst[i + 0] = (int32_t)((val0 << 6) | (val1 >> 2));

		if(length > 1U)
		{
			uint32_t val2 = *pSrc++;
			uint32_t val3 = *pSrc++;
			pDst[i + 1] = INV((int32_t)(((val1 & 0x3U) << 12) | (val2 << 4) | (val3 >> 4)),
							  INV_MASK_14, invert);
			if(length > 2U)
			{
				uint32_t val4 = *pSrc++;
				uint32_t val5 = *pSrc++;
				pDst[i + 2] = INV((int32_t)(((val3 & 0xFU) << 10) | (val4 << 2) | (val5 >> 6)),
								  INV_MASK_14, invert);
			}
		}
	}
}

void convert_tif_15uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for(i = 0; i < (length & ~(size_t)7U); i += 8U)
	{
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;
		uint32_t val7 = *pSrc++;
		uint32_t val8 = *pSrc++;
		uint32_t val9 = *pSrc++;
		uint32_t val10 = *pSrc++;
		uint32_t val11 = *pSrc++;
		uint32_t val12 = *pSrc++;
		uint32_t val13 = *pSrc++;
		uint32_t val14 = *pSrc++;

		pDst[i + 0] = INV((int32_t)((val0 << 7) | (val1 >> 1)), (1 << 15) - 1, invert);
		pDst[i + 1] =
			INV((int32_t)(((val1 & 0x1U) << 14) | (val2 << 6) | (val3 >> 2)), INV_MASK_15, invert);
		pDst[i + 2] =
			INV((int32_t)(((val3 & 0x3U) << 13) | (val4 << 5) | (val5 >> 3)), INV_MASK_15, invert);
		pDst[i + 3] =
			INV((int32_t)(((val5 & 0x7U) << 12) | (val6 << 4) | (val7 >> 4)), INV_MASK_15, invert);
		pDst[i + 4] =
			INV((int32_t)(((val7 & 0xFU) << 11) | (val8 << 3) | (val9 >> 5)), INV_MASK_15, invert);
		pDst[i + 5] = INV((int32_t)(((val9 & 0x1FU) << 10) | (val10 << 2) | (val11 >> 6)),
						  INV_MASK_15, invert);
		pDst[i + 6] = INV((int32_t)(((val11 & 0x3FU) << 9) | (val12 << 1) | (val13 >> 7)),
						  INV_MASK_15, invert);
		pDst[i + 7] = INV((int32_t)(((val13 & 0x7FU) << 8) | (val14)), INV_MASK_15, invert);
	}
	length = length & 7U;
	if(length)
	{
		uint32_t val;
		int available = 0;
		for(size_t j = 0; j < length; ++j)
			GETBITS(pDst[i + j], 15, INV_MASK_15, invert)
	}
}

/* seems that libtiff decodes this to machine endianness */
void convert_tif_16uto32s(const uint16_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	for(size_t i = 0; i < length; i++)
		pDst[i] = INV(pSrc[i], 0xFFFF, invert);
}
