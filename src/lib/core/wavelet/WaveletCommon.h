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

// the reversible 5/3 inverse lifting accumulates sums of two samples: valid
// jpeg 2000 coefficients stay in range, but fuzzer-crafted ones overflow the
// int32 accumulator. the intended result is 2's-complement wraparound (the
// -fwrapv build option defines it as such), so mark these functions to keep
// oss-fuzz's explicit signed-integer-overflow check from flagging it.
#if defined(__clang__) || defined(__GNUC__)
#define GRK_NO_SANITIZE_OVERFLOW __attribute__((no_sanitize("signed-integer-overflow")))
#else
#define GRK_NO_SANITIZE_OVERFLOW
#endif

namespace grk
{

class dwt53
{
public:
  void encode_v(int32_t* res, int32_t* scratch, uint32_t height, uint8_t parity, uint32_t stride,
                uint32_t cols, int32_t dcShift = 0, bool intInput = false);

  void encode_h(int32_t* row, int32_t* scratch, uint32_t width, uint8_t parity, uint32_t stride,
                uint32_t rows, int32_t dcShift = 0);
};

class dwt53_16
{
public:
  void encode_v(int16_t* res, int16_t* scratch, uint32_t height, uint8_t parity, uint32_t stride,
                uint32_t cols, int16_t dcShift = 0, bool intInput = false);

  void encode_h(int16_t* row, int16_t* scratch, uint32_t width, uint8_t parity, uint32_t stride,
                uint32_t rows, int16_t dcShift = 0);
};

class dwt97
{
public:
  void encode_v(float* res, float* scratch, uint32_t height, uint8_t parity, uint32_t stride,
                uint32_t cols, float dcShift = 0.0f, bool intInput = false);

  void encode_h(float* row, float* scratch, uint32_t width, uint8_t parity, uint32_t stride,
                uint32_t rows, float dcShift = 0.0f);
};

template<typename T, size_t N>
struct vec
{
  vec(void) : val{0} {}
  explicit vec(T m)
  {
    for(size_t i = 0; i < N; ++i)
      val[i] = m;
  }
  vec operator+(const vec& rhs)
  {
    vec rc;
    for(size_t i = 0; i < N; ++i)
      rc.val[i] = val[i] + rhs.val[i];

    return rc;
  }
  vec& operator+=(const vec& rhs)
  {
    for(size_t i = 0; i < N; ++i)
      val[i] += rhs.val[i];

    return *this;
  }
  vec operator-(const vec& rhs)
  {
    vec rc;
    for(size_t i = 0; i < N; ++i)
      rc.val[i] = val[i] - rhs.val[i];

    return rc;
  }
  vec& operator-=(const vec& rhs)
  {
    for(size_t i = 0; i < N; ++i)
      val[i] -= rhs.val[i];

    return *this;
  }

  constexpr static size_t NUM_ELTS = N;
  T val[N];
};

typedef vec<float, 4> vec4f;
// one 128-bit pack of the int16 9/7 partial lifting: 8 rows of a horizontal strip,
// or 8 columns of a vertical one
typedef vec<int16_t, 8> vec8s;

// int16 saturating add, matching Highway SaturatedAdd
inline int16_t sat_add_16(int16_t a, int16_t b)
{
  return (int16_t)std::clamp((int32_t)a + (int32_t)b, -32768, 32767);
}

// right shift, rounding to nearest with ties to even. one value in 2^shift lands exactly
// halfway between two outputs, and sending all of those up brightens the decoded image.
// the int16 9/7 uses it for the beta step and for the Q-format synthesis sink.
inline int16_t rshift_even_16(int16_t x, int shift)
{
  if(shift <= 0)
    return x;
  int16_t bias = (int16_t)((1 << (shift - 1)) - 1 + ((x >> shift) & 1));
  return (int16_t)(sat_add_16(x, bias) >> shift);
}

} // namespace grk
