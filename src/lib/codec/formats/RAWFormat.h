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

#include "grk_apps_config.h"
#include "grok.h"
#include "spdlogwrapper.h"
#include "FileStandardIO.h"
#include "common.h"
#include "ImageFormat.h"
#include <memory>

class RAWFormat : public ImageFormat
{
public:
  explicit RAWFormat(bool isBig) : bigEndian(isBig) {}
  bool writeHeader(void) override;
  bool writeImage() override;
  bool writeImageBand(uint32_t yBegin, uint32_t yEnd) override;
  using ImageFormat::writeStrip;
  bool writeFinish(void) override;
  std::optional<ImageFileHeader> readHeader(const std::string& filename,
                                            grk_cparameters* parameters) override;
  grk_image* readPixels(grk_cparameters* parameters, grk_data_type dataType) override;

private:
  bool bigEndian;

  template<typename T>
  bool readSamples(grk_image* image, const grk_raw_cparameters* raw_cp);

  template<typename WT>
  bool writeToFile(FILE* fileStream_, bool bigEndian, int32_t* ptr, uint32_t w, uint32_t stride,
                   uint32_t h, int32_t lower, int32_t upper);

  template<typename RT, typename T>
  bool readFile(FILE* fileHandle, bool bigEndian, T* ptr, uint64_t nloop);
};

template<typename WT>
bool RAWFormat::writeToFile(FILE* fileStream_, bool bigEndian, int32_t* ptr, uint32_t w,
                            uint32_t stride, uint32_t h, int32_t lower, int32_t upper)
{
  constexpr size_t bufferLength = (1 << 16) / sizeof(WT);
  WT buffer[bufferLength];
  size_t buffered = 0;
  for(uint32_t row = 0; row < h; ++row, ptr += stride)
  {
    for(uint32_t column = 0; column < w;)
    {
      auto count = std::min<size_t>(w - column, bufferLength - buffered);
      auto source = ptr + column;
      auto destination = buffer + buffered;
      for(size_t i = 0; i < count; ++i)
        destination[i] =
            grk::endian<WT>((WT)std::clamp<int32_t>(source[i], lower, upper), bigEndian);
      column += (uint32_t)count;
      buffered += count;
      if(buffered == bufferLength)
      {
        if(fwrite(buffer, sizeof(WT), buffered, fileStream_) != buffered)
          return false;
        buffered = 0;
      }
    }
  }
  if(buffered && fwrite(buffer, sizeof(WT), buffered, fileStream_) != buffered)
    return false;

  return true;
}

template<typename RT, typename T>
bool RAWFormat::readFile(FILE* fileHandle, bool bigEndian, T* ptr, uint64_t nloop)
{
  const size_t bufSize = 4096;
  RT buf[bufSize];

  for(uint64_t i = 0; i < nloop; i += bufSize)
  {
    size_t target = (i + bufSize > nloop) ? (nloop - i) : bufSize;
    size_t ct = fread(buf, sizeof(RT), target, fileHandle);
    if(ct != target)
      return false;
    RT* inPtr = buf;
    for(size_t j = 0; j < ct; j++)
      *(ptr++) = (T)grk::endian<RT>(*inPtr++, bigEndian);
  }

  return true;
}

inline bool RAWFormat::writeHeader(void)
{
  writeState_ = IMAGE_FORMAT_HEADER_WRITTEN;
  return true;
}

