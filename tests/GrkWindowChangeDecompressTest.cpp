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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
// 14x15 tiles over 61x67 puts the second window on a different set of tiles
// than the first, which is where a stale cached tile shows up
const uint32_t IMAGE_WIDTH = 61;
const uint32_t IMAGE_HEIGHT = 67;
const uint32_t TILE_WIDTH = 14;
const uint32_t TILE_HEIGHT = 15;
const uint16_t NUM_COMPONENTS = 1;
const uint8_t PRECISION = 8;

struct Window
{
  uint32_t x0, y0, x1, y1;
};

// the second window covers the first window's tiles and more, so a cached tile
// from the first decode is on the second decode's path
const Window FIRST_WINDOW = {0, 0, 20, 22};
const Window SECOND_WINDOW = {7, 9, 47, 52};

struct Plane
{
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<int32_t> samples;
};

void reportLog(const char* message, void*)
{
  fprintf(stderr, "%s\n", message);
}

int32_t sampleAt(const grk_image_comp& comp, uint64_t index)
{
  if(comp.data_type == GRK_INT_16)
    return static_cast<int16_t*>(comp.data)[index];
  return static_cast<int32_t*>(comp.data)[index];
}

bool capture(grk_image* image, Plane& out)
{
  const auto& source = image->comps[0];
  if(!source.data || source.w == 0 || source.h == 0)
  {
    fprintf(stderr, "decoded component is empty: %ux%u\n", source.w, source.h);
    return false;
  }
  out.width = source.w;
  out.height = source.h;
  out.samples.resize((size_t)source.w * source.h);
  for(uint32_t y = 0; y < source.h; ++y)
    for(uint32_t x = 0; x < source.w; ++x)
      out.samples[(size_t)y * source.w + x] = sampleAt(source, (uint64_t)y * source.stride + x);
  return true;
}

grk_image* makeImage(void)
{
  grk_image_comp params[NUM_COMPONENTS] = {};
  for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
  {
    params[c].dx = 1;
    params[c].dy = 1;
    params[c].w = IMAGE_WIDTH;
    params[c].h = IMAGE_HEIGHT;
    params[c].prec = PRECISION;
    params[c].sgnd = false;
  }
  grk_image* image = grk_image_new(NUM_COMPONENTS, params, GRK_CLRSPC_GRAY, true);
  if(!image)
    return nullptr;
  auto* data = static_cast<int32_t*>(image->comps[0].data);
  if(!data)
  {
    grk_object_unref(&image->obj);
    return nullptr;
  }
  uint32_t stride = image->comps[0].stride;
  for(uint32_t y = 0; y < IMAGE_HEIGHT; ++y)
    for(uint32_t x = 0; x < IMAGE_WIDTH; ++x)
      data[(size_t)y * stride + x] = (int32_t)((x * 7 + y * 13 + ((x * y) % 29) * 3) & 0xFF);
  return image;
}

bool compress(const std::string& path, bool tiled)
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
  parameters.irreversible = false;
  if(tiled)
  {
    parameters.tile_size_on = true;
    parameters.t_width = TILE_WIDTH;
    parameters.t_height = TILE_HEIGHT;
  }

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

void fillParams(grk_decompress_parameters& params, const Window& window)
{
  params = {};
  params.dw_x0 = window.x0;
  params.dw_y0 = window.y0;
  params.dw_x1 = window.x1;
  params.dw_y1 = window.y1;
}

bool decodeOnce(const std::string& path, const Window& window, Plane& out)
{
  grk_decompress_parameters params;
  fillParams(params, window);
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
      ok = capture(image, out);
  }
  grk_object_unref(codec);
  return ok;
}

// decode the first window, then move the window and decode again on the same codec
bool decodeTwice(const std::string& path, const Window& first, const Window& second, Plane& out)
{
  grk_decompress_parameters params;
  fillParams(params, first);
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
    fprintf(stderr, "first grk_decompress failed\n");
  else
  {
    grk_decompress_parameters secondParams;
    fillParams(secondParams, second);
    if(!grk_decompress_update(&secondParams, codec))
      fprintf(stderr, "grk_decompress_update failed\n");
    else if(!grk_decompress(codec, nullptr))
      fprintf(stderr, "second grk_decompress failed\n");
    else
    {
      grk_image* image = grk_decompress_get_image(codec);
      if(!image)
        fprintf(stderr, "grk_decompress_get_image returned null\n");
      else
        ok = capture(image, out);
    }
  }
  grk_object_unref(codec);
  return ok;
}

bool samePlane(const char* label, const Plane& got, const Plane& expected)
{
  if(got.width != expected.width || got.height != expected.height)
  {
    fprintf(stderr, "%s: window decode is %ux%u, a fresh codec gives %ux%u\n", label, got.width,
            got.height, expected.width, expected.height);
    return false;
  }
  for(size_t i = 0; i < expected.samples.size(); ++i)
  {
    if(got.samples[i] != expected.samples[i])
    {
      uint32_t x = (uint32_t)(i % expected.width);
      uint32_t y = (uint32_t)(i / expected.width);
      fprintf(stderr, "%s: sample (%u,%u) is %d, a fresh codec gives %d\n", label, x, y,
              got.samples[i], expected.samples[i]);
      return false;
    }
  }
  return true;
}

bool checkStream(const char* label, bool tiled)
{
  std::string path = std::string("window_change_") + label + ".j2k";
  if(!compress(path, tiled))
    return false;

  Plane fresh;
  Plane reused;
  bool ok = decodeOnce(path, SECOND_WINDOW, fresh) &&
            decodeTwice(path, FIRST_WINDOW, SECOND_WINDOW, reused) &&
            samePlane(label, reused, fresh);
  remove(path.c_str());
  if(ok)
    printf("%s: the second window decodes the same on a reused codec\n", label);
  return ok;
}
} // namespace

int main(void)
{
  grk_initialize(nullptr, 0, nullptr);
  grk_msg_handlers handlers = {};
  handlers.error_callback = reportLog;
  grk_set_msg_handlers(handlers);

  bool ok = checkStream("single_tile", false);
  ok = checkStream("multi_tile", true) && ok;

  grk_deinitialize();
  return ok ? 0 : 1;
}
