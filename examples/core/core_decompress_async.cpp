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

/*************************************************************************

Example demonstrating asynchronous decompression with simulated synchronous
waiting, using swath-based tile retrieval.

This pattern is used by the GDAL JP2Grok driver:
  1. Start async decompression with grk_decompress() (once)
  2. Wait for specific swath regions with grk_decompress_wait()
  3. Retrieve per-tile data with grk_decompress_get_tile_image()
  4. For subsequent swaths, only call grk_decompress_wait() (no restart)

**************************************************************************/

#include <cstdio>
#include <string>
#include <cstring>
#include <cassert>
#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <sstream>
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

uint8_t img_buf_async[] = {
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

const std::string fileType_async = ".bmp";

struct ReadStreamInfoAsync
{
  explicit ReadStreamInfoAsync(grk_stream_params* streamParams)
      : streamParams_(streamParams), data_(nullptr), dataLen_(0), offset_(0), fp_(nullptr)
  {}
  grk_stream_params* streamParams_;
  uint8_t* data_;
  size_t dataLen_;
  size_t offset_;
  FILE* fp_;
};

static size_t sReadBytesAsync = 0;
static size_t sReadCountAsync = 0;
static size_t stream_read_fn_async(uint8_t* buffer, size_t numBytes, void* user_data)
{
  auto sinfo = (ReadStreamInfoAsync*)user_data;
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
    sReadBytesAsync += readBytes;
    sReadCountAsync++;
  }
  return readBytes;
}

