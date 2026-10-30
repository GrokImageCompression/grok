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

// a window inside one tile of a multi-tile image still yields the tile image

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "grok.h"

namespace
{
const uint32_t IMAGE_SIZE = 64;
const uint32_t TILE_SIZE = 32;
const uint8_t PRECISION = 8;
const uint32_t WINDOW_X0 = 36;
const uint32_t WINDOW_Y0 = 40;
const uint32_t WINDOW_X1 = 50;
const uint32_t WINDOW_Y1 = 60;
const uint16_t WINDOW_TILE = 3;

int32_t expectedSample(uint32_t x, uint32_t y)
{
  return (int32_t)((x * 7 + y * 13) & 0xFF);
}

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
      data[(size_t)y * comp.stride + x] = expectedSample(x, y);
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

bool windowMatches(const grk_image* tileImage, const char* source)
{
  if(!tileImage)
  {
    fprintf(stderr, "%s: no image for tile %u\n", source, WINDOW_TILE);
    return false;
  }
  const auto& comp = tileImage->comps[0];
  if(!comp.data || comp.x0 > WINDOW_X0 || comp.y0 > WINDOW_Y0 || comp.x0 + comp.w < WINDOW_X1 ||
     comp.y0 + comp.h < WINDOW_Y1)
  {
    fprintf(stderr, "%s: tile image <%u,%u,%u,%u> with %s data does not cover the window\n", source,
            comp.x0, comp.y0, comp.x0 + comp.w, comp.y0 + comp.h, comp.data ? "" : "no");
    return false;
  }
  for(uint32_t y = WINDOW_Y0; y < WINDOW_Y1; ++y)
  {
    for(uint32_t x = WINDOW_X0; x < WINDOW_X1; ++x)
    {
      size_t index = (size_t)(y - comp.y0) * comp.stride + (x - comp.x0);
      int32_t got = comp.data_type == GRK_INT_16 ? static_cast<int16_t*>(comp.data)[index]
                                                 : static_cast<int32_t*>(comp.data)[index];
      int32_t want = expectedSample(x, y);
      if(got != want)
      {
        fprintf(stderr, "%s: sample (%u,%u) is %d, expected %d\n", source, x, y, got, want);
        return false;
      }
    }
  }
  return true;
}

struct CallbackResult
{
  bool called = false;
  bool matches = false;
};

void onTileDecompressed(void*, uint16_t tileIndex, grk_image* tileImage, uint8_t, void* userData)
{
  if(tileIndex != WINDOW_TILE)
    return;
  auto result = static_cast<CallbackResult*>(userData);
  result->called = true;
  result->matches = windowMatches(tileImage, "decompress callback");
}

bool decodeWindow(const std::string& path)
{
  CallbackResult callbackResult;
  grk_decompress_parameters params = {};
  params.asynchronous = true;
  params.simulate_synchronous = true;
  params.core.skip_allocate_composite = true;
  params.decompress_callback = onTileDecompressed;
  params.decompress_callback_user_data = &callbackResult;
  params.dw_x0 = WINDOW_X0;
  params.dw_y0 = WINDOW_Y0;
  params.dw_x1 = WINDOW_X1;
  params.dw_y1 = WINDOW_Y1;
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "grk_decompress_init failed\n");
    return false;
  }
  bool ok = false;
  grk_header_info headerInfo = {};
  if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "grk_decompress_read_header failed\n");
  else if(!grk_decompress_update(&params, codec))
    fprintf(stderr, "grk_decompress_update failed\n");
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "grk_decompress failed\n");
  else
  {
    grk_wait_swath swath = {};
    swath.x0 = WINDOW_X0;
    swath.y0 = WINDOW_Y0;
    swath.x1 = WINDOW_X1;
    swath.y1 = WINDOW_Y1;
    grk_decompress_wait(codec, &swath);
    ok = windowMatches(grk_decompress_get_tile_image(codec, WINDOW_TILE, true),
                       "grk_decompress_get_tile_image");
    if(!callbackResult.called)
    {
      fprintf(stderr, "decompress callback was not called for tile %u\n", WINDOW_TILE);
      ok = false;
    }
    ok = ok && callbackResult.matches;
  }
  grk_object_unref(codec);
  return ok;
}
} // namespace

int main(void)
{
  grk_initialize(nullptr, 0, nullptr);

  std::string path = "single_tile_window_tile_image.j2k";
  bool ok = compress(path) && decodeWindow(path);
  remove(path.c_str());

  grk_deinitialize();
  if(ok)
    printf("single tile window tile image test passed\n");
  return ok ? 0 : 1;
}
