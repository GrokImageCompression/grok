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

// 9/7 round trip of high frequency patterns on the float wavelet path. the
// delta lifting step must see the high pass samples before their K scaling: a
// nyquist stripe comes back with a constant offset and wider stripes with
// tilted bars when it does not, while smooth content hides the error.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <vector>

#include "grok.h"

namespace
{
// irreversible compress runs the float 9/7 at every precision
const uint8_t PRECISIONS[] = {8, 12, 16};
// a lossy round trip of a pattern the wavelet represents exactly rounds to the
// source within one code, two leaves room for the quantiser
const int32_t MAX_ERROR_ALLOWED = 2;

int failures = 0;

struct Pattern
{
  const char* name;
  std::function<int32_t(uint32_t x, uint32_t y, int32_t maxValue)> sample;
};

const Pattern PATTERNS[] = {
    {"nyquist stripes",
     [](uint32_t x, uint32_t, int32_t mx) { return (x % 2) ? mx * 3 / 4 : mx / 4; }},
    {"vertical stripes",
     [](uint32_t x, uint32_t, int32_t mx) { return ((x / 4) % 2) ? mx * 3 / 4 : mx / 4; }},
    {"horizontal stripes",
     [](uint32_t, uint32_t y, int32_t mx) { return ((y / 4) % 2) ? mx * 3 / 4 : mx / 4; }},
    {"checkerboard", [](uint32_t x, uint32_t y,
                        int32_t mx) { return (((x / 4) + (y / 4)) % 2) ? mx * 3 / 4 : mx / 4; }},
};

grk_image* makeImage(uint32_t width, uint32_t height, uint8_t precision, const Pattern& pattern)
{
  grk_image_comp component = {};
  component.w = width;
  component.h = height;
  component.dx = 1;
  component.dy = 1;
  component.prec = precision;
  auto image = grk_image_new(1, &component, GRK_CLRSPC_GRAY, true);
  if(!image)
    return nullptr;
  const int32_t maxValue = (1 << precision) - 1;
  auto data = (int32_t*)image->comps[0].data;
  for(uint32_t y = 0; y < height; ++y)
  {
    for(uint32_t x = 0; x < width; ++x)
      data[x] = pattern.sample(x, y, maxValue);
    data += image->comps[0].stride;
  }
  return image;
}

// the decoded component, row major without padding, empty on failure
std::vector<int32_t> roundTrip(grk_image* image, uint8_t numResolutions)
{
  std::vector<int32_t> decoded;
  grk_cparameters parameters;
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.irreversible = true;
  parameters.numresolution = numResolutions;
  parameters.numlayers = 1;
  const uint32_t width = image->comps[0].w;
  const uint32_t height = image->comps[0].h;
  std::vector<uint8_t> stream((size_t)width * height * 4 + 4096);
  grk_stream_params streamParams = {};
  streamParams.buf = stream.data();
  streamParams.buf_len = stream.size();
  auto compressor = grk_compress_init(&streamParams, &parameters, image);
  uint64_t length = compressor ? grk_compress(compressor, nullptr) : 0;
  grk_object_unref(compressor);
  if(!length)
    return decoded;

  grk_decompress_parameters decompressParameters = {};
  grk_stream_params decodeStream = {};
  decodeStream.buf = stream.data();
  decodeStream.buf_len = length;
  auto decompressor = grk_decompress_init(&decodeStream, &decompressParameters);
  grk_header_info header = {};
  if(!decompressor || !grk_decompress_read_header(decompressor, &header) ||
     !grk_decompress(decompressor, nullptr))
  {
    grk_object_unref(decompressor);
    return decoded;
  }
  auto output = grk_decompress_get_image(decompressor);
  auto component = output->comps;
  for(uint32_t y = 0; y < height; ++y)
    for(uint32_t x = 0; x < width; ++x)
    {
      size_t index = (size_t)y * component->stride + x;
      decoded.push_back(component->data_type == GRK_INT_16 ? ((int16_t*)component->data)[index]
                                                           : ((int32_t*)component->data)[index]);
    }
  grk_object_unref(decompressor);
  return decoded;
}

void check(uint32_t width, uint32_t height, uint8_t precision, uint8_t numResolutions,
           const Pattern& pattern)
{
  auto source = makeImage(width, height, precision, pattern);
  auto input = makeImage(width, height, precision, pattern);
  if(!source || !input)
  {
    std::fprintf(stderr, "FAIL: image allocation\n");
    ++failures;
    return;
  }
  auto decoded = roundTrip(input, numResolutions);
  int32_t maxError = -1;
  if(decoded.size() == (size_t)width * height)
  {
    maxError = 0;
    auto data = (const int32_t*)source->comps[0].data;
    for(uint32_t y = 0; y < height; ++y)
      for(uint32_t x = 0; x < width; ++x)
        maxError = std::max(maxError, std::abs(decoded[(size_t)y * width + x] -
                                               data[(size_t)y * source->comps[0].stride + x]));
  }
  bool ok = maxError >= 0 && maxError <= MAX_ERROR_ALLOWED;
  std::printf("%s %ux%u %u bit %u resolutions %-18s max error %d\n", ok ? "ok  " : "FAIL", width,
              height, precision, numResolutions, pattern.name, maxError);
  if(!ok)
    ++failures;
  grk_object_unref(&source->obj);
  grk_object_unref(&input->obj);
}
} // namespace

int main()
{
  grk_initialize(nullptr, 0, nullptr);
  for(auto precision : PRECISIONS)
    for(auto& pattern : PATTERNS)
      for(uint8_t numResolutions : {(uint8_t)2, (uint8_t)6})
      {
        check(1024, 64, precision, numResolutions, pattern);
        // odd dimensions put the high pass on the even samples
        check(1023, 63, precision, numResolutions, pattern);
      }
  grk_deinitialize();
  if(failures)
  {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  std::printf("irreversible lifting OK\n");
  return 0;
}
