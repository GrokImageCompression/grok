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

/* Component clipping */
void clip_component(grk_image_comp *component, uint32_t precision) {
	uint32_t stride_diff = component->stride - component->w;
	assert(precision <= 16);

	if (component->sgnd) {
		auto data = component->data;
		int32_t max = std::numeric_limits<int32_t>::max();
		int32_t min = std::numeric_limits<int32_t>::min();
		size_t index = 0;
		for (uint32_t j = 0; j < component->h; ++j){
			for (uint32_t i = 0; i < component->w; ++i){
				data[index] = std::clamp<int32_t>(data[index], min, max);
				index++;
			}
			index+= stride_diff;
		}
	} else {
		auto data = (uint32_t*) component->data;
		size_t index = 0;
		uint32_t max = std::numeric_limits<uint32_t>::max();
		for (uint32_t j = 0; j < component->h; ++j){
			for (uint32_t i = 0; i < component->w; ++i){
				data[index] = std::min<uint32_t>(data[index], max);
				index++;
			}
			index += stride_diff;
		}
	}
	component->prec = precision;
}

/* Component precision scaling */
static void scale_component_up(grk_image_comp *component, uint32_t precision) {
	uint32_t stride_diff = component->stride - component->w;
	if (component->sgnd) {
		int64_t newMax = (int64_t) 1U << (precision - 1);
		int64_t oldMax = (int64_t) 1U << (component->prec - 1);
		auto data = component->data;
		size_t index = 0;
		for (uint32_t j = 0; j < component->h; ++j){
			for (uint32_t i = 0; i < component->w; ++i){
				data[index] = (int32_t) (((int64_t) data[index] * newMax) / oldMax);
				index++;
			}
			index += stride_diff;
		}
	} else {
		uint64_t newMax = ((uint64_t) 1U << precision) - 1U;
		uint64_t oldMax = ((uint64_t) 1U << component->prec) - 1U;
		auto data = (uint32_t*) component->data;
		size_t index = 0;
		for (uint32_t j = 0; j < component->h; ++j){
			for (uint32_t i = 0; i < component->w; ++i){
				data[index] = (uint32_t) (((uint64_t) data[index] * newMax) / oldMax);
				index++;
			}
			index += stride_diff;
		}
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
	uint32_t stride_diff = component->stride - component->w;
	if (component->sgnd) {
		auto data = component->data;
		size_t index = 0;
		for (uint32_t j = 0; j < component->h; ++j){
			for (uint32_t i = 0; i < component->w; ++i){
				data[index] >>= shift;
				index++;
			}
			index += stride_diff;
		}
	} else {
		auto data = (uint32_t*) component->data;
		size_t index = 0;
		for (uint32_t j = 0; j < component->h; ++j){
			for (uint32_t i = 0; i < component->w; ++i){
				data[index] >>= shift;
				index++;
			}
			index += stride_diff;
		}
	}
	component->prec = precision;
}


grk_image* convert_gray_to_rgb(grk_image *original) {
	if (original->numcomps == 0)
		return nullptr;
	uint32_t compno;
	grk_image *new_image = nullptr;
	grk_image_cmptparm *new_components = nullptr;

	new_components = (grk_image_cmptparm*) malloc(
			(original->numcomps + 2U) * sizeof(grk_image_cmptparm));
	if (new_components == nullptr) {
		spdlog::error(
				"grk_decompress: failed to allocate memory for RGB image.");
		grk_image_destroy(original);
		return nullptr;
	}

	new_components[0].dx = new_components[1].dx = new_components[2].dx =
			original->comps[0].dx;
	new_components[0].dy = new_components[1].dy = new_components[2].dy =
			original->comps[0].dy;
	new_components[0].h = new_components[1].h = new_components[2].h =
			original->comps[0].h;
	new_components[0].w = new_components[1].w = new_components[2].w =
			original->comps[0].w;
	new_components[0].prec = new_components[1].prec = new_components[2].prec =
			original->comps[0].prec;
	new_components[0].sgnd = new_components[1].sgnd = new_components[2].sgnd =
			original->comps[0].sgnd;
	new_components[0].x0 = new_components[1].x0 = new_components[2].x0 =
			original->comps[0].x0;
	new_components[0].y0 = new_components[1].y0 = new_components[2].y0 =
			original->comps[0].y0;

	for (compno = 1U; compno < original->numcomps; ++compno) {
		new_components[compno + 2U].dx = original->comps[compno].dx;
		new_components[compno + 2U].dy = original->comps[compno].dy;
		new_components[compno + 2U].h = original->comps[compno].h;
		new_components[compno + 2U].w = original->comps[compno].w;
		new_components[compno + 2U].prec = original->comps[compno].prec;
		new_components[compno + 2U].sgnd = original->comps[compno].sgnd;
		new_components[compno + 2U].x0 = original->comps[compno].x0;
		new_components[compno + 2U].y0 = original->comps[compno].y0;
	}

	new_image = grk_image_create(original->numcomps + 2U, new_components,
			GRK_CLRSPC_SRGB,true);
	free(new_components);
	if (new_image == nullptr) {
		spdlog::error(
				"grk_decompress: failed to allocate memory for RGB image.");
		grk_image_destroy(original);
		return nullptr;
	}

	new_image->x0 = original->x0;
	new_image->x1 = original->x1;
	new_image->y0 = original->y0;
	new_image->y1 = original->y1;

	new_image->comps[0].type = new_image->comps[1].type =
			new_image->comps[2].type = original->comps[0].type;
	memcpy(new_image->comps[0].data, original->comps[0].data,
			original->comps[0].stride * original->comps[0].h * sizeof(int32_t));
	memcpy(new_image->comps[1].data, original->comps[0].data,
			original->comps[0].stride * original->comps[0].h * sizeof(int32_t));
	memcpy(new_image->comps[2].data, original->comps[0].data,
			original->comps[0].stride * original->comps[0].h * sizeof(int32_t));

	for (compno = 1U; compno < original->numcomps; ++compno) {
		new_image->comps[compno + 2U].type = original->comps[compno].type;
		memcpy(new_image->comps[compno + 2U].data, original->comps[compno].data,
				original->comps[compno].stride * original->comps[compno].h
						* sizeof(int32_t));
	}
	grk_image_destroy(original);
	return new_image;
}

grk_image* upsample_image_components(grk_image *original) {
	grk_image *new_image = nullptr;
	grk_image_cmptparm *new_components = nullptr;
	bool upsample_need = false;
	uint32_t compno;

	if (!original || !original->comps)
		return nullptr;

	for (compno = 0U; compno < original->numcomps; ++compno) {
		if (!(original->comps + compno))
			return nullptr;
		if ((original->comps[compno].dx > 1U)
				|| (original->comps[compno].dy > 1U)) {
			upsample_need = true;
			break;
		}
	}
	if (!upsample_need) {
		return original;
	}
	/* Upsample is needed */
	new_components = (grk_image_cmptparm*) malloc(
			original->numcomps * sizeof(grk_image_cmptparm));
	if (new_components == nullptr) {
		spdlog::error(
				"grk_decompress: failed to allocate memory for upsampled components.");
		grk_image_destroy(original);
		return nullptr;
	}

	for (compno = 0U; compno < original->numcomps; ++compno) {
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

		if (org_cmp->dx > 1U)
			new_cmp->w = original->x1 - original->x0;
		if (org_cmp->dy > 1U)
			new_cmp->h = original->y1 - original->y0;
	}

	new_image = grk_image_create(original->numcomps, new_components,
			original->color_space,true);
	free(new_components);
	if (new_image == nullptr) {
		spdlog::error(
				"grk_decompress: failed to allocate memory for upsampled components.");
		grk_image_destroy(original);
		return nullptr;
	}

	new_image->x0 = original->x0;
	new_image->x1 = original->x1;
	new_image->y0 = original->y0;
	new_image->y1 = original->y1;

	for (compno = 0U; compno < original->numcomps; ++compno) {
		grk_image_comp *new_cmp = &(new_image->comps[compno]);
		grk_image_comp *org_cmp = &(original->comps[compno]);

		new_cmp->type = org_cmp->type;

		if ((org_cmp->dx > 1U) || (org_cmp->dy > 1U)) {
			auto src = org_cmp->data;
			auto dst = new_cmp->data;

			/* need to take into account dx & dy */
			uint32_t xoff = org_cmp->dx * org_cmp->x0 - original->x0;
			uint32_t yoff = org_cmp->dy * org_cmp->y0 - original->y0;
			if ((xoff >= org_cmp->dx) || (yoff >= org_cmp->dy)) {
				spdlog::error(
						"grk_decompress: Invalid image/component parameters found when upsampling");
				grk_image_destroy(original);
				grk_image_destroy(new_image);
				return nullptr;
			}

			uint32_t y;
			for (y = 0U; y < yoff; ++y) {
				memset(dst, 0U, new_cmp->w * sizeof(int32_t));
				dst += new_cmp->stride;
			}

			if (new_cmp->h > (org_cmp->dy - 1U)) { /* check subtraction overflow for really small images */
				for (; y < new_cmp->h - (org_cmp->dy - 1U); y += org_cmp->dy) {
					uint32_t x, dy;
					uint32_t xorg = 0;
					for (x = 0U; x < xoff; ++x)
						dst[x] = 0;

					if (new_cmp->w > (org_cmp->dx - 1U)) { /* check subtraction overflow for really small images */
						for (; x < new_cmp->w - (org_cmp->dx - 1U);	x += org_cmp->dx, ++xorg) {
							for (uint32_t dx = 0U; dx < org_cmp->dx; ++dx)
								dst[x + dx] = src[xorg];
						}
					}
					for (; x < new_cmp->w; ++x)
						dst[x] = src[xorg];
					dst += new_cmp->stride;

					for (dy = 1U; dy < org_cmp->dy; ++dy) {
						memcpy(dst, dst - new_cmp->stride, new_cmp->w * sizeof(int32_t));
						dst += new_cmp->stride;
					}
					src += org_cmp->stride;
				}
			}
			if (y < new_cmp->h) {
				uint32_t x;
				uint32_t xorg;

				xorg = 0U;
				for (x = 0U; x < xoff; ++x)
					dst[x] = 0;

				if (new_cmp->w > (org_cmp->dx - 1U)) { /* check subtraction overflow for really small images */
					for (; x < new_cmp->w - (org_cmp->dx - 1U); x += org_cmp->dx, ++xorg) {
						for (uint32_t dx = 0U; dx < org_cmp->dx; ++dx)
							dst[x + dx] = src[xorg];
					}
				}
				for (; x < new_cmp->w; ++x)
					dst[x] = src[xorg];
				dst += new_cmp->stride;
				++y;
				for (; y < new_cmp->h; ++y) {
					memcpy(dst, dst - new_cmp->stride, new_cmp->w * sizeof(int32_t));
					dst += new_cmp->stride;
				}
			}
		} else {
			memcpy(new_cmp->data, org_cmp->data, org_cmp->stride * org_cmp->h * sizeof(int32_t));
		}
	}
	grk_image_destroy(original);
	return new_image;
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
 * convert 1 bpp to 8 bit
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
 * convert 2 bpp to 8 bit
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
 * convert 4 bpp to 8 bit
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
 * convert 6 bpp to 8 bit
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
 * convert 8 bpp to 8 bit
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


// TIFF ////////

#define PUTBITS2(s, nb) \
	trailing <<= remaining; \
	trailing |= (uint32_t)((s) >> (nb - remaining)); \
	*pDst++ = (uint8_t)trailing; \
	trailing = (uint32_t)((s) & ((1U << (nb - remaining)) - 1U)); \
	if (nb >= (remaining + 8)) { \
		*pDst++ = (uint8_t)(trailing >> (nb - (remaining + 8))); \
		trailing &= (uint32_t)((1U << (nb - (remaining + 8))) - 1U); \
		remaining += 16 - nb; \
	} else { \
		remaining += 8 - nb; \
	}

#define PUTBITS(s, nb) \
  if (nb >= remaining) { \
		PUTBITS2(s, nb) \
	} else { \
		trailing <<= nb; \
		trailing |= (uint32_t)(s); \
		remaining -= nb; \
	}
#define FLUSHBITS() \
	if (remaining != 8) { \
		trailing <<= remaining; \
		*pDst++ = (uint8_t)trailing; \
	}

#define GETBITS(dest, nb, mask, invert) { \
	int needed = (nb); \
	uint32_t dst = 0U; \
	if (available == 0) { \
		val = *pSrc++; \
		available = 8; \
	} \
	while (needed > available) { \
		dst |= val & ((1U << available) - 1U); \
		needed -= available; \
		dst <<= needed; \
		val = *pSrc++; \
		available = 8; \
	} \
	dst |= (val >> (available - needed)) & ((1U << needed) - 1U); \
	available -= needed; \
	dest = INV((int32_t)dst, mask,invert); \
}

void convert_tif_32sto3u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
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

		*pDst++ = (uint8_t) ((src0 << 5) | (src1 << 2) | (src2 >> 1));
		*pDst++ = (uint8_t) ((src2 << 7) | (src3 << 4) | (src4 << 1)
				| (src5 >> 2));
		*pDst++ = (uint8_t) ((src5 << 6) | (src6 << 3) | (src7));
	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS((uint32_t )pSrc[i + 0], 3)
		if (length > 1U) {
			PUTBITS((uint32_t )pSrc[i + 1], 3)
			if (length > 2U) {
				PUTBITS((uint32_t )pSrc[i + 2], 3)
				if (length > 3U) {
					PUTBITS((uint32_t )pSrc[i + 3], 3)
					if (length > 4U) {
						PUTBITS((uint32_t )pSrc[i + 4], 3)
						if (length > 5U) {
							PUTBITS((uint32_t )pSrc[i + 5], 3)
							if (length > 6U) {
								PUTBITS((uint32_t )pSrc[i + 6], 3)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}

void convert_tif_32sto5u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
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

		*pDst++ = (uint8_t) ((src0 << 3) | (src1 >> 2));
		*pDst++ = (uint8_t) ((src1 << 6) | (src2 << 1) | (src3 >> 4));
		*pDst++ = (uint8_t) ((src3 << 4) | (src4 >> 1));
		*pDst++ = (uint8_t) ((src4 << 7) | (src5 << 2) | (src6 >> 3));
		*pDst++ = (uint8_t) ((src6 << 5) | (src7));

	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS((uint32_t )pSrc[i + 0], 5)
		if (length > 1U) {
			PUTBITS((uint32_t )pSrc[i + 1], 5)
			if (length > 2U) {
				PUTBITS((uint32_t )pSrc[i + 2], 5)
				if (length > 3U) {
					PUTBITS((uint32_t )pSrc[i + 3], 5)
					if (length > 4U) {
						PUTBITS((uint32_t )pSrc[i + 4], 5)
						if (length > 5U) {
							PUTBITS((uint32_t )pSrc[i + 5], 5)
							if (length > 6U) {
								PUTBITS((uint32_t )pSrc[i + 6], 5)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}

void convert_tif_32sto7u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
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

		*pDst++ = (uint8_t) ((src0 << 1) | (src1 >> 6));
		*pDst++ = (uint8_t) ((src1 << 2) | (src2 >> 5));
		*pDst++ = (uint8_t) ((src2 << 3) | (src3 >> 4));
		*pDst++ = (uint8_t) ((src3 << 4) | (src4 >> 3));
		*pDst++ = (uint8_t) ((src4 << 5) | (src5 >> 2));
		*pDst++ = (uint8_t) ((src5 << 6) | (src6 >> 1));
		*pDst++ = (uint8_t) ((src6 << 7) | (src7));
	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS((uint32_t )pSrc[i + 0], 7)
		if (length > 1U) {
			PUTBITS((uint32_t )pSrc[i + 1], 7)
			if (length > 2U) {
				PUTBITS((uint32_t )pSrc[i + 2], 7)
				if (length > 3U) {
					PUTBITS((uint32_t )pSrc[i + 3], 7)
					if (length > 4U) {
						PUTBITS((uint32_t )pSrc[i + 4], 7)
						if (length > 5U) {
							PUTBITS((uint32_t )pSrc[i + 5], 7)
							if (length > 6U) {
								PUTBITS((uint32_t )pSrc[i + 6], 7)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}

void convert_tif_32sto9u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
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

		*pDst++ = (uint8_t) ((src0 >> 1));
		*pDst++ = (uint8_t) ((src0 << 7) | (src1 >> 2));
		*pDst++ = (uint8_t) ((src1 << 6) | (src2 >> 3));
		*pDst++ = (uint8_t) ((src2 << 5) | (src3 >> 4));
		*pDst++ = (uint8_t) ((src3 << 4) | (src4 >> 5));
		*pDst++ = (uint8_t) ((src4 << 3) | (src5 >> 6));
		*pDst++ = (uint8_t) ((src5 << 2) | (src6 >> 7));
		*pDst++ = (uint8_t) ((src6 << 1) | (src7 >> 8));
		*pDst++ = (uint8_t) (src7);
	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS2((uint32_t )pSrc[i + 0], 9)
		if (length > 1U) {
			PUTBITS2((uint32_t )pSrc[i + 1], 9)
			if (length > 2U) {
				PUTBITS2((uint32_t )pSrc[i + 2], 9)
				if (length > 3U) {
					PUTBITS2((uint32_t )pSrc[i + 3], 9)
					if (length > 4U) {
						PUTBITS2((uint32_t )pSrc[i + 4], 9)
						if (length > 5U) {
							PUTBITS2((uint32_t )pSrc[i + 5], 9)
							if (length > 6U) {
								PUTBITS2((uint32_t )pSrc[i + 6], 9)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}

void convert_tif_32sto10u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];

		*pDst++ = (uint8_t) (src0 >> 2);
		*pDst++ = (uint8_t) (((src0 & 0x3U) << 6) | (src1 >> 4));
		*pDst++ = (uint8_t) (((src1 & 0xFU) << 4) | (src2 >> 6));
		*pDst++ = (uint8_t) (((src2 & 0x3FU) << 2) | (src3 >> 8));
		*pDst++ = (uint8_t) (src3);
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
		*pDst++ = (uint8_t) (src0 >> 2);
		*pDst++ = (uint8_t) (((src0 & 0x3U) << 6) | (src1 >> 4));
		if (length > 1U) {
			*pDst++ = (uint8_t) (((src1 & 0xFU) << 4) | (src2 >> 6));
			if (length > 2U) {
				*pDst++ = (uint8_t) (((src2 & 0x3FU) << 2));
			}
		}
	}
}

void convert_tif_32sto11u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
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

		*pDst++ = (uint8_t) ((src0 >> 3));
		*pDst++ = (uint8_t) ((src0 << 5) | (src1 >> 6));
		*pDst++ = (uint8_t) ((src1 << 2) | (src2 >> 9));
		*pDst++ = (uint8_t) ((src2 >> 1));
		*pDst++ = (uint8_t) ((src2 << 7) | (src3 >> 4));
		*pDst++ = (uint8_t) ((src3 << 4) | (src4 >> 7));
		*pDst++ = (uint8_t) ((src4 << 1) | (src5 >> 10));
		*pDst++ = (uint8_t) ((src5 >> 2));
		*pDst++ = (uint8_t) ((src5 << 6) | (src6 >> 5));
		*pDst++ = (uint8_t) ((src6 << 3) | (src7 >> 8));
		*pDst++ = (uint8_t) (src7);
	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS2((uint32_t )pSrc[i + 0], 11)
		if (length > 1U) {
			PUTBITS2((uint32_t )pSrc[i + 1], 11)
			if (length > 2U) {
				PUTBITS2((uint32_t )pSrc[i + 2], 11)
				if (length > 3U) {
					PUTBITS2((uint32_t )pSrc[i + 3], 11)
					if (length > 4U) {
						PUTBITS2((uint32_t )pSrc[i + 4], 11)
						if (length > 5U) {
							PUTBITS2((uint32_t )pSrc[i + 5], 11)
							if (length > 6U) {
								PUTBITS2((uint32_t )pSrc[i + 6], 11)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}
void convert_tif_32sto12u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 1U); i += 2U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];

		*pDst++ = (uint8_t) (src0 >> 4);
		*pDst++ = (uint8_t) (((src0 & 0xFU) << 4) | (src1 >> 8));
		*pDst++ = (uint8_t) (src1);
	}

	if (length & 1U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		*pDst++ = (uint8_t) (src0 >> 4);
		*pDst++ = (uint8_t) (((src0 & 0xFU) << 4));
	}
}

void convert_tif_32sto13u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
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

		*pDst++ = (uint8_t) ((src0 >> 5));
		*pDst++ = (uint8_t) ((src0 << 3) | (src1 >> 10));
		*pDst++ = (uint8_t) ((src1 >> 2));
		*pDst++ = (uint8_t) ((src1 << 6) | (src2 >> 7));
		*pDst++ = (uint8_t) ((src2 << 1) | (src3 >> 12));
		*pDst++ = (uint8_t) ((src3 >> 4));
		*pDst++ = (uint8_t) ((src3 << 4) | (src4 >> 9));
		*pDst++ = (uint8_t) ((src4 >> 1));
		*pDst++ = (uint8_t) ((src4 << 7) | (src5 >> 6));
		*pDst++ = (uint8_t) ((src5 << 2) | (src6 >> 11));
		*pDst++ = (uint8_t) ((src6 >> 3));
		*pDst++ = (uint8_t) ((src6 << 5) | (src7 >> 8));
		*pDst++ = (uint8_t) (src7);
	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS2((uint32_t )pSrc[i + 0], 13)
		if (length > 1U) {
			PUTBITS2((uint32_t )pSrc[i + 1], 13)
			if (length > 2U) {
				PUTBITS2((uint32_t )pSrc[i + 2], 13)
				if (length > 3U) {
					PUTBITS2((uint32_t )pSrc[i + 3], 13)
					if (length > 4U) {
						PUTBITS2((uint32_t )pSrc[i + 4], 13)
						if (length > 5U) {
							PUTBITS2((uint32_t )pSrc[i + 5], 13)
							if (length > 6U) {
								PUTBITS2((uint32_t )pSrc[i + 6], 13)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}

void convert_tif_32sto14u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t src0 = (uint32_t) pSrc[i + 0];
		uint32_t src1 = (uint32_t) pSrc[i + 1];
		uint32_t src2 = (uint32_t) pSrc[i + 2];
		uint32_t src3 = (uint32_t) pSrc[i + 3];

		*pDst++ = (uint8_t) (src0 >> 6);
		*pDst++ = (uint8_t) (((src0 & 0x3FU) << 2) | (src1 >> 12));
		*pDst++ = (uint8_t) (src1 >> 4);
		*pDst++ = (uint8_t) (((src1 & 0xFU) << 4) | (src2 >> 10));
		*pDst++ = (uint8_t) (src2 >> 2);
		*pDst++ = (uint8_t) (((src2 & 0x3U) << 6) | (src3 >> 8));
		*pDst++ = (uint8_t) (src3);
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
		*pDst++ = (uint8_t) (src0 >> 6);
		*pDst++ = (uint8_t) (((src0 & 0x3FU) << 2) | (src1 >> 12));
		if (length > 1U) {
			*pDst++ = (uint8_t) (src1 >> 4);
			*pDst++ = (uint8_t) (((src1 & 0xFU) << 4) | (src2 >> 10));
			if (length > 2U) {
				*pDst++ = (uint8_t) (src2 >> 2);
				*pDst++ = (uint8_t) (((src2 & 0x3U) << 6));
			}
		}
	}
}

void convert_tif_32sto15u(const int32_t *pSrc, uint8_t *pDst, size_t length) {
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

		*pDst++ = (uint8_t) ((src0 >> 7));
		*pDst++ = (uint8_t) ((src0 << 1) | (src1 >> 14));
		*pDst++ = (uint8_t) ((src1 >> 6));
		*pDst++ = (uint8_t) ((src1 << 2) | (src2 >> 13));
		*pDst++ = (uint8_t) ((src2 >> 5));
		*pDst++ = (uint8_t) ((src2 << 3) | (src3 >> 12));
		*pDst++ = (uint8_t) ((src3 >> 4));
		*pDst++ = (uint8_t) ((src3 << 4) | (src4 >> 11));
		*pDst++ = (uint8_t) ((src4 >> 3));
		*pDst++ = (uint8_t) ((src4 << 5) | (src5 >> 10));
		*pDst++ = (uint8_t) ((src5 >> 2));
		*pDst++ = (uint8_t) ((src5 << 6) | (src6 >> 9));
		*pDst++ = (uint8_t) ((src6 >> 1));
		*pDst++ = (uint8_t) ((src6 << 7) | (src7 >> 8));
		*pDst++ = (uint8_t) (src7);
	}

	if (length & 7U) {
		uint32_t trailing = 0U;
		int remaining = 8U;
		length &= 7U;
		PUTBITS2((uint32_t )pSrc[i + 0], 15)
		if (length > 1U) {
			PUTBITS2((uint32_t )pSrc[i + 1], 15)
			if (length > 2U) {
				PUTBITS2((uint32_t )pSrc[i + 2], 15)
				if (length > 3U) {
					PUTBITS2((uint32_t )pSrc[i + 3], 15)
					if (length > 4U) {
						PUTBITS2((uint32_t )pSrc[i + 4], 15)
						if (length > 5U) {
							PUTBITS2((uint32_t )pSrc[i + 5], 15)
							if (length > 6U) {
								PUTBITS2((uint32_t )pSrc[i + 6], 15)
							}
						}
					}
				}
			}
		}
		FLUSHBITS()
	}
}

void convert_tif_32sto16u(const int32_t *pSrc, uint16_t *pDst, size_t length) {
	for (size_t i = 0; i < length; ++i)
		pDst[i] = (uint16_t) pSrc[i];
}

void convert_tif_3uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 >> 5)), INV_MASK_3, invert);
		pDst[i + 1] = INV((int32_t )(((val0 & 0x1FU) >> 2)), INV_MASK_3,
				invert);
		pDst[i + 2] = INV((int32_t )(((val0 & 0x3U) << 1) | (val1 >> 7)),
				INV_MASK_3, invert);
		pDst[i + 3] = INV((int32_t )(((val1 & 0x7FU) >> 4)), INV_MASK_3,
				invert);
		pDst[i + 4] = INV((int32_t )(((val1 & 0xFU) >> 1)), INV_MASK_3, invert);
		pDst[i + 5] = INV((int32_t )(((val1 & 0x1U) << 2) | (val2 >> 6)),
				INV_MASK_3, invert);
		pDst[i + 6] = INV((int32_t )(((val2 & 0x3FU) >> 3)), INV_MASK_3,
				invert);
		pDst[i + 7] = INV((int32_t )(((val2 & 0x7U))), INV_MASK_3, invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 3, INV_MASK_3, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 3, INV_MASK_3, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 3, INV_MASK_3, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 3, INV_MASK_3, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 3, INV_MASK_3, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 3, INV_MASK_3, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 3, INV_MASK_3, invert)
							}
						}
					}
				}
			}
		}
	}
}
void convert_tif_5uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 >> 3)), INV_MASK_5, invert);
		pDst[i + 1] = INV((int32_t )(((val0 & 0x7U) << 2) | (val1 >> 6)),
				INV_MASK_5, invert);
		pDst[i + 2] = INV((int32_t )(((val1 & 0x3FU) >> 1)), INV_MASK_5,
				invert);
		pDst[i + 3] = INV((int32_t )(((val1 & 0x1U) << 4) | (val2 >> 4)),
				INV_MASK_5, invert);
		pDst[i + 4] = INV((int32_t )(((val2 & 0xFU) << 1) | (val3 >> 7)),
				INV_MASK_5, invert);
		pDst[i + 5] = INV((int32_t )(((val3 & 0x7FU) >> 2)), INV_MASK_5,
				invert);
		pDst[i + 6] = INV((int32_t )(((val3 & 0x3U) << 3) | (val4 >> 5)),
				INV_MASK_5, invert);
		pDst[i + 7] = INV((int32_t )(((val4 & 0x1FU))), INV_MASK_5, invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 5, INV_MASK_5, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 5, INV_MASK_5, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 5, INV_MASK_5, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 5, INV_MASK_5, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 5, INV_MASK_5, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 5, INV_MASK_5, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 5, INV_MASK_5, invert)
							}
						}
					}
				}
			}
		}
	}
}
void convert_tif_7uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 >> 1)), INV_MASK_7, invert);
		pDst[i + 1] = INV((int32_t )(((val0 & 0x1U) << 6) | (val1 >> 2)),
				INV_MASK_7, invert);
		pDst[i + 2] = INV((int32_t )(((val1 & 0x3U) << 5) | (val2 >> 3)),
				INV_MASK_7, invert);
		pDst[i + 3] = INV((int32_t )(((val2 & 0x7U) << 4) | (val3 >> 4)),
				INV_MASK_7, invert);
		pDst[i + 4] = INV((int32_t )(((val3 & 0xFU) << 3) | (val4 >> 5)),
				INV_MASK_7, invert);
		pDst[i + 5] = INV((int32_t )(((val4 & 0x1FU) << 2) | (val5 >> 6)),
				INV_MASK_7, invert);
		pDst[i + 6] = INV((int32_t )(((val5 & 0x3FU) << 1) | (val6 >> 7)),
				INV_MASK_7, invert);
		pDst[i + 7] = INV((int32_t )(((val6 & 0x7FU))), INV_MASK_7, invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 7, INV_MASK_7, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 7, INV_MASK_7, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 7, INV_MASK_7, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 7, INV_MASK_7, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 7, INV_MASK_7, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 7, INV_MASK_7, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 7, INV_MASK_7, invert)
							}
						}
					}
				}
			}
		}
	}
}
void convert_tif_9uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;
		uint32_t val7 = *pSrc++;
		uint32_t val8 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 1) | (val1 >> 7)), INV_MASK_9,
				invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0x7FU) << 2) | (val2 >> 6)),
				INV_MASK_9, invert);
		pDst[i + 2] = INV((int32_t )(((val2 & 0x3FU) << 3) | (val3 >> 5)),
				INV_MASK_9, invert);
		pDst[i + 3] = INV((int32_t )(((val3 & 0x1FU) << 4) | (val4 >> 4)),
				INV_MASK_9, invert);
		pDst[i + 4] = INV((int32_t )(((val4 & 0xFU) << 5) | (val5 >> 3)),
				INV_MASK_9, invert);
		pDst[i + 5] = INV((int32_t )(((val5 & 0x7U) << 6) | (val6 >> 2)),
				INV_MASK_9, invert);
		pDst[i + 6] = INV((int32_t )(((val6 & 0x3U) << 7) | (val7 >> 1)),
				INV_MASK_9, invert);
		pDst[i + 7] = INV((int32_t )(((val7 & 0x1U) << 8) | (val8)), INV_MASK_9,
				invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 9, INV_MASK_9, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 9, INV_MASK_9, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 9, INV_MASK_9, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 9, INV_MASK_9, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 9, INV_MASK_9, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 9, INV_MASK_9, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 9, INV_MASK_9, invert)
							}
						}
					}
				}
			}
		}
	}
}

