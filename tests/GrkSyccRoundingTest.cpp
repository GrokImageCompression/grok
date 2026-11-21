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
#include <vector>

#include "GrkImageSIMD.h"
#include "SyccToRGB.h"

namespace
{
int g_failures = 0;
const int maxReportedFailures = 10;

// compare the simd path against the scalar path over one plane of y/cb/cr samples
void compareOnePlane(const char* label, uint8_t precision, uint32_t width, uint32_t height,
                     const std::vector<int32_t>& y, const std::vector<int32_t>& cb,
                     const std::vector<int32_t>& cr)
{
  const int32_t offset = (int32_t)1 << (precision - 1);
  const int32_t upperBound = ((int32_t)1 << precision) - 1;
  const size_t sampleCount = (size_t)width * height;

  std::vector<int32_t> simdRed(sampleCount), simdGreen(sampleCount), simdBlue(sampleCount);
  grk::hwy_sycc444_to_rgb_i32(y.data(), cb.data(), cr.data(), simdRed.data(), simdGreen.data(),
                              simdBlue.data(), width, height, width, width, offset, upperBound);

  for(size_t index = 0; index < sampleCount; ++index)
  {
    int32_t red = 0, green = 0, blue = 0;
    grk::sycc_to_rgb<int32_t>(offset, upperBound, y[index], cb[index], cr[index], &red, &green,
                              &blue);
    if(red == simdRed[index] && green == simdGreen[index] && blue == simdBlue[index])
      continue;
    ++g_failures;
    if(g_failures <= maxReportedFailures)
      std::fprintf(stderr, "FAIL %s prec %u: y %d cb %d cr %d scalar (%d,%d,%d) simd (%d,%d,%d)\n",
                   label, precision, y[index], cb[index], cr[index], red, green, blue,
                   simdRed[index], simdGreen[index], simdBlue[index]);
  }
}

// every luma, chroma blue and chroma red combination at 8 bits
void sweepFullEightBit(void)
{
  const uint32_t range = 256;
  std::vector<int32_t> y(range * range), cb(range * range), cr(range * range);
  for(uint32_t luma = 0; luma < range; ++luma)
  {
    for(uint32_t row = 0; row < range; ++row)
    {
      for(uint32_t column = 0; column < range; ++column)
      {
        size_t index = (size_t)row * range + column;
        y[index] = (int32_t)luma;
        cb[index] = (int32_t)row;
        cr[index] = (int32_t)column;
      }
    }
    compareOnePlane("full 8 bit sweep", 8, range, range, y, cb, cr);
  }
}

// a strided sweep at a higher precision, on a width that leaves a partial simd vector
void sweepHigherPrecision(uint8_t precision, int32_t step)
{
  const int32_t sampleMax = ((int32_t)1 << precision) - 1;
  const uint32_t width = 253;
  std::vector<int32_t> y, cb, cr;
  for(int32_t luma = 0; luma <= sampleMax; luma += step)
  {
    for(int32_t chromaBlue = 0; chromaBlue <= sampleMax; chromaBlue += step)
    {
      for(int32_t chromaRed = 0; chromaRed <= sampleMax; chromaRed += step)
      {
        y.push_back(luma);
        cb.push_back(chromaBlue);
        cr.push_back(chromaRed);
      }
    }
  }
  // the extremes are where clamping and sign changes meet
  const int32_t extremes[] = {0, 1, sampleMax / 2, sampleMax / 2 + 1, sampleMax - 1, sampleMax};
  for(int32_t luma : extremes)
  {
    for(int32_t chromaBlue : extremes)
    {
      for(int32_t chromaRed : extremes)
      {
        y.push_back(luma);
        cb.push_back(chromaBlue);
        cr.push_back(chromaRed);
      }
    }
  }
  while(y.size() % width)
  {
    y.push_back(sampleMax / 2);
    cb.push_back(sampleMax / 2);
    cr.push_back(sampleMax / 2);
  }
  char label[64];
  std::snprintf(label, sizeof(label), "%u bit strided sweep", precision);
  compareOnePlane(label, precision, width, (uint32_t)(y.size() / width), y, cb, cr);
}
} // namespace

int main(void)
{
  sweepFullEightBit();
  sweepHigherPrecision(12, 37);
  sweepHigherPrecision(16, 601);

  if(g_failures)
  {
    std::fprintf(stderr, "grk_sycc_rounding_test: %d mismatches between scalar and simd\n",
                 g_failures);
    return 1;
  }
  std::printf("grk_sycc_rounding_test: passed\n");
  return 0;
}
