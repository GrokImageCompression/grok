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

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
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

template<typename ExpectedSample>
bool decodedSamplesMatch(const char* name, const uint8_t* compressedData, size_t compressedSize,
                         uint16_t componentCount, uint32_t width, uint32_t height,
                         std::optional<grk_data_type> expectedDecodeType,
                         ExpectedSample expectedSample)
{
  grk_decompress_parameters parameters = {};
  grk_stream_params stream = {};
  stream.buf = const_cast<uint8_t*>(compressedData);
  stream.buf_len = compressedSize;
  stream.is_read_stream = true;
  auto codec = grk_decompress_init(&stream, &parameters);
  if(!codec)
  {
    std::fprintf(stderr, "%s: grk_decompress_init failed\n", name);
    return false;
  }

  grk_header_info headerInfo = {};
  bool success = grk_decompress_read_header(codec, &headerInfo) && grk_decompress(codec, nullptr);
  auto image = success ? grk_decompress_get_image(codec) : nullptr;
  if(!image || image->numcomps != componentCount)
    success = false;

  for(uint16_t componentIndex = 0; success && componentIndex < image->numcomps; ++componentIndex)
  {
    auto component = image->comps + componentIndex;
    if(component->w != width || component->h != height || !component->data)
    {
      success = false;
      break;
    }
    if(expectedDecodeType && component->data_type != *expectedDecodeType)
    {
      std::fprintf(stderr, "%s: component %u decoded as type %d, expected %d\n", name,
                   componentIndex, (int)component->data_type, (int)*expectedDecodeType);
      success = false;
      break;
    }
    for(uint32_t y = 0; success && y < height; ++y)
      for(uint32_t x = 0; x < width; ++x)
      {
        size_t sampleIndex = (size_t)y * component->stride + x;
        int32_t actual = component->data_type == GRK_INT_16
                             ? static_cast<int16_t*>(component->data)[sampleIndex]
                             : static_cast<int32_t*>(component->data)[sampleIndex];
        int32_t expected = expectedSample(componentIndex, x, y);
        if(actual != expected)
        {
          std::fprintf(stderr, "%s: component %u sample (%u,%u) is %d, expected %d\n", name,
                       componentIndex, x, y, actual, expected);
          success = false;
          break;
        }
      }
  }

  if(!success)
    std::fprintf(stderr, "%s: decode failed or returned the wrong image\n", name);
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
  return decodedSamplesMatch(testCase.name, compressed.data(), compressedSize,
                             testCase.componentCount, imageWidth, imageHeight, std::nullopt,
                             [&](uint16_t componentIndex, uint32_t x, uint32_t y) {
                               return expectedSample(testCase, componentIndex, x, y);
                             });
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

  expect("reversible 13 bit five levels", 13, false, GRK_INT_16);
  expect("reversible 14 bit five levels", 14, false, GRK_INT_32);

  parameters.mct = 1;
  expect("reversible MCT component 12 bit five levels", 12, true, GRK_INT_16);
  expect("reversible MCT component 13 bit five levels", 13, true, GRK_INT_32);
  expect("reversible non-MCT component 13 bit five levels", 13, false, GRK_INT_16);
  parameters.mct = 0;

  parameters.numresolution = 7;
  expect("reversible 12 bit six levels", 12, false, GRK_INT_16);
  expect("reversible 13 bit six levels", 13, false, GRK_INT_32);
  parameters.mct = 1;
  expect("reversible MCT component 11 bit six levels", 11, true, GRK_INT_16);
  expect("reversible MCT component 12 bit six levels", 12, true, GRK_INT_32);
  parameters.mct = 0;
  parameters.numresolution = defaultResolutionCount;

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

constexpr uint32_t worstCaseSide = 512;

struct WorstCaseCase
{
  const char* name;
  uint8_t precision;
  uint16_t componentCount;
  uint8_t numResolutions;
  bool fitsInt16;
};

void linearAnalysis53(const std::vector<double>& signal, std::vector<double>& low,
                      std::vector<double>& high)
{
  size_t half = signal.size() / 2;
  low.resize(half);
  high.resize(half);
  for(size_t k = 0; k < half; ++k)
  {
    low[k] = signal[2 * k];
    high[k] = signal[2 * k + 1];
  }
  for(size_t k = 0; k < half; ++k)
    high[k] -= (low[k] + low[k + 1 < half ? k + 1 : k]) / 2;
  for(size_t k = 0; k < half; ++k)
    low[k] += (high[k > 0 ? k - 1 : 0] + high[k]) / 4;
}

std::vector<bool> worstCasePositiveWeights(uint8_t numLevels)
{
  std::vector<std::vector<double>> response(worstCaseSide);
  for(uint32_t position = 0; position < worstCaseSide; ++position)
  {
    std::vector<double> signal(worstCaseSide, 0.0), low, high;
    signal[position] = 1.0;
    for(uint8_t level = 0; level < numLevels; ++level)
    {
      linearAnalysis53(signal, low, high);
      signal = low;
    }
    response[position] = high;
  }
  size_t worstCoefficient = 0;
  double worstGain = 0;
  for(size_t coefficient = 0; coefficient < response[0].size(); ++coefficient)
  {
    double gain = 0;
    for(const auto& weights : response)
      gain += std::fabs(weights[coefficient]);
    if(gain > worstGain)
    {
      worstGain = gain;
      worstCoefficient = coefficient;
    }
  }
  std::vector<bool> positive(worstCaseSide);
  for(uint32_t position = 0; position < worstCaseSide; ++position)
    positive[position] = response[position][worstCoefficient] >= 0;
  return positive;
}

int32_t worstCaseSample(const WorstCaseCase& testCase, const std::vector<bool>& positive,
                        uint16_t componentIndex, uint32_t x, uint32_t y)
{
  int32_t maximum = (1 << testCase.precision) - 1;
  bool positiveWeight = positive[x] == positive[y];
  if(testCase.componentCount == 1)
    return positiveWeight ? maximum : 0;
  if(componentIndex == 0)
    return 1 << (testCase.precision - 1);
  // RCT chroma B - G is largest where blue and green take opposite extremes
  bool isBlue = componentIndex == 2;
  return positiveWeight == isBlue ? maximum : 0;
}

std::vector<uint8_t> compressWorstCase(const WorstCaseCase& testCase,
                                       const std::vector<bool>& positive, grk_data_type dataType)
{
  std::vector<grk_image_comp> componentParameters(testCase.componentCount);
  for(auto& component : componentParameters)
  {
    component.w = worstCaseSide;
    component.h = worstCaseSide;
    component.dx = 1;
    component.dy = 1;
    component.prec = testCase.precision;
    component.data_type = dataType;
  }
  auto image =
      grk_image_new(testCase.componentCount, componentParameters.data(),
                    testCase.componentCount == 3 ? GRK_CLRSPC_SRGB : GRK_CLRSPC_GRAY, true);
  if(!image)
    return {};
  for(uint16_t componentIndex = 0; componentIndex < testCase.componentCount; ++componentIndex)
  {
    auto component = image->comps + componentIndex;
    for(uint32_t y = 0; y < worstCaseSide; ++y)
      for(uint32_t x = 0; x < worstCaseSide; ++x)
      {
        size_t sampleIndex = (size_t)y * component->stride + x;
        int32_t sample = worstCaseSample(testCase, positive, componentIndex, x, y);
        if(dataType == GRK_INT_16)
          static_cast<int16_t*>(component->data)[sampleIndex] = (int16_t)sample;
        else
          static_cast<int32_t*>(component->data)[sampleIndex] = sample;
      }
  }

  std::vector<uint8_t> compressed(compressedCapacity);
  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.numlayers = 1;
  parameters.mct = testCase.componentCount == 3 ? 1 : 0;
  parameters.numresolution = testCase.numResolutions;
  // two guard bits cannot hold the largest coefficients of these images
  parameters.numgbits = 3;
  grk_stream_params stream = {};
  stream.buf = compressed.data();
  stream.buf_len = compressed.size();
  auto codec = grk_compress_init(&stream, &parameters, image);
  size_t compressedSize = codec ? grk_compress(codec, nullptr) : 0;
  if(codec)
    grk_object_unref(codec);
  grk_object_unref(&image->obj);
  compressed.resize(compressedSize);
  return compressed;
}

bool runWorstCase(const WorstCaseCase& testCase)
{
  auto positive = worstCasePositiveWeights((uint8_t)(testCase.numResolutions - 1));
  auto fromInt32 = compressWorstCase(testCase, positive, GRK_INT_32);
  auto fromInt16 = compressWorstCase(testCase, positive, GRK_INT_16);
  if(fromInt32.empty() || fromInt16.empty())
  {
    std::fprintf(stderr, "%s: compression failed\n", testCase.name);
    return false;
  }
  if(fromInt32 != fromInt16)
  {
    std::fprintf(stderr, "%s: int16 and int32 sources compress differently\n", testCase.name);
    return false;
  }
  return decodedSamplesMatch(testCase.name, fromInt32.data(), fromInt32.size(),
                             testCase.componentCount, worstCaseSide, worstCaseSide,
                             testCase.fitsInt16 ? GRK_INT_16 : GRK_INT_32,
                             [&](uint16_t componentIndex, uint32_t x, uint32_t y) {
                               return worstCaseSample(testCase, positive, componentIndex, x, y);
                             });
}

uint16_t readBigEndian16(const std::vector<uint8_t>& bytes, size_t offset)
{
  return (uint16_t)((bytes[offset] << 8) | bytes[offset + 1]);
}

const uint16_t markerCod = 0xFF52;
const uint16_t markerQcd = 0xFF5C;
const uint16_t markerTlm = 0xFF55;
const uint16_t markerSot = 0xFF90;

std::vector<uint8_t> moveCodingStyleToTile(const std::vector<uint8_t>& stream)
{
  std::vector<uint8_t> mainHeader(stream.begin(), stream.begin() + 2);
  std::vector<uint8_t> tileMarkers;
  size_t position = 2;
  while(readBigEndian16(stream, position) != markerSot)
  {
    uint16_t marker = readBigEndian16(stream, position);
    size_t segmentEnd = position + 2 + readBigEndian16(stream, position + 2);
    std::vector<uint8_t> segment(stream.begin() + (long)position,
                                 stream.begin() + (long)segmentEnd);
    position = segmentEnd;
    if(marker == markerTlm)
      return {};
    if(marker != markerCod && marker != markerQcd)
    {
      mainHeader.insert(mainHeader.end(), segment.begin(), segment.end());
      continue;
    }
    tileMarkers.insert(tileMarkers.end(), segment.begin(), segment.end());
    if(marker == markerCod)
    {
      bool hasPrecinctSizes = segment[4] & 1;
      if(hasPrecinctSizes)
        return {};
      segment[9]--;
    }
    else
    {
      // reversible QCD stores one exponent byte per subband, three per level
      segment.resize(segment.size() - 3);
      segment[3] = (uint8_t)(segment[3] - 3);
    }
    mainHeader.insert(mainHeader.end(), segment.begin(), segment.end());
  }
  const size_t sotSegmentLength = 12;
  std::vector<uint8_t> result = mainHeader;
  result.insert(result.end(), stream.begin() + (long)position,
                stream.begin() + (long)(position + sotSegmentLength));
  size_t tilePartLengthOffset = result.size() - 6;
  uint32_t tilePartLength = ((uint32_t)result[tilePartLengthOffset] << 24) |
                            ((uint32_t)result[tilePartLengthOffset + 1] << 16) |
                            ((uint32_t)result[tilePartLengthOffset + 2] << 8) |
                            result[tilePartLengthOffset + 3];
  tilePartLength += (uint32_t)tileMarkers.size();
  for(int byteIndex = 0; byteIndex < 4; ++byteIndex)
    result[tilePartLengthOffset + (size_t)byteIndex] =
        (uint8_t)(tilePartLength >> (24 - 8 * byteIndex));
  result.insert(result.end(), tileMarkers.begin(), tileMarkers.end());
  result.insert(result.end(), stream.begin() + (long)(position + sotSegmentLength), stream.end());
  return result;
}

bool decompressSucceeds(std::vector<uint8_t>& stream)
{
  grk_decompress_parameters parameters = {};
  grk_stream_params streamParameters = {};
  streamParameters.buf = stream.data();
  streamParameters.buf_len = stream.size();
  streamParameters.is_read_stream = true;
  auto codec = grk_decompress_init(&streamParameters, &parameters);
  if(!codec)
    return false;
  grk_header_info headerInfo = {};
  bool success = grk_decompress_read_header(codec, &headerInfo) && grk_decompress(codec, nullptr);
  grk_object_unref(codec);
  return success;
}

bool tileLevelOverrideIsChecked()
{
  const WorstCaseCase fitsBothCase = {"tile override 12 bit gray", 12, 1, 7, true};
  const WorstCaseCase exceedsTileCase = {"tile override 13 bit gray", 13, 1, 7, false};
  bool success = true;
  for(const char* mercury : {"0", "1"})
    for(const auto* testCase : {&fitsBothCase, &exceedsTileCase})
    {
#if defined(_WIN32)
      _putenv_s("GRK_MERCURY", mercury);
#else
      setenv("GRK_MERCURY", mercury, 1);
#endif
      auto positive = worstCasePositiveWeights((uint8_t)(testCase->numResolutions - 1));
      auto stream = moveCodingStyleToTile(compressWorstCase(*testCase, positive, GRK_INT_32));
      if(stream.empty())
      {
        std::fprintf(stderr, "%s: could not move COD and QCD into the tile\n", testCase->name);
        success = false;
        continue;
      }
      if(testCase->fitsInt16)
      {
        success &= decodedSamplesMatch(
            testCase->name, stream.data(), stream.size(), 1, worstCaseSide, worstCaseSide,
            GRK_INT_16, [&](uint16_t componentIndex, uint32_t x, uint32_t y) {
              return worstCaseSample(*testCase, positive, componentIndex, x, y);
            });
        continue;
      }
      if(decompressSucceeds(stream))
      {
        std::fprintf(stderr, "%s: decode exceeding int16 in one tile was not rejected\n",
                     testCase->name);
        success = false;
      }
    }
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "0");
#else
  setenv("GRK_MERCURY", "0", 1);
#endif
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

  const WorstCaseCase worstCases[] = {
      {"worst case 13 bit gray five levels", 13, 1, 6, true},
      {"worst case 14 bit gray five levels", 14, 1, 6, false},
      {"worst case 12 bit RCT five levels", 12, 3, 6, true},
      {"worst case 13 bit RCT five levels", 13, 3, 6, false},
      {"worst case 12 bit gray six levels", 12, 1, 7, true},
      {"worst case 13 bit gray six levels", 13, 1, 7, false},
      {"worst case 11 bit RCT six levels", 11, 3, 7, true},
      {"worst case 12 bit RCT six levels", 12, 3, 7, false},
  };
  for(const auto& testCase : worstCases)
    if(!runWorstCase(testCase))
      ++failures;
  if(!tileLevelOverrideIsChecked())
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
