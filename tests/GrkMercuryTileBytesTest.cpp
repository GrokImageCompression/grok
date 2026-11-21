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

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
const uint32_t IMAGE_SIZE = 600;
const uint32_t TILE_SIZE = 128;
const char* MERCURY_ASYNC_MARKER = "mercury per-tile async path";

std::mutex logMutex;
std::string logText;

void appendLog(const char* message, void*)
{
  std::lock_guard<std::mutex> lock(logMutex);
  logText += message;
  logText += '\n';
}

bool logContains(const char* marker)
{
  std::lock_guard<std::mutex> lock(logMutex);
  return logText.find(marker) != std::string::npos;
}

void clearLog(void)
{
  std::lock_guard<std::mutex> lock(logMutex);
  logText.clear();
}

uint8_t expectedSample(uint32_t x, uint32_t y)
{
  uint32_t mixed = (x * 73856093u) ^ (y * 19349663u);
  mixed ^= mixed >> 13;
  mixed *= 1274126177u;
  mixed ^= mixed >> 16;
  return (uint8_t)mixed;
}

std::vector<uint8_t> compress(void)
{
  grk_image_comp params = {};
  params.dx = 1;
  params.dy = 1;
  params.w = IMAGE_SIZE;
  params.h = IMAGE_SIZE;
  params.prec = 8;
  grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
  if(!image)
    return {};
  auto& comp = image->comps[0];
  auto data = static_cast<int32_t*>(comp.data);
  for(uint32_t y = 0; y < comp.h; ++y)
    for(uint32_t x = 0; x < comp.w; ++x)
      data[(size_t)y * comp.stride + x] = expectedSample(x, y);

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.tile_size_on = true;
  parameters.t_width = TILE_SIZE;
  parameters.t_height = TILE_SIZE;
  parameters.cblockw_init = 16;
  parameters.cblockh_init = 16;
  parameters.enable_tile_part_generation = true;
  parameters.new_tile_part_progression_divider = 'R';
  std::vector<uint8_t> bytes((size_t)IMAGE_SIZE * IMAGE_SIZE * 4);
  grk_stream_params stream = {};
  stream.buf = bytes.data();
  stream.buf_len = bytes.size();
  grk_object* codec = grk_compress_init(&stream, &parameters, image);
  uint64_t length = codec ? grk_compress(codec, nullptr) : 0;
  if(codec)
    grk_object_unref(codec);
  grk_object_unref(&image->obj);
  bytes.resize(length);
  return bytes;
}

struct CallbackSource
{
  const std::vector<uint8_t>* bytes = nullptr;
  uint64_t offset = 0;
  std::atomic<uint64_t> reads{0};
};

size_t callbackRead(uint8_t* buffer, size_t numBytes, void* userData)
{
  auto* source = static_cast<CallbackSource*>(userData);
  source->reads++;
  uint64_t total = source->bytes->size();
  if(source->offset >= total)
    return 0;
  size_t toCopy = (size_t)std::min<uint64_t>(numBytes, total - source->offset);
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

bool swathsMatch(const char* label, grk_stream_params& stream)
{
  grk_decompress_parameters params = {};
  params.asynchronous = true;
  params.simulate_synchronous = true;
  params.core.tile_cache_strategy = GRK_TILE_CACHE_NONE;
  params.core.skip_allocate_composite = true;
  clearLog();
  grk_object* codec = grk_decompress_init(&stream, &params);
  grk_header_info header = {};
  if(!codec || !grk_decompress_read_header(codec, &header) ||
     !grk_decompress_update(&params, codec) || !grk_decompress(codec, nullptr))
  {
    fprintf(stderr, "%s: decoder setup failed\n", label);
    if(codec)
      grk_object_unref(codec);
    return false;
  }
  bool ok = true;
  std::vector<uint8_t> destination((size_t)IMAGE_SIZE * TILE_SIZE);
  for(uint32_t y0 = 0; ok && y0 < IMAGE_SIZE; y0 += TILE_SIZE)
  {
    grk_wait_swath swath = {};
    swath.x1 = IMAGE_SIZE;
    swath.y0 = y0;
    swath.y1 = std::min(y0 + TILE_SIZE, IMAGE_SIZE);
    grk_decompress_wait(codec, &swath);
    grk_swath_buffer buffer = {};
    buffer.data = destination.data();
    buffer.prec = 8;
    buffer.numcomps = 1;
    buffer.pixel_space = 1;
    buffer.line_space = IMAGE_SIZE;
    buffer.band_space = (int64_t)IMAGE_SIZE * TILE_SIZE;
    buffer.promote_alpha = -1;
    buffer.x1 = IMAGE_SIZE;
    buffer.y0 = swath.y0;
    buffer.y1 = swath.y1;
    grk_decompress_schedule_swath_copy(codec, &swath, &buffer);
    grk_decompress_wait_swath_copy(codec);
    for(uint32_t y = swath.y0; ok && y < swath.y1; ++y)
      for(uint32_t x = 0; ok && x < IMAGE_SIZE; ++x)
      {
        uint8_t sample = destination[(size_t)(y - swath.y0) * IMAGE_SIZE + x];
        if(sample != expectedSample(x, y))
        {
          fprintf(stderr, "%s: sample %u at x %u, y %u, expected %u\n", label, sample, x, y,
                  expectedSample(x, y));
          ok = false;
        }
      }
  }
  grk_object_unref(codec);
  if(ok && !logContains(MERCURY_ASYNC_MARKER))
  {
    fprintf(stderr, "%s: mercury fell back to the classic pipeline\n", label);
    ok = false;
  }
  return ok;
}
} // namespace

int main(void)
{
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "");
#else
  unsetenv("GRK_MERCURY");
#endif
  grk_msg_handlers handlers = {};
  handlers.info_callback = appendLog;
  handlers.warn_callback = appendLog;
  handlers.error_callback = appendLog;
  grk_set_msg_handlers(handlers);
  grk_initialize(nullptr, 0, nullptr);

  int result = 1;
  std::vector<uint8_t> bytes = compress();
  CallbackSource source;
  source.bytes = &bytes;
  grk_stream_params stream = {};
  stream.read_fn = callbackRead;
  stream.seek_fn = callbackSeek;
  stream.user_data = &source;
  stream.stream_len = bytes.size();
  if(bytes.empty())
    fprintf(stderr, "compress failed\n");
  else if(swathsMatch("callback input", stream))
  {
    // measured 327 reads with tile buffers and 2276 with one read per code block
    const uint64_t maxCallbackReads = 800;
    if(source.reads > maxCallbackReads)
      fprintf(stderr, "callback input: %llu reads, expected at most %llu\n",
              (unsigned long long)source.reads.load(), (unsigned long long)maxCallbackReads);
    else
      result = 0;
  }
  grk_deinitialize();
  return result;
}
