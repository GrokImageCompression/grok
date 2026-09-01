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

// window decode of a corrupt stream must read failed code blocks as zero and
// still decode the rest. the first decode dirties the heap, so an
// uninitialized read or a timing-dependent skip changes the second image.

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
const uint32_t WINDOW_SIZE = 1024;

int32_t sampleAt(const grk_image_comp& comp, uint64_t index)
{
  if(comp.data_type == GRK_INT_16)
    return static_cast<int16_t*>(comp.data)[index];
  return static_cast<int32_t*>(comp.data)[index];
}

bool decodeWindow(const std::string& path, std::vector<int32_t>& samples)
{
  grk_decompress_parameters params = {};
  params.dw_x1 = WINDOW_SIZE;
  params.dw_y1 = WINDOW_SIZE;
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "grk_decompress_init failed\n");
    return false;
  }
  grk_header_info headerInfo = {};
  if(!grk_decompress_read_header(codec, &headerInfo))
  {
    fprintf(stderr, "grk_decompress_read_header failed\n");
    grk_object_unref(codec);
    return false;
  }
  if(!grk_decompress(codec, nullptr))
  {
    fprintf(stderr, "grk_decompress failed\n");
    grk_object_unref(codec);
    return false;
  }
  grk_image* image = grk_decompress_get_image(codec);
  if(!image)
  {
    fprintf(stderr, "grk_decompress_get_image returned null\n");
    grk_object_unref(codec);
    return false;
  }
  samples.clear();
  for(uint16_t c = 0; c < image->numcomps; ++c)
  {
    const auto& comp = image->comps[c];
    if(!comp.data)
      continue;
    for(uint32_t y = 0; y < comp.h; ++y)
      for(uint32_t x = 0; x < comp.w; ++x)
        samples.push_back(sampleAt(comp, (uint64_t)y * comp.stride + x));
  }
  grk_object_unref(codec);
  return true;
}
} // namespace

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "usage: %s <corrupt codestream>\n", argv[0]);
    return 1;
  }
  std::string path = argv[1];

  grk_initialize(nullptr, 0, nullptr);

  int rc = 1;
  std::vector<int32_t> first, second;
  if(decodeWindow(path, first) && decodeWindow(path, second))
  {
    if(first.empty())
      fprintf(stderr, "decode produced no samples\n");
    else if(first != second)
      fprintf(stderr, "repeat decode of the same stream changed %zu samples\n", first.size());
    else
    {
      printf("both decodes produced the same %zu samples\n", first.size());
      rc = 0;
    }
  }
  grk_deinitialize();
  return rc;
}
