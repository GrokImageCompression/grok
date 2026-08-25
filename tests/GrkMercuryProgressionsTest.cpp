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
    _putenv_s("GRK_MERCURY", on ? "1" : "0");
#else
    setenv("GRK_MERCURY", on ? "1" : "0", 1);
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
    GRK_PROG_ORDER order;
    uint32_t tileWidth;
    uint32_t tileHeight;
    uint16_t layers;
    // 'R', 'C' or 'L' to split tile-parts, 0 for a single tile-part
    char tilePartDivider;
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

  // structured enough that a mis-ordered packet parse shows up as a sample
  // difference rather than as flat noise
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

  bool compress(const Config& config, const std::string& path)
  {
    grk_image* image = makeImage();
    if(!image)
    {
      fprintf(stderr, "%s: could not build the source image\n", config.name);
      return false;
    }

    grk_cparameters parameters = {};
    grk_compress_set_default_params(&parameters);
    parameters.cod_format = GRK_FMT_J2K;
    parameters.prog_order = config.order;
    parameters.irreversible = false;
    parameters.tile_size_on = true;
    parameters.t_width = config.tileWidth;
    parameters.t_height = config.tileHeight;
    parameters.numlayers = config.layers;
    if(config.layers > 1)
    {
      // the last layer's rate 0 keeps the stream lossless overall
      parameters.allocation_by_rate_distortion = true;
      for(uint16_t i = 0; i + 1 < config.layers; ++i)
        parameters.layer_rate[i] = 20.0 - (double)i * 8.0;
      parameters.layer_rate[config.layers - 1] = 0.0;
    }
    if(config.tilePartDivider)
    {
      parameters.enable_tile_part_generation = true;
      parameters.new_tile_part_progression_divider = (uint8_t)config.tilePartDivider;
    }

    grk_stream_params streamParams = {};
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
    bool ok = false;
    if(!codec)
      fprintf(stderr, "%s: grk_compress_init failed\n", config.name);
    else
    {
      ok = grk_compress(codec, nullptr) != 0;
      if(!ok)
        fprintf(stderr, "%s: grk_compress failed\n", config.name);
      grk_object_unref(codec);
    }
    grk_object_unref(&image->obj);
    return ok;
  }

  bool decode(const Config& config, const std::string& path, bool mercury, Decoded& out)
  {
    grk_decompress_parameters params = {};
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
    else if(headerInfo.prog_order != config.order || headerInfo.t_width != config.tileWidth ||
            headerInfo.t_height != config.tileHeight || headerInfo.num_layers != config.layers)
      fprintf(stderr,
              "%s: the codestream does not carry the configuration: order %d tiles %ux%u "
              "layers %u\n",
              config.name, (int)headerInfo.prog_order, headerInfo.t_width, headerInfo.t_height,
              headerInfo.num_layers);
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

  bool runConfig(const Config& config)
  {
    std::string path = std::string("mercury_progressions_") + config.name + ".j2k";
    if(!compress(config, path))
      return false;

    bool ok = false;
    Decoded classic;
    Decoded mercury;
    if(decode(config, path, false, classic) && decode(config, path, true, mercury))
    {
      std::string log = takeLog();
      if(log.find(MERCURY_SUCCESS_MARKER) == std::string::npos)
        fprintf(stderr,
                "%s fell back to the classic pipeline: no \"%s\" in the log.\n"
                "captured log:\n%s\n",
                config.name, MERCURY_SUCCESS_MARKER, log.c_str());
      else
        ok = sameImage(config, classic, mercury);
    }
    remove(path.c_str());
    return ok;
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

  // 16x16 tiles land on the precinct grid of every resolution, 14x15 tiles
  // do not, which is what the position-ordered progressions key on
  const Config configs[] = {
      {"lrcp_aligned", GRK_LRCP, 16, 16, 1, 0},
      {"rlcp_aligned", GRK_RLCP, 16, 16, 1, 0},
      {"rpcl_aligned", GRK_RPCL, 16, 16, 1, 0},
      {"pcrl_aligned", GRK_PCRL, 16, 16, 1, 0},
      {"cprl_aligned", GRK_CPRL, 16, 16, 1, 0},
      {"lrcp_unaligned", GRK_LRCP, 14, 15, 1, 0},
      {"rlcp_unaligned", GRK_RLCP, 14, 15, 1, 0},
      {"rpcl_unaligned", GRK_RPCL, 14, 15, 1, 0},
      {"pcrl_unaligned", GRK_PCRL, 14, 15, 1, 0},
      {"cprl_unaligned", GRK_CPRL, 14, 15, 1, 0},
      {"pcrl_unaligned_res_tile_parts", GRK_PCRL, 14, 15, 1, 'R'},
      {"cprl_unaligned_res_tile_parts", GRK_CPRL, 14, 15, 1, 'R'},
      {"pcrl_unaligned_layers", GRK_PCRL, 14, 15, 3, 0},
  };

  int result = 0;
  for(const auto& config : configs)
  {
    if(runConfig(config))
      printf("%s passed\n", config.name);
    else
    {
      fprintf(stderr, "%s FAILED\n", config.name);
      result = 1;
    }
  }

  grk_deinitialize();
  return result;
}
