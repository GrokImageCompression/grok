/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include "grk_apps_config.h"
#ifdef GROK_HAVE_LIBTIFF

#ifdef GRK_BUILD_LIBTIFF
#include "tiffiop.h"
#endif

#if !defined(_WIN32) && defined(GRK_BUILD_LIBTIFF)
#define GRK_CUSTOM_TIFF_IO
#endif

#include "ImageFormat.h"
#include <tiffio.h>
#include "grok.h"
#include "spdlogwrapper.h"
#include "convert.h"
#include "common.h"

/* TIFF conversion*/
void tiffSetErrorAndWarningHandlers(bool verbose);

template<typename T>
class TIFFFormat : public ImageFormat
{
public:
  TIFFFormat();
  ~TIFFFormat() override;
  void registerGrkReclaimCallback(grk_io_init io_init, grk_io_callback reclaim_callback,
                                  void* user_data) override;

  bool encodeInit(grk_image* image, const std::string& filename, uint32_t compression_level,
                  uint32_t concurrency) override;
  bool encodeHeader(void) override;
  /***
   * application-orchestrated pixel encoding
   */
  bool encodePixels() override;
  /***
   * library-orchestrated pixel encoding
   */
  virtual bool encodePixels(uint32_t workerId, grk_io_buf pixels) override;
  bool encodeFinish(void) override;
  grk_image* decode(const std::string& filename, grk_cparameters* parameters) override;

private:
#ifdef GRK_CUSTOM_TIFF_IO
  TIFF* MyTIFFOpen(const char* name, const char* mode);
#endif
  bool encodeHeader(TIFF* tif);
  /***
   * Common core pixel encoding write to disk
   */
  bool encodePixelsCoreWrite(grk_io_buf pixels) override;

  bool readTiffPixels(TIFF* tif, grk_image_comp* comps, uint16_t numcomps, uint16_t tiSpp,
                      uint16_t tiPC, uint16_t tiPhoto, uint32_t chroma_subsample_x,
                      uint32_t chroma_subsample_y);

  template<typename RT>
  bool readTiffPixelsSigned(TIFF* tif, grk_image_comp* comps, uint16_t numcomps, uint16_t tiSpp,
                            uint16_t tiPC);
  TIFF* tif_;
  uint32_t chroma_subsample_x;
  uint32_t chroma_subsample_y;
  size_t units;
  grk_io_callback grkReclaimCallback_;
  void* grkReclaimUserData_;
};

#ifdef GRK_CUSTOM_TIFF_IO

static inline tmsize_t TiffRead([[maybe_unused]] thandle_t handle, [[maybe_unused]] void* buf,
                                tmsize_t size)
{
  return size;
}
static inline tmsize_t TiffWrite(thandle_t handle, void* buf, tmsize_t size)
{
  auto* serializer = static_cast<FileOrchestratorIO*>(handle);
  const size_t bytes_total = (size_t)size;
  if((tmsize_t)bytes_total != size)
  {
    errno = EINVAL;
    return (tmsize_t)-1;
  }

  if(serializer->write((uint8_t*)buf, bytes_total) == bytes_total)
    return size;
  else
    return (tmsize_t)-1;
}

static inline uint64_t TiffSeek(thandle_t handle, uint64_t off, int32_t whence)
{
  auto* serializer = static_cast<FileOrchestratorIO*>(handle);
  _TIFF_off_t off_io = (_TIFF_off_t)off;

  if((uint64_t)off_io != off)
  {
    errno = EINVAL;
    return (uint64_t)-1;
  }

  return serializer->seek((int64_t)off, whence);
}

static inline int TiffClose(thandle_t handle)
{
  auto* serializer = static_cast<FileOrchestratorIO*>(handle);

  return serializer->close() ? 0 : EINVAL;
}

static inline uint64_t TiffSize([[maybe_unused]] thandle_t handle)
{
  return 0U;
}

template<typename T>
TIFF* TIFFFormat<T>::MyTIFFOpen(const char* name, const char* mode)
{
  if(!orchestrator.open(name, mode, true))
    return ((TIFF*)0);
  auto tif = TIFFClientOpen(name, mode, &orchestrator, TiffRead, TiffWrite, TiffSeek, TiffClose,
                            TiffSize, nullptr, nullptr);
  if(!tif)
    orchestrator.close();

  return tif;
}

#endif

template<typename T>
TIFFFormat<T>::TIFFFormat()
    : tif_(nullptr), chroma_subsample_x(1), chroma_subsample_y(1), units(0),
      grkReclaimCallback_(nullptr), grkReclaimUserData_(nullptr)

{}

template<typename T>
TIFFFormat<T>::~TIFFFormat()
{
  if(tif_)
    TIFFClose(tif_);
}

template<typename T>
void TIFFFormat<T>::registerGrkReclaimCallback(grk_io_init io_init,
                                               grk_io_callback reclaim_callback, void* user_data)
{
  grkReclaimCallback_ = reclaim_callback;
  grkReclaimUserData_ = user_data;
  if(io_init.max_pooled_requests)
    orchestrator.setMaxPooledRequests(io_init.max_pooled_requests);
}

template<typename T>
bool TIFFFormat<T>::encodeInit(grk_image* image, const std::string& filename,
                               uint32_t compression_level, uint32_t concurrency)
{
  if(encodeState & IMAGE_FORMAT_ENCODED_PIXELS)
  {
    assert(!tif_);
    return true;
  }

  return ImageFormat::encodeInit(image, filename, compression_level, concurrency);
}

template<typename T>
bool TIFFFormat<T>::encodeHeader(void)
{
  if(isHeaderEncoded())
    return true;

  uint32_t width = image_->decompress_width;
  uint32_t height = image_->decompress_height;
  uint8_t bps = image_->decompress_prec;
  uint16_t numcomps = image_->decompress_num_comps;
  size_t file_size = (size_t)width * height * (((size_t)bps + 7) / 8) * numcomps;

#ifdef GRK_ENABLE_BIGTIFF
  const char* mode = (file_size > UINT32_MAX) ? "w8b" : "wb";
#else
  if(file_size > UINT32_MAX)
  {
    spdlog::error(
        "TIFFFormat<T>::encodeHeader:file size greater than UINT32_MAX and BigTIFF not supported.");
    return false;
  }
  const char* mode = "wb";
#endif

#ifdef GRK_CUSTOM_TIFF_IO
  tif_ = MyTIFFOpen(fileName_.c_str(), mode);
#else
  tif_ = TIFFOpen(fileName_.c_str(), mode);
#endif
  if(!tif_)
  {
    spdlog::error("TIFFFormat<T>::encodeHeader:failed to open {} for writing", fileName_.c_str());
    return false;
  }

  return encodeHeader(tif_);
}

