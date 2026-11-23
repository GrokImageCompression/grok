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

// a swath wait that covers all of tile row 0 and the top of tile row 1 returns once
// tile row 0 completes, even after tile row 1 stops at its row limit

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <string>

#include "grok.h"

namespace
{
const uint32_t IMAGE_WIDTH = 512;
const uint32_t TILE_HEIGHT = 2048;
const uint32_t IMAGE_HEIGHT = 2 * TILE_HEIGHT;
const uint32_t ROWS_OF_TILE_ROW_1 = 64;

grk_image* makeImage(void)
{
  grk_image_comp params = {};
  params.dx = 1;
  params.dy = 1;
  params.w = IMAGE_WIDTH;
  params.h = IMAGE_HEIGHT;
  params.prec = 8;
  grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
  if(!image)
    return nullptr;
  auto& comp = image->comps[0];
  auto data = static_cast<int32_t*>(comp.data);
  uint32_t state = 1;
  for(uint32_t y = 0; y < comp.h; ++y)
    for(uint32_t x = 0; x < comp.w; ++x)
    {
      state = state * 1664525u + 1013904223u;
      data[(size_t)y * comp.stride + x] = (int32_t)(state >> 24);
    }
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
  parameters.t_width = IMAGE_WIDTH;
  parameters.t_height = TILE_HEIGHT;

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

bool swathAcrossTileRowsReturns(const std::string& path)
{
  grk_decompress_parameters params = {};
  params.asynchronous = true;
  params.simulate_synchronous = true;
  params.core.skip_allocate_composite = true;

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
  if(!grk_decompress_read_header(codec, &headerInfo) || !grk_decompress_update(&params, codec) ||
     !grk_decompress(codec, nullptr))
  {
    fprintf(stderr, "could not start the decode\n");
    grk_object_unref(codec);
    return false;
  }

  auto waited = std::async(std::launch::async, [codec] {
    grk_wait_swath swath = {};
    swath.x0 = 0;
    swath.y0 = 0;
    swath.x1 = IMAGE_WIDTH;
    swath.y1 = TILE_HEIGHT + ROWS_OF_TILE_ROW_1;
    grk_decompress_wait(codec, &swath);
  });
  if(waited.wait_for(std::chrono::seconds(20)) != std::future_status::ready)
  {
    fprintf(stderr, "swath wait across two tile rows did not return\n");
    // the waiting thread never returns, so the codec cannot be released
    std::_Exit(1);
  }
  grk_object_unref(codec);
  return true;
}
} // namespace

int main(void)
{
  grk_initialize(nullptr, 4, nullptr);
  std::string path = "swath_wait_across_tile_rows.j2k";
  bool ok = compress(path);
  for(int run = 0; ok && run < 20; ++run)
    ok = swathAcrossTileRowsReturns(path);
  remove(path.c_str());
  grk_deinitialize();
  return ok ? 0 : 1;
}
