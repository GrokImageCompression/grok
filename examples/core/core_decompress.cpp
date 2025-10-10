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

#include <cstdio>
#include <string>
#include <cstring>
#include <cassert>
#include <iostream>
#include <cstdlib>
#include <regex>
#include <inttypes.h>

#include "grok.h"
#include "grk_examples_config.h"
#include <CLI/CLI.hpp>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#ifndef _WIN32
#pragma GCC diagnostic pop
#endif

#include "core.h"

uint8_t img_buf[] = {
    0xff, 0x4f, 0xff, 0x51, 0x00, 0x2c, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x0c,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x0c,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x07, 0x04, 0x01, 0x07, 0x01, 0x01,
    0xff, 0x52, 0x00, 0x0e, 0x07, 0x02, 0x00, 0x01, 0x00, 0x01, 0x04, 0x04, 0x00, 0x01, 0x00, 0x11,
    0xff, 0x53, 0x00, 0x0b, 0x01, 0x01, 0x01, 0x04, 0x04, 0x00, 0x01, 0x11, 0x22, 0xff, 0x5c, 0x00,
    0x07, 0x40, 0x40, 0x48, 0x48, 0x50, 0xff, 0x64, 0x00, 0x2d, 0x00, 0x01, 0x43, 0x72, 0x65, 0x61,
    0x74, 0x6f, 0x72, 0x3a, 0x20, 0x41, 0x56, 0x2d, 0x4a, 0x32, 0x4b, 0x20, 0x28, 0x63, 0x29, 0x20,
    0x32, 0x30, 0x30, 0x30, 0x2c, 0x32, 0x30, 0x30, 0x31, 0x20, 0x41, 0x6c, 0x67, 0x6f, 0x20, 0x56,
    0x69, 0x73, 0x69, 0x6f, 0x6e, 0xff, 0x90, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x01, 0xb2, 0x00,
    0x01, 0xff, 0x93, 0xff, 0x91, 0x00, 0x04, 0x00, 0x00, 0xcf, 0xb4, 0x14, 0xff, 0x92, 0x0d, 0xe6,
    0x72, 0x28, 0x08, 0xff, 0x91, 0x00, 0x04, 0x00, 0x01, 0xcf, 0xb4, 0x04, 0xff, 0x92, 0x07, 0xff,
    0x91, 0x00, 0x04, 0x00, 0x02, 0xdf, 0x80, 0x28, 0xff, 0x92, 0x07, 0x99, 0x26, 0x2e, 0xe7, 0xff,
    0x91, 0x00, 0x04, 0x00, 0x03, 0xcf, 0xb4, 0x04, 0xff, 0x92, 0x07, 0xff, 0x91, 0x00, 0x04, 0x00,
    0x04, 0xdf, 0x80, 0x28, 0xff, 0x92, 0x0e, 0x07, 0xe6, 0x46, 0xd9, 0xff, 0x91, 0x00, 0x04, 0x00,
    0x05, 0xcf, 0xb4, 0x04, 0xff, 0x92, 0x07, 0xff, 0x91, 0x00, 0x04, 0x00, 0x06, 0xcf, 0xb4, 0x14,
    0xff, 0x92, 0x0c, 0xf6, 0x74, 0xf6, 0xcb, 0xff, 0x91, 0x00, 0x04, 0x00, 0x07, 0xcf, 0xb4, 0x04,
    0xff, 0x92, 0x07, 0xff, 0x91, 0x00, 0x04, 0x00, 0x08, 0xdf, 0x80, 0x18, 0xff, 0x92, 0x0e, 0x07,
    0x78, 0xff, 0x91, 0x00, 0x04, 0x00, 0x09, 0xcf, 0xb4, 0x04, 0xff, 0x92, 0x07, 0xff, 0x91, 0x00,
    0x04, 0x00, 0x0a, 0xcf, 0xb4, 0x0c, 0xff, 0x92, 0x0c, 0xfa, 0x1b, 0xff, 0x91, 0x00, 0x04, 0x00,
    0x0b, 0xcf, 0xb4, 0x04, 0xff, 0x92, 0x07, 0xff, 0x91, 0x00, 0x04, 0x00, 0x0c, 0xcf, 0xc0, 0x04,
    0xff, 0x92, 0x04, 0xff, 0x91, 0x00, 0x04, 0x00, 0x0d, 0xc7, 0xda, 0x09, 0x0f, 0xa8, 0x12, 0x1f,
    0x68, 0x18, 0xff, 0x92, 0x02, 0x48, 0x0a, 0x04, 0x0b, 0x81, 0x06, 0x3b, 0x0b, 0x66, 0x81, 0xff,
    0x91, 0x00, 0x04, 0x00, 0x0e, 0x80, 0xff, 0x92, 0xff, 0x91, 0x00, 0x04, 0x00, 0x0f, 0xc7, 0xda,
    0x09, 0x1f, 0x68, 0x24, 0x3e, 0xd0, 0x40, 0xff, 0x92, 0x02, 0xe8, 0x7b, 0xe6, 0x07, 0xcd, 0xd0,
    0x8e, 0x0b, 0x72, 0x34, 0xd4, 0xff, 0x91, 0x00, 0x04, 0x00, 0x10, 0xcf, 0xc0, 0x04, 0xff, 0x92,
    0x04, 0xff, 0x91, 0x00, 0x04, 0x00, 0x11, 0x80, 0xff, 0x92, 0xff, 0x91, 0x00, 0x04, 0x00, 0x12,
    0xcf, 0xc0, 0x04, 0xff, 0x92, 0x04, 0xff, 0x91, 0x00, 0x04, 0x00, 0x13, 0xc7, 0xda, 0x07, 0x0f,
    0xa8, 0x0a, 0x1f, 0x68, 0x10, 0xff, 0x92, 0x06, 0x40, 0x23, 0x07, 0xb1, 0x08, 0x0c, 0xff, 0x91,
    0x00, 0x04, 0x00, 0x14, 0x80, 0xff, 0x92, 0xff, 0x91, 0x00, 0x04, 0x00, 0x15, 0xc7, 0xda, 0x0a,
    0x00, 0xff, 0x92, 0x01, 0x66, 0x0a, 0xa0, 0x2c, 0xff, 0x91, 0x00, 0x04, 0x00, 0x16, 0xcf, 0xc0,
    0x04, 0xff, 0x92, 0x04, 0xff, 0x91, 0x00, 0x04, 0x00, 0x17, 0x80, 0xff, 0x92, 0xff, 0x91, 0x00,
    0x04, 0x00, 0x18, 0xcf, 0xc0, 0x04, 0xff, 0x92, 0x04, 0xff, 0x91, 0x00, 0x04, 0x00, 0x19, 0xc7,
    0xda, 0x06, 0x00, 0xff, 0x92, 0x01, 0x6d, 0x0f, 0xff, 0x91, 0x00, 0x04, 0x00, 0x1a, 0x80, 0xff,
    0x92, 0xff, 0x91, 0x00, 0x04, 0x00, 0x1b, 0xc7, 0xda, 0x0a, 0x00, 0xff, 0x92, 0x01, 0x66, 0x1a,
    0xa1, 0x0d, 0xff, 0x91, 0x00, 0x04, 0x00, 0x1c, 0xcf, 0xc0, 0x04, 0xff, 0x92, 0x04, 0xff, 0x91,
    0x00, 0x04, 0x00, 0x1d, 0x80, 0xff, 0x92, 0xff, 0xd9};