inline bool RAWFormat::writeImage(void)
{
  const char* outfile = fileName_.c_str();
  uint16_t compno, numcomps;
  bool success = false;

  if((image_->decompress_num_comps * image_->x1 * image_->y1) == 0)
  {
    spdlog::error("writeImage: invalid raw image_ parameters");
    goto beach;
  }

  numcomps = image_->decompress_num_comps;
  for(compno = 1; compno < numcomps; ++compno)
  {
    if(image_->comps[0].dx != image_->comps[compno].dx)
      break;
    if(image_->comps[0].dy != image_->comps[compno].dy)
      break;
    if(image_->comps[0].prec != image_->comps[compno].prec)
      break;
    if(image_->comps[0].sgnd != image_->comps[compno].sgnd)
      break;
  }
  if(compno != numcomps)
  {
    spdlog::error("writeImage: All components shall have the same subsampling, same bit depth, "
                  "same sign.");
    goto beach;
  }
  if(fileIO_)
    delete fileIO_;
  fileIO_ = new FileStandardIO();
  if(!fileIO_->open(outfile, "wb"))
    goto beach;

  spdlog::info("writeImage: raw image_ characteristics: {} components",
               image_->decompress_num_comps);

  for(compno = 0; compno < image_->decompress_num_comps; compno++)
  {
    auto comp = image_->comps + compno;
    spdlog::info("Component {} characteristics: {}x{}x{} {}", compno, comp->w, comp->h, comp->prec,
                 comp->sgnd == 1 ? "signed" : "unsigned");

    if(!comp->data)
    {
      spdlog::error("writeImage: component {} is null.", compno);

      goto beach;
    }
    auto w = comp->w;
    auto h = comp->h;
    auto stride = comp->stride;
    bool sgnd = comp->sgnd;
    auto prec = comp->prec;

    int32_t lower = sgnd ? -(1 << (prec - 1)) : 0;
    int32_t upper = sgnd ? -lower - 1 : (1 << comp->prec) - 1;
    bool isInt16 = comp->data_type == GRK_INT_16;

    bool rc;
    if(prec <= 8)
    {
      if(isInt16)
      {
        // widen int16 → int32 for writeToFile; data is 8-bit so this path is rare
        auto src16 = (int16_t*)comp->data;
        auto widened = std::make_unique<int32_t[]>((uint64_t)stride * h);
        for(uint64_t idx = 0; idx < (uint64_t)stride * h; ++idx)
          widened[idx] = src16[idx];
        if(sgnd)
          rc = writeToFile<int8_t>(fileIO_->getFileHandle(), bigEndian, widened.get(), w, stride, h,
                                   lower, upper);
        else
          rc = writeToFile<uint8_t>(fileIO_->getFileHandle(), bigEndian, widened.get(), w, stride,
                                    h, lower, upper);
      }
      else
      {
        auto ptr = (int32_t*)comp->data;
        if(sgnd)
          rc = writeToFile<int8_t>(fileIO_->getFileHandle(), bigEndian, ptr, w, stride, h, lower,
                                   upper);
        else
          rc = writeToFile<uint8_t>(fileIO_->getFileHandle(), bigEndian, ptr, w, stride, h, lower,
                                    upper);
      }
      if(!rc)
        spdlog::error("writeImage: failed to write bytes for {}", outfile);
    }
    else if(prec <= 16)
    {
      if(isInt16)
      {
        auto src16 = (int16_t*)comp->data;
        auto widened = std::make_unique<int32_t[]>((uint64_t)stride * h);
        for(uint64_t idx = 0; idx < (uint64_t)stride * h; ++idx)
          widened[idx] = src16[idx];
        if(sgnd)
          rc = writeToFile<int16_t>(fileIO_->getFileHandle(), bigEndian, widened.get(), w, stride,
                                    h, lower, upper);
        else
          rc = writeToFile<uint16_t>(fileIO_->getFileHandle(), bigEndian, widened.get(), w, stride,
                                     h, lower, upper);
      }
      else
      {
        auto ptr = (int32_t*)comp->data;
        if(sgnd)
          rc = writeToFile<int16_t>(fileIO_->getFileHandle(), bigEndian, ptr, w, stride, h, lower,
                                    upper);
        else
          rc = writeToFile<uint16_t>(fileIO_->getFileHandle(), bigEndian, ptr, w, stride, h, lower,
                                     upper);
      }
      if(!rc)
        spdlog::error("writeImage: failed to write bytes for {}", outfile);
    }
    else
    {
      spdlog::error("writeImage: invalid precision: {}", comp->prec);
      goto beach;
    }
  }
  success = true;

beach:

  return success;
}

inline bool RAWFormat::writeImageBand(uint32_t yBegin, uint32_t yEnd)
{
  // RAW writes component-planar data — band-by-band not supported
  if(yBegin != 0 || yEnd != image_->comps[0].h)
  {
    spdlog::error("RAWFormat: partial band writing not supported");
    return false;
  }
  return writeImage();
}

inline bool RAWFormat::writeFinish(void)
{
  return fileIO_->close();
}

