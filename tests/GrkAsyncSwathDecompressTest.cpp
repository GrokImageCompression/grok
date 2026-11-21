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

// A tile can carry a later tile part anywhere in the codestream, so a tile row can
// finish long after the rows below it.  The asynchronous batch scheduler throttles
// against the swath the caller waits on, and that swath is only reachable by
// scheduling on, so the decode used to stop dead.  Run under a ctest timeout: a
// stall fails.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
template<size_t N>
void safe_strcpy(char (&dest)[N], const char* src)
{
  size_t len = strnlen(src, N - 1);
  memcpy(dest, src, len);
  dest[len] = '\0';
}

struct CodecDeleter
{
  void operator()(grk_object* codec) const
  {
    if(codec)
      grk_object_unref(codec);
  }
};
using CodecPtr = std::unique_ptr<grk_object, CodecDeleter>;

// extent of one whole image component, in component coordinates
struct PlaneGeometry
{
  uint32_t x0, y0, w, h;
};

using ComponentPlanes = std::vector<std::vector<int32_t>>;

int32_t sampleAt(const grk_image_comp* comp, uint64_t index)
{
  if(comp->data_type == GRK_INT_16)
    return ((const int16_t*)comp->data)[index];

  return ((const int32_t*)comp->data)[index];
}

bool copyIntoPlane(const grk_image_comp* source, const PlaneGeometry& whole,
                   std::vector<int32_t>& plane)
{
  if(!source->data || source->x0 < whole.x0 || source->y0 < whole.y0 ||
     source->x0 + source->w > whole.x0 + whole.w || source->y0 + source->h > whole.y0 + whole.h)
    return false;

  uint32_t offsetX = source->x0 - whole.x0;
  uint32_t offsetY = source->y0 - whole.y0;
  for(uint32_t y = 0; y < source->h; ++y)
  {
    for(uint32_t x = 0; x < source->w; ++x)
      plane[(uint64_t)(offsetY + y) * whole.w + offsetX + x] =
          sampleAt(source, (uint64_t)y * source->stride + x);
  }

  return true;
}

bool decompressWholeImage(const std::string& input, std::vector<PlaneGeometry>& geometry,
                          ComponentPlanes& planes)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  safe_strcpy(streamParams.file, input.c_str());

  CodecPtr codec(grk_decompress_init(&streamParams, &params));
  if(!codec)
    return false;
  grk_header_info headerInfo = {};
  if(!grk_decompress_read_header(codec.get(), &headerInfo))
    return false;
  if(!grk_decompress(codec.get(), nullptr))
    return false;
  auto image = grk_decompress_get_image(codec.get());
  if(!image)
    return false;

  geometry.resize(image->numcomps);
  planes.resize(image->numcomps);
  for(uint16_t compIndex = 0; compIndex < image->numcomps; ++compIndex)
  {
    auto comp = image->comps + compIndex;
    geometry[compIndex] = {comp->x0, comp->y0, comp->w, comp->h};
    planes[compIndex].assign((size_t)comp->w * comp->h, 0);
    if(!copyIntoPlane(comp, geometry[compIndex], planes[compIndex]))
      return false;
  }

  return true;
}

bool decompressSwaths(const std::string& input, const std::vector<PlaneGeometry>& geometry,
                      ComponentPlanes& planes)
{
  grk_decompress_parameters params = {};
  params.asynchronous = true;
  params.simulate_synchronous = true;
  params.core.tile_cache_strategy = GRK_TILE_CACHE_IMAGE;
  params.core.skip_allocate_composite = true;
  grk_stream_params streamParams = {};
  safe_strcpy(streamParams.file, input.c_str());

  CodecPtr codec(grk_decompress_init(&streamParams, &params));
  if(!codec)
    return false;
  grk_header_info headerInfo = {};
  if(!grk_decompress_read_header(codec.get(), &headerInfo))
    return false;
  if(!grk_decompress_update(&params, codec.get()))
    return false;
  if(!grk_decompress(codec.get(), nullptr))
    return false;

  planes.resize(geometry.size());
  for(size_t compIndex = 0; compIndex < geometry.size(); ++compIndex)
    planes[compIndex].assign((size_t)geometry[compIndex].w * geometry[compIndex].h, 0);

  auto y = headerInfo.header_image.y0;
  while(y < headerInfo.header_image.y1)
  {
    grk_wait_swath swath = {};
    swath.x0 = headerInfo.header_image.x0;
    swath.y0 = y;
    swath.x1 = headerInfo.header_image.x1;
    swath.y1 = std::min(y + headerInfo.t_height, headerInfo.header_image.y1);
    grk_decompress_wait(codec.get(), &swath);

    for(uint16_t tileY = swath.tile_y0; tileY < swath.tile_y1; ++tileY)
    {
      for(uint16_t tileX = swath.tile_x0; tileX < swath.tile_x1; ++tileX)
      {
        auto tileIndex = (uint16_t)(tileY * swath.num_tile_cols + tileX);
        auto tileImage = grk_decompress_get_tile_image(codec.get(), tileIndex, true);
        if(!tileImage || (size_t)tileImage->numcomps != geometry.size())
        {
          fprintf(stderr, "no tile image for tile %u\n", tileIndex);
          return false;
        }
        for(uint16_t compIndex = 0; compIndex < tileImage->numcomps; ++compIndex)
        {
          if(!copyIntoPlane(tileImage->comps + compIndex, geometry[compIndex], planes[compIndex]))
          {
            fprintf(stderr, "tile %u component %u does not fit the image plane\n", tileIndex,
                    compIndex);
            return false;
          }
        }
      }
    }
    y = swath.y1;
  }

  return true;
}
} // namespace

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "Usage: %s <codestream>\n", argv[0]);
    return EXIT_FAILURE;
  }
  std::string input = argv[1];

  grk_initialize(nullptr, 0, nullptr);

  std::vector<PlaneGeometry> geometry;
  ComponentPlanes wholeImage;
  if(!decompressWholeImage(input, geometry, wholeImage))
  {
    fprintf(stderr, "%s: synchronous decompress failed\n", input.c_str());
    return EXIT_FAILURE;
  }

  ComponentPlanes swaths;
  if(!decompressSwaths(input, geometry, swaths))
  {
    fprintf(stderr, "%s: asynchronous simulate-synchronous decompress failed\n", input.c_str());
    return EXIT_FAILURE;
  }

  for(size_t compIndex = 0; compIndex < wholeImage.size(); ++compIndex)
  {
    if(swaths[compIndex] != wholeImage[compIndex])
    {
      fprintf(stderr, "%s: component %zu differs between the swath and whole image decodes\n",
              input.c_str(), compIndex);
      return EXIT_FAILURE;
    }
  }

  grk_deinitialize();

  return EXIT_SUCCESS;
}