const std::string fileType = ".bmp";

struct ReadStreamInfoExample
{
  explicit ReadStreamInfoExample(grk_stream_params* streamParams)
      : streamParams_(streamParams), data_(nullptr), dataLen_(0), offset_(0), fp_(nullptr)
  {}
  grk_stream_params* streamParams_;
  uint8_t* data_;
  size_t dataLen_;
  size_t offset_;
  FILE* fp_;
};

static size_t sReadBytes = 0;
static size_t sReadCount = 0;
static size_t stream_read_fn(uint8_t* buffer, size_t numBytes, void* user_data)
{
  auto sinfo = (ReadStreamInfoExample*)user_data;
  size_t readBytes = numBytes;
  if(sinfo->data_)
  {
    size_t bytesAvailable = sinfo->dataLen_ - sinfo->offset_;
    readBytes = std::min(numBytes, bytesAvailable);
  }
  if(readBytes)
  {
    if(sinfo->data_)
      memcpy(buffer, sinfo->data_ + sinfo->offset_, readBytes);
    else if(sinfo->fp_)
    {
      readBytes = fread(buffer, 1, readBytes, sinfo->fp_);
    }
    sReadBytes += readBytes;
    sReadCount++;
  }
  return readBytes;
}

static bool stream_seek_fn(uint64_t offset, void* user_data)
{
  auto sinfo = (ReadStreamInfoExample*)user_data;
  if(offset <= sinfo->dataLen_)
    sinfo->offset_ = offset;
  else
    sinfo->offset_ = sinfo->dataLen_;
  if(sinfo->fp_)
  {
    return fseek(sinfo->fp_, (long int)offset, SEEK_SET) == 0;
  }
  else
  {
    return true;
  }
}