static bool stream_seek_fn_async(uint64_t offset, void* user_data)
{
  auto sinfo = (ReadStreamInfoAsync*)user_data;
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

class ChronoTimerAsync
{
public:
  explicit ChronoTimerAsync(const std::string& msg) : message(msg)
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

static bool decompress_init(grk_object** codec, grk_decompress_parameters* decompressParams,
                            grk_stream_params* streamParams, grk_header_info* headerInfo)
{
  *codec = grk_decompress_init(streamParams, decompressParams);
  if(!*codec)
  {
    fprintf(stderr, "Failed to set up decompressor\n");
    return false;
  }

  if(!grk_decompress_read_header(*codec, headerInfo))
  {
    fprintf(stderr, "Failed to read the header\n");
    return false;
  }

  return true;
}

static void print_tile_info(grk_image* img, uint16_t tileIndex)
{
  if(!img)
    return;
  printf("  Tile %u: %ux%u, %u components", tileIndex, img->comps[0].w, img->comps[0].h,
         img->numcomps);
  uint64_t totalPixels = 0;
  for(uint16_t c = 0; c < img->numcomps; ++c)
    totalPixels += (uint64_t)img->comps[c].w * img->comps[c].h;
  printf(", %" PRIu64 " total pixels\n", totalPixels);
}

static const char* prog_order_to_string_async(GRK_PROG_ORDER order)
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

template<typename T>
bool write_async(grk_image* image, std::string fileName)
{
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

    for(uint16_t c = 0; c < image->numcomps; ++c)
    {
      auto comp = image->comps + c;
      auto stride = comp->stride;
      srcPtrs[c] += stride - comp->w;
    }
  }

  fileName += fileType_async;
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
  printf("Wrote %s\n", fileName.c_str());
  return true;
}

bool isNetworkAsync(std::string& f)
{
  std::string_view file{f};
  return file.starts_with("http://") || file.starts_with("https://") || file.starts_with("/vsis3/");
}

int main([[maybe_unused]] int argc, [[maybe_unused]] const char** argv)
{
  int rc = EXIT_FAILURE;
  std::string inputFilePath;

  CLI::App app{"Async Decompressor - swath-based tile retrieval"};

  auto fileOpt = app.add_option("-i,--input", inputFilePath, "Input file path");

  uint16_t tileIndex = 0;
  auto tileOpt = app.add_option("-t,--tile", tileIndex, "Tile index to process")->default_val(0);

  uint16_t maxLayers = 0;
  app.add_option("-l,--max-layers", maxLayers, "Maximum layers to process")->default_val(0xFFFF);

  uint8_t maxResolutions = 0;
  app.add_option("-r,--max-resolutions", maxResolutions, "Maximum resolutions to process")
      ->default_val(0xFF);

  uint8_t numThreads = 0;
  app.add_option("-H,--num-threads", numThreads, "Number of threads")->default_val(0);

  bool doStore = false;
  app.add_flag("-s,--store", doStore, "Store output to disk");

  bool useSwathBuf = false;
  app.add_flag("-B,--swath-buf", useSwathBuf,
               "Schedule tile copies into a swath buffer via Taskflow + Highway SIMD");

  std::vector<double> dwindow;
  app.add_option(
      "-d,--window",
      [&dwindow](const std::vector<std::string>& val) {
        std::string item;
        for(const auto& v : val)
        {
          std::stringstream ss(v);
          while(std::getline(ss, item, ','))
          {
            if(!item.empty())
              dwindow.push_back(std::stof(item));
          }
        }
        return true;
      },
      "Decompress window (x0,y0,x1,y1)");

  uint32_t swathHeight = 0;
  app.add_option("-S,--swath-height", swathHeight, "Swath height in pixels (0 = tile height)")
      ->default_val(0);

  try
  {
    app.parse(argc, argv);
  }
  catch(const CLI::ParseError& e)
  {
    return app.exit(e);
  }

  grk_initialize(nullptr, numThreads, nullptr);

  ChronoTimerAsync timer("Async decompress time ");

  grk_decompress_parameters decompressParams = {};
  decompressParams.asynchronous = true;
  decompressParams.simulate_synchronous = true;
  decompressParams.core.tile_cache_strategy = GRK_TILE_CACHE_IMAGE;
  decompressParams.core.layers_to_decompress = maxLayers;

  bool fromBuffer = fileOpt->count() == 0;
  bool decompressTile = tileOpt->count() > 0;
  bool useCallbacks = false;

  if(dwindow.size() == 4)
  {
    decompressParams.dw_x0 = dwindow[0];
    decompressParams.dw_y0 = dwindow[1];
    decompressParams.dw_x1 = dwindow[2];
    decompressParams.dw_y1 = dwindow[3];
    printf("Window: %.0f,%.0f,%.0f,%.0f\n", dwindow[0], dwindow[1], dwindow[2], dwindow[3]);
  }

  if(useCallbacks && !fromBuffer && isNetworkAsync(inputFilePath))
  {
    printf("[WARNING] Disabling callbacks for network file %s\n", inputFilePath.c_str());
    useCallbacks = false;
  }

  grk_stream_params streamParams = {};
  ReadStreamInfoAsync sinfo(&streamParams);

  if(useCallbacks)
  {
    streamParams.seek_fn = stream_seek_fn_async;
    streamParams.read_fn = stream_read_fn_async;
    streamParams.user_data = &sinfo;
    if(fromBuffer)
    {
      streamParams.stream_len = sizeof(img_buf_async);
      sinfo.data_ = img_buf_async;
      sinfo.dataLen_ = sizeof(img_buf_async);
    }
    else
    {
      sinfo.fp_ = fopen(inputFilePath.c_str(), "rb");
      if(!sinfo.fp_)
      {
        fprintf(stderr, "Failed to open file %s for reading\n", inputFilePath.c_str());
        goto beach;
      }
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
    streamParams.buf = img_buf_async;
    streamParams.buf_len = sizeof(img_buf_async);
  }
  else
  {
    safe_strcpy(streamParams.file, inputFilePath.c_str());
  }

  if(fromBuffer)
    printf("Decompressing buffer\n");
  else
    printf("Decompressing file %s\n", inputFilePath.c_str());

  {
    grk_header_info headerInfo = {};
    grk_object* codec = nullptr;

    if(!decompress_init(&codec, &decompressParams, &streamParams, &headerInfo))
      goto beach;

    uint16_t numTiles = headerInfo.t_grid_width * headerInfo.t_grid_height;
    bool singleTile = (numTiles == 1);

    printf("Image: %ux%u\n", headerInfo.header_image.x1, headerInfo.header_image.y1);
    printf("Tiles: %u (%ux%u grid, nominal %ux%u)\n", numTiles, headerInfo.t_grid_width,
           headerInfo.t_grid_height, headerInfo.t_width, headerInfo.t_height);
    printf("Number of layers: %u\n", headerInfo.num_layers);
    printf("Number of resolutions: %u\n", headerInfo.numresolutions);
    printf("Progression: %s\n", prog_order_to_string_async(headerInfo.prog_order));

    // For single-tile images, we need the composite buffer since
    // grk_decompress_get_tile_image returns NULL for single-tile.
    if(singleTile)
      decompressParams.core.skip_allocate_composite = !doStore;
    else
      decompressParams.core.skip_allocate_composite = true;

    // Apply reduce: clamp to available resolutions
    uint8_t reduce = 0;
    if(maxResolutions < headerInfo.numresolutions)
      reduce = headerInfo.numresolutions - maxResolutions;
    decompressParams.core.reduce = reduce;

    // Update parameters on the codec
    if(!grk_decompress_update(&decompressParams, codec))
    {
      fprintf(stderr, "grk_decompress_update() failed\n");
      grk_object_unref(codec);
      goto beach;
    }

    // Handle single-tile decompress by tile index
    if(decompressTile)
    {
      printf("\nDecompressing tile %u async...\n", tileIndex);
      if(!grk_decompress_tile(codec, tileIndex))
      {
        fprintf(stderr, "grk_decompress_tile() failed\n");
        grk_object_unref(codec);
        goto beach;
      }
      grk_image* tileImg = grk_decompress_get_tile_image(codec, tileIndex, true);
      if(!tileImg)
      {
        fprintf(stderr, "Failed to get tile image for tile %u\n", tileIndex);
        grk_object_unref(codec);
        goto beach;
      }
      print_tile_info(tileImg, tileIndex);
      timer.finish();
      if(doStore)
        write_async<int32_t>(tileImg, "async_tile_" + std::to_string(tileIndex));
      grk_object_unref(codec);
      rc = EXIT_SUCCESS;
      goto beach;
    }

    // Step 1: Start async decompression (called once)
    printf("\nStarting async decompression...\n");
    if(!grk_decompress(codec, nullptr))
    {
      fprintf(stderr, "grk_decompress() failed\n");
      grk_object_unref(codec);
      goto beach;
    }

    // Determine image dimensions (accounting for reduce)
    uint32_t imgX0 = headerInfo.header_image.x0;
    uint32_t imgY0 = headerInfo.header_image.y0;
    uint32_t imgX1 = headerInfo.header_image.x1;
    uint32_t imgY1 = headerInfo.header_image.y1;

    for(uint8_t r = 0; r < reduce; ++r)
    {
      imgX1 = (imgX1 + 1) / 2;
      imgY1 = (imgY1 + 1) / 2;
      imgX0 = (imgX0 + 1) / 2;
      imgY0 = (imgY0 + 1) / 2;
    }

    uint32_t fullWidth = imgX1 - imgX0;
    uint32_t fullHeight = imgY1 - imgY0;

    // If no swath height specified, default to tile height for
    // natural swath-based progress through the tile grid
    if(swathHeight == 0)
      swathHeight = headerInfo.t_height;

    printf("Decompressing %ux%u (reduce=%u) in swaths of %u rows\n", fullWidth, fullHeight, reduce,
           swathHeight);

    auto numcomps = headerInfo.header_image.numcomps;
    auto tilePrecBits = headerInfo.header_image.comps[0].prec;

    // Step 2: Wait for swaths and retrieve tile data
    auto y = imgY0;
    auto swathIndex = 0;
    while(y < imgY1)
    {
      auto swathY1 = std::min(y + swathHeight, imgY1);
      auto thisSwathH = swathY1 - y;

      grk_wait_swath swath = {};
      swath.x0 = imgX0;
      swath.y0 = y;
      swath.x1 = imgX1;
      swath.y1 = swathY1;

      // Wait for this swath region to be decoded
      grk_decompress_wait(codec, &swath);

      uint16_t numSwathTiles =
          (uint16_t)((swath.tile_x1 - swath.tile_x0) * (swath.tile_y1 - swath.tile_y0));
      printf("Swath %u: rows [%u, %u), %u tiles\n", swathIndex, y, swathY1, numSwathTiles);

      if(useSwathBuf)
      {
        // Step 3a: Schedule tile copies into a user-managed swath buffer via
        // Taskflow + Highway SIMD.  The copy converts int32_t planar tile data
        // to 8-bit (or 16-bit if tile precision > 8) packed BSQ output.
        // Allocation: numcomps planes × swath_height × full_width elements.
        bool use16bit = (tilePrecBits > 8);
        size_t elemSize = use16bit ? sizeof(uint16_t) : sizeof(uint8_t);
        size_t bufBytes = (size_t)numcomps * thisSwathH * fullWidth * elemSize;
        std::vector<uint8_t> swathBufData(bufBytes, 0);

        grk_swath_buffer swathBuf = {};
        swathBuf.data = swathBufData.data();
        swathBuf.prec = use16bit ? (uint8_t)16 : (uint8_t)8;
        swathBuf.sgnd = false;
        swathBuf.numcomps = numcomps;
        swathBuf.pixel_space = (int64_t)elemSize;
        swathBuf.line_space = (int64_t)fullWidth * (int64_t)elemSize;
        swathBuf.band_space = (int64_t)thisSwathH * (int64_t)fullWidth * (int64_t)elemSize;
        swathBuf.band_map = nullptr;
        swathBuf.promote_alpha = -1;
        swathBuf.x0 = imgX0;
        swathBuf.y0 = y;
        swathBuf.x1 = imgX1;
        swathBuf.y1 = swathY1;

        // Schedule copies into swathBuf (runs in parallel on the Taskflow executor)
        grk_decompress_schedule_swath_copy(codec, &swath, &swathBuf);

        // While copies are in-flight, the caller is free to do other work here
        // (e.g., start reading the next swath, process the previous swath, etc.)

        // Wait for all copy tasks for this swath to finish
        grk_decompress_wait_swath_copy(codec);

        printf("  Swath %u: %zu bytes copied to swath buffer (%u-bit)\n", swathIndex, bufBytes,
               swathBuf.prec);

        if(doStore)
        {
          std::string fname = "swathbuf_swath" + std::to_string(swathIndex) + ".raw";
          FILE* fp = fopen(fname.c_str(), "wb");
          if(fp)
          {
            fwrite(swathBufData.data(), 1, bufBytes, fp);
            fclose(fp);
            printf("  Wrote %s\n", fname.c_str());
          }
        }
      }
      else
      {
        // Step 3b: Retrieve per-tile image data directly (original path)
        for(uint16_t ty = swath.tile_y0; ty < swath.tile_y1; ++ty)
        {
          for(uint16_t tx = swath.tile_x0; tx < swath.tile_x1; ++tx)
          {
            uint16_t tidx = (uint16_t)(ty * swath.num_tile_cols + tx);
            grk_image* tileImg;
            if(singleTile)
            {
              // Single-tile: use composite image
              tileImg = grk_decompress_get_image(codec);
            }
            else
            {
              // Multi-tile: get per-tile image
              tileImg = grk_decompress_get_tile_image(codec, tidx, true);
            }
            if(!tileImg)
            {
              fprintf(stderr, "Failed to get tile image for tile %u\n", tidx);
              grk_object_unref(codec);
              goto beach;
            }
            if(doStore)
            {
              std::string fname =
                  "async_swath" + std::to_string(swathIndex) + "_tile" + std::to_string(tidx);
              write_async<int32_t>(tileImg, fname);
            }
          }
        }
      }

      y = swathY1;
      swathIndex++;
    }

    timer.finish();

    if(sReadBytesAsync)
    {
      printf("IO ops: %" PRIu64 ", total bytes read (MB): %lf\n", (uint64_t)sReadCountAsync,
             (double)sReadBytesAsync / (1024 * 1024));
    }

    printf("\nAsync decompression complete: %u swath(s) processed\n", swathIndex);

    grk_object_unref(codec);
  }

  rc = EXIT_SUCCESS;
beach:
  if(sinfo.fp_)
    fclose(sinfo.fp_);

  return rc;
}
