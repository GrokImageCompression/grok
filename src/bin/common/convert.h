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

#pragma once


/* Component precision scaling */
void scale_component(grk_image_comp* component, uint8_t precision);
grk_image* upsample(grk_image* original);

/* planar / interleaved conversions */
typedef void (*cvtInterleavedToPlanar)(const int32_t* pSrc, int32_t* const* pDst, size_t length);
extern const cvtInterleavedToPlanar cvtInterleavedToPlanar_LUT[10];
typedef void (*cvtPlanarToInterleaved)(int32_t const* const* pSrc, int32_t* pDst, size_t length,
									   int32_t adjust);
extern const cvtPlanarToInterleaved cvtPlanarToInterleaved_LUT[10];

/* bit depth conversions */
typedef void (*cvtTo32)(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);
extern const cvtTo32 cvtTo32_LUT[9]; /* up to 8bpp */
extern const cvtTo32 cvtsTo32_LUT[9]; /* up to 8bpp */
typedef void (*cvtFrom32)(const int32_t* pSrc, uint8_t* pDst, size_t length);
extern const cvtFrom32 cvtFrom32_LUT[9]; /* up to 8bpp */

void convert_16u32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);

void convert_tif_32sto3u(const int32_t* pSrc, uint8_t* pDst, size_t length);
void convert_tif_32sto5u(const int32_t* pSrc, uint8_t* pDst, size_t length);
void convert_tif_32sto7u(const int32_t* pSrc, uint8_t* pDst, size_t length);
void convert_tif_32sto9u(const int32_t* pSrc, uint8_t* pDst, size_t length);
void convert_tif_32sto10u(const int32_t* pSrc, uint8_t* pDst, size_t length);
void convert_tif_32sto11u(const int32_t* pSrc, uint8_t* pDst, size_t length);
void convert_tif_32sto12u(const int32_t* pSrc, uint8_t* pDst, size_t length);
void convert_tif_32sto13u(const int32_t* pSrc, uint8_t* pDst, size_t length);
void convert_tif_32sto14u(const int32_t* pSrc, uint8_t* pDst, size_t length);
void convert_tif_32sto15u(const int32_t* pSrc, uint8_t* pDst, size_t length);
void convert_tif_32sto16u(const int32_t* pSrc, uint8_t* pDst, size_t length);

void convert_tif_3uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);
void convert_tif_5uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);
void convert_tif_7uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);
void convert_tif_9uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);
void convert_tif_10sto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);
void convert_tif_10uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);
void convert_tif_11uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);
void convert_tif_12sto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);
void convert_tif_12uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);
void convert_tif_13uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);
void convert_tif_14uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);
void convert_tif_15uto32s(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert);
void convert_tif_16uto32s(const uint16_t* pSrc, int32_t* pDst, size_t length, bool invert);

int32_t sign_extend(int32_t val, uint8_t shift);