class ChronoTimer
{
public:
  explicit ChronoTimer(const std::string& msg) : message(msg)
  {
    start();
  }
  void start(void)
  {
    startTime = std::chrono::high_resolution_clock::now();
  }
  void finish(void)
  {
    auto finish = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = finish - startTime;
    printf("%s : %f ms\n", message.c_str(), elapsed.count() * 1000);
    start();
  }

private:
  std::string message;
  std::chrono::high_resolution_clock::time_point startTime;
};
static bool runGMCompare(const std::string& newFile, const std::string& referenceFile)
{
  printf("Comparing %s to reference %s\n", newFile.c_str(), referenceFile.c_str());
  std::string command = "gm compare -metric PSNR \"" + newFile + "\" \"" + referenceFile + "\"";
  std::string result;
  char buffer[128];

  // Open a pipe to run the command and capture its output
  FILE* pipe = popen(command.c_str(), "r");
  if(!pipe)
  {
    std::cerr << "Error running gm compare command!" << std::endl;
    return false;
  }

  // Read the command output
  while(fgets(buffer, sizeof(buffer), pipe) != nullptr)
  {
    result += buffer;
  }

  // Close the pipe and get exit status
  int exitStatus = pclose(pipe);
  if(exitStatus != 0)
  {
    std::cerr << "gm compare failed with exit status: " << exitStatus << std::endl;
    std::cout << "Output: " << result << std::endl;
    return false;
  }

  // Print the full output first
  std::cout << "gm compare output: " << result << std::endl;

  // Debug: Print raw string with escaped characters
  std::cout << "Raw result (escaped): ";
  for(char c : result)
  {
    if(c == '\n')
      std::cout << "\\n";
    else if(c == '\r')
      std::cout << "\\r";
    else if(c == '\t')
      std::cout << "\\t";
    else
      std::cout << c;
  }
  std::cout << std::endl;

  // Parse all PSNR values (Red, Green, Blue, Total)
  std::regex psnrRegex("(Red|Green|Blue|Total):\\s+([A-Za-z0-9.]+)", std::regex::icase);
  std::string remaining = result;
  bool allInf = true;
  std::string nonInfChannel;
  std::string nonInfValue;

  // Loop over all matches in the string
  std::smatch match;
  int matchCount = 0;
  while(std::regex_search(remaining, match, psnrRegex))
  {
    matchCount++;
    std::string channel = match[1]; // e.g., "Red", "Total"
    std::string psnrValue = match[2]; // e.g., "8.02", "inf"

    std::cout << "Matched: " << channel << " with value " << psnrValue << std::endl;

    // Case-insensitive comparison for "inf"
    std::string psnrLower = psnrValue;
    std::transform(psnrLower.begin(), psnrLower.end(), psnrLower.begin(), ::tolower);

    if(psnrLower != "inf")
    {
      allInf = false;
      nonInfChannel = channel;
      nonInfValue = psnrValue;
      break; // Stop at first non-inf
    }

    // Move past the current match
    remaining = match.suffix();
  }

  // Check if we found any matches
  if(matchCount == 0) // No matches found
  {
    std::cerr << "Failed to parse any PSNR values from gm compare output: " << result << std::endl;
    return false;
  }

  // Check result and report failure if any non-inf
  if(!allInf)
  {
    std::cerr << "PSNR is not INF for " << nonInfChannel << ": " << nonInfValue << " for "
              << newFile << " vs " << referenceFile << std::endl;
    return false;
  }
  else
  {
    printf("PSNR is INF (perfect match) for %s vs %s\n", newFile.c_str(), referenceFile.c_str());
    return true;
  }
}

/**
 * @brief writes image to disk using STB library
 * Images are scaled to 8 bit as the library does not handle data
 * precision greater than 8
 */
