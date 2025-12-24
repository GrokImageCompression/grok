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
#include "FileOrchestratorIO.h"
#include <sstream>
#include "FileStandardIO.h"
#include "grk_apps_config.h"
#include <iterator>
#include "grok.h"
#include "spdlogwrapper.h"
#include "convert.h"
#include "common.h"

enum PNM_COLOUR_SPACE
{
  PNM_UNKNOWN,
  PNM_BW,
  PNM_GRAY,
  PNM_GRAYA,
  PNM_RGB,
  PNM_RGBA
};

struct pnm_header
{
  uint32_t width, height, maxval, depth, format;
  PNM_COLOUR_SPACE colour_space;
};

template<typename T>
class PNMFormat : public ImageFormat
{
public:
  explicit PNMFormat(bool split);
  bool encodeHeader(void) override;
  bool encodePixels() override;
  using ImageFormat::encodePixels;
  bool encodeFinish(void) override;
  grk_image* decode(const std::string& filename, grk_cparameters* parameters) override;

private:
  bool forceSplit;
  bool hasAlpha(void);
  bool isOpacity(uint16_t compno);
  bool hasOpacity(void);
  bool doNonSplitEncode(void);
  bool writeHeader(bool doPGM);
  template<typename WT>
  bool writeRows(uint32_t rowsOffset, uint32_t rows, uint16_t compno, WT* buf, size_t* outCount);
  template<typename WT>
  bool encodeRows(uint32_t rows);
  template<typename WT>
  bool readBytes(FILE* fp, grk_image* image, size_t area);
  bool closeStream(void);

  grk_image* decode(const grk_cparameters* parameters);
  bool decodeHeader(struct pnm_header* ph);
};

template<typename T>
PNMFormat<T>::PNMFormat(bool split) : forceSplit(split)
{}

template<typename T>
bool PNMFormat<T>::encodeHeader(void)
{
  if(isHeaderEncoded())
    return true;

  if(!allComponentsSanityCheck(image_, true))
  {
    spdlog::error("PNMFormat<T>::encodeHeader: image sanity check failed.");
    return false;
  }
  if(!areAllComponentsSameSubsampling(image_))
    return false;

  uint16_t ncomp = image_->decompress_num_comps;
  if(ncomp > 4)
  {
    spdlog::error("PNMFormat<T>::encodeHeader: Number of components cannot be greater than 4; %u "
                  "number of components not supported.",
                  image_->decompress_num_comps);
    return false;
  }
  if(hasOpacity() && !hasAlpha())
  {
    spdlog::error("PNMFormat: alpha channel must be stored in final component of image");
    return false;
  }
  if(useStdIO() && forceSplit)
  {
    spdlog::warn("Unable to write split file to stdout. Disabling");
    forceSplit = false;
  }
  // write first header if we start with non-split encode
  if(doNonSplitEncode())
  {
    if(!orchestrator.open(fileName_, "wb", true))
      return false;
    if(!writeHeader(false))
      return false;
  }
  encodeState = IMAGE_FORMAT_ENCODED_HEADER;

  return true;
}

template<typename T>
bool PNMFormat<T>::writeHeader(bool doPGM)
{
  std::ostringstream iss;
  uint32_t prec = image_->decompress_prec;
  uint32_t width = image_->decompress_width;
  uint32_t height = image_->decompress_height;
  uint32_t max = (uint32_t)((1U << prec) - 1);

  if(doPGM || image_->decompress_num_comps == 1)
  {
    iss << "P5\n#Grok-" << grk_version() << "\n" << width << " " << height << "\n" << max << "\n";
  }
  else
  {
    if(hasAlpha())
    {
      uint16_t ncomp = image_->decompress_num_comps;
      iss << "P7\n# Grok-" << grk_version() << "\nWIDTH " << width << "\nHEIGHT " << height;
      iss << "\nDEPTH " << ncomp << "\nMAXVAL " << max;
      iss << "\nTUPLTYPE " << ((ncomp >= 3) ? "RGB_ALPHA" : "GRAYSCALE_ALPHA") << "\nENDHDR\n";
    }
    else
    {
      iss << "P6\n# Grok-" << grk_version() << "\n"
          << width << " " << height << "\n"
          << max << "\n";
    }
  }
  auto str = iss.str();
  size_t res;
  if(fileIO_ && fileIO_->getFileHandle() != nullptr)
    res = fwrite(str.c_str(), sizeof(uint8_t), str.size(), fileIO_->getFileHandle());
  else
    res = orchestrator.write((uint8_t*)str.c_str(), str.size());

  return res == str.size();
}

