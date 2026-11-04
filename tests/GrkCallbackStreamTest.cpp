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
#include "NoiseSample.h"

namespace
{
const uint8_t PRECISION = 8;
const uint32_t SINGLE_TILE_SWATH_ROWS = 256;
// noise at this size compresses to more than grok's 512 KB initial stream buffer
const uint32_t CLASSIC_IMAGE_SIZE = 2048;
const uint32_t TILE_SIZE = 512;
// noise at this size compresses to more than twice mercury's 4 MB plan read
const uint32_t MERCURY_IMAGE_SIZE = 4096;
const char* MERCURY_SYNC_MARKER = "mercury fast path: decoded";
const char* MERCURY_ASYNC_MARKER = "mercury per-tile async path";

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

bool logContains(const char* marker)
{
  std::lock_guard<std::mutex> lock(logMutex);
  return logText.find(marker) != std::string::npos;
}

void setMercury(const char* value)
{
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", value ? value : "");
#else
  if(value)
    setenv("GRK_MERCURY", value, 1);
  else
    unsetenv("GRK_MERCURY");
#endif
}

struct CallbackStream
{
  std::vector<uint8_t> bytes;
  uint64_t position = 0;
  size_t largestRead = 0;
  uint64_t bytesRead = 0;
  std::atomic<int> callsInFlight{0};
  std::atomic<bool> callsOverlapped{false};
};

class CallGuard
{
public:
  explicit CallGuard(CallbackStream& stream) : stream_(stream)
  {
    if(stream_.callsInFlight.fetch_add(1) > 0)
      stream_.callsOverlapped = true;
  }
  ~CallGuard()
  {
    stream_.callsInFlight.fetch_sub(1);
  }

private:
  CallbackStream& stream_;
};

size_t readBytes(uint8_t* buffer, size_t numBytes, void* userData)
{
  auto& stream = *static_cast<CallbackStream*>(userData);
  CallGuard guard(stream);
  stream.largestRead = std::max(stream.largestRead, numBytes);
  if(stream.position >= stream.bytes.size())
    return 0;
  size_t available = (size_t)(stream.bytes.size() - stream.position);
  size_t copied = std::min(numBytes, available);
  memcpy(buffer, stream.bytes.data() + stream.position, copied);
  stream.position += copied;
  stream.bytesRead += copied;
  return copied;
}

size_t writeBytes(const uint8_t* buffer, size_t numBytes, void* userData)
{
  auto& stream = *static_cast<CallbackStream*>(userData);
  CallGuard guard(stream);
  size_t end = (size_t)stream.position + numBytes;
  if(end > stream.bytes.size())
    stream.bytes.resize(end);
  memcpy(stream.bytes.data() + stream.position, buffer, numBytes);
  stream.position = end;
  return numBytes;
}

bool seekTo(uint64_t offset, void* userData)
{
  auto& stream = *static_cast<CallbackStream*>(userData);
  CallGuard guard(stream);
  // VSIFSeekL and fseek accept offsets past the end of a file, and the next read returns 0 bytes
  stream.position = offset;
  return true;
}

struct Layout
{
  const char* name;
  uint32_t imageSize;
  uint32_t tileSize;
  bool writeTLM;
};

bool isSingleTile(const Layout& layout)
{
  return layout.tileSize == 0;
}

grk_image* makeImage(const Layout& layout)
{
  grk_image_comp params = {};
  params.dx = 1;
  params.dy = 1;
  params.w = layout.imageSize;
  params.h = layout.imageSize;
  params.prec = PRECISION;
  grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
  if(!image)
    return nullptr;
  auto& comp = image->comps[0];
  auto data = static_cast<int32_t*>(comp.data);
  for(uint32_t y = 0; y < comp.h; ++y)
    for(uint32_t x = 0; x < comp.w; ++x)
      data[(size_t)y * comp.stride + x] = noiseSample(x, y);
  return image;
}

bool compressThroughCallbacks(const Layout& layout, std::vector<uint8_t>& out)
{
  grk_image* image = makeImage(layout);
  if(!image)
  {
    fprintf(stderr, "%s: could not build the source image\n", layout.name);
    return false;
  }
  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.numlayers = 1;
  parameters.write_tlm = layout.writeTLM;
  if(!isSingleTile(layout))
  {
    parameters.tile_size_on = true;
    parameters.t_width = layout.tileSize;
    parameters.t_height = layout.tileSize;
  }

  CallbackStream stream;
  grk_stream_params streamParams = {};
  streamParams.write_fn = writeBytes;
  streamParams.seek_fn = seekTo;
  streamParams.user_data = &stream;

  bool ok = false;
  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  if(!codec)
    fprintf(stderr, "%s: grk_compress_init failed\n", layout.name);
  else
  {
    uint64_t length = grk_compress(codec, nullptr);
    ok = length != 0 && length == stream.bytes.size();
    if(!ok)
      fprintf(stderr, "%s: grk_compress returned %llu with %zu bytes written\n", layout.name,
              (unsigned long long)length, stream.bytes.size());
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);
  out = std::move(stream.bytes);
  return ok;
}

grk_object* openDecoder(const std::string& label, CallbackStream& stream, size_t declaredLength,
                        grk_decompress_parameters& params, bool fromNetwork = false)
{
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  streamParams.read_fn = readBytes;
  streamParams.seek_fn = seekTo;
  streamParams.user_data = &stream;
  streamParams.stream_len = declaredLength;
  streamParams.from_network = fromNetwork;

  clearLog();
  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "%s: grk_decompress_init failed\n", label.c_str());
    return nullptr;
  }
  grk_header_info headerInfo = {};
  const char* failedStep = nullptr;
  if(!grk_decompress_read_header(codec, &headerInfo))
    failedStep = "grk_decompress_read_header";
  else if(!grk_decompress_update(&params, codec))
    failedStep = "grk_decompress_update";
  if(!failedStep)
    return codec;
  fprintf(stderr, "%s: %s failed\n", label.c_str(), failedStep);
  grk_object_unref(codec);
  return nullptr;
}

