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
#include "grok.h"
#include "spdlogwrapper.h"
#include "FileStandardIO.h"
#include "common.h"
#include "ImageFormat.h"

template<typename T>
class RAWFormat : public ImageFormat
{
public:
  explicit RAWFormat(bool isBig) : bigEndian(isBig) {}
  bool encodeHeader(void) override;
  bool encodePixels() override;
  using ImageFormat::encodePixels;
  bool encodeFinish(void) override;
  grk_image* decode(const std::string& filename, grk_cparameters* parameters) override;

private:
  bool bigEndian;
  grk_image* decode(const char* filename, grk_cparameters* parameters, bool big_endian);

  template<typename WT>
  bool writeToFile(FILE* fileStream_, bool bigEndian, T* ptr, uint32_t w, uint32_t stride,
                   uint32_t h, T lower, T upper);

  template<typename RT>
  bool readFile(FILE* fileHandle, bool bigEndian, T* ptr, uint64_t nloop);
};

template<typename T>
template<typename WT>
bool RAWFormat<T>::writeToFile(FILE* fileStream_, bool bigEndian, T* ptr, uint32_t w,
                               uint32_t stride, uint32_t h, T lower, T upper)
{
  const size_t bufSize = 4096;
  WT buf[bufSize];
  WT* outPtr = buf;
  size_t outCount = 0;
  auto stride_diff = stride - w;
  for(uint32_t j = 0; j < h; ++j)
  {
    for(uint32_t i = 0; i < w; ++i)
    {
      T curr = *ptr++;
      if(curr > upper)
        curr = upper;
      else if(curr < lower)
        curr = lower;
      if(!grk::writeBytes<WT>((WT)curr, buf, &outPtr, &outCount, bufSize, bigEndian, fileStream_))
        return false;
    }
    ptr += stride_diff;
  }
  // flush
  if(outCount)
  {
    size_t res = fwrite(buf, sizeof(WT), outCount, fileStream_);
    if(res != outCount)
      return false;
  }

  return true;
}

template<typename T>
template<typename RT>
bool RAWFormat<T>::readFile(FILE* fileHandle, bool bigEndian, T* ptr, uint64_t nloop)
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
      *(ptr++) = grk::endian<RT>(*inPtr++, bigEndian);
  }

  return true;
}

template<typename T>
bool RAWFormat<T>::encodeHeader(void)
{
  encodeState = IMAGE_FORMAT_ENCODED_HEADER;
  return true;
}

template<typename T>
bool RAWFormat<T>::encodePixels(void)
{
  const char* outfile = fileName_.c_str();
  uint16_t compno, numcomps;
  bool success = false;

  if((image_->decompress_num_comps * image_->x1 * image_->y1) == 0)
  {
    spdlog::error("encodePixels: invalid raw image_ parameters");
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
    spdlog::error("encodePixels: All components shall have the same subsampling, same bit depth, "
                  "same sign.");
    goto beach;
  }
  if(fileIO_)
    delete fileIO_;
  fileIO_ = new FileStandardIO();
  if(!fileIO_->open(outfile, "wb"))
    goto beach;

  spdlog::info("encodePixels: raw image_ characteristics: {} components",
               image_->decompress_num_comps);

  for(compno = 0; compno < image_->decompress_num_comps; compno++)
  {
    auto comp = image_->comps + compno;
    spdlog::info("Component {} characteristics: {}x{}x{} {}", compno, comp->w, comp->h, comp->prec,
                 comp->sgnd == 1 ? "signed" : "unsigned");

    if(!comp->data)
    {
      spdlog::error("encodePixels: component {} is null.", compno);

      goto beach;
    }
    auto w = comp->w;
    auto h = comp->h;
    auto stride = comp->stride;
    bool sgnd = comp->sgnd;
    auto prec = comp->prec;

    T lower = sgnd ? -(1 << (prec - 1)) : 0;
    T upper = sgnd ? -lower - 1 : (1 << comp->prec) - 1;
    auto ptr = (T*)comp->data;

    bool rc;
    if(prec <= 8)
    {
      if(sgnd)
        rc = writeToFile<int8_t>(fileIO_->getFileHandle(), bigEndian, ptr, w, stride, h, lower,
                                 upper);
      else
        rc = writeToFile<uint8_t>(fileIO_->getFileHandle(), bigEndian, ptr, w, stride, h, lower,
                                  upper);
      if(!rc)
        spdlog::error("encodePixels: failed to write bytes for {}", outfile);
    }
    else if(prec <= 16)
    {
      if(sgnd)
        rc = writeToFile<int16_t>(fileIO_->getFileHandle(), bigEndian, ptr, w, stride, h, lower,
                                  upper);
      else
        rc = writeToFile<uint16_t>(fileIO_->getFileHandle(), bigEndian, ptr, w, stride, h, lower,
                                   upper);
      if(!rc)
        spdlog::error("encodePixels: ailed to write bytes for {}", outfile);
    }
    else
    {
      spdlog::error("encodePixels: invalid precision: {}", comp->prec);
      goto beach;
    }
  }
  success = true;

beach:

  return success;
}