void convert_tif_10uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 2) | (val1 >> 6)), INV_MASK_10,
				invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0x3FU) << 4) | (val2 >> 4)),
				INV_MASK_10, invert);
		pDst[i + 2] = INV((int32_t )(((val2 & 0xFU) << 6) | (val3 >> 2)),
				INV_MASK_10, invert);
		pDst[i + 3] = INV((int32_t )(((val3 & 0x3U) << 8) | val4), INV_MASK_10,
				invert);

	}
	if (length & 3U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		length = length & 3U;
		pDst[i + 0] = INV((int32_t )((val0 << 2) | (val1 >> 6)), INV_MASK_10,
				invert);

		if (length > 1U) {
			uint32_t val2 = *pSrc++;
			pDst[i + 1] = INV((int32_t )(((val1 & 0x3FU) << 4) | (val2 >> 4)),
					INV_MASK_10, invert);
			if (length > 2U) {
				uint32_t val3 = *pSrc++;
				pDst[i + 2] = INV(
						(int32_t )(((val2 & 0xFU) << 6) | (val3 >> 2)),
						INV_MASK_10, invert);
			}
		}
	}
}

void convert_tif_11uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
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

		pDst[i + 0] = INV((int32_t )((val0 << 3) | (val1 >> 5)), INV_MASK_11,
				invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0x1FU) << 6) | (val2 >> 2)),
				INV_MASK_11, invert);
		pDst[i + 2] = INV(
				(int32_t )(((val2 & 0x3U) << 9) | (val3 << 1) | (val4 >> 7)),
				INV_MASK_11, invert);
		pDst[i + 3] = INV((int32_t )(((val4 & 0x7FU) << 4) | (val5 >> 4)),
				INV_MASK_11, invert);
		pDst[i + 4] = INV((int32_t )(((val5 & 0xFU) << 7) | (val6 >> 1)),
				INV_MASK_11, invert);
		pDst[i + 5] = INV(
				(int32_t )(((val6 & 0x1U) << 10) | (val7 << 2) | (val8 >> 6)),
				INV_MASK_11, invert);
		pDst[i + 6] = INV((int32_t )(((val8 & 0x3FU) << 5) | (val9 >> 3)),
				INV_MASK_11, invert);
		pDst[i + 7] = INV((int32_t )(((val9 & 0x7U) << 8) | (val10)),
				INV_MASK_11, invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 11, INV_MASK_11, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 11, INV_MASK_11, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 11, INV_MASK_11, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 11, INV_MASK_11, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 11, INV_MASK_11, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 11, INV_MASK_11, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 11, INV_MASK_11, invert)
							}
						}
					}
				}
			}
		}
	}
}
void convert_tif_12uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 1U); i += 2U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 4) | (val1 >> 4)), INV_MASK_12,
				invert);
		pDst[i + 1] = INV((int32_t )(((val1 & 0xFU) << 8) | val2), INV_MASK_12,
				invert);
	}
	if (length & 1U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		pDst[i + 0] = INV((int32_t )((val0 << 4) | (val1 >> 4)), INV_MASK_12,
				invert);
	}
}