const size_t bufSize = 4096;

template<typename T>
bool PNMFormat<T>::doNonSplitEncode(void)
{
  return !forceSplit || image_->decompress_num_comps > 1;
}

/***
 * application-orchestrated pixel encoding
 */
template<typename T>
bool PNMFormat<T>::encodePixels(void)
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

  return (image_->decompress_prec > 8U) ? encodeRows<uint16_t>(image_->decompress_height)
                                        : encodeRows<uint8_t>(image_->decompress_height);
}

template<typename T>
bool PNMFormat<T>::encodeFinish(void)
{
  if(encodeState & IMAGE_FORMAT_ENCODED_PIXELS)
    return true;

  encodeState |= IMAGE_FORMAT_ENCODED_PIXELS;

  return orchestrator.close() && closeStream();
}

/***
 * application-orchestrated pixel encoding of entire image
 */
template<typename T>
template<typename WT>
bool PNMFormat<T>::encodeRows([[maybe_unused]] uint32_t rows)
{
  uint16_t ncomp = image_->numcomps;
  bool success = false;
  uint32_t height = image_->decompress_height;

  // 1. write first file: PAM or PPM
  if(doNonSplitEncode())
  {
    uint16_t decompress_num_comps = image_->decompress_num_comps;
    auto planes = std::make_unique<T*[]>(decompress_num_comps);
    for(uint32_t i = 0U; i < decompress_num_comps; ++i)
      planes[i] = (T*)image_->comps[i].data;
    uint32_t h = 0;
    GrkIOBuf packedBuf;
    T adjust = (image_->comps[0].sgnd ? 1 << (image_->decompress_prec - 1) : 0);
    auto iter = grk::InterleaverFactory<int32_t>::makeInterleaver(
        image_->decompress_prec > 8U ? grk::packer16BitBE : 8);

    if(!iter)
      goto cleanup;
    while(h < height)
    {
      uint32_t stripRows = (std::min)(image_->rows_per_strip, height - h);
      packedBuf = pool.get(image_->packed_row_bytes * stripRows);
      iter->interleave(((T**)planes.get()), decompress_num_comps, packedBuf.data,
                       image_->decompress_width, image_->comps[0].stride, image_->packed_row_bytes,
                       stripRows, adjust);
      packedBuf.pooled = true;
      packedBuf.offset = orchestrator.getOffset();
      packedBuf.len = image_->packed_row_bytes * stripRows;
      packedBuf.index = orchestrator.getNumPooledRequests();
      if(!encodePixelsCore(0, packedBuf))
      {
        delete iter;
        applicationOrchestratedReclaim(packedBuf);
        goto cleanup;
      }
      h += stripRows;
      applicationOrchestratedReclaim(packedBuf);
    }
    delete iter;

    if(!orchestrator.close())
      goto cleanup;
    if(!forceSplit)
    {
      success = true;
      goto cleanup;
    }
  }
  // 2. write split files (PGM)
  for(uint16_t compno = 0; compno < ncomp; compno++)
  {
    std::string destname;
    if(ncomp > 1)
    {
      size_t lastindex = fileName_.find_last_of(".");
      if(lastindex == std::string::npos)
      {
        spdlog::error(" imagetopnm: missing file tag");
        goto cleanup;
      }
      std::string rawname = fileName_.substr(0, lastindex);
      std::ostringstream iss;
      iss << rawname << "_" << compno << ".pgm";
      destname = iss.str().c_str();
    }
    else
    {
      destname = fileName_;
    }
    if(fileIO_)
      delete fileIO_;
    fileIO_ = new FileStandardIO();
    if(!fileIO_->open(destname, "wb"))
      goto cleanup;
    if(!writeHeader(true))
      goto cleanup;
    size_t outCount = 0;
    WT buf[bufSize];
    uint32_t i = 0;
    for(; i + image_->rows_per_strip < height; i += image_->rows_per_strip)
    {
      if(!writeRows<WT>(i, image_->rows_per_strip, compno, buf, &outCount))
        goto cleanup;
    }
    if(i < height)
    {
      if(!writeRows<WT>(i, height - i, compno, buf, &outCount))
        goto cleanup;
    }
    if(outCount)
    {
      size_t res = fwrite(buf, sizeof(WT), outCount, fileIO_->getFileHandle());
      if(res != outCount)
        goto cleanup;
    }
    if(!closeStream())
      goto cleanup;
  } /* for (compno */
  success = true;
cleanup:
  return orchestrator.close() && closeStream() && success;
}

