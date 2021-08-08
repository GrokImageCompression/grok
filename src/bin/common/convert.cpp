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


/* Component clipping */
template<typename T>
void clip(grk_image_comp* component, uint8_t precision)
{
	uint32_t stride_diff = component->stride - component->w;
	assert(precision <= 16);
	auto data = component->data;
	T max = std::numeric_limits<T>::max();
	T min = std::numeric_limits<T>::min();
	size_t index = 0;
	for(uint32_t j = 0; j < component->h; ++j)
	{
		for(uint32_t i = 0; i < component->w; ++i)
		{
			data[index] = (int32_t)std::clamp<T>((T)data[index], min, max);
			index++;
		}
		index += stride_diff;
	}
	component->prec = precision;
}

void clip_component(grk_image_comp* component, uint8_t precision)
{
	if(component->sgnd)
		clip<int32_t>(component, precision);
	else
		clip<uint32_t>(component, precision);
}

void scale_component(grk_image_comp* component, uint8_t precision)
{
	if(component->prec == precision)
		return;
	uint32_t stride_diff = component->stride - component->w;
	auto data = component->data;
	if(component->prec < precision)
	{
		int32_t scale = 1U << (uint32_t)(precision - component->prec);
		size_t index = 0;
		for(uint32_t j = 0; j < component->h; ++j)
		{
			for(uint32_t i = 0; i < component->w; ++i)
			{
				data[index] = data[index] * scale;
				index++;
			}
			index += stride_diff;
		}
	}
	else
	{
		int32_t scale = 1U << (uint32_t)(component->prec - precision);
		size_t index = 0;
		for(uint32_t j = 0; j < component->h; ++j)
		{
			for(uint32_t i = 0; i < component->w; ++i)
			{
				data[index] = data[index] / scale;
				index++;
			}
			index += stride_diff;
		}
	}
	component->prec = precision;
}

grk_image* convert_gray_to_rgb(grk_image* original)
{
	if(original->numcomps == 0)
		return nullptr;
	auto new_components = new grk_image_cmptparm[original->numcomps + 2U];
	new_components[0].dx = new_components[1].dx = new_components[2].dx = original->comps[0].dx;
	new_components[0].dy = new_components[1].dy = new_components[2].dy = original->comps[0].dy;
	new_components[0].h = new_components[1].h = new_components[2].h = original->comps[0].h;
	new_components[0].w = new_components[1].w = new_components[2].w = original->comps[0].w;
	new_components[0].prec = new_components[1].prec = new_components[2].prec =
		original->comps[0].prec;
	new_components[0].sgnd = new_components[1].sgnd = new_components[2].sgnd =
		original->comps[0].sgnd;
	new_components[0].x0 = new_components[1].x0 = new_components[2].x0 = original->comps[0].x0;
	new_components[0].y0 = new_components[1].y0 = new_components[2].y0 = original->comps[0].y0;

	for(uint32_t compno = 1U; compno < original->numcomps; ++compno)
	{
		new_components[compno + 2U].dx = original->comps[compno].dx;
		new_components[compno + 2U].dy = original->comps[compno].dy;
		new_components[compno + 2U].h = original->comps[compno].h;
		new_components[compno + 2U].w = original->comps[compno].w;
		new_components[compno + 2U].prec = original->comps[compno].prec;
		new_components[compno + 2U].sgnd = original->comps[compno].sgnd;
		new_components[compno + 2U].x0 = original->comps[compno].x0;
		new_components[compno + 2U].y0 = original->comps[compno].y0;
	}

	auto new_image =
		grk_image_new((uint16_t)(original->numcomps + 2U), new_components, GRK_CLRSPC_SRGB, true);
	delete[] new_components;
	if(new_image == nullptr)
	{
		spdlog::error("grk_decompress: failed to allocate memory for RGB image.");
		return nullptr;
	}

	new_image->x0 = original->x0;
	new_image->x1 = original->x1;
	new_image->y0 = original->y0;
	new_image->y1 = original->y1;

	new_image->comps[0].type = new_image->comps[1].type = new_image->comps[2].type =
		original->comps[0].type;
	memcpy(new_image->comps[0].data, original->comps[0].data,
		   original->comps[0].stride * original->comps[0].h * sizeof(int32_t));
	memcpy(new_image->comps[1].data, original->comps[0].data,
		   original->comps[0].stride * original->comps[0].h * sizeof(int32_t));
	memcpy(new_image->comps[2].data, original->comps[0].data,
		   original->comps[0].stride * original->comps[0].h * sizeof(int32_t));

	for(uint32_t compno = 1U; compno < original->numcomps; ++compno)
	{
		new_image->comps[compno + 2U].type = original->comps[compno].type;
		memcpy(new_image->comps[compno + 2U].data, original->comps[compno].data,
			   original->comps[compno].stride * original->comps[compno].h * sizeof(int32_t));
	}

	return new_image;
}

