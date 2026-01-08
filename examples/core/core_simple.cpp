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

/***********************************************************

Simple example demonstrating compression and decompression
with memory buffers as source and destination

************************************************************/

#include <cstdio>
#include <cstring>
#include <memory>
#include <inttypes.h>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <cassert>

#include "grok.h"

// template parameter T determines the type of input data: uint8_t, uint16_t etc
template<typename T>
int core_compress(uint32_t dimX, uint32_t dimY, uint8_t precision,
                  std::vector<std::unique_ptr<T[]>>& uncompressedData,
                  std::vector<uint8_t>& compressedData, bool jp2, bool htj2k)
{
  const uint16_t numComps = (uint16_t)uncompressedData.size();
  const auto colourSpace = numComps == 3 ? GRK_CLRSPC_SRGB : GRK_CLRSPC_GRAY;

  grk_object* codec = nullptr; // compression codec object
  int32_t rc = EXIT_FAILURE;
  size_t compressedLength = 0;
  grk_stream_params encCompressedStream = {};

  // 1. initialize compress parameters
  grk_cparameters compressParams;
  grk_compress_set_default_params(&compressParams);
  compressParams.cod_format = jp2 ? GRK_FMT_JP2 : GRK_FMT_J2K;
  compressParams.verbose = true;
  compressParams.irreversible = false; // Enable reversible (lossless) compression
  if(htj2k)
    compressParams.cblk_sty = GRK_CBLKSTY_HT_MIXED;

  encCompressedStream.buf = compressedData.data();
  encCompressedStream.buf_len = compressedData.size();

  // 3. create image that will be passed to encoder
  auto components = std::make_unique<grk_image_comp[]>(numComps);
  for(uint32_t i = 0; i < numComps; ++i)
  {
    auto c = &components[i];
    *c = {};
    c->w = dimX;
    c->h = dimY;
    c->prec = precision;
  }
  auto encInputImage = grk_image_new(numComps, components.get(), colourSpace, true);

  // 4. fill in component data: see grok.h header for full details of image structure
  for(uint16_t compno = 0; compno < encInputImage->numcomps; ++compno)
  {
    auto comp = encInputImage->comps + compno;
    auto compWidth = comp->w;
    auto compHeight = comp->h;
    auto destData = (int32_t*)comp->data;
    if(!destData)
    {
      fprintf(stderr, "Image has null data for component %d\n", compno);
      goto beach;
    }

    // copy from uncompressedData to encInputImage data, respecting stride
    auto srcPtr = uncompressedData[compno].get();
    auto destPtr = destData;
    for(uint32_t j = 0; j < compHeight; ++j)
    {
      for(uint32_t i = 0; i < compWidth; ++i)
        destPtr[i] = srcPtr[i];

      srcPtr += compWidth;
      destPtr += comp->stride;
    }
  }

  // 5. compress
  codec = grk_compress_init(&encCompressedStream, &compressParams, encInputImage);
  if(!codec)
  {
    fprintf(stderr, "Failed to initialize compressor\n");
    goto beach;
  }
  compressedLength = grk_compress(codec, nullptr);
  if(compressedLength == 0)
  {
    fprintf(stderr, "Failed to compress\n");
    goto beach;
  }
  compressedData.resize(compressedLength);

  printf("Compression succeeded: %" PRIu64 " bytes used\n", compressedLength);

  rc = EXIT_SUCCESS;

beach:
  grk_object_unref(codec);
  if(encInputImage)
    grk_object_unref(&encInputImage->obj);
  return rc;
}