template<typename T>
bool write(grk_image* image, std::string fileName)
{
  // see grok.h header for full details of image structure
  auto width = image->comps[0].w;
  auto height = image->comps[0].h;
  auto dstData = std::make_unique<uint8_t[]>(width * height * image->numcomps);
  auto destPtr = dstData.get();
  auto srcPtrs = std::make_unique<T*[]>(image->numcomps);

  for(uint8_t c = 0; c < image->numcomps; ++c)
  {
    auto comp = image->comps + c;
    srcPtrs[c] = (T*)comp->data;
    if(!srcPtrs[c])
    {
      fprintf(stderr, "Image has null data for component %d\n", c);
      return false;
    }
  }

  int32_t maximum = 0;
  for(uint32_t j = 0; j < height; ++j)
  {
    for(uint32_t i = 0; i < width; ++i)
    {
      for(uint16_t c = 0; c < image->numcomps; ++c)
      {
        uint64_t index = (j * width + i);
        maximum = std::max(maximum, srcPtrs[c][index]);
      }
    }
  }
  for(uint32_t j = 0; j < height; ++j)
  {
    for(uint32_t i = 0; i < width; ++i)
    {
      for(uint16_t c = 0; c < image->numcomps; ++c)
      {
        *destPtr = (uint8_t)(float(*srcPtrs[c]) * (255.0f / (1 << image->comps->prec)));
        srcPtrs[c]++;
        destPtr++;
      }
    }

    // Correct for input image stride
    for(uint16_t c = 0; c < image->numcomps; ++c)
    {
      auto comp = image->comps + c;
      auto stride = comp->stride;
      srcPtrs[c] += stride - comp->w;
    }
  }

  fileName += fileType;
  if(fileName.find(".png") != std::string::npos)
  {
    uint64_t stride_bytes = image->comps[0].stride * image->numcomps;
    stbi_write_png(fileName.c_str(), (int)width, (int)height, image->numcomps, dstData.get(),
                   (int)stride_bytes);
  }
  else if(fileName.find(".bmp") != std::string::npos)
  {
    stbi_write_bmp(fileName.c_str(), (int)width, (int)height, image->numcomps, dstData.get());
  }
  return true;
}

ChronoTimer timer("");
static std::pair<grk_object*, bool>
    decompress_init(grk_object* codec, grk_decompress_parameters* decompressParams,
                    grk_image** image, grk_stream_params* streamParams, grk_header_info* headerInfo)
{
  timer = ChronoTimer("Decompress time ");
  if(!decompressParams || !image || !streamParams || !headerInfo)
    return {codec, false};

  bool createCodec = !codec;

  if(createCodec)
  {
    codec = grk_decompress_init(streamParams, decompressParams);
    if(!codec)
    {
      fprintf(stderr, "Failed to set up decompressor\n");
      return {nullptr, false};
    }
  }
  else
  {
    auto rc = grk_decompress_update(decompressParams, codec);
    if(!rc)
    {
      fprintf(stderr, "Failed to update decompressor\n");
      return {codec, false};
    }
  }

  // read j2k header
  if(!grk_decompress_read_header(codec, headerInfo))
  {
    fprintf(stderr, "Failed to read the header\n");
    return {codec, false};
  }

  return {codec, true};
}

static void wait_tile_range(grk_object* codec, const grk_header_info* headerInfo, uint16_t start,
                            uint16_t end)
{
  for(uint16_t i = start; i < end; ++i)
  {
    uint16_t tx = i % headerInfo->t_grid_width;
    uint16_t ty = i / headerInfo->t_grid_width;
    grk_wait_swath swath = {};
    swath.x0 = tx * headerInfo->t_width;
    swath.y0 = ty * headerInfo->t_height;
    swath.x1 = std::min(swath.x0 + headerInfo->t_width, headerInfo->header_image.x1);
    swath.y1 = std::min(swath.y0 + headerInfo->t_height, headerInfo->header_image.y1);
    grk_decompress_wait(codec, &swath);
  }
}

static bool decompress(grk_object* codec, grk_header_info* headerInfo,
                       [[maybe_unused]] grk_decompress_parameters* decompressParams,
                       bool decompressTile, uint16_t tileIndex, grk_image** image)
{
  if(decompressTile)
  {
    // decompress a particular tile
    if(!grk_decompress_tile(codec, tileIndex))
    {
      fprintf(stderr, "Decompression failed\n");
      return false;
    }
  }
  else
  {
    // decompress all tiles
    if(!grk_decompress(codec, nullptr))
    {
      fprintf(stderr, "Decompression failed\n");
      return false;
    }
  }

  if(image && *image && decompressParams->asynchronous)
  {
    if(decompressParams->dw_x1 == 0 && !decompressTile)
    {
      grk_wait_swath swath = {};
      swath.x0 = (*image)->x0;
      swath.y0 = (*image)->y0;
      swath.x1 = (*image)->x1;
      swath.y1 = (*image)->y1;
      grk_decompress_wait(codec, &swath);
    }
  }

  // retrieve image that will store uncompressed image data
  if(!decompressTile)
  {
    wait_tile_range(codec, headerInfo, 0, headerInfo->t_grid_width * headerInfo->t_grid_height);
  }

  *image = decompressTile ? grk_decompress_get_tile_image(codec, tileIndex, true)
                          : grk_decompress_get_image(codec);
  if(!*image)
  {
    fprintf(stderr, "Failed to retrieve image \n");
    return false;
  }

  timer.finish();

  if(sReadBytes)
  {
    printf("IO ops: %" PRIu64 ", total bytes read (MB): %lf\n", (uint64_t)sReadCount,
           (double)sReadBytes / (1024 * 1024));
    sReadBytes = 0;
    sReadCount = 0;
  }
  return true;
}

