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

// window decodes of tiled multi-layer streams with PLT markers, in every
// progression order, must match the full decode

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
const uint32_t IMAGE_WIDTH = 300;
const uint32_t IMAGE_HEIGHT = 220;
const uint32_t TILE_WIDTH = 128;
const uint32_t TILE_HEIGHT = 96;
const uint16_t NUM_COMPONENTS = 3;
const uint8_t PRECISION = 8;
const uint32_t PRECINCT = 32;
const uint32_t CODE_BLOCK = 16;
const uint16_t NUM_LAYERS = 3;
// Scod bit for explicit precinct sizes
const uint8_t CSTY_PRECINCTS = 0x01;

struct Window
{
  uint32_t x0, y0, x1, y1;
  uint8_t reduce;
};

struct Decoded
{
  std::vector<std::vector<int32_t>> comps;
  std::vector<uint32_t> x0, y0, w, h;
};

int32_t expectedSample(uint32_t x, uint32_t y, uint16_t c)
{
  return (int32_t)((x * 5 + y * 11 + c * 71 + ((x * y) & 63)) & 0xFF);
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
  }
  grk_image* image = grk_image_new(NUM_COMPONENTS, params, GRK_CLRSPC_SRGB, true);
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

bool compress(GRK_PROG_ORDER order, const std::string& path)
{
  grk_image* image = makeImage();
  if(!image)
    return false;
  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.prog_order = order;
  parameters.write_plt = true;
  parameters.tile_size_on = true;
  parameters.t_width = TILE_WIDTH;
  parameters.t_height = TILE_HEIGHT;
  parameters.cblockw_init = CODE_BLOCK;
  parameters.cblockh_init = CODE_BLOCK;
  parameters.csty |= CSTY_PRECINCTS;
  parameters.res_spec = parameters.numresolution;
  for(uint32_t r = 0; r < (uint32_t)parameters.res_spec; ++r)
  {
    parameters.prcw_init[r] = PRECINCT;
    parameters.prch_init[r] = PRECINCT;
  }
  // the last layer's rate 0 keeps the stream lossless overall
  parameters.numlayers = NUM_LAYERS;
  parameters.allocation_by_rate_distortion = true;
  parameters.layer_rate[0] = 20.0;
  parameters.layer_rate[1] = 12.0;
  parameters.layer_rate[2] = 0.0;

  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());
  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  bool ok = false;
  if(codec)
  {
    ok = grk_compress(codec, nullptr) != 0;
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);
  if(!ok)
    fprintf(stderr, "order %d: compress failed\n", (int)order);
  return ok;
}

bool decode(const std::string& path, const Window* window, Decoded& out)
{
  grk_decompress_parameters params = {};
  if(window)
  {
    params.dw_x0 = window->x0;
    params.dw_y0 = window->y0;
    params.dw_x1 = window->x1;
    params.dw_y1 = window->y1;
    params.core.reduce = window->reduce;
  }
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());
  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
    return false;
  bool ok = false;
  grk_header_info headerInfo = {};
  if(grk_decompress_read_header(codec, &headerInfo) && grk_decompress(codec, nullptr))
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(image && image->numcomps == NUM_COMPONENTS)
    {
      ok = true;
      out.comps.assign(NUM_COMPONENTS, {});
      out.x0.assign(NUM_COMPONENTS, 0);
      out.y0.assign(NUM_COMPONENTS, 0);
      out.w.assign(NUM_COMPONENTS, 0);
      out.h.assign(NUM_COMPONENTS, 0);
      for(uint16_t c = 0; c < NUM_COMPONENTS && ok; ++c)
      {
        const auto& comp = image->comps[c];
        if(!comp.data)
        {
          ok = false;
          break;
        }
        out.x0[c] = comp.x0;
        out.y0[c] = comp.y0;
        out.w[c] = comp.w;
        out.h[c] = comp.h;
        out.comps[c].resize((size_t)comp.w * comp.h);
        for(uint32_t y = 0; y < comp.h; ++y)
          for(uint32_t x = 0; x < comp.w; ++x)
          {
            size_t index = (size_t)y * comp.stride + x;
            out.comps[c][(size_t)y * comp.w + x] = comp.data_type == GRK_INT_16
                                                       ? static_cast<int16_t*>(comp.data)[index]
                                                       : static_cast<int32_t*>(comp.data)[index];
          }
      }
    }
  }
  grk_object_unref(codec);
  if(!ok)
    fprintf(stderr, "decode of %s failed\n", path.c_str());
  return ok;
}

bool windowMatches(const Decoded& full, const Decoded& windowed, const char* label)
{
  for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
  {
    for(uint32_t y = 0; y < windowed.h[c]; ++y)
    {
      for(uint32_t x = 0; x < windowed.w[c]; ++x)
      {
        uint32_t fullX = windowed.x0[c] - full.x0[c] + x;
        uint32_t fullY = windowed.y0[c] - full.y0[c] + y;
        if(fullX >= full.w[c] || fullY >= full.h[c])
        {
          fprintf(stderr, "%s: component %u window (%u,%u %ux%u) leaves the full image\n", label, c,
                  windowed.x0[c], windowed.y0[c], windowed.w[c], windowed.h[c]);
          return false;
        }
        int32_t got = windowed.comps[c][(size_t)y * windowed.w[c] + x];
        int32_t want = full.comps[c][(size_t)fullY * full.w[c] + fullX];
        if(got != want)
        {
          fprintf(stderr, "%s: component %u sample (%u,%u) is %d, full decode has %d\n", label, c,
                  windowed.x0[c] + x, windowed.y0[c] + y, got, want);
          return false;
        }
      }
    }
  }
  return true;
}

bool runOrder(GRK_PROG_ORDER order, const char* name)
{
  std::string path = std::string("plt_window_skip_") + name + ".j2k";
  if(!compress(order, path))
    return false;
  bool ok = false;
  Decoded full;
  Decoded reducedFull;
  Window fullReduced = {0, 0, IMAGE_WIDTH, IMAGE_HEIGHT, 1};
  if(decode(path, nullptr, full) && decode(path, &fullReduced, reducedFull))
  {
    ok = true;
    // windows inside one tile, across tile borders, at the image edges, and reduced
    const Window windows[] = {
        {10, 10, 30, 30, 0}, {140, 100, 250, 200, 0}, {64, 0, 300, 96, 0}, {200, 150, 300, 220, 0},
        {0, 0, 300, 220, 0}, {130, 90, 260, 210, 1},  {40, 40, 41, 41, 0}};
    for(const auto& window : windows)
    {
      Decoded windowed;
      char label[128];
      snprintf(label, sizeof(label), "%s window %u,%u,%u,%u reduce %u", name, window.x0, window.y0,
               window.x1, window.y1, window.reduce);
      if(!decode(path, &window, windowed) ||
         !windowMatches(window.reduce ? reducedFull : full, windowed, label))
        ok = false;
    }
  }
  remove(path.c_str());
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

  const struct
  {
    GRK_PROG_ORDER order;
    const char* name;
  } orders[] = {{GRK_LRCP, "lrcp"},
                {GRK_RLCP, "rlcp"},
                {GRK_RPCL, "rpcl"},
                {GRK_PCRL, "pcrl"},
                {GRK_CPRL, "cprl"}};
  int result = 0;
  for(const auto& entry : orders)
  {
    if(runOrder(entry.order, entry.name))
      printf("%s passed\n", entry.name);
    else
    {
      fprintf(stderr, "%s FAILED\n", entry.name);
      result = 1;
    }
  }
  grk_deinitialize();
  return result;
}
