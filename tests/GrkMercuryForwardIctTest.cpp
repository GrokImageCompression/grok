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
#include <new>
#include <vector>

#include "grok.h"

namespace grk
{
bool applyXYZTransform(grk_image* image, uint8_t targetPrec);
}

struct MercuryXyzTables;
extern "C" MercuryXyzTables* mercury_xyz_tables_new(uint32_t precision, uint32_t out_precision);
extern "C" void mercury_xyz_tables_free(MercuryXyzTables* tables);
extern "C" void mercury_hwy_forward_xyz_ict(const MercuryXyzTables* tables, const int32_t* red,
                                            const int32_t* green, const int32_t* blue,
                                            int32_t dc_shift, float* luma, float* chroma_blue,
                                            float* chroma_red, int samples);
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
const uint8_t CINEMA_PRECISION = 12;
const uint8_t SOURCE_PRECISIONS[] = {16, 12, 8};

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
    data = static_cast<int32_t*>(::operator new(bytes, std::align_val_t(BUFFER_ALIGNMENT)));
    std::memset(data, 0, bytes);
  }
  ~AlignedPlane()
  {
    ::operator delete(data, std::align_val_t(BUFFER_ALIGNMENT));
  }
  int32_t* data;
};

// one row: mercury's kernel against classic's loop, every float compared bit for bit
bool rowMatches(const int32_t* red, const int32_t* green, const int32_t* blue, size_t width,
                const MercuryXyzTables* xyzTables = nullptr, uint8_t sourcePrecision = 0)
{
  size_t padded = (width + VECTOR_SAMPLES - 1) / VECTOR_SAMPLES * VECTOR_SAMPLES;
  AlignedPlane classic[3] = {AlignedPlane(padded), AlignedPlane(padded), AlignedPlane(padded)};
  std::memcpy(classic[0].data, red, width * 4);
  std::memcpy(classic[1].data, green, width * 4);
  std::memcpy(classic[2].data, blue, width * 4);
  if(xyzTables)
  {
    grk_image_comp comps[3] = {};
    for(auto& comp : comps)
    {
      comp.w = (uint32_t)width;
      comp.h = 1;
      comp.dx = 1;
      comp.dy = 1;
      comp.prec = sourcePrecision;
    }
    auto image = grk_image_new(3, comps, GRK_CLRSPC_SRGB, true);
    for(int c = 0; c < 3; ++c)
      std::memcpy(image->comps[c].data, classic[c].data, width * 4);
    bool transformed = grk::applyXYZTransform(image, CINEMA_PRECISION);
    for(int c = 0; c < 3; ++c)
      std::memcpy(classic[c].data, image->comps[c].data, width * 4);
    grk_object_unref(&image->obj);
    if(!transformed)
      return false;
  }
  grk_classic_forward_ict(classic[0].data, classic[1].data, classic[2].data, TWELVE_BIT_SHIFT,
                          padded);
  std::vector<float> mercury[3] = {std::vector<float>(width), std::vector<float>(width),
                                   std::vector<float>(width)};
  if(xyzTables)
    mercury_hwy_forward_xyz_ict(xyzTables, red, green, blue, TWELVE_BIT_SHIFT, mercury[0].data(),
                                mercury[1].data(), mercury[2].data(), (int)width);
  else
    mercury_hwy_forward_ict(red, green, blue, TWELVE_BIT_SHIFT, mercury[0].data(),
                            mercury[1].data(), mercury[2].data(), (int)width);
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

  grk_initialize(nullptr, 1, nullptr);
  size_t xyzRows = 0;
  for(uint8_t precision : SOURCE_PRECISIONS)
  {
    auto tables = mercury_xyz_tables_new(precision, CINEMA_PRECISION);
    const int32_t maxValue = (1 << precision) - 1;
    for(size_t row = 0; row < RANDOM_ROWS; ++row)
    {
      for(size_t x = 0; x < RANDOM_ROW_WIDTH; ++x)
      {
        red[x] = (int32_t)(nextRandom(state) % (maxValue + 1));
        green[x] = (int32_t)(nextRandom(state) % (maxValue + 1));
        blue[x] = (int32_t)(nextRandom(state) % (maxValue + 1));
      }
      if(!rowMatches(red.data(), green.data(), blue.data(), RANDOM_ROW_WIDTH, tables, precision))
        return EXIT_FAILURE;
      ++xyzRows;
    }
    // out of range samples clamp like classic's unsigned read
    const int32_t xyzExtremes[] = {0, 1, maxValue / 2, maxValue, -1};
    for(uint32_t width = 1; width <= TAIL_WIDTHS; ++width)
    {
      for(size_t x = 0; x < width; ++x)
      {
        size_t pick = x + width;
        red[x] = xyzExtremes[pick % 5];
        green[x] = xyzExtremes[(pick / 5) % 5];
        blue[x] = xyzExtremes[(pick / 25) % 5];
      }
      if(!rowMatches(red.data(), green.data(), blue.data(), width, tables, precision))
        return EXIT_FAILURE;
      ++xyzRows;
    }
    mercury_xyz_tables_free(tables);
  }
  printf("forward XYZ and ICT identical on %zu rows\n", xyzRows);
  grk_deinitialize();
  return EXIT_SUCCESS;
}