template<typename T>
template<typename WT>
bool PNMFormat<T>::writeRows(uint32_t rowsOffset, uint32_t rows, uint16_t compno, WT* buf,
                             size_t* outCount)
{
  if(rows == 0)
  {
    spdlog::warn("PNMFormat: Attempt to write zero rows");
    return true;
  }
  uint16_t ncomp = image_->decompress_num_comps;
  bool singleComp = compno <= 4;
  if(!singleComp && !hasAlpha())
    ncomp = (std::min)(ncomp, (uint16_t)3);
  uint32_t width = image_->decompress_width;
  uint32_t stride_diff = image_->comps[0].stride - width;
  // all components have same sign and precision
  T adjust = (image_->comps[0].sgnd ? 1 << (image_->decompress_prec - 1) : 0);
  T* compPtr[4] = {nullptr, nullptr, nullptr, nullptr};
  WT* outPtr = buf + *outCount;
  uint16_t start = singleComp ? compno : 0;
  uint16_t end = singleComp ? compno + 1 : ncomp;
  for(uint16_t comp = start; comp < end; ++comp)
    compPtr[comp] = (T*)(image_->comps + comp)->data + rowsOffset * image_->comps[0].stride;
  for(uint32_t j = 0; j < rows; ++j)
  {
    for(uint32_t i = 0; i < width; ++i)
    {
      for(uint16_t comp = start; comp < end; ++comp)
      {
        T v = *compPtr[comp]++ + adjust;
        if(fileIO_->getFileHandle())
        {
          if(!grk::writeBytes<WT>((WT)v, buf, &outPtr, outCount, bufSize, true,
                                  fileIO_->getFileHandle()))
            return false;
        }
        else
        {
          if(!grk::writeBytes<WT>((WT)v, buf, &outPtr, outCount, bufSize, true, &orchestrator))
            return false;
        }
      }
    }
    for(uint16_t i = start; i < end; ++i)
      compPtr[i] += stride_diff;
  }

  return true;
}

template<typename T>
bool PNMFormat<T>::hasAlpha(void)
{
  if(!image_)
    return false;
  uint16_t ncomp = image_->decompress_num_comps;
  return (ncomp == 4 && isOpacity(ncomp - 1)) || (ncomp == 2 && isOpacity(ncomp - 1));
}

template<typename T>
bool PNMFormat<T>::isOpacity(uint16_t compno)
{
  if(!image_ || compno >= image_->decompress_num_comps)
    return false;
  auto comp = image_->comps + compno;

  return (comp->type == GRK_CHANNEL_TYPE_OPACITY ||
          comp->type == GRK_CHANNEL_TYPE_PREMULTIPLIED_OPACITY);
}

template<typename T>
bool PNMFormat<T>::hasOpacity(void)
{
  if(!image_)
    return false;
  for(uint16_t i = 0; i < image_->decompress_num_comps; ++i)
  {
    if(isOpacity(i))
      return true;
  }

  return false;
}

template<typename T>
bool PNMFormat<T>::closeStream(void)
{
  return !fileIO_ || fileIO_->close();
}

static char* skip_white(char* s)
{
  if(!s)
    return nullptr;
  while(*s)
  {
    if(*s == '\n' || *s == '\r' || *s == '\t')
      return nullptr;
    if(isspace(*s))
    {
      ++s;
      continue;
    }
    return s;
  }
  return nullptr;
}

static char* skip_int(char* start, uint32_t* out_n)
{
  char* s;
  char c;

  *out_n = 0;

  s = skip_white(start);
  if(s == nullptr)
    return nullptr;
  start = s;

  while(*s)
  {
    if(!isdigit(*s))
      break;
    ++s;
  }
  c = *s;
  *s = 0;
  *out_n = (uint32_t)atoi(start);
  *s = c;
  return s;
}

static int32_t convert(const std::string& s)
{
  try
  {
    return stoi(s);
  }
  catch([[maybe_unused]] std::invalid_argument const& e)
  {
    spdlog::error("Bad input: std::invalid_argument thrown");
  }
  catch([[maybe_unused]] std::out_of_range const& e)
  {
    spdlog::error("Integer overflow: std::out_of_range thrown");
  }
  return -1;
}

