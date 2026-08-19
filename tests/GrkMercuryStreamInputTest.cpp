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

  bool loggedMercurySuccess(const std::string& log)
  {
    return log.find(MERCURY_SUCCESS_MARKER) != std::string::npos;
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

  bool readFileBytes(const std::string& path, std::vector<uint8_t>& out)
  {
    FILE* file = fopen(path.c_str(), "rb");
    if(!file)
      return false;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    bool ok = size > 0;
    if(ok)
    {
      out.resize((size_t)size);
      ok = fread(out.data(), 1, out.size(), file) == out.size();
    }
    fclose(file);
    return ok;
  }

  struct CallbackSource
  {
    const std::vector<uint8_t>* bytes = nullptr;
    uint64_t offset = 0;
  };

  size_t callbackRead(uint8_t* buffer, size_t numBytes, void* userData)
  {
    auto* source = static_cast<CallbackSource*>(userData);
    uint64_t total = source->bytes->size();
    if(source->offset >= total)
      return 0;
    uint64_t available = total - source->offset;
    size_t toCopy = numBytes < available ? numBytes : (size_t)available;
    memcpy(buffer, source->bytes->data() + source->offset, toCopy);
    source->offset += toCopy;
    return toCopy;
  }

  bool callbackSeek(uint64_t offset, void* userData)
  {
    auto* source = static_cast<CallbackSource*>(userData);
    if(offset > source->bytes->size())
      return false;
    source->offset = offset;
    return true;
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

  enum class Mode
  {
    File,
    Buffer,
    Callback
  };

  const char* modeName(Mode mode)
  {
    switch(mode)
    {
      case Mode::File:
        return "file";
      case Mode::Buffer:
        return "buffer";
      case Mode::Callback:
        return "callback";
    }
    return "unknown";
  }

  bool decode(Mode mode, const std::string& path, std::vector<uint8_t>& bytes, Decoded& out)
  {
    grk_decompress_parameters params;
    memset(&params, 0, sizeof(params));

    grk_stream_params streamParams;
    memset(&streamParams, 0, sizeof(streamParams));
    streamParams.is_read_stream = true;

    CallbackSource source;
    switch(mode)
    {
      case Mode::File:
        snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());
        break;
      case Mode::Buffer:
        streamParams.buf = bytes.data();
        streamParams.buf_len = bytes.size();
        break;
      case Mode::Callback:
        source.bytes = &bytes;
        streamParams.read_fn = callbackRead;
        streamParams.seek_fn = callbackSeek;
        streamParams.user_data = &source;
        streamParams.stream_len = bytes.size();
        break;
    }

    clearLog();
    grk_object* codec = grk_decompress_init(&streamParams, &params);
    if(!codec)
    {
      fprintf(stderr, "%s mode: grk_decompress_init failed\n", modeName(mode));
      return false;
    }
    bool ok = false;
    grk_header_info headerInfo;
    memset(&headerInfo, 0, sizeof(headerInfo));
    if(!grk_decompress_read_header(codec, &headerInfo))
      fprintf(stderr, "%s mode: grk_decompress_read_header failed\n", modeName(mode));
    else if(!grk_decompress(codec, nullptr))
      fprintf(stderr, "%s mode: grk_decompress failed\n", modeName(mode));
    else
    {
      grk_image* image = grk_decompress_get_image(codec);
      if(!image)
        fprintf(stderr, "%s mode: grk_decompress_get_image returned null\n", modeName(mode));
      else
        ok = capture(image, out);
    }
    grk_object_unref(codec);
    return ok;
  }

  bool sameImage(const Decoded& reference, const Decoded& other, const char* label)
  {
    if(reference.x0 != other.x0 || reference.y0 != other.y0 || reference.x1 != other.x1 ||
       reference.y1 != other.y1 || reference.numcomps != other.numcomps)
    {
      fprintf(stderr, "%s: geometry mismatch: %ux%u..%ux%u/%u vs %ux%u..%ux%u/%u\n", label,
              reference.x0, reference.y0, reference.x1, reference.y1, reference.numcomps, other.x0,
              other.y0, other.x1, other.y1, other.numcomps);
      return false;
    }
    for(uint16_t c = 0; c < reference.numcomps; ++c)
    {
      const auto& a = reference.comps[c];
      const auto& b = other.comps[c];
      if(a.w != b.w || a.h != b.h || a.prec != b.prec || a.sgnd != b.sgnd)
      {
        fprintf(stderr, "%s: component %u layout mismatch: %ux%u prec %u sgnd %d vs "
                        "%ux%u prec %u sgnd %d\n",
                label, c, a.w, a.h, a.prec, (int)a.sgnd, b.w, b.h, b.prec, (int)b.sgnd);
        return false;
      }
      for(size_t i = 0; i < a.samples.size(); ++i)
      {
        if(a.samples[i] != b.samples[i])
        {
          fprintf(stderr, "%s: component %u sample %zu (x %zu, y %zu) mismatch: %d vs %d\n", label,
                  c, i, i % a.w, i / a.w, a.samples[i], b.samples[i]);
          return false;
        }
      }
    }
    return true;
  }

  bool runMode(Mode mode, const std::string& path, std::vector<uint8_t>& bytes, Decoded& out)
  {
    if(!decode(mode, path, bytes, out))
      return false;
    std::string log = takeLog();
    if(!loggedMercurySuccess(log))
    {
      fprintf(stderr,
              "%s mode fell back to the classic pipeline: no \"%s\" in the log.\n"
              "captured log:\n%s\n",
              modeName(mode), MERCURY_SUCCESS_MARKER, log.c_str());
      return false;
    }
    return true;
  }
} // namespace

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "usage: %s <reversible codestream>\n", argv[0]);
    return 1;
  }
  std::string path = argv[1];

  // GRK_MERCURY_DEBUG prints the bail reason to stderr when the fast path
  // falls back
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "1");
  _putenv_s("GRK_MERCURY_DEBUG", "1");
#else
  setenv("GRK_MERCURY", "1", 1);
  setenv("GRK_MERCURY_DEBUG", "1", 1);
#endif

  grk_msg_handlers handlers;
  memset(&handlers, 0, sizeof(handlers));
  handlers.info_callback = appendLog;
  handlers.warn_callback = appendLog;
  handlers.error_callback = appendLog;
  grk_set_msg_handlers(handlers);

  grk_initialize(nullptr, 0, nullptr);

  std::vector<uint8_t> bytes;
  if(!readFileBytes(path, bytes))
  {
    fprintf(stderr, "could not read %s\n", path.c_str());
    grk_deinitialize();
    return 1;
  }

  int result = 0;
  Decoded fromFile, fromBuffer, fromCallback;
  if(!runMode(Mode::File, path, bytes, fromFile))
    result = 1;
  else if(!runMode(Mode::Buffer, path, bytes, fromBuffer))
    result = 1;
  else if(!runMode(Mode::Callback, path, bytes, fromCallback))
    result = 1;
  else if(!sameImage(fromFile, fromBuffer, "buffer vs file"))
    result = 1;
  else if(!sameImage(fromFile, fromCallback, "callback vs file"))
    result = 1;

  grk_deinitialize();
  if(result == 0)
    printf("mercury stream input test passed on %s\n", path.c_str());
  return result;
}
