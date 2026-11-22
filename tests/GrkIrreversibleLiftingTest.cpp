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

// 9/7 round trip of high frequency patterns. the delta lifting step must see the
// high pass samples before their K scaling: a nyquist stripe comes back with a
// constant offset and wider stripes with tilted bars when it does not, while
// smooth content hides the error. int16 images up to 10 bits take the fixed point
// 9/7, where a wrong band scale shifts every sample of that band.

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
// int32 images run the float 9/7 at every precision, int16 images run the fixed point 9/7 up to
// 10 bits
const uint8_t FLOAT_PRECISIONS[] = {8, 12, 16};
const uint8_t FIXED_POINT_PRECISIONS[] = {8, 10};
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

struct Geometry
{
  uint32_t width;
  uint32_t height;
  uint8_t precision;
  uint8_t numResolutions;
  uint16_t numComponents;
  grk_data_type dataType;
};

int32_t sampleAt(const grk_image_comp* component, size_t index)
{
  return component->data_type == GRK_INT_16 ? ((const int16_t*)component->data)[index]
                                            : ((const int32_t*)component->data)[index];
}

grk_image* makeImage(const Geometry& geometry, const Pattern& pattern)
{
  std::vector<grk_image_comp> components(geometry.numComponents);
  for(auto& component : components)
  {
    component = {};
    component.w = geometry.width;
    component.h = geometry.height;
    component.dx = 1;
    component.dy = 1;
    component.prec = geometry.precision;
    component.data_type = geometry.dataType;
  }
  auto colourSpace = geometry.numComponents == 3 ? GRK_CLRSPC_SRGB : GRK_CLRSPC_GRAY;
  auto image = grk_image_new(geometry.numComponents, components.data(), colourSpace, true);
  if(!image)
    return nullptr;
  const int32_t maxValue = (1 << geometry.precision) - 1;
  for(uint16_t compno = 0; compno < geometry.numComponents; ++compno)
  {
    auto component = image->comps + compno;
    for(uint32_t y = 0; y < geometry.height; ++y)
    {
      for(uint32_t x = 0; x < geometry.width; ++x)
      {
        size_t index = (size_t)y * component->stride + x;
        int32_t value = pattern.sample(x + compno, y, maxValue);
        if(geometry.dataType == GRK_INT_16)
          ((int16_t*)component->data)[index] = (int16_t)value;
        else
          ((int32_t*)component->data)[index] = value;
      }
    }
  }
  return image;
}

// the decoded components, row major without padding, empty on failure
std::vector<int32_t> roundTrip(grk_image* image, uint8_t numResolutions)
{
  std::vector<int32_t> decoded;
  grk_cparameters parameters;
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.irreversible = true;
  parameters.numresolution = numResolutions;
  parameters.numlayers = 1;
  parameters.mct = image->numcomps == 3 ? 1 : 0;
  const uint32_t width = image->comps[0].w;
  const uint32_t height = image->comps[0].h;
  std::vector<uint8_t> stream((size_t)width * height * image->numcomps * 4 + 4096);
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
  for(uint16_t compno = 0; compno < output->numcomps; ++compno)
  {
    auto component = output->comps + compno;
    for(uint32_t y = 0; y < height; ++y)
      for(uint32_t x = 0; x < width; ++x)
        decoded.push_back(sampleAt(component, (size_t)y * component->stride + x));
  }
  grk_object_unref(decompressor);
  return decoded;
}

void check(const Geometry& geometry, const Pattern& pattern)
{
  auto source = makeImage(geometry, pattern);
  auto input = makeImage(geometry, pattern);
  if(!source || !input)
  {
    std::fprintf(stderr, "FAIL: image allocation\n");
    ++failures;
    return;
  }
  const uint32_t width = geometry.width;
  const uint32_t height = geometry.height;
  auto decoded = roundTrip(input, geometry.numResolutions);
  int32_t maxError = -1;
  if(decoded.size() == (size_t)width * height * geometry.numComponents)
  {
    maxError = 0;
    size_t decodedIndex = 0;
    for(uint16_t compno = 0; compno < geometry.numComponents; ++compno)
    {
      auto component = source->comps + compno;
      for(uint32_t y = 0; y < height; ++y)
        for(uint32_t x = 0; x < width; ++x)
          maxError =
              std::max(maxError, std::abs(decoded[decodedIndex++] -
                                          sampleAt(component, (size_t)y * component->stride + x)));
    }
  }
  // classic's int16 forward and inverse 9/7 each keep three fraction bits at 10 bits, and their
  // rounding adds up to three codes on a 3-component checkerboard
  const bool fewFractionBits = geometry.dataType == GRK_INT_16 && geometry.precision > 8;
  const int32_t maxErrorAllowed = MAX_ERROR_ALLOWED + (fewFractionBits ? 3 : 0);
  bool ok = maxError >= 0 && maxError <= maxErrorAllowed;
  std::printf("%s %s %ux%ux%u %u bit %u resolutions %-18s max error %d\n", ok ? "ok  " : "FAIL",
              geometry.dataType == GRK_INT_16 ? "int16" : "int32", width, height,
              geometry.numComponents, geometry.precision, geometry.numResolutions, pattern.name,
              maxError);
  if(!ok)
    ++failures;
  grk_object_unref(&source->obj);
  grk_object_unref(&input->obj);
}
} // namespace

int main()
{
  grk_initialize(nullptr, 0, nullptr);
  for(auto precision : FLOAT_PRECISIONS)
    for(auto& pattern : PATTERNS)
      for(uint8_t numResolutions : {(uint8_t)2, (uint8_t)6})
      {
        check({1024, 64, precision, numResolutions, 1, GRK_INT_32}, pattern);
        // odd dimensions put the high pass on the even samples
        check({1023, 63, precision, numResolutions, 1, GRK_INT_32}, pattern);
      }
  for(auto precision : FIXED_POINT_PRECISIONS)
    for(auto& pattern : PATTERNS)
      for(uint16_t numComponents : {(uint16_t)1, (uint16_t)3})
      {
        for(uint8_t numResolutions : {(uint8_t)2, (uint8_t)6})
        {
          check({1024, 64, precision, numResolutions, numComponents, GRK_INT_16}, pattern);
          check({1023, 63, precision, numResolutions, numComponents, GRK_INT_16}, pattern);
        }
        // deep levels shrink to one row or one column, on even and odd positions
        check({257, 3, precision, 8, numComponents, GRK_INT_16}, pattern);
        check({3, 257, precision, 8, numComponents, GRK_INT_16}, pattern);
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
