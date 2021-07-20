// Copyright (c) 2019 - 2021, Osamu Watanabe
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
//    modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
//    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once
#include "simd.h"

#include <cstdint>
#include <cstdlib>

#define round_up(x, n) (((x) + (n)-1) & (-n))
#define round_down(x, n) ((x) & (-n))
#define ceil_int(a, b) ((a) + ((b)-1)) / (b)

static inline size_t popcount32(uintmax_t num) {
  size_t precision = 0;
#if defined(_MSC_VER)
  precision = __popcnt(static_cast<uint32_t>(num));
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  precision = _popcnt32(num);
#else
  while (num != 0) {
    if (1 == (num & 1)) {
      precision++;
    }
    num >>= 1;
  }
#endif
  return precision;
}

static inline uint32_t int_log2(const uint32_t x) {
  uint32_t y;
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #if defined(_MSC_VER)
  unsigned long tmp;
  _BitScanReverse(&tmp, x);
  y = tmp;
  #else
  asm("bsr %1, %0" : "=r"(y) : "r"(x));
  #endif
#elif (defined(__arm64__) || defined(__arm__)) && defined(__ARM_FEATURE_CLZ)
  y = 31 - __clz(x);
#endif
  return y;
}

static inline uint32_t count_leading_zeros(const uint32_t x) {
#if defined(_MSC_VER)
  return __lzcnt(x);
#elif defined(__AVX2__)
  return _lzcnt_u32(x);
#elif defined(__MINGW32__) || defined(__MINGW64__)
  // avoid undefined behaviour
  return (x == 0) ? 32 : __builtin_clz(x);
#elif defined(__ARM_FEATURE_CLZ)
  return __clz(x);
#else
  return 31 - int_log2(x);
#endif
}

static inline void* aligned_mem_alloc(size_t size, size_t align) {
  void* result;
#if defined(__INTEL_COMPILER)
  result = _mm_malloc(size, align);
#elif defined(_MSC_VER)
  result = _aligned_malloc(size, align);
#elif defined(__MINGW32__) || defined(__MINGW64__)
  result = __mingw_aligned_malloc(size, align);
#else
  if (posix_memalign(&result, align, size)) {
    result = nullptr;
  }
#endif
  return result;
}

static inline void aligned_mem_free(void* ptr) {
#if defined(__INTEL_COMPILER)
  _mm_free(ptr);
#elif defined(_MSC_VER)
  _aligned_free(ptr);
#elif defined(__MINGW32__) || defined(__MINGW64__)
  __mingw_aligned_free(ptr);
#else
  free(ptr);
#endif
}
