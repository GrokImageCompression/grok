/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
#include "spdlog/spdlog.h"
#include "grok.h"
#include "BMPFormat.h"
#include "convert.h"
#include <cstring>
#include "common.h"
#ifdef GROK_HAVE_URING
#include "FileUringIO.h"
#endif

// `MBED` in big endian format
const uint32_t ICC_PROFILE_EMBEDDED = 0x4d424544;
const uint32_t fileHeaderSize = 14;

const uint32_t BITMAPCOREHEADER_LENGTH = 12U;
const uint32_t BITMAPINFOHEADER_LENGTH = 40U;
const uint32_t BITMAPV2INFOHEADER_LENGTH = 52U;
const uint32_t BITMAPV3INFOHEADER_LENGTH = 56U;
const uint32_t BITMAPV4HEADER_LENGTH = 108U;
const uint32_t BITMAPV5HEADER_LENGTH = 124U;

const uint32_t os2_palette_element_len = 3;
const uint32_t palette_element_len = 4;

template<typename T>
void get_int(T** buf, T* val)
{
   *val = grk::endian<T>((*buf)[0], false);
   (*buf)++;
}

template<typename T>
void put_int(T** buf, T val)
{
   *buf[0] = grk::endian<T>(val, false);
   (*buf)++;
}

BMPFormat::BMPFormat(void) : off_(0), header_(nullptr), srcIndex_(0)
{
   memset(&fileHeader_, 0, sizeof(GRK_BITMAPFILEHEADER));
   memset(&infoHeader_, 0, sizeof(GRK_BITMAPINFOHEADER));
}

BMPFormat::~BMPFormat(void)
{
   delete[] header_;
}

bool BMPFormat::encodeHeader(void)
{
   if(isHeaderEncoded())
	  return true;

#ifdef GROK_HAVE_URING
   delete fileIO_;
   fileIO_ = new FileUringIO();
   if(!fileIO_->open(fileName_, "w"))
	  return false;
#else
   if(!openFile())
	  return false;
#endif
   bool ret = false;
   uint32_t w = image_->decompress_width;
   uint32_t h = image_->decompress_height;
   uint32_t padW = (uint32_t)image_->packed_row_bytes;
   uint32_t image_size = padW * h;
   uint32_t colours_used, lut_size;
   uint32_t full_header_size, info_header_size, icc_size = 0;
   uint32_t header_plus_lut = 0;
   uint8_t* header_ptr = nullptr;
   GrkIOBuf destBuff;

   if(!allComponentsSanityCheck(image_, false))
	  goto cleanup;
   if(isFinalOutputSubsampled(image_))
   {
	  spdlog::error("Sub-sampled images not supported");
	  goto cleanup;
   }
   if(image_->decompress_num_comps != 1 &&
	  (image_->decompress_num_comps != 3 && image_->decompress_num_comps != 4))
   {
	  spdlog::error("Unsupported number of components: {}", image_->decompress_num_comps);
	  goto cleanup;
   }

   colours_used = (image_->decompress_num_comps == 3) ? 0 : 256;
   lut_size = colours_used * (uint32_t)sizeof(uint32_t);
   full_header_size = fileHeaderSize + BITMAPINFOHEADER_LENGTH;
   if(image_->meta && image_->meta->color.icc_profile_buf)
   {
	  full_header_size = fileHeaderSize + sizeof(GRK_BITMAPINFOHEADER);
	  icc_size = image_->meta->color.icc_profile_len;
   }
   info_header_size = full_header_size - fileHeaderSize;
   header_plus_lut = full_header_size + lut_size;

   header_ptr = header_ = new uint8_t[header_plus_lut];
   *header_ptr++ = 'B';
   *header_ptr++ = 'M';

   /* FILE HEADER */
   // total size
   put_int((uint32_t**)(&header_ptr), full_header_size + lut_size + image_size + icc_size);
   // reserved
   put_int((uint32_t**)(&header_ptr), 0U);
   put_int((uint32_t**)(&header_ptr), full_header_size + lut_size);
   /* INFO HEADER   */
   put_int((uint32_t**)(&header_ptr), info_header_size);
   put_int((uint32_t**)(&header_ptr), w);
   put_int((uint32_t**)(&header_ptr), h);
   put_int((uint16_t**)(&header_ptr), (uint16_t)1);
   put_int((uint16_t**)(&header_ptr), (uint16_t)(image_->decompress_num_comps * 8));
   put_int((uint32_t**)(&header_ptr), 0U);
   put_int((uint32_t**)(&header_ptr), image_size);
   for(uint32_t i = 0; i < 2; ++i)
   {
	  double cap = (image_->capture_resolution[i] != 0) ? image_->capture_resolution[i] : 7834.0;
	  put_int((uint32_t**)(&header_ptr), (uint32_t)(cap + 0.5f));
   }
   put_int((uint32_t**)(&header_ptr), colours_used);
   put_int((uint32_t**)(&header_ptr), colours_used);
   if(image_->meta && image_->meta->color.icc_profile_buf)
   {
	  put_int((uint32_t**)(&header_ptr), 0U);
	  put_int((uint32_t**)(&header_ptr), 0U);
	  put_int((uint32_t**)(&header_ptr), 0U);
	  put_int((uint32_t**)(&header_ptr), 0U);
	  put_int((uint32_t**)(&header_ptr), ICC_PROFILE_EMBEDDED);
	  memset(header_ptr, 0, 36);
	  header_ptr += 36;
	  put_int((uint32_t**)(&header_ptr), 0U);
	  put_int((uint32_t**)(&header_ptr), 0U);
	  put_int((uint32_t**)(&header_ptr), 0U);
	  put_int((uint32_t**)(&header_ptr), 0U);
	  put_int((uint32_t**)(&header_ptr), info_header_size + lut_size + image_size);
	  put_int((uint32_t**)(&header_ptr), image_->meta->color.icc_profile_len);
	  put_int((uint32_t**)(&header_ptr), 0U);
   }
   // 1024-byte LUT
   if(image_->decompress_num_comps == 1)
   {
	  for(uint32_t i = 0; i < 256; i++)
	  {
		 *header_ptr++ = (uint8_t)i;
		 *header_ptr++ = (uint8_t)i;
		 *header_ptr++ = (uint8_t)i;
		 *header_ptr++ = 0;
	  }
   }
   destBuff.data_ = header_;
   destBuff.offset_ = off_;
   destBuff.pooled_ = false;
   destBuff.len_ = header_plus_lut;
   if(write(destBuff) != destBuff.len_)
	  goto cleanup;
   off_ += header_plus_lut;
   ret = true;
   encodeState = IMAGE_FORMAT_ENCODED_HEADER;
cleanup:

   return ret;
}

