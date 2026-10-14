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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" void grk_classic_forward_ict(int32_t* red, int32_t* green, int32_t* blue, int32_t dcShift,
                                        uint64_t samples);
extern "C" void mercury_hwy_forward_ict(const int32_t* red, const int32_t* green,
                                        const int32_t* blue, int32_t dc_shift, float* luma,
                                        float* chroma_blue, float* chroma_red, int samples);

namespace
{
// whole vectors for the widest dispatched target, so classic's loop never reads past a buffer
const size_t VECTOR_SAMPLES = 16;
const size_t BUFFER_ALIGNMENT = 64;
const int32_t TWELVE_BIT_MAX = 4095;
const int32_t TWELVE_BIT_SHIFT = -2048;
const size_t RANDOM_ROWS = 256;
const size_t RANDOM_ROW_WIDTH = 4096;
const uint32_t TAIL_WIDTHS = 67;

uint64_t nextRandom(uint64_t& state)
{
  state = state * 6364136223846793005ULL + 1442695040888963407ULL;
  return state >> 33;
}

struct AlignedPlane
{
  explicit AlignedPlane(size_t samples)
  {
    size_t bytes = (samples * 4 + BUFFER_ALIGNMENT - 1) / BUFFER_ALIGNMENT * BUFFER_ALIGNMENT;
    data = static_cast<int32_t*>(std::aligned_alloc(BUFFER_ALIGNMENT, bytes));
    std::memset(data, 0, bytes);
  }
  ~AlignedPlane()
  {
    std::free(data);
  }
  int32_t* data;
};

// one row: mercury's kernel against classic's loop, every float compared bit for bit
bool rowMatches(const int32_t* red, const int32_t* green, const int32_t* blue, size_t width)
{
  size_t padded = (width + VECTOR_SAMPLES - 1) / VECTOR_SAMPLES * VECTOR_SAMPLES;
  AlignedPlane classic[3] = {AlignedPlane(padded), AlignedPlane(padded), AlignedPlane(padded)};
  std::memcpy(classic[0].data, red, width * 4);
  std::memcpy(classic[1].data, green, width * 4);
  std::memcpy(classic[2].data, blue, width * 4);
  grk_classic_forward_ict(classic[0].data, classic[1].data, classic[2].data, TWELVE_BIT_SHIFT,
                          padded);
  std::vector<float> mercury[3] = {std::vector<float>(width), std::vector<float>(width),
                                   std::vector<float>(width)};
  mercury_hwy_forward_ict(red, green, blue, TWELVE_BIT_SHIFT, mercury[0].data(), mercury[1].data(),
                          mercury[2].data(), (int)width);
  for(int c = 0; c < 3; ++c)
  {
    for(size_t x = 0; x < width; ++x)
    {
      uint32_t expected, actual;
      std::memcpy(&expected, classic[c].data + x, 4);
      std::memcpy(&actual, mercury[c].data() + x, 4);
      if(expected == actual)
        continue;
      fprintf(stderr, "width %zu component %d sample %zu: rgb %d %d %d classic %08x mercury %08x\n",
              width, c, x, red[x], green[x], blue[x], expected, actual);
      return false;
    }
  }
  return true;
}
} // namespace

int main(void)
{
  uint64_t state = 1;
  size_t rows = 0;
  std::vector<int32_t> red(RANDOM_ROW_WIDTH), green(RANDOM_ROW_WIDTH), blue(RANDOM_ROW_WIDTH);
  for(size_t row = 0; row < RANDOM_ROWS; ++row)
  {
    for(size_t x = 0; x < RANDOM_ROW_WIDTH; ++x)
    {
      red[x] = (int32_t)(nextRandom(state) % (TWELVE_BIT_MAX + 1));
      green[x] = (int32_t)(nextRandom(state) % (TWELVE_BIT_MAX + 1));
      blue[x] = (int32_t)(nextRandom(state) % (TWELVE_BIT_MAX + 1));
    }
    if(!rowMatches(red.data(), green.data(), blue.data(), RANDOM_ROW_WIDTH))
      return EXIT_FAILURE;
    ++rows;
  }
  // every combination of the extremes, at each tail width
  const int32_t extremes[] = {0, 1, 2047, 2048, TWELVE_BIT_MAX};
  for(uint32_t width = 1; width <= TAIL_WIDTHS; ++width)
  {
    for(size_t x = 0; x < width; ++x)
    {
      size_t pick = x + width;
      red[x] = extremes[pick % 5];
      green[x] = extremes[(pick / 5) % 5];
      blue[x] = extremes[(pick / 25) % 5];
    }
    if(!rowMatches(red.data(), green.data(), blue.data(), width))
      return EXIT_FAILURE;
    ++rows;
  }
  printf("forward ICT identical on %zu rows\n", rows);
  return EXIT_SUCCESS;
}