int32_t sampleAt(const grk_image_comp& comp, uint64_t index)
{
  if(comp.data_type == GRK_INT_16)
    return static_cast<int16_t*>(comp.data)[index];
  return static_cast<int32_t*>(comp.data)[index];
}

bool decodeAndCheckImage(const std::string& label, grk_object* codec, const Layout& layout)
{
  if(!grk_decompress(codec, nullptr))
  {
    fprintf(stderr, "%s: grk_decompress failed\n", label.c_str());
    return false;
  }
  grk_image* image = grk_decompress_get_image(codec);
  if(!image || image->numcomps != 1 || !image->comps[0].data ||
     image->comps[0].w != layout.imageSize || image->comps[0].h != layout.imageSize)
  {
    fprintf(stderr, "%s: grk_decompress_get_image returned no usable image\n", label.c_str());
    return false;
  }
  const auto& comp = image->comps[0];
  for(uint32_t y = 0; y < comp.h; ++y)
    for(uint32_t x = 0; x < comp.w; ++x)
      if(sampleAt(comp, (uint64_t)y * comp.stride + x) != noiseSample(x, y))
      {
        fprintf(stderr, "%s: sample mismatch at x %u, y %u\n", label.c_str(), x, y);
        return false;
      }
  return true;
}

bool decodeAndCheckSwaths(const std::string& label, grk_object* codec, const Layout& layout)
{
  if(!grk_decompress(codec, nullptr))
  {
    fprintf(stderr, "%s: grk_decompress failed\n", label.c_str());
    return false;
  }
  uint32_t swathRows = isSingleTile(layout) ? SINGLE_TILE_SWATH_ROWS : layout.tileSize;
  std::vector<uint8_t> samples((size_t)swathRows * layout.imageSize);
  for(uint32_t y0 = 0; y0 < layout.imageSize; y0 += swathRows)
  {
    uint32_t y1 = std::min(y0 + swathRows, layout.imageSize);
    grk_wait_swath swath = {};
    swath.x1 = layout.imageSize;
    swath.y0 = y0;
    swath.y1 = y1;
    grk_decompress_wait(codec, &swath);

    grk_swath_buffer buffer = {};
    buffer.data = samples.data();
    buffer.prec = PRECISION;
    buffer.numcomps = 1;
    buffer.pixel_space = 1;
    buffer.line_space = layout.imageSize;
    buffer.band_space = (int64_t)(y1 - y0) * layout.imageSize;
    buffer.promote_alpha = -1;
    buffer.x1 = layout.imageSize;
    buffer.y0 = y0;
    buffer.y1 = y1;
    grk_decompress_schedule_swath_copy(codec, &swath, &buffer);
    grk_decompress_wait_swath_copy(codec);

    for(uint32_t y = y0; y < y1; ++y)
      for(uint32_t x = 0; x < layout.imageSize; ++x)
        if(samples[(size_t)(y - y0) * layout.imageSize + x] != noiseSample(x, y))
        {
          fprintf(stderr, "%s: swath sample mismatch at x %u, y %u\n", label.c_str(), x, y);
          return false;
        }
  }
  return true;
}

