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
#include "ImageFormat.h"

/**
 Load a single image component encoded in PGX file format
 @param filename Name of the PGX file to load
 @param parameters *List ?*
 @return a greyscale image if successful, returns nullptr otherwise
 */

#include "FileStandardIO.h"
#include "common.h"
#include "grk_apps_config.h"
#include "grok.h"
#include "spdlogwrapper.h"
#include "PGXFormat.h"
#include "convert.h"

template<typename T>
T readchar(FILE* f)
{
  T c1;
  if(!fread(&c1, 1, 1, f))
  {
    spdlog::error(" fread return a number of element different from the expected.");
    return 0;
  }
  return c1;
}

template<typename T>
T readshort(FILE* f, bool bigendian)
{
  uint8_t c1, c2;
  if(!fread(&c1, 1, 1, f))
  {
    spdlog::error("  fread return a number of element different from the expected.");
    return 0;
  }
  if(!fread(&c2, 1, 1, f))
  {
    spdlog::error("  fread return a number of element different from the expected.");
    return 0;
  }
  if(bigendian)
    return (T)((c1 << 8) + c2);
  else
    return (T)((c2 << 8) + c1);
}

class PGXFormat : public ImageFormat
{
public:
  bool writeHeader(void) override;
  bool writeImage() override;
  bool writeImageBand(uint32_t yBegin, uint32_t yEnd) override;
  using ImageFormat::writeStrip;
  bool writeFinish(void) override;
  std::optional<ImageFileHeader> readHeader(const std::string& filename,
                                            grk_cparameters* parameters) override;
  grk_image* readPixels(grk_cparameters* parameters, grk_data_type dataType) override;

private:
  template<typename T>
  void readSamples(grk_image* image);

  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint8_t prec_ = 0;
  bool sgnd_ = false;
  bool bigEndian_ = false;
};

inline std::optional<ImageFileHeader>
    PGXFormat::readHeader(const std::string& filename, [[maybe_unused]] grk_cparameters* parameters)
{
  uint32_t prec;
  uint64_t i = 0;
  int c;
  char endian1, endian2;
  char signtmp[32];
  char temp[32];
  FILE* f;

  sgnd_ = false;
  delete fileIO_;
  fileIO_ = new FileStandardIO();
  if(!fileIO_->open(filename, "rb"))
    return std::nullopt;
  f = fileIO_->getFileHandle();
  if(fscanf(f, "PG%31[ \t]%c%c%31[ \t+-]%u%31[ \t]%u%31[ \t]%u", temp, &endian1, &endian2, signtmp,
            &prec, temp, &width_, temp, &height_) != 9)
  {
    spdlog::error(" Failed to read the right number of element from the fscanf() function.");
    goto cleanup;
  }

  if(prec < 4)
  {
    spdlog::error("Precision must be >= 4");
    goto cleanup;
  }
  prec_ = (uint8_t)prec;
  while(signtmp[i] != '\0')
  {
    if(signtmp[i] == '-')
    {
      sgnd_ = true;
      break;
    }
    i++;
  }

  c = fgetc(f);
  if(c == EOF)
    goto cleanup;
  if(endian1 == 'M' && endian2 == 'L')
  {
    bigEndian_ = true;
  }
  else if(endian2 == 'M' && endian1 == 'L')
  {
    bigEndian_ = false;
  }
  else
  {
    spdlog::error("Bad pgx header, please check input file");
    goto cleanup;
  }
  return ImageFileHeader{1, prec_};
cleanup:
  fileIO_->close();
  return std::nullopt;
}

inline grk_image* PGXFormat::readPixels(grk_cparameters* parameters, grk_data_type dataType)
{
  grk_image_comp cmptparm; /* maximum of 1 component  */
  memset(&cmptparm, 0, sizeof(grk_image_comp));
  cmptparm.x0 = parameters->image_offset_x0;
  cmptparm.y0 = parameters->image_offset_y0;
  cmptparm.w = !cmptparm.x0 ? ((width_ - 1) * parameters->subsampling_dx + 1)
                            : cmptparm.x0 + (uint32_t)(width_ - 1) * parameters->subsampling_dx + 1;
  cmptparm.h = !cmptparm.y0
                   ? ((height_ - 1) * parameters->subsampling_dy + 1)
                   : cmptparm.y0 + (uint32_t)(height_ - 1) * parameters->subsampling_dy + 1;
  cmptparm.sgnd = sgnd_;
  cmptparm.prec = prec_;
  cmptparm.data_type = dataType;
  cmptparm.dx = parameters->subsampling_dx;
  cmptparm.dy = parameters->subsampling_dy;

  /* create the image */
  auto image = grk_image_new(1, &cmptparm, GRK_CLRSPC_GRAY, true);
  if(image)
  {
    /* set image offset and reference grid */
    image->x0 = cmptparm.x0;
    image->y0 = cmptparm.x0;
    image->x1 = cmptparm.w;
    image->y1 = cmptparm.h;
    if(!readSamplesAs(dataType, [&](auto sample) {
         readSamples<decltype(sample)>(image);
         return true;
       }))
    {
      grk_object_unref(&image->obj);
      image = nullptr;
    }
  }
  if(!fileIO_->close() && image)
  {
    grk_object_unref(&image->obj);
    image = nullptr;
  }

  return image;
}

