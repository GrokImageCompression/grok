/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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
#include "grk_apps_config.h"
#include "spdlogwrapper.h"
#include "grok.h"
#include "common.h"

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

// BMP data is little endian
template<typename T>
void get_int(uint8_t** buf, T* val)
{
  T v;
  memcpy(&v, *buf, sizeof(T));
  *val = grk::endian<T>(v, false);
  *buf += sizeof(T);
}

template<typename T>
void put_int(uint8_t** buf, T val)
{
  T v = grk::endian<T>(val, false);
  memcpy(*buf, &v, sizeof(T));
  *buf += sizeof(T);
}

class BMPFormat : public ImageFormat, public IStripReader
{
public:
  BMPFormat(void);
  ~BMPFormat(void) override;
  bool writeHeader(void) override;
  bool writeImage() override;
  bool writeImageBand(uint32_t yBegin, uint32_t yEnd) override;
  using ImageFormat::writeStrip;
  bool writeFinish(void) override;
  std::optional<ImageFileHeader> readHeader(const std::string& filename,
                                            grk_cparameters* parameters) override;
  grk_image* readPixels(grk_cparameters* parameters, grk_data_type dataType) override;
  grk_image* readStripHeader(grk_cparameters* parameters, grk_data_type dataType) override;
  bool pushStrips(grk_object* codec, grk_data_type dataType) override;

private:
  uint64_t off_;
  bool bitfieldMasksValid(void);
  bool setSampleLayout(void);
  grk_image* createImage(grk_cparameters* parameters, grk_data_type dataType, bool allocData);
  std::unique_ptr<uint8_t[]> readAllRows(void);
  template<typename T>
  void convertRows(const uint8_t* fileRows, uint32_t rows, T* const* planes, size_t stride);
  template<typename T>
  bool pushStrips(grk_object* codec);
  bool readFileHeader(GRK_BITMAPFILEHEADER* fileHeader, GRK_BITMAPINFOHEADER* infoHeader);
  bool readInfoHeader(const GRK_BITMAPFILEHEADER* fileHeader, GRK_BITMAPINFOHEADER* infoHeader);
  bool readRawData(uint8_t* pData, uint32_t stride, uint32_t height);
  bool readRle8Data(uint8_t* pData, uint32_t stride, uint32_t width, uint32_t height);
  bool readRle4Data(uint8_t* pData, uint32_t stride, uint32_t width, uint32_t height);

  uint8_t* header_;
  uint64_t srcIndex_;
  GRK_BITMAPFILEHEADER fileHeader_;
  GRK_BITMAPINFOHEADER infoHeader_;
  bool topDown_ = false;
  uint8_t lutRed_[256];
  uint8_t lutGreen_[256];
  uint8_t lutBlue_[256];
  uint32_t paletteNumEntries_ = 0;
  bool paletteHasColour_ = false;
  uint16_t numcomps_ = 0;
  GRK_COLOR_SPACE colourSpace_ = GRK_CLRSPC_UNKNOWN;
  uint32_t bmpStride_ = 0;
  uint32_t masks_[4] = {};

  template<typename T>
  void conv_1u32s(uint8_t const* pSrc, int32_t srcStride, T* pDst, int32_t dstStride,
                  uint32_t destWidth, uint32_t destHeight);
  template<typename T>
  void conv_4u32s(uint8_t const* pSrc, int32_t srcStride, T* pDst, int32_t dstStride,
                  uint32_t destWidth, uint32_t destHeight);
  template<typename T>
  void conv_8u32s(uint8_t const* pSrc, int32_t srcStride, T* pDst, int32_t dstStride,
                  uint32_t width, uint32_t height);

  template<typename T, size_t bytesPerPixel>
  void convertMaskedRows(const uint8_t* rowZero, int32_t rowStep, uint32_t rows, T* const* planes,
                         size_t stride);
  template<typename T>
  void convert24Rows(const uint8_t* rowZero, int32_t rowStep, uint32_t rows, T* const* planes,
                     size_t stride);
  void maskGetShiftAndPrec(uint32_t mask, uint8_t* shift, uint8_t* prec);
};

inline BMPFormat::BMPFormat(void) : off_(0), header_(nullptr), srcIndex_(0)
{
  memset(&fileHeader_, 0, sizeof(GRK_BITMAPFILEHEADER));
  memset(&infoHeader_, 0, sizeof(GRK_BITMAPINFOHEADER));
}

inline BMPFormat::~BMPFormat(void)
{
  delete[] header_;
}

