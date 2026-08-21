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

// a codec that has already handed back the whole image has nothing left to
// decode, so a repeat call must not reach either decoder. the fast path names
// itself in the log when it runs and on stderr when it declines, so a second
// call that says nothing is a second call that decoded nothing.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define close _close
#else
#include <unistd.h>
#endif

#include "grok.h"

namespace
{
  const uint32_t IMAGE_WIDTH = 129;
  const uint32_t IMAGE_HEIGHT = 97;
  const uint16_t NUM_COMPONENTS = 3;
  const uint8_t PRECISION = 8;
  const char* MERCURY_DECODED_MARKER = "mercury fast path: decoded";
  const char* MERCURY_BAIL_MARKER = "mercury fastpath bail";
  const char* CAPTURED_STDERR_PATH = "repeat_decompress_test_stderr.txt";

  std::mutex logMutex;
  std::string logText;

  void appendLog(const char* message, void*)
  {
    std::lock_guard<std::mutex> lock(logMutex);
    logText += message;
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

  struct Plane
  {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<int32_t> samples;
  };

  struct Decoded
  {
    std::vector<Plane> planes;
  };

  int32_t sampleAt(const grk_image_comp& comp, uint64_t index)
  {
    if(comp.data_type == GRK_INT_16)
      return static_cast<int16_t*>(comp.data)[index];
    return static_cast<int32_t*>(comp.data)[index];
  }

  bool capture(grk_image* image, Decoded& out)
  {
    out.planes.resize(image->numcomps);
    for(uint16_t c = 0; c < image->numcomps; ++c)
    {
      const auto& source = image->comps[c];
      if(!source.data || source.w == 0 || source.h == 0)
      {
        fprintf(stdout, "component %u is empty: %ux%u\n", c, source.w, source.h);
        return false;
      }
      auto& plane = out.planes[c];
      plane.width = source.w;
      plane.height = source.h;
      plane.samples.resize((size_t)source.w * source.h);
      for(uint32_t y = 0; y < source.h; ++y)
        for(uint32_t x = 0; x < source.w; ++x)
          plane.samples[(size_t)y * source.w + x] =
              sampleAt(source, (uint64_t)y * source.stride + x);
    }
    return true;
  }

  bool same(const Decoded& first, const Decoded& second)
  {
    if(first.planes.size() != second.planes.size())
    {
      fprintf(stdout, "component count changed between calls: %zu then %zu\n", first.planes.size(),
              second.planes.size());
      return false;
    }
    for(size_t c = 0; c < first.planes.size(); ++c)
    {
      const auto& a = first.planes[c];
      const auto& b = second.planes[c];
      if(a.width != b.width || a.height != b.height)
      {
        fprintf(stdout, "component %zu changed size: %ux%u then %ux%u\n", c, a.width, a.height,
                b.width, b.height);
        return false;
      }
      for(size_t i = 0; i < a.samples.size(); ++i)
      {
        if(a.samples[i] != b.samples[i])
        {
          fprintf(stdout, "component %zu sample %zu is %d on the first call and %d on the second\n",
                  c, i, a.samples[i], b.samples[i]);
          return false;
        }
      }
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
          data[(size_t)y * stride + x] = (int32_t)((x * 7 + y * 13 + c * 41) & 0xFF);
    }
    return image;
  }

  bool compress(const std::string& path)
  {
    grk_image* image = makeImage();
    if(!image)
      return false;

    grk_cparameters parameters = {};
    grk_compress_set_default_params(&parameters);
    parameters.cod_format = GRK_FMT_J2K;
    parameters.irreversible = false;

    grk_stream_params streamParams = {};
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
    bool ok = false;
    if(codec)
    {
      ok = grk_compress(codec, nullptr) != 0;
      grk_object_unref(codec);
    }
    grk_object_unref(&image->obj);
    return ok;
  }

  // the fast path only names its refusals on stderr, so the second call runs
  // with stderr pointed at a file the test can read back
  class StderrCapture
  {
  public:
    bool begin(void)
    {
      fflush(stderr);
      saved_ = dup(fileno(stderr));
      if(saved_ < 0)
        return false;
      file_ = fopen(CAPTURED_STDERR_PATH, "w+");
      if(!file_)
        return false;
      return dup2(fileno(file_), fileno(stderr)) >= 0;
    }

    std::string end(void)
    {
      fflush(stderr);
      dup2(saved_, fileno(stderr));
      close(saved_);
      std::string captured;
      rewind(file_);
      char buffer[1024];
      size_t read = 0;
      while((read = fread(buffer, 1, sizeof(buffer), file_)) > 0)
        captured.append(buffer, read);
      fclose(file_);
      remove(CAPTURED_STDERR_PATH);
      return captured;
    }

  private:
    int saved_ = -1;
    FILE* file_ = nullptr;
  };

  bool decodeTwice(const std::string& path)
  {
    grk_decompress_parameters params = {};
    grk_stream_params streamParams = {};
    streamParams.is_read_stream = true;
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    grk_object* codec = grk_decompress_init(&streamParams, &params);
    if(!codec)
    {
      fprintf(stdout, "grk_decompress_init failed\n");
      return false;
    }

    bool ok = false;
    grk_header_info headerInfo = {};
    Decoded first;
    Decoded second;
    clearLog();
    if(!grk_decompress_read_header(codec, &headerInfo))
      fprintf(stdout, "grk_decompress_read_header failed\n");
    else if(!grk_decompress(codec, nullptr))
      fprintf(stdout, "first grk_decompress failed\n");
    else if(!capture(grk_decompress_get_image(codec), first))
      fprintf(stdout, "the first call handed back no image\n");
    else if(takeLog().find(MERCURY_DECODED_MARKER) == std::string::npos)
      fprintf(stdout, "the first call did not run the fast path, so there is nothing to repeat\n");
    else
    {
      clearLog();
      StderrCapture capture_stderr;
      if(!capture_stderr.begin())
        fprintf(stdout, "cannot redirect stderr\n");
      else
      {
        bool decoded = grk_decompress(codec, nullptr);
        std::string captured = capture_stderr.end();
        std::string log = takeLog();
        if(!decoded)
          fprintf(stdout, "second grk_decompress failed\n");
        else if(captured.find(MERCURY_BAIL_MARKER) != std::string::npos)
          fprintf(stdout, "the second call entered the fast path again: %s", captured.c_str());
        else if(log.find(MERCURY_DECODED_MARKER) != std::string::npos)
          fprintf(stdout, "the second call decoded the stream again: %s", log.c_str());
        else if(!capture(grk_decompress_get_image(codec), second))
          fprintf(stdout, "the second call handed back no image\n");
        else
          ok = same(first, second);
      }
    }
    grk_object_unref(codec);
    return ok;
  }
} // namespace

int main(void)
{
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "1");
  _putenv_s("GRK_MERCURY_DEBUG", "1");
#else
  setenv("GRK_MERCURY", "1", 1);
  setenv("GRK_MERCURY_DEBUG", "1", 1);
#endif
  grk_initialize(nullptr, 0, nullptr);
  grk_msg_handlers handlers = {};
  handlers.info_callback = appendLog;
  handlers.warn_callback = appendLog;
  handlers.error_callback = appendLog;
  grk_set_msg_handlers(handlers);

  std::string path = "repeat_decompress_test.j2k";
  if(!compress(path))
  {
    fprintf(stdout, "could not build the source codestream\n");
    grk_deinitialize();
    return 1;
  }

  bool ok = decodeTwice(path);
  remove(path.c_str());
  grk_deinitialize();
  if(!ok)
    return 1;
  printf("a repeat decompress hands back the same image without decoding again\n");
  return 0;
}
