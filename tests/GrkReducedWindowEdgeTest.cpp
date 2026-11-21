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

// a reduced window ending at the reduced image edge decodes without warnings

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "grok.h"

namespace
{
const uint32_t IMAGE_SIZE = 100;
const uint32_t TILE_SIZE = 32;
const uint8_t PRECISION = 8;
const uint8_t REDUCE = 3;
// ceil(100 / 8), which scales back up to 104
const uint32_t REDUCED_SIZE = 13;

grk_image* makeImage(void)
{
  grk_image_comp params = {};
  params.dx = 1;
  params.dy = 1;
  params.w = IMAGE_SIZE;
  params.h = IMAGE_SIZE;
  params.prec = PRECISION;
  grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
  if(!image)
    return nullptr;
  auto& comp = image->comps[0];
  auto data = static_cast<int32_t*>(comp.data);
  for(uint32_t y = 0; y < comp.h; ++y)
    for(uint32_t x = 0; x < comp.w; ++x)
      data[(size_t)y * comp.stride + x] = (int32_t)((x * 7 + y * 13) & 0xFF);
  return image;
}

bool compress(const std::string& path)
{
  grk_image* image = makeImage();
  if(!image)
  {
    fprintf(stderr, "could not build the source image\n");
    return false;
  }
  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.tile_size_on = true;
  parameters.t_width = TILE_SIZE;
  parameters.t_height = TILE_SIZE;
  parameters.numlayers = 1;

  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  bool ok = false;
  if(!codec)
    fprintf(stderr, "grk_compress_init failed\n");
  else
  {
    ok = grk_compress(codec, nullptr) != 0;
    if(!ok)
      fprintf(stderr, "grk_compress failed\n");
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);
  return ok;
}

void countWarning(const char*, void* warnings)
{
  ++*static_cast<uint32_t*>(warnings);
}

// returns the number of warnings, or -1 if the decode fails or has the wrong size
int decodeReducedWindow(const std::string& path, uint32_t x1, uint32_t y1, uint32_t expectedSize)
{
  uint32_t warnings = 0;
  grk_msg_handlers handlers = {};
  handlers.warn_callback = countWarning;
  handlers.warn_data = &warnings;
  grk_set_msg_handlers(handlers);

  grk_decompress_parameters params = {};
  params.core.reduce = REDUCE;
  params.dw_reduced = true;
  params.dw_x1 = x1;
  params.dw_y1 = y1;
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "grk_decompress_init failed\n");
    return -1;
  }
  int result = -1;
  grk_header_info headerInfo = {};
  if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "grk_decompress_read_header failed\n");
  else if(!grk_decompress_update(&params, codec))
    fprintf(stderr, "grk_decompress_update failed\n");
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "grk_decompress failed\n");
  else
  {
    const auto& comp = grk_decompress_get_image(codec)->comps[0];
    if(comp.w != expectedSize || comp.h != expectedSize)
      fprintf(stderr, "window %u x %u decoded to %u x %u, expected %u x %u\n", x1, y1, comp.w,
              comp.h, expectedSize, expectedSize);
    else
      result = (int)warnings;
  }
  grk_object_unref(codec);
  grk_set_msg_handlers(grk_msg_handlers{});
  return result;
}
} // namespace

int main(void)
{
  grk_initialize(nullptr, 0, nullptr);

  std::string path = "reduced_window_edge.j2k";
  bool ok = compress(path);
  if(ok)
  {
    int edgeWarnings = decodeReducedWindow(path, REDUCED_SIZE, REDUCED_SIZE, REDUCED_SIZE);
    if(edgeWarnings != 0)
    {
      fprintf(stderr, "window at the reduced image edge: %d warnings, expected 0\n", edgeWarnings);
      ok = false;
    }
    int outsideWarnings =
        decodeReducedWindow(path, REDUCED_SIZE + 2, REDUCED_SIZE + 2, REDUCED_SIZE);
    if(outsideWarnings <= 0)
    {
      fprintf(stderr, "window past the reduced image edge: %d warnings, expected some\n",
              outsideWarnings);
      ok = false;
    }
  }
  remove(path.c_str());

  grk_deinitialize();
  if(ok)
    printf("reduced window edge test passed\n");
  return ok ? 0 : 1;
}