void convert_tif_13uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
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

		pDst[i + 0] = INV((int32_t )((val0 << 5) | (val1 >> 3)), INV_MASK_13,
				invert);
		pDst[i + 1] = INV(
				(int32_t )(((val1 & 0x7U) << 10) | (val2 << 2) | (val3 >> 6)),
				INV_MASK_13, invert);
		pDst[i + 2] = INV((int32_t )(((val3 & 0x3FU) << 7) | (val4 >> 1)),
				INV_MASK_13, invert);
		pDst[i + 3] = INV(
				(int32_t )(((val4 & 0x1U) << 12) | (val5 << 4) | (val6 >> 4)),
				INV_MASK_13, invert);
		pDst[i + 4] = INV(
				(int32_t )(((val6 & 0xFU) << 9) | (val7 << 1) | (val8 >> 7)),
				INV_MASK_13, invert);
		pDst[i + 5] = INV((int32_t )(((val8 & 0x7FU) << 6) | (val9 >> 2)),
				INV_MASK_13, invert);
		pDst[i + 6] = INV(
				(int32_t )(((val9 & 0x3U) << 11) | (val10 << 3) | (val11 >> 5)),
				INV_MASK_13, invert);
		pDst[i + 7] = INV((int32_t )(((val11 & 0x1FU) << 8) | (val12)),
				INV_MASK_13, invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 13, INV_MASK_13, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 13, INV_MASK_13, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 13, INV_MASK_13, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 13, INV_MASK_13, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 13, INV_MASK_13, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 13, INV_MASK_13, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 13, INV_MASK_13, invert)
							}
						}
					}
				}
			}
		}
	}
}

