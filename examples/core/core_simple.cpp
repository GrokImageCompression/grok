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

/***********************************************************

Simple example demonstrating compression and decompression
with memory buffers as source and destination

************************************************************/

#include <cstdio>
#include <cstring>
#include <memory>
#include <inttypes.h>
#include <vector>

#include "grok.h"

int main([[maybe_unused]] int argc, [[maybe_unused]] const char** argv)
{
  const uint32_t dimX = 640;
  const uint32_t dimY = 480;
  const auto colourSpace = GRK_CLRSPC_GRAY;
  const uint32_t numComps = colourSpace == GRK_CLRSPC_SRGB ? 3 : 1;
  const uint32_t precision = 16;

  grk_object* codec = nullptr; // compression/decompression codec object
  grk_image* encInputImage = nullptr; // uncompressed image passed into compressor
  grk_image* decOutputImage = nullptr; // uncompressed image created by decompressor
  grk_header_info headerInfo = {}; // compressed image header info
  int32_t rc = EXIT_FAILURE;

  uint64_t compressedLength = 0; // length of compressed stream

  bool images_equal = true;
  grk_stream_params decCompressedStream = {}; // compressed stream passed to decompressor

  // 1. initialize compress and decompress parameters
  grk_cparameters compressParams;
  grk_compress_set_default_params(&compressParams);
  compressParams.cod_format = GRK_FMT_JP2;
  compressParams.verbose = true;
  compressParams.irreversible = false; // Enable reversible (lossless) compression

  grk_decompress_parameters decompressParams = {};

  // 2.initialize struct holding encoder compressed stream
  std::unique_ptr<uint8_t[]> compressedData;
  // allocate size of input image, assuming that compressed stream
  // is smaller than input
  size_t bufLen = (size_t)numComps * ((precision + 7) / 8) * dimX * dimY;
  compressedData = std::make_unique<uint8_t[]>(bufLen);

  grk_stream_params encCompressedStream = {};
  encCompressedStream.buf = compressedData.get();
  encCompressedStream.buf_len = bufLen;

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
  encInputImage = grk_image_new(numComps, components.get(), colourSpace, true);

  // create uncompressed data buffers (contiguous, stride == width)
  // these buffers will be copied to the grk_image component data
  // before compression
  std::vector<std::unique_ptr<int32_t[]>> uncompressedData(numComps);

  // fill in component data: see grok.h header for full details of image structure
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

    // allocate and fill uncompressedData buffer with grid pattern (contiguous)
    uncompressedData[compno] = std::make_unique<int32_t[]>((size_t)compWidth * compHeight);
    auto srcPtr = uncompressedData[compno].get();
    const int32_t white = (1 << precision) - 1;
    for(uint32_t j = 0; j < compHeight; ++j)
    {
      for(uint32_t i = 0; i < compWidth; ++i)
        srcPtr[i] = (i % 32 == 0 || j % 32 == 0) ? white : 0;
      srcPtr += compWidth;
    }

    // copy from uncompressedData to encInputImage data, respecting stride
    auto destPtr = destData;
    srcPtr = uncompressedData[compno].get();
    for(uint32_t j = 0; j < compHeight; ++j)
    {
      memcpy(destPtr, srcPtr, (size_t)compWidth * sizeof(int32_t));
      srcPtr += compWidth;
      destPtr += comp->stride;
    }
  }

  // 4. compress
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
  // destroy compression codec
  grk_object_unref(codec);
  printf("Compression succeeded: %" PRIu64 " bytes used\n", compressedLength);

  // 5. copy encCompressedStream to decCompressedStream to prepare for decompression
  decCompressedStream.buf = encCompressedStream.buf;
  decCompressedStream.buf_len = compressedLength;

  // 6. decompress
  codec = grk_decompress_init(&decCompressedStream, &decompressParams);
  if(!grk_decompress_read_header(codec, &headerInfo))
  {
    fprintf(stderr, "Failed to read the header\n");
    goto beach;
  }
  if(!grk_decompress(codec, nullptr))
  {
    fprintf(stderr, "Decompression failed\n");
    goto beach;
  }

  // 7. retrieve decompressed image
  decOutputImage = grk_decompress_get_image(codec);
  if(!decOutputImage)
  {
    fprintf(stderr, "Decompression failed\n");
    goto beach;
  }
  printf("Decompression succeeded\n");

  // 8. compare original uncompressedData to decompressed decInputmage
  if(encInputImage->numcomps != decOutputImage->numcomps ||
     encInputImage->x0 != decOutputImage->x0 || encInputImage->y0 != decOutputImage->y0 ||
     encInputImage->x1 != decOutputImage->x1 || encInputImage->y1 != decOutputImage->y1 ||
     encInputImage->color_space != decOutputImage->color_space)
  {
    images_equal = false;
  }
  else
  {
    for(uint32_t compno = 0; compno < encInputImage->numcomps; ++compno)
    {
      auto encImageComp = encInputImage->comps + compno;
      auto decImageComp = decOutputImage->comps + compno;
      if(encImageComp->dx != decImageComp->dx || encImageComp->dy != decImageComp->dy ||
         encImageComp->w != decImageComp->w || encImageComp->h != decImageComp->h ||
         encImageComp->prec != decImageComp->prec || encImageComp->sgnd != decImageComp->sgnd)
      {
        images_equal = false;
        break;
      }
      auto in_data = uncompressedData[compno].get();
      auto in_stride = encImageComp->w;
      auto out_data = (int32_t*)decImageComp->data;
      auto out_stride = decImageComp->stride;
      bool comp_equal = true;
      for(uint32_t j = 0; j < encImageComp->h; ++j)
      {
        if(memcmp(in_data, out_data, encImageComp->w * sizeof(int32_t)) != 0)
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
  if(images_equal)
    printf("Input and output data buffers are identical\n");
  else
    printf("Input and output data bufffers differ\n");

  rc = EXIT_SUCCESS;

beach:
  // cleanup
  grk_object_unref(codec);
  if(encInputImage)
    grk_object_unref(&encInputImage->obj);
  // note: since decImage was allocated by library, it will be cleaned up by library
  grk_deinitialize();

  return rc;
}