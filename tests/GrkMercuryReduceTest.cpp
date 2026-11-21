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
const char* MERCURY_BAND_MARKER = "mercury fast path: streamed";
// matches tests/mercury_ab_sweep.py: mercury's 9/7 is f32 where classic uses
// int16 fixed point, so a few LSBs of divergence are by design
const int32_t IRREVERSIBLE_PEAK_TOLERANCE = 4;

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
  uint8_t numresolutions = 0;
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

// Accumulates the rows the band callback delivers into whole component
// planes, so a band decode can be compared against a composite one.
struct BandAccum
{
  Decoded image;
  bool started = false;
  bool failed = false;
};

bool bandCallback(uint32_t yBegin, uint32_t yEnd, grk_image* image, void* userData)
{
  auto* accum = static_cast<BandAccum*>(userData);
  if(!accum->started)
  {
    accum->started = true;
    accum->image.x0 = image->x0;
    accum->image.y0 = image->y0;
    accum->image.x1 = image->x1;
    accum->image.y1 = image->y1;
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
    if(!src.data || src.w != dst.w)
    {
      accum->failed = true;
      return false;
    }
    for(uint32_t y = yBegin; y < yEnd; ++y)
      for(uint32_t x = 0; x < src.w; ++x)
        dst.samples.push_back(sampleAt(src, (uint64_t)y * src.stride + x));
    dst.h += yEnd - yBegin;
  }
  return true;
}

bool decode(const std::string& path, uint8_t reduce, bool mercury, Decoded& out,
            BandAccum* bands = nullptr)
{
  grk_decompress_parameters params;
  memset(&params, 0, sizeof(params));
  params.core.reduce = reduce;
  if(bands)
  {
    params.core.io_band_callback = bandCallback;
    params.core.io_band_user_data = bands;
  }

  grk_stream_params streamParams;
  memset(&streamParams, 0, sizeof(streamParams));
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  useMercury(mercury);
  clearLog();
  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "reduce %u: grk_decompress_init failed\n", reduce);
    return false;
  }
  bool ok = false;
  grk_header_info headerInfo;
  memset(&headerInfo, 0, sizeof(headerInfo));
  if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "reduce %u: grk_decompress_read_header failed\n", reduce);
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "reduce %u: grk_decompress failed\n", reduce);
  else if(bands)
  {
    ok = !bands->failed && bands->started;
    if(!ok)
      fprintf(stderr, "reduce %u: band callback delivered nothing usable\n", reduce);
    else
    {
      out = bands->image;
      out.numresolutions = headerInfo.numresolutions;
    }
  }
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!image)
      fprintf(stderr, "reduce %u: grk_decompress_get_image returned null\n", reduce);
    else if(capture(image, out))
    {
      out.numresolutions = headerInfo.numresolutions;
      ok = true;
    }
  }
  grk_object_unref(codec);
  return ok;
}

bool sameImage(const Decoded& classic, const Decoded& mercury, int32_t tolerance, uint8_t reduce)
{
  if(classic.x0 != mercury.x0 || classic.y0 != mercury.y0 || classic.x1 != mercury.x1 ||
     classic.y1 != mercury.y1 || classic.numcomps != mercury.numcomps)
  {
    fprintf(stderr, "reduce %u: geometry mismatch: %u,%u..%u,%u/%u vs %u,%u..%u,%u/%u\n", reduce,
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
      fprintf(stderr,
              "reduce %u: component %u layout mismatch: %ux%u prec %u sgnd %d vs "
              "%ux%u prec %u sgnd %d\n",
              reduce, c, a.w, a.h, a.prec, (int)a.sgnd, b.w, b.h, b.prec, (int)b.sgnd);
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
                "reduce %u: component %u sample %zu (x %zu, y %zu) differs by %d "
                "(tolerance %d): %d vs %d\n",
                reduce, c, i, i % a.w, i / a.w, diff, tolerance, a.samples[i], b.samples[i]);
        return false;
      }
    }
  }
  return true;
}

bool tookFastPath(const std::string& log)
{
  return log.find(MERCURY_SUCCESS_MARKER) != std::string::npos;
}

