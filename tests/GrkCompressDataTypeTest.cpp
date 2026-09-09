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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "grok.h"

namespace
{
constexpr uint32_t imageWidth = 67;
constexpr uint32_t imageHeight = 49;
constexpr size_t compressedCapacity = 1 << 20;

struct CompressionCase
{
  const char* name;
  grk_data_type dataType;
  uint8_t precision;
  bool isSigned;
  uint16_t componentCount;
  bool useMct;
  bool useTiles;
  bool useHighThroughput;
  bool shouldSucceed;
};

void discardLog(const char*, void*) {}

int32_t expectedSample(const CompressionCase& testCase, uint16_t componentIndex, uint32_t x,
                       uint32_t y)
{
  uint32_t sampleRange = 1U << testCase.precision;
  uint32_t value = (x * 2654435761U + y * 2246822519U + componentIndex * 3266489917U) % sampleRange;
  return testCase.isSigned ? (int32_t)value - (int32_t)(sampleRange >> 1) : (int32_t)value;
}

grk_image* makeImage(const CompressionCase& testCase)
{
  std::vector<grk_image_comp> componentParameters(testCase.componentCount);
  for(auto& component : componentParameters)
  {
    component.w = imageWidth;
    component.h = imageHeight;
    component.dx = 1;
    component.dy = 1;
    component.prec = testCase.precision;
    component.sgnd = testCase.isSigned;
    component.data_type = testCase.dataType;
  }
  auto image =
      grk_image_new(testCase.componentCount, componentParameters.data(),
                    testCase.componentCount == 3 ? GRK_CLRSPC_SRGB : GRK_CLRSPC_GRAY, true);
  if(!image || !testCase.shouldSucceed)
    return image;

  for(uint16_t componentIndex = 0; componentIndex < testCase.componentCount; ++componentIndex)
  {
    auto component = image->comps + componentIndex;
    for(uint32_t y = 0; y < imageHeight; ++y)
      for(uint32_t x = 0; x < imageWidth; ++x)
      {
        size_t sampleIndex = (size_t)y * component->stride + x;
        int32_t sample = expectedSample(testCase, componentIndex, x, y);
        if(testCase.dataType == GRK_INT_16)
          static_cast<int16_t*>(component->data)[sampleIndex] = (int16_t)sample;
        else
          static_cast<int32_t*>(component->data)[sampleIndex] = sample;
      }
  }
  return image;
}

bool decodedSamplesMatch(const CompressionCase& testCase, const uint8_t* compressedData,
                         size_t compressedSize)
{
  grk_decompress_parameters parameters = {};
  grk_stream_params stream = {};
  stream.buf = const_cast<uint8_t*>(compressedData);
  stream.buf_len = compressedSize;
  stream.is_read_stream = true;
  auto codec = grk_decompress_init(&stream, &parameters);
  if(!codec)
  {
    std::fprintf(stderr, "%s: grk_decompress_init failed\n", testCase.name);
    return false;
  }

  grk_header_info headerInfo = {};
  bool success = grk_decompress_read_header(codec, &headerInfo) && grk_decompress(codec, nullptr);
  auto image = success ? grk_decompress_get_image(codec) : nullptr;
  if(!image || image->numcomps != testCase.componentCount)
    success = false;

  for(uint16_t componentIndex = 0; success && componentIndex < image->numcomps; ++componentIndex)
  {
    auto component = image->comps + componentIndex;
    if(component->w != imageWidth || component->h != imageHeight || !component->data)
    {
      success = false;
      break;
    }
    for(uint32_t y = 0; success && y < imageHeight; ++y)
      for(uint32_t x = 0; x < imageWidth; ++x)
      {
        size_t sampleIndex = (size_t)y * component->stride + x;
        int32_t actual = component->data_type == GRK_INT_16
                             ? static_cast<int16_t*>(component->data)[sampleIndex]
                             : static_cast<int32_t*>(component->data)[sampleIndex];
        int32_t expected = expectedSample(testCase, componentIndex, x, y);
        if(actual != expected)
        {
          std::fprintf(stderr, "%s: component %u sample (%u,%u) is %d, expected %d\n",
                       testCase.name, componentIndex, x, y, actual, expected);
          success = false;
          break;
        }
      }
  }

  if(!success)
    std::fprintf(stderr, "%s: decode failed or returned the wrong image\n", testCase.name);
  grk_object_unref(codec);
  return success;
}

bool runCase(const CompressionCase& testCase)
{
  auto image = makeImage(testCase);
  if(!image)
  {
    std::fprintf(stderr, "%s: image allocation failed\n", testCase.name);
    return false;
  }

  std::vector<uint8_t> compressed(compressedCapacity);
  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.numlayers = 1;
  parameters.mct = testCase.useMct ? 1 : 0;
  parameters.tile_size_on = testCase.useTiles;
  parameters.t_width = 31;
  parameters.t_height = 23;
  parameters.cblk_sty = testCase.useHighThroughput ? GRK_CBLKSTY_HT_ONLY : 0;

  grk_stream_params stream = {};
  stream.buf = compressed.data();
  stream.buf_len = compressed.size();
  auto codec = grk_compress_init(&stream, &parameters, image);
  if(!testCase.shouldSucceed)
  {
    bool rejected = codec == nullptr;
    if(codec)
      grk_object_unref(codec);
    grk_object_unref(&image->obj);
    if(!rejected)
      std::fprintf(stderr, "%s: grk_compress_init accepted invalid input\n", testCase.name);
    return rejected;
  }

  size_t compressedSize = codec ? grk_compress(codec, nullptr) : 0;
  if(codec)
    grk_object_unref(codec);
  grk_object_unref(&image->obj);
  if(!compressedSize)
  {
    std::fprintf(stderr, "%s: compression failed\n", testCase.name);
    return false;
  }
  return decodedSamplesMatch(testCase, compressed.data(), compressedSize);
}

bool recommendedDataTypesMatch()
{
  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  uint8_t defaultResolutionCount = parameters.numresolution;
  bool success = true;
  auto expect = [&](const char* name, uint8_t precision, bool isMctComponent,
                    grk_data_type expected) {
    auto actual = grk_compress_get_recommended_data_type(&parameters, precision, isMctComponent);
    if(actual == expected)
      return;
    std::fprintf(stderr, "%s: recommended type is %d, expected %d\n", name, (int)actual,
                 (int)expected);
    success = false;
  };

  expect("reversible 12 bit", 12, false, GRK_INT_16);
  expect("reversible 13 bit", 13, false, GRK_INT_32);

  parameters.mct = 1;
  expect("reversible MCT component 11 bit", 11, true, GRK_INT_16);
  expect("reversible MCT component 12 bit", 12, true, GRK_INT_32);
  expect("reversible non-MCT component 12 bit", 12, false, GRK_INT_16);
  parameters.mct = 0;

  parameters.irreversible = true;
  expect("irreversible", 8, false, GRK_INT_32);
  parameters.irreversible = false;

  parameters.numresolution = 1;
  expect("single resolution", 8, false, GRK_INT_32);
  parameters.numresolution = defaultResolutionCount;

  parameters.apply_icc = true;
  expect("ICC", 8, false, GRK_INT_32);
  parameters.apply_icc = false;

  parameters.apply_xyz_transform = true;
  expect("XYZ", 8, false, GRK_INT_32);
  parameters.apply_xyz_transform = false;

  parameters.rsiz = GRK_PROFILE_CINEMA_2K;
  expect("Cinema", 8, false, GRK_INT_32);

  parameters.rsiz = GRK_PROFILE_BC_SINGLE;
  expect("lossy Broadcast", 8, false, GRK_INT_32);
  parameters.rsiz = GRK_PROFILE_BC_MULTI_R;
  expect("reversible Broadcast", 12, false, GRK_INT_16);

  parameters.rsiz = GRK_PROFILE_IMF_2K;
  expect("lossy IMF", 8, false, GRK_INT_32);
  parameters.rsiz = GRK_PROFILE_IMF_2K_R;
  expect("reversible IMF", 12, false, GRK_INT_16);

  parameters.rsiz = GRK_PROFILE_NONE;
  parameters.mct = 2;
  expect("custom MCT", 8, false, GRK_INT_32);

  if(grk_compress_get_recommended_data_type(nullptr, 8, false) != GRK_INT_32)
  {
    std::fprintf(stderr, "null parameters: expected GRK_INT_32\n");
    success = false;
  }
  return success;
}

} // namespace

