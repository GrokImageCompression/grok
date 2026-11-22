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
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

#include "grok.h"

inline uint64_t compressInStrips(grk_stream_params* stream, grk_cparameters* parameters,
                                 grk_image* image)
{
  const uint32_t stripRows[] = {1, 13, 64, 7};
  auto codec = grk_compress_init_strips(stream, parameters, image);
  if(!codec)
    return 0;
  const uint32_t height = image->comps[0].h;
  const uint32_t stride = image->comps[0].stride;
  const size_t sampleSize = image->comps[0].data_type == GRK_INT_16 ? 2 : 4;
  std::vector<const void*> planes(image->numcomps);
  bool pushed = true;
  uint32_t push = 0;
  for(uint32_t y = 0; pushed && y < height; ++push)
  {
    uint32_t rows = std::min(stripRows[push % std::size(stripRows)], height - y);
    for(uint16_t c = 0; c < image->numcomps; ++c)
      planes[c] = (const uint8_t*)image->comps[c].data + (size_t)y * stride * sampleSize;
    grk_strip strip = {rows, planes.data(), stride};
    pushed = grk_compress_push_strip(codec, &strip);
    y += rows;
  }
  uint64_t length = pushed ? grk_compress(codec, nullptr) : 0;
  grk_object_unref(codec);
  return length;
}
