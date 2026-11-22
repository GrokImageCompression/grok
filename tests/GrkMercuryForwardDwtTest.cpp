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
#include <iterator>
#include <new>
#include <vector>

extern "C" int32_t grk_classic_forward_dwt_tile(bool reversible, void* data, uint32_t stride,
                                                const uint32_t* bounds, uint32_t levels,
                                                int32_t dcShift, bool intInput);
extern "C" int32_t grk_classic_forward_dwt_tile_16(int16_t* data, uint32_t stride,
                                                   const uint32_t* bounds, uint32_t levels,
                                                   int16_t dcShift, uint8_t fractionBits);
extern "C" int32_t grk_classic_forward97_int16_level_factor(uint8_t level);
extern "C" int32_t mercury_forward_dwt_tile_16(const int16_t* input, int16_t* output,
                                               uint32_t stride, const uint32_t* bounds,
                                               uint32_t levels, int16_t dcShift,
                                               uint32_t fractionBits, const int32_t* levelFactors);
extern "C" int32_t mercury_forward_dwt_tile(bool reversible, const void* input, void* output,
                                            uint32_t stride, const uint32_t* bounds,
                                            uint32_t levels, int32_t dcShift, bool intInput);

namespace
{
const uint32_t STRIDE_MULTIPLE = 16;
const size_t BUFFER_ALIGNMENT = 64;
const int32_t TWELVE_BIT_SHIFT = 2048;
const int32_t EIGHT_BIT_SHIFT = 128;
const int32_t TEN_BIT_SHIFT = 512;
// grok's wavelet97Int16FractionBits at 8 and 10 bits
const uint8_t EIGHT_BIT_FRACTION_BITS = 5;
const uint8_t TEN_BIT_FRACTION_BITS = 3;
const uint32_t MAX_LEVELS = 32;

enum class Mode
{
  FloatInput97,
  IntInput97,
  Reversible53,
  Int16EightBit97,
  Int16TenBit97
};

struct Geometry
{
  uint32_t x0, y0, width, height, levels;
};

uint64_t nextRandom(uint64_t& state)
{
  state = state * 6364136223846793005ULL + 1442695040888963407ULL;
  return state >> 33;
}

struct AlignedBuffer
{
  explicit AlignedBuffer(size_t samples)
  {
    size_t bytes = (samples * 4 + BUFFER_ALIGNMENT - 1) / BUFFER_ALIGNMENT * BUFFER_ALIGNMENT;
    data = ::operator new(bytes, std::align_val_t(BUFFER_ALIGNMENT));
    std::memset(data, 0, bytes);
  }
  ~AlignedBuffer()
  {
    ::operator delete(data, std::align_val_t(BUFFER_ALIGNMENT));
  }
  void* data;
};

bool runInt16Case(Mode mode, const Geometry& g, uint64_t seed)
{
  const uint32_t stride = (g.width + STRIDE_MULTIPLE - 1) / STRIDE_MULTIPLE * STRIDE_MULTIPLE;
  const size_t samples = (size_t)stride * g.height + STRIDE_MULTIPLE;
  const bool eightBit = mode == Mode::Int16EightBit97;
  const int32_t shift = eightBit ? EIGHT_BIT_SHIFT : TEN_BIT_SHIFT;
  const uint8_t fractionBits = eightBit ? EIGHT_BIT_FRACTION_BITS : TEN_BIT_FRACTION_BITS;
  AlignedBuffer classic(samples);
  AlignedBuffer input(samples);
  AlignedBuffer mercury(samples);
  auto classicSamples = (int16_t*)classic.data;
  uint64_t state = seed;
  for(uint32_t y = 0; y < g.height; ++y)
    for(uint32_t x = 0; x < g.width; ++x)
      classicSamples[(size_t)y * stride + x] = (int16_t)(nextRandom(state) % (uint64_t)(2 * shift));
  std::memcpy(input.data, classic.data, samples * sizeof(int16_t));
  int32_t levelFactors[MAX_LEVELS];
  for(uint32_t level = 0; level < g.levels; ++level)
    levelFactors[level] = grk_classic_forward97_int16_level_factor((uint8_t)level);

  const uint32_t bounds[4] = {g.x0, g.y0, g.x0 + g.width, g.y0 + g.height};
  grk_classic_forward_dwt_tile_16(classicSamples, stride, bounds, g.levels, (int16_t)shift,
                                  fractionBits);
  if(mercury_forward_dwt_tile_16((const int16_t*)input.data, (int16_t*)mercury.data, stride, bounds,
                                 g.levels, (int16_t)shift, fractionBits, levelFactors) != 0)
  {
    fprintf(stderr, "mercury_forward_dwt_tile_16 failed\n");
    return false;
  }
  auto mercurySamples = (const int16_t*)mercury.data;
  for(uint32_t y = 0; y < g.height; ++y)
  {
    for(uint32_t x = 0; x < g.width; ++x)
    {
      size_t i = (size_t)y * stride + x;
      if(classicSamples[i] == mercurySamples[i])
        continue;
      fprintf(stderr,
              "mode %d tile (%u,%u) %ux%u levels %u: sample (%u,%u) classic %d mercury %d\n",
              (int)mode, g.x0, g.y0, g.width, g.height, g.levels, x, y, classicSamples[i],
              mercurySamples[i]);
      return false;
    }
  }
  return true;
}

bool runCase(Mode mode, const Geometry& g, uint64_t seed)
{
  const uint32_t stride = (g.width + STRIDE_MULTIPLE - 1) / STRIDE_MULTIPLE * STRIDE_MULTIPLE;
  const size_t samples = (size_t)stride * g.height + STRIDE_MULTIPLE;
  AlignedBuffer classic(samples);
  AlignedBuffer input(samples);
  AlignedBuffer mercury(samples);
  const bool reversible = mode == Mode::Reversible53;
  const bool intInput = mode == Mode::IntInput97;
  const int32_t shift = reversible ? EIGHT_BIT_SHIFT : (intInput ? TWELVE_BIT_SHIFT : 0);
  const int32_t range = 2 * (reversible ? EIGHT_BIT_SHIFT : TWELVE_BIT_SHIFT);
  uint64_t state = seed;
  for(uint32_t y = 0; y < g.height; ++y)
  {
    for(uint32_t x = 0; x < g.width; ++x)
    {
      size_t i = (size_t)y * stride + x;
      int32_t value = (int32_t)(nextRandom(state) % (uint64_t)range);
      if(mode == Mode::FloatInput97)
      {
        // post-ICT samples are centred and fractional
        float sample =
            (float)(value - TWELVE_BIT_SHIFT) + (float)(nextRandom(state) % 1024) / 1024.0f;
        ((float*)classic.data)[i] = sample;
      }
      else
      {
        ((int32_t*)classic.data)[i] = value;
      }
    }
  }
  std::memcpy(input.data, classic.data, samples * 4);

  const uint32_t bounds[4] = {g.x0, g.y0, g.x0 + g.width, g.y0 + g.height};
  grk_classic_forward_dwt_tile(reversible, classic.data, stride, bounds, g.levels, shift, intInput);
  if(mercury_forward_dwt_tile(reversible, input.data, mercury.data, stride, bounds, g.levels, shift,
                              intInput) != 0)
  {
    fprintf(stderr, "mercury_forward_dwt_tile failed\n");
    return false;
  }
  for(uint32_t y = 0; y < g.height; ++y)
  {
    for(uint32_t x = 0; x < g.width; ++x)
    {
      size_t i = (size_t)y * stride + x;
      uint32_t expected, actual;
      std::memcpy(&expected, (uint32_t*)classic.data + i, 4);
      std::memcpy(&actual, (uint32_t*)mercury.data + i, 4);
      if(expected == actual)
        continue;
      fprintf(stderr,
              "mode %d tile (%u,%u) %ux%u levels %u: sample (%u,%u) classic %08x mercury %08x\n",
              (int)mode, g.x0, g.y0, g.width, g.height, g.levels, x, y, expected, actual);
      if(!reversible)
      {
        float e, a;
        std::memcpy(&e, &expected, 4);
        std::memcpy(&a, &actual, 4);
        fprintf(stderr, "  as float: classic %.9g mercury %.9g\n", e, a);
      }
      return false;
    }
  }
  return true;
}
} // namespace