void convert_tif_14uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 3U); i += 4U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		uint32_t val2 = *pSrc++;
		uint32_t val3 = *pSrc++;
		uint32_t val4 = *pSrc++;
		uint32_t val5 = *pSrc++;
		uint32_t val6 = *pSrc++;

		pDst[i + 0] = INV((int32_t )((val0 << 6) | (val1 >> 2)), INV_MASK_14,
				invert);
		pDst[i + 1] = INV(
				(int32_t )(((val1 & 0x3U) << 12) | (val2 << 4) | (val3 >> 4)),
				INV_MASK_14, invert);
		pDst[i + 2] = INV(
				(int32_t )(((val3 & 0xFU) << 10) | (val4 << 2) | (val5 >> 6)),
				INV_MASK_14, invert);
		pDst[i + 3] = INV((int32_t )(((val5 & 0x3FU) << 8) | val6), INV_MASK_14,
				invert);

	}
	if (length & 3U) {
		uint32_t val0 = *pSrc++;
		uint32_t val1 = *pSrc++;
		length = length & 3U;
		pDst[i + 0] = (int32_t) ((val0 << 6) | (val1 >> 2));

		if (length > 1U) {
			uint32_t val2 = *pSrc++;
			uint32_t val3 = *pSrc++;
			pDst[i + 1] =
					INV(
							(int32_t )(((val1 & 0x3U) << 12) | (val2 << 4)
									| (val3 >> 4)), INV_MASK_14, invert);
			if (length > 2U) {
				uint32_t val4 = *pSrc++;
				uint32_t val5 = *pSrc++;
				pDst[i + 2] = INV(
						(int32_t )(((val3 & 0xFU) << 10) | (val4 << 2)
								| (val5 >> 6)), INV_MASK_14, invert);
			}
		}
	}
}

