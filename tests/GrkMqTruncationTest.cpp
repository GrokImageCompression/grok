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
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

extern "C" int32_t mercury_grok_t1_encode(const int32_t* samples, int32_t num_cols,
                                          int32_t num_rows, uint32_t max_magnitude,
                                          int32_t orientation, int32_t compno, int32_t level,
                                          double stepsize, int32_t mct, uint16_t early_stop_slope,
                                          const uint8_t** coded_data, uint16_t* pass_rates,
                                          double* pass_distortions, uint8_t* pass_terminated,
                                          int32_t* numbps);
extern "C" const int32_t* mercury_grok_t1_decode(const uint8_t* coded_data, int32_t coded_length,
                                                 int32_t num_passes, int32_t missing_msbs,
                                                 int32_t k_max_prime, int32_t orientation,
                                                 int32_t modes, int32_t num_cols, int32_t num_rows,
                                                 const int32_t* segment_lengths,
                                                 int32_t num_segments);

namespace
{
constexpr int32_t fractionBits = 6;
constexpr int32_t maxPasses = 3 * 32;
constexpr uint32_t blocksPerShape = 40;

struct BlockShape
{
  int32_t width;
  int32_t height;
  double scale;
  double zeroFraction;
};

std::vector<int32_t> decode(const std::vector<uint8_t>& coded, int32_t length, int32_t numPasses,
                            int32_t numbps, int32_t orientation, const BlockShape& shape)
{
  const int32_t* samples =
      mercury_grok_t1_decode(coded.data(), length, numPasses, 0, numbps, orientation, 0,
                             shape.width, shape.height, &length, 1);
  if(!samples)
    return {};
  return std::vector<int32_t>(samples, samples + (size_t)shape.width * shape.height);
}

struct ShapeResult
{
  bool valid = true;
  uint32_t checkedPasses = 0;
  uint32_t shorterLengthsThatFail = 0;
};

ShapeResult checkShape(const BlockShape& shape, std::mt19937& random)
{
  ShapeResult result;
  std::exponential_distribution<double> magnitude(1.0 / shape.scale);
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  std::vector<int32_t> samples((size_t)shape.width * shape.height);
  for(uint32_t block = 0; block < blocksPerShape; ++block)
  {
    int32_t orientation = (int32_t)(block % 4);
    uint32_t maxMagnitude = 0;
    for(auto& sample : samples)
    {
      uint32_t value = unit(random) < shape.zeroFraction
                           ? 0
                           : (uint32_t)std::lround(magnitude(random) * (double)(1 << fractionBits));
      maxMagnitude = std::max(maxMagnitude, value);
      sample = (int32_t)(value | (unit(random) < 0.5 ? 0x80000000U : 0U));
    }

    const uint8_t* codedData = nullptr;
    uint16_t rates[maxPasses];
    double distortions[maxPasses];
    uint8_t terminated[maxPasses];
    int32_t numbps = 0;
    int32_t numPasses = mercury_grok_t1_encode(samples.data(), shape.width, shape.height,
                                               maxMagnitude, orientation, 0, 0, 1.0, 0, 0,
                                               &codedData, rates, distortions, terminated, &numbps);
    if(numPasses <= 1)
      continue;
    int32_t fullLength = rates[numPasses - 1];
    std::vector<uint8_t> coded(codedData, codedData + fullLength);

    for(int32_t pass = 0; pass + 1 < numPasses; ++pass)
    {
      if(terminated[pass])
        continue;
      auto expected = decode(coded, fullLength, pass + 1, numbps, orientation, shape);
      auto truncated = decode(coded, rates[pass], pass + 1, numbps, orientation, shape);
      if(expected.empty() || truncated != expected)
      {
        std::fprintf(stderr,
                     "%dx%d block %u pass %d: %u of %d bytes decodes differently from the full "
                     "block\n",
                     shape.width, shape.height, block, pass, rates[pass], fullLength);
        result.valid = false;
        return result;
      }
      result.checkedPasses++;
      if(rates[pass] > 1 &&
         decode(coded, rates[pass] - 1, pass + 1, numbps, orientation, shape) != expected)
        result.shorterLengthsThatFail++;
    }
  }
  return result;
}
} // namespace

int main()
{
  const BlockShape shapes[] = {
      {64, 64, 4.0, 0.2},  {32, 32, 2.0, 0.5}, {16, 16, 1.0, 0.7},
      {16, 16, 40.0, 0.0}, {8, 64, 0.5, 0.9},  {64, 4, 8.0, 0.3},
  };
  std::mt19937 random(20261121);
  bool valid = true;
  uint32_t checkedPasses = 0;
  uint32_t shorterLengthsThatFail = 0;
  for(const auto& shape : shapes)
  {
    auto result = checkShape(shape, random);
    valid = valid && result.valid;
    checkedPasses += result.checkedPasses;
    shorterLengthsThatFail += result.shorterLengthsThatFail;
  }
  // padded lengths decode correctly one byte shorter as well
  if(valid && shorterLengthsThatFail * 2 < checkedPasses)
  {
    std::fprintf(stderr, "only %u of %u pass lengths are the shortest that decodes correctly\n",
                 shorterLengthsThatFail, checkedPasses);
    valid = false;
  }
  return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
