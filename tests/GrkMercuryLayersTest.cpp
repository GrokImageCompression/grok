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
    uint16_t numlayers = 0;
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

  // one decode; layers 0 means every layer, matching grok's core parameter
  bool decode(const std::string& path, uint16_t layers, uint8_t reduce, bool mercury, Decoded& out)
  {
    grk_decompress_parameters params;
    memset(&params, 0, sizeof(params));
    params.core.layers_to_decompress = layers;
    params.core.reduce = reduce;

    grk_stream_params streamParams;
    memset(&streamParams, 0, sizeof(streamParams));
    streamParams.is_read_stream = true;
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    useMercury(mercury);
    clearLog();
    grk_object* codec = grk_decompress_init(&streamParams, &params);
    if(!codec)
    {
      fprintf(stderr, "layers %u reduce %u: grk_decompress_init failed\n", layers, reduce);
      return false;
    }
    bool ok = false;
    grk_header_info headerInfo;
    memset(&headerInfo, 0, sizeof(headerInfo));
    if(!grk_decompress_read_header(codec, &headerInfo))
      fprintf(stderr, "layers %u reduce %u: grk_decompress_read_header failed\n", layers, reduce);
    else if(!grk_decompress(codec, nullptr))
      fprintf(stderr, "layers %u reduce %u: grk_decompress failed\n", layers, reduce);
    else
    {
      grk_image* image = grk_decompress_get_image(codec);
      if(!image)
        fprintf(stderr, "layers %u reduce %u: grk_decompress_get_image returned null\n", layers,
                reduce);
      else if(capture(image, out))
      {
        out.numlayers = headerInfo.num_layers;
        out.numresolutions = headerInfo.numresolutions;
        ok = true;
      }
    }
    grk_object_unref(codec);
    return ok;
  }

  bool sameImage(const Decoded& classic, const Decoded& mercury, const char* what)
  {
    if(classic.x0 != mercury.x0 || classic.y0 != mercury.y0 || classic.x1 != mercury.x1 ||
       classic.y1 != mercury.y1 || classic.numcomps != mercury.numcomps)
    {
      fprintf(stderr, "%s: geometry mismatch: %u,%u..%u,%u/%u vs %u,%u..%u,%u/%u\n", what,
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
                "%s: component %u layout mismatch: %ux%u prec %u sgnd %d vs "
                "%ux%u prec %u sgnd %d\n",
                what, c, a.w, a.h, a.prec, (int)a.sgnd, b.w, b.h, b.prec, (int)b.sgnd);
        return false;
      }
      for(size_t i = 0; i < a.samples.size(); ++i)
      {
        if(a.samples[i] != b.samples[i])
        {
          fprintf(stderr, "%s: component %u sample %zu (x %zu, y %zu) differs: %d vs %d\n", what, c,
                  i, i % a.w, i / a.w, a.samples[i], b.samples[i]);
          return false;
        }
      }
    }
    return true;
  }

  bool sameSamples(const Decoded& a, const Decoded& b)
  {
    for(uint16_t c = 0; c < a.numcomps; ++c)
      if(a.comps[c].samples != b.comps[c].samples)
        return false;
    return true;
  }

  // decode once with each pipeline and compare; mercury must own its decode
  bool compare(const std::string& path, uint16_t layers, uint8_t reduce, Decoded& classicOut)
  {
    char what[64];
    snprintf(what, sizeof(what), "layers %u reduce %u", layers, reduce);
    if(!decode(path, layers, reduce, false, classicOut))
      return false;
    Decoded mercury;
    if(!decode(path, layers, reduce, true, mercury))
      return false;
    std::string log = takeLog();
    if(log.find(MERCURY_SUCCESS_MARKER) == std::string::npos)
    {
      fprintf(stderr,
              "%s fell back to the classic pipeline: no \"%s\" in the log.\n"
              "captured log:\n%s\n",
              what, MERCURY_SUCCESS_MARKER, log.c_str());
      return false;
    }
    return sameImage(classicOut, mercury, what);
  }
} // namespace

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "usage: %s <codestream>\n", argv[0]);
    return 1;
  }
  std::string path = argv[1];

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
  if(!decode(path, 0, 0, false, probe))
  {
    fprintf(stderr, "could not decode %s\n", path.c_str());
    grk_deinitialize();
    return 1;
  }
  uint16_t numlayers = probe.numlayers;
  uint8_t numres = probe.numresolutions;
  if(numlayers < 2)
  {
    fprintf(stderr, "%s has %u quality layer(s), need at least 2\n", path.c_str(), numlayers);
    grk_deinitialize();
    return 1;
  }

  Decoded firstLayerOnly;
  for(uint16_t layers = 1; layers <= numlayers && result == 0; ++layers)
  {
    Decoded classic;
    if(!compare(path, layers, 0, classic))
      result = 1;
    else if(layers == 1)
      firstLayerOnly = classic;
  }

  // 0 means every layer, as does a limit at or above the layer count
  for(uint16_t layers : {(uint16_t)0, (uint16_t)(numlayers + 1)})
  {
    if(result != 0)
      break;
    Decoded classic;
    if(!compare(path, layers, 0, classic))
      result = 1;
    // a limit that truncated nothing must not match the one-layer decode,
    // otherwise the comparisons above would pass with the limit ignored
    else if(sameSamples(classic, firstLayerOnly))
    {
      fprintf(stderr, "layers %u decodes the same samples as a single layer\n", layers);
      result = 1;
    }
  }

  // a layer limit and a reduce must compose
  if(result == 0 && numres >= 3)
  {
    Decoded classic;
    if(!compare(path, 1, (uint8_t)(numres - 2), classic))
      result = 1;
  }

  grk_deinitialize();
  if(result == 0)
    printf("mercury layers test passed on %s (%u layers, %u resolutions)\n", path.c_str(),
           numlayers, numres);
  return result;
}
