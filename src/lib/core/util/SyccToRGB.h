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

#pragma once

#include <algorithm>
#include <cstdint>

namespace grk
{
/*--------------------------------------------------------
Matrix for sYCC, Amendment 1 to IEC 61966-2-1

Y  |  0.299   0.587    0.114  |    R
Cb | -0.1687 -0.3312   0.5    | x  G
Cr |  0.5    -0.4187  -0.0812 |    B

Inverse:

R   |1        -3.68213e-05    1.40199     |    Y
G = |1.00003  -0.344125      -0.714128    | x  Cb - 2^(prec - 1)
B   |0.999823  1.77204       -8.04142e-06 |    Cr - 2^(prec - 1)

-----------------------------------------------------------*/

// the inverse coefficients as integers scaled by 2^syccFractionalBits. a coefficient times a
// chroma sample of at most 16 bits is exact in int64 and in double alike, so this scalar path
// and the simd path in GrkImageSIMD.cpp round to nearest on identical values
constexpr int syccFractionalBits = 30;
constexpr double syccCoefficientScale = (double)((int64_t)1 << syccFractionalBits);
constexpr int64_t syccRoundingOffset = (int64_t)1 << (syccFractionalBits - 1);
constexpr int32_t syccCrToRed = (int32_t)(1.402 * syccCoefficientScale + 0.5);
constexpr int32_t syccCbToGreen = (int32_t)(0.344 * syccCoefficientScale + 0.5);
constexpr int32_t syccCrToGreen = (int32_t)(0.714 * syccCoefficientScale + 0.5);
constexpr int32_t syccCbToBlue = (int32_t)(1.772 * syccCoefficientScale + 0.5);

inline int32_t syccRoundToNearest(int64_t scaled)
{
  return (int32_t)((scaled + syccRoundingOffset) >> syccFractionalBits);
}

template<typename T>
void sycc_to_rgb(T offset, T upb, T y, T cb, T cr, T* out_r, T* out_g, T* out_b)
{
  const int32_t luma = (int32_t)y;
  const int32_t chromaBlue = (int32_t)cb - (int32_t)offset;
  const int32_t chromaRed = (int32_t)cr - (int32_t)offset;
  const int32_t upper = (int32_t)upb;

  int32_t red = luma + syccRoundToNearest((int64_t)syccCrToRed * chromaRed);
  int32_t green = luma - syccRoundToNearest((int64_t)syccCbToGreen * chromaBlue +
                                            (int64_t)syccCrToGreen * chromaRed);
  int32_t blue = luma + syccRoundToNearest((int64_t)syccCbToBlue * chromaBlue);

  *out_r = (T)std::min(std::max(red, 0), upper);
  *out_g = (T)std::min(std::max(green, 0), upper);
  *out_b = (T)std::min(std::max(blue, 0), upper);
}

} // namespace grk