static std::pair<grk_object*, bool> decompress(const std::string& filename, bool decompressTile,
                                               uint16_t tileIndex, grk_object* codec,
                                               grk_decompress_parameters* decompressParams,
                                               grk_image** image, grk_stream_params* streamParams,
                                               grk_header_info* headerInfo)
{
  auto [c, rc] = decompress_init(codec, decompressParams, image, streamParams, headerInfo);
  if(!rc)
    return {c, false};
  if(c)
    codec = c;

  rc = decompress(codec, headerInfo, decompressParams, decompressTile, tileIndex, image);
  if(!rc)
    return {c, false};
  if(image && !filename.empty())
  {
    rc = write<int32_t>(*image, filename);
  }
  return {c, rc};
}

static const char* prog_order_to_string(GRK_PROG_ORDER order)
{
  switch(order)
  {
    case GRK_LRCP:
      return "LRCP (Layer-Resolution-Component-Precinct)";
    case GRK_RLCP:
      return "RLCP (Resolution-Layer-Component-Precinct)";
    case GRK_RPCL:
      return "RPCL (Resolution-Precinct-Component-Layer)";
    case GRK_PCRL:
      return "PCRL (Precinct-Component-Resolution-Layer)";
    case GRK_CPRL:
      return "CPRL (Component-Precinct-Resolution-Layer)";
    case GRK_PROG_UNKNOWN:
    default:
      return "UNKNOWN";
  }
}

bool isNetwork(std::string& f)
{
  std::string_view file{f};
  return file.starts_with("http://") || file.starts_with("https://") || file.starts_with("/vsis3/");
}

