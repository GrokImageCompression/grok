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

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "grok.h"
#include "NoiseSample.h"

namespace
{
const uint32_t SINGLE_TILE_SWATH_ROWS = 1024;

struct CallbackFile
{
  int fd = -1;
  uint64_t size = 0;
  uint64_t position = 0;
  uint64_t readCalls = 0;
  uint64_t readBytes = 0;
  uint64_t largestRead = 0;
};

size_t readCallback(uint8_t* buffer, size_t bytes, void* userData)
{
  auto& file = *static_cast<CallbackFile*>(userData);
  file.readCalls++;
  file.largestRead = std::max<uint64_t>(file.largestRead, bytes);
  ssize_t got = pread(file.fd, buffer, bytes, static_cast<off_t>(file.position));
  if(got <= 0)
    return 0;
  file.position += static_cast<uint64_t>(got);
  file.readBytes += static_cast<uint64_t>(got);
  return static_cast<size_t>(got);
}

bool seekCallback(uint64_t offset, void* userData)
{
  static_cast<CallbackFile*>(userData)->position = offset;
  return true;
}

uint64_t decodeSwaths(grk_object* codec, const grk_image& image, uint32_t swathRows, bool checksum)
{
  uint32_t width = image.x1 - image.x0;
  uint16_t numcomps = image.numcomps;
  bool sixteenBit = image.comps[0].prec > 8;
  size_t sampleBytes = sixteenBit ? 2 : 1;
  std::vector<uint8_t> samples(size_t(numcomps) * swathRows * width * sampleBytes);
  uint64_t sum = 0;
  for(uint32_t y0 = image.y0; y0 < image.y1; y0 += swathRows)
  {
    uint32_t y1 = std::min(y0 + swathRows, image.y1);
    grk_wait_swath swath = {};
    swath.x0 = image.x0;
    swath.x1 = image.x1;
    swath.y0 = y0;
    swath.y1 = y1;
    grk_decompress_wait(codec, &swath);
    size_t rows = y1 - y0;
    grk_swath_buffer buffer = {};
    buffer.data = samples.data();
    buffer.prec = sixteenBit ? 16 : 8;
    buffer.numcomps = numcomps;
    buffer.pixel_space = int64_t(sampleBytes);
    buffer.line_space = int64_t(width * sampleBytes);
    buffer.band_space = int64_t(rows * width * sampleBytes);
    buffer.promote_alpha = -1;
    buffer.x0 = image.x0;
    buffer.x1 = image.x1;
    buffer.y0 = y0;
    buffer.y1 = y1;
    grk_decompress_schedule_swath_copy(codec, &swath, &buffer);
    grk_decompress_wait_swath_copy(codec);
    if(!checksum)
      continue;
    size_t count = size_t(numcomps) * rows * width;
    for(size_t i = 0; i < count; ++i)
      sum += sixteenBit ? reinterpret_cast<const uint16_t*>(samples.data())[i] : samples[i];
  }
  return sum;
}

int decode(const std::string& input, const std::string& source, bool checksum)
{
  CallbackFile file;
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  if(source == "callback")
  {
    file.fd = open(input.c_str(), O_RDONLY);
    struct stat info;
    if(file.fd < 0 || fstat(file.fd, &info) != 0)
      throw std::runtime_error("cannot open " + input);
    file.size = static_cast<uint64_t>(info.st_size);
    streamParams.read_fn = readCallback;
    streamParams.seek_fn = seekCallback;
    streamParams.user_data = &file;
    streamParams.stream_len = file.size;
  }
  else
  {
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", input.c_str());
  }

  grk_initialize(nullptr, 0, nullptr);
  auto start = std::chrono::steady_clock::now();
  grk_decompress_parameters params = {};
  params.asynchronous = true;
  params.simulate_synchronous = true;
  params.core.tile_cache_strategy = GRK_TILE_CACHE_NONE;
  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
    throw std::runtime_error("grk_decompress_init failed");
  grk_header_info header = {};
  if(!grk_decompress_read_header(codec, &header))
    throw std::runtime_error("grk_decompress_read_header failed");
  bool singleTile = header.t_grid_width * header.t_grid_height == 1;
  // grok stores a single tile's samples in the composite image
  params.core.skip_allocate_composite = !singleTile;
  if(!grk_decompress_update(&params, codec) || !grk_decompress(codec, nullptr))
    throw std::runtime_error("grk_decompress failed");
  uint32_t swathRows = singleTile ? SINGLE_TILE_SWATH_ROWS : header.t_height;
  uint64_t sum = decodeSwaths(codec, header.header_image, swathRows, checksum);
  grk_object_unref(codec);
  double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

  struct rusage usage;
  getrusage(RUSAGE_SELF, &usage);
  printf("{\"ms\": %.1f, \"rss_mb\": %.1f, \"read_calls\": %llu, \"read_bytes\": %llu, "
         "\"largest_read\": %llu, \"checksum\": %llu}\n",
         ms, usage.ru_maxrss / 1024.0, (unsigned long long)file.readCalls,
         (unsigned long long)file.readBytes, (unsigned long long)file.largestRead,
         (unsigned long long)sum);
  if(file.fd >= 0)
    close(file.fd);
  grk_deinitialize();
  return 0;
}

int generate(const std::string& output, uint32_t size, uint32_t tileSize)
{
  grk_initialize(nullptr, 0, nullptr);
  grk_image_comp compParams = {};
  compParams.dx = 1;
  compParams.dy = 1;
  compParams.w = size;
  compParams.h = size;
  compParams.prec = 8;
  grk_image* image = grk_image_new(1, &compParams, GRK_CLRSPC_GRAY, true);
  if(!image)
    throw std::runtime_error("grk_image_new failed");
  auto& comp = image->comps[0];
  auto data = static_cast<int32_t*>(comp.data);
  for(uint32_t y = 0; y < size; ++y)
    for(uint32_t x = 0; x < size; ++x)
      data[(size_t)y * comp.stride + x] = noiseSample(x, y);

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.numlayers = 1;
  if(tileSize)
  {
    parameters.tile_size_on = true;
    parameters.t_width = tileSize;
    parameters.t_height = tileSize;
  }
  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", output.c_str());
  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  if(!codec || !grk_compress(codec, nullptr))
    throw std::runtime_error("compress failed");
  grk_object_unref(codec);
  grk_object_unref(&image->obj);
  grk_deinitialize();
  return 0;
}
} // namespace

int main(int argc, char** argv)
{
  std::string command = argc > 1 ? argv[1] : "";
  try
  {
    if(command == "decode" && argc >= 4)
      return decode(argv[2], argv[3], argc > 4 && strcmp(argv[4], "checksum") == 0);
    if(command == "generate" && argc == 5)
      return generate(argv[2], (uint32_t)std::stoul(argv[3]), (uint32_t)std::stoul(argv[4]));
  }
  catch(const std::exception& error)
  {
    fprintf(stderr, "error: %s\n", error.what());
    return 1;
  }
  fprintf(stderr,
          "usage: %s decode <input> <file|callback> [checksum]\n"
          "       %s generate <output> <size> <tile size, 0 for one tile>\n",
          argv[0], argv[0]);
  return 1;
}