inline std::optional<ImageFileHeader> RAWFormat::readHeader(const std::string& filename,
                                                            grk_cparameters* parameters)
{
  grk_raw_cparameters* raw_cp = &parameters->raw_cp;
  if(!(raw_cp->width && raw_cp->height && raw_cp->numcomps && raw_cp->prec))
  {
    spdlog::error("invalid raw image parameters");
    spdlog::error("Please use the Format option -F:");
    spdlog::error("-F <width>,<height>,<ncomp>,<bitdepth>,{s,u}@<dx1>x<dy1>:...:<dxn>x<dyn>");
    spdlog::error("If subsampling is omitted, 1x1 is assumed for all components");
    spdlog::error("Example: -i image.raw -o image.j2k -F 512,512,3,8,u@1x1:2x2:2x2");
    spdlog::error("         for raw 512x512 image with 4:2:0 subsampling");
    return std::nullopt;
  }
  for(uint16_t compno = 0; compno < raw_cp->numcomps; compno++)
  {
    if(raw_cp->comps[compno].dx * raw_cp->comps[compno].dy != 1)
    {
      spdlog::error("Subsampled raw images are not currently supported");
      return std::nullopt;
    }
  }
  if(raw_cp->prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
  {
    spdlog::error("Grok cannot encode raw components with bit depth higher than %d bits.",
                  GRK_MAX_SUPPORTED_IMAGE_PRECISION);
    return std::nullopt;
  }
  delete fileIO_;
  fileIO_ = new FileStandardIO();
  if(!fileIO_->open(filename, "rb"))
  {
    spdlog::error("Failed to open {} for reading", filename);
    return std::nullopt;
  }

  return ImageFileHeader{raw_cp->numcomps, raw_cp->prec};
}

inline grk_image* RAWFormat::readPixels(grk_cparameters* parameters, grk_data_type dataType)
{
  grk_raw_cparameters* raw_cp = &parameters->raw_cp;
  uint32_t subsampling_dx = parameters->subsampling_dx;
  uint32_t subsampling_dy = parameters->subsampling_dy;
  uint16_t numcomps = raw_cp->numcomps;
  uint32_t w = raw_cp->width;
  uint32_t h = raw_cp->height;
  GRK_COLOR_SPACE color_space = GRK_CLRSPC_UNKNOWN;
  uint16_t ch;
  bool success = false;
  grk_image* image = nullptr;

  if(numcomps == 1)
    color_space = GRK_CLRSPC_GRAY;
  else if((numcomps >= 3) && (parameters->mct == 0))
    color_space = GRK_CLRSPC_SYCC;
  else if((numcomps >= 3) && (parameters->mct != 2))
    color_space = GRK_CLRSPC_SRGB;

  auto cmptparm = std::make_unique<grk_image_comp[]>(numcomps);
  /* initialize image components */
  for(uint16_t i = 0; i < numcomps; i++)
  {
    memset(&cmptparm[i], 0, sizeof(grk_image_comp));
    cmptparm[i].prec = raw_cp->prec;
    cmptparm[i].sgnd = raw_cp->sgnd;
    cmptparm[i].data_type = dataType;
    cmptparm[i].dx = (uint8_t)(subsampling_dx * raw_cp->comps[i].dx);
    cmptparm[i].dy = (uint8_t)(subsampling_dy * raw_cp->comps[i].dy);
    cmptparm[i].w = w;
    cmptparm[i].h = h;
  }
  /* create the image */
  image = grk_image_new(numcomps, &cmptparm[0], color_space, true);
  if(!image)
    goto cleanup;

  /* set image offset and reference grid */
  image->x0 = parameters->image_offset_x0;
  image->y0 = parameters->image_offset_y0;
  image->x1 = parameters->image_offset_x0 + (w - 1) * subsampling_dx + 1;
  image->y1 = parameters->image_offset_y0 + (h - 1) * subsampling_dy + 1;

  if(!readSamplesAs(dataType,
                    [&](auto sample) { return readSamples<decltype(sample)>(image, raw_cp); }))
    goto cleanup;

  if(fread(&ch, 1, 1, fileIO_->getFileHandle()))
    spdlog::warn("End of raw file not reached... processing anyway");
  success = true;
cleanup:
  fileIO_->close();
  if(!success)
  {
    if(image)
      grk_object_unref(&image->obj);
    image = nullptr;
  }
  return image;
}

template<typename T>
bool RAWFormat::readSamples(grk_image* image, const grk_raw_cparameters* raw_cp)
{
  uint32_t w = raw_cp->width;
  uint32_t h = raw_cp->height;
  for(uint16_t compno = 0; compno < image->numcomps; compno++)
  {
    auto ptr = (T*)image->comps[compno].data;
    for(uint32_t j = 0; j < h; ++j)
    {
      bool rc;
      if(raw_cp->prec <= 8)
        rc = raw_cp->sgnd ? readFile<int8_t>(fileIO_->getFileHandle(), bigEndian, ptr, w)
                          : readFile<uint8_t>(fileIO_->getFileHandle(), bigEndian, ptr, w);
      else
        rc = raw_cp->sgnd ? readFile<int16_t>(fileIO_->getFileHandle(), bigEndian, ptr, w)
                          : readFile<uint16_t>(fileIO_->getFileHandle(), bigEndian, ptr, w);
      if(!rc)
      {
        spdlog::error("Error reading raw file. End of file probably reached.");
        return false;
      }
      ptr += image->comps[compno].stride;
    }
  }

  return true;
}