template<typename T>
void PGXFormat::readSamples(grk_image* image)
{
  auto f = fileIO_->getFileHandle();
  uint64_t index = 0;
  uint32_t stride_diff = image->comps->stride - width_;
  uint8_t shift = (uint8_t)(sizeof(T) * 8 - prec_);
  auto dest = (T*)image->comps->data;
  for(uint32_t j = 0; j < height_; ++j)
  {
    for(uint32_t k = 0; k < width_; ++k)
    {
      int32_t v = 0;
      if(prec_ < 8)
      {
        v = sgnd_ ? sign_extend((T)readchar<int8_t>(f), shift) : readchar<int8_t>(f);
      }
      else
      {
        if(image->comps->prec == 8)
        {
          if(!image->comps->sgnd)
            v = (T)readchar<uint8_t>(f);
          else
            v = (T)readchar<int8_t>(f);
        }
        else
        {
          if(!image->comps->sgnd)
            v = (T)readshort<uint16_t>(f, bigEndian_);
          else
            v = (T)readshort<int16_t>(f, bigEndian_);
        }
      }
      dest[index++] = (T)v;
    }
    index += stride_diff;
  }
}

inline bool PGXFormat::writeHeader(void)
{
  if(!allComponentsSanityCheck(image_, false))
  {
    spdlog::error("PGXFormat::writeHeader: image sanity check failed.");
    return false;
  }

  writeState_ = IMAGE_FORMAT_HEADER_WRITTEN;
  return true;
}

inline bool PGXFormat::writeImage(void)
{
  bool success = false;
  for(uint16_t compno = 0; compno < image_->numcomps; compno++)
  {
    auto comp = &image_->comps[compno];
    if(comp->w == 0 || comp->h == 0)
      continue;
    int nbytes = 0;
    if(fileName_.size() < 4)
    {
      spdlog::error(" imagetopgx: output file name size less than 4.");
      goto beach;
    }
    auto pos = fileName_.rfind(".");
    if(pos == std::string::npos)
    {
      spdlog::error(" pgx was recognized but there was no dot at expected position .");
      goto beach;
    }
    std::string fileOut =
        fileName_.substr(0, pos) + std::string("_") + std::to_string(compno) + ".pgx";
    if(fileIO_)
      delete fileIO_;
    fileIO_ = new FileStandardIO();
    if(!fileIO_->open(fileOut.c_str(), "wb"))
    {
      spdlog::error("failed to open {} for writing", fileOut);
      goto beach;
    }

    if(!comp->data)
    {
      spdlog::error("PGXFormat: component {} has no data", compno);
      goto beach;
    }
    uint32_t w = comp->w;
    uint32_t h = comp->h;

    fprintf(fileIO_->getFileHandle(), "PG ML %c %u %u %u\n", comp->sgnd ? '-' : '+', comp->prec, w,
            h);

    if(comp->prec <= 8)
      nbytes = 1;
    else if(comp->prec <= 16)
      nbytes = 2;

    const size_t bufSize = 4096;
    size_t outCount = 0;
    size_t index = 0;
    uint32_t stride_diff = comp->stride - w;
    bool isInt16 = comp->data_type == GRK_INT_16;
    if(nbytes == 1)
    {
      uint8_t buf[bufSize];
      uint8_t* outPtr = buf;
      for(uint32_t j = 0; j < h; ++j)
      {
        for(uint32_t i = 0; i < w; ++i)
        {
          const int32_t val =
              isInt16 ? ((int16_t*)comp->data)[index++] : ((int32_t*)comp->data)[index++];
          if(!grk::writeBytes<uint8_t>((uint8_t)val, buf, &outPtr, &outCount, bufSize, true,
                                       fileIO_->getFileHandle()))
          {
            spdlog::error("failed to write bytes for {}", fileOut);
            goto beach;
          }
        }
        index += stride_diff;
      }
      if(outCount)
      {
        size_t res = fwrite(buf, sizeof(uint8_t), outCount, fileIO_->getFileHandle());
        if(res != outCount)
        {
          spdlog::error("failed to write bytes for {}", fileOut);
          goto beach;
        }
      }
    }
    else
    {
      uint16_t buf[bufSize];
      uint16_t* outPtr = buf;
      for(uint32_t j = 0; j < h; ++j)
      {
        for(uint32_t i = 0; i < w; ++i)
        {
          const int32_t val = isInt16 ? ((int16_t*)image_->comps[compno].data)[index++]
                                      : ((int32_t*)image_->comps[compno].data)[index++];
          if(!grk::writeBytes<uint16_t>((uint16_t)val, buf, &outPtr, &outCount, bufSize, true,
                                        fileIO_->getFileHandle()))
          {
            spdlog::error("failed to write bytes for {}", fileOut);
            goto beach;
          }
        }
        index += stride_diff;
      }
      if(outCount)
      {
        size_t res = fwrite(buf, sizeof(uint16_t), outCount, fileIO_->getFileHandle());
        if(res != outCount)
        {
          spdlog::error("failed to write bytes for {}", fileOut);
          goto beach;
        }
      }
    }
    bool closed = fileIO_->close();
    delete fileIO_;
    fileIO_ = nullptr;
    if(!closed)
      goto beach;
  }
  success = true;
beach:
  return success;
}

inline bool PGXFormat::writeImageBand(uint32_t yBegin, uint32_t yEnd)
{
  // PGX writes separate files per component — band-by-band not supported
  if(yBegin != 0 || yEnd != image_->comps[0].h)
  {
    spdlog::error("PGXFormat: partial band writing not supported");
    return false;
  }
  return writeImage();
}

inline bool PGXFormat::writeFinish(void)
{
  return !fileIO_ || fileIO_->close();
}
