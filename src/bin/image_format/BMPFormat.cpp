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

#include <cstdio>
#include <cstdlib>
#include "grk_apps_config.h"
#include "grok.h"
#include "BMPFormat.h"
#include "convert.h"
#include <cstring>
#include "common.h"

// `MBED` in big endian format
const uint32_t BMP_ICC_PROFILE_EMBEDDED = 0x4d424544;

typedef struct {
	uint16_t bfType; 		/* 'BM' for Bitmap (19776) */
	uint32_t bfSize; 		/* Size of the file        */
	uint16_t bfReserved1; 	/* Reserved : 0            */
	uint16_t bfReserved2; 	/* Reserved : 0            */
	uint32_t bfOffBits; 	/* Offset                  */
} GRK_BITMAPFILEHEADER;

const uint32_t fileHeaderSize = 14;

typedef struct {
	uint32_t biSize; 			/* Size of the structure in bytes */
	int32_t biWidth; 			/* Width of the image in pixels */
	int32_t biHeight; 			/* Height of the image in pixels */
	uint16_t biPlanes; 			/* 1 */
	uint16_t biBitCount; 		/* Number of color bits per pixels */
	uint32_t biCompression; 	/* Type of encoding:
	 	 	 	 	 	 	 	   0: none
	 	 	 	 	 	 	 	   1: RLE8
	 	 	 	 	 	 	 	   2: RLE4
	 	 	 	 	 	 	 	   3: BITFIELD */
	uint32_t biSizeImage; 		/* Size of the image in bytes */
	int32_t biXpelsPerMeter; 	/* Horizontal (X) resolution in pixels/meter */
	int32_t biYpelsPerMeter; 	/* Vertical (Y) resolution in pixels/meter */
	uint32_t biClrUsed; 		/* Number of color used in the image (0: ALL) */
	uint32_t biClrImportant; 	/* Number of important color (0: ALL) */
	uint32_t biRedMask; 		/* Red channel bit mask */
	uint32_t biGreenMask; 		/* Green channel bit mask */
	uint32_t biBlueMask; 		/* Blue channel bit mask */
	uint32_t biAlphaMask; 		/* Alpha channel bit mask */
	uint32_t biColorSpaceType; 	/* Color space type */
	uint8_t biColorSpaceEP[36]; /* Color space end points */
	uint32_t biRedGamma; 		/* Red channel gamma */
	uint32_t biGreenGamma; 		/* Green channel gamma */
	uint32_t biBlueGamma; 		/* Blue channel gamma */
	uint32_t biIntent; 			/* Intent */
	uint32_t biIccProfileData; 	/* offset to ICC profile data */
	uint32_t biIccProfileSize; 	/* ICC profile size */
	uint32_t biReserved; 		/* Reserved */
} GRK_BITMAPINFOHEADER;

template<typename T> bool get_int(FILE *INPUT, T *val) {
	T rc = 0;
	for (size_t i = 0; i < sizeof(T) << 3; i += 8) {
		int temp = getc(INPUT);
		if (temp == EOF)
			return false;
		rc |= (T) (temp << i);
	}
	*val = rc;
	return true;
}

template<typename T> bool put_int(FILE *INPUT, T val) {
	for (size_t i = 0; i < sizeof(T) << 3; i += 8)
		if (putc((val >> i)&0xFF, INPUT) == EOF )
			return false;
	return true;
}

