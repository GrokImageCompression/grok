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
// through mercury and match the classic pipeline bit for bit. A tile whose own
// list differs carries a POC in its first tile-part instead, which mercury
// rejects.

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
const uint8_t MARKER_SOD_LOW_BYTE = 0x93;

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
  uint32_t tileSize; // 0 for a single tile
  Volume volumes[MAX_VOLUMES];
  bool perTileLists = false;
  bool expectFastPath = true;
};

// odd tiles swap the two volumes' progression orders, so their packet order is
// their own while every packet is still covered
Volume volumeFor(const Config& config, uint32_t tile, uint32_t volumeIndex)
{
  Volume volume = config.volumes[volumeIndex];
  if(config.perTileLists && (tile & 1))
    volume.order = config.volumes[MAX_VOLUMES - 1 - volumeIndex].order;
  return volume;
}

uint32_t tileCount(const Config& config)
{
  if(config.tileSize == 0)
    return 1;
  uint32_t across = (IMAGE_WIDTH + config.tileSize - 1) / config.tileSize;
  uint32_t down = (IMAGE_HEIGHT + config.tileSize - 1) / config.tileSize;
  return across * down;
}

int32_t sourceSample(uint32_t x, uint32_t y, uint16_t c)
{
  return (int32_t)((x * 7 + y * 13 + c * 53 + ((x ^ y) & 31) * 3) & 0xFF);
}

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
        data[(size_t)y * stride + x] = sourceSample(x, y, c);
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
  if(config.tileSize)
  {
    parameters.tile_size_on = true;
    parameters.t_width = config.tileSize;
    parameters.t_height = config.tileSize;
  }
  // the entries are laid out volume-major so one tile's volumes are not
  // adjacent in the list, which the compressor has to cope with when it picks a
  // tile's entries by tileno
  uint32_t numTiles = tileCount(config);
  // numpocs is the entry count minus one
  parameters.numpocs = MAX_VOLUMES * numTiles - 1;
  for(uint32_t v = 0; v < MAX_VOLUMES; ++v)
  {
    for(uint32_t t = 0; t < numTiles; ++t)
    {
      const Volume vol = volumeFor(config, t, v);
      auto& prog = parameters.progression[v * numTiles + t];
      prog.res_s = vol.res_s;
      prog.comp_s = vol.comp_s;
      prog.lay_e = vol.lay_e;
      prog.res_e = vol.res_e;
      prog.comp_e = vol.comp_e;
      prog.specified_compression_poc_prog = vol.order;
      prog.tileno = t;
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

uint32_t readPsot(const std::vector<uint8_t>& bytes, size_t sot)
{
  uint32_t psot = 0;
  for(size_t i = 0; i < 4; ++i)
    psot = psot << 8 | bytes[sot + 6 + i];
  return psot;
}

// one byte each for CSpoc and CEpoc, which is what three components give
const size_t POC_VOLUME_BYTES = 7;
const size_t SOT_SEGMENT_BYTES = 12;
const size_t SOT_ISOT_OFFSET = 4;
const size_t SOT_TPSOT_OFFSET = 10;

bool pocEncodesTileList(const Config& config, uint32_t tile, const std::vector<uint8_t>& bytes,
                        size_t poc)
{
  size_t segment = 2 + ((size_t)bytes[poc + 2] << 8 | bytes[poc + 3]);
  size_t expected = 4 + POC_VOLUME_BYTES * MAX_VOLUMES;
  if(segment != expected || poc + segment > bytes.size())
  {
    fprintf(stderr, "%s: tile %u POC segment is %zu bytes, expected %zu\n", config.name, tile,
            segment, expected);
    return false;
  }
  for(uint32_t v = 0; v < MAX_VOLUMES; ++v)
  {
    const uint8_t* entry = bytes.data() + poc + 4 + POC_VOLUME_BYTES * v;
    Volume want = volumeFor(config, tile, v);
    Volume got;
    got.res_s = entry[0];
    got.comp_s = entry[1];
    got.lay_e = (uint16_t)((uint16_t)entry[2] << 8 | entry[3]);
    got.res_e = entry[4];
    got.comp_e = entry[5];
    got.order = (GRK_PROG_ORDER)entry[6];
    if(got.res_s != want.res_s || got.comp_s != want.comp_s || got.lay_e != want.lay_e ||
       got.res_e != want.res_e || got.comp_e != want.comp_e || got.order != want.order)
    {
      fprintf(stderr,
              "%s: tile %u POC volume %u is res %u..%u comp %u..%u layers %u order %d, "
              "expected res %u..%u comp %u..%u layers %u order %d\n",
              config.name, tile, v, got.res_s, got.res_e, got.comp_s, got.comp_e, got.lay_e,
              (int)got.order, want.res_s, want.res_e, want.comp_s, want.comp_e, want.lay_e,
              (int)want.order);
      return false;
    }
  }
  return true;
}

// The main header carries tile 0's volume list and applies to every tile, so a
// tile-part POC belongs only to a tile whose own list differs.
bool checkPocPlacement(const Config& config, const std::string& path)
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
  if(!pocEncodesTileList(config, 0, bytes, mainPoc))
    return false;
  while(sot + SOT_SEGMENT_BYTES <= bytes.size() && bytes[sot] == MARKER_HIGH_BYTE &&
        bytes[sot + 1] == MARKER_SOT_LOW_BYTE)
  {
    uint32_t psot = readPsot(bytes, sot);
    if(psot == 0)
      break;
    uint32_t tile =
        (uint32_t)((uint32_t)bytes[sot + SOT_ISOT_OFFSET] << 8 | bytes[sot + SOT_ISOT_OFFSET + 1]);
    uint8_t tilePart = bytes[sot + SOT_TPSOT_OFFSET];
    // the tile-part header runs from the SOT segment to the SOD marker
    size_t sod = findMarker(bytes, sot, MARKER_SOD_LOW_BYTE);
    if(sod == bytes.size())
    {
      fprintf(stderr, "%s: tile %u tile-part %u has no SOD marker\n", config.name, tile, tilePart);
      return false;
    }
    size_t tilePoc = findMarker(bytes, sot, MARKER_POC_LOW_BYTE);
    bool carriesPoc = tilePoc < sod;
    bool needsPoc = tilePart == 0 && config.perTileLists && (tile & 1);
    if(carriesPoc != needsPoc)
    {
      fprintf(stderr, "%s: tile %u tile-part %u %s POC marker\n", config.name, tile, tilePart,
              carriesPoc ? "carries an unwanted" : "is missing its");
      return false;
    }
    if(carriesPoc && !pocEncodesTileList(config, tile, bytes, tilePoc))
      return false;
    sot += psot;
  }
  return true;
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

bool matchesSource(const Config& config, const Decoded& decoded)
{
  for(uint16_t c = 0; c < decoded.numcomps; ++c)
  {
    const auto& comp = decoded.comps[c];
    for(uint32_t y = 0; y < comp.h; ++y)
      for(uint32_t x = 0; x < comp.w; ++x)
      {
        int32_t got = comp.samples[(size_t)y * comp.w + x];
        if(got != sourceSample(x, y, c))
        {
          fprintf(stderr,
                  "%s: classic decode differs from the source at component %u (x %u, y %u): "
                  "%d vs %d\n",
                  config.name, c, x, y, got, sourceSample(x, y, c));
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
  if(checkPocPlacement(config, path) && decode(config, path, false, classic) &&
     decode(config, path, true, mercury))
  {
    std::string log = takeLog();
    bool fastPath = log.find(MERCURY_SUCCESS_MARKER) != std::string::npos;
    bool rejected = log.find(MERCURY_REJECT_MARKER) != std::string::npos;
    if(fastPath != config.expectFastPath || rejected == config.expectFastPath)
      fprintf(stderr, "%s: mercury %s, expected it to %s.\ncaptured log:\n%s\n", config.name,
              fastPath ? "took the fast path"
                       : (rejected ? "rejected the plan" : "fell back without a reason"),
              config.expectFastPath ? "decode" : "reject the tile-part POC", log.c_str());
    else
      ok = matchesSource(config, classic) && sameImage(config, classic, mercury);
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
      // the first volume stops two layers in, so the second revisits those
      // precincts for their last layer after skipping the included ones
      {"partial_layers",
       GRK_LRCP,
       0,
       0,
       {{0, 0, 2, 2, NUM_COMPONENTS, GRK_RLCP},
        {0, 0, NUM_LAYERS, NUM_RESOLUTIONS, NUM_COMPONENTS, GRK_CPRL}}},
      // six tiles, each with its own two entries in the progression list
      {"tiled_volumes",
       GRK_LRCP,
       0,
       32,
       {{0, 0, 2, 2, NUM_COMPONENTS, GRK_RLCP},
        {0, 0, NUM_LAYERS, NUM_RESOLUTIONS, NUM_COMPONENTS, GRK_CPRL}}},
      // two orders, and the low resolutions the second volume must skip
      {"two_volumes",
       GRK_LRCP,
       0,
       0,
       {{0, 0, NUM_LAYERS, 2, NUM_COMPONENTS, GRK_RLCP},
        {0, 0, NUM_LAYERS, NUM_RESOLUTIONS, NUM_COMPONENTS, GRK_CPRL}}},
      // the first volume already covers everything, so the second contributes
      // no packet at all and the POC just replaces the COD order
      {"order_replaces_cod",
       GRK_LRCP,
       0,
       0,
       {{0, 0, NUM_LAYERS, NUM_RESOLUTIONS, NUM_COMPONENTS, GRK_CPRL},
        {0, 0, NUM_LAYERS, 2, NUM_COMPONENTS, GRK_LRCP}}},
      // the first volume takes component 0 alone, so the second has to skip a
      // component's worth of packets scattered through its own walk
      {"component_range",
       GRK_RLCP,
       0,
       0,
       {{0, 0, NUM_LAYERS, NUM_RESOLUTIONS, 1, GRK_CPRL},
        {0, 0, NUM_LAYERS, NUM_RESOLUTIONS, NUM_COMPONENTS, GRK_LRCP}}},
      // 16x16 precincts give every resolution several precinct positions,
      // which is what the position-ordered volumes key on
      {"position_orders",
       GRK_RLCP,
       16,
       0,
       {{0, 0, NUM_LAYERS, 2, NUM_COMPONENTS, GRK_RPCL},
        {0, 0, NUM_LAYERS, NUM_RESOLUTIONS, NUM_COMPONENTS, GRK_PCRL}}},
      // odd tiles get their own volume orders, so they carry a POC in their
      // first tile-part and mercury has to bail
      {"per_tile_lists",
       GRK_LRCP,
       0,
       32,
       {{0, 0, 2, 2, NUM_COMPONENTS, GRK_RLCP},
        {0, 0, NUM_LAYERS, NUM_RESOLUTIONS, NUM_COMPONENTS, GRK_CPRL}},
       true,
       false},
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