static bool header_rewind(char* s, char* line, FILE* reader)
{
  // if s points to ' ', then rewind file
  // to two past current position of s
  if(*s == ' ')
  {
    auto len = (int64_t)s - (int64_t)line;
    if(GRK_FSEEK(reader, -int64_t(strlen(line)) + len + 2, SEEK_CUR))
      return false;
  }
  return true;
}

template<typename T>
bool PNMFormat<T>::decodeHeader(struct pnm_header* ph)
{
  uint32_t format;
  const size_t lineSize = 256;
  const size_t lineSearch = 250;
  char line[lineSize];
  char c;

  if(fread(&c, 1, 1, fileIO_->getFileHandle()) != 1)
  {
    spdlog::error(" fread error");
    return false;
  }
  if(c != 'P')
  {
    spdlog::error("read_pnm_header:PNM:magic P missing");
    return false;
  }
  if(fread(&c, 1, 1, fileIO_->getFileHandle()) != 1)
  {
    spdlog::error(" fread error");
    return false;
  }
  format = (uint32_t)(c - 48);
  if(format < 1 || format > 7)
  {
    spdlog::error("read_pnm_header:magic format {} invalid", format);
    return false;
  }
  ph->format = format;
  if(format == 7)
  {
    uint32_t end = 0;
    while(fgets(line, lineSearch, fileIO_->getFileHandle()))
    {
      if(*line == '#' || *line == '\n')
        continue;

      std::istringstream iss(line);
      std::vector<std::string> tokens{std::istream_iterator<std::string>{iss},
                                      std::istream_iterator<std::string>{}};
      if(tokens.size() == 0)
        continue;
      std::string idf = tokens[0];
      if(idf == "ENDHDR")
      {
        end = 1;
        break;
      }
      if(tokens.size() == 2)
      {
        int32_t temp;
        if(idf == "WIDTH")
        {
          temp = convert(tokens[1]);
          if(temp < 1)
          {
            spdlog::error("Invalid width");
            return false;
          }
          ph->width = (uint32_t)temp;
        }
        else if(idf == "HEIGHT")
        {
          temp = convert(tokens[1]);
          if(temp < 1)
          {
            spdlog::error("Invalid height");
            return false;
          }
          ph->height = (uint32_t)temp;
        }
        else if(idf == "DEPTH")
        {
          temp = convert(tokens[1]);
          if(temp < 1 || temp > 4)
          {
            spdlog::error("Invalid depth {}", temp);
            return false;
          }
          ph->depth = (uint32_t)temp;
        }
        else if(idf == "MAXVAL")
        {
          temp = convert(tokens[1]);
          if(temp < 1 || temp > USHRT_MAX)
          {
            spdlog::error("Invalid maximum value {}", temp);
            return false;
          }
          ph->maxval = (uint32_t)temp;
        }
        else if(idf == "TUPLTYPE")
        {
          std::string type = tokens[1];
          if(type == "BLACKANDWHITE")
            ph->colour_space = PNM_BW;
          else if(type == "GRAYSCALE")
            ph->colour_space = PNM_GRAY;
          else if(type == "GRAYSCALE_ALPHA")
            ph->colour_space = PNM_GRAYA;
          else if(type == "RGB")
            ph->colour_space = PNM_RGB;
          else if(type == "RGB_ALPHA")
            ph->colour_space = PNM_RGBA;
          else
            spdlog::error(" read_pnm_header:unknown P7 TUPLTYPE {}", type);
        }
      }
      else
      {
        continue;
      }
    } /* while(fgets( ) */
    if(!end)
    {
      spdlog::error("read_pnm_header:P7 without ENDHDR");
      return false;
    }
    if(ph->depth == 0)
    {
      spdlog::error("Depth is missing");
      return false;
    }
    if(ph->maxval == 0)
    {
      spdlog::error("Maximum value is missing");
      return false;
    }
    PNM_COLOUR_SPACE depth_colour_space = PNM_UNKNOWN;
    switch(ph->depth)
    {
      case 1:
        depth_colour_space = (ph->maxval == 1) ? PNM_BW : PNM_GRAY;
        break;
      case 2:
        depth_colour_space = PNM_GRAYA;
        break;
      case 3:
        depth_colour_space = PNM_RGB;
        break;
      case 4:
        depth_colour_space = PNM_RGBA;
        break;
    }
    if(ph->colour_space != PNM_UNKNOWN && ph->colour_space != depth_colour_space)
    {
      spdlog::warn("Tuple colour space {} does not match depth {}. "
                   "Will use depth colour space",
                   (size_t)ph->colour_space, (size_t)depth_colour_space);
    }
    ph->colour_space = depth_colour_space;
  }
  else
  {
    while(fgets(line, lineSearch, fileIO_->getFileHandle()))
    {
      int32_t allow_null = 0;
      if(*line == '#' || *line == '\n' || *line == '\r')
        continue;

      char* s = line;
      /* Here format is in range [1,6] */
      if(ph->width == 0)
      {
        s = skip_int(s, &ph->width);
        if((!s) || (*s == 0) || (ph->width < 1))
        {
          spdlog::error("Invalid width {}", (s && *s != 0) ? ph->width : 0U);
          return false;
        }
        allow_null = 1;
      }
      if(ph->height == 0)
      {
        s = skip_int(s, &ph->height);
        if((s == nullptr) && allow_null)
          continue;
        if(!s || (*s == 0) || (ph->height < 1))
        {
          spdlog::error("Invalid height {}", (s && *s != 0) ? ph->height : 0U);
          return false;
        }
        if(format == 1 || format == 4)
        {
          if(!header_rewind(s, line, fileIO_->getFileHandle()))
            return false;
          break;
        }
        allow_null = 1;
      }
      /* here, format is in P2, P3, P5, P6 */
      s = skip_int(s, &ph->maxval);
      if(!s && allow_null)
        continue;
      if(!s || (*s == 0))
        return false;

      if(!header_rewind(s, line, fileIO_->getFileHandle()))
        return false;

      break;
    } /* while(fgets( ) */

    if(format == 2 || format == 3 || format > 4)
    {
      if(ph->maxval < 1 || ph->maxval > USHRT_MAX)
      {
        spdlog::error("Invalid max value {}", ph->maxval);
        return false;
      }
    }
    if(ph->width < 1 || ph->height < 1)
    {
      spdlog::error("Invalid width or height");
      return false;
    }
    // bitmap (ascii or binary)
    if(format == 1 || format == 4)
      ph->maxval = 1;

    // sanity check
    uint64_t area = (uint64_t)ph->width * ph->height;
    uint64_t minBytes = (ph->maxval != 1) ? area : area / 8;
    if(minBytes)
    {
      int64_t currentPos = GRK_FTELL(fileIO_->getFileHandle());
      GRK_FSEEK(fileIO_->getFileHandle(), 0L, SEEK_END);
      uint64_t length = (uint64_t)GRK_FTELL(fileIO_->getFileHandle());
      if(length < minBytes)
      {
        spdlog::error("File is truncated");
        return false;
      }
      GRK_FSEEK(fileIO_->getFileHandle(), currentPos, SEEK_SET);
    }
  }
  return true;
}

