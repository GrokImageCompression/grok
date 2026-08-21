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

// Cross-validates grok's PLT writer against mercury's PLT reader: a stream
// compressed with write_plt must decode bit-exact on the mercury fast path,
// with and without layer limits and reduce (where the planner hops skipped
// packets by their PLT lengths), and with GRK_RANDOM_ACCESS_PLT set (where it
// must ignore them).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
  // absence of this log line means the decode fell back to the classic pipeline
  const char* MERCURY_SUCCESS_MARKER = "mercury fast path: decoded";

  const uint32_t IMAGE_WIDTH = 61;
  const uint32_t IMAGE_HEIGHT = 69;
  const uint16_t NUM_COMPONENTS = 3;
  const uint8_t PRECISION = 8;
  const uint16_t NUM_LAYERS = 3;

  std::mutex logMutex;
  std::string logText;

  void appendLog(const char* msg, void*)
  {
    std::lock_guard<std::mutex> lock(logMutex);
    logText += msg;
    logText += '\n';
  }

  void clearLog(void)
  {
    std::lock_guard<std::mutex> lock(logMutex);
    logText.clear();
  }

  std::string takeLog(void)
  {
    std::lock_guard<std::mutex> lock(logMutex);
    return logText;
  }

  void useMercury(bool on)
  {
#if defined(_WIN32)
    _putenv_s("GRK_MERCURY", on ? "1" : "");
#else
    if(on)
      setenv("GRK_MERCURY", "1", 1);
    else
      unsetenv("GRK_MERCURY");
#endif
  }

  struct Component
  {
    uint32_t w = 0;
    uint32_t h = 0;
    uint8_t prec = 0;
    bool sgnd = false;
    std::vector<int32_t> samples;
  };

  struct Decoded
  {
    uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    uint16_t numcomps = 0;
    std::vector<Component> comps;
  };

  struct Config
  {
    const char* name;
    uint16_t layersToDecompress;
    uint8_t reduce;
    bool disablePlt;
  };

  int32_t sampleAt(const grk_image_comp& comp, uint64_t index)
  {
    if(comp.data_type == GRK_INT_16)
      return static_cast<int16_t*>(comp.data)[index];
    return static_cast<int32_t*>(comp.data)[index];
  }

  bool capture(grk_image* image, Decoded& out)
  {
    out.x0 = image->x0;
    out.y0 = image->y0;
    out.x1 = image->x1;
    out.y1 = image->y1;
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
      dst.prec = src.prec;
      dst.sgnd = src.sgnd;
      dst.samples.resize((size_t)src.w * src.h);
      for(uint32_t y = 0; y < src.h; ++y)
        for(uint32_t x = 0; x < src.w; ++x)
          dst.samples[(size_t)y * src.w + x] = sampleAt(src, (uint64_t)y * src.stride + x);
    }
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
    grk_image* image = grk_image_new(NUM_COMPONENTS, params, GRK_CLRSPC_SRGB, true);
    if(!image)
      return nullptr;
    for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
    {
      auto* data = static_cast<int32_t*>(image->comps[c].data);
      if(!data)
      {
        grk_object_unref(&image->obj);
        return nullptr;
      }
      uint32_t stride = image->comps[c].stride;
      for(uint32_t y = 0; y < IMAGE_HEIGHT; ++y)
        for(uint32_t x = 0; x < IMAGE_WIDTH; ++x)
          data[(size_t)y * stride + x] =
              (int32_t)((x * 7 + y * 13 + c * 53 + ((x ^ y) & 31) * 3) & 0xFF);
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
    parameters.irreversible = false;
    parameters.write_plt = true;
    parameters.numlayers = NUM_LAYERS;
    // the last layer's rate 0 keeps the stream lossless overall
    parameters.allocation_by_rate_distortion = true;
    parameters.layer_rate[0] = 20.0;
    parameters.layer_rate[1] = 12.0;
    parameters.layer_rate[2] = 0.0;

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

  bool decode(const Config& config, const std::string& path, bool mercury, Decoded& out)
  {
    grk_decompress_parameters params = {};
    params.core.layers_to_decompress = config.layersToDecompress;
    params.core.reduce = config.reduce;
    if(config.disablePlt)
      params.core.disable_random_access_flags = GRK_RANDOM_ACCESS_PLT;
    grk_stream_params streamParams = {};
    streamParams.is_read_stream = true;
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    useMercury(mercury);
    clearLog();
    grk_object* codec = grk_decompress_init(&streamParams, &params);
    if(!codec)
    {
      fprintf(stderr, "%s: grk_decompress_init failed\n", config.name);
      return false;
    }
    bool ok = false;
    grk_header_info headerInfo = {};
    if(!grk_decompress_read_header(codec, &headerInfo))
      fprintf(stderr, "%s: grk_decompress_read_header failed\n", config.name);
    else if(!grk_decompress(codec, nullptr))
      fprintf(stderr, "%s: grk_decompress failed\n", config.name);
    else
    {
      grk_image* image = grk_decompress_get_image(codec);
      if(!image)
        fprintf(stderr, "%s: grk_decompress_get_image returned null\n", config.name);
      else
        ok = capture(image, out);
    }
    grk_object_unref(codec);
    return ok;
  }

  bool sameImage(const Config& config, const Decoded& classic, const Decoded& mercury)
  {
    if(classic.x0 != mercury.x0 || classic.y0 != mercury.y0 || classic.x1 != mercury.x1 ||
       classic.y1 != mercury.y1 || classic.numcomps != mercury.numcomps)
    {
      fprintf(stderr, "%s: geometry mismatch: %u,%u..%u,%u/%u vs %u,%u..%u,%u/%u\n", config.name,
              classic.x0, classic.y0, classic.x1, classic.y1, classic.numcomps, mercury.x0,
              mercury.y0, mercury.x1, mercury.y1, mercury.numcomps);
      return false;
    }
    for(uint16_t c = 0; c < classic.numcomps; ++c)
    {
      const auto& a = classic.comps[c];
      const auto& b = mercury.comps[c];
      if(a.w != b.w || a.h != b.h || a.prec != b.prec || a.sgnd != b.sgnd)
      {
        fprintf(stderr, "%s: component %u layout mismatch: %ux%u prec %u vs %ux%u prec %u\n",
                config.name, c, a.w, a.h, a.prec, b.w, b.h, b.prec);
        return false;
      }
      for(size_t i = 0; i < a.samples.size(); ++i)
      {
        if(a.samples[i] != b.samples[i])
        {
          fprintf(stderr, "%s: component %u sample %zu (x %zu, y %zu) differs: %d vs %d\n",
                  config.name, c, i, i % a.w, i / a.w, a.samples[i], b.samples[i]);
          return false;
        }
      }
    }
    return true;
  }

  bool runConfig(const Config& config, const std::string& path)
  {
    Decoded classic;
    Decoded mercury;
    if(!decode(config, path, false, classic) || !decode(config, path, true, mercury))
      return false;
    std::string log = takeLog();
    if(log.find(MERCURY_SUCCESS_MARKER) == std::string::npos)
    {
      fprintf(stderr,
              "%s fell back to the classic pipeline: no \"%s\" in the log.\n"
              "captured log:\n%s\n",
              config.name, MERCURY_SUCCESS_MARKER, log.c_str());
      return false;
    }
    return sameImage(config, classic, mercury);
  }
} // namespace

int main(void)
{
  // GRK_MERCURY_DEBUG prints the bail reason to stderr when the fast path
  // falls back
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY_DEBUG", "1");
#else
  setenv("GRK_MERCURY_DEBUG", "1", 1);
#endif

  grk_msg_handlers handlers = {};
  handlers.info_callback = appendLog;
  handlers.warn_callback = appendLog;
  handlers.error_callback = appendLog;
  grk_set_msg_handlers(handlers);

  grk_initialize(nullptr, 0, nullptr);

  const Config configs[] = {
      {"all_layers", 0, 0, false},
      {"one_layer", 1, 0, false},
      {"two_layers", 2, 0, false},
      {"all_layers_reduced", 0, 2, false},
      {"one_layer_reduced", 1, 2, false},
      {"plt_disabled", 1, 0, true},
  };

  const std::string path = "mercury_plt_test.j2k";
  int result = 1;
  if(compress(path))
  {
    result = 0;
    for(const auto& config : configs)
    {
      if(runConfig(config, path))
        printf("%s passed\n", config.name);
      else
      {
        fprintf(stderr, "%s FAILED\n", config.name);
        result = 1;
      }
    }
  }
  remove(path.c_str());

  grk_deinitialize();
  return result;
}