static void grk_applyLUT8u_1u32s_C1R(uint8_t const *pSrc, int32_t srcStride,
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

static void grk_applyLUT8u_4u32s_C1R(uint8_t const *pSrc, int32_t srcStride,
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

static void grk_applyLUT8u_8u32s_C1R(uint8_t const *pSrc,
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

static void grk_applyLUT8u_1u32s_C1P3R(uint8_t const *pSrc, int32_t srcStride,
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


static void grk_applyLUT8u_4u32s_C1P3R(uint8_t const *pSrc, int32_t srcStride,
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

static void grk_applyLUT8u_8u32s_C1P3R(uint8_t const *pSrc, int32_t srcStride,
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
static void bmp24toimage(const uint8_t *pData, uint32_t srcStride,
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
static void bmpmask32toimage(const uint8_t *pData, uint32_t srcStride,
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
static void bmpmask16toimage(const uint8_t *pData, uint32_t srcStride,
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
static grk_image* bmp8toimage(const uint8_t *pData, uint32_t srcStride,
								grk_image *image, uint8_t const *const*pLUT,
								bool topDown) {
	uint32_t width = image->comps[0].w;
	uint32_t height = image->comps[0].h;
	auto pSrc = topDown ? pData : (pData + (height - 1U) * srcStride);
	int32_t s_stride = topDown ? (int32_t)srcStride : (-(int32_t)srcStride);
	if (image->numcomps == 1U) {
		grk_applyLUT8u_8u32s_C1R(pSrc,
								s_stride,
								image->comps[0].data,
								(int32_t) image->comps[0].stride,
								pLUT[0],
								width,
								height);
	} else {
		int32_t *pDst[3];
		int32_t pDstStride[3];

		pDst[0] = image->comps[0].data;
		pDst[1] = image->comps[1].data;
		pDst[2] = image->comps[2].data;
		pDstStride[0] = (int32_t) image->comps[0].stride;
		pDstStride[1] = (int32_t) image->comps[0].stride;
		pDstStride[2] = (int32_t) image->comps[0].stride;
		grk_applyLUT8u_8u32s_C1P3R(pSrc, s_stride, pDst, pDstStride,
				pLUT, width, height);
	}
	return image;
}
static grk_image* bmp4toimage(const uint8_t *pData, uint32_t srcStride,
		grk_image *image, uint8_t const *const*pLUT) {
	uint32_t width = image->comps[0].w;
	uint32_t height = image->comps[0].h;
	auto pSrc = pData + (height - 1U) * srcStride;
	if (image->numcomps == 1U) {
		grk_applyLUT8u_4u32s_C1R(pSrc, -(int32_t) srcStride, image->comps[0].data,
				(int32_t) image->comps[0].stride, pLUT[0], width, height);
	} else {
		int32_t *pDst[3];
		int32_t pDstStride[3];

		pDst[0] = image->comps[0].data;
		pDst[1] = image->comps[1].data;
		pDst[2] = image->comps[2].data;
		pDstStride[0] = (int32_t) image->comps[0].stride;
		pDstStride[1] = (int32_t) image->comps[0].stride;
		pDstStride[2] = (int32_t) image->comps[0].stride;
		grk_applyLUT8u_4u32s_C1P3R(pSrc, -(int32_t) srcStride, pDst, pDstStride,
				pLUT, width, height);
	}
	return image;
}
static grk_image* bmp1toimage(const uint8_t *pData, uint32_t srcStride,
		grk_image *image, uint8_t const *const*pLUT) {
	uint32_t width = image->comps[0].w;
	uint32_t height = image->comps[0].h;
	auto pSrc = pData + (height - 1U) * srcStride;
	if (image->numcomps == 1U) {
		grk_applyLUT8u_1u32s_C1R(pSrc, -(int32_t) srcStride, image->comps[0].data,
				(int32_t) image->comps[0].stride, pLUT[0], width, height);
	} else {
		int32_t *pDst[3];
		int32_t pDstStride[3];

		pDst[0] = image->comps[0].data;
		pDst[1] = image->comps[1].data;
		pDst[2] = image->comps[2].data;
		pDstStride[0] = (int32_t) image->comps[0].stride;
		pDstStride[1] = (int32_t) image->comps[0].stride;
		pDstStride[2] = (int32_t) image->comps[0].stride;
		grk_applyLUT8u_1u32s_C1P3R(pSrc, -(int32_t) srcStride, pDst, pDstStride,
				pLUT, width, height);
	}
	return image;
}
static bool bmp_read_file_header(FILE *INPUT, GRK_BITMAPFILEHEADER *header) {
	if (!get_int(INPUT, &header->bfType))
		return false;
	if (header->bfType != 19778) {
		spdlog::error("Not a BMP file");
		return false;
	}
	if (!get_int(INPUT, &header->bfSize))
		return false;
	if (!get_int(INPUT, &header->bfReserved1))
		return false;
	if (!get_int(INPUT, &header->bfReserved2))
		return false;
	if (!get_int(INPUT, &header->bfOffBits))
		return false;
	return true;
}

const uint32_t BITMAPCOREHEADER_LENGTH = 12U;
const uint32_t BITMAPINFOHEADER_LENGTH = 40U;
const uint32_t BITMAPV2INFOHEADER_LENGTH = 52U;
const uint32_t BITMAPV3INFOHEADER_LENGTH = 56U;
const uint32_t BITMAPV4HEADER_LENGTH = 108U;
const uint32_t BITMAPV5HEADER_LENGTH = 124U;

static bool bmp_read_info_header(FILE *INPUT, GRK_BITMAPFILEHEADER *file_header, GRK_BITMAPINFOHEADER *header) {
	memset(header, 0, sizeof(*header));
	/* INFO HEADER */
	/* ------------- */
	if (!get_int(INPUT, &header->biSize))
		return false;

	switch (header->biSize) {
	case BITMAPCOREHEADER_LENGTH:
	case BITMAPINFOHEADER_LENGTH:
	case BITMAPV2INFOHEADER_LENGTH:
	case BITMAPV3INFOHEADER_LENGTH:
	case BITMAPV4HEADER_LENGTH:
	case BITMAPV5HEADER_LENGTH:
		break;
	default:
		spdlog::error("unknown BMP header size {}", header->biSize);
		return false;
	}
	if (header->biSize == BITMAPCOREHEADER_LENGTH){	//OS2
		uint16_t temp;
		if (!get_int(INPUT, &temp))
			return false;
		header->biWidth = temp;
		if (!get_int(INPUT, &temp))
			return false;
		header->biHeight = temp;
	} else {
		if (!get_int(INPUT, &header->biWidth))
			return false;
		if (!get_int(INPUT, &header->biHeight))
			return false;
	}
	if (!get_int(INPUT, &header->biPlanes))
		return false;
	if (!get_int(INPUT, &header->biBitCount))
		return false;
	// sanity check
	if (header->biBitCount > 32){
		spdlog::error("Bit count {} not supported.",header->biBitCount);
		return false;
	}
	if (header->biSize >= BITMAPINFOHEADER_LENGTH) {
		if (!get_int(INPUT, &header->biCompression))
			return false;
		if (!get_int(INPUT, &header->biSizeImage))
			return false;
		if (!get_int(INPUT, &header->biXpelsPerMeter))
			return false;
		if (!get_int(INPUT, &header->biYpelsPerMeter))
			return false;
		if (!get_int(INPUT, &header->biClrUsed))
			return false;
		if (!get_int(INPUT, &header->biClrImportant))
			return false;
		//re-adjust header size
		uint32_t defacto_header_size =
				file_header->bfSize - fileHeaderSize  -
					header->biClrUsed * (uint32_t)sizeof(uint32_t) - header->biSizeImage;
		if (defacto_header_size > header->biSize)
			header->biSize = std::min<uint32_t>(defacto_header_size,BITMAPV5HEADER_LENGTH);
	}
	if (header->biSize >= BITMAPV2INFOHEADER_LENGTH) {
		if (!get_int(INPUT, &header->biRedMask))
			return false;
		if (!get_int(INPUT, &header->biGreenMask))
			return false;
		if (!get_int(INPUT, &header->biBlueMask))
			return false;
	}
	if (header->biSize >= BITMAPV3INFOHEADER_LENGTH) {
		if (!get_int(INPUT, &header->biAlphaMask))
			return false;
	}
	if (header->biSize >= BITMAPV4HEADER_LENGTH) {
		if (!get_int(INPUT, &header->biColorSpaceType))
			return false;
		if (fread(&(header->biColorSpaceEP), 1U, sizeof(header->biColorSpaceEP),
				INPUT) != sizeof(header->biColorSpaceEP)) {
			spdlog::error("can't  read BMP header");
			return false;
		}
		if (!get_int(INPUT, &header->biRedGamma))
			return false;
		if (!get_int(INPUT, &header->biGreenGamma))
			return false;
		if (!get_int(INPUT, &header->biBlueGamma))
			return false;
	}
	if (header->biSize >= BITMAPV5HEADER_LENGTH) {
		if (!get_int(INPUT, &header->biIntent))
			return false;
		if (!get_int(INPUT, &header->biIccProfileData))
			return false;
		if (!get_int(INPUT, &header->biIccProfileSize))
			return false;
		if (!get_int(INPUT, &header->biReserved))
			return false;
	}
	return true;
}

static bool bmp_read_raw_data(FILE *INPUT, uint8_t *pData, uint32_t stride,
		uint32_t width, uint32_t height) {
	(void) (width);
	if (fread(pData, sizeof(uint8_t), stride * height, INPUT)
			!= (stride * height)) {
		spdlog::error("fread read fewer than expected number of bytes.");
		return false;
	}
	return true;
}

static bool bmp_read_rle8_data(FILE *INPUT, uint8_t *pData, uint32_t stride,
		uint32_t width, uint32_t height) {
	uint32_t x, y, written;
	uint8_t *pix;
	const uint8_t *beyond;

	beyond = pData + stride * height;
	pix = pData;

	x = y = written = 0U;
	while (y < height) {
		int c = getc(INPUT);
		if (c == EOF)
			return false;
		if (c) {
			int j;
			int temp = getc(INPUT);
			if (temp == EOF)
				return false;
			uint8_t c1 = (uint8_t) temp;
			for (j = 0;
					(j < c) && (x < width) && ((size_t) pix < (size_t) beyond);
					j++, x++, pix++) {
				*pix = c1;
				written++;
			}
		} else {
			c = getc(INPUT);
			if (c == EOF)
				return false;
			if (c == 0x00) { /* EOL */
				x = 0;
				++y;
				pix = pData + y * stride + x;
			} else if (c == 0x01) { /* EOP */
				break;
			} else if (c == 0x02) { /* MOVE by dxdy */
				c = getc(INPUT);
				if (c == EOF)
					return false;
				x += (uint32_t) c;
				c = getc(INPUT);
				if (c == EOF)
					return false;
				y += (uint32_t) c;
				pix = pData + y * stride + x;
			} else { /* 03 .. 255 */
				int j;
				for (j = 0;
						(j < c) && (x < width)
								&& ((size_t) pix < (size_t) beyond);
						j++, x++, pix++) {
					int temp = getc(INPUT);
					if (temp == EOF)
						return false;
					uint8_t c1 = (uint8_t) temp;
					*pix = c1;
					written++;
				}
				if ((uint32_t) c & 1U) { /* skip padding byte */
					if (getc(INPUT) == EOF)
						return false;
				}
			}
		}
	}/* while() */
	if (written != width * height) {
		spdlog::error(
				"Number of pixels written does not match specified image dimensions.");
		return false;
	}
	return true;
}
static bool bmp_read_rle4_data(FILE *INPUT, uint8_t *pData, uint32_t stride,
		uint32_t width, uint32_t height) {
	uint32_t x, y;
	uint8_t *pix;
	const uint8_t *beyond;

	beyond = pData + stride * height;
	pix = pData;
	x = y = 0U;
	while (y < height) {
		int c = getc(INPUT);
		if (c == EOF)
			return false;

		if (c) {/* encoded mode */
			int j;
			int temp = getc(INPUT);
			if (temp == EOF)
				return false;
			uint8_t c1 = (uint8_t) temp;

			for (j = 0;
					(j < c) && (x < width) && ((size_t) pix < (size_t) beyond);
					j++, x++, pix++) {
				*pix = (uint8_t) ((j & 1) ? (c1 & 0x0fU) : ((c1 >> 4) & 0x0fU));
			}
		} else { /* absolute mode */
			c = getc(INPUT);
			if (c == EOF)
				break;

			if (c == 0x00) { /* EOL */
				x = 0;
				y++;
				pix = pData + y * stride;
			} else if (c == 0x01) { /* EOP */
				break;
			} else if (c == 0x02) { /* MOVE by dxdy */
				int temp = getc(INPUT);
				if (temp == EOF)
					return false;
				c = (uint8_t) temp;
				x += (uint32_t) c;

				temp = getc(INPUT);
				if (temp == EOF)
					return false;
				c = (uint8_t) temp;
				y += (uint32_t) c;
				pix = pData + y * stride + x;
			} else { /* 03 .. 255 : absolute mode */
				int j;
				uint8_t c1 = 0U;

				for (j = 0;
						(j < c) && (x < width)
								&& ((size_t) pix < (size_t) beyond);
						j++, x++, pix++) {
					if ((j & 1) == 0) {
						int temp = getc(INPUT);
						if (temp == EOF)
							return false;
						c1 = (uint8_t) temp;
					}
					*pix = (uint8_t) (
							(j & 1) ? (c1 & 0x0fU) : ((c1 >> 4) & 0x0fU));
				}
				if (((c & 3) == 1) || ((c & 3) == 2)) { /* skip padding byte */
					if (getc(INPUT) == EOF)
						return false;
				}
			}
		}
	} /* while(y < height) */
	return true;
}
static grk_image* bmptoimage(const char *filename,
		grk_cparameters *parameters) {
	bool readFromStdin = grk::useStdio(filename);
	grk_image_cmptparm cmptparm[4]; /* maximum of 4 components */
	uint8_t lut_R[256], lut_G[256], lut_B[256];
	uint8_t const *pLUT[3];
	grk_image *image = nullptr;
	FILE *INPUT = nullptr;
	GRK_BITMAPFILEHEADER File_h;
	GRK_BITMAPINFOHEADER Info_h;
	uint32_t palette_len, numcmpts = 1U;
	bool l_result = false;
	uint8_t *pData = nullptr;
	uint32_t bmpStride;
	pLUT[0] = lut_R;
	pLUT[1] = lut_G;
	pLUT[2] = lut_B;
	bool handled = true;
	bool topDown = false;

	if (readFromStdin) {
		if (!grk::grk_set_binary_mode(stdin))
			return nullptr;
		INPUT = stdin;
	} else {
		INPUT = fopen(filename, "rb");
		if (!INPUT) {
			spdlog::error("Failed to open {} for reading", filename);
			return nullptr;
		}
	}

	if (!bmp_read_file_header(INPUT, &File_h))
		goto cleanup;
	if (!bmp_read_info_header(INPUT, &File_h, &Info_h))
		goto cleanup;
	if (Info_h.biSize == 12){
		spdlog::error("OS2 file header not supported");
		goto cleanup;
	}
	if (Info_h.biHeight < 0){
		topDown = true;
		Info_h.biHeight = -Info_h.biHeight;
	}
	/* Load palette */
	if (Info_h.biBitCount <= 8U) {
		memset(lut_R, 0, sizeof(lut_R));
		memset(lut_G, 0, sizeof(lut_G));
		memset(lut_B, 0, sizeof(lut_B));

		palette_len = Info_h.biClrUsed;
		if ((palette_len == 0U) && (Info_h.biBitCount <= 8U)) {
			palette_len = (1U << Info_h.biBitCount);
		}
		if (palette_len > 256U) {
			palette_len = 256U;
		}
		if (palette_len > 0U) {
			uint8_t has_color = 0U;
			for (uint32_t i = 0U; i < palette_len; i++) {
				int temp = getc(INPUT);
				if (temp == EOF)
					goto cleanup;
				lut_B[i] = (uint8_t) temp;
				temp = getc(INPUT);
				if (temp == EOF)
					goto cleanup;
				lut_G[i] = (uint8_t) temp;
				temp = getc(INPUT);
				if (temp == EOF)
					goto cleanup;
				lut_R[i] = (uint8_t) temp;
				temp = getc(INPUT); /* padding */
				if (temp == EOF)
					goto cleanup;
				has_color |= (lut_B[i] ^ lut_G[i]) | (lut_G[i] ^ lut_R[i]);
			}
			if (has_color) {
				numcmpts = 3U;
			}
		}
	} else {
		numcmpts = 3U;
		if ((Info_h.biCompression == 3) && (Info_h.biAlphaMask != 0U)) {
			numcmpts++;
		}
	}

	if (Info_h.biWidth == 0 || Info_h.biHeight == 0)
		goto cleanup;
	if (Info_h.biBitCount > (((uint32_t) -1) - 31) / Info_h.biWidth)
		goto cleanup;

	bmpStride = ((Info_h.biWidth * Info_h.biBitCount + 31U) / 32U) * sizeof(uint32_t); /* rows are aligned on 32bits */
	if (Info_h.biBitCount == 4 && Info_h.biCompression == 2) { /* RLE 4 gets decoded as 8 bits data for now... */
		if (8 > (((uint32_t) -1) - 31) / Info_h.biWidth)
			goto cleanup;
		bmpStride = ((Info_h.biWidth * 8U + 31U) / 32U) * sizeof(uint32_t);
	}

	if (bmpStride > ((uint32_t) -1) / sizeof(uint8_t) / Info_h.biHeight)
		goto cleanup;
	pData = (uint8_t*) calloc(1, bmpStride * Info_h.biHeight * sizeof(uint8_t));
	if (pData == nullptr)
		goto cleanup;
	/* Place the cursor at the beginning of the image information */
	if (fseek(INPUT, 0, SEEK_SET))
		goto cleanup;
	if (fseek(INPUT, (long) File_h.bfOffBits, SEEK_SET))
		goto cleanup;

	switch (Info_h.biCompression) {
	case 0:
	case 3:
		/* read raw data */
		l_result = bmp_read_raw_data(INPUT, pData, bmpStride, Info_h.biWidth,
				Info_h.biHeight);
		break;
	case 1:
		/* read rle8 data */
		l_result = bmp_read_rle8_data(INPUT, pData, bmpStride, Info_h.biWidth,
				Info_h.biHeight);
		break;
	case 2:
		/* read rle4 data */
		l_result = bmp_read_rle4_data(INPUT, pData, bmpStride, Info_h.biWidth,
				Info_h.biHeight);
		break;
	default:
		spdlog::error("Unsupported BMP compression");
		l_result = false;
		break;
	}
	if (!l_result) {
		goto cleanup;
	}

	/* create the image */
	memset(&cmptparm[0], 0, sizeof(cmptparm));
	for (uint32_t i = 0; i < 4U; i++) {
		auto img_comp = cmptparm + i;
		img_comp->prec = 8;
		img_comp->sgnd = false;
		img_comp->dx = parameters->subsampling_dx;
		img_comp->dy = parameters->subsampling_dy;
		img_comp->w = grk::ceildiv<uint32_t>(Info_h.biWidth, img_comp->dx);
		img_comp->h = grk::ceildiv<uint32_t>(Info_h.biHeight, img_comp->dy);
	}

	image = grk_image_create(numcmpts, &cmptparm[0],
			(numcmpts == 1U) ? GRK_CLRSPC_GRAY : GRK_CLRSPC_SRGB,true);
	if (!image) {
		goto cleanup;
	}
	// ICC profile
	if (Info_h.biSize == sizeof(GRK_BITMAPINFOHEADER)
			&& Info_h.biColorSpaceType == BMP_ICC_PROFILE_EMBEDDED
			&& Info_h.biIccProfileSize
			&& Info_h.biIccProfileSize < grk::maxICCProfileBufferLen) {

		//read in ICC profile
		if (fseek(INPUT, fileHeaderSize + Info_h.biIccProfileData,
				SEEK_SET)) {
			goto cleanup;
		}
		//allocate buffer
		image->icc_profile_buf = new uint8_t[Info_h.biIccProfileSize];
		size_t bytesRead = fread(image->icc_profile_buf, 1,
				Info_h.biIccProfileSize, INPUT);
		if (bytesRead != Info_h.biIccProfileSize) {
			delete[] image->icc_profile_buf;
			image->icc_profile_buf = nullptr;
			goto cleanup;
		}
		image->icc_profile_len = Info_h.biIccProfileSize;
		image->color_space = GRK_CLRSPC_ICC;
	}
	if (numcmpts == 4U) {
		image->comps[3].type = GRK_COMPONENT_TYPE_OPACITY;
	    image->comps[3].association = GRK_COMPONENT_ASSOC_WHOLE_IMAGE;
	}

	/* set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->y0 = parameters->image_offset_y0;
	image->x1 = image->x0 + (Info_h.biWidth - 1U) * parameters->subsampling_dx
			+ 1U;
	image->y1 = image->y0 + (Info_h.biHeight - 1U) * parameters->subsampling_dy
			+ 1U;

	/* Read the data */
	switch(Info_h.biCompression){
	case 0:
		switch (Info_h.biBitCount) {
			case 32:		/* RGBX */
				bmpmask32toimage(pData, bmpStride, image, 0x00FF0000U, 0x0000FF00U,
						0x000000FFU, 0x00000000U);
				break;
			case 24:  	/*RGB */
				bmp24toimage(pData, bmpStride, image);
				break;
			case 16:	/*RGBX */
				bmpmask16toimage(pData, bmpStride, image, 0x7C00U, 0x03E0U, 0x001FU,
						0x0000U);
				break;
			case 8: 	/* RGB 8bpp Indexed */
				bmp8toimage(pData, bmpStride, image, pLUT, topDown);
				break;
			case 4:    /* RGB 4bpp Indexed */
				bmp4toimage(pData, bmpStride, image, pLUT);
				break;
			case 1: 	/* Grayscale 1bpp Indexed */
				bmp1toimage(pData, bmpStride, image, pLUT);
				break;
			default:
				handled = false;
				break;
		}
		break;
		case 1:
			switch (Info_h.biBitCount) {
				case 8:		/*RLE8*/
					bmp8toimage(pData, bmpStride, image, pLUT, topDown);
					break;
				default:
					handled = false;
					break;
			}
		break;
	case 2:
		switch (Info_h.biBitCount) {
			case 4:		 /*RLE4*/
				bmp8toimage(pData, bmpStride, image, pLUT, topDown); /* RLE 4 gets decoded as 8 bits data for now */
				break;
			default:
				handled = false;
				break;
		}
		break;
	case 3:
		switch (Info_h.biBitCount) {
			case 32: /* BITFIELDS bit mask */
				if (Info_h.biRedMask && Info_h.biGreenMask && 	Info_h.biBlueMask) {
					bool fail = false;
					bool hasAlpha = image->numcomps > 3;
					// sanity check on bit masks
					uint32_t m[4] = {Info_h.biRedMask,
										Info_h.biGreenMask,
											Info_h.biBlueMask,
												Info_h.biAlphaMask};
					for (uint32_t i = 0; i < image->numcomps; ++i){
						int lead = grk::count_leading_zeros(m[i]);
						int trail = grk::count_trailing_zeros(m[i]);
						int cnt = grk::population_count(m[i]);
						// check contiguous
						if (lead + trail + cnt != 32){
							spdlog::error("RGB(A) bit masks must be contiguous");
							fail = true;
							break;
						}
						// check supported precision
						if (cnt > 16){
							spdlog::error("RGB(A) bit mask with precision ({0:d}) greater than 16 is not supported",cnt);
							fail = true;
						}
					}
					// check overlap
					if ( (m[0]&m[1]) || (m[0]&m[2]) || 	(m[1]&m[2]) )	{
						spdlog::error("RGB(A) bit masks must not overlap");
						fail = true;
					}
					if (hasAlpha && !fail){
						if ( (m[0]&m[3]) || (m[1]&m[3]) || (m[2]&m[3]) )	{
							spdlog::error("RGB(A) bit masks must not overlap");
							fail = true;
						}
					}
					if (fail){
						spdlog::error("RGB(A) bit masks:\n"
								"{0:b}\n"
								"{0:b}\n"
								"{0:b}\n"
								"{0:b}",m[0],m[1],m[2],m[3]);
						grk_image_destroy(image);
						image = nullptr;
						goto cleanup;
					}
				}else {
					spdlog::error("RGB(A) bit masks must be non-zero");
					handled = false;
					break;
				}

				bmpmask32toimage(pData, bmpStride, image, Info_h.biRedMask,
						Info_h.biGreenMask, Info_h.biBlueMask, Info_h.biAlphaMask);
				break;
			case 16:  /* BITFIELDS bit mask*/
				if ((Info_h.biRedMask == 0U) && (Info_h.biGreenMask == 0U)
						&& (Info_h.biBlueMask == 0U)) {
					Info_h.biRedMask = 0xF800U;
					Info_h.biGreenMask = 0x07E0U;
					Info_h.biBlueMask = 0x001FU;
				}
				bmpmask16toimage(pData, bmpStride, image, Info_h.biRedMask,
						Info_h.biGreenMask, Info_h.biBlueMask, Info_h.biAlphaMask);
				break;
			default:
				handled = false;
				break;
		}
		break;

	default:
		handled = false;
		break;
	}
	if (!handled){
		grk_image_destroy(image);
		image = nullptr;
		spdlog::error(
				"Precision [{}] does not match supported precision: "
				"24 bit RGB, 8 bit RGB, 4/8 bit RLE and 16/32 bit BITFIELD",
				Info_h.biBitCount);
	}
	cleanup:
		free(pData);
		if (!readFromStdin && INPUT) {
			if (!grk::safe_fclose(INPUT)) {
				grk_image_destroy(image);
				image = nullptr;
			}
		}
	return image;
}

uint32_t BMPFormat::getPaddedWidth(){
	assert(m_image);
	return ((m_image->numcomps *  m_image->comps[0].w + 3) >> 2) << 2;
}


bool BMPFormat::encode() {
	const char *outfile = m_fileName.c_str();
	m_writeToStdout = grk::useStdio(outfile);
	int ret = -1;
	uint32_t w = m_image->comps[0].w;
	uint32_t h = m_image->comps[0].h;
	uint32_t padW = getPaddedWidth();
	uint32_t image_size = padW * h;
	uint32_t colours_used, lut_size;
	uint32_t full_header_size, info_header_size, icc_size=0;

	if (!grk::all_components_sanity_check(m_image,false))
		goto cleanup;
	if (m_image->numcomps != 1 &&
			(m_image->numcomps != 3 && m_image->numcomps != 4) ) {
		spdlog::error("Unsupported number of components: {}",
				m_image->numcomps);
		goto cleanup;
	}
	if (grk::isSubsampled(m_image)) {
		spdlog::error("Sub-sampled images not supported");
		goto cleanup;
	}
	for (uint32_t i = 0; i < m_image->numcomps; ++i) {
		if (m_image->comps[i].prec == 0) {
			spdlog::error("Unsupported precision: 0 for component {}",i);
			goto cleanup;
		}
	}
	if (!grk::grk_open_for_output(&m_file, outfile,m_writeToStdout))
		goto cleanup;
	colours_used = (m_image->numcomps == 3) ? 0 : 256 ;
	lut_size = colours_used * sizeof(uint32_t) ;
	full_header_size = fileHeaderSize + BITMAPINFOHEADER_LENGTH;
	if (m_image->icc_profile_buf){
		full_header_size = fileHeaderSize + sizeof(GRK_BITMAPINFOHEADER);
		icc_size = m_image->icc_profile_len;
	}
	info_header_size = full_header_size - fileHeaderSize;

	if (fprintf(m_file, "BM") != 2)
		goto cleanup;

	/* FILE HEADER */
	// total size
	if (!put_int(m_file, full_header_size + lut_size + image_size + icc_size))
		goto cleanup;
	// reserved
	if (!put_int(m_file, 0U))
		goto cleanup;
	if (!put_int(m_file, full_header_size + lut_size))
		goto cleanup;

	/* INFO HEADER   */
	if (!put_int(m_file, info_header_size))
		goto cleanup;
	if (!put_int(m_file, w))
		goto cleanup;
	if (!put_int(m_file, h))
		goto cleanup;
	if (!put_int(m_file, (uint16_t)1))
		goto cleanup;
	if (!put_int(m_file, (uint16_t)(m_image->numcomps * 8)))
		goto cleanup;
	if (!put_int(m_file, 0U))
		goto cleanup;
	if (!put_int(m_file, image_size) )
		goto cleanup;
	for (uint32_t i = 0; i < 2; ++i){
		double cap = m_image->capture_resolution[i] ?
				m_image->capture_resolution[i] : 7834;
		if (!put_int(m_file, (uint32_t)(cap + 0.5f)))
			goto cleanup;
	}
	if (!put_int(m_file, colours_used))
		goto cleanup;
	if (!put_int(m_file, colours_used))
		goto cleanup;

	if (m_image->icc_profile_buf){
		if (!put_int(m_file, 0U))
			goto cleanup;
		if (!put_int(m_file, 0U))
			goto cleanup;
		if (!put_int(m_file, 0U))
			goto cleanup;
		if (!put_int(m_file, 0U))
			goto cleanup;
		if (!put_int(m_file, BMP_ICC_PROFILE_EMBEDDED))
			goto cleanup;
		uint8_t temp[36];
		memset(temp, 0, sizeof(temp));
		if (fwrite(temp, 1, 36, m_file) != 36 )
			goto cleanup;
		if (!put_int(m_file, 0U))
			goto cleanup;
		if (!put_int(m_file, 0U))
			goto cleanup;
		if (!put_int(m_file, 0U))
			goto cleanup;

		if (!put_int(m_file, 0U))
			goto cleanup;
		if (!put_int(m_file, info_header_size +  lut_size + image_size))
			goto cleanup;
		if (!put_int(m_file, m_image->icc_profile_len))
			goto cleanup;
		if (!put_int(m_file, 0U))
			goto cleanup;
	}

	if (!encodeStrip(0))
		goto cleanup;

	ret = 0;
cleanup:
	if (!encodeFinish())
		return false;

	return (ret ? false : true);
}

BMPFormat::BMPFormat(void) : m_destBuff(nullptr),
							m_destIndex(0),
							m_writeToStdout(false)
{}

bool BMPFormat::encodeHeader(grk_image *  image, const std::string &filename, uint32_t compressionParam){
	(void) compressionParam;
	m_fileName = filename;
	m_image = image;
	return encode();
}



bool BMPFormat::encodeStrip(uint32_t rows){
	bool ret = false;
	auto w = m_image->comps[0].w;
	auto h = m_image->comps[0].h;
	auto stride = m_image->comps[0].stride;
	m_destIndex = stride * (h - 1);
	uint32_t padW = getPaddedWidth();
	uint32_t strideDiff = 4 - (m_image->numcomps * w) % 4;
	if (strideDiff == 4)
		strideDiff = 0;
	//auto chunkRows = (512 * 1024)/ (w * m_image->numcomps);
	//if (chunkRows > h)
	//	chunkRows = h;
	//auto numChunkRows = h / chunkRows;

	uint32_t j = 0;
	int trunc[4] = {0,0,0,0};
	float scale[4] = {1.0f, 1.0f, 1.0f, 1.0f};

	for (uint32_t compno = 0; compno < m_image->numcomps; ++compno){
		if (m_image->comps[compno].prec > 8) {
				trunc[compno] = (int) m_image->comps[compno].prec - 8;
				spdlog::warn("BMP conversion: truncating component {} from {} bits to 8 bits",
						compno, m_image->comps[compno].prec);
		} else if (m_image->comps[0].prec < 8) {
			scale[compno]= 255.0f/(1U << m_image->comps[compno].prec);
			spdlog::warn("BMP conversion: scaling component {} from {} bits to 8 bits",
					compno, m_image->comps[compno].prec);
		}
	}

	// 1024-byte LUT
	if (m_image->numcomps ==1) {
		for (uint32_t i = 0; i < 256; i++) {
			if (fprintf(m_file, "%c%c%c%c", i, i, i, 0) != 4)
				goto cleanup;
		}
	}
	m_destBuff = new uint8_t[padW];
	while ( j < h) {
		uint64_t destInd = 0;
		for (uint32_t i = 0; i < w; i++) {
			uint8_t rc[4];
			for (uint32_t compno = 0; compno < m_image->numcomps; ++compno){
				int32_t r = m_image->comps[compno].data[m_destIndex + i];
				r += (m_image->comps[compno].sgnd ?
								1 << (m_image->comps[compno].prec - 1) : 0);
				if (trunc[compno] != 0)
					r = ((r >> trunc[compno]) + ((r >> (trunc[compno] - 1)) % 2));
				else if (scale[compno] != 1.0f)
					r = (int32_t)(((float)r * scale[compno]) + 0.5f);
				if (r > 255)
					r = 255;
				else if (r < 0)
					r = 0;
				rc[compno] = (uint8_t)r;
			}
			if (m_image->numcomps == 1) {
					m_destBuff[destInd++] = rc[0];
			} else {
				m_destBuff[destInd++] = rc[2];
				m_destBuff[destInd++] = rc[1];
				m_destBuff[destInd++] = rc[0];
				if (m_image->numcomps == 4)
					m_destBuff[destInd++] = rc[3];
			}
		}
		// zero out padding at end of line
		for (uint32_t pad = 0; pad < strideDiff; pad++)
			m_destBuff[destInd++] = 0;
		m_destIndex -= stride;
		if (fwrite(m_destBuff, 1, destInd, m_file) != destInd)
			goto cleanup;
		j++;
	}

	if (m_image->icc_profile_buf) {
		if (fwrite(m_image->icc_profile_buf,
				1, m_image->icc_profile_len, m_file) != m_image->icc_profile_len )
			goto cleanup;
	}

	ret = true;
cleanup:

	return ret;
}
bool BMPFormat::encodeFinish(void){
	delete[] m_destBuff;
	if (!m_writeToStdout && m_file) {
		if (!grk::safe_fclose(m_file))
			return false;
	}
	return true;
}

grk_image *  BMPFormat::decode(const std::string &filename,  grk_cparameters  *parameters){
	return bmptoimage(filename.c_str(), parameters);
}
