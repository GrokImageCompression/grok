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

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "grok.h"

namespace
{
const uint32_t imageSize = 128;
const uint32_t levels = 5;
const uint16_t numComponents = 3;
const int32_t brightestSample = 4095;

using Pattern = std::array<int8_t, imageSize>;

std::vector<double> forwardLinear53(std::vector<double> values)
{
  auto size = (int64_t)values.size();
  auto mirrored = [size](int64_t index) {
    index = index < 0 ? -index : index;
    return index > size - 1 ? 2 * (size - 1) - index : index;
  };
  for(int64_t i = 1; i < size; i += 2)
    values[(size_t)i] -= (values[(size_t)mirrored(i - 1)] + values[(size_t)mirrored(i + 1)]) / 2;
  for(int64_t i = 0; i < size; i += 2)
    values[(size_t)i] += (values[(size_t)mirrored(i - 1)] + values[(size_t)mirrored(i + 1)]) / 4;
  return values;
}

Pattern worstCasePattern(uint32_t depth, bool highBand)
{
  Pattern pattern;
  for(uint32_t position = 0; position < imageSize; ++position)
  {
    std::vector<double> values(imageSize, 0.0);
    values[position] = 1.0;
    for(uint32_t level = 0; level < depth; ++level)
    {
      values = forwardLinear53(values);
      if(level + 1 == depth)
        break;
      std::vector<double> low;
      for(size_t i = 0; i < values.size(); i += 2)
        low.push_back(values[i]);
      values = low;
    }
    size_t centre = values.size() / 2;
    centre = highBand ? centre | 1 : centre & ~(size_t)1;
    pattern[position] = values[centre] >= 0 ? 1 : -1;
  }
  return pattern;
}

std::vector<Pattern> allPatterns()
{
  std::vector<Pattern> patterns;
  Pattern constant;
  constant.fill(1);
  patterns.push_back(constant);
  for(uint32_t depth = 1; depth <= levels; ++depth)
  {
    patterns.push_back(worstCasePattern(depth, false));
    patterns.push_back(worstCasePattern(depth, true));
  }
  return patterns;
}

void writeSample(const grk_image_comp& comp, uint32_t x, uint32_t y, int32_t value)
{
  size_t index = (size_t)y * comp.stride + x;
  if(comp.data_type == GRK_INT_16)
    static_cast<int16_t*>(comp.data)[index] = (int16_t)value;
  else
    static_cast<int32_t*>(comp.data)[index] = value;
}

int32_t readSample(const grk_image_comp& comp, uint32_t x, uint32_t y)
{
  size_t index = (size_t)y * comp.stride + x;
  if(comp.data_type == GRK_INT_16)
    return static_cast<int16_t*>(comp.data)[index];
  return static_cast<int32_t*>(comp.data)[index];
}

using Image = std::array<std::vector<int32_t>, numComponents>;

Image makeSamples(const Pattern& rows, const Pattern& columns,
                  const std::array<int8_t, numComponents>& componentSigns)
{
  Image samples;
  for(uint16_t c = 0; c < numComponents; ++c)
  {
    samples[c].resize((size_t)imageSize * imageSize);
    for(uint32_t y = 0; y < imageSize; ++y)
      for(uint32_t x = 0; x < imageSize; ++x)
        samples[c][(size_t)y * imageSize + x] =
            rows[y] * columns[x] * componentSigns[c] > 0 ? brightestSample : 0;
  }
  return samples;
}

std::vector<uint8_t> compressLossless(const Image& samples)
{
  grk_image_comp params[numComponents] = {};
  for(uint16_t c = 0; c < numComponents; ++c)
  {
    params[c].dx = 1;
    params[c].dy = 1;
    params[c].w = imageSize;
    params[c].h = imageSize;
    params[c].prec = 12;
    params[c].sgnd = false;
  }
  grk_image* image = grk_image_new(numComponents, params, GRK_CLRSPC_SRGB, true);
  if(!image)
    return {};
  for(uint16_t c = 0; c < numComponents; ++c)
    for(uint32_t y = 0; y < imageSize; ++y)
      for(uint32_t x = 0; x < imageSize; ++x)
        writeSample(image->comps[c], x, y, samples[c][(size_t)y * imageSize + x]);

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.mct = 1;
  parameters.numresolution = levels + 1;

  std::vector<uint8_t> output((size_t)imageSize * imageSize * numComponents * 4);
  grk_stream_params streamParams = {};
  streamParams.buf = output.data();
  streamParams.buf_len = output.size();
  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  uint64_t length = codec ? grk_compress(codec, nullptr) : 0;
  if(codec)
    grk_object_unref(codec);
  grk_object_unref(&image->obj);
  output.resize(length);
  return output;
}

bool decodesToSource(std::vector<uint8_t>& stream, const Image& samples)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  streamParams.buf = stream.data();
  streamParams.buf_len = stream.size();
  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
    return false;
  grk_header_info headerInfo = {};
  bool matches = grk_decompress_read_header(codec, &headerInfo) && grk_decompress(codec, nullptr);
  auto image = matches ? grk_decompress_get_image(codec) : nullptr;
  matches = image != nullptr;
  for(uint16_t c = 0; matches && c < numComponents; ++c)
  {
    if(image->comps[c].data_type != GRK_INT_16)
      matches = false;
    for(uint32_t y = 0; matches && y < imageSize; ++y)
      for(uint32_t x = 0; matches && x < imageSize; ++x)
        matches = readSample(image->comps[c], x, y) == samples[c][(size_t)y * imageSize + x];
  }
  grk_object_unref(codec);
  return matches;
}

void setMercury(const char* value)
{
#ifdef _WIN32
  _putenv_s("GRK_MERCURY", value);
#else
  setenv("GRK_MERCURY", value, 1);
#endif
}
} // namespace

int main(void)
{
  grk_initialize(nullptr, 0, nullptr);
  const std::array<std::array<int8_t, numComponents>, 3> componentSigns = {
      {{1, -1, 1}, {1, 1, 1}, {-1, -1, 1}}};
  auto patterns = allPatterns();
  bool ok = true;
  size_t encodedStreams = 0;
  for(size_t rowIndex = 0; rowIndex < patterns.size(); ++rowIndex)
  {
    for(size_t columnIndex = 0; columnIndex < patterns.size(); ++columnIndex)
    {
      for(size_t signIndex = 0; signIndex < componentSigns.size(); ++signIndex)
      {
        auto samples =
            makeSamples(patterns[rowIndex], patterns[columnIndex], componentSigns[signIndex]);
        auto stream = compressLossless(samples);
        // TODO: grk_compress returns no stream for 34 of these patterns
        if(stream.empty())
          continue;
        ++encodedStreams;
        for(const char* mercury : {"0", "1"})
        {
          setMercury(mercury);
          if(!decodesToSource(stream, samples))
          {
            fprintf(stderr, "pattern %zu %zu %zu GRK_MERCURY=%s did not decode exactly\n", rowIndex,
                    columnIndex, signIndex, mercury);
            ok = false;
          }
        }
      }
    }
  }
  grk_deinitialize();
  return ok && encodedStreams > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
