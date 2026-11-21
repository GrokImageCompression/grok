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
const uint32_t TILE_SIZE = 256;
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

struct SampleFormat
{
  const char* name;
  uint8_t prec;
  bool sgnd;
};

int32_t expectedSample(const SampleFormat& format, uint32_t x, uint32_t y)
{
  uint32_t mixed = (x * 73856093u) ^ (y * 19349663u);
  mixed ^= mixed >> 13;
  mixed *= 1274126177u;
  mixed ^= mixed >> 16;
  int32_t value = (int32_t)(mixed & ((1u << format.prec) - 1));
  return format.sgnd ? value - (1 << (format.prec - 1)) : value;
}

std::vector<uint8_t> compress(const SampleFormat& format)
{
  grk_image_comp params = {};
  params.dx = 1;
  params.dy = 1;
  params.w = IMAGE_SIZE;
  params.h = IMAGE_SIZE;
  params.prec = format.prec;
  params.sgnd = format.sgnd;
  grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
  if(!image)
    return {};
  auto& comp = image->comps[0];
  auto data = static_cast<int32_t*>(comp.data);
  for(uint32_t y = 0; y < comp.h; ++y)
    for(uint32_t x = 0; x < comp.w; ++x)
      data[(size_t)y * comp.stride + x] = expectedSample(format, x, y);

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.tile_size_on = true;
  parameters.t_width = TILE_SIZE;
  parameters.t_height = TILE_SIZE;
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

grk_object* openSwathDecoder(std::vector<uint8_t>& bytes)
{
  grk_decompress_parameters params = {};
  params.asynchronous = true;
  params.simulate_synchronous = true;
  params.core.tile_cache_strategy = GRK_TILE_CACHE_NONE;
  params.core.skip_allocate_composite = true;
  grk_stream_params stream = {};
  stream.buf = bytes.data();
  stream.buf_len = bytes.size();
  clearLog();
  grk_object* codec = grk_decompress_init(&stream, &params);
  if(!codec)
    return nullptr;
  grk_header_info header = {};
  if(!grk_decompress_read_header(codec, &header) || !grk_decompress_update(&params, codec) ||
     !grk_decompress(codec, nullptr))
  {
    grk_object_unref(codec);
    return nullptr;
  }
  return codec;
}

int32_t destinationSample(const SampleFormat& format, const uint8_t* pixel)
{
  if(format.sgnd)
  {
    int16_t value;
    memcpy(&value, pixel, sizeof value);
    return value;
  }
  uint16_t value;
  memcpy(&value, pixel, sizeof value);
  return value;
}

bool copySwathAndCheck(const std::string& label, const SampleFormat& format, grk_object* codec,
                       grk_wait_swath& swath, uint32_t pixelSpace)
{
  const uint32_t rows = swath.y1 - swath.y0;
  const int64_t lineSpace = (int64_t)IMAGE_SIZE * pixelSpace;
  std::vector<uint8_t> destination((size_t)lineSpace * rows);
  grk_swath_buffer buffer = {};
  buffer.data = destination.data();
  buffer.prec = 16;
  buffer.sgnd = format.sgnd;
  buffer.numcomps = 1;
  buffer.pixel_space = pixelSpace;
  buffer.line_space = lineSpace;
  buffer.band_space = lineSpace * rows;
  buffer.promote_alpha = -1;
  buffer.x1 = IMAGE_SIZE;
  buffer.y0 = swath.y0;
  buffer.y1 = swath.y1;
  grk_decompress_schedule_swath_copy(codec, &swath, &buffer);
  grk_decompress_wait_swath_copy(codec);
  for(uint32_t y = swath.y0; y < swath.y1; ++y)
    for(uint32_t x = 0; x < IMAGE_SIZE; ++x)
    {
      const uint8_t* pixel =
          destination.data() + (size_t)(y - swath.y0) * lineSpace + (size_t)x * pixelSpace;
      if(destinationSample(format, pixel) != expectedSample(format, x, y))
      {
        fprintf(stderr, "%s, pixel space %u: swath sample %d at x %u, y %u, expected %d\n",
                label.c_str(), pixelSpace, destinationSample(format, pixel), x, y,
                expectedSample(format, x, y));
        return false;
      }
    }
  return true;
}

bool swathsMatch(const SampleFormat& format, std::vector<uint8_t>& bytes)
{
  std::string label = std::string(format.name) + " swaths";
  grk_object* codec = openSwathDecoder(bytes);
  if(!codec)
  {
    fprintf(stderr, "%s: decoder setup failed\n", label.c_str());
    return false;
  }
  bool ok = true;
  const uint32_t packed = 2;
  const uint32_t interleavedWithAnotherBand = 4;
  for(uint32_t y0 = 0; ok && y0 < IMAGE_SIZE; y0 += TILE_SIZE)
  {
    grk_wait_swath swath = {};
    swath.x1 = IMAGE_SIZE;
    swath.y0 = y0;
    swath.y1 = std::min(y0 + TILE_SIZE, IMAGE_SIZE);
    grk_decompress_wait(codec, &swath);
    ok = copySwathAndCheck(label, format, codec, swath, packed) &&
         copySwathAndCheck(label, format, codec, swath, interleavedWithAnotherBand);
  }
  grk_object_unref(codec);
  if(ok && !logContains(MERCURY_ASYNC_MARKER))
  {
    fprintf(stderr, "%s: mercury fell back to the classic pipeline\n", label.c_str());
    ok = false;
  }
  return ok;
}

bool tileImageMatches(const SampleFormat& format, std::vector<uint8_t>& bytes)
{
  std::string label = std::string(format.name) + " tile image";
  grk_object* codec = openSwathDecoder(bytes);
  if(!codec)
  {
    fprintf(stderr, "%s: decoder setup failed\n", label.c_str());
    return false;
  }
  const uint16_t lastTile = 8;
  grk_image* image = grk_decompress_get_tile_image(codec, lastTile, true);
  bool ok = image && image->numcomps == 1 && image->comps[0].data;
  if(!ok)
    fprintf(stderr, "%s: no tile image\n", label.c_str());
  else if(image->comps[0].data_type != GRK_INT_32)
  {
    fprintf(stderr, "%s: data type %d, classic returns int32\n", label.c_str(),
            (int)image->comps[0].data_type);
    ok = false;
  }
  for(uint32_t y = 0; ok && y < image->comps[0].h; ++y)
    for(uint32_t x = 0; ok && x < image->comps[0].w; ++x)
    {
      const auto& comp = image->comps[0];
      int32_t sample = static_cast<const int32_t*>(comp.data)[(size_t)y * comp.stride + x];
      int32_t expected = expectedSample(format, comp.x0 + x, comp.y0 + y);
      if(sample != expected)
      {
        fprintf(stderr, "%s: sample %d at x %u, y %u, expected %d\n", label.c_str(), sample,
                comp.x0 + x, comp.y0 + y, expected);
        ok = false;
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
#else
  setenv("GRK_MERCURY", "1", 1);
#endif
  grk_msg_handlers handlers = {};
  handlers.info_callback = appendLog;
  handlers.warn_callback = appendLog;
  handlers.error_callback = appendLog;
  grk_set_msg_handlers(handlers);
  grk_initialize(nullptr, 0, nullptr);
  const SampleFormat formats[] = {
      {"unsigned 16-bit", 16, false}, {"unsigned 15-bit", 15, false}, {"signed 16-bit", 16, true}};
  int result = 0;
  for(const auto& format : formats)
  {
    std::vector<uint8_t> bytes = compress(format);
    if(bytes.empty())
    {
      fprintf(stderr, "%s: compress failed\n", format.name);
      result = 1;
      continue;
    }
    if(!swathsMatch(format, bytes) || !tileImageMatches(format, bytes))
      result = 1;
  }
  grk_deinitialize();
  return result;
}
