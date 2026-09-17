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

// A main-header POC replaces the COD progression with a list of volumes, each
// with its own order and its own layer, resolution and component ranges. The
// fast path has to follow that list packet for packet, so these streams decode
// through mercury and match the classic pipeline bit for bit.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
const char* MERCURY_SUCCESS_MARKER = "mercury fast path: decoded";
const char* MERCURY_REJECT_MARKER = "mercury fast path: plan rejected";

const uint32_t IMAGE_WIDTH = 61;
const uint32_t IMAGE_HEIGHT = 69;
const uint16_t NUM_COMPONENTS = 3;
const uint8_t PRECISION = 8;
const uint8_t NUM_RESOLUTIONS = 4;
const uint16_t NUM_LAYERS = 3;
const uint32_t MAX_VOLUMES = 2;

const uint8_t MARKER_HIGH_BYTE = 0xFF;
const uint8_t MARKER_POC_LOW_BYTE = 0x5F;
const uint8_t MARKER_SOT_LOW_BYTE = 0x90;

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

struct Volume
{
  uint8_t res_s;
  uint16_t comp_s;
  uint16_t lay_e;
  uint8_t res_e;
  uint16_t comp_e;
  GRK_PROG_ORDER order;
};

struct Config
{
  const char* name;
  GRK_PROG_ORDER codOrder;
  uint32_t precinctSize; // 0 for the default one precinct per resolution
  Volume volumes[MAX_VOLUMES];
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
  parameters.prog_order = config.codOrder;
  parameters.irreversible = false;
  parameters.numresolution = NUM_RESOLUTIONS;
  parameters.numlayers = NUM_LAYERS;
  // the last layer's rate 0 keeps the stream lossless overall
  parameters.allocation_by_rate_distortion = true;
  for(uint16_t i = 0; i + 1 < NUM_LAYERS; ++i)
    parameters.layer_rate[i] = 20.0 - (double)i * 8.0;
  parameters.layer_rate[NUM_LAYERS - 1] = 0.0;
  if(config.precinctSize)
  {
    parameters.csty |= 0x01;
    parameters.res_spec = NUM_RESOLUTIONS;
    for(uint8_t r = 0; r < NUM_RESOLUTIONS; ++r)
    {
      parameters.prcw_init[r] = config.precinctSize;
      parameters.prch_init[r] = config.precinctSize;
    }
  }
  // numpocs is the volume count minus one
  parameters.numpocs = MAX_VOLUMES - 1;
  for(uint32_t i = 0; i < MAX_VOLUMES; ++i)
  {
    const auto& vol = config.volumes[i];
    auto& prog = parameters.progression[i];
    prog.res_s = vol.res_s;
    prog.comp_s = vol.comp_s;
    prog.lay_e = vol.lay_e;
    prog.res_e = vol.res_e;
    prog.comp_e = vol.comp_e;
    prog.specified_compression_poc_prog = vol.order;
    prog.tileno = 0;
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

bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
  FILE* file = fopen(path.c_str(), "rb");
  if(!file)
    return false;
  uint8_t chunk[4096];
  size_t read = 0;
  while((read = fread(chunk, 1, sizeof(chunk), file)) > 0)
    out.insert(out.end(), chunk, chunk + read);
  fclose(file);
  return !out.empty();
}

size_t findMarker(const std::vector<uint8_t>& bytes, size_t from, uint8_t low)
{
  for(size_t i = from; i + 1 < bytes.size(); ++i)
    if(bytes[i] == MARKER_HIGH_BYTE && bytes[i + 1] == low)
      return i;
  return bytes.size();
}

// The compressor repeats the volume list in the first tile-part header, which
// mercury rejects; the conformance streams carry it in the main header only, so
// drop the copy. Also checks that the main header really got a POC.
bool keepOnlyTheMainHeaderPoc(const Config& config, const std::string& path)
{
  std::vector<uint8_t> bytes;
  if(!readFile(path, bytes))
    return false;
  size_t sot = findMarker(bytes, 0, MARKER_SOT_LOW_BYTE);
  size_t mainPoc = findMarker(bytes, 0, MARKER_POC_LOW_BYTE);
  if(sot == bytes.size() || mainPoc > sot)
  {
    fprintf(stderr, "%s: no POC marker in the main header\n", config.name);
    return false;
  }
  size_t tilePoc = findMarker(bytes, sot, MARKER_POC_LOW_BYTE);
  if(tilePoc == bytes.size())
    return true;

  size_t segment = 2 + ((size_t)bytes[tilePoc + 2] << 8 | bytes[tilePoc + 3]);
  uint32_t psot = 0;
  for(size_t i = 0; i < 4; ++i)
    psot = psot << 8 | bytes[sot + 6 + i];
  psot -= (uint32_t)segment;
  for(size_t i = 0; i < 4; ++i)
    bytes[sot + 6 + i] = (uint8_t)(psot >> (24 - 8 * i));
  bytes.erase(bytes.begin() + (long)tilePoc, bytes.begin() + (long)(tilePoc + segment));

  FILE* file = fopen(path.c_str(), "wb");
  if(!file)
    return false;
  bool ok = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
  fclose(file);
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
  else if(headerInfo.prog_order != config.codOrder || headerInfo.num_layers != NUM_LAYERS)
    fprintf(stderr, "%s: the codestream does not carry the configuration: order %d layers %u\n",
            config.name, (int)headerInfo.prog_order, headerInfo.num_layers);
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
  std::string path = std::string("mercury_poc_") + config.name + ".j2k";
  if(!compress(config, path))
    return false;

  bool ok = false;
  Decoded classic;
  Decoded mercury;
  if(keepOnlyTheMainHeaderPoc(config, path) && decode(config, path, false, classic) &&
     decode(config, path, true, mercury))
  {
    std::string log = takeLog();
    if(log.find(MERCURY_REJECT_MARKER) != std::string::npos)
      fprintf(stderr, "%s: mercury rejected the plan.\ncaptured log:\n%s\n", config.name,
              log.c_str());
    else if(log.find(MERCURY_SUCCESS_MARKER) == std::string::npos)
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

  // the compressor requires the volume list to cover every packet, so the last
  // volume of each config spans the whole image
  const Config configs[] = {
      // two orders, and the low resolutions the second volume must skip
      {"two_volumes",
       GRK_LRCP,
       0,
       {{0, 0, NUM_LAYERS, 2, NUM_COMPONENTS, GRK_RLCP},
        {0, 0, NUM_LAYERS, NUM_RESOLUTIONS, NUM_COMPONENTS, GRK_CPRL}}},
      // the first volume already covers everything, so the second contributes
      // no packet at all and the POC just replaces the COD order
      {"order_replaces_cod",
       GRK_LRCP,
       0,
       {{0, 0, NUM_LAYERS, NUM_RESOLUTIONS, NUM_COMPONENTS, GRK_CPRL},
        {0, 0, NUM_LAYERS, 2, NUM_COMPONENTS, GRK_LRCP}}},
      // the first volume takes component 0 alone, so the second has to skip a
      // component's worth of packets scattered through its own walk
      {"component_range",
       GRK_RLCP,
       0,
       {{0, 0, NUM_LAYERS, NUM_RESOLUTIONS, 1, GRK_CPRL},
        {0, 0, NUM_LAYERS, NUM_RESOLUTIONS, NUM_COMPONENTS, GRK_LRCP}}},
      // 16x16 precincts give every resolution several precinct positions,
      // which is what the position-ordered volumes key on
      {"position_orders",
       GRK_RLCP,
       16,
       {{0, 0, NUM_LAYERS, 2, NUM_COMPONENTS, GRK_RPCL},
        {0, 0, NUM_LAYERS, NUM_RESOLUTIONS, NUM_COMPONENTS, GRK_PCRL}}},
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