inline bool BMPFormat::writeHeader(void)
{
  if(isHeaderWritten())
    return true;

  if(!openFile())
    return false;

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

  if(!allComponentsSanityCheck(image_, true))
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

  colours_used = (image_->decompress_num_comps >= 3) ? 0 : 256;
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
  put_int(&header_ptr, full_header_size + lut_size + image_size + icc_size);
  // reserved
  put_int(&header_ptr, 0U);
  put_int(&header_ptr, full_header_size + lut_size);
  /* INFO HEADER   */
  put_int(&header_ptr, info_header_size);
  put_int(&header_ptr, w);
  put_int(&header_ptr, h);
  put_int(&header_ptr, (uint16_t)1);
  put_int(&header_ptr, (uint16_t)(image_->decompress_num_comps * 8));
  put_int(&header_ptr, 0U);
  put_int(&header_ptr, image_size);
  for(uint32_t i = 0; i < 2; ++i)
  {
    double cap = (image_->capture_resolution[i] != 0) ? image_->capture_resolution[i] : 7834.0;
    put_int(&header_ptr, (uint32_t)(cap + 0.5f));
  }
  put_int(&header_ptr, colours_used);
  put_int(&header_ptr, colours_used);
  if(image_->meta && image_->meta->color.icc_profile_buf)
  {
    put_int(&header_ptr, 0U);
    put_int(&header_ptr, 0U);
    put_int(&header_ptr, 0U);
    put_int(&header_ptr, 0U);
    put_int(&header_ptr, ICC_PROFILE_EMBEDDED);
    memset(header_ptr, 0, 36);
    header_ptr += 36;
    put_int(&header_ptr, 0U);
    put_int(&header_ptr, 0U);
    put_int(&header_ptr, 0U);
    put_int(&header_ptr, 0U);
    put_int(&header_ptr, info_header_size + lut_size + image_size);
    put_int(&header_ptr, image_->meta->color.icc_profile_len);
    put_int(&header_ptr, 0U);
  }
  // 1024-byte LUT
  if(image_->decompress_num_comps == 1)
  {
    bool writePalette = image_->meta && image_->meta->color.palette &&
                        image_->meta->color.palette->component_mapping && !image_->apply_palette &&
                        image_->meta->color.palette->num_channels == 3;
    if(writePalette)
    {
      auto pal = image_->meta->color.palette;
      uint32_t numEntries = (pal->num_entries > 256) ? 256 : pal->num_entries;
      for(uint32_t i = 0; i < numEntries; i++)
      {
        uint8_t bgr[3];
        for(uint8_t c = 0; c < 3; ++c)
        {
          int32_t v = pal->lut[i * pal->num_channels + c];
          uint8_t cprec = pal->channel_prec[c];
          uint32_t uv = (uint32_t)v;
          if(cprec < 8)
            uv <<= (8 - cprec);
          else if(cprec > 8)
            uv >>= (cprec - 8);
          bgr[c] = (uint8_t)uv;
        }
        *header_ptr++ = bgr[2]; // B
        *header_ptr++ = bgr[1]; // G
        *header_ptr++ = bgr[0]; // R
        *header_ptr++ = 0;
      }
      for(uint32_t i = numEntries; i < 256; i++)
      {
        *header_ptr++ = 0;
        *header_ptr++ = 0;
        *header_ptr++ = 0;
        *header_ptr++ = 0;
      }
    }
    else
    {
      for(uint32_t i = 0; i < 256; i++)
      {
        *header_ptr++ = (uint8_t)i;
        *header_ptr++ = (uint8_t)i;
        *header_ptr++ = (uint8_t)i;
        *header_ptr++ = 0;
      }
    }
  }
  destBuff.data = header_;
  destBuff.offset = off_;
  destBuff.pooled = false;
  destBuff.len = header_plus_lut;
  if(write(destBuff) != destBuff.len)
    goto cleanup;
  off_ += header_plus_lut;
  ret = true;
  writeState_ = IMAGE_FORMAT_HEADER_WRITTEN;
cleanup:

  return ret;
}

inline bool BMPFormat::writeImage()
{
  if(writeState_ & IMAGE_FORMAT_PIXELS_WRITTEN)
    return true;
  if(!isHeaderWritten())
  {
    if(!writeHeader())
      return false;
  }
  return writeImageBand(0, image_->decompress_height);
}