template<typename T>
bool TIFFFormat<T>::encodeHeader(TIFF* tif)
{
  if(isHeaderEncoded())
    return true;

  int tiPhoto = PHOTOMETRIC_MINISBLACK;
  bool success = false;
  int32_t firstExtraChannel = -1;
  uint32_t num_colour_channels = 0;
  size_t numExtraChannels = 0;
  bool sgnd = image_->comps[0].sgnd;
  uint32_t width = image_->decompress_width;
  units = width;
  uint32_t height = image_->decompress_height;
  uint8_t bps = image_->decompress_prec;
  uint16_t numcomps = image_->decompress_num_comps;
  bool subsampled = isFinalOutputSubsampled(image_);
  auto colourSpace = image_->decompress_colour_space;
  if(bps == 0)
  {
    spdlog::error("TIFFFormat<T>::encodeHeader: image_ precision is zero.");
    goto cleanup;
  }
  if(!allComponentsSanityCheck(image_, true))
  {
    spdlog::error("TIFFFormat<T>::encodeHeader: Image sanity check failed.");
    goto cleanup;
  }
  if(colourSpace == GRK_CLRSPC_CMYK)
  {
    if(numcomps < 4U)
    {
      spdlog::error(
          "TIFFFormat<T>::encodeHeader: CMYK images shall be composed of at least 4 planes.");

      return false;
    }
    tiPhoto = PHOTOMETRIC_SEPARATED;
    if(numcomps > 4U)
    {
      spdlog::warn("TIFFFormat<T>::encodeHeader: number of components {} is "
                   "greater than 4. Truncating to 4",
                   numcomps);
      numcomps = 4U;
    }
  }
  else if(numcomps > 2U)
  {
    switch(colourSpace)
    {
      case GRK_CLRSPC_EYCC:
      case GRK_CLRSPC_SYCC:
        if(subsampled && numcomps != 3)
        {
          spdlog::error("TIFFFormat<T>::encodeHeader: subsampled YCbCr image_ with alpha "
                        "not supported.");
          goto cleanup;
        }
        chroma_subsample_x = image_->comps[1].dx;
        chroma_subsample_y = image_->comps[1].dy;
        tiPhoto = PHOTOMETRIC_YCBCR;
        break;
      case GRK_CLRSPC_DEFAULT_CIE:
      case GRK_CLRSPC_CUSTOM_CIE:
        tiPhoto = sgnd ? PHOTOMETRIC_CIELAB : PHOTOMETRIC_ICCLAB;
        break;
      default:
        tiPhoto = PHOTOMETRIC_RGB;
        break;
    }
  }
  if(isFinalOutputSubsampled(image_))
  {
    if(tiPhoto != PHOTOMETRIC_YCBCR)
    {
      spdlog::error("TIFFFormat : subsampling only supported for YCbCr images");
      goto cleanup;
    }
    if(!isChromaSubsampled(image_))
    {
      spdlog::error("TIFFFormat<T>::encodeHeader: only chroma channels can be subsampled");
      goto cleanup;
    }
  }
  // extra channels (use actual number of components, before post processing)
  for(uint16_t i = 0U; i < image_->numcomps; ++i)
  {
    auto type = image_->comps[i].type;
    assert(type == GRK_CHANNEL_TYPE_COLOUR || type == GRK_CHANNEL_TYPE_OPACITY ||
           type == GRK_CHANNEL_TYPE_PREMULTIPLIED_OPACITY || type == GRK_CHANNEL_TYPE_UNSPECIFIED);
    if(type != GRK_CHANNEL_TYPE_COLOUR)
    {
      if(firstExtraChannel == -1)
        firstExtraChannel = (int32_t)i;
      numExtraChannels++;
    }
  }
  // TIFF assumes that alpha channels occur as last channels in image_.
  if(numExtraChannels > 0)
  {
    num_colour_channels = (uint32_t)(numcomps - (uint32_t)numExtraChannels);
    if((uint32_t)firstExtraChannel < num_colour_channels)
    {
      spdlog::warn("TIFFFormat<T>::encodeHeader: TIFF requires that non-colour channels occur as "
                   "last channels in image_. "
                   "TIFFTAG_EXTRASAMPLES tag for extra channels will not be set");
      numExtraChannels = 0;
    }
  }

  if(subsampled)
    units = (width + chroma_subsample_x - 1) / chroma_subsample_x;
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, width);
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, height);
  TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, sgnd ? SAMPLEFORMAT_INT : SAMPLEFORMAT_UINT);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, numcomps);
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, bps);
  TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, tiPhoto);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, image_->rows_per_strip);
  if(tiPhoto == PHOTOMETRIC_YCBCR)
  {
    float refBlackWhite[6] = {0.0, 255.0, 128.0, 255.0, 128.0, 255.0};
    float YCbCrCoefficients[3] = {0.299f, 0.587f, 0.114f};

    TIFFSetField(tif, TIFFTAG_YCBCRSUBSAMPLING, chroma_subsample_x, chroma_subsample_y);
    TIFFSetField(tif, TIFFTAG_REFERENCEBLACKWHITE, refBlackWhite);
    TIFFSetField(tif, TIFFTAG_YCBCRCOEFFICIENTS, YCbCrCoefficients);
    TIFFSetField(tif, TIFFTAG_YCBCRPOSITIONING, YCBCRPOSITION_CENTERED);
  }
  switch(compressionLevel_)
  {
    case COMPRESSION_ADOBE_DEFLATE:
#ifdef ZIP_SUPPORT
      TIFFSetField(tif, TIFFTAG_COMPRESSION, compressionLevel_); // zip compression
#endif
      break;
    default:
      if(compressionLevel_ != 0)
        TIFFSetField(tif, TIFFTAG_COMPRESSION, compressionLevel_);
  }
  if(image_->meta)
  {
    if(image_->meta->color.icc_profile_buf)
    {
      if(colourSpace == GRK_CLRSPC_ICC)
        TIFFSetField(tif, TIFFTAG_ICCPROFILE, image_->meta->color.icc_profile_len,
                     image_->meta->color.icc_profile_buf);
    }
    if(image_->meta->xmp_buf && image_->meta->xmp_len)
      TIFFSetField(tif, TIFFTAG_XMLPACKET, image_->meta->xmp_len, image_->meta->xmp_buf);
    if(image_->meta->iptc_buf && image_->meta->iptc_len)
    {
      // length must be multiple of 4
      auto iptc_len = image_->meta->iptc_len;
      iptc_len += (4 - (iptc_len & 0x03));
      if(iptc_len > image_->meta->iptc_len)
      {
        auto new_iptf_buf = new uint8_t[iptc_len];
        memset(new_iptf_buf, 0, iptc_len);
        memcpy(new_iptf_buf, image_->meta->iptc_buf, image_->meta->iptc_len);
        delete[] image_->meta->iptc_buf;
        image_->meta->iptc_buf = new_iptf_buf;
        image_->meta->iptc_len = iptc_len;
      }
      // Tag is of type TIFF_LONG, so byte length is divided by four
      if(TIFFIsByteSwapped(tif))
        TIFFSwabArrayOfLong((uint32_t*)image_->meta->iptc_buf,
                            (tmsize_t)(image_->meta->iptc_len / 4));
      TIFFSetField(tif, TIFFTAG_RICHTIFFIPTC, (uint32_t)(image_->meta->iptc_len / 4),
                   (void*)image_->meta->iptc_buf);
    }
  }
  if(image_->capture_resolution[0] > 0 && image_->capture_resolution[1] > 0)
  {
    TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_CENTIMETER); // cm
    for(uint8_t i = 0; i < 2; ++i)
    {
      TIFFSetField(tif, TIFFTAG_XRESOLUTION, image_->capture_resolution[0] / 100);
      TIFFSetField(tif, TIFFTAG_YRESOLUTION, image_->capture_resolution[1] / 100);
    }
  }
  if(numExtraChannels)
  {
    std::unique_ptr<uint16_t[]> out(new uint16_t[numExtraChannels]);
    numExtraChannels = 0;
    for(uint16_t i = 0U; i < numcomps; ++i)
    {
      auto comp = image_->comps + i;
      if(comp->type != GRK_CHANNEL_TYPE_COLOUR)
      {
        if(comp->type == GRK_CHANNEL_TYPE_OPACITY ||
           comp->type == GRK_CHANNEL_TYPE_PREMULTIPLIED_OPACITY)
          out[numExtraChannels++] = (image_->comps[i].type == GRK_CHANNEL_TYPE_OPACITY)
                                        ? EXTRASAMPLE_UNASSALPHA
                                        : EXTRASAMPLE_ASSOCALPHA;
        else
          out[numExtraChannels++] = EXTRASAMPLE_UNSPECIFIED;
      }
    }
    TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, numExtraChannels, out.get());
  }
  success = true;
  encodeState = IMAGE_FORMAT_ENCODED_HEADER;