bool BMPFormat::encodePixels()
{
   if(encodeState & IMAGE_FORMAT_ENCODED_PIXELS)
	  return true;
   if(!isHeaderEncoded())
   {
	  if(!encodeHeader())
		 return false;
   }
   bool ret = false;
   auto w = image_->decompress_width;
   auto h = image_->decompress_height;
   auto decompress_num_comps = image_->decompress_num_comps;
   auto stride_src = image_->comps[0].stride;
   srcIndex_ = (uint64_t)stride_src * (h - 1);
   uint32_t w_dest = (uint32_t)image_->packed_row_bytes;
   uint32_t pad_dest = (4 - (((uint64_t)decompress_num_comps * w) & 3)) & 3;

   int32_t scale[4] = {1, 1, 1, 1};
   uint8_t scaleType[4] = {0, 0, 0, 0};
   int32_t shift[4] = {0, 0, 0, 0};

   for(uint16_t compno = 0; compno < decompress_num_comps; ++compno)
   {
	  if(image_->comps[0].prec != 8)
	  {
		 if(image_->comps[0].prec < 8)
		 {
			scale[compno] = 1 << (8 - image_->comps[compno].prec);
			scaleType[compno] = 1;
		 }
		 else
		 {
			scale[compno] = 1 << (image_->comps[compno].prec - 8);
			scaleType[compno] = 2;
		 }
		 spdlog::warn("BMP conversion: scaling component {} from {} bits to 8 bits", compno,
					  image_->comps[compno].prec);
	  }
	  shift[compno] = (image_->comps[compno].sgnd ? 1 << (image_->comps[compno].prec - 1) : 0);
   }

   auto packedLen = (uint64_t)image_->rows_per_strip * w_dest;
   auto destBuff = pool.get(packedLen);
   // zero out padding at end of line
   if(pad_dest)
   {
	  uint8_t* ptr = destBuff.data_ + w_dest - pad_dest;
	  for(uint32_t m = 0; m < image_->rows_per_strip; ++m)
	  {
		 memset(ptr, 0, pad_dest);
		 ptr += w_dest;
	  }
   }
   uint32_t rowCount = 0;
   while(rowCount < h)
   {
	  uint64_t destInd = 0;
	  uint32_t k_max = std::min<uint32_t>(image_->rows_per_strip, (uint32_t)(h - rowCount));
	  for(uint32_t k = 0; k < k_max; k++)
	  {
		 for(uint32_t i = 0; i < w; i++)
		 {
			uint8_t rc[4] = {0, 0, 0, 0};
			for(uint16_t compno = 0; compno < decompress_num_comps; ++compno)
			{
			   int32_t r = image_->comps[compno].data[srcIndex_ + i];
			   r += shift[compno];
			   if(scaleType[compno] == 1)
				  r *= scale[compno];
			   else if(scaleType[compno] == 2)
				  r /= scale[compno];
			   rc[compno] = (uint8_t)r;
			}
			if(decompress_num_comps == 1)
			{
			   destBuff.data_[destInd++] = rc[0];
			}
			else
			{
			   destBuff.data_[destInd++] = rc[2];
			   destBuff.data_[destInd++] = rc[1];
			   destBuff.data_[destInd++] = rc[0];
			   if(decompress_num_comps == 4)
				  destBuff.data_[destInd++] = rc[3];
			}
		 }
		 destInd += pad_dest;
		 srcIndex_ -= stride_src;
	  }
	  destBuff.offset_ = off_;
	  destBuff.pooled_ = true;
	  destBuff.len_ = destInd;
	  if(write(destBuff) != destBuff.len_)
		 goto cleanup;
	  destBuff = pool.get(packedLen);
	  // pooled buffer may not have been zero-padded
#ifdef GROK_HAVE_URING
	  if(pad_dest)
	  {
		 uint8_t* ptr = destBuff.data_ + w_dest - pad_dest;
		 for(uint32_t m = 0; m < image_->rows_per_strip; ++m)
		 {
			memset(ptr, 0, pad_dest);
			ptr += w_dest;
		 }
	  }
#endif
	  off_ += destInd;
	  rowCount += k_max;
   }

   ret = true;
cleanup:
   pool.put(destBuff);

   return ret;
}
bool BMPFormat::encodeFinish(void)
{
   if(image_->meta && image_->meta->color.icc_profile_buf)
   {
	  GrkIOBuf destBuff;
	  destBuff.data_ = image_->meta->color.icc_profile_buf;
	  destBuff.offset_ = off_;
	  destBuff.pooled_ = false;
	  destBuff.len_ = image_->meta->color.icc_profile_len;
	  if(write(destBuff) != destBuff.len_)
		 return false;
	  off_ += image_->meta->color.icc_profile_len;
   }

   return ImageFormat::encodeFinish();
}

grk_image* BMPFormat::bmp1toimage(const uint8_t* pData, uint32_t srcStride, grk_image* image,
								  uint8_t const* const* pLUT)
{
   uint32_t width = image->decompress_width;
   uint32_t height = image->decompress_height;
   auto pSrc = pData + (height - 1U) * srcStride;
   if(image->decompress_num_comps == 1U)
   {
	  conv_1u32s(pSrc, -(int32_t)srcStride, image->comps[0].data, (int32_t)image->comps[0].stride,
				 width, height);
   }
   else
   {
	  int32_t* pDst[3];
	  int32_t pDstStride[3];

	  pDst[0] = image->comps[0].data;
	  pDst[1] = image->comps[1].data;
	  pDst[2] = image->comps[2].data;
	  pDstStride[0] = (int32_t)image->comps[0].stride;
	  pDstStride[1] = (int32_t)image->comps[0].stride;
	  pDstStride[2] = (int32_t)image->comps[0].stride;
	  applyLUT8u_1u32s_C1P3R(pSrc, -(int32_t)srcStride, pDst, pDstStride, pLUT, width, height);
   }
   return image;
}

grk_image* BMPFormat::bmp4toimage(const uint8_t* pData, uint32_t srcStride, grk_image* image,
								  uint8_t const* const* pLUT)
{
   uint32_t width = image->decompress_width;
   uint32_t height = image->decompress_height;
   auto pSrc = pData + (height - 1U) * srcStride;
   if(image->decompress_num_comps == 1U)
   {
	  conv_4u32s(pSrc, -(int32_t)srcStride, image->comps[0].data, (int32_t)image->comps[0].stride,
				 width, height);
   }
   else
   {
	  int32_t* pDst[3];
	  int32_t pDstStride[3];

	  pDst[0] = image->comps[0].data;
	  pDst[1] = image->comps[1].data;
	  pDst[2] = image->comps[2].data;
	  pDstStride[0] = (int32_t)image->comps[0].stride;
	  pDstStride[1] = (int32_t)image->comps[0].stride;
	  pDstStride[2] = (int32_t)image->comps[0].stride;
	  applyLUT8u_4u32s_C1P3R(pSrc, -(int32_t)srcStride, pDst, pDstStride, pLUT, width, height);
   }
   return image;
}