void convert_tif_15uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	size_t i;
	for (i = 0; i < (length & ~(size_t) 7U); i += 8U) {
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

		pDst[i + 0] = INV((int32_t )((val0 << 7) | (val1 >> 1)), (1 << 15) - 1,
				invert);
		pDst[i + 1] = INV(
				(int32_t )(((val1 & 0x1U) << 14) | (val2 << 6) | (val3 >> 2)),
				INV_MASK_15, invert);
		pDst[i + 2] = INV(
				(int32_t )(((val3 & 0x3U) << 13) | (val4 << 5) | (val5 >> 3)),
				INV_MASK_15, invert);
		pDst[i + 3] = INV(
				(int32_t )(((val5 & 0x7U) << 12) | (val6 << 4) | (val7 >> 4)),
				INV_MASK_15, invert);
		pDst[i + 4] = INV(
				(int32_t )(((val7 & 0xFU) << 11) | (val8 << 3) | (val9 >> 5)),
				INV_MASK_15, invert);
		pDst[i + 5] =
				INV(
						(int32_t )(((val9 & 0x1FU) << 10) | (val10 << 2)
								| (val11 >> 6)), INV_MASK_15, invert);
		pDst[i + 6] =
				INV(
						(int32_t )(((val11 & 0x3FU) << 9) | (val12 << 1)
								| (val13 >> 7)), INV_MASK_15, invert);
		pDst[i + 7] = INV((int32_t )(((val13 & 0x7FU) << 8) | (val14)),
				INV_MASK_15, invert);

	}
	if (length & 7U) {
		uint32_t val;
		int available = 0;

		length = length & 7U;

		GETBITS(pDst[i + 0], 15, INV_MASK_15, invert)

		if (length > 1U) {
			GETBITS(pDst[i + 1], 15, INV_MASK_15, invert)
			if (length > 2U) {
				GETBITS(pDst[i + 2], 15, INV_MASK_15, invert)
				if (length > 3U) {
					GETBITS(pDst[i + 3], 15, INV_MASK_15, invert)
					if (length > 4U) {
						GETBITS(pDst[i + 4], 15, INV_MASK_15, invert)
						if (length > 5U) {
							GETBITS(pDst[i + 5], 15, INV_MASK_15, invert)
							if (length > 6U) {
								GETBITS(pDst[i + 6], 15, INV_MASK_15, invert)
							}
						}
					}
				}
			}
		}
	}
}