inline bool BMPFormat::writeImageBand(uint32_t yBegin, uint32_t yEnd)
{
  // BMP writes rows bottom-up — partial bands are not supported
  if(yBegin != 0 || yEnd != image_->decompress_height)
  {
    spdlog::error("BMPFormat: partial band writing not supported (BMP is bottom-up)");
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
    uint8_t* ptr = destBuff.data + w_dest - pad_dest;
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
        bool isInt16 = image_->comps[0].data_type == GRK_INT_16;
        for(uint16_t compno = 0; compno < decompress_num_comps; ++compno)
        {
          int32_t r;
          if(isInt16)
            r = ((int16_t*)image_->comps[compno].data)[srcIndex_ + i];
          else
            r = ((int32_t*)image_->comps[compno].data)[srcIndex_ + i];
          r += shift[compno];
          if(scaleType[compno] == 1)
            r *= scale[compno];
          else if(scaleType[compno] == 2)
            r /= scale[compno];
          rc[compno] = (uint8_t)r;
        }
        if(decompress_num_comps == 1)
        {
          destBuff.data[destInd++] = rc[0];
        }
        else
        {
          destBuff.data[destInd++] = rc[2];
          destBuff.data[destInd++] = rc[1];
          destBuff.data[destInd++] = rc[0];
          if(decompress_num_comps == 4)
            destBuff.data[destInd++] = rc[3];
        }
      }
      destInd += pad_dest;
      // todo figure out why srcIndex sometimes exceeds stride_src
      // assert(srcIndex_ >= stride_src);
      srcIndex_ -= stride_src;
    }
    destBuff.offset = off_;
    destBuff.pooled = true;
    destBuff.len = destInd;
    if(write(destBuff) != destBuff.len)
      goto cleanup;
    destBuff = pool.get(packedLen);
    // pooled buffer may not have been zero-padded
    off_ += destInd;
    rowCount += k_max;
  }

  ret = true;
cleanup:
  pool.put(destBuff);

  return ret;
}

inline bool BMPFormat::writeFinish(void)
{
  if(image_->meta && image_->meta->color.icc_profile_buf)
  {
    GrkIOBuf destBuff;
    destBuff.data = image_->meta->color.icc_profile_buf;
    destBuff.offset = off_;
    destBuff.pooled = false;
    destBuff.len = image_->meta->color.icc_profile_len;
    if(write(destBuff) != destBuff.len)
      return false;
    off_ += image_->meta->color.icc_profile_len;
  }

  return ImageFormat::writeFinish();
}

inline bool BMPFormat::readFileHeader(GRK_BITMAPFILEHEADER* fileHeader,
                                      GRK_BITMAPINFOHEADER* infoHeader)
{
  memset(infoHeader, 0, sizeof(*infoHeader));
  const size_t len = fileHeaderSize + sizeof(uint32_t);
  uint8_t temp[len];
  uint8_t* temp_ptr = temp;
  if(!read(temp, len))
    return false;
  get_int(&temp_ptr, &fileHeader->bfType);
  if(fileHeader->bfType != 19778)
  {
    spdlog::error("Not a BMP file");
    return false;
  }
  get_int(&temp_ptr, &fileHeader->bfSize);
  get_int(&temp_ptr, &fileHeader->bfReserved1);
  get_int(&temp_ptr, &fileHeader->bfReserved2);
  get_int(&temp_ptr, &fileHeader->bfOffBits);
  get_int(&temp_ptr, &infoHeader->biSize);

  return true;
}

