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

// a window with no sample of a 4x subsampled component still decodes the other components

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "grok.h"

namespace
{
const uint32_t IMAGE_SIZE = 64;
const uint32_t TILE_SIZE = 32;
const uint16_t NUM_COMPONENTS = 3;
const uint16_t SUBSAMPLED_COMPONENT = 2;
const uint8_t SUBSAMPLING = 4;
const uint8_t PRECISION = 8;
// ceil(5/4) == ceil(7/4), so the subsampled component has no column in the window
const uint32_t WINDOW_X0 = 5;
const uint32_t WINDOW_X1 = 7;

int32_t expectedSample(uint32_t x, uint32_t y, uint16_t c)
{
  return (int32_t)((x * 7 + y * 13 + c * 53) & 0xFF);
}

grk_image* makeImage(void)
{
  grk_image_comp params[NUM_COMPONENTS] = {};
  for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
  {
    bool subsampled = c == SUBSAMPLED_COMPONENT;
    params[c].dx = subsampled ? SUBSAMPLING : 1;
    params[c].dy = 1;
    params[c].w = subsampled ? IMAGE_SIZE / SUBSAMPLING : IMAGE_SIZE;
    params[c].h = IMAGE_SIZE;
    params[c].prec = PRECISION;
  }
  grk_image* image = grk_image_new(NUM_COMPONENTS, params, GRK_CLRSPC_UNKNOWN, true);
  if(!image)
    return nullptr;
  for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
  {
    auto& comp = image->comps[c];
    auto data = static_cast<int32_t*>(comp.data);
    for(uint32_t y = 0; y < comp.h; ++y)
      for(uint32_t x = 0; x < comp.w; ++x)
        data[(size_t)y * comp.stride + x] = expectedSample(x, y, c);
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

bool componentMatches(const grk_image_comp& comp, uint16_t c)
{
  if(comp.w != WINDOW_X1 - WINDOW_X0 || comp.h != IMAGE_SIZE || !comp.data)
  {
    fprintf(stderr, "component %u is %ux%u with %s data\n", c, comp.w, comp.h,
            comp.data ? "" : "no");
    return false;
  }
  for(uint32_t y = 0; y < comp.h; ++y)
  {
    for(uint32_t x = 0; x < comp.w; ++x)
    {
      int32_t got = comp.data_type == GRK_INT_16
                        ? static_cast<int16_t*>(comp.data)[(size_t)y * comp.stride + x]
                        : static_cast<int32_t*>(comp.data)[(size_t)y * comp.stride + x];
      int32_t want = expectedSample(x + WINDOW_X0, y, c);
      if(got != want)
      {
        fprintf(stderr, "component %u sample (%u,%u) is %d, expected %d\n", c, x, y, got, want);
        return false;
      }
    }
  }
  return true;
}

bool decodeWindow(const std::string& path)
{
  grk_decompress_parameters params = {};
  params.dw_x0 = WINDOW_X0;
  params.dw_x1 = WINDOW_X1;
  params.dw_y0 = 0;
  params.dw_y1 = IMAGE_SIZE;
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
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "grk_decompress failed\n");
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!image)
      fprintf(stderr, "grk_decompress_get_image returned null\n");
    else
    {
      ok = true;
      for(uint16_t c = 0; c < NUM_COMPONENTS && ok; ++c)
      {
        const auto& comp = image->comps[c];
        if(c == SUBSAMPLED_COMPONENT)
        {
          if(comp.w != 0)
          {
            fprintf(stderr, "subsampled component is %u wide, expected 0\n", comp.w);
            ok = false;
          }
        }
        else
          ok = componentMatches(comp, c);
      }
    }
  }
  grk_object_unref(codec);
  return ok;
}
} // namespace

int main(void)
{
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "0");
#else
  setenv("GRK_MERCURY", "0", 1);
#endif
  grk_initialize(nullptr, 0, nullptr);

  std::string path = "empty_component_multi_tile.j2k";
  bool ok = compress(path) && decodeWindow(path);
  remove(path.c_str());

  grk_deinitialize();
  if(ok)
    printf("empty component multi tile test passed\n");
  return ok ? 0 : 1;
}
