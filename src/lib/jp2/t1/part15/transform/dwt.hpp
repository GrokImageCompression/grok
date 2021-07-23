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

#include <cstdint>

#ifdef _OPENMP
  #include <omp.h>
#endif

#if defined(_MSC_VER) || defined(__MINGW64__)
  #include <intrin.h>
#elif defined(__x86_64__)
  #include <x86intrin.h>
#endif

#define SIMD_LEN_I16 16

constexpr int32_t Acoeff = -25987;
constexpr int32_t Bcoeff = -3472;
constexpr int32_t Ccoeff = 28931;
constexpr int32_t Dcoeff = 29066;

constexpr int32_t Aoffset = 8192;
constexpr int32_t Boffset = 32767;
constexpr int32_t Coffset = 16384;
constexpr int32_t Doffset = 32767;

constexpr int32_t Ashift = 14;
constexpr int32_t Bshift = 16;
constexpr int32_t Cshift = 15;
constexpr int32_t Dshift = 16;

// define pointer to FDWT functions
typedef void (*fdwt_1d_filtr_func_fixed)(int16_t *, int32_t, int32_t, const uint32_t, const uint32_t);
typedef void (*fdwt_ver_filtr_func_fixed)(int16_t *, const uint32_t, const uint32_t, const uint32_t,
                                          const uint32_t);

static inline int32_t PSEo(const int32_t i, const int32_t i0, const int32_t i1) {
  const int32_t tmp0    = 2 * (i1 - i0 - 1);
  const int32_t tmp1    = ((i - i0) < 0) ? i0 - i : i - i0;
  const int32_t mod_val = tmp1 % tmp0;
  const int32_t min_val = mod_val < tmp0 - mod_val ? mod_val : tmp0 - mod_val;
  return i0 + min_val;
}
template <class T>
static inline void dwt_1d_extr_fixed(T *extbuf, T *buf, const int32_t left, const int32_t right,
                                     const uint32_t i0, const uint32_t i1) {
  memcpy(extbuf + left, buf, sizeof(T) * (i1 - i0));
  for (uint32_t i = 1; i <= left; i++) {
    extbuf[left - i] = buf[PSEo(i0 - i, i0, i1) - i0];
  }
  for (uint32_t i = 1; i <= right; i++) {
    extbuf[left + (i1 - i0) + i - 1] = buf[PSEo(i1 - i0 + i - 1 + i0, i0, i1) - i0];
  }
}

// FDWT
void fdwt_2d_sr_fixed(int16_t *previousLL, int16_t *LL, int16_t *HL, int16_t *LH, int16_t *HH, uint32_t u0,
                      uint32_t u1, uint32_t v0, uint32_t v1, uint8_t transformation);

// IDWT
void idwt_2d_sr_fixed(int16_t *nextLL, int16_t *LL, int16_t *HL, int16_t *LH, int16_t *HH, uint32_t u0,
                      uint32_t u1, uint32_t v0, uint32_t v1, uint8_t transformation,
                      uint8_t normalizing_upshift);