static inline uint32_t uint_floorlog2(uint32_t a)
{
  uint32_t l;
  for(l = 0; a > 1; l++)
  {
    a >>= 1;
  }
  return l;
}

template<typename T>
template<typename WT>
bool PNMFormat<T>::readBytes(FILE* fp, grk_image* image, size_t area)
{
  if(!fp || !image)
    return false;

  assert(image->decompress_num_comps <= 4);

  uint64_t i = 0;
  uint64_t index = 0;
  uint16_t compno = 0;
  uint64_t totalSize = area * image->decompress_num_comps;
  const uint64_t chunkSize = 4096 * 4;
  WT chunk[chunkSize];
  uint32_t width = image->decompress_width;
  uint32_t stride_diff = image->comps[0].stride - width;
  uint32_t counter = 0;
  while(i < totalSize)
  {
    uint64_t toRead = (std::min)(chunkSize, (uint64_t)(totalSize - i));
    size_t bytesRead = fread(chunk, sizeof(WT), toRead, fp);
    if(bytesRead == 0)
      break;
    WT* chunkPtr = chunk;
    for(size_t ct = 0; ct < bytesRead; ++ct)
    {
      ((T*)image->comps[compno++].data)[index] =
          sizeof(WT) > 1 ? grk::endian<WT>(*chunkPtr++, true) : *chunkPtr++;
      if(compno == image->decompress_num_comps)
      {
        compno = 0;
        index++;
        counter++;
        if(counter == width)
        {
          index += stride_diff;
          counter = 0;
        }
      }
    }
    i += bytesRead;
  }
  if(i != totalSize)
  {
    spdlog::error("bytes read ({}) are less than expected number of bytes ({})", i, totalSize);
    return false;
  }

  return true;
}

