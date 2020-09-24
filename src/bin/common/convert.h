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

#pragma once

#define INV_MASK_16 0xFFFF
#define INV_MASK_15 ((1<<15)-1)
#define INV_MASK_14 ((1<<14)-1)
#define INV_MASK_13 ((1<<13)-1)
#define INV_MASK_12 ((1<<12)-1)
#define INV_MASK_11 ((1<<11)-1)
#define INV_MASK_10 ((1<<10)-1)
#define INV_MASK_9 ((1<<9)-1)
#define INV_MASK_8 0xFF
#define INV_MASK_7 ((1<<7)-1)
#define INV_MASK_6 ((1<<6)-1)
#define INV_MASK_5 ((1<<5)-1)
#define INV_MASK_4 ((1<<4)-1)
#define INV_MASK_3 ((1<<3)-1)
#define INV_MASK_2 ((1<<2)-1)
#define INV(val, mask,invert)  ((invert) ? ((val)^(mask)) : (val))

/* Component precision clipping */
void clip_component( grk_image_comp  *  component, uint32_t precision);
/* Component precision scaling */
void scale_component( grk_image_comp  *  component, uint32_t precision);

grk_image* convert_gray_to_rgb(grk_image *original);
grk_image* upsample_image_components(grk_image *original);

/* planar / interleaved conversions */
typedef void(*cvtInterleavedToPlanar)(const int32_t* pSrc, int32_t* const* pDst, size_t length);
extern const cvtInterleavedToPlanar cvtInterleavedToPlanar_LUT[10];
typedef void(*cvtPlanarToInterleaved)(int32_t const* const* pSrc, int32_t* pDst, size_t length, int32_t adjust);
extern const cvtPlanarToInterleaved cvtPlanarToInterleaved_LUT[10];

/* bit depth conversions */
typedef void(*cvtTo32)(const uint8_t* pSrc, int32_t* pDst, size_t length,bool invert);
extern const cvtTo32 cvtTo32_LUT[9]; /* up to 8bpp */
typedef void(*cvtFrom32)(const int32_t* pSrc, uint8_t* pDst, size_t length);
extern const cvtFrom32 cvtFrom32_LUT[9]; /* up to 8bpp */

void convert_16u32s_C1R(const uint8_t *pSrc, int32_t *pDst,
		size_t length, bool invert);
