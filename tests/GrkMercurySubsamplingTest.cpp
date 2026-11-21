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

// Components with their own xr_siz/yr_siz take the mercury fast path and land
// on the same per-component rectangles and samples the classic pipeline
// produces, through both the whole-composite sink and the band callback.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
const char* MERCURY_COMPOSITE_MARKER = "mercury fast path: decoded";
const char* MERCURY_BAND_MARKER = "mercury fast path: streamed";
const char* MERCURY_PLAN_REJECTED_MARKER = "mercury fast path: plan rejected";

const uint16_t MAX_COMPONENTS = 3;
const uint8_t PRECISION = 8;

struct Config
{
  const char* name;
  uint16_t numcomps;
  uint32_t width;
  uint32_t height;
  uint8_t dx[MAX_COMPONENTS];
  uint8_t dy[MAX_COMPONENTS];
  uint8_t mct;
  uint8_t reduce;
  // decode window in canvas coordinates; all zero decodes everything
  uint32_t wx0, wy0, wx1, wy1;
  // 0 leaves the image untiled
  uint32_t tileWidth, tileHeight;
  bool bandMode;
};

const Config CONFIGS[] = {
    {"yuv420", 3, 61, 69, {1, 2, 2}, {1, 2, 2}, 0, 0, 0, 0, 0, 0, 0, 0, false},
    {"yuv420_reduce1", 3, 61, 69, {1, 2, 2}, {1, 2, 2}, 0, 1, 0, 0, 0, 0, 0, 0, false},
    {"yuv420_odd_window", 3, 61, 69, {1, 2, 2}, {1, 2, 2}, 0, 0, 5, 7, 53, 61, 0, 0, false},
    {"all_2x2_mct", 3, 61, 69, {2, 2, 2}, {2, 2, 2}, 1, 0, 0, 0, 0, 0, 0, 0, false},
    {"wide_4x1", 2, 61, 69, {4, 1, 1}, {1, 1, 1}, 0, 0, 0, 0, 0, 0, 0, 0, false},
    {"yuv420_tiled", 3, 61, 69, {1, 2, 2}, {1, 2, 2}, 0, 0, 0, 0, 0, 0, 32, 32, false},
    {"yuv420_bands", 3, 61, 69, {1, 2, 2}, {1, 2, 2}, 0, 0, 0, 0, 0, 0, 0, 0, true},
    {"yuv420_tiled_bands", 3, 61, 69, {1, 2, 2}, {1, 2, 2}, 0, 0, 0, 0, 0, 0, 32, 32, true},
};

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
  uint16_t numcomps = 0;
  std::vector<Component> comps;
};

int32_t sampleAt(const grk_image_comp& comp, uint64_t index)
{
  if(comp.data_type == GRK_INT_16)
    return static_cast<int16_t*>(comp.data)[index];
  return static_cast<int32_t*>(comp.data)[index];
}