grk_decompress_parameters swathParameters(const Layout& layout)
{
  grk_decompress_parameters params = {};
  params.asynchronous = true;
  params.simulate_synchronous = true;
  params.core.tile_cache_strategy = GRK_TILE_CACHE_NONE;
  // grok stores a single tile's samples in the composite image
  params.core.skip_allocate_composite = !isSingleTile(layout);
  return params;
}

bool callsStayedSerial(const std::string& label, const CallbackStream& stream)
{
  if(!stream.callsOverlapped)
    return true;
  fprintf(stderr, "%s: read and seek callbacks ran concurrently\n", label.c_str());
  return false;
}

bool classicDecodeAfterLayerChange(const Layout& layout, const std::vector<uint8_t>& bytes)
{
  std::string label = std::string("classic decode after layer change, ") + layout.name;
  setMercury("0");
  CallbackStream stream;
  stream.bytes = bytes;
  grk_decompress_parameters params = {};
  params.core.tile_cache_strategy = GRK_TILE_CACHE_NONE;
  params.core.layers_to_decompress = 1;
  grk_object* codec = openDecoder(label, stream, bytes.size(), params);
  if(!codec)
    return false;
  bool ok = decodeAndCheckImage(label + ", first", codec, layout);
  uint64_t bytesReadByFirstDecode = stream.bytesRead;
  // changed layer counts mark every tile dirty, which makes the next decode reread released tiles
  params.core.layers_to_decompress = 0;
  ok = ok && grk_decompress_update(&params, codec) &&
       decodeAndCheckImage(label + ", second", codec, layout);
  grk_object_unref(codec);
  if(ok && stream.bytesRead == bytesReadByFirstDecode)
  {
    fprintf(stderr, "%s: the second decode read no compressed data, %zu byte stream\n",
            label.c_str(), bytes.size());
    ok = false;
  }
  return ok && callsStayedSerial(label, stream);
}

bool classicSwaths(const Layout& layout, const std::vector<uint8_t>& bytes)
{
  std::string label = std::string("classic swaths, ") + layout.name;
  setMercury("0");
  CallbackStream stream;
  stream.bytes = bytes;
  grk_decompress_parameters params = swathParameters(layout);
  grk_object* codec = openDecoder(label, stream, bytes.size(), params);
  if(!codec)
    return false;
  bool ok = decodeAndCheckSwaths(label, codec, layout);
  grk_object_unref(codec);
  return ok && callsStayedSerial(label, stream);
}

bool mercuryReadInPieces(const std::string& label, const CallbackStream& stream)
{
  if(stream.largestRead < stream.bytes.size() / 2)
    return true;
  fprintf(stderr, "%s: one read asked for %zu bytes of a %zu byte stream\n", label.c_str(),
          stream.largestRead, stream.bytes.size());
  return false;
}

bool mercurySwaths(const Layout& layout, const std::vector<uint8_t>& bytes)
{
  std::string label = "mercury swaths";
  setMercury(nullptr);
  CallbackStream stream;
  stream.bytes = bytes;
  grk_decompress_parameters params = swathParameters(layout);
  grk_object* codec = openDecoder(label, stream, bytes.size(), params);
  if(!codec)
    return false;
  bool ok = decodeAndCheckSwaths(label, codec, layout);
  grk_object_unref(codec);
  if(ok && !logContains(MERCURY_ASYNC_MARKER))
  {
    fprintf(stderr, "%s: fell back to the classic pipeline\n", label.c_str());
    ok = false;
  }
  return ok && callsStayedSerial(label, stream) && mercuryReadInPieces(label, stream);
}