cleanup:

  return success;
}
/***
 * library-orchestrated pixel encoding
 */
template<typename T>
bool TIFFFormat<T>::encodePixels(uint32_t workerId, grk_io_buf pixels)
{
  std::unique_lock<std::mutex> lk(encodePixelmutex);
  if(encodeState & IMAGE_FORMAT_ENCODED_PIXELS)
    return true;
  if(!isHeaderEncoded() && !encodeHeader())
    return false;

  return encodePixelsCore(workerId, pixels);
}

/***
 * application-orchestrated pixel encoding
 */
template<typename T>
bool TIFFFormat<T>::encodePixels()
{
  if(encodeState & IMAGE_FORMAT_ENCODED_PIXELS)
    return true;
  if(!isHeaderEncoded())
  {
    if(!encodeHeader())
      return false;
  }
  for(uint16_t i = 0U; i < image_->numcomps; ++i)
  {
    if(!image_->comps[i].data)
    {
      spdlog::error("encodePixels: component {} has null data.", i);
      return false;
    }
  }
  bool success = false;
  uint32_t height = image_->decompress_height;
  auto planes = std::make_unique<T*[]>(image_->numcomps);
  auto planesBegin = std::make_unique<T*[]>(image_->numcomps);
  uint16_t numcomps = image_->decompress_num_comps;
  for(uint16_t i = 0U; i < numcomps; ++i)
  {
    planes[i] = (T*)image_->comps[i].data;
    planesBegin[i] = planes[i];
  }
  uint32_t h = 0;
  GrkIOBuf packedBuf;

  if(isFinalOutputSubsampled(image_))
  {
    // TIFF-specific
    uint64_t packedLengthEncoded = (uint64_t)TIFFVStripSize(tif_, (uint32_t)image_->rows_per_strip);
    packedBuf = pool.get(packedLengthEncoded);
    auto bufPtr = (int8_t*)packedBuf.data;
    uint32_t bytesToWrite = 0;
    uint32_t rowsWritten = 0;
    for(; h < rowsWritten + height; h += chroma_subsample_y)
    {
      uint32_t rowsSoFar = h - rowsWritten;
      if(bytesToWrite > 0 && rowsSoFar > 0 && (rowsSoFar % image_->rows_per_strip == 0))
      {
        packedBuf.len = bytesToWrite;
        packedBuf.offset = orchestrator.getOffset();
        packedBuf.index = orchestrator.getNumPooledRequests();
        if(!encodePixelsCore(0, packedBuf))
          goto cleanup;
        packedBuf = pool.get(packedLengthEncoded);
        bufPtr = (int8_t*)(packedBuf.data);
        bytesToWrite = 0;
      }
      size_t xposLuma = 0;
      size_t xposChroma = 0;
      for(uint32_t u = 0; u < units; ++u)
      {
        // 1. luma
        for(size_t sub_h = 0; sub_h < chroma_subsample_y; ++sub_h)
        {
          for(size_t sub_x = xposLuma; sub_x < xposLuma + chroma_subsample_x; ++sub_x)
          {
            bool accept = (h + sub_h) < height && sub_x < image_->decompress_width;
            *bufPtr++ = accept ? (int8_t)planes[0][sub_x + sub_h * image_->comps[0].stride] : 0;
            bytesToWrite++;
          }
        }
        if(xposChroma >= image_->comps[1].stride || xposChroma >= image_->comps[2].stride)
        {
          spdlog::warn("TIFFFormat<T>::encodePixels: chroma channel width is too short - "
                       "skipping out of bounds pixel location.");
          break;
        }
        // 2. chroma
        *bufPtr++ = (int8_t)*planes[1]++;
        *bufPtr++ = (int8_t)*planes[2]++;
        bytesToWrite += 2;
        xposChroma++;
        xposLuma += chroma_subsample_x;
      }
      planes[0] += image_->comps[0].stride * chroma_subsample_y;
      planesBegin[1] += image_->comps[1].stride;
      planes[1] = planesBegin[1];
      planesBegin[2] += image_->comps[2].stride;
      planes[2] = planesBegin[2];
    }
    if(h != rowsWritten)
      rowsWritten += h - chroma_subsample_y - rowsWritten;
    // cleanup
    packedBuf.len = bytesToWrite;
    packedBuf.offset = orchestrator.getOffset();
    packedBuf.index = orchestrator.getNumPooledRequests();
    if(bytesToWrite && !encodePixelsCore(0, packedBuf))
      goto cleanup;
  }
  else
  {
    auto iter = grk::InterleaverFactory<T>::makeInterleaver(image_->decompress_prec);
    if(!iter)
      goto cleanup;
    while(h < height)
    {
      uint32_t stripRows = (std::min)(image_->rows_per_strip, height - h);
      packedBuf = pool.get(image_->packed_row_bytes * stripRows);
      iter->interleave(((T**)planes.get()), numcomps, packedBuf.data, image_->decompress_width,
                       image_->comps[0].stride, image_->packed_row_bytes, stripRows, 0);
      packedBuf.pooled = true;
      packedBuf.offset = orchestrator.getOffset();
      packedBuf.len = image_->packed_row_bytes * stripRows;
      packedBuf.index = orchestrator.getNumPooledRequests();
      if(!encodePixelsCore(0, packedBuf))
      {
        delete iter;
        goto cleanup;
      }
      h += stripRows;
    }
    delete iter;
  }
  success = true;
cleanup:

  return success;
}

