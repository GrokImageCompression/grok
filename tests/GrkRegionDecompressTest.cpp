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
#include <string>
#include <vector>

#include "grok.h"

namespace
{
// 14x15 tiles put four of the five tile rows at an origin the image origin does
// not share, which is what the band window derivation keys on
const uint32_t IMAGE_WIDTH = 61;
const uint32_t IMAGE_HEIGHT = 67;
const uint32_t TILE_WIDTH = 14;
const uint32_t TILE_HEIGHT = 15;
const uint16_t NUM_COMPONENTS = 1;
const uint8_t PRECISION = 8;

struct Plane
{
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<int32_t> samples;
};

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

// high frequency content in both directions so a dropped or misaligned
// lifting step shows up as a sample difference
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
  parameters.irreversible = false;
  parameters.tile_size_on = true;
  parameters.t_width = TILE_WIDTH;
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

// window == nullptr decodes the whole image
bool decode(const std::string& path, const uint32_t* window, Plane& out)
{
  grk_decompress_parameters params = {};
  if(window)
  {
    params.dw_x0 = window[0];
    params.dw_y0 = window[1];
    params.dw_x1 = window[2];
    params.dw_y1 = window[3];
  }
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

bool sameAsCrop(const Plane& full, const Plane& window, uint32_t x0, uint32_t y0)
{
  if(window.width != 0 && window.height != 0 &&
     (window.width > full.width - x0 || window.height > full.height - y0))
  {
    fprintf(stderr, "window (%u,%u) %ux%u does not fit the full decode %ux%u\n", x0, y0,
            window.width, window.height, full.width, full.height);
    return false;
  }
  for(uint32_t y = 0; y < window.height; ++y)
  {
    for(uint32_t x = 0; x < window.width; ++x)
    {
      int32_t got = window.samples[(size_t)y * window.width + x];
      int32_t expected = full.samples[(size_t)(y0 + y) * full.width + x0 + x];
      if(got != expected)
      {
        fprintf(stderr, "window (%u,%u,%u,%u): sample (%u,%u) is %d, whole image decode has %d\n",
                x0, y0, x0 + window.width, y0 + window.height, x0 + x, y0 + y, got, expected);
        return false;
      }
    }
  }
  return true;
}

bool checkWindow(const std::string& path, const Plane& full, uint32_t x0, uint32_t y0, uint32_t x1,
                 uint32_t y1)
{
  const uint32_t window[4] = {x0, y0, x1, y1};
  Plane decoded;
  if(!decode(path, window, decoded))
  {
    fprintf(stderr, "window (%u,%u,%u,%u) failed to decode\n", x0, y0, x1, y1);
    return false;
  }
  if(decoded.width != x1 - x0 || decoded.height != y1 - y0)
  {
    fprintf(stderr, "window (%u,%u,%u,%u) decoded as %ux%u\n", x0, y0, x1, y1, decoded.width,
            decoded.height);
    return false;
  }
  return sameAsCrop(full, decoded, x0, y0);
}
} // namespace

int main(void)
{
  grk_initialize(nullptr, 0, nullptr);

  std::string path = "region_decompress_test.j2k";
  if(!compress(path))
  {
    grk_deinitialize();
    return 1;
  }

  Plane full;
  if(!decode(path, nullptr, full))
  {
    remove(path.c_str());
    grk_deinitialize();
    return 1;
  }
  if(full.width != IMAGE_WIDTH || full.height != IMAGE_HEIGHT)
  {
    fprintf(stderr, "whole image decode is %ux%u\n", full.width, full.height);
    remove(path.c_str());
    grk_deinitialize();
    return 1;
  }

  // both sweeps walk every offset, so every tile-interior and tile-edge
  // start position is covered whatever the tile grid parity
  const uint32_t heights[] = {1, 2, 3, 5};
  const uint32_t widths[] = {2, 3, 5};
  uint32_t failures = 0;

  for(uint32_t y0 = 0; y0 < IMAGE_HEIGHT; ++y0)
  {
    for(uint32_t height : heights)
    {
      uint32_t y1 = y0 + height;
      if(y1 > IMAGE_HEIGHT)
        continue;
      if(!checkWindow(path, full, 0, y0, IMAGE_WIDTH, y1))
        ++failures;
    }
  }

  for(uint32_t x0 = 0; x0 < IMAGE_WIDTH; ++x0)
  {
    for(uint32_t width : widths)
    {
      uint32_t x1 = x0 + width;
      if(x1 > IMAGE_WIDTH)
        continue;
      if(!checkWindow(path, full, x0, 0, x1, IMAGE_HEIGHT))
        ++failures;
    }
  }

  // rectangles that clip on all four sides at once
  for(uint32_t y0 = 0; y0 + 7 <= IMAGE_HEIGHT; y0 += 3)
  {
    for(uint32_t x0 = 0; x0 + 9 <= IMAGE_WIDTH; x0 += 4)
    {
      if(!checkWindow(path, full, x0, y0, x0 + 9, y0 + 7))
        ++failures;
    }
  }

  remove(path.c_str());
  grk_deinitialize();

  if(failures)
  {
    fprintf(stderr, "%u region decodes disagree with the whole image decode\n", failures);
    return 1;
  }
  printf("all region decodes match the whole image decode\n");
  return 0;
}