bool mercuryNetworkStreamIsReadOnce(const Layout& layout, const std::vector<uint8_t>& bytes)
{
  std::string label = "mercury swaths from a network stream";
  setMercury(nullptr);
  CallbackStream stream;
  stream.bytes = bytes;
  grk_decompress_parameters params = swathParameters(layout);
  const bool fromNetwork = true;
  grk_object* codec = openDecoder(label, stream, bytes.size(), params, fromNetwork);
  if(!codec)
    return false;
  bool ok = decodeAndCheckSwaths(label, codec, layout);
  grk_object_unref(codec);
  if(ok && !logContains(MERCURY_ASYNC_MARKER))
  {
    fprintf(stderr, "%s: fell back to the classic pipeline\n", label.c_str());
    ok = false;
  }
  // on demand reads make thousands of range requests over http
  if(ok && stream.largestRead < bytes.size())
  {
    fprintf(stderr, "%s: largest read %zu of a %zu byte stream\n", label.c_str(),
            stream.largestRead, bytes.size());
    ok = false;
  }
  return ok && callsStayedSerial(label, stream);
}

bool mercuryRepeatDecode(const Layout& layout, const std::vector<uint8_t>& bytes)
{
  std::string label = "mercury repeat decode";
  setMercury(nullptr);
  CallbackStream stream;
  stream.bytes = bytes;
  grk_decompress_parameters params = {};
  grk_object* codec = openDecoder(label, stream, bytes.size(), params);
  if(!codec)
    return false;
  bool ok = decodeAndCheckImage(label + ", first", codec, layout) &&
            decodeAndCheckImage(label + ", second", codec, layout);
  grk_object_unref(codec);
  if(ok && !logContains(MERCURY_SYNC_MARKER))
  {
    fprintf(stderr, "%s: fell back to the classic pipeline\n", label.c_str());
    ok = false;
  }
  return ok && callsStayedSerial(label, stream) && mercuryReadInPieces(label, stream);
}

bool mercuryRejectsOverstatedLength(const Layout& layout, const std::vector<uint8_t>& bytes)
{
  std::string label = "mercury with an overstated stream_len";
  setMercury(nullptr);
  CallbackStream stream;
  stream.bytes = bytes;
  grk_decompress_parameters params = {};
  grk_object* codec = openDecoder(label, stream, bytes.size() + 4096, params);
  if(!codec)
    return false;
  bool ok = decodeAndCheckImage(label, codec, layout);
  grk_object_unref(codec);
  // mercury needs every declared byte to exist, the classic pipeline stops at the EOC marker
  if(ok && logContains(MERCURY_SYNC_MARKER))
  {
    fprintf(stderr, "%s: mercury decoded a stream shorter than declared\n", label.c_str());
    ok = false;
  }
  return ok;
}

int runClassic(void)
{
  const Layout layouts[] = {{"tiles", CLASSIC_IMAGE_SIZE, TILE_SIZE, false},
                            {"tiles with TLM", CLASSIC_IMAGE_SIZE, TILE_SIZE, true},
                            {"single tile", CLASSIC_IMAGE_SIZE, 0, false}};
  int result = 0;
  for(const auto& layout : layouts)
  {
    std::vector<uint8_t> bytes;
    if(!compressThroughCallbacks(layout, bytes) || !classicDecodeAfterLayerChange(layout, bytes) ||
       !classicSwaths(layout, bytes))
      result = 1;
  }
  return result;
}

int runMercury(void)
{
  const Layout layout = {"single tile", MERCURY_IMAGE_SIZE, 0, false};
  std::vector<uint8_t> bytes;
  if(!compressThroughCallbacks(layout, bytes))
    return 1;
  int result = 0;
  if(!mercurySwaths(layout, bytes))
    result = 1;
  if(!mercuryNetworkStreamIsReadOnce(layout, bytes))
    result = 1;
  if(!mercuryRepeatDecode(layout, bytes))
    result = 1;
  if(!mercuryRejectsOverstatedLength(layout, bytes))
    result = 1;
  return result;
}
} // namespace

int main(int argc, char** argv)
{
  bool mercury = argc > 1 && strcmp(argv[1], "mercury") == 0;
  grk_msg_handlers handlers = {};
  handlers.info_callback = appendLog;
  handlers.warn_callback = appendLog;
  handlers.error_callback = appendLog;
  grk_set_msg_handlers(handlers);
  grk_initialize(nullptr, 0, nullptr);
  int result = mercury ? runMercury() : runClassic();
  grk_deinitialize();
  return result;
}