// decode once with each pipeline and compare; mercury must own the decode
bool compareAtReduce(const std::string& path, uint8_t reduce, int32_t tolerance)
{
  Decoded classic;
  if(!decode(path, reduce, false, classic))
    return false;
  Decoded mercury;
  if(!decode(path, reduce, true, mercury))
    return false;
  std::string log = takeLog();
  if(!tookFastPath(log))
  {
    fprintf(stderr,
            "reduce %u fell back to the classic pipeline: no \"%s\" in the log.\n"
            "captured log:\n%s\n",
            reduce, MERCURY_SUCCESS_MARKER, log.c_str());
    return false;
  }
  return sameImage(classic, mercury, tolerance, reduce);
}

// The band-callback branch of the fast path derives its strip geometry from
// mercury's reported dims, so it needs its own reduced-decode check.
bool compareBandsAtReduce(const std::string& path, uint8_t reduce, int32_t tolerance)
{
  // compare against the classic COMPOSITE decode: that also pins the total
  // number of rows the band writer delivered
  Decoded classic;
  if(!decode(path, reduce, false, classic))
    return false;
  BandAccum mercuryBands;
  Decoded mercury;
  if(!decode(path, reduce, true, mercury, &mercuryBands))
    return false;
  std::string log = takeLog();
  if(log.find(MERCURY_BAND_MARKER) == std::string::npos)
  {
    fprintf(stderr,
            "reduce %u band mode fell back to the classic pipeline: no \"%s\" in the log.\n"
            "captured log:\n%s\n",
            reduce, MERCURY_BAND_MARKER, log.c_str());
    return false;
  }
  return sameImage(classic, mercury, tolerance, reduce);
}
} // namespace

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "usage: %s <codestream> [--irreversible]\n", argv[0]);
    return 1;
  }
  std::string path = argv[1];
  int32_t tolerance = 0;
  for(int i = 2; i < argc; ++i)
  {
    if(strcmp(argv[i], "--irreversible") == 0)
      tolerance = IRREVERSIBLE_PEAK_TOLERANCE;
  }

  // GRK_MERCURY_DEBUG prints the bail reason to stderr when the fast path
  // falls back
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY_DEBUG", "1");
#else
  setenv("GRK_MERCURY_DEBUG", "1", 1);
#endif

  grk_msg_handlers handlers;
  memset(&handlers, 0, sizeof(handlers));
  handlers.info_callback = appendLog;
  handlers.warn_callback = appendLog;
  handlers.error_callback = appendLog;
  grk_set_msg_handlers(handlers);

  grk_initialize(nullptr, 0, nullptr);

  int result = 0;
  Decoded probe;
  if(!decode(path, 0, false, probe))
  {
    fprintf(stderr, "could not decode %s\n", path.c_str());
    grk_deinitialize();
    return 1;
  }
  uint8_t numres = probe.numresolutions;
  if(numres < 3)
  {
    fprintf(stderr, "%s has only %u resolutions, need at least 3 to exercise reduce\n",
            path.c_str(), numres);
    grk_deinitialize();
    return 1;
  }

  for(uint8_t reduce = 0; reduce + 1 < numres && result == 0; ++reduce)
  {
    if(!compareAtReduce(path, reduce, tolerance))
      result = 1;
  }

  if(result == 0 && !compareBandsAtReduce(path, (uint8_t)(numres - 2), tolerance))
    result = 1;

  // one level must remain for mercury's synthesis chain, so the last reduce
  // belongs to classic
  if(result == 0)
  {
    Decoded lowest;
    if(!decode(path, (uint8_t)(numres - 1), true, lowest))
    {
      fprintf(stderr, "reduce %u: decode failed\n", numres - 1);
      result = 1;
    }
    else if(tookFastPath(takeLog()))
    {
      fprintf(stderr, "reduce %u: fast path ran but should have bailed to classic\n", numres - 1);
      result = 1;
    }
  }

  grk_deinitialize();
  if(result == 0)
    printf("mercury reduce test passed on %s (%u resolutions)\n", path.c_str(), numres);
  return result;
}
