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
	uint16_t bfType; /* 'BM' for Bitmap (19776) */
	uint32_t bfSize; /* Size of the file        */
	uint16_t bfReserved1; /* Reserved : 0            */
	uint16_t bfReserved2; /* Reserved : 0            */
	uint32_t bfOffBits; /* Offset                  */
} GRK_BITMAPFILEHEADER;

typedef struct {
	uint32_t biSize; /* Size of the structure in bytes */
	uint32_t biWidth; /* Width of the image in pixels */
	uint32_t biHeight; /* Height of the image in pixels */
	uint16_t biPlanes; /* 1 */
	uint16_t biBitCount; /* Number of color bits by pixels */
	uint32_t biCompression; /* Type of encoding 0: none 1: RLE8 2: RLE4 */
	uint32_t biSizeImage; /* Size of the image in bytes */
	uint32_t biXpelsPerMeter; /* Horizontal (X) resolution in pixels/meter */
	uint32_t biYpelsPerMeter; /* Vertical (Y) resolution in pixels/meter */
	uint32_t biClrUsed; /* Number of color used in the image (0: ALL) */
	uint32_t biClrImportant; /* Number of important color (0: ALL) */
	uint32_t biRedMask; /* Red channel bit mask */
	uint32_t biGreenMask; /* Green channel bit mask */
	uint32_t biBlueMask; /* Blue channel bit mask */
	uint32_t biAlphaMask; /* Alpha channel bit mask */
	uint32_t biColorSpaceType; /* Color space type */
	uint8_t biColorSpaceEP[36]; /* Color space end points */
	uint32_t biRedGamma; /* Red channel gamma */
	uint32_t biGreenGamma; /* Green channel gamma */
	uint32_t biBlueGamma; /* Blue channel gamma */
	uint32_t biIntent; /* Intent */
	uint32_t biIccProfileData; /* ICC profile data */
	uint32_t biIccProfileSize; /* ICC profile size */
	uint32_t biReserved; /* Reserved */
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

static void grk_applyLUT8u_8u32s_C1R(uint8_t const *pSrc, int32_t srcStride,
		int32_t *pDst, int32_t dstStride, uint8_t const *pLUT, uint32_t width,
		uint32_t height) {
	for (uint32_t y = height; y != 0U; --y) {
		uint32_t x;
		for (x = 0; x < width; x++) {
			pDst[x] = (int32_t) pLUT[pSrc[x]];
		}
		pSrc += srcStride;
		pDst += dstStride;
	}
}

static void grk_applyLUT8u_8u32s_C1P3R(uint8_t const *pSrc, int32_t srcStride,
		int32_t *const*pDst, int32_t const *pDstStride,
		uint8_t const *const*pLUT, uint32_t width, uint32_t height) {
	uint32_t y;
	int32_t *pR = pDst[0];
	int32_t *pG = pDst[1];
	int32_t *pB = pDst[2];
	uint8_t const *pLUT_R = pLUT[0];
	uint8_t const *pLUT_G = pLUT[1];
	uint8_t const *pLUT_B = pLUT[2];

	for (y = height; y != 0U; --y) {
		for (uint32_t x = 0; x < width; x++) {
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
static void bmp24toimage(const uint8_t *pData, uint32_t stride,
		grk_image *image) {
	int index;
	uint32_t width, height;
	const uint8_t *pSrc = nullptr;
	width = image->comps[0].w;
	height = image->comps[0].h;
	index = 0;
	pSrc = pData + (height - 1U) * stride;
	for (uint32_t y = 0; y < height; y++) {
		size_t src_index = 0;
		for (uint32_t x = 0; x < width; x++) {
			image->comps[0].data[index] = (int32_t) pSrc[src_index + 2]; /* R */
			image->comps[1].data[index] = (int32_t) pSrc[src_index + 1]; /* G */
			image->comps[2].data[index] = (int32_t) pSrc[src_index]; /* B */
			index++;
			src_index += 3;
		}
		pSrc -= stride;
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
static void bmpmask32toimage(const uint8_t *pData, uint32_t stride,
		grk_image *image, uint32_t redMask, uint32_t greenMask,
		uint32_t blueMask, uint32_t alphaMask) {
	int index;
	uint32_t width, height;
	uint32_t x, y;
	const uint8_t *pSrc = nullptr;
	bool hasAlpha = false;
	uint32_t redShift, redPrec;
	uint32_t greenShift, greenPrec;
	uint32_t blueShift, bluePrec;
	uint32_t alphaShift, alphaPrec;

	width = image->comps[0].w;
	height = image->comps[0].h;
	hasAlpha = image->numcomps > 3U;
	bmp_mask_get_shift_and_prec(redMask, &redShift, &redPrec);
	bmp_mask_get_shift_and_prec(greenMask, &greenShift, &greenPrec);
	bmp_mask_get_shift_and_prec(blueMask, &blueShift, &bluePrec);
	bmp_mask_get_shift_and_prec(alphaMask, &alphaShift, &alphaPrec);
	image->comps[0].prec = redPrec;
	image->comps[1].prec = greenPrec;
	image->comps[2].prec = bluePrec;
	if (hasAlpha) {
		image->comps[3].prec = alphaPrec;
	}
	index = 0;
	pSrc = pData + (height - 1U) * stride;
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
		pSrc -= stride;
	}
}
static void bmpmask16toimage(const uint8_t *pData, uint32_t stride,
		grk_image *image, uint32_t redMask, uint32_t greenMask,
		uint32_t blueMask, uint32_t alphaMask) {
	int index;
	uint32_t width, height;
	uint32_t x, y;
	const uint8_t *pSrc = nullptr;
	bool hasAlpha = false;
	uint32_t redShift, redPrec;
	uint32_t greenShift, greenPrec;
	uint32_t blueShift, bluePrec;
	uint32_t alphaShift, alphaPrec;

	width = image->comps[0].w;
	height = image->comps[0].h;
	hasAlpha = image->numcomps > 3U;
	bmp_mask_get_shift_and_prec(redMask, &redShift, &redPrec);
	bmp_mask_get_shift_and_prec(greenMask, &greenShift, &greenPrec);
	bmp_mask_get_shift_and_prec(blueMask, &blueShift, &bluePrec);
	bmp_mask_get_shift_and_prec(alphaMask, &alphaShift, &alphaPrec);
	image->comps[0].prec = redPrec;
	image->comps[1].prec = greenPrec;
	image->comps[2].prec = bluePrec;
	if (hasAlpha) {
		image->comps[3].prec = alphaPrec;
	}
	index = 0;
	pSrc = pData + (height - 1U) * stride;
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
		pSrc -= stride;
	}
}
static grk_image* bmp8toimage(const uint8_t *pData, uint32_t stride,
		grk_image *image, uint8_t const *const*pLUT) {
	uint32_t width, height;
	const uint8_t *pSrc = nullptr;

	width = image->comps[0].w;
	height = image->comps[0].h;
	pSrc = pData + (height - 1U) * stride;
	if (image->numcomps == 1U) {
		grk_applyLUT8u_8u32s_C1R(pSrc, -(int32_t) stride, image->comps[0].data,
				(int32_t) width, pLUT[0], width, height);
	} else {
		int32_t *pDst[3];
		int32_t pDstStride[3];

		pDst[0] = image->comps[0].data;
		pDst[1] = image->comps[1].data;
		pDst[2] = image->comps[2].data;
		pDstStride[0] = (int32_t) width;
		pDstStride[1] = (int32_t) width;
		pDstStride[2] = (int32_t) width;
		grk_applyLUT8u_8u32s_C1P3R(pSrc, -(int32_t) stride, pDst, pDstStride,
				pLUT, width, height);
	}
	return image;
}
static bool bmp_read_file_header(FILE *INPUT, GRK_BITMAPFILEHEADER *header) {
	if (!get_int(INPUT, &header->bfType))
		return false;
	if (header->bfType != 19778) {
		spdlog::error("not a BMP file!");
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
static bool bmp_read_info_header(FILE *INPUT, GRK_BITMAPINFOHEADER *header) {
	memset(header, 0, sizeof(*header));
	/* INFO HEADER */
	/* ------------- */
	if (!get_int(INPUT, &header->biSize))
		return false;

	switch (header->biSize) {
	case 12U: /* BITMAPCOREHEADER */
	case 40U: /* BITMAPINFOHEADER */
	case 52U: /* BITMAPV2INFOHEADER */
	case 56U: /* BITMAPV3INFOHEADER */
	case 108U: /* BITMAPV4HEADER */
	case 124U: /* BITMAPV5HEADER */
		break;
	default:
		spdlog::error("unknown BMP header size {}", header->biSize);
		return false;
	}
	if (!get_int(INPUT, &header->biWidth))
		return false;
	if (!get_int(INPUT, &header->biHeight))
		return false;
	if (!get_int(INPUT, &header->biPlanes))
		return false;
	if (!get_int(INPUT, &header->biBitCount))
		return false;
	if (header->biSize >= 40U) {
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
	}
	if (header->biSize >= 56U) {
		if (!get_int(INPUT, &header->biRedMask))
			return false;
		if (!get_int(INPUT, &header->biGreenMask))
			return false;
		if (!get_int(INPUT, &header->biBlueMask))
			return false;
		if (!get_int(INPUT, &header->biAlphaMask))
			return false;
	}
	if (header->biSize >= 108U) {
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
	if (header->biSize >= 124U) {
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
		spdlog::error(
				"fread return a number of element different from the expected.");
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
	uint32_t i, palette_len, numcmpts = 1U;
	bool l_result = false;
	uint8_t *pData = nullptr;
	uint32_t stride;
	long beginningOfInfoHeader = -1;
	pLUT[0] = lut_R;
	pLUT[1] = lut_G;
	pLUT[2] = lut_B;

	if (readFromStdin) {
		if (!grk::grok_set_binary_mode(stdin))
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
	//cache location of beginning of info header
	beginningOfInfoHeader = ftell(INPUT);
	if (beginningOfInfoHeader == -1)
		goto cleanup;
	if (!bmp_read_info_header(INPUT, &Info_h))
		goto cleanup;
	/* Load palette */
	if (Info_h.biBitCount <= 8U) {
		memset(&lut_R[0], 0, sizeof(lut_R));
		memset(&lut_G[0], 0, sizeof(lut_G));
		memset(&lut_B[0], 0, sizeof(lut_B));

		palette_len = Info_h.biClrUsed;
		if ((palette_len == 0U) && (Info_h.biBitCount <= 8U)) {
			palette_len = (1U << Info_h.biBitCount);
		}
		if (palette_len > 256U) {
			palette_len = 256U;
		}
		if (palette_len > 0U) {
			uint8_t has_color = 0U;
			for (i = 0U; i < palette_len; i++) {
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

	stride = ((Info_h.biWidth * Info_h.biBitCount + 31U) / 32U) * 4U; /* rows are aligned on 32bits */
	if (Info_h.biBitCount == 4 && Info_h.biCompression == 2) { /* RLE 4 gets decoded as 8 bits data for now... */
		if (8 > (((uint32_t) -1) - 31) / Info_h.biWidth)
			goto cleanup;
		stride = ((Info_h.biWidth * 8U + 31U) / 32U) * 4U;
	}

	if (stride > ((uint32_t) -1) / sizeof(uint8_t) / Info_h.biHeight)
		goto cleanup;
	pData = (uint8_t*) calloc(1, stride * Info_h.biHeight * sizeof(uint8_t));
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
		l_result = bmp_read_raw_data(INPUT, pData, stride, Info_h.biWidth,
				Info_h.biHeight);
		break;
	case 1:
		/* read rle8 data */
		l_result = bmp_read_rle8_data(INPUT, pData, stride, Info_h.biWidth,
				Info_h.biHeight);
		break;
	case 2:
		/* read rle4 data */
		l_result = bmp_read_rle4_data(INPUT, pData, stride, Info_h.biWidth,
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
	for (i = 0; i < 4U; i++) {
		cmptparm[i].prec = 8;
		cmptparm[i].sgnd = 0;
		cmptparm[i].dx = parameters->subsampling_dx;
		cmptparm[i].dy = parameters->subsampling_dy;
		cmptparm[i].w = Info_h.biWidth;
		cmptparm[i].h = Info_h.biHeight;
	}

	image = grk_image_create(numcmpts, &cmptparm[0],
			(numcmpts == 1U) ? GRK_CLRSPC_GRAY : GRK_CLRSPC_SRGB);
	if (!image) {
		goto cleanup;
	}
	// ICC profile
	if (Info_h.biSize == sizeof(GRK_BITMAPINFOHEADER)
			&& Info_h.biColorSpaceType == BMP_ICC_PROFILE_EMBEDDED
			&& Info_h.biIccProfileSize
			&& Info_h.biIccProfileSize < grk::maxICCProfileBufferLen) {

		//read in ICC profile
		if (fseek(INPUT, beginningOfInfoHeader + Info_h.biIccProfileData,
				SEEK_SET)) {
			goto cleanup;
		}
		//allocate buffer
		image->icc_profile_buf = grk_buffer_new(Info_h.biIccProfileSize);
		size_t bytesRead = fread(image->icc_profile_buf, 1,
				Info_h.biIccProfileSize, INPUT);
		if (bytesRead != Info_h.biIccProfileSize) {
			grk_buffer_delete(image->icc_profile_buf);
			image->icc_profile_buf = nullptr;
			goto cleanup;
		}
		image->icc_profile_len = Info_h.biIccProfileSize;
		image->color_space = GRK_CLRSPC_ICC;
	}
	if (numcmpts == 4U) {
		image->comps[3].alpha = 1;
	}

	/* set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->y0 = parameters->image_offset_y0;
	image->x1 = image->x0 + (Info_h.biWidth - 1U) * parameters->subsampling_dx
			+ 1U;
	image->y1 = image->y0 + (Info_h.biHeight - 1U) * parameters->subsampling_dy
			+ 1U;

	/* Read the data */
	if (Info_h.biBitCount == 24 && Info_h.biCompression == 0) { /*RGB */
		bmp24toimage(pData, stride, image);
	} else if (Info_h.biBitCount == 8 && Info_h.biCompression == 0) { /* RGB 8bpp Indexed */
		bmp8toimage(pData, stride, image, pLUT);
	} else if (Info_h.biBitCount == 8 && Info_h.biCompression == 1) { /*RLE8*/
		bmp8toimage(pData, stride, image, pLUT);
	} else if (Info_h.biBitCount == 4 && Info_h.biCompression == 2) { /*RLE4*/
		bmp8toimage(pData, stride, image, pLUT); /* RLE 4 gets decoded as 8 bits data for now */
	} else if (Info_h.biBitCount == 32 && Info_h.biCompression == 0) { /* RGBX */
		bmpmask32toimage(pData, stride, image, 0x00FF0000U, 0x0000FF00U,
				0x000000FFU, 0x00000000U);
	} else if (Info_h.biBitCount == 32 && Info_h.biCompression == 3) { /* bitmask */
		bmpmask32toimage(pData, stride, image, Info_h.biRedMask,
				Info_h.biGreenMask, Info_h.biBlueMask, Info_h.biAlphaMask);
	} else if (Info_h.biBitCount == 16 && Info_h.biCompression == 0) { /* RGBX */
		bmpmask16toimage(pData, stride, image, 0x7C00U, 0x03E0U, 0x001FU,
				0x0000U);
	} else if (Info_h.biBitCount == 16 && Info_h.biCompression == 3) { /* bitmask */
		if ((Info_h.biRedMask == 0U) && (Info_h.biGreenMask == 0U)
				&& (Info_h.biBlueMask == 0U)) {
			Info_h.biRedMask = 0xF800U;
			Info_h.biGreenMask = 0x07E0U;
			Info_h.biBlueMask = 0x001FU;
		}
		bmpmask16toimage(pData, stride, image, Info_h.biRedMask,
				Info_h.biGreenMask, Info_h.biBlueMask, Info_h.biAlphaMask);
	} else {
		grk_image_destroy(image);
		image = nullptr;
		spdlog::error(
				"Other system than 24 bits/pixels or 8 bits (no RLE coding) is not yet implemented [{}]",
				Info_h.biBitCount);
	}
	cleanup: if (pData)
		free(pData);
	if (!readFromStdin && INPUT) {
		if (!grk::safe_fclose(INPUT)) {
			grk_image_destroy(image);
			image = nullptr;
		}
	}
	return image;
}
static bool write_int(FILE *fdest, uint32_t val) {
	int rc = fprintf(fdest, "%c%c%c%c", val & 0xff, (val >> 8) & 0xff,
			(val >> 16) & 0xff, (val >> 24) & 0xff);
	return (rc == sizeof(val));
}
static bool write_short(FILE *fdest, uint16_t val) {
	int rc = fprintf(fdest, "%c%c", val & 0xff, (val >> 8) & 0xff);
	return (rc == sizeof(val));
}
static int imagetobmp(grk_image *image, const char *outfile, bool verbose) {
	bool writeToStdout = grk::useStdio(outfile);
	uint32_t w, h;
	int32_t pad;
	FILE *fdest = nullptr;
	int adjustR, adjustG, adjustB;
	int rc = -1;
	uint8_t *destBuff = nullptr;

	if (!grk::sanityCheckOnImage(image, image->numcomps))
		goto cleanup;
	if (image->numcomps != 1 && image->numcomps != 3) {
		spdlog::error("Unsupported number of components: {}",
				image->numcomps);
		goto cleanup;
	}
	if (grk::isSubsampled(image)) {
		spdlog::error("Sub-sampled images not supported");
		goto cleanup;
	}

	for (uint32_t i = 0; i < image->numcomps; ++i) {
		if (image->comps[i].prec < 8) {
			spdlog::error("Unsupported precision: {} for component {}",
					image->comps[i].prec, i);
			goto cleanup;
		}
		if (!image->comps[i].data) {
			spdlog::error("imagetopng: component {} is null.", i);
			spdlog::error("\tAborting");
			goto cleanup;
		}
	}
	if (writeToStdout) {
		if (!grk::grok_set_binary_mode(stdout))
			goto cleanup;
		fdest = stdout;
	} else {
		fdest = fopen(outfile, "wb");
		if (!fdest) {
			spdlog::error("failed to open {} for writing", outfile);
			goto cleanup;
		}
	}

	if (image->numcomps == 3) {
		/* -->> -->> -->> -->>
		 24 bits color
		 <<-- <<-- <<-- <<-- */
		w = image->comps[0].w;
		h = image->comps[0].h;

		if (fprintf(fdest, "BM") != 2)
			goto cleanup;

		/* FILE HEADER */
		/* ------------- */
		if (!write_int(fdest, 3 * h * w + 3 * h * (w % 2) + 54))
			goto cleanup;
		if (!write_int(fdest, 0))
			goto cleanup;
		if (!write_int(fdest, 54))
			goto cleanup;

		/* INFO HEADER   */
		/* ------------- */
		if (!write_int(fdest, 40))
			goto cleanup;
		if (!write_int(fdest, w))
			goto cleanup;
		if (!write_int(fdest, h))
			goto cleanup;
		if (!write_short(fdest, 1))
			goto cleanup;
		if (!write_short(fdest, 24))
			goto cleanup;
		if (!write_int(fdest, 0))
			goto cleanup;
		if (!write_int(fdest, 3 * h * w + 3 * h * (w % 2)))
			goto cleanup;
		if (!write_int(fdest, 7834))
			goto cleanup;
		if (!write_int(fdest, 7834))
			goto cleanup;
		if (!write_int(fdest, 0))
			goto cleanup;
		if (!write_int(fdest, 0))
			goto cleanup;
		if (image->comps[0].prec > 8) {
			adjustR = (int) image->comps[0].prec - 8;
			if (verbose)
				spdlog::warn(
						"BMP CONVERSION: Truncating component 0 from {} bits to 8 bits",
						image->comps[0].prec);
		} else
			adjustR = 0;
		if (image->comps[1].prec > 8) {
			adjustG = (int) image->comps[1].prec - 8;
			if (verbose)
				spdlog::warn(
						"BMP CONVERSION: Truncating component 1 from {} bits to 8 bits",
						image->comps[1].prec);
		} else
			adjustG = 0;
		if (image->comps[2].prec > 8) {
			adjustB = (int) image->comps[2].prec - 8;
			if (verbose)
				spdlog::warn(
						"BMP CONVERSION: Truncating component 2 from {} bits to 8 bits",
						image->comps[2].prec);
		} else
			adjustB = 0;
		uint32_t sz = w * h;
		size_t padW = ((3 * w + 3) >> 2) << 2;
		uint8_t *destBuff = new uint8_t[padW];
		for (uint32_t j = 0; j < h; j++) {
			uint32_t destInd = 0;
			for (uint32_t i = 0; i < w; i++) {
				uint8_t rc, gc, bc;
				int32_t r, g, b;

				r = image->comps[0].data[sz - (j + 1) * w + i];
				r +=
						(image->comps[0].sgnd ?
								1 << (image->comps[0].prec - 1) : 0);
				if (adjustR > 0)
					r = ((r >> adjustR) + ((r >> (adjustR - 1)) % 2));
				if (r > 255)
					r = 255;
				else if (r < 0)
					r = 0;
				rc = (uint8_t) r;

				g = image->comps[1].data[sz - (j + 1) * w + i];
				g +=
						(image->comps[1].sgnd ?
								1 << (image->comps[1].prec - 1) : 0);
				if (adjustG > 0)
					g = ((g >> adjustG) + ((g >> (adjustG - 1)) % 2));
				if (g > 255)
					g = 255;
				else if (g < 0)
					g = 0;
				gc = (uint8_t) g;

				b = image->comps[2].data[sz - (j + 1) * w + i];
				b +=
						(image->comps[2].sgnd ?
								1 << (image->comps[2].prec - 1) : 0);
				if (adjustB > 0)
					b = ((b >> adjustB) + ((b >> (adjustB - 1)) % 2));
				if (b > 255)
					b = 255;
				else if (b < 0)
					b = 0;
				bc = (uint8_t) b;
				destBuff[destInd++] = bc;
				destBuff[destInd++] = gc;
				destBuff[destInd++] = rc;
			}
			// pad at end of row to ensure that width is divisible by 4
			for (pad = (3 * w) % 4 ? 4 - (3 * w) % 4 : 0; pad > 0; pad--) { /* ADD */
				destBuff[destInd++] = 0;
			}
			if (fwrite(destBuff, 1, destInd, fdest) != destInd)
				goto cleanup;
		}
	} else { /* Gray-scale */

		/* -->> -->> -->> -->>
		 8 bits non code (Gray scale)
		 <<-- <<-- <<-- <<-- */
		w = image->comps[0].w;
		h = image->comps[0].h;

		if (fprintf(fdest, "BM") != 2)
			goto cleanup;

		/* FILE HEADER */
		/* ------------- */
		if (!write_int(fdest, h * w + 54 + 1024 + h * (w % 2)))
			goto cleanup;
		if (!write_int(fdest, 0))
			goto cleanup;
		if (!write_int(fdest, 54 + 1024))
			goto cleanup;

		/* INFO HEADER   */
		/* ------------- */
		if (!write_int(fdest, 40))
			goto cleanup;
		if (!write_int(fdest, w))
			goto cleanup;
		if (!write_int(fdest, h))
			goto cleanup;
		if (!write_short(fdest, 1))
			goto cleanup;
		if (!write_short(fdest, 8))
			goto cleanup;
		if (!write_int(fdest, 0))
			goto cleanup;
		if (!write_int(fdest, h * w + h * (w % 2)))
			goto cleanup;
		if (!write_int(fdest, 7834))
			goto cleanup;
		if (!write_int(fdest, 7834))
			goto cleanup;
		if (!write_int(fdest, 256))
			goto cleanup;
		if (!write_int(fdest, 256))
			goto cleanup;
		if (image->comps[0].prec > 8) {
			adjustR = (int) image->comps[0].prec - 8;
			if (verbose)
				spdlog::warn(
						"BMP CONVERSION: Truncating component 0 from {} bits to 8 bits",
						image->comps[0].prec);
		} else
			adjustR = 0;

		for (uint32_t i = 0; i < 256; i++) {
			if (fprintf(fdest, "%c%c%c%c", i, i, i, 0) != 4)
				goto cleanup;
		}

		uint32_t sz = w * h;
		size_t padW = ((w + 3) >> 2) << 2;
		uint8_t *destBuff = new uint8_t[padW];
		for (uint32_t j = 0; j < h; j++) {
			uint32_t destInd = 0;
			for (uint32_t i = 0; i < w; i++) {
				int32_t r = image->comps[0].data[sz - (j + 1) * w + i];
				r +=
						(image->comps[0].sgnd ?
								1 << (image->comps[0].prec - 1) : 0);
				if (adjustR > 0)
					r = ((r >> adjustR) + ((r >> (adjustR - 1)) % 2));
				if (r > 255)
					r = 255;
				else if (r < 0)
					r = 0;
				destBuff[destInd++] = (uint8_t) r;
			}
			// pad at end of row to ensure that width is divisible by 4
			for (pad = w % 4 ? 4 - w % 4 : 0; pad > 0; pad--) /* ADD */
				destBuff[destInd++] = 0;
			if (fwrite(destBuff, 1, destInd, fdest) != destInd)
				goto cleanup;
		}

	}
	// success
	rc = 0;
	cleanup: delete[] destBuff;
	if (!writeToStdout && fdest) {
		if (!grk::safe_fclose(fdest)) {
			rc = 1;
		}
	}
	return rc;
}

bool BMPFormat::encode(grk_image *image, const char *filename,
		int32_t compressionParam, bool verbose) {
	(void) compressionParam;
	return imagetobmp(image, filename, verbose) ? false : true;
}
grk_image* BMPFormat::decode(const char *filename,
		grk_cparameters *parameters) {
	return bmptoimage(filename, parameters);
}
