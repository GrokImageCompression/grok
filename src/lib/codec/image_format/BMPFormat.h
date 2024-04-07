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

struct GRK_BITMAPFILEHEADER
{
   uint16_t bfType; /* 'BM' for Bitmap (19776) */
   uint32_t bfSize; /* Size of the file        */
   uint16_t bfReserved1; /* Reserved : 0            */
   uint16_t bfReserved2; /* Reserved : 0            */
   uint32_t bfOffBits; /* Offset                  */
};

struct GRK_BITMAPINFOHEADER
{
   uint32_t biSize; /* Size of the structure in bytes */
   int32_t biWidth; /* Width of the image in pixels */
   int32_t biHeight; /* Height of the image in pixels */
   uint16_t biPlanes; /* 1 */
   uint16_t biBitCount; /* Number of color bits per pixels */
   uint32_t biCompression; /* Type of compressing:
				  0: none
				  1: RLE8
				  2: RLE4
				  3: BITFIELD */
   uint32_t biSizeImage; /* Size of the image in bytes */
   int32_t biXpelsPerMeter; /* Horizontal (X) resolution in pixels/meter */
   int32_t biYpelsPerMeter; /* Vertical (Y) resolution in pixels/meter */
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
   uint32_t biIccProfileOffset; /* offset to ICC profile data */
   uint32_t biIccProfileSize; /* ICC profile size */
   uint32_t biReserved; /* Reserved */
};

class BMPFormat : public ImageFormat
{
 public:
   BMPFormat(void);
   ~BMPFormat(void);
   bool encodeHeader(void) override;
   bool encodePixels() override;
   using ImageFormat::encodePixels;
   bool encodeFinish(void) override;
   grk_image* decode(const std::string& filename, grk_cparameters* parameters) override;

 private:
   uint64_t off_;
   grk_image* bmp8toimage(const uint8_t* pData, uint32_t srcStride, grk_image* image,
						  uint8_t const* const* pLUT, bool topDown);
   grk_image* bmp4toimage(const uint8_t* pData, uint32_t srcStride, grk_image* image,
						  uint8_t const* const* pLUT);
   grk_image* bmp1toimage(const uint8_t* pData, uint32_t srcStride, grk_image* image,
						  uint8_t const* const* pLUT);
   bool read_file_header(GRK_BITMAPFILEHEADER* fileHeader, GRK_BITMAPINFOHEADER* infoHeader);
   bool read_info_header(GRK_BITMAPFILEHEADER* fileHeader, GRK_BITMAPINFOHEADER* infoHeader);
   bool read_raw_data(uint8_t* pData, uint32_t stride, uint32_t height);
   bool read_rle8_data(uint8_t* pData, uint32_t stride, uint32_t width, uint32_t height);
   bool read_rle4_data(uint8_t* pData, uint32_t stride, uint32_t width, uint32_t height);

   uint8_t* header_;
   uint64_t srcIndex_;
   GRK_BITMAPFILEHEADER fileHeader_;
   GRK_BITMAPINFOHEADER infoHeader_;

   void conv_1u32s(uint8_t const* pSrc, int32_t srcStride, int32_t* pDst, int32_t dstStride,
				   uint32_t destWidth, uint32_t destHeight);
   void conv_4u32s(uint8_t const* pSrc, int32_t srcStride, int32_t* pDst, int32_t dstStride,
				   uint32_t destWidth, uint32_t destHeight);
   void conv_8u32s(uint8_t const* pSrc, int32_t srcStride, int32_t* pDst, int32_t dstStride,
				   uint32_t width, uint32_t height);

   void applyLUT8u_1u32s_C1P3R(uint8_t const* pSrc, int32_t srcStride, int32_t* const* pDst,
							   int32_t const* pDstStride, uint8_t const* const* pLUT,
							   uint32_t destWidth, uint32_t destHeight);
   void applyLUT8u_4u32s_C1P3R(uint8_t const* pSrc, int32_t srcStride, int32_t* const* pDst,
							   int32_t const* pDstStride, uint8_t const* const* pLUT,
							   uint32_t destWidth, uint32_t destHeight);
   void applyLUT8u_8u32s_C1P3R(uint8_t const* pSrc, int32_t srcStride, int32_t* const* pDst,
							   int32_t const* pDstStride, uint8_t const* const* pLUT,
							   uint32_t destWidth, uint32_t destHeight);
   void mask32toimage(const uint8_t* pData, uint32_t srcStride, grk_image* image, uint32_t redMask,
					  uint32_t greenMask, uint32_t blueMask, uint32_t alphaMask);
   void mask16toimage(const uint8_t* pData, uint32_t srcStride, grk_image* image, uint32_t redMask,
					  uint32_t greenMask, uint32_t blueMask, uint32_t alphaMask);
   void bmp24toimage(const uint8_t* pData, uint32_t srcStride, grk_image* image);
   void mask_get_shift_and_prec(uint32_t mask, uint8_t* shift, uint8_t* prec);
};
