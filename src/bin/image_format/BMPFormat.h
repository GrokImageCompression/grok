/*
*    Copyright (C) 2016 Grok Image Compression Inc.
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
 */
#pragma once

#pragma once
#include "ImageFormat.h"

typedef struct {
	uint16_t bfType; 		/* 'BM' for Bitmap (19776) */
	uint32_t bfSize; 		/* Size of the file        */
	uint16_t bfReserved1; 	/* Reserved : 0            */
	uint16_t bfReserved2; 	/* Reserved : 0            */
	uint32_t bfOffBits; 	/* Offset                  */
} GRK_BITMAPFILEHEADER;

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
	uint32_t biIccProfileOffset; 	/* offset to ICC profile data */
	uint32_t biIccProfileSize; 	/* ICC profile size */
	uint32_t biReserved; 		/* Reserved */
} GRK_BITMAPINFOHEADER;


class BMPFormat  : public ImageFormat{
public:
	BMPFormat(void);
	bool encodeHeader(grk_image *  image, const std::string &filename, uint32_t compressionParam) override;
	bool encodeStrip(uint32_t rows) override;
	bool encodeFinish(void) override;
	grk_image *  decode(const std::string &filename,  grk_cparameters  *parameters) override;

private:
	grk_image* bmp8toimage(const uint8_t *pData, uint32_t srcStride,
									grk_image *image, uint8_t const *const*pLUT,
									bool topDown);
	grk_image* bmp4toimage(const uint8_t *pData, uint32_t srcStride,
			grk_image *image, uint8_t const *const*pLUT);
	grk_image* bmp1toimage(const uint8_t *pData, uint32_t srcStride,
			grk_image *image, uint8_t const *const*pLUT);
	bool bmp_read_file_header(GRK_BITMAPFILEHEADER *fileHeader, GRK_BITMAPINFOHEADER *infoHeader);
	bool bmp_read_info_header(GRK_BITMAPFILEHEADER *fileHeader, GRK_BITMAPINFOHEADER *infoHeader);
	bool bmp_read_raw_data(uint8_t *pData, uint32_t stride, uint32_t height);
	bool bmp_read_rle8_data(uint8_t *pData, uint32_t stride,
			uint32_t width, uint32_t height);
	bool bmp_read_rle4_data(uint8_t *pData, uint32_t stride,
			uint32_t width, uint32_t height);

	uint32_t getPaddedWidth();
	uint64_t m_srcIndex;
	GRK_BITMAPFILEHEADER File_h;
	GRK_BITMAPINFOHEADER Info_h;

};