grk_image* upsample_image_components(grk_image* original)
{
	grk_image* new_image = nullptr;
	grk_image_cmptparm* new_components = nullptr;
	bool upsample_need = false;
	uint32_t compno;

	if(!original || !original->comps)
		return nullptr;

	for(compno = 0U; compno < original->numcomps; ++compno)
	{
		if(!(original->comps + compno))
			return nullptr;
		if((original->comps[compno].dx > 1U) || (original->comps[compno].dy > 1U))
		{
			upsample_need = true;
			break;
		}
	}
	if(!upsample_need)
	{
		return original;
	}
	/* Upsample is needed */
	new_components = new grk_image_cmptparm[original->numcomps];
	for(compno = 0U; compno < original->numcomps; ++compno)
	{
		auto new_cmp = &(new_components[compno]);
		auto org_cmp = &(original->comps[compno]);

		new_cmp->prec = org_cmp->prec;
		new_cmp->sgnd = org_cmp->sgnd;
		new_cmp->x0 = original->x0;
		new_cmp->y0 = original->y0;
		new_cmp->dx = 1;
		new_cmp->dy = 1;
		new_cmp->w = org_cmp->w; /* should be original->x1 - original->x0 for dx==1 */
		new_cmp->h = org_cmp->h; /* should be original->y1 - original->y0 for dy==0 */

		if(org_cmp->dx > 1U)
			new_cmp->w = original->x1 - original->x0;
		if(org_cmp->dy > 1U)
			new_cmp->h = original->y1 - original->y0;
	}

	new_image = grk_image_new(original->numcomps, new_components, original->color_space, true);
	delete[] new_components;
	if(new_image == nullptr)
	{
		spdlog::error("grk_decompress: failed to allocate memory for upsampled components.");
		return nullptr;
	}

	new_image->x0 = original->x0;
	new_image->x1 = original->x1;
	new_image->y0 = original->y0;
	new_image->y1 = original->y1;

	for(compno = 0U; compno < original->numcomps; ++compno)
	{
		grk_image_comp* new_cmp = &(new_image->comps[compno]);
		grk_image_comp* org_cmp = &(original->comps[compno]);

		new_cmp->type = org_cmp->type;

		if((org_cmp->dx > 1U) || (org_cmp->dy > 1U))
		{
			auto src = org_cmp->data;
			auto dst = new_cmp->data;

			/* need to take into account dx & dy */
			uint32_t xoff = org_cmp->dx * org_cmp->x0 - original->x0;
			uint32_t yoff = org_cmp->dy * org_cmp->y0 - original->y0;
			if((xoff >= org_cmp->dx) || (yoff >= org_cmp->dy))
			{
				spdlog::error("upsample: Invalid image/component parameters found when upsampling");
				grk_object_unref(&new_image->obj);
				return nullptr;
			}

			uint32_t y;
			for(y = 0U; y < yoff; ++y)
			{
				memset(dst, 0U, new_cmp->w * sizeof(int32_t));
				dst += new_cmp->stride;
			}

			if(new_cmp->h > (org_cmp->dy - 1U))
			{ /* check subtraction overflow for really small images */
				for(; y < new_cmp->h - (org_cmp->dy - 1U); y += org_cmp->dy)
				{
					uint32_t x, dy;
					uint32_t xorg = 0;
					for(x = 0U; x < xoff; ++x)
						dst[x] = 0;

					if(new_cmp->w > (org_cmp->dx - 1U))
					{ /* check subtraction overflow for really small images */
						for(; x < new_cmp->w - (org_cmp->dx - 1U); x += org_cmp->dx, ++xorg)
						{
							for(uint32_t dx = 0U; dx < org_cmp->dx; ++dx)
								dst[x + dx] = src[xorg];
						}
					}
					for(; x < new_cmp->w; ++x)
						dst[x] = src[xorg];
					dst += new_cmp->stride;

					for(dy = 1U; dy < org_cmp->dy; ++dy)
					{
						memcpy(dst, dst - new_cmp->stride, new_cmp->w * sizeof(int32_t));
						dst += new_cmp->stride;
					}
					src += org_cmp->stride;
				}
			}
			if(y < new_cmp->h)
			{
				uint32_t x;
				uint32_t xorg = 0;
				for(x = 0U; x < xoff; ++x)
					dst[x] = 0;

				if(new_cmp->w > (org_cmp->dx - 1U))
				{ /* check subtraction overflow for really small images */
					for(; x < new_cmp->w - (org_cmp->dx - 1U); x += org_cmp->dx, ++xorg)
					{
						for(uint32_t dx = 0U; dx < org_cmp->dx; ++dx)
							dst[x + dx] = src[xorg];
					}
				}
				for(; x < new_cmp->w; ++x)
					dst[x] = src[xorg];
				dst += new_cmp->stride;
				++y;
				for(; y < new_cmp->h; ++y)
				{
					memcpy(dst, dst - new_cmp->stride, new_cmp->w * sizeof(int32_t));
					dst += new_cmp->stride;
				}
			}
		}
		else
		{
			memcpy(new_cmp->data, org_cmp->data, org_cmp->stride * org_cmp->h * sizeof(int32_t));
		}
	}

	return new_image;
}

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

int32_t sign_extend(int32_t val, uint8_t shift){
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
		pDst[i + 0] = INV(sign_extend(val>>4, 28), 0xF, invert);
		pDst[i + 1] = INV(sign_extend(val & 0xF, 28), 0xF, invert);
	}
	if(length & 1U)
	{
		uint8_t val = *pSrc++;
		pDst[i + 0] = INV(sign_extend(val >> 4, 28), 15, invert);
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

const cvtTo32 cvtsTo32_LUT[9] = {nullptr,		   convert_1u32s_C1R, convert_2u32s_C1R,
								nullptr,		   convert_4s32s_C1R, nullptr,
								convert_6u32s_C1R, nullptr,			  convert_8u32s_C1R};

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