int main([[maybe_unused]] int argc, [[maybe_unused]] const char** argv)
{
  int rc = EXIT_FAILURE;

  uint16_t numTiles = 0;
  std::string inputFilePath;

  // CLI11 app instance
  CLI::App app{"Core Decompressor"};

  // Add input file option and get the option pointer
  auto fileOpt = app.add_option("-i,--input", inputFilePath, "Input file path");

  // Add tile index option and get the option pointer
  uint16_t tileIndex = 0;
  auto tileOpt = app.add_option("-t,--tile", tileIndex, "Tile index to process")
                     ->default_val(0); // Default value is 0

  uint16_t maxLayers = 0;
  auto layersOpt = app.add_option("-l,--max-layers", maxLayers, "Maximum layers to process")
                       ->default_val(0xFFFF); // Default value is 0

  uint8_t maxResolutions = 0;
  auto resOpt =
      app.add_option("-r,--max-resolutions", maxResolutions, "Maximum resolutions process")
          ->default_val(0xFF); // Default value is 0

  uint8_t numThreads = 0;
  app.add_option("-H,--num-threads", numThreads, "Number of threads")
      ->default_val(0); // Default value is 0

  bool doFullReference = true;
  app.add_flag("-F,--full-reference", doFullReference, "Decompress full reference image");

  bool doLayerAndRes = false;
  app.add_flag("-L,--layers-and-res", doLayerAndRes,
               "Differential decompress of both layers and resolutions");

  bool doStore = false;
  app.add_flag("-s,--store", doStore, "Store output to disk");

  bool doDifferential = false;
  app.add_flag("-f,--differential", doDifferential, "Perform differential decompres");

  std::vector<double> dwindow;
  auto dWindowOpt = app.add_option(
      "-d,--window",
      [&dwindow](const std::vector<std::string>& val) {
        std::string item;
        for(const auto& v : val)
        {
          std::stringstream ss(v);
          while(std::getline(ss, item, ','))
          {
            if(!item.empty())
              dwindow.push_back(std::stof(item)); // Append values
          }
        }
        return true;
      },
      "Decompress window (x0,y0,x1,y1)");
  try
  {
    // Parse command-line arguments
    app.parse(argc, argv);
  }
  catch(const CLI::ParseError& e)
  {
    return app.exit(e); // Handle parsing errors
  }

  // initialize decompress parameters
  grk_decompress_parameters decompressParams = {};
  decompressParams.core.skip_allocate_composite = !doStore;
  // differential: disable as it breaks differential decompression
  if(!doDifferential)
  {
    decompressParams.asynchronous = true;
    decompressParams.simulate_synchronous = true;
  }

  bool fromBuffer = fileOpt->count() == 0;
  if(dWindowOpt->count() > 0)
  {
    decompressParams.dw_x0 = dwindow[0];
    decompressParams.dw_y0 = dwindow[1];
    decompressParams.dw_x1 = dwindow[2];
    decompressParams.dw_y1 = dwindow[3];
    printf("Window set to %f,%f,%f,%f\n", dwindow[0], dwindow[1], dwindow[2], dwindow[3]);
  }

  grk_image* image = nullptr;
  const char* inputFileStr = nullptr;
  bool useCallbacks = false;
  if(useCallbacks && !fromBuffer && isNetwork(inputFilePath))
  {
    printf("[WARNING] Disabling callbacks for network file %s\n", inputFilePath.c_str());
    useCallbacks = false;
  }

  bool differentialByLayer = true;
  if(layersOpt->count() > 0 && maxLayers == 0)
    differentialByLayer = false;
  bool differentialByResolution = true;
  if(resOpt->count() > 0 && maxResolutions == 0)
    differentialByResolution = false;

  // if we store, then no need to cache images, otherwise we choose minimum caching - image
  uint32_t fullCacheStrategy = doStore ? GRK_TILE_CACHE_NONE : GRK_TILE_CACHE_IMAGE;

  // if true, decompress a particular tile, otherwise decompress
  // all tiles
  bool decompressTile = tileOpt->count() > 0;

  grk_object* codec_diff = nullptr;
  grk_object* codec_full = nullptr;
  grk_header_info headerInfo = {};
  std::pair<grk_object*, bool> drc;

  // initialize library
  grk_initialize(nullptr, numThreads);

  // create j2k file stream
  inputFileStr = inputFilePath.c_str();
  if(fromBuffer)
    printf("Decompressing buffer\n");
  else
    printf("Decompressing file %s\n", inputFilePath.c_str());

  grk_stream_params streamParams = {};
  ReadStreamInfoExample sinfo(&streamParams);
  if(useCallbacks)
  {
    streamParams.seek_fn = stream_seek_fn;
    streamParams.read_fn = stream_read_fn;
    streamParams.user_data = &sinfo;
    if(fromBuffer)
    {
      streamParams.stream_len = sizeof(img_buf);
      sinfo.data_ = img_buf;
      sinfo.dataLen_ = sizeof(img_buf);
    }
    else
    {
      sinfo.fp_ = fopen(inputFilePath.c_str(), "rb");
      if(!sinfo.fp_)
      {
        fprintf(stderr, "Failed to open file %s for reading\n", inputFilePath.c_str());
        goto beach;
      }
      // Move the file pointer to the end and get the file size
      fseek(sinfo.fp_, 0, SEEK_END);
      auto len = ftell(sinfo.fp_);
      if(len == -1)
        goto beach;
      streamParams.stream_len = (size_t)len;
      rewind(sinfo.fp_);
    }
  }
  else if(fromBuffer)
  {
    streamParams.buf = img_buf;
    streamParams.buf_len = sizeof(img_buf);
  }
  else
  {
    safe_strcpy(streamParams.file, inputFileStr);
  }

  if(doFullReference)
  {
    // 1. decompress full image, to be used as reference image
    printf("\nFull decompress : all layers\n");
    decompressParams.core.tile_cache_strategy = fullCacheStrategy;
    decompressParams.core.layers_to_decompress = maxLayers;
    decompressParams.core.skip_allocate_composite = !doStore;
    drc = decompress(doStore ? "reference" : "", decompressTile, tileIndex, codec_full,
                     &decompressParams, &image, &streamParams, &headerInfo);
    if(!codec_full)
      codec_full = drc.first;
    if(!drc.second)
      goto beach;
    // print out header info
    numTiles = (uint16_t)(headerInfo.t_grid_width * headerInfo.t_grid_height);
    if(image)
    {
      printf("Width: %d\n", image->comps[0].w);
      printf("Height: %d\n", image->comps[0].h);
      printf("Number of components: %d\n", image->numcomps);
      for(uint16_t compno = 0; compno < image->numcomps; ++compno)
        printf("Precision of component %d : %d\n", compno, image->comps[compno].prec);
      printf("Progression: %s\n", prog_order_to_string(headerInfo.prog_order));
    }
    printf("Number of tiles: %d\n", numTiles);
    if(numTiles > 1)
      printf("Nominal tile dimensions: (%d,%d)\n", headerInfo.t_width, headerInfo.t_height);
    if(decompressTile)
      printf("Tile: %d\n", tileIndex);
    printf("Number of layers: %d\n", headerInfo.num_layers);
    printf("Number of resolutions: %d\n", headerInfo.numresolutions);

    maxLayers = std::min(maxLayers, headerInfo.num_layers);
    maxResolutions = std::min(maxResolutions, headerInfo.numresolutions);
    grk_object_unref(codec_full);
    codec_full = nullptr;
  }

  if(!doDifferential)
  {
    rc = EXIT_SUCCESS;
    goto beach;
  }

  // 2. differential decompression by layer
  // we must set the GRK_TILE_CACHE_ALL cache strategy to enable differential decompression
  decompressParams.core.tile_cache_strategy = GRK_TILE_CACHE_ALL;
  if(differentialByLayer && maxLayers > 1)
  {
    // decompress first layer
    uint16_t initialLayer = 1;
    printf("\nFull decompression : layer %d\n", initialLayer);
    if(sinfo.fp_)
      rewind(sinfo.fp_);

    decompressParams.core.tile_cache_strategy = GRK_TILE_CACHE_ALL;
    decompressParams.core.layers_to_decompress = initialLayer;
    drc = decompress(doStore ? "progressive_layer_1" : "", decompressTile, tileIndex, codec_diff,
                     &decompressParams, &image, &streamParams, &headerInfo);
    if(!codec_diff)
      codec_diff = drc.first;
    if(!drc.second)
      goto beach;

    uint16_t i = initialLayer + 5;
    while(i <= maxLayers)
    {
      printf("\nProgressive decompression : layer %u\n", i);
      if(sinfo.fp_)
        rewind(sinfo.fp_);
      decompressParams.core.tile_cache_strategy = GRK_TILE_CACHE_ALL;
      decompressParams.core.layers_to_decompress = i;
      std::string progressiveFile = "progressive_layer_" + std::to_string(i);
      drc = decompress(doStore ? progressiveFile : "", decompressTile, tileIndex, codec_diff,
                       &decompressParams, &image, &streamParams, &headerInfo);
      if(!codec_diff)
        codec_diff = drc.first;
      if(!drc.second)
        goto beach;
      maxLayers = std::min(maxLayers, headerInfo.num_layers);
      maxResolutions = std::min(maxResolutions, headerInfo.numresolutions);

      printf("Full decompression up to and including layer %u\n", i);
      decompressParams.core.tile_cache_strategy = fullCacheStrategy;
      std::string referenceFile = "reference_layer_" + std::to_string(i);
      drc = decompress(doStore ? referenceFile : "", decompressTile, tileIndex, codec_full,
                       &decompressParams, &image, &streamParams, &headerInfo);
      if(!codec_full)
        codec_full = drc.first;
      if(!drc.second)
        goto beach;
      grk_object_unref(codec_full);
      codec_full = nullptr;
      if(doStore)
      {
        if(!runGMCompare(progressiveFile + fileType, referenceFile + fileType))
          goto beach;
      }
      i += 4;
    }
  }
  grk_object_unref(codec_diff);
  codec_diff = nullptr;

  // 2. differential decompression by resolution
  if(differentialByResolution && maxResolutions > 1)
  {
    // decompress first resolution
    printf("\nFull decompression : first resolution\n");
    if(sinfo.fp_)
      rewind(sinfo.fp_);
    decompressParams.core.tile_cache_strategy = GRK_TILE_CACHE_ALL;
    decompressParams.core.reduce = headerInfo.numresolutions - 1;
    drc = decompress(doStore ? "progressive_resolution_1" : "", decompressTile, tileIndex,
                     codec_diff, &decompressParams, &image, &streamParams, &headerInfo);
    if(!codec_diff)
      codec_diff = drc.first;
    if(!drc.second)
      goto beach;
    printf("Width: %d\n", image->comps[0].w);
    printf("Height: %d\n", image->comps[0].h);

    for(uint16_t i = 1; i < maxResolutions; ++i)
    {
      printf("\nProgressive decompression : resolution %u\n", i + 1);
      if(sinfo.fp_)
        rewind(sinfo.fp_);
      decompressParams.core.tile_cache_strategy = GRK_TILE_CACHE_ALL;
      decompressParams.core.reduce = (uint8_t)(headerInfo.numresolutions - 1 - i);
      std::string progressiveFile = "progressive_resolution_" + std::to_string(i + 1);
      drc = decompress(doStore ? progressiveFile : "", decompressTile, tileIndex, codec_diff,
                       &decompressParams, &image, &streamParams, &headerInfo);
      if(!codec_diff)
        codec_diff = drc.first;
      if(!drc.second)
        goto beach;
      maxLayers = std::min(maxLayers, headerInfo.num_layers);
      maxResolutions = std::min(maxResolutions, headerInfo.numresolutions);
      printf("Width: %d\n", image->comps[0].w);
      printf("Height: %d\n", image->comps[0].h);
      printf("Full decompression up to and including resolution %u\n", i + 1);
      std::string referenceFile = "reference_resolution_" + std::to_string(i + 1);
      decompressParams.core.tile_cache_strategy = fullCacheStrategy;
      drc = decompress(doStore ? referenceFile : "", decompressTile, tileIndex, codec_full,
                       &decompressParams, &image, &streamParams, &headerInfo);
      if(!codec_full)
        codec_full = drc.first;
      if(!drc.second)
        goto beach;
      grk_object_unref(codec_full);
      codec_full = nullptr;
      if(doStore)
      {
        if(!runGMCompare(progressiveFile + fileType, referenceFile + fileType))
          goto beach;
      }
    }
  }

  // 3. differential decompression by resolution and layer
  if(doLayerAndRes && headerInfo.numresolutions > 1)
  {
    // decompress first resolution and layer
    printf("\nFull decompression : first resolution and layer\n");
    if(sinfo.fp_)
      rewind(sinfo.fp_);
    decompressParams.core.tile_cache_strategy = GRK_TILE_CACHE_ALL;
    decompressParams.core.reduce = headerInfo.numresolutions - 1;
    decompressParams.core.layers_to_decompress = 1;
    drc = decompress(doStore ? "progressive_resolution_layer_1" : "", decompressTile, tileIndex,
                     codec_diff, &decompressParams, &image, &streamParams, &headerInfo);
    if(!codec_diff)
      codec_diff = drc.first;
    if(!drc.second)
      goto beach;

    printf("Width: %d\n", image->comps[0].w);
    printf("Height: %d\n", image->comps[0].h);

    for(uint16_t i = 1; i < headerInfo.numresolutions; ++i)
    {
      printf("\nProgressive decompression : resolution and layer %u\n", i + 1);
      if(sinfo.fp_)
        rewind(sinfo.fp_);
      decompressParams.core.tile_cache_strategy = GRK_TILE_CACHE_ALL;
      decompressParams.core.reduce = (uint8_t)(headerInfo.numresolutions - 1 - i);
      decompressParams.core.layers_to_decompress = i + 1;
      std::string progressiveFile = "progressive_resolution_layer_" + std::to_string(i + 1);
      drc = decompress(doStore ? progressiveFile : "", decompressTile, tileIndex, codec_diff,
                       &decompressParams, &image, &streamParams, &headerInfo);
      if(!codec_diff)
        codec_diff = drc.first;
      if(!drc.second)
        goto beach;
      maxLayers = std::min(maxLayers, headerInfo.num_layers);
      maxResolutions = std::min(maxResolutions, headerInfo.numresolutions);
      printf("Width: %d\n", image->comps[0].w);
      printf("Height: %d\n", image->comps[0].h);
      printf("Full decompression up to and including resolution %u\n", i + 1);
      std::string referenceFile = "reference_resolution_layer_" + std::to_string(i + 1);
      decompressParams.core.tile_cache_strategy = fullCacheStrategy;
      drc = decompress(doStore ? referenceFile : "", decompressTile, tileIndex, codec_full,
                       &decompressParams, &image, &streamParams, &headerInfo);
      if(!codec_full)
        codec_full = drc.first;
      if(!drc.second)
        goto beach;
      grk_object_unref(codec_full);
      codec_full = nullptr;
      if(doStore)
      {
        if(!runGMCompare(progressiveFile + fileType, referenceFile + fileType))
          goto beach;
      }
    }
  }

  rc = EXIT_SUCCESS;
beach:
  // cleanup
  grk_object_unref(codec_diff);
  grk_object_unref(codec_full);
  grk_deinitialize();

  return rc;
}