int main()
{
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "0");
#else
  setenv("GRK_MERCURY", "0", 1);
#endif

  grk_msg_handlers handlers = {};
  handlers.info_callback = discardLog;
  handlers.warn_callback = discardLog;
  handlers.error_callback = discardLog;
  grk_set_msg_handlers(handlers);
  grk_initialize(nullptr, 0, nullptr);

  const CompressionCase cases[] = {
      {"int32 baseline", GRK_INT_32, 12, false, 1, false, false, false, true},
      {"unsigned int16 in place", GRK_INT_16, 12, false, 1, false, false, false, true},
      {"signed int16 in place", GRK_INT_16, 12, true, 1, false, false, false, true},
      {"unsigned int16 tiled in place", GRK_INT_16, 12, false, 1, false, true, false, true},
      {"unsigned int16 mct in place", GRK_INT_16, 11, false, 3, true, false, false, true},
      {"unsigned int16 ht in place", GRK_INT_16, 12, false, 1, false, false, true, true},
      {"unsigned int16 widened tiles", GRK_INT_16, 15, false, 1, false, true, false, true},
      {"signed int16 widened", GRK_INT_16, 16, true, 1, false, false, false, true},
      {"unsigned int16 overflow", GRK_INT_16, 16, false, 1, false, false, false, false},
      {"int8 input", GRK_INT_8, 8, false, 1, false, false, false, false},
      {"float input", GRK_FLOAT, 8, false, 1, false, false, false, false},
      {"double input", GRK_DOUBLE, 8, false, 1, false, false, false, false},
  };

  int failures = 0;
  if(!recommendedDataTypesMatch())
    ++failures;
  for(const auto& testCase : cases)
    if(!runCase(testCase))
      ++failures;
  grk_deinitialize();

  if(failures == 0)
  {
    std::fprintf(stderr, "GrkCompressDataTypeTest: all tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "GrkCompressDataTypeTest: %d failure(s)\n", failures);
  return 1;
}