inline bool BMPFormat::readInfoHeader(const GRK_BITMAPFILEHEADER* fileHeader,
                                      GRK_BITMAPINFOHEADER* infoHeader)
{
  // Validate biSize against the legal header-size set BEFORE using it to size
  // the read: an unvalidated biSize either underflows len_initial (biSize < 4)
  // or overruns the fixed temp buffer (biSize > sizeof temp), smashing the stack.
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
  const size_t len_initial = infoHeader->biSize - sizeof(uint32_t);
  uint8_t temp[sizeof(GRK_BITMAPINFOHEADER)];
  uint8_t* temp_ptr = temp;
  if(len_initial > sizeof(temp))
    return false;
  if(!read(temp, len_initial))
    return false;
  bool is_os2 = infoHeader->biSize == BITMAPCOREHEADER_LENGTH;
  if(is_os2)
  { // OS2
    int16_t val;
    get_int(&temp_ptr, &val);
    infoHeader->biWidth = val;
    get_int(&temp_ptr, &val);
    infoHeader->biHeight = val;
  }
  else
  {
    get_int(&temp_ptr, &infoHeader->biWidth);
    get_int(&temp_ptr, &infoHeader->biHeight);
  }
  get_int(&temp_ptr, &infoHeader->biPlanes);
  get_int(&temp_ptr, &infoHeader->biBitCount);
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
    get_int(&temp_ptr, &infoHeader->biXpelsPerMeter);
    get_int(&temp_ptr, &infoHeader->biYpelsPerMeter);
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
    get_int(&temp_ptr, &infoHeader->biAlphaMask);
  }
  if(infoHeader->biSize >= BITMAPV4HEADER_LENGTH)
  {
    get_int(&temp_ptr, &infoHeader->biColorSpaceType);
    memcpy(infoHeader->biColorSpaceEP, temp_ptr, sizeof(infoHeader->biColorSpaceEP));
    temp_ptr += sizeof(infoHeader->biColorSpaceEP);
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

inline bool BMPFormat::readRawData(uint8_t* pData, uint32_t stride, uint32_t height)
{
  return read(pData, (size_t)stride * height);
}

inline bool BMPFormat::readRle8Data(uint8_t* pData, uint32_t stride, uint32_t width,
                                    uint32_t height)
{
  uint32_t x = 0, y = 0, written = 0;
  uint8_t* pix = nullptr;
  const uint8_t* beyond = nullptr;
  const uint8_t* pixels_ptr = nullptr;
  const uint8_t* pixels_end = nullptr;
  bool rc = false;
  auto pixels = new uint8_t[infoHeader_.biSizeImage];
  if(!read(pixels, infoHeader_.biSizeImage))
  {
    goto cleanup;
  }
  pixels_ptr = pixels;
  pixels_end = pixels + infoHeader_.biSizeImage;
  beyond = pData + (size_t)stride * height;
  pix = pData;

  while(y < height)
  {
    // every read below must stay within the biSizeImage-sized input buffer
    if(pixels_ptr >= pixels_end)
      goto cleanup;
    int c = *pixels_ptr++;
    if(c)
    {
      int j;
      if(pixels_ptr >= pixels_end)
        goto cleanup;
      uint8_t c1 = *pixels_ptr++;
      for(j = 0; (j < c) && (x < width) && ((size_t)pix < (size_t)beyond); j++, x++, pix++)
      {
        *pix = c1;
        written++;
      }
    }
    else
    {
      if(pixels_ptr >= pixels_end)
        goto cleanup;
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
        if(pixels_ptr + 2 > pixels_end)
          goto cleanup;
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
          if(pixels_ptr >= pixels_end)
            goto cleanup;
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

inline bool BMPFormat::readRle4Data(uint8_t* pData, uint32_t stride, uint32_t width,
                                    uint32_t height)
{
  uint32_t x = 0, y = 0, written = 0;
  uint8_t* pix = nullptr;
  const uint8_t* pixels_ptr = nullptr;
  const uint8_t* pixels_end = nullptr;
  const uint8_t* beyond = nullptr;
  bool rc = false;
  auto pixels = new uint8_t[infoHeader_.biSizeImage];
  if(!read(pixels, infoHeader_.biSizeImage))
    goto cleanup;
  pixels_ptr = pixels;
  pixels_end = pixels + infoHeader_.biSizeImage;
  beyond = pData + (size_t)stride * height;
  pix = pData;
  while(y < height)
  {
    // every read below must stay within the biSizeImage-sized input buffer
    if(pixels_ptr >= pixels_end)
      goto cleanup;
    int c = *pixels_ptr++;
    if(c)
    { /* encoded mode */
      int j;
      if(pixels_ptr >= pixels_end)
        goto cleanup;
      uint8_t c1 = *pixels_ptr++;

      for(j = 0; (j < c) && (x < width) && ((size_t)pix < (size_t)beyond); j++, x++, pix++)
      {
        *pix = (uint8_t)((j & 1) ? (c1 & 0x0fU) : ((uint8_t)(c1 >> 4) & 0x0fU));
        written++;
      }
    }
    else
    { /* absolute mode */
      if(pixels_ptr >= pixels_end)
        goto cleanup;
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
        if(pixels_ptr + 2 > pixels_end)
          goto cleanup;
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
          {
            if(pixels_ptr >= pixels_end)
              goto cleanup;
            c1 = *pixels_ptr++;
          }
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

inline std::optional<ImageFileHeader>
    BMPFormat::readHeader(const std::string& fname, [[maybe_unused]] grk_cparameters* parameters)
{
  bool is_os2 = false;
  uint8_t palette_has_colour = 0U;
  uint8_t maxPrecision = 0;

  image_ = nullptr;
  topDown_ = false;
  paletteNumEntries_ = 0;
  numcomps_ = 1U;
  if(!open(fname, "r"))
    return std::nullopt;

  if(!readFileHeader(&fileHeader_, &infoHeader_))
    goto cleanup;
  if(!readInfoHeader(&fileHeader_, &infoHeader_))
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
    topDown_ = true;
    infoHeader_.biHeight = -infoHeader_.biHeight;
  }
  /* Load palette */
  if(infoHeader_.biBitCount <= 8U)
  {
    memset(lutRed_, 0, sizeof(lutRed_));
    memset(lutGreen_, 0, sizeof(lutGreen_));
    memset(lutBlue_, 0, sizeof(lutBlue_));

    paletteNumEntries_ = infoHeader_.biClrUsed;
    // need to check this a second time for OS2 files
    if(paletteNumEntries_ == 0U)
      paletteNumEntries_ = (1U << infoHeader_.biBitCount);
    else if(paletteNumEntries_ > 256U)
      paletteNumEntries_ = 256U;

    const uint32_t palette_bytes =
        paletteNumEntries_ * (is_os2 ? os2_palette_element_len : palette_element_len);
    std::vector<uint8_t> palette(palette_bytes);
    if(!read(palette.data(), palette_bytes))
      goto cleanup;
    const uint8_t* pal_ptr = palette.data();

    if(paletteNumEntries_ > 0U)
    {
      for(uint32_t i = 0U; i < paletteNumEntries_; i++)
      {
        lutBlue_[i] = *pal_ptr++;
        lutGreen_[i] = *pal_ptr++;
        lutRed_[i] = *pal_ptr++;
        if(!is_os2)
          pal_ptr++;
        palette_has_colour |= (lutBlue_[i] ^ lutGreen_[i]) | (lutGreen_[i] ^ lutRed_[i]);
      }
      if(palette_has_colour)
        numcomps_ = 3U;
    }
  }
  else
  {
    numcomps_ = 3U;
    if((infoHeader_.biCompression == 3) && (infoHeader_.biAlphaMask != 0U))
      numcomps_++;
  }
  paletteHasColour_ = palette_has_colour != 0;

  if(infoHeader_.biWidth == 0 || infoHeader_.biHeight == 0)
    goto cleanup;
  if(!setSampleLayout())
    goto cleanup;

  colourSpace_ = (numcomps_ == 1U) ? GRK_CLRSPC_GRAY : GRK_CLRSPC_SRGB;
  if(infoHeader_.biBitCount <= 8U && paletteHasColour_)
    numcomps_ = 1;

  maxPrecision = (numcomps_ == 1U) ? (uint8_t)infoHeader_.biBitCount : 8U;
  if(infoHeader_.biCompression == 3)
  {
    for(uint32_t mask : {infoHeader_.biRedMask, infoHeader_.biGreenMask, infoHeader_.biBlueMask,
                         infoHeader_.biAlphaMask})
    {
      uint8_t shift, prec;
      maskGetShiftAndPrec(mask, &shift, &prec);
      maxPrecision = std::max(maxPrecision, prec);
    }
  }
  return ImageFileHeader{numcomps_, maxPrecision};
cleanup:
  fileIO_->close();
  return std::nullopt;
}

inline bool BMPFormat::bitfieldMasksValid(void)
{
  if(!(masks_[0] && masks_[1] && masks_[2]))
  {
    spdlog::error("RGB(A) bit masks must be non-zero");
    return false;
  }
  bool fail = false;
  bool hasAlpha = numcomps_ > 3;
  const uint32_t* m = masks_;
  for(uint32_t i = 0; i < numcomps_; ++i)
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
  }
  return !fail;
}

inline bool BMPFormat::setSampleLayout(void)
{
  uint32_t bitCount = infoHeader_.biBitCount;
  uint32_t compression = infoHeader_.biCompression;
  bool uncompressed = compression == 0 && (bitCount == 32 || bitCount == 24 || bitCount == 16 ||
                                           bitCount == 8 || bitCount == 4 || bitCount == 1);
  bool runLengthEncoded =
      (compression == 1 && bitCount == 8) || (compression == 2 && bitCount == 4);
  bool bitfields = compression == 3 && (bitCount == 32 || bitCount == 16);
  if(!uncompressed && !runLengthEncoded && !bitfields)
  {
    spdlog::error("Precision [{}] does not match supported precision: "
                  "24 bit RGB, 8 bit RGB, 4/8 bit RLE and 16/32 bit BITFIELD",
                  bitCount);
    return false;
  }
  if(uncompressed && bitCount == 32)
  {
    masks_[0] = 0x00FF0000U;
    masks_[1] = 0x0000FF00U;
    masks_[2] = 0x000000FFU;
  }
  else if(uncompressed && bitCount == 16)
  {
    masks_[0] = 0x7C00U;
    masks_[1] = 0x03E0U;
    masks_[2] = 0x001FU;
  }
  else if(bitfields)
  {
    masks_[0] = infoHeader_.biRedMask;
    masks_[1] = infoHeader_.biGreenMask;
    masks_[2] = infoHeader_.biBlueMask;
    masks_[3] = infoHeader_.biAlphaMask;
    if(bitCount == 16 && !masks_[0] && !masks_[1] && !masks_[2])
    {
      masks_[0] = 0xF800U;
      masks_[1] = 0x07E0U;
      masks_[2] = 0x001FU;
    }
    if(bitCount == 32 && !bitfieldMasksValid())
      return false;
  }
  auto width = (uint32_t)infoHeader_.biWidth;
  uint32_t strideBits = compression == 2 ? 8U : bitCount;
  if(strideBits > (UINT32_MAX - 31U) / width)
    return false;
  bmpStride_ = ((width * strideBits + 31U) / 32U) * (uint32_t)sizeof(uint32_t);
  return bmpStride_ <= UINT32_MAX / (uint32_t)infoHeader_.biHeight;
}

inline grk_image* BMPFormat::createImage(grk_cparameters* parameters, grk_data_type dataType,
                                         bool allocData)
{
  grk_image_comp cmptparm[4]; /* maximum of 4 components */
  memset(&cmptparm[0], 0, sizeof(cmptparm));
  for(uint32_t i = 0; i < numcomps_; i++)
  {
    auto img_comp = cmptparm + i;
    uint8_t maskShift, maskPrecision;
    maskGetShiftAndPrec(masks_[i], &maskShift, &maskPrecision);
    img_comp->prec = (numcomps_ == 1U) ? (uint8_t)infoHeader_.biBitCount : 8U;
    if(masks_[0])
      img_comp->prec = maskPrecision;
    img_comp->sgnd = false;
    img_comp->data_type = dataType;
    img_comp->dx = parameters->subsampling_dx;
    img_comp->dy = parameters->subsampling_dy;
    img_comp->w = grk::ceildiv<uint32_t>((uint32_t)infoHeader_.biWidth, img_comp->dx);
    img_comp->h = grk::ceildiv<uint32_t>((uint32_t)infoHeader_.biHeight, img_comp->dy);
  }

  auto image = grk_image_new(numcomps_, &cmptparm[0], colourSpace_, allocData);
  if(!image)
    return nullptr;

  if(infoHeader_.biBitCount <= 8U)
  {
    uint8_t num_channels = paletteHasColour_ ? 3U : 1U;
    createMeta(image);
    auto meta = image->meta;
    allocPalette(&meta->color, num_channels, (uint16_t)paletteNumEntries_);
    auto cmap = new _grk_component_mapping_comp[num_channels];
    for(uint8_t i = 0; i < num_channels; ++i)
    {
      cmap[i].component = 0;
      cmap[i].mapping_type = 1;
      cmap[i].palette_column = i;
      meta->color.palette->channel_prec[i] = 8U;
      meta->color.palette->channel_sign[i] = false;
    }
    meta->color.palette->component_mapping = cmap;
    auto lut_ptr = meta->color.palette->lut;
    for(uint16_t i = 0; i < paletteNumEntries_; i++)
    {
      *lut_ptr++ = lutRed_[i];
      if(num_channels == 3)
      {
        *lut_ptr++ = lutGreen_[i];
        *lut_ptr++ = lutBlue_[i];
      }
    }
  }
  if(infoHeader_.biSize == sizeof(GRK_BITMAPINFOHEADER) &&
     infoHeader_.biColorSpaceType == ICC_PROFILE_EMBEDDED && infoHeader_.biIccProfileSize &&
     infoHeader_.biIccProfileSize < grk::maxICCProfileBufferLen)
  {
    std::vector<uint8_t> iccbuf(infoHeader_.biIccProfileSize);
    if(seek(fileHeaderSize + infoHeader_.biIccProfileOffset, SEEK_SET) &&
       read(iccbuf.data(), iccbuf.size()))
      copyICC(image, iccbuf.data(), infoHeader_.biIccProfileSize);
    else
      spdlog::warn("Unable to read full ICC profile. Profile will be ignored.");
  }
  if(numcomps_ == 4U)
  {
    image->comps[3].type = GRK_CHANNEL_TYPE_OPACITY;
    image->comps[3].association = GRK_CHANNEL_ASSOC_WHOLE_IMAGE;
  }

  image->x0 = parameters->image_offset_x0;
  image->y0 = parameters->image_offset_y0;
  image->x1 = image->x0 + ((uint32_t)infoHeader_.biWidth - 1U) * parameters->subsampling_dx + 1U;
  image->y1 = image->y0 + ((uint32_t)infoHeader_.biHeight - 1U) * parameters->subsampling_dy + 1U;
  return image;
}

inline std::unique_ptr<uint8_t[]> BMPFormat::readAllRows(void)
{
  auto width = (uint32_t)infoHeader_.biWidth;
  auto height = (uint32_t)infoHeader_.biHeight;
  std::unique_ptr<uint8_t[]> fileRows(new uint8_t[(size_t)bmpStride_ * height]);
  if(!seek(fileHeader_.bfOffBits, SEEK_SET))
    return nullptr;
  bool read;
  switch(infoHeader_.biCompression)
  {
    case 1:
      read = readRle8Data(fileRows.get(), bmpStride_, width, height);
      break;
    case 2:
      read = readRle4Data(fileRows.get(), bmpStride_, width, height);
      break;
    default:
      read = readRawData(fileRows.get(), bmpStride_, height);
      break;
  }
  return read ? std::move(fileRows) : nullptr;
}

template<typename T>
void BMPFormat::convertRows(const uint8_t* fileRows, uint32_t rows, T* const* planes, size_t stride)
{
  auto width = (uint32_t)infoHeader_.biWidth;
  auto rowZero = topDown_ ? fileRows : fileRows + (size_t)(rows - 1U) * bmpStride_;
  int32_t rowStep = topDown_ ? (int32_t)bmpStride_ : -(int32_t)bmpStride_;
  switch(infoHeader_.biBitCount)
  {
    case 32:
      convertMaskedRows<T, 4>(rowZero, rowStep, rows, planes, stride);
      break;
    case 24:
      convert24Rows<T>(rowZero, rowStep, rows, planes, stride);
      break;
    case 16:
      convertMaskedRows<T, 2>(rowZero, rowStep, rows, planes, stride);
      break;
    case 8:
      conv_8u32s(rowZero, rowStep, planes[0], (int32_t)stride, width, rows);
      break;
    case 4:
      if(infoHeader_.biCompression == 2)
        conv_8u32s(rowZero, rowStep, planes[0], (int32_t)stride, width, rows);
      else
        conv_4u32s(rowZero, rowStep, planes[0], (int32_t)stride, width, rows);
      break;
    case 1:
      conv_1u32s(rowZero, rowStep, planes[0], (int32_t)stride, width, rows);
      break;
  }
}

inline grk_image* BMPFormat::readPixels(grk_cparameters* parameters, grk_data_type dataType)
{
  auto image = createImage(parameters, dataType, true);
  auto fileRows = image ? readAllRows() : nullptr;
  bool read = fileRows && readSamplesAs(dataType, [&](auto sample) {
                using T = decltype(sample);
                T* planes[4];
                for(uint16_t c = 0; c < numcomps_; ++c)
                  planes[c] = (T*)image->comps[c].data;
                convertRows<T>(fileRows.get(), (uint32_t)infoHeader_.biHeight, planes,
                               image->comps[0].stride);
                return true;
              });
  if(!fileIO_->close() || !read)
  {
    if(image)
      grk_object_unref(&image->obj);
    return nullptr;
  }
  return image;
}

inline grk_image* BMPFormat::readStripHeader(grk_cparameters* parameters, grk_data_type dataType)
{
  return createImage(parameters, dataType, false);
}

template<typename T>
bool BMPFormat::pushStrips(grk_object* codec)
{
  auto width = (uint32_t)infoHeader_.biWidth;
  auto height = (uint32_t)infoHeader_.biHeight;
  uint32_t rowsPerStrip = (std::min)(stripReadRows, height);
  bool runLengthEncoded = infoHeader_.biCompression == 1 || infoHeader_.biCompression == 2;
  std::unique_ptr<uint8_t[]> decodedRows;
  if(runLengthEncoded)
  {
    decodedRows = readAllRows();
    if(!decodedRows)
    {
      fileIO_->close();
      return false;
    }
  }
  std::vector<uint8_t> stripBytes;
  std::vector<T*> planePointers(numcomps_);
  auto readStrip = [&](uint32_t strip, const std::vector<T*>& planes) {
    uint32_t firstRow = strip * rowsPerStrip;
    uint32_t rows = (std::min)(rowsPerStrip, height - firstRow);
    uint32_t firstFileRow = topDown_ ? firstRow : height - firstRow - rows;
    size_t fileOffset = (size_t)firstFileRow * bmpStride_;
    for(uint16_t c = 0; c < numcomps_; ++c)
      planePointers[c] = planes[c];
    if(runLengthEncoded)
    {
      convertRows<T>(decodedRows.get() + fileOffset, rows, planePointers.data(), width);
      return true;
    }
    stripBytes.resize((size_t)rows * bmpStride_);
    if(!seek((int64_t)(fileHeader_.bfOffBits + fileOffset), SEEK_SET) ||
       !read(stripBytes.data(), stripBytes.size()))
      return false;
    convertRows<T>(stripBytes.data(), rows, planePointers.data(), width);
    return true;
  };
  bool pushed = pushStripsReadAhead<T>(codec, numcomps_, width, height, rowsPerStrip, readStrip);
  return fileIO_->close() && pushed;
}

inline bool BMPFormat::pushStrips(grk_object* codec, grk_data_type dataType)
{
  return readSamplesAs(dataType, [&](auto sample) { return pushStrips<decltype(sample)>(codec); });
}

template<typename T>
void BMPFormat::conv_1u32s(uint8_t const* pSrc, int32_t srcStride, T* pDst, int32_t dstStride,
                           uint32_t destWidth, uint32_t destHeight)
{
  uint32_t absSrcStride = (uint32_t)std::abs(srcStride);
  for(uint32_t y = destHeight; y != 0U; --y)
  {
    uint32_t destIndex = 0;
    for(uint32_t srcIndex = 0; srcIndex < absSrcStride && destIndex < destWidth; srcIndex++)
    {
      uint8_t val = pSrc[srcIndex];
      for(int32_t ct = 7; ct >= 0; --ct)
      {
        pDst[destIndex++] = (T)(val >> (ct)) & 1;
        if(destIndex == destWidth)
          break;
      }
    }
    pSrc += srcStride;
    pDst += dstStride;
  }
}

template<typename T>
void BMPFormat::conv_4u32s(uint8_t const* pSrc, int32_t srcStride, T* pDst, int32_t dstStride,
                           uint32_t destWidth, uint32_t destHeight)
{
  uint32_t absSrcStride = (uint32_t)std::abs(srcStride);
  for(uint32_t y = destHeight; y != 0U; --y)
  {
    uint32_t destIndex = 0;
    for(uint32_t srcIndex = 0; srcIndex < absSrcStride && destIndex < destWidth; srcIndex++)
    {
      uint8_t val = pSrc[srcIndex];
      for(int32_t ct = 4; ct >= 0; ct -= 4)
      {
        pDst[destIndex++] = (T)(val >> (ct)) & 0xF;
        if(destIndex == destWidth)
          break;
      }
    }
    pSrc += srcStride;
    pDst += dstStride;
  }
}

template<typename T>
void BMPFormat::conv_8u32s(uint8_t const* pSrc, int32_t srcStride, T* pDst, int32_t dstStride,
                           uint32_t width, uint32_t height)
{
  for(uint32_t y = height; y != 0U; --y)
  {
    for(uint32_t x = 0; x < width; x++)
      pDst[x] = (T)pSrc[x];
    pSrc += srcStride;
    pDst += dstStride;
  }
}

inline void BMPFormat::maskGetShiftAndPrec(uint32_t mask, uint8_t* shift, uint8_t* prec)
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

template<typename T, size_t bytesPerPixel>
void BMPFormat::convertMaskedRows(const uint8_t* rowZero, int32_t rowStep, uint32_t rows,
                                  T* const* planes, size_t stride)
{
  auto width = (uint32_t)infoHeader_.biWidth;
  uint8_t shifts[4];
  uint8_t precisions[4];
  for(uint16_t c = 0; c < 4; ++c)
    maskGetShiftAndPrec(masks_[c], shifts + c, precisions + c);
  bool hasAlpha = numcomps_ > 3U;
  auto source = rowZero;
  for(uint32_t y = 0; y < rows; ++y, source += rowStep)
  {
    T* r = planes[0] + y * stride;
    T* g = planes[1] + y * stride;
    T* b = planes[2] + y * stride;
    T* alpha = hasAlpha ? planes[3] + y * stride : nullptr;
    const uint8_t* pixel = source;
    for(uint32_t x = 0; x < width; ++x, pixel += bytesPerPixel)
    {
      uint32_t value = 0U;
      for(size_t i = 0; i < bytesPerPixel; ++i)
        value |= (uint32_t)pixel[i] << (8 * i);
      r[x] = (T)((value & masks_[0]) >> shifts[0]);
      g[x] = (T)((value & masks_[1]) >> shifts[1]);
      b[x] = (T)((value & masks_[2]) >> shifts[2]);
      if(hasAlpha)
        alpha[x] = (T)((value & masks_[3]) >> shifts[3]);
    }
  }
}

template<typename T>
void BMPFormat::convert24Rows(const uint8_t* rowZero, int32_t rowStep, uint32_t rows,
                              T* const* planes, size_t stride)
{
  auto width = (uint32_t)infoHeader_.biWidth;
  auto source = rowZero;
  for(uint32_t y = 0; y < rows; ++y, source += rowStep)
  {
    T* r = planes[0] + y * stride;
    T* g = planes[1] + y * stride;
    T* b = planes[2] + y * stride;
    const uint8_t* pixel = source;
    for(uint32_t x = 0; x < width; ++x, pixel += 3)
    {
      // bmp stores blue, green, red
      r[x] = (T)pixel[2];
      g[x] = (T)pixel[1];
      b[x] = (T)pixel[0];
    }
  }
}