template<typename T>
bool RAWFormat<T>::encodeFinish(void)
{
  return fileIO_->close();
}

template<typename T>
grk_image* RAWFormat<T>::decode(const std::string& filename, grk_cparameters* parameters)
{
  return decode(filename.c_str(), parameters, bigEndian);
}

template<typename T>
grk_image* RAWFormat<T>::decode(const char* filename, grk_cparameters* parameters, bool bigEndian)
{
  grk_raw_cparameters* raw_cp = &parameters->raw_cp;
  uint32_t subsampling_dx = parameters->subsampling_dx;
  uint32_t subsampling_dy = parameters->subsampling_dy;

  uint32_t i, w, h;
  uint16_t numcomps, compno;
  GRK_COLOR_SPACE color_space = GRK_CLRSPC_UNKNOWN;
  grk_image_comp* cmptparm;
  grk_image* image = nullptr;
  uint16_t ch;
  bool success = false;

  if(!(raw_cp->width && raw_cp->height && raw_cp->numcomps && raw_cp->prec))
  {
    spdlog::error("invalid raw image parameters");
    spdlog::error("Please use the Format option -F:");
    spdlog::error("-F <width>,<height>,<ncomp>,<bitdepth>,{s,u}@<dx1>x<dy1>:...:<dxn>x<dyn>");
    spdlog::error("If subsampling is omitted, 1x1 is assumed for all components");
    spdlog::error("Example: -i image.raw -o image.j2k -F 512,512,3,8,u@1x1:2x2:2x2");
    spdlog::error("         for raw 512x512 image with 4:2:0 subsampling");
    return nullptr;
  }
  if(fileIO_)
    delete fileIO_;
  fileIO_ = new FileStandardIO();
  if(!fileIO_->open(filename, "rb"))
  {
    spdlog::error("Failed to open {} for reading", filename);
    goto cleanup;
  }

  numcomps = raw_cp->numcomps;
  if(numcomps == 1)
    color_space = GRK_CLRSPC_GRAY;
  else if((numcomps >= 3) && (parameters->mct == 0))
    color_space = GRK_CLRSPC_SYCC;
  else if((numcomps >= 3) && (parameters->mct != 2))
    color_space = GRK_CLRSPC_SRGB;

  w = raw_cp->width;
  h = raw_cp->height;
  cmptparm = new grk_image_comp[numcomps];
  /* initialize image components */
  for(i = 0; i < numcomps; i++)
  {
    memset(cmptparm + i, 0, sizeof(grk_image_comp));
    cmptparm[i].prec = raw_cp->prec;
    cmptparm[i].sgnd = raw_cp->sgnd;
    cmptparm[i].dx = (uint8_t)(subsampling_dx * raw_cp->comps[i].dx);
    cmptparm[i].dy = (uint8_t)(subsampling_dy * raw_cp->comps[i].dy);
    cmptparm[i].w = w;
    cmptparm[i].h = h;

    if(raw_cp->comps[i].dx * raw_cp->comps[i].dy != 1)
    {
      spdlog::error("Subsampled raw images are not currently supported");
      goto cleanup;
    }
  }
  /* create the image */
  image = grk_image_new(numcomps, &cmptparm[0], color_space, true);
  delete[] cmptparm;

  /* set image offset and reference grid */
  image->x0 = parameters->image_offset_x0;
  image->y0 = parameters->image_offset_y0;
  image->x1 = parameters->image_offset_x0 + (w - 1) * subsampling_dx + 1;
  image->y1 = parameters->image_offset_y0 + (h - 1) * subsampling_dy + 1;

  if(raw_cp->prec <= 8)
  {
    for(compno = 0; compno < numcomps; compno++)
    {
      auto ptr = (T*)image->comps[compno].data;
      for(uint32_t j = 0; j < h; ++j)
      {
        bool rc;
        if(raw_cp->sgnd)
          rc = readFile<int8_t>(fileIO_->getFileHandle(), bigEndian, ptr, w);
        else
          rc = readFile<uint8_t>(fileIO_->getFileHandle(), bigEndian, ptr, w);
        if(!rc)
        {
          spdlog::error("Error reading raw file. End of file probably reached.");
          goto cleanup;
        }
        ptr += image->comps[compno].stride;
      }
    }
  }
  else if(raw_cp->prec <= 16)
  {
    for(compno = 0; compno < numcomps; compno++)
    {
      auto ptr = (T*)image->comps[compno].data;
      for(uint32_t j = 0; j < h; ++j)
      {
        bool rc;
        if(raw_cp->sgnd)
          rc = readFile<int16_t>(fileIO_->getFileHandle(), bigEndian, ptr, w);
        else
          rc = readFile<uint16_t>(fileIO_->getFileHandle(), bigEndian, ptr, w);
        if(!rc)
        {
          spdlog::error("Error reading raw file. End of file probably reached.");
          goto cleanup;
        }
        ptr += image->comps[compno].stride;
      }
    }
  }
  else
  {
    spdlog::error("Grok cannot encode raw components with bit depth higher than %d bits.",
                  GRK_MAX_SUPPORTED_IMAGE_PRECISION);
    goto cleanup;
  }

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
