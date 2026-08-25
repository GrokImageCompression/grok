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
struct Component
{
  uint32_t w = 0;
  uint32_t h = 0;
  std::vector<int32_t> samples;
};

struct Decoded
{
  uint16_t numcomps = 0;
  uint16_t numlayers = 0;
  std::vector<Component> comps;
};

int32_t sampleAt(const grk_image_comp& comp, uint64_t index)
{
  if(comp.data_type == GRK_INT_16)
    return static_cast<int16_t*>(comp.data)[index];
  return static_cast<int32_t*>(comp.data)[index];
}

bool capture(grk_image* image, Decoded& out)
{
  out.numcomps = image->numcomps;
  out.comps.resize(image->numcomps);
  for(uint16_t c = 0; c < image->numcomps; ++c)
  {
    const auto& src = image->comps[c];
    if(!src.data || src.w == 0 || src.h == 0)
    {
      fprintf(stderr, "component %u is empty: %ux%u data %p\n", c, src.w, src.h, src.data);
      return false;
    }
    auto& dst = out.comps[c];
    dst.w = src.w;
    dst.h = src.h;
    dst.samples.resize((size_t)src.w * src.h);
    for(uint32_t y = 0; y < src.h; ++y)
      for(uint32_t x = 0; x < src.w; ++x)
        dst.samples[(size_t)y * src.w + x] = sampleAt(src, (uint64_t)y * src.stride + x);
  }
  return true;
}

void fillParams(grk_decompress_parameters& params, uint16_t layers, uint32_t cacheStrategy)
{
  memset(&params, 0, sizeof(params));
  params.core.layers_to_decompress = layers;
  params.core.tile_cache_strategy = cacheStrategy;
}

void fillStream(grk_stream_params& streamParams, const std::string& path)
{
  memset(&streamParams, 0, sizeof(streamParams));
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());
}

bool decodeFull(const std::string& path, uint16_t layers, Decoded& out)
{
  grk_decompress_parameters params;
  fillParams(params, layers, GRK_TILE_CACHE_IMAGE);
  grk_stream_params streamParams;
  fillStream(streamParams, path);

  auto codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "layers %u: grk_decompress_init failed\n", layers);
    return false;
  }
  bool ok = false;
  grk_header_info headerInfo;
  memset(&headerInfo, 0, sizeof(headerInfo));
  if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "layers %u: grk_decompress_read_header failed\n", layers);
  else if(!grk_decompress_tile(codec, 0))
    fprintf(stderr, "layers %u: grk_decompress_tile failed\n", layers);
  else
  {
    auto image = grk_decompress_get_tile_image(codec, 0, true);
    if(!image)
      fprintf(stderr, "layers %u: grk_decompress_get_tile_image returned null\n", layers);
    else if(capture(image, out))
    {
      out.numlayers = headerInfo.num_layers;
      ok = true;
    }
  }
  grk_object_unref(codec);

  return ok;
}

bool sameSamples(const Decoded& progressive, const Decoded& full, uint16_t layers)
{
  if(progressive.numcomps != full.numcomps)
  {
    fprintf(stderr, "layers %u: component count %u differs from full decode %u\n", layers,
            progressive.numcomps, full.numcomps);
    return false;
  }
  for(uint16_t c = 0; c < progressive.numcomps; ++c)
  {
    const auto& a = progressive.comps[c];
    const auto& b = full.comps[c];
    if(a.w != b.w || a.h != b.h)
    {
      fprintf(stderr, "layers %u: component %u is %ux%u, full decode is %ux%u\n", layers, c, a.w,
              a.h, b.w, b.h);
      return false;
    }
    for(size_t i = 0; i < a.samples.size(); ++i)
    {
      if(a.samples[i] != b.samples[i])
      {
        fprintf(stderr,
                "layers %u: component %u sample %zu (x %zu, y %zu) is %d, full decode gives %d\n",
                layers, c, i, i % a.w, i / a.w, a.samples[i], b.samples[i]);
        return false;
      }
    }
  }
  return true;
}

bool progressiveMatchesFull(const std::string& path, bool wholeImage)
{
  Decoded probe;
  if(!decodeFull(path, 1, probe))
  {
    fprintf(stderr, "could not decode %s\n", path.c_str());
    return false;
  }
  if(probe.numlayers < 2)
  {
    fprintf(stderr, "%s has %u layers, need at least 2\n", path.c_str(), probe.numlayers);
    return false;
  }

  grk_decompress_parameters params;
  fillParams(params, 1, GRK_TILE_CACHE_ALL);
  grk_stream_params streamParams;
  fillStream(streamParams, path);

  auto codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "differential: grk_decompress_init failed\n");
    return false;
  }

  bool ok = true;
  for(uint16_t layers = 1; ok && layers <= probe.numlayers; ++layers)
  {
    fillParams(params, layers, GRK_TILE_CACHE_ALL);
    if(layers > 1 && !grk_decompress_update(&params, codec))
    {
      fprintf(stderr, "layers %u: grk_decompress_update failed\n", layers);
      ok = false;
      break;
    }
    grk_header_info headerInfo;
    memset(&headerInfo, 0, sizeof(headerInfo));
    if(!grk_decompress_read_header(codec, &headerInfo))
    {
      fprintf(stderr, "layers %u: differential grk_decompress_read_header failed\n", layers);
      ok = false;
      break;
    }
    bool decoded = wholeImage ? grk_decompress(codec, nullptr) : grk_decompress_tile(codec, 0);
    if(!decoded)
    {
      fprintf(stderr, "layers %u: differential decode failed\n", layers);
      ok = false;
      break;
    }
    auto image = wholeImage ? grk_decompress_get_image(codec)
                            : grk_decompress_get_tile_image(codec, 0, true);
    Decoded progressive;
    if(!image || !capture(image, progressive))
    {
      fprintf(stderr, "layers %u: differential decode produced no image\n", layers);
      ok = false;
      break;
    }
    Decoded full;
    if(!decodeFull(path, layers, full))
    {
      ok = false;
      break;
    }
    ok = sameSamples(progressive, full, layers);
  }
  grk_object_unref(codec);

  return ok;
}
} // namespace

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "usage: %s <codestream>\n", argv[0]);
    return EXIT_FAILURE;
  }

  // mercury bails on differential decode
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "0");
#else
  setenv("GRK_MERCURY", "0", 1);
#endif

  grk_initialize(nullptr, 0, nullptr);
  // the whole-image route builds a composite, the tile route reads the cached tile
  bool ok = progressiveMatchesFull(argv[1], true) && progressiveMatchesFull(argv[1], false);
  grk_deinitialize();

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