template<typename T>
grk_image* PNMFormat<T>::decode(const grk_cparameters* parameters)
{
  uint8_t subsampling_dx = parameters->subsampling_dx;
  uint8_t subsampling_dy = parameters->subsampling_dy;
  uint16_t decompress_num_comps;
  uint16_t compno;
  uint32_t w, stride_diff, width, counter, h, format;
  uint8_t prec;
  GRK_COLOR_SPACE color_space;
  grk_image_comp cmptparm[4]; /* RGBA: max. 4 components */
  grk_image* image = nullptr;
  struct pnm_header header_info;
  uint64_t area = 0;
  bool success = false;

  if(fileIO_)
    delete fileIO_;
  fileIO_ = new FileStandardIO();
  if(!fileIO_->open(fileName_.c_str(), "rb"))
  {
    spdlog::error("pnmtoimage:Failed to open {} for reading.", fileName_.c_str());
    goto cleanup;
  }
  memset(&header_info, 0, sizeof(struct pnm_header));
  if(!decodeHeader(&header_info))
  {
    spdlog::error("Invalid PNM header");
    goto cleanup;
  }

  format = header_info.format;
  switch(format)
  {
    case 1: /* ascii bitmap */
    case 4: /* binary bitmap */
      decompress_num_comps = 1;
      break;
    case 2: /* ascii greymap */
    case 5: /* binary greymap */
      decompress_num_comps = 1;
      break;
    case 3: /* ascii pixmap */
    case 6: /* binary pixmap */
      decompress_num_comps = 3;
      break;
    case 7: /* arbitrary map */
      decompress_num_comps = (uint16_t)header_info.depth;
      break;
    default:
      goto cleanup;
  }
  if(decompress_num_comps < 3)
    color_space = GRK_CLRSPC_GRAY; /* GRAY, GRAYA */
  else
    color_space = GRK_CLRSPC_SRGB; /* RGB, RGBA */

  prec = (uint8_t)(uint_floorlog2(header_info.maxval) + 1);
  if(prec > GRK_MAX_SUPPORTED_IMAGE_PRECISION)
  {
    spdlog::error("Precision {} is greater than max supported precision (%d)", prec,
                  GRK_MAX_SUPPORTED_IMAGE_PRECISION);
    goto cleanup;
  }
  w = header_info.width;
  h = header_info.height;
  area = (uint64_t)w * h;
  subsampling_dx = parameters->subsampling_dx;
  subsampling_dy = parameters->subsampling_dy;
  memset(&cmptparm[0], 0, (size_t)decompress_num_comps * sizeof(grk_image_comp));

  for(uint32_t i = 0; i < decompress_num_comps; i++)
  {
    cmptparm[i].prec = prec;
    cmptparm[i].sgnd = false;
    cmptparm[i].dx = subsampling_dx;
    cmptparm[i].dy = subsampling_dy;
    cmptparm[i].w = w;
    cmptparm[i].h = h;
  }
  image = grk_image_new(decompress_num_comps, &cmptparm[0], color_space, true);
  if(!image)
  {
    spdlog::error("pnmtoimage: Failed to create image");
    goto cleanup;
  }

  /* set image offset and reference grid */
  image->x0 = parameters->image_offset_x0;
  image->y0 = parameters->image_offset_y0;
  image->x1 = (parameters->image_offset_x0 + (w - 1) * subsampling_dx + 1);
  image->y1 = (parameters->image_offset_y0 + (h - 1) * subsampling_dy + 1);

  width = image->decompress_width;
  stride_diff = image->comps[0].stride - width;
  counter = 0;

  if(format == 1)
  { /* ascii bitmap */
    const size_t chunkSize = 4096;
    uint8_t chunk[chunkSize];
    uint64_t i = 0;
    area = (uint64_t)image->comps[0].stride * h;
    auto dest = (T*)image->comps[0].data;
    while(i < area)
    {
      size_t bytesRead = fread(chunk, 1, chunkSize, fileIO_->getFileHandle());
      if(bytesRead == 0)
        break;
      auto chunkPtr = (const uint8_t*)chunk;
      for(size_t ct = 0; ct < bytesRead; ++ct)
      {
        uint8_t c = *chunkPtr++;
        if(c != '\n' && c != ' ')
        {
          dest[i++] = (c & 1) ^ 1;
          counter++;
          if(counter == w)
          {
            counter = 0;
            i += stride_diff;
          }
        }
      }
    }
    if(i != area)
    {
      spdlog::error("pixels read ({}) less than image area ({})", i, area);
      goto cleanup;
    }
  }
  else if(format == 2 || format == 3)
  { /* ascii pixmap */
    area = (uint64_t)image->comps[0].stride * h;
    for(uint64_t i = 0; i < area; i++)
    {
      for(compno = 0; compno < decompress_num_comps; compno++)
      {
        uint32_t val = 0;
        auto dest = (T*)image->comps[compno].data;
        if(fscanf(fileIO_->getFileHandle(), "%u", &val) != 1)
        {
          spdlog::error("error reading ASCII PPM pixel data");
          goto cleanup;
        }
        dest[i] = (T)val;
      }
      counter++;
      if(counter == w)
      {
        counter = 0;
        i += stride_diff;
      }
    }
  }
  else if(format == 5 || format == 6 ||
          ((format == 7) &&
           (header_info.colour_space == PNM_GRAY || header_info.colour_space == PNM_GRAYA ||
            header_info.colour_space == PNM_RGB || header_info.colour_space == PNM_RGBA)))
  {
    bool rc = false;
    if(prec <= 8)
      rc = readBytes<uint8_t>(fileIO_->getFileHandle(), image, area);
    else
      rc = readBytes<uint16_t>(fileIO_->getFileHandle(), image, area);
    if(!rc)
      goto cleanup;
  }
  else if(format == 4 || (format == 7 && header_info.colour_space == PNM_BW))
  { /* binary bitmap */
    bool packed = false;
    uint64_t packed_area = (uint64_t)((w + 7) / 8) * h;
    if(format == 4)
    {
      packed = true;
    }
    else
    {
      /* let's see if bits are packed into bytes or not */
      int64_t currentPos = GRK_FTELL(fileIO_->getFileHandle());
      if(currentPos == -1)
        goto cleanup;
      if(GRK_FSEEK(fileIO_->getFileHandle(), 0L, SEEK_END))
        goto cleanup;
      int64_t endPos = GRK_FTELL(fileIO_->getFileHandle());
      if(endPos == -1)
        goto cleanup;
      if(GRK_FSEEK(fileIO_->getFileHandle(), currentPos, SEEK_SET))
        goto cleanup;
      uint64_t pixels = (uint64_t)(endPos - currentPos);
      if(pixels == packed_area)
        packed = true;
    }
    if(packed)
      area = packed_area;

    uint64_t index = 0;
    const size_t chunkSize = 4096;
    uint8_t chunk[chunkSize];
    uint64_t i = 0;
    auto dest = (T*)image->comps[0].data;
    while(i < area)
    {
      auto toRead = std::min((uint64_t)chunkSize, (uint64_t)(area - i));
      size_t bytesRead = fread(chunk, 1, toRead, fileIO_->getFileHandle());
      if(bytesRead == 0)
        break;
      auto chunkPtr = (const uint8_t*)chunk;
      for(size_t ct = 0; ct < bytesRead; ++ct)
      {
        uint8_t c = *chunkPtr++;
        if(packed)
        {
          for(int32_t j = 7; j >= 0; --j)
          {
            dest[index++] = ((c >> j) & 1) ^ 1;
            counter++;
            if(counter == w)
            {
              counter = 0;
              index += stride_diff;
              break;
            }
          }
        }
        else
        {
          dest[index++] = c & 1;
          counter++;
          if(counter == w)
          {
            counter = 0;
            index += stride_diff;
          }
        }
        i++;
      }
    }
    if(i != area)
    {
      spdlog::error("pixels read ({}) differs from image area ({})", i, area);
      goto cleanup;
    }
  }
  success = true;
cleanup:
  if(!grk::safe_fclose(fileIO_->getFileHandle()) || !success)
  {
    grk_object_unref(&image->obj);
    image = nullptr;
  }
  return image;
} /* pnmtoimage() */

template<typename T>
grk_image* PNMFormat<T>::decode(const std::string& filename, grk_cparameters* parameters)
{
  fileName_ = filename;
  return decode(parameters);
}