/* seems that libtiff decodes this to machine endianness */
void convert_tif_16uto32s(const uint16_t *pSrc, int32_t *pDst, size_t length,
		bool invert) {
	for (size_t i = 0; i < length; i++)
		pDst[i] = INV(pSrc[i], 0xFFFF, invert);
}

void bmp_applyLUT8u_1u32s_C1R(uint8_t const *pSrc, int32_t srcStride,
		int32_t *pDst, int32_t dstStride, uint8_t const *pLUT, uint32_t destWidth,
		uint32_t destHeight) {
	uint32_t absSrcStride = std::abs(srcStride);
	for (uint32_t y = destHeight; y != 0U; --y) {
		uint32_t destIndex = 0;
		for (uint32_t srcIndex = 0; srcIndex < absSrcStride; srcIndex++) {
			uint8_t val = pSrc[srcIndex];
			for (int32_t ct = 7; ct >= 0; --ct) {
				pDst[destIndex++] = (int32_t) pLUT[(val >> (ct))&1];
				if (destIndex == destWidth)
					break;
			}
		}
		pSrc += srcStride;
		pDst += dstStride;
	}
}

void bmp_applyLUT8u_4u32s_C1R(uint8_t const *pSrc, int32_t srcStride,
		int32_t *pDst, int32_t dstStride, uint8_t const *pLUT, uint32_t destWidth,
		uint32_t destHeight) {
	uint32_t absSrcStride = std::abs(srcStride);
	for (uint32_t y = destHeight; y != 0U; --y) {
		uint32_t destIndex = 0;
		for (uint32_t srcIndex = 0; srcIndex < absSrcStride; srcIndex++) {
			uint8_t val = pSrc[srcIndex];
			for (int32_t ct = 4; ct >= 0; ct-=4) {
				pDst[destIndex++] = (int32_t) pLUT[(val >> (ct)) & 0xF];
				if (destIndex == destWidth)
					break;
			}
		}
		pSrc += srcStride;
		pDst += dstStride;
	}
}

