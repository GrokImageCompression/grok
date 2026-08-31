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

// Region (windowed) decode on the mercury fast path: every config compares a
// mercury decode against classic on the same window and must match (bit-exact
// for 5/3, peak tolerance for 9/7), and the fast path must own the decode —
// a fallback fails the test.

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
// matches tests/mercury_ab_sweep.py: mercury's 9/7 is f32 where classic uses
// fixed point, so a few LSBs of divergence are by design
const int32_t IRREVERSIBLE_PEAK_TOLERANCE = 4;

const uint32_t IMAGE_WIDTH = 200;
const uint32_t IMAGE_HEIGHT = 150;
const uint16_t NUM_COMPONENTS = 3;
const uint8_t CSTY_PRECINCTS = 0x01;
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
  grk_data_type dataType = GRK_INT_32;
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
  // 0 = single tile covering the image
  uint32_t tileWidth;
  uint32_t tileHeight;
  bool irreversible;
  bool writePlt;
  bool writeTlm;
  // decode window (canvas coordinates)
  uint32_t wx0, wy0, wx1, wy1;
  uint8_t reduce;
  uint16_t layersToDecompress;
  // uniform precinct size, 0 leaves the default (one precinct per band)
  uint32_t precinct;
  // code-block size, 0 leaves the default
  uint32_t codeBlock;
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
    dst.dataType = src.data_type;
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
  parameters.irreversible = config.irreversible;
  parameters.write_plt = config.writePlt;
  parameters.write_tlm = config.writeTlm;
  parameters.numlayers = 3;
  // the last layer's rate 0 keeps the stream lossless overall
  parameters.allocation_by_rate_distortion = true;
  parameters.layer_rate[0] = 20.0;
  parameters.layer_rate[1] = 12.0;
  parameters.layer_rate[2] = 0.0;
  if(config.tileWidth)
  {
    parameters.tile_size_on = true;
    parameters.t_width = config.tileWidth;
    parameters.t_height = config.tileHeight;
  }
  if(config.codeBlock)
  {
    parameters.cblockw_init = config.codeBlock;
    parameters.cblockh_init = config.codeBlock;
  }
  if(config.precinct)
  {
    parameters.csty |= CSTY_PRECINCTS;
    parameters.res_spec = parameters.numresolution;
    for(uint32_t r = 0; r < (uint32_t)parameters.res_spec; ++r)
    {
      parameters.prcw_init[r] = config.precinct;
      parameters.prch_init[r] = config.precinct;
    }
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
  params.core.reduce = config.reduce;
  params.core.layers_to_decompress = config.layersToDecompress;
  params.dw_x0 = config.wx0;
  params.dw_y0 = config.wy0;
  params.dw_x1 = config.wx1;
  params.dw_y1 = config.wy1;

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

bool sameImage(const Config& config, const Decoded& classic, const Decoded& mercury,
               int32_t tolerance)
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
    if(a.dataType != b.dataType)
    {
      fprintf(stderr, "%s: component %u data type mismatch: classic %d vs mercury %d\n",
              config.name, c, (int)a.dataType, (int)b.dataType);
      return false;
    }
    for(size_t i = 0; i < a.samples.size(); ++i)
    {
      int32_t diff = a.samples[i] - b.samples[i];
      if(diff < 0)
        diff = -diff;
      if(diff > tolerance)
      {
        fprintf(stderr,
                "%s: component %u sample %zu (x %zu, y %zu) differs by %d (tolerance %d): "
                "%d vs %d\n",
                config.name, c, i, i % a.w, i / a.w, diff, tolerance, a.samples[i], b.samples[i]);
        return false;
      }
    }
  }
  return true;
}

bool runConfig(const Config& config)
{
  std::string path = std::string("mercury_window_") + config.name + ".j2k";
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
    {
      ok = sameImage(config, classic, mercury,
                     config.irreversible ? IRREVERSIBLE_PEAK_TOLERANCE : 0);
      // prec 8 reversible is int16-eligible even for a window, on both pipelines
      if(ok && !config.irreversible && classic.comps[0].dataType != GRK_INT_16)
      {
        fprintf(stderr, "%s: expected int16 output, got data type %d\n", config.name,
                (int)classic.comps[0].dataType);
        ok = false;
      }
    }
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

  const Config configs[] = {
      // name, tileW, tileH, irrev, plt, tlm, wx0, wy0, wx1, wy1, reduce, layers,
      // precinct, codeBlock
      {"single_tile_interior", 0, 0, false, false, false, 50, 40, 150, 110, 0, 0, 0, 0},
      {"single_tile_odd_origin", 0, 0, false, false, false, 51, 41, 149, 109, 0, 0, 0, 0},
      {"tiled_interior", 64, 50, false, false, false, 80, 60, 150, 120, 0, 0, 0, 0},
      {"tiled_corner", 64, 50, false, false, false, 10, 10, 30, 30, 0, 0, 0, 0},
      {"tiled_edge", 64, 50, false, false, false, 100, 100, 200, 150, 0, 0, 0, 0},
      {"tiled_full_cover", 64, 50, false, false, false, 0, 0, 200, 150, 0, 0, 0, 0},
      {"tiled_reduced", 64, 50, false, false, false, 80, 60, 150, 120, 2, 0, 0, 0},
      {"tiled_layers", 64, 50, false, false, false, 80, 60, 150, 120, 0, 2, 0, 0},
      {"tiled_plt_tlm", 64, 50, false, true, true, 80, 60, 150, 120, 0, 0, 0, 0},
      {"tiled_plt_tlm_reduced_layers", 64, 50, false, true, true, 80, 60, 150, 120, 1, 2, 0, 0},
      {"tiled_irreversible", 64, 50, true, false, false, 80, 60, 150, 120, 0, 0, 0, 0},
      {"single_row_of_tiles", 64, 50, false, true, true, 10, 60, 190, 90, 0, 0, 0, 0},
      // several precincts per band, so a window keeps a block rectangle that
      // starts past the band's first block
      {"precincts_far_corner", 0, 0, false, false, false, 168, 120, 200, 150, 0, 0, 128, 0},
      {"precincts_far_corner_reduced", 0, 0, false, false, false, 168, 120, 200, 150, 1, 0, 128, 0},
      {"precincts_interior", 0, 0, false, false, false, 90, 70, 160, 130, 0, 0, 128, 0},
      {"precincts_irreversible", 0, 0, true, false, false, 168, 120, 200, 150, 0, 0, 128, 0},
      {"precincts_tiled", 128, 100, false, true, true, 168, 120, 200, 150, 0, 0, 128, 0},
      // 32x32 blocks put the far-corner window past the band's first block row
      {"precincts_small_blocks", 0, 0, false, false, false, 168, 136, 200, 150, 0, 0, 128, 32},
      {"precincts_small_blocks_layers", 0, 0, false, false, false, 168, 136, 200, 150, 0, 2, 128,
       32},
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