int main(void)
{
  std::vector<Geometry> geometries;
  for(uint32_t levels = 1; levels <= 3; ++levels)
    for(uint32_t offset = 0; offset < 4; ++offset)
      for(uint32_t width = 1; width <= 9; ++width)
        for(uint32_t height = 1; height <= 9; ++height)
          geometries.push_back({offset, (offset * 3) % 4, width, height, levels});
  geometries.push_back({3, 5, 61, 69, 5});
  geometries.push_back({0, 0, 2048, 1080, 5});
  geometries.push_back({0, 0, 4096, 2160, 6});

  const Mode modes[] = {Mode::FloatInput97, Mode::IntInput97, Mode::Reversible53,
                        Mode::Int16EightBit97, Mode::Int16TenBit97};
  uint64_t seed = 1;
  size_t failures = 0;
  for(auto mode : modes)
  {
    bool int16 = mode == Mode::Int16EightBit97 || mode == Mode::Int16TenBit97;
    for(auto& g : geometries)
    {
      bool identical = int16 ? runInt16Case(mode, g, seed++) : runCase(mode, g, seed++);
      if(!identical && ++failures >= 10)
        return EXIT_FAILURE;
    }
  }
  if(failures)
    return EXIT_FAILURE;
  printf("forward DWT identical on %zu tiles\n", geometries.size() * std::size(modes));
  return EXIT_SUCCESS;
}