void bmp_applyLUT8u_8u32s_C1R(uint8_t const *pSrc,
									int32_t srcStride,
									int32_t *pDst,
									int32_t dstStride,
									uint8_t const *pLUT,
									uint32_t width,
									uint32_t height) {
	for (uint32_t y = height; y != 0U; --y) {
		for (uint32_t x = 0; x < width; x++)
			pDst[x] = (int32_t) pLUT[pSrc[x]];
		pSrc += srcStride;
		pDst += dstStride;
	}
}

void bmp_applyLUT8u_1u32s_C1P3R(uint8_t const *pSrc, int32_t srcStride,
		int32_t *const*pDst, int32_t const *pDstStride,
		uint8_t const *const*pLUT, uint32_t destWidth, uint32_t destHeight) {
	uint32_t absSrcStride = std::abs(srcStride);
	uint32_t y;
	int32_t *pR = pDst[0];
	int32_t *pG = pDst[1];
	int32_t *pB = pDst[2];
	uint8_t const *pLUT_R = pLUT[0];
	uint8_t const *pLUT_G = pLUT[1];
	uint8_t const *pLUT_B = pLUT[2];

	for (y = destHeight; y != 0U; --y) {
		uint32_t destIndex = 0;
		for (uint32_t srcIndex = 0; srcIndex < absSrcStride; srcIndex++) {
			uint8_t idx = pSrc[srcIndex];
			for (int32_t ct = 7; ct >= 0; ct--) {
				uint8_t val = (idx >> ct) & 0x1;
				pR[destIndex] = (int32_t) pLUT_R[val];
				pG[destIndex] = (int32_t) pLUT_G[val];
				pB[destIndex] = (int32_t) pLUT_B[val];
				destIndex++;
				if (destIndex == destWidth)
					break;
			}
		}
		pSrc += srcStride;
		pR += pDstStride[0];
		pG += pDstStride[1];
		pB += pDstStride[2];
	}
}


void bmp_applyLUT8u_4u32s_C1P3R(uint8_t const *pSrc, int32_t srcStride,
		int32_t *const*pDst, int32_t const *pDstStride,
		uint8_t const *const*pLUT, uint32_t destWidth, uint32_t destHeight) {
	uint32_t absSrcStride = std::abs(srcStride);
	uint32_t y;
	int32_t *pR = pDst[0];
	int32_t *pG = pDst[1];
	int32_t *pB = pDst[2];
	uint8_t const *pLUT_R = pLUT[0];
	uint8_t const *pLUT_G = pLUT[1];
	uint8_t const *pLUT_B = pLUT[2];

	for (y = destHeight; y != 0U; --y) {
		uint32_t destIndex = 0;
		for (uint32_t srcIndex = 0; srcIndex < absSrcStride; srcIndex++) {
			uint8_t idx = pSrc[srcIndex];
			for (int32_t ct = 4; ct >= 0; ct-=4) {
				uint8_t val = (idx >> ct) & 0xF;
				pR[destIndex] = (int32_t) pLUT_R[val];
				pG[destIndex] = (int32_t) pLUT_G[val];
				pB[destIndex] = (int32_t) pLUT_B[val];
				destIndex++;
				if (destIndex == destWidth)
					break;
			}
		}
		pSrc += srcStride;
		pR += pDstStride[0];
		pG += pDstStride[1];
		pB += pDstStride[2];
	}
}