/***
 * Common core pixel encoding write to disk
 */
template<typename T>
bool TIFFFormat<T>::encodePixelsCoreWrite(grk_io_buf pixels)
{
  tmsize_t written = TIFFWriteEncodedStrip(tif_, pixels.index, pixels.data, (tmsize_t)pixels.len);
  return written != -1;
}

template<typename T>
bool TIFFFormat<T>::encodeFinish(void)
{
  if(encodeState & IMAGE_FORMAT_ENCODED_PIXELS)
  {
    assert(!tif_);
    return true;
  }
  if(tif_)
    TIFFClose(tif_);
  tif_ = nullptr;
  encodeState |= IMAGE_FORMAT_ENCODED_PIXELS;

  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////

static inline std::string getSampleFormatString(uint16_t tiSampleFormat)
{
  switch(tiSampleFormat)
  {
    case SAMPLEFORMAT_UINT:
      return "UINT";
      break;
    case SAMPLEFORMAT_INT:
      return "INT";
      break;
    case SAMPLEFORMAT_IEEEFP:
      return "IEEEFP";
      break;
    case SAMPLEFORMAT_VOID:
      return "VOID";
      break;
    case SAMPLEFORMAT_COMPLEXINT:
      return "COMPLEXINT";
      break;
    case SAMPLEFORMAT_COMPLEXIEEEFP:
      return "COMPLEXIEEEFP";
      break;
    default:
      return "unknown";
  }
}

static inline std::string getColourFormatString(uint16_t tiPhoto)
{
  switch(tiPhoto)
  {
    case PHOTOMETRIC_MINISWHITE:
      return "MINISWHITE";
      break;
    case PHOTOMETRIC_MINISBLACK:
      return "MINISBLACK";
      break;
    case PHOTOMETRIC_RGB:
      return "RGB";
      break;
    case PHOTOMETRIC_PALETTE:
      return "PALETTE";
      break;
    case PHOTOMETRIC_MASK:
      return "MASK";
      break;
    case PHOTOMETRIC_SEPARATED:
      return "SEPARATED";
      break;
    case PHOTOMETRIC_YCBCR:
      return "YCBCR";
      break;
    case PHOTOMETRIC_CIELAB:
      return "CIELAB";
      break;
    case PHOTOMETRIC_ICCLAB:
      return "ICCLAB";
      break;
    case PHOTOMETRIC_ITULAB:
      return "ITULAB";
      break;
    case PHOTOMETRIC_CFA:
      return "CFA";
      break;
    case PHOTOMETRIC_LOGL:
      return "LOGL";
      break;
    case PHOTOMETRIC_LOGLUV:
      return "LOGLUV";
      break;
    default:
      return "unknown";
  }
}

static inline void set_resolution(double* res, float resx, float resy, short resUnit)
{
  // resolution is in pels / metre
  res[0] = resx;
  res[1] = resy;

  for(uint32_t i = 0; i < 2; ++i)
  {
    switch(resUnit)
    {
      case RESUNIT_INCH:
        res[i] *= 39.370078740157;
        break;
        // cm
      case RESUNIT_CENTIMETER:
        res[i] *= 100;
        break;
      default:
        break;
    }
    res[i] = floor(res[i] + 0.5);
  }
}

template<typename T>
bool TIFFFormat<T>::readTiffPixels(TIFF* tif, grk_image_comp* comps, uint16_t numcomps,
                                   uint16_t tiSpp, uint16_t tiPC, uint16_t tiPhoto,
                                   uint32_t chroma_subsample_x, uint32_t chroma_subsample_y)
{
  if(!tif)
    return false;

  bool success = true;
  auto planes = std::make_unique<T*[]>(numcomps);
  tsize_t rowStride;
  bool invert;
  tdata_t buf = nullptr;
  tstrip_t strip;
  tsize_t strip_size;
  uint32_t currentPlane = 0;
  T* buffer32s = nullptr;
  bool subsampled = chroma_subsample_x != 1 || chroma_subsample_y != 1;
  size_t luma_block = chroma_subsample_x * chroma_subsample_y;
  size_t unitSize = luma_block + 2;
  if(tiPC == PLANARCONFIG_SEPARATE)
    tiSpp = 1U; /* consider only one sample per plane */
  strip_size = TIFFStripSize(tif);
  buf = _TIFFmalloc(strip_size);
  if(buf == nullptr)
  {
    success = false;
    goto beach;
  }
  rowStride = (comps[0].w * tiSpp * comps[0].prec + 7U) / 8U;
  buffer32s = new T[(size_t)comps[0].w * tiSpp];
  strip = 0;
  invert = tiPhoto == PHOTOMETRIC_MINISWHITE;
  for(uint16_t j = 0; j < numcomps; j++)
    planes[j] = (T*)comps[j].data;
  do
  {
    auto comp = comps + currentPlane;
    planes[0] = (T*)comp->data;
    uint32_t height = 0;
    // if width % chroma_subsample_x != 0...
    size_t units = (comp->w + chroma_subsample_x - 1) / chroma_subsample_x;
    // each coded row will be padded to fill unit
    size_t padding = (units * chroma_subsample_x - comp->w);
    if(subsampled)
      rowStride = (tsize_t)(units * unitSize);
    size_t xpos = 0;
    for(; (height < comp->h) && (strip < TIFFNumberOfStrips(tif)); strip++)
    {
      tsize_t ssize = TIFFReadEncodedStrip(tif, strip, buf, strip_size);
      if(ssize < 1 || ssize > strip_size)
      {
        spdlog::error("tiftoimage: Bad value for ssize({}) "
                      "vs. strip_size({}).",
                      (long long)ssize, (long long)strip_size);
        success = false;
        goto beach;
      }
      assert(ssize >= rowStride);
      const uint8_t* datau8 = (const uint8_t*)buf;
      while(ssize >= rowStride)
      {
        if(chroma_subsample_x == 1 && chroma_subsample_y == 1)
        {
          switch(comps[0].prec)
          {
            case 1:
              convertToOutputTIFF<1, false, T>(datau8, buffer32s, (size_t)comp->w * tiSpp, invert);
              break;
            case 2:
              convertToOutputTIFF<2, false, T>(datau8, buffer32s, (size_t)comp->w * tiSpp, invert);
              break;
            case 3:
              convertToOutputTIFF<3, false, T>(datau8, buffer32s, (size_t)comp->w * tiSpp, invert);
              break;
            case 4:
              comps[0].sgnd ? convertToOutputTIFF<4, true, T>(datau8, buffer32s,
                                                              (size_t)comp->w * tiSpp, invert)
                            : convertToOutputTIFF<4, false, T>(datau8, buffer32s,
                                                               (size_t)comp->w * tiSpp, invert);
              break;
            case 5:
              convertToOutputTIFF<5, false, T>(datau8, buffer32s, (size_t)comp->w * tiSpp, invert);
              break;
            case 6:
              convertToOutputTIFF<6, false, T>(datau8, buffer32s, (size_t)comp->w * tiSpp, invert);
              break;
            case 7:
              convertToOutputTIFF<7, false, T>(datau8, buffer32s, (size_t)comp->w * tiSpp, invert);
              break;
            case 8:
              comps[0].sgnd ? convertToOutputTIFF<8, true, T>(datau8, buffer32s,
                                                              (size_t)comp->w * tiSpp, invert)
                            : convertToOutputTIFF<8, false, T>(datau8, buffer32s,
                                                               (size_t)comp->w * tiSpp, invert);

              break;
            case 9:
              convertToOutputTIFF<9, false, T>(datau8, buffer32s, (size_t)comp->w * tiSpp, invert);
              break;
            case 10:
              comps[0].sgnd ? convertToOutputTIFF<10, true, T>(datau8, buffer32s,
                                                               (size_t)comp->w * tiSpp, invert)
                            : convertToOutputTIFF<10, false, T>(datau8, buffer32s,
                                                                (size_t)comp->w * tiSpp, invert);

              break;
            case 11:
              convertToOutputTIFF<11, false, T>(datau8, buffer32s, (size_t)comp->w * tiSpp, invert);
              break;
            case 12:
              comps[0].sgnd ? convertToOutputTIFF<12, true, T>(datau8, buffer32s,
                                                               (size_t)comp->w * tiSpp, invert)
                            : convertToOutputTIFF<12, false, T>(datau8, buffer32s,
                                                                (size_t)comp->w * tiSpp, invert);

              break;
            case 13:
              convertToOutputTIFF<13, false, T>(datau8, buffer32s, (size_t)comp->w * tiSpp, invert);
              break;
            case 14:
              convertToOutputTIFF<14, false, T>(datau8, buffer32s, (size_t)comp->w * tiSpp, invert);
              break;
            case 15:
              convertToOutputTIFF<15, false, T>(datau8, buffer32s, (size_t)comp->w * tiSpp, invert);
              break;
            case 16:
              convertToOutputTIFF<16, false, T>(datau8, buffer32s, (size_t)comp->w * tiSpp, invert);
              break;
            default:
              goto beach;
          }
          uint16_t targetPlanes = numcomps;
          if(tiSpp == 1)
            targetPlanes = 1;
          interleave(buffer32s, planes.get(), comp->w, targetPlanes);
          for(uint16_t k = 0; k < numcomps; ++k)
            planes[k] += comp->stride;
          datau8 += rowStride;
          ssize -= rowStride;
          height++;
        }
        else
        {
          uint32_t strideDiffCb = comps[1].stride - comps[1].w;
          uint32_t strideDiffCr = comps[2].stride - comps[2].w;
          for(size_t i = 0; i < (size_t)rowStride; i += unitSize)
          {
            // process a unit
            // 1. luma
            for(size_t k = 0; k < chroma_subsample_y; ++k)
            {
              for(size_t j = 0; j < chroma_subsample_x; ++j)
              {
                bool accept = height + k < comp->h && xpos + j < comp->w;
                if(accept)
                  planes[0][xpos + j + k * comp->stride] = datau8[j];
              }
              datau8 += chroma_subsample_x;
            }
            // 2. chroma
            *planes[1]++ = *datau8++;
            *planes[2]++ = *datau8++;

            // 3. increment raster x
            xpos += chroma_subsample_x;
            if(xpos >= comp->w)
            {
              datau8 += padding;
              xpos = 0;
              planes[0] += comp->stride * chroma_subsample_y;
              planes[1] += strideDiffCb;
              planes[2] += strideDiffCr;
              height += chroma_subsample_y;
            }
          }
          ssize -= rowStride;
        }
      }
    }
    currentPlane++;
  } while((tiPC == PLANARCONFIG_SEPARATE) && (currentPlane < numcomps));
beach:
  delete[] buffer32s;
  if(buf)
    _TIFFfree(buf);
  return success;
}

template<typename T>
template<typename RT>
bool TIFFFormat<T>::readTiffPixelsSigned(TIFF* tif, grk_image_comp* comps, uint16_t numcomps,
                                         uint16_t tiSpp, uint16_t tiPC)
{
  if(!tif)
    return false;

  bool success = true;
  // cvtInterleavedToPlanar cvtToPlanar = nullptr;
  auto planes = std::make_unique<T*[]>(numcomps);
  tsize_t rowStride;
  tdata_t buf = nullptr;
  tstrip_t strip;
  tsize_t strip_size;
  uint32_t currentPlane = 0;
  T* buffer32s = nullptr;
  if(tiPC == PLANARCONFIG_SEPARATE)
    tiSpp = 1U; /* consider only one sample per plane */
  strip_size = TIFFStripSize(tif);
  buf = _TIFFmalloc(strip_size);
  if(buf == nullptr)
  {
    success = false;
    goto beach;
  }
  rowStride = (comps[0].w * tiSpp * comps[0].prec + 7U) / 8U;
  buffer32s = new T[(size_t)comps[0].w * tiSpp];
  strip = 0;
  for(uint16_t j = 0; j < numcomps; j++)
    planes[j] = (T*)comps[j].data;
  do
  {
    grk_image_comp* comp = comps + currentPlane;
    planes[0] = (T*)comp->data; /* to manage planar data */
    uint32_t height = comp->h;
    /* Read the Image components */
    for(; (height > 0) && (strip < TIFFNumberOfStrips(tif)); strip++)
    {
      tsize_t ssize = TIFFReadEncodedStrip(tif, strip, buf, strip_size);
      if(ssize < 1 || ssize > strip_size)
      {
        spdlog::error("tiftoimage: Bad value for ssize({}) "
                      "vs. strip_size({}).",
                      (long long)ssize, (long long)strip_size);
        success = false;
        goto beach;
      }
      const RT* data = (const RT*)buf;
      while(ssize >= rowStride)
      {
        for(size_t i = 0; i < (size_t)comp->w * tiSpp; ++i)
          buffer32s[i] = data[i];
        uint16_t targetPlanes = numcomps;
        if(tiSpp == 1)
          targetPlanes = 1;
        interleave(buffer32s, planes.get(), comp->w, targetPlanes);
        for(uint16_t k = 0; k < numcomps; ++k)
          planes[k] += comp->stride;
        data += (size_t)rowStride / sizeof(RT);
        ssize -= rowStride;
        height--;
      }
    }
    currentPlane++;
  } while((tiPC == PLANARCONFIG_SEPARATE) && (currentPlane < numcomps));
beach:
  delete[] buffer32s;
  if(buf)
    _TIFFfree(buf);
  return success;
}

// rec 601 conversion factors, multiplied by 1000
const uint32_t rec_601_luma[3]{299, 587, 114};

template<typename T>
grk_image* TIFFFormat<T>::decode(const std::string& filename, grk_cparameters* parameters)
{
  bool found_assocalpha = false;
  size_t alpha_count = 0;
  chroma_subsample_x = 1;
  chroma_subsample_y = 1;
  GRK_COLOR_SPACE color_space = GRK_CLRSPC_UNKNOWN;
  std::unique_ptr<grk_image_comp[]> cmptparm;
  grk_image* image = nullptr;
  uint16_t tiBps = 0, tiPhoto = 0, tiSf = SAMPLEFORMAT_UINT, tiSpp = 0, tiPC = 0;
  bool hasTiSf = false;
  short tiResUnit = 0;
  float tiXRes = 0, tiYRes = 0;
  uint32_t tiWidth = 0, tiHeight = 0;
  bool is_cinema = GRK_IS_CINEMA(parameters->rsiz);
  bool success = false;
  bool isCIE = false;
  uint16_t compress;
  float *luma = nullptr, *refBlackWhite = nullptr;
  uint16_t *red_orig = nullptr, *green_orig = nullptr, *blue_orig = nullptr;
  uint32_t num_pages = 0;
  uint32_t cur_dir;

  tif_ = TIFFOpen(filename.c_str(), "r");
  if(!tif_)
  {
    spdlog::error("TIFFFormat<T>::decode: Failed to open {} for reading", filename);
    return 0;
  }

  if(TIFFIsTiled(tif_))
  {
    spdlog::error("TIFFFormat<T>::decode: tiled TIFF images not supported");
    if(tif_)
      TIFFClose(tif_);
    tif_ = nullptr;
    return 0;
  }

  TIFFGetField(tif_, TIFFTAG_COMPRESSION, &compress);
  TIFFGetFieldDefaulted(tif_, TIFFTAG_IMAGEWIDTH, &tiWidth);
  TIFFGetFieldDefaulted(tif_, TIFFTAG_IMAGELENGTH, &tiHeight);
  TIFFGetFieldDefaulted(tif_, TIFFTAG_BITSPERSAMPLE, &tiBps);
  TIFFGetFieldDefaulted(tif_, TIFFTAG_SAMPLESPERPIXEL, &tiSpp);
  TIFFGetFieldDefaulted(tif_, TIFFTAG_PHOTOMETRIC, &tiPhoto);
  TIFFGetFieldDefaulted(tif_, TIFFTAG_PLANARCONFIG, &tiPC);
  hasTiSf = TIFFGetFieldDefaulted(tif_, TIFFTAG_SAMPLEFORMAT, &tiSf) == 1;

  TIFFGetFieldDefaulted(tif_, TIFFTAG_REFERENCEBLACKWHITE, &refBlackWhite);

  uint32_t w = tiWidth;
  uint32_t h = tiHeight;
  uint16_t numcomps = 0;
  uint32_t icclen = 0;
  uint8_t* iccbuf = nullptr;
  uint8_t* iptc_buf = nullptr;
  uint32_t iptc_len = 0;
  uint8_t* xmp_buf = nullptr;
  uint32_t xmp_len = 0;
  uint16_t* sampleinfo = nullptr;
  uint16_t extrasamples = 0;
  bool hasXRes = false, hasYRes = false, hasResUnit = false;
  bool isSigned = (tiSf == SAMPLEFORMAT_INT);
  bool needSignedPixelReader = isSigned && (tiBps == 8 || tiBps == 16);

  // 1. sanity checks

  cur_dir = TIFFCurrentDirectory(tif_);
  do
  {
    uint32_t subfiletype = 0;
    if(TIFFGetField(tif_, TIFFTAG_SUBFILETYPE, &subfiletype))
    {
      if(subfiletype == FILETYPE_PAGE)
        num_pages++;
    }
    else
    {
      num_pages++;
    }
  } while(TIFFReadDirectory(tif_));
  if(num_pages > 1)
    spdlog::warn(
        "TIFFFormat<T>::decode: multi-page document detected. Only the first page will be encoded");
  if(!TIFFSetDirectory(tif_, cur_dir))
  {
    spdlog::error("TIFFFormat<T>::decode: failed to reset to directory {}", cur_dir);
    goto cleanup;
  }

  // check for supported photometric interpretation
  if(tiPhoto != PHOTOMETRIC_MINISBLACK && tiPhoto != PHOTOMETRIC_MINISWHITE &&
     tiPhoto != PHOTOMETRIC_RGB && tiPhoto != PHOTOMETRIC_ICCLAB && tiPhoto != PHOTOMETRIC_CIELAB &&
     tiPhoto != PHOTOMETRIC_YCBCR && tiPhoto != PHOTOMETRIC_SEPARATED &&
     tiPhoto != PHOTOMETRIC_PALETTE)
  {
    spdlog::error("TIFFFormat<T>::decode: Unsupported color format {}.\n"
                  "Only RGB(A), GRAY(A), CIELAB, YCC, CMYK and PALETTE have been implemented.",
                  getColourFormatString(tiPhoto));
    goto cleanup;
  }
  // check for rec601
  if(tiPhoto == PHOTOMETRIC_YCBCR)
  {
    TIFFGetFieldDefaulted(tif_, TIFFTAG_YCBCRCOEFFICIENTS, &luma);
    for(size_t i = 0; i < 3; ++i)
    {
      if((uint32_t)(luma[i] * 1000.0f + 0.5f) != rec_601_luma[i])
      {
        spdlog::error(
            "TIFFFormat<T>::decode: YCbCr image with unsupported non Rec. 601 colour space;");
        spdlog::error("YCbCrCoefficients: {},{},{}", luma[0], luma[1], luma[2]);
        spdlog::error("Please convert to sRGB before compressing.");
        goto cleanup;
      }
    }
  }
  // check sample format
  if(hasTiSf && tiSf != SAMPLEFORMAT_UINT && tiSf != SAMPLEFORMAT_INT)
  {
    spdlog::error("TIFFFormat<T>::decode: Unsupported sample format: {}.",
                  getSampleFormatString(tiSf));
    goto cleanup;
  }
  if(tiSpp == 0)
  {
    spdlog::error("TIFFFormat<T>::decode: Samples per pixel must be non-zero");
    goto cleanup;
  }
  if(tiBps > 16U || tiBps == 0)
  {
    spdlog::error("TIFFFormat<T>::decode: Unsupported precision {}. Maximum 16 Bits supported.",
                  tiBps);
    goto cleanup;
  }
  if(tiWidth == 0 || tiHeight == 0)
  {
    spdlog::error("TIFFFormat<T>::decode: Width({}) and height({}) must both "
                  "be non-zero",
                  tiWidth, tiHeight);
    goto cleanup;
  }
  TIFFGetFieldDefaulted(tif_, TIFFTAG_EXTRASAMPLES, &extrasamples, &sampleinfo);

  // 2. initialize image components and signed/unsigned
  if((tiPhoto == PHOTOMETRIC_RGB) && (is_cinema) && (tiBps != 12U))
  {
    spdlog::warn("TIFFFormat<T>::decode: Input image bitdepth is {} bits.", tiBps);
    spdlog::warn("TIF conversion has automatically rescaled to 12-bits");
    spdlog::warn("to comply with cinema profiles.\n");
  }
  else
  {
    is_cinema = 0U;
  }
  numcomps = extrasamples;
  switch(tiPhoto)
  {
    case PHOTOMETRIC_PALETTE:
      if(isSigned)
      {
        spdlog::error("TIFFFormat<T>::decode: Signed palette image not supported");
        goto cleanup;
      }
      color_space = GRK_CLRSPC_SRGB;
      numcomps++;
      break;
    case PHOTOMETRIC_MINISBLACK:
    case PHOTOMETRIC_MINISWHITE:
      color_space = GRK_CLRSPC_GRAY;
      numcomps++;
      break;
    case PHOTOMETRIC_RGB:
      color_space = GRK_CLRSPC_SRGB;
      numcomps = (uint16_t)(numcomps + 3);
      break;
    case PHOTOMETRIC_CIELAB:
    case PHOTOMETRIC_ICCLAB:
      isCIE = true;
      color_space = GRK_CLRSPC_DEFAULT_CIE;
      numcomps = (uint16_t)(numcomps + 3);
      break;
    case PHOTOMETRIC_YCBCR:
      // jpeg library is needed to convert from YCbCr to RGB
      if(compress == COMPRESSION_OJPEG || compress == COMPRESSION_JPEG)
      {
        spdlog::error("TIFFFormat<T>::decode: YCbCr image with JPEG compression"
                      " is not supported");
        goto cleanup;
      }
      else if(compress == COMPRESSION_PACKBITS)
      {
        spdlog::error("TIFFFormat<T>::decode: YCbCr image with PACKBITS compression"
                      " is not supported");
        goto cleanup;
      }
      color_space = GRK_CLRSPC_SYCC;
      numcomps = (uint16_t)(numcomps + 3);
      TIFFGetFieldDefaulted(tif_, TIFFTAG_YCBCRSUBSAMPLING, &chroma_subsample_x,
                            &chroma_subsample_y);
      if(chroma_subsample_x == 0 || chroma_subsample_y == 0)
      {
        spdlog::error("TIFFFormat<T>::decode: chroma subsampling factors must be positive.");
        goto cleanup;
      }
      if(chroma_subsample_x > 255 || chroma_subsample_y > 255)
      {
        spdlog::error(
            "TIFFFormat<T>::decode: chroma subsampling factors must each be less than 256.");
        goto cleanup;
      }
      if(chroma_subsample_x != 1 || chroma_subsample_y != 1)
      {
        if(isSigned)
        {
          spdlog::error("TIFFFormat<T>::decode: chroma subsampling {},{} with signed data "
                        "is not supported",
                        chroma_subsample_x, chroma_subsample_y);
          goto cleanup;
        }
        if(numcomps != 3)
        {
          spdlog::error("TIFFFormat<T>::decode: chroma subsampling {},{} with alpha "
                        "channel(s) not supported",
                        chroma_subsample_x, chroma_subsample_y);
          goto cleanup;
        }
      }
      break;
    case PHOTOMETRIC_SEPARATED:
      color_space = GRK_CLRSPC_CMYK;
      numcomps = (uint16_t)(numcomps + 4);
      break;
    default:
      spdlog::error("TIFFFormat<T>::decode: Unsupported colour space {}.", tiPhoto);
      goto cleanup;
      break;
  }
  if(tiPhoto == PHOTOMETRIC_CIELAB)
  {
    if(hasTiSf && (tiSf != SAMPLEFORMAT_INT))
      spdlog::warn("TIFFFormat<T>::decode: Input image is in CIE colour space"
                   " but sample format is unsigned int. Forcing to signed int");
    isSigned = true;
  }
  else if(tiPhoto == PHOTOMETRIC_ICCLAB)
  {
    if(hasTiSf && (tiSf != SAMPLEFORMAT_UINT))
      spdlog::warn("TIFFFormat<T>::decode: Input image is in ICC CIE colour"
                   " space but sample format is signed int. Forcing to unsigned int");
    isSigned = false;
  }

  if(isSigned)
  {
    if(tiPhoto == PHOTOMETRIC_MINISWHITE)
      spdlog::error("TIFFFormat<T>::decode: signed image with "
                    "MINISWHITE format is not fully supported");
    if(tiBps != 4 && tiBps != 8 && tiBps != 10 && tiBps != 12 && tiBps != 16)
    {
      spdlog::error("TIFFFormat<T>::decode: signed image with bit"
                    " depth {} is not supported",
                    tiBps);
      goto cleanup;
    }
  }
  cmptparm = std::make_unique<grk_image_comp[]>(numcomps);
  // 4. create image
  for(uint16_t j = 0; j < numcomps; j++)
  {
    auto img_comp = &cmptparm[j];
    memset(img_comp, 0, sizeof(grk_image_comp));
    img_comp->prec = (uint8_t)tiBps;
    bool chroma = (j == 1 || j == 2);
    img_comp->dx = chroma ? (uint8_t)chroma_subsample_x : 1;
    img_comp->dy = chroma ? (uint8_t)chroma_subsample_y : 1;
    img_comp->w = grk::ceildiv<uint32_t>(w, img_comp->dx);
    img_comp->h = grk::ceildiv<uint32_t>(h, img_comp->dy);
  }
  image = grk_image_new(numcomps, &cmptparm[0], color_space, true);
  if(!image)
    goto cleanup;

  /* set image offset and reference grid */
  image->x0 = parameters->image_offset_x0;
  image->x1 = image->x0 + (w - 1) * 1 + 1;
  if(image->x1 <= image->x0)
  {
    spdlog::error("TIFFFormat<T>::decode: Bad value for image->x1({}) vs. "
                  "image->x0({}).",
                  image->x1, image->x0);
    goto cleanup;
  }
  image->y0 = parameters->image_offset_y0;
  image->y1 = image->y0 + (h - 1) * 1 + 1;
  if(image->y1 <= image->y0)
  {
    spdlog::error("TIFFFormat<T>::decode: Bad value for image->y1({}) vs. "
                  "image->y0({}).",
                  image->y1, image->y0);
    goto cleanup;
  }
  if(tiPhoto == PHOTOMETRIC_PALETTE)
  {
    if(!TIFFGetField(tif_, TIFFTAG_COLORMAP, &red_orig, &green_orig, &blue_orig))
    {
      spdlog::error("TIFFFormat<T>::decode: Missing required \"Colormap\" tag");
      goto cleanup;
    }
    uint16_t palette_num_entries = (uint16_t)(1U << tiBps);
    uint8_t num_channels = 3U;
    createMeta(image);
    allocPalette(&image->meta->color, num_channels, (uint16_t)palette_num_entries);
    auto cmap = new _grk_component_mapping_comp[num_channels];
    for(uint8_t i = 0; i < num_channels; ++i)
    {
      cmap[i].component = 0;
      cmap[i].mapping_type = 1;
      cmap[i].palette_column = i;
      image->meta->color.palette->channel_prec[i] = 16;
      image->meta->color.palette->channel_sign[i] = false;
    }
    image->meta->color.palette->component_mapping = cmap;
    auto lut_ptr = image->meta->color.palette->lut;
    for(uint16_t i = 0; i < palette_num_entries; i++)
    {
      *lut_ptr++ = red_orig[i];
      *lut_ptr++ = green_orig[i];
      *lut_ptr++ = blue_orig[i];
    }
  }
  for(uint16_t j = 0; j < numcomps; j++)
  {
    // handle non-colour channel
    uint16_t numColourChannels = (uint16_t)(numcomps - extrasamples);
    auto comp = image->comps + j;

    if(extrasamples > 0 && j >= numColourChannels)
    {
      comp->type = GRK_CHANNEL_TYPE_UNSPECIFIED;
      comp->association = GRK_CHANNEL_ASSOC_UNASSOCIATED;
      auto alphaType = sampleinfo[j - numColourChannels];
      if(alphaType == EXTRASAMPLE_ASSOCALPHA)
      {
        if(found_assocalpha)
          spdlog::warn("TIFFFormat<T>::decode: Found more than one associated alpha channel");
        alpha_count++;
        comp->type = GRK_CHANNEL_TYPE_PREMULTIPLIED_OPACITY;
        found_assocalpha = true;
      }
      else if(alphaType == EXTRASAMPLE_UNASSALPHA)
      {
        alpha_count++;
        comp->type = GRK_CHANNEL_TYPE_OPACITY;
      }
      else
      {
        // some older mono or RGB images may have alpha channel
        // stored as EXTRASAMPLE_UNSPECIFIED
        if((color_space == GRK_CLRSPC_GRAY && numcomps == 2) ||
           (color_space == GRK_CLRSPC_SRGB && numcomps == 4))
        {
          alpha_count++;
          comp->type = GRK_CHANNEL_TYPE_OPACITY;
        }
      }
    }
    if(comp->type == GRK_CHANNEL_TYPE_OPACITY ||
       comp->type == GRK_CHANNEL_TYPE_PREMULTIPLIED_OPACITY)
    {
      switch(alpha_count)
      {
        case 1:
          comp->association = GRK_CHANNEL_ASSOC_WHOLE_IMAGE;
          break;
        case 2:
          comp->association = GRK_CHANNEL_ASSOC_UNASSOCIATED;
          break;
        default:
          comp->type = GRK_CHANNEL_TYPE_UNSPECIFIED;
          comp->association = GRK_CHANNEL_ASSOC_UNASSOCIATED;
          break;
      }
    }
    comp->sgnd = isSigned;
  }

  if(needSignedPixelReader && isFinalOutputSubsampled(image))
  {
    spdlog::error("TIFF: subsampling not supported for signed 8 and 16 bit images");
    goto cleanup;
  }

  // 5. extract capture resolution
  hasXRes = TIFFGetFieldDefaulted(tif_, TIFFTAG_XRESOLUTION, &tiXRes) == 1;
  hasYRes = TIFFGetFieldDefaulted(tif_, TIFFTAG_YRESOLUTION, &tiYRes) == 1;
  hasResUnit = TIFFGetFieldDefaulted(tif_, TIFFTAG_RESOLUTIONUNIT, &tiResUnit) == 1;
  if(hasXRes && hasYRes && hasResUnit && tiResUnit != RESUNIT_NONE)
  {
    set_resolution(parameters->capture_resolution_from_file, tiXRes, tiYRes, tiResUnit);
    parameters->write_capture_resolution_from_file = true;
  }
  // 6. extract embedded ICC profile (with sanity check on binary size of profile)
  // note: we ignore ICC profile for CIE images as JPEG 2000 can't signal both
  // CIE and ICC
  if(!isCIE)
  {
    if((TIFFGetFieldDefaulted(tif_, TIFFTAG_ICCPROFILE, &icclen, &iccbuf) == 1) && icclen > 0 &&
       icclen < grk::maxICCProfileBufferLen)
      copyICC(image, iccbuf, icclen);
  }
  // 7. extract IPTC meta-data
  if(TIFFGetFieldDefaulted(tif_, TIFFTAG_RICHTIFFIPTC, &iptc_len, &iptc_buf) == 1)
  {
    if(TIFFIsByteSwapped(tif_))
      TIFFSwabArrayOfLong((uint32_t*)iptc_buf, iptc_len);
    // since TIFFTAG_RICHTIFFIPTC is of type TIFF_LONG, we must multiply
    // by 4 to get the length in bytes
    createMeta(image);
    image->meta->iptc_len = iptc_len * 4;
    image->meta->iptc_buf = new uint8_t[iptc_len];
    memcpy(image->meta->iptc_buf, iptc_buf, iptc_len);
  }
  // 8. extract XML meta-data
  if(TIFFGetFieldDefaulted(tif_, TIFFTAG_XMLPACKET, &xmp_len, &xmp_buf) == 1)
  {
    createMeta(image);
    image->meta->xmp_len = xmp_len;
    image->meta->xmp_buf = new uint8_t[xmp_len];
    memcpy(image->meta->xmp_buf, xmp_buf, xmp_len);
  }
  // 9. read pixel data
  if(needSignedPixelReader)
  {
    if(tiBps == 8)
      success = readTiffPixelsSigned<int8_t>(tif_, image->comps, numcomps, tiSpp, tiPC);
    else
      success = readTiffPixelsSigned<int16_t>(tif_, image->comps, numcomps, tiSpp, tiPC);
  }
  else
  {
    success = readTiffPixels(tif_, image->comps, numcomps, tiSpp, tiPC, tiPhoto, chroma_subsample_x,
                             chroma_subsample_y);
  }
cleanup:
  if(tif_)
    TIFFClose(tif_);
  tif_ = nullptr;
  if(success)
  {
    if(is_cinema)
    {
      for(uint16_t j = 0; j < numcomps; ++j)
        scaleComponent<T>(image->comps + j, 12);
    }
    return image;
  }
  if(image)
    grk_object_unref(&image->obj);

  return nullptr;
}

#endif
