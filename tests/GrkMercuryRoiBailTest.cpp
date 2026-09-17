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

// An ROI upshift (RGN marker) must send the stream to the classic pipeline:
// mercury is never launched, and the samples match a GRK_MERCURY=0 decode.

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
// presence of this log line means mercury was launched and rejected the plan
// itself, i.e. grok's eligibility check missed the stream
const char* MERCURY_PLAN_REJECTED_MARKER = "mercury fast path: plan rejected";

const char* TEST_NAME = "roi_bail";
const char* STREAM_PATH = "mercury_roi_bail.j2k";

const uint32_t IMAGE_WIDTH = 61;
const uint32_t IMAGE_HEIGHT = 69;
const uint16_t NUM_COMPONENTS = 3;
const uint8_t PRECISION = 8;

const int32_t ROI_COMPONENT = 1;
const uint32_t ROI_SHIFT = 8;

const uint8_t MARKER_HIGH_BYTE = 0xFF;
const uint8_t MARKER_RGN_LOW_BYTE = 0x5E;
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
    fprintf(stderr, "%s: could not build the source image\n", TEST_NAME);
    return false;
  }

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.irreversible = false;
  parameters.roi_compno = ROI_COMPONENT;
  parameters.roi_shift = ROI_SHIFT;

  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  bool ok = false;
  if(!codec)
    fprintf(stderr, "%s: grk_compress_init failed\n", TEST_NAME);
  else
  {
    ok = grk_compress(codec, nullptr) != 0;
    if(!ok)
      fprintf(stderr, "%s: grk_compress failed\n", TEST_NAME);
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);
  return ok;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
  FILE* file = fopen(path.c_str(), "rb");
  if(!file)
  {
    fprintf(stderr, "%s: could not open %s\n", TEST_NAME, path.c_str());
    return false;
  }
  uint8_t chunk[4096];
  size_t read = 0;
  while((read = fread(chunk, 1, sizeof(chunk), file)) > 0)
    out.insert(out.end(), chunk, chunk + read);
  fclose(file);
  return !out.empty();
}

// the main header ends at the first SOT, so an RGN there is the one the
// compressor wrote for the ROI component
bool mainHeaderHasRegionMarker(const std::string& path)
{
  std::vector<uint8_t> bytes;
  if(!readFile(path, bytes))
    return false;
  for(size_t i = 0; i + 1 < bytes.size(); ++i)
  {
    if(bytes[i] != MARKER_HIGH_BYTE)
      continue;
    if(bytes[i + 1] == MARKER_RGN_LOW_BYTE)
      return true;
    if(bytes[i + 1] == MARKER_SOT_LOW_BYTE)
      break;
  }
  fprintf(stderr, "%s: no RGN marker in the main header, the stream has no ROI upshift\n",
          TEST_NAME);
  return false;
}

bool decode(const std::string& path, bool mercury, Decoded& out)
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
    fprintf(stderr, "%s: grk_decompress_init failed\n", TEST_NAME);
    return false;
  }
  bool ok = false;
  grk_header_info headerInfo = {};
  if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "%s: grk_decompress_read_header failed\n", TEST_NAME);
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "%s: grk_decompress failed\n", TEST_NAME);
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!image)
      fprintf(stderr, "%s: grk_decompress_get_image returned null\n", TEST_NAME);
    else
      ok = capture(image, out);
  }
  grk_object_unref(codec);
  return ok;
}

bool sameImage(const Decoded& classic, const Decoded& mercury)
{
  if(classic.x0 != mercury.x0 || classic.y0 != mercury.y0 || classic.x1 != mercury.x1 ||
     classic.y1 != mercury.y1 || classic.numcomps != mercury.numcomps)
  {
    fprintf(stderr, "%s: geometry mismatch: %u,%u..%u,%u/%u vs %u,%u..%u,%u/%u\n", TEST_NAME,
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
              TEST_NAME, c, a.w, a.h, a.prec, b.w, b.h, b.prec);
      return false;
    }
    for(size_t i = 0; i < a.samples.size(); ++i)
    {
      if(a.samples[i] != b.samples[i])
      {
        fprintf(stderr, "%s: component %u sample %zu (x %zu, y %zu) differs: %d vs %d\n", TEST_NAME,
                c, i, i % a.w, i / a.w, a.samples[i], b.samples[i]);
        return false;
      }
    }
  }
  return true;
}

bool run(void)
{
  std::string path = STREAM_PATH;
  if(!compress(path))
    return false;

  bool ok = false;
  Decoded classic;
  Decoded mercury;
  if(mainHeaderHasRegionMarker(path) && decode(path, false, classic) && decode(path, true, mercury))
  {
    std::string log = takeLog();
    bool fastPath = log.find(MERCURY_SUCCESS_MARKER) != std::string::npos;
    bool planRejected = log.find(MERCURY_PLAN_REJECTED_MARKER) != std::string::npos;
    if(fastPath)
      fprintf(stderr, "%s: the fast path decoded an ROI stream.\ncaptured log:\n%s\n", TEST_NAME,
              log.c_str());
    else if(planRejected)
      fprintf(stderr,
              "%s: mercury was launched and rejected the plan itself; the eligibility "
              "check should have bailed on the RGN marker first.\ncaptured log:\n%s\n",
              TEST_NAME, log.c_str());
    else
      ok = sameImage(classic, mercury);
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

  int result = 0;
  if(run())
    printf("%s passed\n", TEST_NAME);
  else
  {
    fprintf(stderr, "%s FAILED\n", TEST_NAME);
    result = 1;
  }

  grk_deinitialize();
  return result;
}
