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

class dwt97_16
{
public:
  // Adapts int32_t template interface to int16_t 9/7 DWT functions.
  // The template passes int32_t* scratch; we cast to int16_t* internally
  // (the scratch buffer is large enough since sizeof(int32_t) >= sizeof(int16_t)).
  void encode_v(int32_t* res, int32_t* scratch, uint32_t height, uint8_t parity, uint32_t stride,
                uint32_t cols, int32_t dcShift = 0, bool intInput = false);

  void encode_h(int32_t* row, int32_t* scratch, uint32_t width, uint8_t parity, uint32_t stride,
                uint32_t rows, int32_t dcShift = 0);
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

} // namespace grk