grk_image* BMPFormat::bmp8toimage(const uint8_t* pData, uint32_t srcStride, grk_image* image,
								  uint8_t const* const* pLUT, bool topDown)
{
   uint32_t width = image->decompress_width;
   uint32_t height = image->decompress_height;
   auto pSrc = topDown ? pData : (pData + (height - 1U) * srcStride);
   int32_t s_stride = topDown ? (int32_t)srcStride : (-(int32_t)srcStride);
   if(image->decompress_num_comps == 1U)
   {
	  conv_8u32s(pSrc, s_stride, image->comps[0].data, (int32_t)image->comps[0].stride, width,
				 height);
   }
   else
   {
	  int32_t* pDst[3];
	  int32_t pDstStride[3];

	  pDst[0] = image->comps[0].data;
	  pDst[1] = image->comps[1].data;
	  pDst[2] = image->comps[2].data;
	  pDstStride[0] = (int32_t)image->comps[0].stride;
	  pDstStride[1] = (int32_t)image->comps[0].stride;
	  pDstStride[2] = (int32_t)image->comps[0].stride;
	  applyLUT8u_8u32s_C1P3R(pSrc, s_stride, pDst, pDstStride, pLUT, width, height);
   }
   return image;
}

bool BMPFormat::read_file_header(GRK_BITMAPFILEHEADER* fileHeader, GRK_BITMAPINFOHEADER* infoHeader)
{
   memset(infoHeader, 0, sizeof(*infoHeader));
   const size_t len = fileHeaderSize + sizeof(uint32_t);
   uint8_t temp[len];
   auto temp_ptr = (uint32_t*)temp;
   if(!read(temp, len))
	  return false;
   get_int((uint16_t**)&temp_ptr, &fileHeader->bfType);
   if(fileHeader->bfType != 19778)
   {
	  spdlog::error("Not a BMP file");
	  return false;
   }
   get_int(&temp_ptr, &fileHeader->bfSize);
   get_int((uint16_t**)&temp_ptr, &fileHeader->bfReserved1);
   get_int((uint16_t**)&temp_ptr, &fileHeader->bfReserved2);
   get_int(&temp_ptr, &fileHeader->bfOffBits);
   get_int(&temp_ptr, &infoHeader->biSize);

   return true;
}

bool BMPFormat::read_info_header(GRK_BITMAPFILEHEADER* fileHeader, GRK_BITMAPINFOHEADER* infoHeader)
{
   const size_t len_initial = infoHeader->biSize - sizeof(uint32_t);
   uint8_t temp[sizeof(GRK_BITMAPINFOHEADER)];
   auto temp_ptr = (uint32_t*)temp;
   if(!read(temp, len_initial))
	  return false;

   switch(infoHeader->biSize)
   {
	  case BITMAPCOREHEADER_LENGTH:
	  case BITMAPINFOHEADER_LENGTH:
	  case BITMAPV2INFOHEADER_LENGTH:
	  case BITMAPV3INFOHEADER_LENGTH:
	  case BITMAPV4HEADER_LENGTH:
	  case BITMAPV5HEADER_LENGTH:
		 break;
	  default:
		 spdlog::error("unknown BMP header size {}", infoHeader->biSize);
		 return false;
   }
   bool is_os2 = infoHeader->biSize == BITMAPCOREHEADER_LENGTH;
   if(is_os2)
   { // OS2
	  int16_t val;
	  get_int((int16_t**)&temp_ptr, &val);
	  infoHeader->biWidth = val;
	  get_int((int16_t**)&temp_ptr, &val);
	  infoHeader->biHeight = val;
   }
   else
   {
	  get_int((int32_t**)&temp_ptr, &infoHeader->biWidth);
	  get_int((int32_t**)&temp_ptr, &infoHeader->biHeight);
   }
   get_int((uint16_t**)&temp_ptr, &infoHeader->biPlanes);
   get_int((uint16_t**)&temp_ptr, &infoHeader->biBitCount);
   // sanity check
   if(infoHeader->biBitCount > 32)
   {
	  spdlog::error("Bit count {} not supported.", infoHeader->biBitCount);
	  return false;
   }
   if(infoHeader->biSize >= BITMAPINFOHEADER_LENGTH)
   {
	  get_int(&temp_ptr, &infoHeader->biCompression);
	  get_int(&temp_ptr, &infoHeader->biSizeImage);
	  get_int((int32_t**)&temp_ptr, &infoHeader->biXpelsPerMeter);
	  get_int((int32_t**)&temp_ptr, &infoHeader->biYpelsPerMeter);
	  get_int(&temp_ptr, &infoHeader->biClrUsed);
	  if(infoHeader_.biBitCount <= 8U && infoHeader->biClrUsed == 0)
		 infoHeader->biClrUsed = (1U << infoHeader_.biBitCount);
	  get_int(&temp_ptr, &infoHeader->biClrImportant);

	  if(fileHeader->bfSize && infoHeader->biSizeImage)
	  {
		 // re-adjust header size
		 // note: fileHeader->bfSize may include ICC profile length if ICC is present, in which
		 // case defacto_header_size will be greater than BITMAPV5HEADER_LENGTH. This is not a
		 // problem below since we truncate the defacto size at BITMAPV5HEADER_LENGTH.
		 uint32_t defacto_header_size = fileHeader->bfSize - fileHeaderSize -
										infoHeader->biClrUsed * (uint32_t)sizeof(uint32_t) -
										infoHeader->biSizeImage;
		 if(defacto_header_size > infoHeader->biSize)
		 {
			infoHeader->biSize = std::min<uint32_t>(defacto_header_size, BITMAPV5HEADER_LENGTH);
			const size_t len_remaining = infoHeader->biSize - (len_initial + sizeof(uint32_t));
			if(!read(temp + len_initial, len_remaining))
			   return false;
		 }
	  }
   }
   if(infoHeader->biSize >= BITMAPV2INFOHEADER_LENGTH)
   {
	  get_int(&temp_ptr, &infoHeader->biRedMask);
	  get_int(&temp_ptr, &infoHeader->biGreenMask);
	  get_int(&temp_ptr, &infoHeader->biBlueMask);
   }
   if(infoHeader->biSize >= BITMAPV3INFOHEADER_LENGTH)
   {
	  get_int(&temp_ptr, (uint32_t*)&infoHeader->biAlphaMask);
   }
   if(infoHeader->biSize >= BITMAPV4HEADER_LENGTH)
   {
	  get_int(&temp_ptr, &infoHeader->biColorSpaceType);
	  memcpy(infoHeader->biColorSpaceEP, temp_ptr, sizeof(infoHeader->biColorSpaceEP));
	  temp_ptr += sizeof(infoHeader->biColorSpaceEP) / sizeof(uint32_t);
	  get_int(&temp_ptr, &infoHeader->biRedGamma);
	  get_int(&temp_ptr, &infoHeader->biGreenGamma);
	  get_int(&temp_ptr, &infoHeader->biBlueGamma);
   }
   if(infoHeader->biSize >= BITMAPV5HEADER_LENGTH)
   {
	  get_int(&temp_ptr, &infoHeader->biIntent);
	  get_int(&temp_ptr, &infoHeader->biIccProfileOffset);
	  get_int(&temp_ptr, &infoHeader->biIccProfileSize);
	  get_int(&temp_ptr, &infoHeader->biReserved);
   }
   return true;
}