// template parameter T determines the type of input data: uint8_t, uint16_t etc
template<typename T>
int core_decompress(std::vector<uint8_t>& compressedData, grk_object*& codec)
{
  grk_header_info headerInfo = {}; // compressed image header info

  grk_stream_params decCompressedStream = {};
  decCompressedStream.buf = compressedData.data();
  decCompressedStream.buf_len = compressedData.size();

  grk_decompress_parameters decompressParams = {};

  // 1. decompress
  codec = grk_decompress_init(&decCompressedStream, &decompressParams);
  if(!grk_decompress_read_header(codec, &headerInfo))
  {
    fprintf(stderr, "Failed to read the header\n");
    return EXIT_FAILURE;
  }
  if(!grk_decompress(codec, nullptr))
  {
    fprintf(stderr, "Decompression failed\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

template<typename T>
int core_simple(uint32_t dimX, uint32_t dimY, uint8_t precision)
{
  uint16_t numComps = 1U;

  assert(precision <= sizeof(T) * 8);

  // 1. allocate and fill uncompressedData buffer with grid pattern
  std::vector<std::unique_ptr<T[]>> uncompressedData(numComps);
  for(uint16_t comp = 0; comp < numComps; ++comp)
  {
    uncompressedData[comp] = std::make_unique<T[]>((size_t)dimX * dimY);
    auto srcPtr = uncompressedData[comp].get();
    const auto white = (T)((1 << precision) - 1);
    for(uint32_t j = 0; j < dimY; ++j)
    {
      for(uint32_t i = 0; i < dimX; ++i)
        srcPtr[i] = (i % 32 == 0 || j % 32 == 0) ? white : 0;
      srcPtr += dimX;
    }
  }

  // 2. run compress and then decompress and compare output with original
  std::vector<uint8_t> compressedData;
  size_t bufLen = (size_t)numComps * (((size_t)precision + 7) / 8) * dimX * dimY;
  compressedData.resize(bufLen);
  bool jp2 = false;
  bool htj2k = false;
  int rc = EXIT_FAILURE;

  auto start_compress = std::chrono::high_resolution_clock::now();
  rc = core_compress<uint16_t>(dimX, dimY, precision, uncompressedData, compressedData, jp2, htj2k);
  auto end_compress = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> compress_time = end_compress - start_compress;
  printf("Compression time: %f seconds\n", compress_time.count());

  if(rc == 0)
  {
    grk_object* codec = nullptr;
    auto start_decompress = std::chrono::high_resolution_clock::now();
    rc = core_decompress<uint16_t>(compressedData, codec);
    auto end_decompress = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> decompress_time = end_decompress - start_decompress;
    printf("Decompression time: %f seconds\n", decompress_time.count());

    // 1. retrieve decompressed image
    bool images_equal = true;
    GRK_COLOR_SPACE colourSpace;
    grk_image* decOutputImage = nullptr; // uncompressed image created by decompressor
    if(numComps != (uint16_t)uncompressedData.size())
    {
      fprintf(stderr, "Decompression failed\n");
      goto beach;
    }
    colourSpace = numComps == 3 ? GRK_CLRSPC_SRGB : GRK_CLRSPC_GRAY;
    decOutputImage = grk_decompress_get_image(codec);
    if(!decOutputImage)
    {
      fprintf(stderr, "Decompression failed\n");
      goto beach;
    }
    printf("Decompression succeeded\n");

    // 2. compare original uncompressedData to decompressed decOutputImage
    if(numComps != decOutputImage->numcomps || (jp2 && colourSpace != decOutputImage->color_space))
    {
      images_equal = false;
    }
    else
    {
      for(uint32_t compno = 0; compno < numComps; ++compno)
      {
        auto decImageComp = decOutputImage->comps + compno;
        if(1 != decImageComp->dx || 1 != decImageComp->dy || dimX != decImageComp->w ||
           dimY != decImageComp->h || precision != decImageComp->prec ||
           false != decImageComp->sgnd)
        {
          images_equal = false;
          break;
        }
        auto in_data = uncompressedData[compno].get();
        auto in_stride = dimX;
        auto out_data = (int32_t*)decImageComp->data;
        auto out_stride = decImageComp->stride;
        bool comp_equal = true;
        for(uint32_t j = 0; j < dimY; ++j)
        {
          bool row_equal = true;
          for(uint32_t i = 0; i < dimX; ++i)
          {
            if((int32_t)in_data[i] != out_data[i])
            {
              row_equal = false;
              break;
            }
          }
          if(!row_equal)
          {
            comp_equal = false;
            break;
          }
          in_data += in_stride;
          out_data += out_stride;
        }
        if(!comp_equal)
        {
          images_equal = false;
          break;
        }
      }
    }
  beach:
    if(images_equal)
      printf("Input and output data buffers are identical\n");
    else
      printf("Input and output data buffers differ\n");

    grk_object_unref(codec);
  }

  return rc;
}

int main(int argc, const char** argv)
{
  if(argc != 3)
  {
    fprintf(stderr, "Usage: %s dimX dimY\n", argv[0]);
    return EXIT_FAILURE;
  }

  uint32_t dimX = (uint32_t)strtoul(argv[1], nullptr, 10);
  uint32_t dimY = (uint32_t)strtoul(argv[2], nullptr, 10);

  return core_simple<uint16_t>(dimX, dimY, 16U);
}