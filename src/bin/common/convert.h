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

void convert_tif_32sto3u(const int32_t *pSrc, uint8_t *pDst, size_t length);
void convert_tif_32sto5u(const int32_t *pSrc, uint8_t *pDst, size_t length);
void convert_tif_32sto7u(const int32_t *pSrc, uint8_t *pDst, size_t length);
void convert_tif_32sto9u(const int32_t *pSrc, uint8_t *pDst, size_t length);
void convert_tif_32sto10u(const int32_t *pSrc, uint8_t *pDst, size_t length);
void convert_tif_32sto11u(const int32_t *pSrc, uint8_t *pDst, size_t length);
void convert_tif_32sto12u(const int32_t *pSrc, uint8_t *pDst, size_t length);
void convert_tif_32sto13u(const int32_t *pSrc, uint8_t *pDst, size_t length);
void convert_tif_32sto14u(const int32_t *pSrc, uint8_t *pDst, size_t length);
void convert_tif_32sto15u(const int32_t *pSrc, uint8_t *pDst, size_t length);
void convert_tif_32sto16u(const int32_t *pSrc, uint16_t *pDst, size_t length);
void convert_tif_3uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
void convert_tif_5uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
void convert_tif_7uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
void convert_tif_9uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
void convert_tif_10uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
void convert_tif_11uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
void convert_tif_12uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
void convert_tif_13uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
void convert_tif_14uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
void convert_tif_15uto32s(const uint8_t *pSrc, int32_t *pDst, size_t length,
		bool invert);
void convert_tif_16uto32s(const uint16_t *pSrc, int32_t *pDst, size_t length,
		bool invert);

void bmp_applyLUT8u_1u32s_C1R(uint8_t const *pSrc, int32_t srcStride,
		int32_t *pDst, int32_t dstStride, uint8_t const *pLUT, uint32_t destWidth,
		uint32_t destHeight);
void bmp_applyLUT8u_4u32s_C1R(uint8_t const *pSrc, int32_t srcStride,
		int32_t *pDst, int32_t dstStride, uint8_t const *pLUT, uint32_t destWidth,
		uint32_t destHeight);
void bmp_applyLUT8u_8u32s_C1R(uint8_t const *pSrc,
									int32_t srcStride,
									int32_t *pDst,
									int32_t dstStride,
									uint8_t const *pLUT,
									uint32_t width,
									uint32_t height);
void bmp_applyLUT8u_1u32s_C1P3R(uint8_t const *pSrc, int32_t srcStride,
		int32_t *const*pDst, int32_t const *pDstStride,
		uint8_t const *const*pLUT, uint32_t destWidth, uint32_t destHeight);
void bmp_applyLUT8u_4u32s_C1P3R(uint8_t const *pSrc, int32_t srcStride,
		int32_t *const*pDst, int32_t const *pDstStride,
		uint8_t const *const*pLUT, uint32_t destWidth, uint32_t destHeight);
void bmp_applyLUT8u_8u32s_C1P3R(uint8_t const *pSrc, int32_t srcStride,
		int32_t *const*pDst, int32_t const *pDstStride,
		uint8_t const *const*pLUT, uint32_t destWidth, uint32_t destHeight);
void bmp_mask32toimage(const uint8_t *pData, uint32_t srcStride,
		grk_image *image, uint32_t redMask, uint32_t greenMask,
		uint32_t blueMask, uint32_t alphaMask);
void bmp_mask16toimage(const uint8_t *pData, uint32_t srcStride,
		grk_image *image, uint32_t redMask, uint32_t greenMask,
		uint32_t blueMask, uint32_t alphaMask);
void bmp24toimage(const uint8_t *pData, uint32_t srcStride,
		grk_image *image);