bool BMPFormat::read_raw_data(uint8_t* pData, uint32_t stride, uint32_t height)
{
   return read(pData, (size_t)stride * height);
}

bool BMPFormat::read_rle8_data(uint8_t* pData, uint32_t stride, uint32_t width, uint32_t height)
{
   uint32_t x = 0, y = 0, written = 0;
   uint8_t* pix = nullptr;
   const uint8_t* beyond = nullptr;
   uint8_t* pixels_ptr = nullptr;
   bool rc = false;
   auto pixels = new uint8_t[infoHeader_.biSizeImage];
   if(!read(pixels, infoHeader_.biSizeImage))
   {
	  goto cleanup;
   }
   pixels_ptr = pixels;
   beyond = pData + (size_t)stride * height;
   pix = pData;

   while(y < height)
   {
	  int c = *pixels_ptr++;
	  if(c)
	  {
		 int j;
		 uint8_t c1 = *pixels_ptr++;
		 for(j = 0; (j < c) && (x < width) && ((size_t)pix < (size_t)beyond); j++, x++, pix++)
		 {
			*pix = c1;
			written++;
		 }
	  }
	  else
	  {
		 c = *pixels_ptr++;
		 if(c == 0x00)
		 { /* EOL */
			x = 0;
			++y;
			pix = pData + y * stride + x;
		 }
		 else if(c == 0x01)
		 { /* EOP */
			break;
		 }
		 else if(c == 0x02)
		 { /* MOVE by dxdy */
			c = *pixels_ptr++;
			x += (uint32_t)c;
			c = *pixels_ptr++;
			y += (uint32_t)c;
			pix = pData + y * stride + x;
		 }
		 else
		 { /* 03 .. 255 */
			int j;
			for(j = 0; (j < c) && (x < width) && ((size_t)pix < (size_t)beyond); j++, x++, pix++)
			{
			   uint8_t c1 = *pixels_ptr++;
			   *pix = c1;
			   written++;
			}
			if((uint32_t)c & 1U)
			{ /* skip padding byte */
			   pixels_ptr++;
			}
		 }
	  }
   } /* while() */
   if(written != width * height)
   {
	  spdlog::error("Number of pixels written does not match specified image dimensions.");
	  goto cleanup;
   }
   rc = true;
cleanup:
   delete[] pixels;
   return rc;
}
bool BMPFormat::read_rle4_data(uint8_t* pData, uint32_t stride, uint32_t width, uint32_t height)
{
   uint32_t x = 0, y = 0, written = 0;
   uint8_t* pix = nullptr;
   uint8_t* pixels_ptr = nullptr;
   const uint8_t* beyond = nullptr;
   bool rc = false;
   auto pixels = new uint8_t[infoHeader_.biSizeImage];
   if(!read(pixels, infoHeader_.biSizeImage))
	  goto cleanup;
   pixels_ptr = pixels;
   beyond = pData + (size_t)stride * height;
   pix = pData;
   while(y < height)
   {
	  int c = *pixels_ptr++;
	  if(c)
	  { /* encoded mode */
		 int j;
		 uint8_t c1 = *pixels_ptr++;

		 for(j = 0; (j < c) && (x < width) && ((size_t)pix < (size_t)beyond); j++, x++, pix++)
		 {
			*pix = (uint8_t)((j & 1) ? (c1 & 0x0fU) : ((uint8_t)(c1 >> 4) & 0x0fU));
			written++;
		 }
	  }
	  else
	  { /* absolute mode */
		 c = *pixels_ptr++;
		 if(c == 0x00)
		 { /* EOL */
			x = 0;
			y++;
			pix = pData + y * stride;
		 }
		 else if(c == 0x01)
		 { /* EOP */
			break;
		 }
		 else if(c == 0x02)
		 { /* MOVE by dxdy */
			c = *pixels_ptr++;
			x += (uint32_t)c;

			c = *pixels_ptr++;
			y += (uint32_t)c;
			pix = pData + y * stride + x;
		 }
		 else
		 { /* 03 .. 255 : absolute mode */
			int j;
			uint8_t c1 = 0U;

			for(j = 0; (j < c) && (x < width) && ((size_t)pix < (size_t)beyond); j++, x++, pix++)
			{
			   if((j & 1) == 0)
				  c1 = *pixels_ptr++;
			   *pix = (uint8_t)((j & 1) ? (c1 & 0x0fU) : ((uint8_t)(c1 >> 4) & 0x0fU));
			   written++;
			}
			/* skip padding byte */
			if(((c & 3) == 1) || ((c & 3) == 2))
			   pixels_ptr++;
		 }
	  }
   } /* while(y < height) */
   if(written != width * height)
   {
	  spdlog::error("Number of pixels written does not match specified image dimensions.");
	  goto cleanup;
   }
   rc = true;
cleanup:
   delete[] pixels;
   return rc;
}

