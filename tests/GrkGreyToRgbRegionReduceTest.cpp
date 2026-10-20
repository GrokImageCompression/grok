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
const uint32_t IMAGE_WIDTH = 256;
const uint32_t IMAGE_HEIGHT = 96;
const uint8_t NUM_RESOLUTIONS = 5;
const uint8_t REDUCE = 1;
const uint8_t PRECISION = 8;
const uint32_t WINDOW[4] = {100, 20, 160, 70};

struct DecodedPlane
{
  uint32_t x0 = 0;
  uint32_t y0 = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<int32_t> samples;
};

int32_t sampleAt(const grk_image_comp& comp, uint32_t x, uint32_t y)
{
  uint64_t index = (uint64_t)y * comp.stride + x;
  if(comp.data_type == GRK_INT_16)
    return static_cast<int16_t*>(comp.data)[index];
  return static_cast<int32_t*>(comp.data)[index];
}

DecodedPlane capturePlane(const grk_image_comp& comp)
{
  DecodedPlane plane;
  plane.x0 = comp.x0;
  plane.y0 = comp.y0;
  plane.width = comp.w;
  plane.height = comp.h;
  plane.samples.resize((size_t)comp.w * comp.h);
  for(uint32_t y = 0; y < comp.h; ++y)
    for(uint32_t x = 0; x < comp.w; ++x)
      plane.samples[(size_t)y * comp.w + x] = sampleAt(comp, x, y);
  return plane;
}

grk_image* makeImage(void)
{
  grk_image_comp params = {};
  params.dx = 1;
  params.dy = 1;
  params.w = IMAGE_WIDTH;
  params.h = IMAGE_HEIGHT;
  params.prec = PRECISION;
  params.sgnd = false;
  grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
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
  parameters.numresolution = NUM_RESOLUTIONS;

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

bool decodeWholeGrey(const std::string& path, DecodedPlane& out)
{
  grk_decompress_parameters params = {};
  params.core.reduce = REDUCE;
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
    if(!image || image->numcomps != 1 || !image->comps[0].data)
      fprintf(stderr, "whole image decode did not return one grey component\n");
    else
    {
      out = capturePlane(image->comps[0]);
      ok = true;
    }
  }
  grk_object_unref(codec);
  return ok;
}

bool planesMatch(const grk_image* image)
{
  const auto& first = image->comps[0];
  for(uint16_t componentNumber = 1; componentNumber < 3; ++componentNumber)
  {
    const auto& component = image->comps[componentNumber];
    if(!component.data || component.w != first.w || component.h != first.h)
    {
      fprintf(stderr, "component %u is %ux%u, first component is %ux%u\n", componentNumber,
              component.w, component.h, first.w, first.h);
      return false;
    }
    for(uint32_t y = 0; y < first.h; ++y)
    {
      for(uint32_t x = 0; x < first.w; ++x)
      {
        int32_t got = sampleAt(component, x, y);
        int32_t expected = sampleAt(first, x, y);
        if(got != expected)
        {
          fprintf(stderr, "component %u sample (%u,%u) is %d, first component has %d\n",
                  componentNumber, x, y, got, expected);
          return false;
        }
      }
    }
  }
  return true;
}

bool decodeWindowRgb(const std::string& path, DecodedPlane& out)
{
  grk_decompress_parameters params = {};
  params.core.reduce = REDUCE;
  params.dw_x0 = WINDOW[0];
  params.dw_y0 = WINDOW[1];
  params.dw_x1 = WINDOW[2];
  params.dw_y1 = WINDOW[3];
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
  headerInfo.force_rgb = true;
  if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "grk_decompress_read_header failed\n");
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "grk_decompress failed\n");
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!image || image->numcomps != 3)
      fprintf(stderr, "force_rgb window decode returned %u components, expected 3\n",
              image ? image->numcomps : 0);
    else if(planesMatch(image))
    {
      out = capturePlane(image->comps[0]);
      ok = true;
    }
  }
  grk_object_unref(codec);
  return ok;
}

bool sameAsCrop(const DecodedPlane& whole, const DecodedPlane& window)
{
  uint32_t offsetX = window.x0 - whole.x0;
  uint32_t offsetY = window.y0 - whole.y0;
  if(window.width == 0 || window.height == 0 || window.width >= whole.width ||
     window.height >= whole.height || offsetX + window.width > whole.width ||
     offsetY + window.height > whole.height)
  {
    fprintf(stderr, "window (%u,%u) %ux%u is not a strict sub-region of the whole decode %ux%u\n",
            window.x0, window.y0, window.width, window.height, whole.width, whole.height);
    return false;
  }
  for(uint32_t y = 0; y < window.height; ++y)
  {
    for(uint32_t x = 0; x < window.width; ++x)
    {
      int32_t got = window.samples[(size_t)y * window.width + x];
      int32_t expected = whole.samples[(size_t)(offsetY + y) * whole.width + offsetX + x];
      if(got != expected)
      {
        fprintf(stderr, "window sample (%u,%u) is %d, whole image decode has %d\n", x, y, got,
                expected);
        return false;
      }
    }
  }
  return true;
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

  std::string path = "grey_to_rgb_region_reduce_test.j2k";
  bool ok = compress(path);
  DecodedPlane whole;
  DecodedPlane window;
  ok = ok && decodeWholeGrey(path, whole);
  ok = ok && decodeWindowRgb(path, window);
  ok = ok && sameAsCrop(whole, window);

  remove(path.c_str());
  grk_deinitialize();

  if(!ok)
    return 1;
  printf("force_rgb window decode at reduce %u matches the grey whole image decode\n", REDUCE);
  return 0;
}