bool capture(const Config& config, grk_image* image, Decoded& out)
{
  out.numcomps = image->numcomps;
  out.comps.resize(image->numcomps);
  for(uint16_t c = 0; c < image->numcomps; ++c)
  {
    const auto& src = image->comps[c];
    if(!src.data || src.w == 0 || src.h == 0)
    {
      fprintf(stderr, "%s: component %u is empty: %ux%u data %p\n", config.name, c, src.w, src.h,
              src.data);
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

// The band callback hands one tile row at a time: yEnd counts canvas rows,
// while each component reports its own y0 and height, so the rows have to be
// read per component.
struct BandAccum
{
  Decoded image;
  bool started = false;
  bool failed = false;
};

bool bandCallback(uint32_t, uint32_t, grk_image* image, void* userData)
{
  auto* accum = static_cast<BandAccum*>(userData);
  if(!accum->started)
  {
    accum->started = true;
    accum->image.numcomps = image->numcomps;
    accum->image.comps.resize(image->numcomps);
    for(uint16_t c = 0; c < image->numcomps; ++c)
    {
      auto& dst = accum->image.comps[c];
      dst.w = image->comps[c].w;
      dst.prec = image->comps[c].prec;
      dst.sgnd = image->comps[c].sgnd;
    }
  }
  for(uint16_t c = 0; c < image->numcomps; ++c)
  {
    const auto& src = image->comps[c];
    auto& dst = accum->image.comps[c];
    if(src.h && (!src.data || src.w != dst.w))
    {
      accum->failed = true;
      return false;
    }
    for(uint32_t y = 0; y < src.h; ++y)
      for(uint32_t x = 0; x < src.w; ++x)
        dst.samples.push_back(sampleAt(src, (uint64_t)y * src.stride + x));
    dst.h += src.h;
  }
  return true;
}

grk_image* makeImage(const Config& config)
{
  grk_image_comp params[MAX_COMPONENTS] = {};
  for(uint16_t c = 0; c < config.numcomps; ++c)
  {
    params[c].dx = config.dx[c];
    params[c].dy = config.dy[c];
    params[c].w = (config.width + config.dx[c] - 1) / config.dx[c];
    params[c].h = (config.height + config.dy[c] - 1) / config.dy[c];
    params[c].prec = PRECISION;
    params[c].sgnd = false;
  }
  auto space = config.numcomps >= 3 ? GRK_CLRSPC_SRGB : GRK_CLRSPC_UNKNOWN;
  grk_image* image = grk_image_new(config.numcomps, params, space, true);
  if(!image)
    return nullptr;
  // grk_image_new takes the canvas from component 0, which is the subsampled
  // rectangle here, not the reference grid
  image->x1 = config.width;
  image->y1 = config.height;
  for(uint16_t c = 0; c < config.numcomps; ++c)
  {
    auto* data = static_cast<int32_t*>(image->comps[c].data);
    if(!data)
    {
      grk_object_unref(&image->obj);
      return nullptr;
    }
    uint32_t stride = image->comps[c].stride;
    for(uint32_t y = 0; y < image->comps[c].h; ++y)
      for(uint32_t x = 0; x < image->comps[c].w; ++x)
        data[(size_t)y * stride + x] =
            (int32_t)((x * 7 + y * 13 + c * 53 + ((x ^ y) & 31) * 3) & 0xFF);
  }
  return image;
}

bool compress(const Config& config, const std::string& path)
{
  grk_image* image = makeImage(config);
  if(!image)
  {
    fprintf(stderr, "%s: could not build the source image\n", config.name);
    return false;
  }

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.irreversible = false;
  parameters.mct = config.mct;
  if(config.tileWidth)
  {
    parameters.tile_size_on = true;
    parameters.t_width = config.tileWidth;
    parameters.t_height = config.tileHeight;
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

bool decode(const Config& config, const std::string& path, bool mercury, bool bandMode,
            Decoded& out)
{
  grk_decompress_parameters params = {};
  params.core.reduce = config.reduce;
  params.dw_x0 = config.wx0;
  params.dw_y0 = config.wy0;
  params.dw_x1 = config.wx1;
  params.dw_y1 = config.wy1;
  BandAccum bands;
  if(bandMode)
  {
    params.core.io_band_callback = bandCallback;
    params.core.io_band_user_data = &bands;
  }

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
  else if(bandMode)
  {
    if(bands.failed || !bands.started)
      fprintf(stderr, "%s: the band callback delivered nothing usable\n", config.name);
    else
    {
      out = bands.image;
      ok = true;
    }
  }
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!image)
      fprintf(stderr, "%s: grk_decompress_get_image returned null\n", config.name);
    else
      ok = capture(config, image, out);
  }
  grk_object_unref(codec);
  return ok;
}

bool sameImage(const Config& config, const Decoded& classic, const Decoded& mercury)
{
  if(classic.numcomps != mercury.numcomps)
  {
    fprintf(stderr, "%s: component count differs: %u vs %u\n", config.name, classic.numcomps,
            mercury.numcomps);
    return false;
  }
  for(uint16_t c = 0; c < classic.numcomps; ++c)
  {
    const auto& a = classic.comps[c];
    const auto& b = mercury.comps[c];
    if(a.w != b.w || a.h != b.h || a.prec != b.prec || a.sgnd != b.sgnd)
    {
      fprintf(stderr, "%s: component %u layout differs: %ux%u prec %u vs %ux%u prec %u\n",
              config.name, c, a.w, a.h, a.prec, b.w, b.h, b.prec);
      return false;
    }
    for(size_t i = 0; i < a.samples.size(); ++i)
    {
      if(a.samples[i] != b.samples[i])
      {
        fprintf(stderr, "%s: component %u sample (x %zu, y %zu) differs: %d vs %d\n", config.name,
                c, i % a.w, i / a.w, a.samples[i], b.samples[i]);
        return false;
      }
    }
  }
  return true;
}

// The decoded components must be the subsampled, reduced rectangles the
// codestream describes, not the canvas rectangle.
bool expectedDims(const Config& config, const Decoded& decoded)
{
  uint32_t canvasX0 = config.wx1 > config.wx0 ? config.wx0 : 0;
  uint32_t canvasY0 = config.wy1 > config.wy0 ? config.wy0 : 0;
  uint32_t canvasX1 = config.wx1 > config.wx0 ? config.wx1 : config.width;
  uint32_t canvasY1 = config.wy1 > config.wy0 ? config.wy1 : config.height;
  auto ceilDiv = [](uint32_t a, uint32_t b) { return (a + b - 1) / b; };
  auto reduce = [&](uint32_t a) {
    uint32_t shift = config.reduce;
    return (a + (1u << shift) - 1) >> shift;
  };
  for(uint16_t c = 0; c < config.numcomps; ++c)
  {
    uint32_t x0 = reduce(ceilDiv(canvasX0, config.dx[c]));
    uint32_t x1 = reduce(ceilDiv(canvasX1, config.dx[c]));
    uint32_t y0 = reduce(ceilDiv(canvasY0, config.dy[c]));
    uint32_t y1 = reduce(ceilDiv(canvasY1, config.dy[c]));
    if(decoded.comps[c].w != x1 - x0 || decoded.comps[c].h != y1 - y0)
    {
      fprintf(stderr, "%s: component %u is %ux%u, expected %ux%u\n", config.name, c,
              decoded.comps[c].w, decoded.comps[c].h, x1 - x0, y1 - y0);
      return false;
    }
  }
  return true;
}

bool runConfig(const Config& config)
{
  std::string path = std::string("mercury_subsampling_") + config.name + ".j2k";
  if(!compress(config, path))
    return false;

  bool ok = false;
  Decoded classic;
  Decoded mercury;
  // the band writer's rows are compared against the whole composite, which is
  // the same contract classic's own band path keeps
  if(decode(config, path, false, false, classic) &&
     decode(config, path, true, config.bandMode, mercury))
  {
    std::string log = takeLog();
    const char* marker = config.bandMode ? MERCURY_BAND_MARKER : MERCURY_COMPOSITE_MARKER;
    if(log.find(MERCURY_PLAN_REJECTED_MARKER) != std::string::npos)
      fprintf(stderr, "%s: mercury rejected the plan.\ncaptured log:\n%s\n", config.name,
              log.c_str());
    else if(log.find(marker) == std::string::npos)
      fprintf(stderr, "%s: the fast path did not run.\ncaptured log:\n%s\n", config.name,
              log.c_str());
    else
      ok = expectedDims(config, classic) && sameImage(config, classic, mercury);
  }
  remove(path.c_str());
  return ok;
}

bool run(void)
{
  bool ok = true;
  for(const auto& config : CONFIGS)
  {
    if(runConfig(config))
      printf("  %s passed\n", config.name);
    else
    {
      fprintf(stderr, "  %s FAILED\n", config.name);
      ok = false;
    }
  }
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

  int result = 0;
  if(run())
    printf("subsampling passed\n");
  else
  {
    fprintf(stderr, "subsampling FAILED\n");
    result = 1;
  }

  grk_deinitialize();
  return result;
}