void bmp_applyLUT8u_8u32s_C1P3R(uint8_t const *pSrc, int32_t srcStride,
		int32_t *const*pDst, int32_t const *pDstStride,
		uint8_t const *const*pLUT, uint32_t destWidth, uint32_t destHeight) {
	uint32_t y;
	int32_t *pR = pDst[0];
	int32_t *pG = pDst[1];
	int32_t *pB = pDst[2];
	uint8_t const *pLUT_R = pLUT[0];
	uint8_t const *pLUT_G = pLUT[1];
	uint8_t const *pLUT_B = pLUT[2];

	for (y = destHeight; y != 0U; --y) {
		for (uint32_t x = 0; x < destWidth; x++) {
			uint8_t idx = pSrc[x];
			pR[x] = (int32_t) pLUT_R[idx];
			pG[x] = (int32_t) pLUT_G[idx];
			pB[x] = (int32_t) pLUT_B[idx];
		}
		pSrc += srcStride;
		pR += pDstStride[0];
		pG += pDstStride[1];
		pB += pDstStride[2];
	}
}
void bmp24toimage(const uint8_t *pData, uint32_t srcStride,
		grk_image *image) {
	int index;
	uint32_t width, height;
	const uint8_t *pSrc = nullptr;
	width = image->comps[0].w;
	height = image->comps[0].h;
	index = 0;
	pSrc = pData + (height - 1U) * srcStride;
	uint32_t stride_diff = image->comps[0].stride - image->comps[0].w;
	for (uint32_t y = 0; y < height; y++) {
		size_t src_index = 0;
		for (uint32_t x = 0; x < width; x++) {
			image->comps[0].data[index] = (int32_t) pSrc[src_index + 2]; /* R */
			image->comps[1].data[index] = (int32_t) pSrc[src_index + 1]; /* G */
			image->comps[2].data[index] = (int32_t) pSrc[src_index]; /* B */
			index++;
			src_index += 3;
		}
		index+= stride_diff;
		pSrc -= srcStride;
	}
}

static void bmp_mask_get_shift_and_prec(uint32_t mask, uint32_t *shift,
		uint32_t *prec) {
	uint32_t l_shift, l_prec;
	l_shift = l_prec = 0U;
	if (mask != 0U) {
		while ((mask & 1U) == 0U) {
			mask >>= 1;
			l_shift++;
		}
		while (mask & 1U) {
			mask >>= 1;
			l_prec++;
		}
	}
	*shift = l_shift;
	*prec = l_prec;
}



void bmp_mask32toimage(const uint8_t *pData, uint32_t srcStride,
		grk_image *image, uint32_t redMask, uint32_t greenMask,
		uint32_t blueMask, uint32_t alphaMask) {
	uint32_t redShift, redPrec;
	uint32_t greenShift, greenPrec;
	uint32_t blueShift, bluePrec;
	uint32_t alphaShift, alphaPrec;

	uint32_t width = image->comps[0].w;
	uint32_t stride_diff = image->comps[0].stride - width;
	uint32_t height = image->comps[0].h;
	bool hasAlpha = image->numcomps > 3U;
	bmp_mask_get_shift_and_prec(redMask, &redShift, &redPrec);
	bmp_mask_get_shift_and_prec(greenMask, &greenShift, &greenPrec);
	bmp_mask_get_shift_and_prec(blueMask, &blueShift, &bluePrec);
	bmp_mask_get_shift_and_prec(alphaMask, &alphaShift, &alphaPrec);
	image->comps[0].prec = redPrec;
	image->comps[1].prec = greenPrec;
	image->comps[2].prec = bluePrec;
	if (hasAlpha)
		image->comps[3].prec = alphaPrec;
	int index=0;
	uint32_t x, y;
	auto pSrc = pData + (height - 1U) * srcStride;
	for (y = 0; y < height; y++) {
		size_t src_index = 0;
		for (x = 0; x < width; x++) {
			uint32_t value = 0U;
			value |= ((uint32_t) pSrc[src_index]) << 0;
			value |= ((uint32_t) pSrc[src_index + 1]) << 8;
			value |= ((uint32_t) pSrc[src_index + 2]) << 16;
			value |= ((uint32_t) pSrc[src_index + 3]) << 24;

			image->comps[0].data[index] = (int32_t) ((value & redMask)
					>> redShift); /* R */
			image->comps[1].data[index] = (int32_t) ((value & greenMask)
					>> greenShift); /* G */
			image->comps[2].data[index] = (int32_t) ((value & blueMask)
					>> blueShift); /* B */
			if (hasAlpha) {
				image->comps[3].data[index] = (int32_t) ((value & alphaMask)
						>> alphaShift); /* A */
			}
			index++;
			src_index += 4;
		}
		index += stride_diff;
		pSrc -= srcStride;
	}
}

void bmp_mask16toimage(const uint8_t *pData, uint32_t srcStride,
		grk_image *image, uint32_t redMask, uint32_t greenMask,
		uint32_t blueMask, uint32_t alphaMask) {
	uint32_t redShift, redPrec;
	uint32_t greenShift, greenPrec;
	uint32_t blueShift, bluePrec;
	uint32_t alphaShift, alphaPrec;

	uint32_t width = image->comps[0].w;
	uint32_t stride_diff = image->comps[0].stride - width;
	uint32_t height = image->comps[0].h;
	bool hasAlpha = image->numcomps > 3U;
	bmp_mask_get_shift_and_prec(redMask, &redShift, &redPrec);
	bmp_mask_get_shift_and_prec(greenMask, &greenShift, &greenPrec);
	bmp_mask_get_shift_and_prec(blueMask, &blueShift, &bluePrec);
	bmp_mask_get_shift_and_prec(alphaMask, &alphaShift, &alphaPrec);
	image->comps[0].prec = redPrec;
	image->comps[1].prec = greenPrec;
	image->comps[2].prec = bluePrec;
	if (hasAlpha)
		image->comps[3].prec = alphaPrec;
	int index=0;
	uint32_t x, y;
	auto pSrc = pData + (height - 1U) * srcStride;
	for (y = 0; y < height; y++) {
		size_t src_index = 0;
		for (x = 0; x < width; x++) {
			uint32_t value = ((uint32_t) pSrc[src_index + 0]) << 0;
			value |= ((uint32_t) pSrc[src_index + 1]) << 8;

			image->comps[0].data[index] = (int32_t) ((value & redMask)
					>> redShift); /* R */
			image->comps[1].data[index] = (int32_t) ((value & greenMask)
					>> greenShift); /* G */
			image->comps[2].data[index] = (int32_t) ((value & blueMask)
					>> blueShift); /* B */
			if (hasAlpha) {
				image->comps[3].data[index] = (int32_t) ((value & alphaMask)
						>> alphaShift); /* A */
			}
			index++;
			src_index += 2;
		}
		index += stride_diff;
		pSrc -= srcStride;
	}
}


