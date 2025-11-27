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

  grk_object* codec = nullptr;
  grk_image* inputImage = nullptr;
  grk_image* outputImage = nullptr;
  grk_header_info headerInfo = {};
  int32_t rc = EXIT_FAILURE;

  uint64_t compressedLength = 0;

  bool images_equal = true;

  // 1. initialize library
  grk_initialize(nullptr, 0);

  // 2. initialize compress and decompress parameters
  grk_cparameters compressParams;
  grk_compress_set_default_params(&compressParams);
  compressParams.cod_format = GRK_FMT_JP2;
  compressParams.verbose = true;
  compressParams.irreversible = 0; // Enable reversible (lossless) compression

  grk_decompress_parameters decompressParams = {};

  // 3.initialize input and output stream parameters
  grk_stream_params outputStreamParams = {};
  std::unique_ptr<uint8_t[]> data;
  size_t bufLen = (size_t)numComps * ((precision + 7) / 8) * dimX * dimY;
  data = std::make_unique<uint8_t[]>(bufLen);
  outputStreamParams.buf = data.get();
  outputStreamParams.buf_len = bufLen;

  grk_stream_params inputStreamParams = {};
  inputStreamParams.buf = data.get();

  // 4. create input image
  auto components = std::make_unique<grk_image_comp[]>(numComps);
  for(uint32_t i = 0; i < numComps; ++i)
  {
    auto c = &components[i];
    c->w = dimX;
    c->h = dimY;
    c->dx = 1;
    c->dy = 1;
    c->prec = precision;
    c->sgnd = false;
  }
  inputImage = grk_image_new(numComps, components.get(), colourSpace, true);

  // create original data buffers (contiguous, stride == width)
  std::vector<std::unique_ptr<int32_t[]>> originalBuffers(numComps);

  // fill in component data: see grok.h header for full details of image structure
  for(uint16_t compno = 0; compno < inputImage->numcomps; ++compno)
  {
    auto comp = inputImage->comps + compno;
    auto compWidth = comp->w;
    auto compHeight = comp->h;
    auto destData = (int32_t*)comp->data;
    if(!destData)
    {
      fprintf(stderr, "Image has null data for component %d\n", compno);
      goto beach;
    }

    // allocate and fill original buffer with grid pattern (contiguous)
    originalBuffers[compno] = std::make_unique<int32_t[]>((size_t)compWidth * compHeight);
    auto srcPtr = originalBuffers[compno].get();
    for(uint32_t j = 0; j < compHeight; ++j)
    {
      for(uint32_t i = 0; i < compWidth; ++i)
      {
        srcPtr[i] = (i % 32 == 0 || j % 32 == 0) ? 65535 : 0;
      }
      srcPtr += compWidth;
    }

    // copy from original buffer to input image data, respecting stride
    auto destPtr = destData;
    srcPtr = originalBuffers[compno].get();
    for(uint32_t j = 0; j < compHeight; ++j)
    {
      memcpy(destPtr, srcPtr, (size_t)compWidth * sizeof(int32_t));
      srcPtr += compWidth;
      destPtr += comp->stride;
    }
  }

  // 5. initialize compressor
  codec = grk_compress_init(&outputStreamParams, &compressParams, inputImage);
  if(!codec)
  {
    fprintf(stderr, "Failed to initialize compressor\n");
    goto beach;
  }

  // 6. compress
  compressedLength = grk_compress(codec, nullptr);
  if(compressedLength == 0)
  {
    fprintf(stderr, "Failed to compress\n");
    goto beach;
  }
  printf("Compression succeeded: %" PRIu64 " bytes used.\n", compressedLength);

  // 7. complete initialization of input stream parameters
  inputStreamParams.buf_len = compressedLength;

  grk_object_unref(codec);
  codec = grk_decompress_init(&inputStreamParams, &decompressParams);
  if(!grk_decompress_read_header(codec, &headerInfo))
  {
    fprintf(stderr, "Failed to read the header\n");
    goto beach;
  }

  // 8. decompress image
  if(!grk_decompress(codec, nullptr))
  {
    fprintf(stderr, "Decompression failed\n");
    goto beach;
  }

  // 9. retrieve decompressed image
  outputImage = grk_decompress_get_image(codec);
  if(!outputImage)
  {
    fprintf(stderr, "Decompression failed\n");
    goto beach;
  }

  printf("Decompression succeeded\n");

  // 10. compare original buffers and output image
  if(inputImage->numcomps != outputImage->numcomps || inputImage->x0 != outputImage->x0 ||
     inputImage->y0 != outputImage->y0 || inputImage->x1 != outputImage->x1 ||
     inputImage->y1 != outputImage->y1 || inputImage->color_space != outputImage->color_space)
  {
    images_equal = false;
  }
  else
  {
    for(uint32_t compno = 0; compno < inputImage->numcomps; ++compno)
    {
      auto in_comp = inputImage->comps + compno;
      auto out_comp = outputImage->comps + compno;
      if(in_comp->dx != out_comp->dx || in_comp->dy != out_comp->dy || in_comp->w != out_comp->w ||
         in_comp->h != out_comp->h || in_comp->prec != out_comp->prec ||
         in_comp->sgnd != out_comp->sgnd)
      {
        images_equal = false;
        break;
      }
      auto in_data = originalBuffers[compno].get();
      auto in_stride = in_comp->w;
      auto out_data = (int32_t*)out_comp->data;
      auto out_stride = out_comp->stride;
      bool comp_equal = true;
      for(uint32_t j = 0; j < in_comp->h; ++j)
      {
        if(memcmp(in_data, out_data, in_comp->w * sizeof(int32_t)) != 0)
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
  {
    printf("Input and output images are identical\n");
  }
  else
  {
    printf("Input and output images differ\n");
  }

  rc = EXIT_SUCCESS;
beach:

  // cleanup
  grk_object_unref(codec);
  if(inputImage)
    grk_object_unref(&inputImage->obj);
  // note: since outputImage was allocated by library, it will be cleaned
  // up by library
  grk_deinitialize();

  return rc;
}