grk_image* BMPFormat::decode(const std::string& fname, grk_cparameters* parameters)
{
   grk_image_comp cmptparm[4]; /* maximum of 4 components */
   uint8_t lut_R[256], lut_G[256], lut_B[256];
   uint8_t const* pLUT[3];
   grk_image* image = nullptr;
   bool result = false;
   uint8_t* pData = nullptr;
   uint32_t bmpStride;
   pLUT[0] = lut_R;
   pLUT[1] = lut_G;
   pLUT[2] = lut_B;
   bool handled = true;
   bool topDown = false;
   bool is_os2 = false;
   uint8_t* palette = nullptr;
   uint32_t palette_num_entries = 0;
   uint8_t palette_has_colour = 0U;
   uint16_t numcmpts = 1U;
   GRK_COLOR_SPACE colour_space = GRK_CLRSPC_UNKNOWN;

   image_ = image;
   if(!open(fname, "r"))
	  return nullptr;

   if(!read_file_header(&fileHeader_, &infoHeader_))
	  goto cleanup;
   if(!read_info_header(&fileHeader_, &infoHeader_))
	  goto cleanup;
   is_os2 = infoHeader_.biSize == BITMAPCOREHEADER_LENGTH;
   if(is_os2)
   {
	  uint32_t num_entries = (fileHeader_.bfOffBits - fileHeaderSize - BITMAPCOREHEADER_LENGTH) /
							 os2_palette_element_len;
	  if(num_entries != (uint32_t)(1 << infoHeader_.biBitCount))
	  {
		 spdlog::error("OS2: calculated number of entries {} "
					   "doesn't match (1 << bit count) {}",
					   num_entries, (uint32_t)(1 << infoHeader_.biBitCount));
		 goto cleanup;
	  }
   }
   if(infoHeader_.biWidth < 0)
   {
	  spdlog::warn("BMP with negative width. Converting to positive value");
	  infoHeader_.biWidth = -infoHeader_.biWidth;
   }
   if(infoHeader_.biHeight < 0)
   {
	  topDown = true;
	  infoHeader_.biHeight = -infoHeader_.biHeight;
   }
   /* Load palette */
   if(infoHeader_.biBitCount <= 8U)
   {
	  memset(lut_R, 0, sizeof(lut_R));
	  memset(lut_G, 0, sizeof(lut_G));
	  memset(lut_B, 0, sizeof(lut_B));

	  palette_num_entries = infoHeader_.biClrUsed;
	  // need to check this a second time for OS2 files
	  if(palette_num_entries == 0U)
		 palette_num_entries = (1U << infoHeader_.biBitCount);
	  else if(palette_num_entries > 256U)
		 palette_num_entries = 256U;

	  const uint32_t palette_bytes =
		  palette_num_entries * (is_os2 ? os2_palette_element_len : palette_element_len);
	  palette = new uint8_t[palette_bytes];
	  if(!read(palette, palette_bytes))
		 goto cleanup;
	  uint8_t* pal_ptr = palette;

	  if(palette_num_entries > 0U)
	  {
		 for(uint32_t i = 0U; i < palette_num_entries; i++)
		 {
			lut_B[i] = *pal_ptr++;
			lut_G[i] = *pal_ptr++;
			lut_R[i] = *pal_ptr++;
			if(!is_os2)
			   pal_ptr++;
			palette_has_colour |= (lut_B[i] ^ lut_G[i]) | (lut_G[i] ^ lut_R[i]);
		 }
		 if(palette_has_colour)
			numcmpts = 3U;
	  }
   }
   else
   {
	  numcmpts = 3U;
	  if((infoHeader_.biCompression == 3) && (infoHeader_.biAlphaMask != 0U))
		 numcmpts++;
   }

   if(infoHeader_.biWidth == 0 || infoHeader_.biHeight == 0)
	  goto cleanup;
   if(infoHeader_.biBitCount > ((uint32_t)((uint32_t)-1) - 31) / (uint32_t)infoHeader_.biWidth)
	  goto cleanup;

   bmpStride = (((uint32_t)infoHeader_.biWidth * infoHeader_.biBitCount + 31U) / 32U) *
			   (uint32_t)sizeof(uint32_t); /* rows are aligned on 32bits */
   if(infoHeader_.biBitCount == 4 && infoHeader_.biCompression == 2)
   { /* RLE 4 gets decoded as 8 bits data for now... */
	  if(8 > ((uint32_t)((uint32_t)-1) - 31) / (uint32_t)infoHeader_.biWidth)
		 goto cleanup;
	  bmpStride = (((uint32_t)infoHeader_.biWidth * 8U + 31U) / 32U) * (uint32_t)sizeof(uint32_t);
   }

   if(bmpStride > ((uint32_t)(uint32_t)-1) / sizeof(uint8_t) / (uint32_t)infoHeader_.biHeight)
	  goto cleanup;
   pData = new uint8_t[bmpStride * (size_t)infoHeader_.biHeight];
   if(pData == nullptr)
	  goto cleanup;
   if(!seek(fileHeader_.bfOffBits, SEEK_SET))
	  goto cleanup;

   switch(infoHeader_.biCompression)
   {
	  case 0:
	  case 3:
		 /* read raw data */
		 result = read_raw_data(pData, bmpStride, (uint32_t)infoHeader_.biHeight);
		 break;
	  case 1:
		 /* read rle8 data */
		 result = read_rle8_data(pData, bmpStride, (uint32_t)infoHeader_.biWidth,
								 (uint32_t)infoHeader_.biHeight);
		 break;
	  case 2:
		 /* read rle4 data */
		 result = read_rle4_data(pData, bmpStride, (uint32_t)infoHeader_.biWidth,
								 (uint32_t)infoHeader_.biHeight);
		 break;
	  default:
		 spdlog::error("Unsupported BMP compression");
		 result = false;
		 break;
   }
   if(!result)
   {
	  goto cleanup;
   }

   colour_space = (numcmpts == 1U) ? GRK_CLRSPC_GRAY : GRK_CLRSPC_SRGB;
   if(palette && palette_has_colour)
	  numcmpts = 1;

   /* create the image */
   memset(&cmptparm[0], 0, sizeof(cmptparm));
   for(uint32_t i = 0; i < numcmpts; i++)
   {
	  auto img_comp = cmptparm + i;
	  img_comp->prec = (numcmpts == 1U) ? (uint8_t)infoHeader_.biBitCount : 8U;
	  img_comp->sgnd = false;
	  img_comp->dx = parameters->subsampling_dx;
	  img_comp->dy = parameters->subsampling_dy;
	  img_comp->w = grk::ceildiv<uint32_t>((uint32_t)infoHeader_.biWidth, img_comp->dx);
	  img_comp->h = grk::ceildiv<uint32_t>((uint32_t)infoHeader_.biHeight, img_comp->dy);
   }

   image = grk_image_new(numcmpts, &cmptparm[0], colour_space, true);
   if(!image)
	  goto cleanup;

   if(palette)
   {
	  uint8_t num_channels = palette_has_colour ? 3U : 1U;
	  create_meta(image);
	  auto meta = image->meta;
	  allocPalette(&meta->color, num_channels, (uint16_t)palette_num_entries);
	  auto cmap = new _grk_component_mapping_comp[num_channels];
	  for(uint8_t i = 0; i < num_channels; ++i)
	  {
		 cmap[i].component_index = 0;
		 cmap[i].mapping_type = 1;
		 cmap[i].palette_column = i;
		 meta->color.palette->channel_prec[i] = 8U;
		 meta->color.palette->channel_sign[i] = false;
	  }
	  meta->color.palette->component_mapping = cmap;
	  auto lut_ptr = meta->color.palette->lut;
	  for(uint16_t i = 0; i < palette_num_entries; i++)
	  {
		 *lut_ptr++ = lut_R[i];
		 if(num_channels == 3)
		 {
			*lut_ptr++ = lut_G[i];
			*lut_ptr++ = lut_B[i];
		 }
	  }
   }

   // ICC profile
   if(infoHeader_.biSize == sizeof(GRK_BITMAPINFOHEADER) &&
	  infoHeader_.biColorSpaceType == ICC_PROFILE_EMBEDDED && infoHeader_.biIccProfileSize &&
	  infoHeader_.biIccProfileSize < grk::maxICCProfileBufferLen)
   {
	  // read in ICC profile
	  if(!seek(fileHeaderSize + infoHeader_.biIccProfileOffset, SEEK_SET))
		 goto cleanup;

	  // allocate buffer
	  auto iccbuf = new uint8_t[infoHeader_.biIccProfileSize];
	  if(!read(iccbuf, infoHeader_.biIccProfileSize))
	  {
		 spdlog::warn("Unable to read full ICC profile. Profile will be ignored.");
		 delete[] iccbuf;
		 goto cleanup;
	  }
	  copy_icc(image, iccbuf, infoHeader_.biIccProfileSize);
	  delete[] iccbuf;
   }
   if(numcmpts == 4U)
   {
	  image->comps[3].type = GRK_CHANNEL_TYPE_OPACITY;
	  image->comps[3].association = GRK_CHANNEL_ASSOC_WHOLE_IMAGE;
   }

   /* set image offset and reference grid */
   image->x0 = parameters->image_offset_x0;
   image->y0 = parameters->image_offset_y0;
   image->x1 = image->x0 + ((uint32_t)infoHeader_.biWidth - 1U) * parameters->subsampling_dx + 1U;
   image->y1 = image->y0 + ((uint32_t)infoHeader_.biHeight - 1U) * parameters->subsampling_dy + 1U;

   /* Read the data */
   switch(infoHeader_.biCompression)
   {
	  case 0:
		 switch(infoHeader_.biBitCount)
		 {
			case 32: /* RGBX */
			   mask32toimage(pData, bmpStride, image, 0x00FF0000U, 0x0000FF00U, 0x000000FFU,
							 0x00000000U);
			   break;
			case 24: /*RGB */
			   bmp24toimage(pData, bmpStride, image);
			   break;
			case 16: /*RGBX */
			   mask16toimage(pData, bmpStride, image, 0x7C00U, 0x03E0U, 0x001FU, 0x0000U);
			   break;
			case 8: /* RGB 8bpp Indexed */
			   bmp8toimage(pData, bmpStride, image, pLUT, topDown);
			   break;
			case 4: /* RGB 4bpp Indexed */
			   bmp4toimage(pData, bmpStride, image, pLUT);
			   break;
			case 1: /* Grayscale 1bpp Indexed */
			   bmp1toimage(pData, bmpStride, image, pLUT);
			   break;
			default:
			   handled = false;
			   break;
		 }
		 break;
	  case 1:
		 switch(infoHeader_.biBitCount)
		 {
			case 8: /*RLE8*/
			   bmp8toimage(pData, bmpStride, image, pLUT, topDown);
			   break;
			default:
			   handled = false;
			   break;
		 }
		 break;
	  case 2:
		 switch(infoHeader_.biBitCount)
		 {
			case 4: /*RLE4*/
			   bmp8toimage(pData, bmpStride, image, pLUT,
						   topDown); /* RLE 4 gets decoded as 8 bits data for now */
			   break;
			default:
			   handled = false;
			   break;
		 }
		 break;
	  case 3:
		 switch(infoHeader_.biBitCount)
		 {
			case 32: /* BITFIELDS bit mask */
			   if(infoHeader_.biRedMask && infoHeader_.biGreenMask && infoHeader_.biBlueMask)
			   {
				  bool fail = false;
				  bool hasAlpha = image->decompress_num_comps > 3;
				  // sanity check on bit masks
				  uint32_t m[4] = {infoHeader_.biRedMask, infoHeader_.biGreenMask,
								   infoHeader_.biBlueMask, infoHeader_.biAlphaMask};
				  for(uint32_t i = 0; i < image->decompress_num_comps; ++i)
				  {
					 int lead = grk::count_leading_zeros(m[i]);
					 int trail = grk::count_trailing_zeros(m[i]);
					 int cnt = grk::population_count(m[i]);
					 // check contiguous
					 if(lead + trail + cnt != 32)
					 {
						spdlog::error("RGB(A) bit masks must be contiguous");
						fail = true;
						break;
					 }
					 // check supported precision
					 if(cnt > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
					 {
						spdlog::error("RGB(A) bit mask with precision ({0:d}) greater than "
									  "%d is not supported",
									  cnt, GRK_MAX_SUPPORTED_IMAGE_PRECISION);
						fail = true;
					 }
				  }
				  // check overlap
				  if((m[0] & m[1]) || (m[0] & m[2]) || (m[1] & m[2]))
				  {
					 spdlog::error("RGB(A) bit masks must not overlap");
					 fail = true;
				  }
				  if(hasAlpha && !fail)
				  {
					 if((m[0] & m[3]) || (m[1] & m[3]) || (m[2] & m[3]))
					 {
						spdlog::error("RGB(A) bit masks must not overlap");
						fail = true;
					 }
				  }
				  if(fail)
				  {
					 spdlog::error("RGB(A) bit masks:\n"
								   "{0:b}\n"
								   "{0:b}\n"
								   "{0:b}\n"
								   "{0:b}",
								   m[0], m[1], m[2], m[3]);
					 grk_object_unref(&image->obj);
					 image = nullptr;
					 goto cleanup;
				  }
			   }
			   else
			   {
				  spdlog::error("RGB(A) bit masks must be non-zero");
				  handled = false;
				  break;
			   }

			   mask32toimage(pData, bmpStride, image, infoHeader_.biRedMask,
							 infoHeader_.biGreenMask, infoHeader_.biBlueMask,
							 infoHeader_.biAlphaMask);
			   break;
			case 16: /* BITFIELDS bit mask*/
			   if((infoHeader_.biRedMask == 0U) && (infoHeader_.biGreenMask == 0U) &&
				  (infoHeader_.biBlueMask == 0U))
			   {
				  infoHeader_.biRedMask = 0xF800U;
				  infoHeader_.biGreenMask = 0x07E0U;
				  infoHeader_.biBlueMask = 0x001FU;
			   }
			   mask16toimage(pData, bmpStride, image, infoHeader_.biRedMask,
							 infoHeader_.biGreenMask, infoHeader_.biBlueMask,
							 infoHeader_.biAlphaMask);
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
   if(!handled)
   {
	  grk_object_unref(&image->obj);
	  image = nullptr;
	  spdlog::error("Precision [{}] does not match supported precision: "
					"24 bit RGB, 8 bit RGB, 4/8 bit RLE and 16/32 bit BITFIELD",
					infoHeader_.biBitCount);
   }
cleanup:
   delete[] palette;
   delete[] pData;
   fileIO_->close();
   return image;
}

void BMPFormat::conv_1u32s(uint8_t const* pSrc, int32_t srcStride, int32_t* pDst, int32_t dstStride,
						   uint32_t destWidth, uint32_t destHeight)
{
   uint32_t absSrcStride = (uint32_t)std::abs(srcStride);
   for(uint32_t y = destHeight; y != 0U; --y)
   {
	  uint32_t destIndex = 0;
	  for(uint32_t srcIndex = 0; srcIndex < absSrcStride; srcIndex++)
	  {
		 uint8_t val = pSrc[srcIndex];
		 for(int32_t ct = 7; ct >= 0; --ct)
		 {
			pDst[destIndex++] = (int32_t)(val >> (ct)) & 1;
			if(destIndex == destWidth)
			   break;
		 }
	  }
	  pSrc += srcStride;
	  pDst += dstStride;
   }
}

void BMPFormat::conv_4u32s(uint8_t const* pSrc, int32_t srcStride, int32_t* pDst, int32_t dstStride,
						   uint32_t destWidth, uint32_t destHeight)
{
   uint32_t absSrcStride = (uint32_t)std::abs(srcStride);
   for(uint32_t y = destHeight; y != 0U; --y)
   {
	  uint32_t destIndex = 0;
	  for(uint32_t srcIndex = 0; srcIndex < absSrcStride; srcIndex++)
	  {
		 uint8_t val = pSrc[srcIndex];
		 for(int32_t ct = 4; ct >= 0; ct -= 4)
		 {
			pDst[destIndex++] = (int32_t)(val >> (ct)) & 0xF;
			if(destIndex == destWidth)
			   break;
		 }
	  }
	  pSrc += srcStride;
	  pDst += dstStride;
   }
}

void BMPFormat::conv_8u32s(uint8_t const* pSrc, int32_t srcStride, int32_t* pDst, int32_t dstStride,
						   uint32_t width, uint32_t height)
{
   for(uint32_t y = height; y != 0U; --y)
   {
	  for(uint32_t x = 0; x < width; x++)
		 pDst[x] = (int32_t)pSrc[x];
	  pSrc += srcStride;
	  pDst += dstStride;
   }
}

void BMPFormat::applyLUT8u_1u32s_C1P3R(uint8_t const* pSrc, int32_t srcStride, int32_t* const* pDst,
									   int32_t const* pDstStride, uint8_t const* const* pLUT,
									   uint32_t destWidth, uint32_t destHeight)
{
   uint32_t absSrcStride = (uint32_t)std::abs(srcStride);
   uint32_t y;
   auto pR = pDst[0];
   auto pG = pDst[1];
   auto pB = pDst[2];
   uint8_t const* pLUT_R = pLUT[0];
   uint8_t const* pLUT_G = pLUT[1];
   uint8_t const* pLUT_B = pLUT[2];

   for(y = destHeight; y != 0U; --y)
   {
	  uint32_t destIndex = 0;
	  for(uint32_t srcIndex = 0; srcIndex < absSrcStride; srcIndex++)
	  {
		 uint8_t idx = pSrc[srcIndex];
		 for(int32_t ct = 7; ct >= 0; ct--)
		 {
			uint8_t val = (idx >> ct) & 0x1;
			pR[destIndex] = (int32_t)pLUT_R[val];
			pG[destIndex] = (int32_t)pLUT_G[val];
			pB[destIndex] = (int32_t)pLUT_B[val];
			destIndex++;
			if(destIndex == destWidth)
			   break;
		 }
	  }
	  pSrc += srcStride;
	  pR += pDstStride[0];
	  pG += pDstStride[1];
	  pB += pDstStride[2];
   }
}

void BMPFormat::applyLUT8u_4u32s_C1P3R(uint8_t const* pSrc, int32_t srcStride, int32_t* const* pDst,
									   int32_t const* pDstStride, uint8_t const* const* pLUT,
									   uint32_t destWidth, uint32_t destHeight)
{
   uint32_t absSrcStride = (uint32_t)std::abs(srcStride);
   uint32_t y;
   auto pR = pDst[0];
   auto pG = pDst[1];
   auto pB = pDst[2];
   uint8_t const* pLUT_R = pLUT[0];
   uint8_t const* pLUT_G = pLUT[1];
   uint8_t const* pLUT_B = pLUT[2];

   for(y = destHeight; y != 0U; --y)
   {
	  uint32_t destIndex = 0;
	  for(uint32_t srcIndex = 0; srcIndex < absSrcStride; srcIndex++)
	  {
		 uint8_t idx = pSrc[srcIndex];
		 for(int32_t ct = 4; ct >= 0; ct -= 4)
		 {
			uint8_t val = (idx >> ct) & 0xF;
			pR[destIndex] = (int32_t)pLUT_R[val];
			pG[destIndex] = (int32_t)pLUT_G[val];
			pB[destIndex] = (int32_t)pLUT_B[val];
			destIndex++;
			if(destIndex == destWidth)
			   break;
		 }
	  }
	  pSrc += srcStride;
	  pR += pDstStride[0];
	  pG += pDstStride[1];
	  pB += pDstStride[2];
   }
}

void BMPFormat::applyLUT8u_8u32s_C1P3R(uint8_t const* pSrc, int32_t srcStride, int32_t* const* pDst,
									   int32_t const* pDstStride, uint8_t const* const* pLUT,
									   uint32_t destWidth, uint32_t destHeight)
{
   uint32_t y;
   auto pR = pDst[0];
   auto pG = pDst[1];
   auto pB = pDst[2];
   uint8_t const* pLUT_R = pLUT[0];
   uint8_t const* pLUT_G = pLUT[1];
   uint8_t const* pLUT_B = pLUT[2];

   for(y = destHeight; y != 0U; --y)
   {
	  for(uint32_t x = 0; x < destWidth; x++)
	  {
		 uint8_t idx = pSrc[x];
		 pR[x] = (int32_t)pLUT_R[idx];
		 pG[x] = (int32_t)pLUT_G[idx];
		 pB[x] = (int32_t)pLUT_B[idx];
	  }
	  pSrc += srcStride;
	  pR += pDstStride[0];
	  pG += pDstStride[1];
	  pB += pDstStride[2];
   }
}
void BMPFormat::bmp24toimage(const uint8_t* pData, uint32_t srcStride, grk_image* image)
{
   uint32_t index;
   uint32_t width, height;
   const uint8_t* pSrc = nullptr;
   width = image->decompress_width;
   height = image->decompress_height;
   index = 0;
   pSrc = pData + (height - 1U) * srcStride;
   uint32_t stride_diff = image->comps[0].stride - image->decompress_width;
   for(uint32_t y = 0; y < height; y++)
   {
	  size_t src_index = 0;
	  for(uint32_t x = 0; x < width; x++)
	  {
		 image->comps[0].data[index] = (int32_t)pSrc[src_index + 2]; /* R */
		 image->comps[1].data[index] = (int32_t)pSrc[src_index + 1]; /* G */
		 image->comps[2].data[index] = (int32_t)pSrc[src_index]; /* B */
		 index++;
		 src_index += 3;
	  }
	  index += stride_diff;
	  pSrc -= srcStride;
   }
}

void BMPFormat::mask_get_shift_and_prec(uint32_t mask, uint8_t* shift, uint8_t* prec)
{
   uint8_t tempShift, tempPrecision;
   tempShift = tempPrecision = 0U;
   if(mask != 0U)
   {
	  while((mask & 1U) == 0U)
	  {
		 mask >>= 1;
		 tempShift++;
	  }
	  while(mask & 1U)
	  {
		 mask >>= 1;
		 tempPrecision++;
	  }
   }
   *shift = tempShift;
   *prec = tempPrecision;
}

void BMPFormat::mask32toimage(const uint8_t* pData, uint32_t srcStride, grk_image* image,
							  uint32_t redMask, uint32_t greenMask, uint32_t blueMask,
							  uint32_t alphaMask)
{
   uint8_t redShift, redPrec;
   uint8_t greenShift, greenPrec;
   uint8_t blueShift, bluePrec;
   uint8_t alphaShift, alphaPrec;

   uint32_t width = image->decompress_width;
   uint32_t stride_diff = image->comps[0].stride - width;
   uint32_t height = image->decompress_height;
   bool hasAlpha = image->decompress_num_comps > 3U;
   mask_get_shift_and_prec(redMask, &redShift, &redPrec);
   mask_get_shift_and_prec(greenMask, &greenShift, &greenPrec);
   mask_get_shift_and_prec(blueMask, &blueShift, &bluePrec);
   mask_get_shift_and_prec(alphaMask, &alphaShift, &alphaPrec);
   image->comps[0].prec = redPrec;
   image->comps[1].prec = greenPrec;
   image->comps[2].prec = bluePrec;
   if(hasAlpha)
	  image->comps[3].prec = alphaPrec;
   uint32_t index = 0;
   uint32_t x, y;
   auto pSrc = pData + (height - 1U) * srcStride;
   for(y = 0; y < height; y++)
   {
	  size_t src_index = 0;
	  for(x = 0; x < width; x++)
	  {
		 uint32_t value = 0U;
		 value |= ((uint32_t)pSrc[src_index]) << 0;
		 value |= ((uint32_t)pSrc[src_index + 1]) << 8;
		 value |= ((uint32_t)pSrc[src_index + 2]) << 16;
		 value |= ((uint32_t)pSrc[src_index + 3]) << 24;

		 image->comps[0].data[index] = (int32_t)((value & redMask) >> redShift); /* R */
		 image->comps[1].data[index] = (int32_t)((value & greenMask) >> greenShift); /* G */
		 image->comps[2].data[index] = (int32_t)((value & blueMask) >> blueShift); /* B */
		 if(hasAlpha)
		 {
			image->comps[3].data[index] = (int32_t)((value & alphaMask) >> alphaShift); /* A */
		 }
		 index++;
		 src_index += 4;
	  }
	  index += stride_diff;
	  pSrc -= srcStride;
   }
}

void BMPFormat::mask16toimage(const uint8_t* pData, uint32_t srcStride, grk_image* image,
							  uint32_t redMask, uint32_t greenMask, uint32_t blueMask,
							  uint32_t alphaMask)
{
   uint8_t redShift, redPrec;
   uint8_t greenShift, greenPrec;
   uint8_t blueShift, bluePrec;
   uint8_t alphaShift, alphaPrec;

   uint32_t width = image->decompress_width;
   uint32_t stride_diff = image->comps[0].stride - width;
   uint32_t height = image->decompress_height;
   bool hasAlpha = image->decompress_num_comps > 3U;
   mask_get_shift_and_prec(redMask, &redShift, &redPrec);
   mask_get_shift_and_prec(greenMask, &greenShift, &greenPrec);
   mask_get_shift_and_prec(blueMask, &blueShift, &bluePrec);
   mask_get_shift_and_prec(alphaMask, &alphaShift, &alphaPrec);
   image->comps[0].prec = redPrec;
   image->comps[1].prec = greenPrec;
   image->comps[2].prec = bluePrec;
   if(hasAlpha)
	  image->comps[3].prec = alphaPrec;
   uint32_t index = 0;
   uint32_t x, y;
   auto pSrc = pData + (height - 1U) * srcStride;
   for(y = 0; y < height; y++)
   {
	  size_t src_index = 0;
	  for(x = 0; x < width; x++)
	  {
		 uint32_t value = ((uint32_t)pSrc[src_index + 0]) << 0;
		 value |= ((uint32_t)pSrc[src_index + 1]) << 8;

		 image->comps[0].data[index] = (int32_t)((value & redMask) >> redShift); /* R */
		 image->comps[1].data[index] = (int32_t)((value & greenMask) >> greenShift); /* G */
		 image->comps[2].data[index] = (int32_t)((value & blueMask) >> blueShift); /* B */
		 if(hasAlpha)
		 {
			image->comps[3].data[index] = (int32_t)((value & alphaMask) >> alphaShift); /* A */
		 }
		 index++;
		 src_index += 2;
	  }
	  index += stride_diff;
	  pSrc -= srcStride;
   }
}
