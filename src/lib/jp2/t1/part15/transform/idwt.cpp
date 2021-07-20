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

#include <cstring>
#include <cmath>
#include "dwt.hpp"
#include "utils.hpp"

// This is not function but lambda expression
auto idwt_1d_filtr_rev53_fixed = [](int16_t *X, const int32_t left, const int32_t right,
                                    const uint32_t u_i0, const uint32_t u_i1) {
  const auto i0 = static_cast<const int32_t>(u_i0);
  const auto i1 = static_cast<const int32_t>(u_i1);

  X += left - i0 % 2;
  for (int32_t n = 0, i = i0 / 2; i < i1 / 2 + 1; ++i, n += 2) {
    X[n] -= ((X[n - 1] + X[n + 1] + 2) >> 2);
  }

  for (int32_t n = 0, i = i0 / 2; i < i1 / 2; ++i, n += 2) {
    X[n + 1] += ((X[n] + X[n + 2]) >> 1);
  }
};

// This is not function but lambda expression
auto idwt_1d_filtr_irrev97_fixed = [](int16_t *X, const int32_t left, const int32_t right,
                                      const uint32_t u_i0, const uint32_t u_i1) {
  const auto i0 = static_cast<const int32_t>(u_i0);
  const auto i1 = static_cast<const int32_t>(u_i1);

  X += left - i0 % 2;

  int32_t sum;
  /* K and 1/K have been already done by dequantization */
  for (int32_t n = -2, i = i0 / 2 - 1; i < i1 / 2 + 2; i++, n += 2) {
    sum = X[n - 1];
    sum += X[n + 1];
    X[n] -= (int16_t)((Dcoeff * sum + Doffset) >> Dshift);
  }
  for (int32_t n = -2, i = i0 / 2 - 1; i < i1 / 2 + 1; i++, n += 2) {
    sum = X[n];
    sum += X[n + 2];
    X[n + 1] -= (int16_t)((Ccoeff * sum + Coffset) >> Cshift);
  }
  for (int32_t n = 0, i = i0 / 2; i < i1 / 2 + 1; i++, n += 2) {
    sum = X[n - 1];
    sum += X[n + 1];
    X[n] -= (int16_t)((Bcoeff * sum + Boffset) >> Bshift);
  }
  for (int32_t n = 0, i = i0 / 2; i < i1 / 2; i++, n += 2) {
    sum = X[n];
    sum += X[n + 2];
    X[n + 1] -= (int16_t)((Acoeff * sum + Aoffset) >> Ashift);
  }
};
typedef void (*idwt_1d_filtr_func_fixed)(int16_t *, int32_t, int32_t, const uint32_t, const uint32_t);
static idwt_1d_filtr_func_fixed idwt_1d_filtr_fixed[2] = {idwt_1d_filtr_irrev97_fixed,
                                                          idwt_1d_filtr_rev53_fixed};

static void idwt_1d_sr_fixed(int16_t *in, int16_t *out, const int32_t left, const int32_t right,
                             const uint32_t i0, const uint32_t i1, const uint8_t transformation) {
  const uint32_t len = round_up(i1 - i0 + left + right, SIMD_LEN_I16);
  auto *buf          = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * len, 32));
  dwt_1d_extr_fixed(buf, in, left, right, i0, i1);
  idwt_1d_filtr_fixed[transformation](buf, left, right, i0, i1);
  memcpy(out, buf + left, sizeof(int16_t) * (i1 - i0));
  aligned_mem_free(buf);
}

static void idwt_hor_sr_fixed(int16_t *out, int16_t *in, const uint32_t u0, const uint32_t u1,
                              const uint32_t v0, const uint32_t v1, const uint8_t transformation) {
  const uint32_t stride              = u1 - u0;
  constexpr int32_t num_pse_i0[2][2] = {{3, 1}, {4, 2}};
  constexpr int32_t num_pse_i1[2][2] = {{4, 2}, {3, 1}};
  const int32_t left                 = num_pse_i0[u0 % 2][transformation];
  const int32_t right                = num_pse_i1[u1 % 2][transformation];

  if (u0 == u1 - 1) {
    // one sample case
    const float K  = (transformation) ? 1 : 1.2301741;  // 04914001;
    const float K1 = (transformation) ? 1 : 0.8128931;  // 066115961;
    for (uint32_t row = 0; row < v1 - v0; ++row) {
      if (u0 % 2 == 0) {
        out[row] = (transformation) ? in[row] : (int16_t)roundf(static_cast<float>(in[row]) * K1);
      } else {
        out[row] = (transformation) ? in[row] >> 1 : (int16_t)roundf(static_cast<float>(in[row]) * 0.5 * K);
      }
    }
  } else {
    // need to perform symmetric extension
#pragma omp parallel for
    for (uint32_t row = 0; row < v1 - v0; ++row) {
      idwt_1d_sr_fixed(&in[row * stride], &out[row * stride], left, right, u0, u1, transformation);
    }
  }
}

static void idwt_ver_sr_fixed(int16_t *in, const uint32_t u0, const uint32_t u1, const uint32_t v0,
                              const uint32_t v1, const uint8_t transformation) {
  const uint32_t stride              = u1 - u0;
  constexpr int32_t num_pse_i0[2][2] = {{3, 1}, {4, 2}};
  constexpr int32_t num_pse_i1[2][2] = {{4, 2}, {3, 1}};
  const int32_t top                  = num_pse_i0[v0 % 2][transformation];
  const int32_t bottom               = num_pse_i1[v1 % 2][transformation];
  if (v0 == v1 - 1) {
    // one sample case
    const float K  = (transformation) ? 1 : 1.2301741;  // 04914001;
    const float K1 = (transformation) ? 1 : 0.8128931;  // 066115961;
    for (uint32_t col = 0; col < u1 - u0; ++col) {
      if (v0 % 2 == 0) {
        in[col] = (transformation) ? in[col] : (int16_t)roundf(static_cast<float>(in[col]) * K1);
      } else {
        in[col] = (transformation) ? in[col] >> 1 : (int16_t)roundf(static_cast<float>(in[col]) * 0.5 * K);
      }
    }
  } else {
    const uint32_t len = round_up(stride, SIMD_LEN_I16);
    auto **buf         = new int16_t *[top + v1 - v0 + bottom];
    for (uint32_t i = 1; i <= top; ++i) {
      buf[top - i] = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * len, 32));
      memcpy(buf[top - i], &in[(PSEo(v0 - i, v0, v1) - v0) * stride], sizeof(int16_t) * stride);
    }
    for (uint32_t row = 0; row < v1 - v0; ++row) {
      buf[top + row] = &in[row * stride];
    }
    for (uint32_t i = 1; i <= bottom; i++) {
      buf[top + (v1 - v0) + i - 1] = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * len, 32));
      memcpy(buf[top + (v1 - v0) + i - 1], &in[(PSEo(v1 - v0 + i - 1 + v0, v0, v1) - v0) * stride],
             sizeof(int16_t) * stride);
    }
    const int32_t start  = v0 / 2;
    const int32_t stop   = v1 / 2;
    const int32_t offset = top - v0 % 2;
    if (transformation) {
      for (int32_t n = 0 + offset, i = start; i < stop + 1; ++i, n += 2) {
        for (uint32_t col = 0; col < u1 - u0; ++col) {
          int32_t sum = buf[n - 1][col];
          sum += buf[n + 1][col];
          buf[n][col] -= ((sum + 2) >> 2);
        }
      }
      for (int32_t n = 0 + offset, i = start; i < stop; ++i, n += 2) {
        for (uint32_t col = 0; col < u1 - u0; ++col) {
          int32_t sum = buf[n][col];
          sum += buf[n + 2][col];
          buf[n + 1][col] += (sum >> 1);
        }
      }
    } else {
      for (int32_t n = -2 + offset, i = start - 1; i < stop + 2; i++, n += 2) {
        for (uint32_t col = 0; col < u1 - u0; ++col) {
          int32_t sum = buf[n - 1][col];
          sum += buf[n + 1][col];
          buf[n][col] -= (int16_t)((Dcoeff * sum + Doffset) >> Dshift);
        }
      }
      for (int32_t n = -2 + offset, i = start - 1; i < stop + 1; i++, n += 2) {
        for (uint32_t col = 0; col < u1 - u0; ++col) {
          int32_t sum = buf[n][col];
          sum += buf[n + 2][col];
          buf[n + 1][col] -= (int16_t)((Ccoeff * sum + Coffset) >> Cshift);
        }
      }
      for (int32_t n = 0 + offset, i = start; i < stop + 1; i++, n += 2) {
        for (uint32_t col = 0; col < u1 - u0; ++col) {
          int32_t sum = buf[n - 1][col];
          sum += buf[n + 1][col];
          buf[n][col] -= (int16_t)((Bcoeff * sum + Boffset) >> Bshift);
        }
      }
      for (int32_t n = 0 + offset, i = start; i < stop; i++, n += 2) {
        for (uint32_t col = 0; col < u1 - u0; ++col) {
          int32_t sum = buf[n][col];
          sum += buf[n + 2][col];
          buf[n + 1][col] -= (int16_t)((Acoeff * sum + Aoffset) >> Ashift);
        }
      }
    }
    for (uint32_t i = 1; i <= top; ++i) {
      aligned_mem_free(buf[top - i]);
    }
    for (uint32_t i = 1; i <= bottom; i++) {
      aligned_mem_free(buf[top + (v1 - v0) + i - 1]);
    }
    delete[] buf;
  }
}
static void idwt_2d_interleave_fixed(int16_t *buf, int16_t *LL, int16_t *HL, int16_t *LH, int16_t *HH,
                                     uint32_t u0, uint32_t u1, uint32_t v0, uint32_t v1) {
  const uint32_t stride     = u1 - u0;
  const uint32_t v_offset   = v0 % 2;
  const uint32_t u_offset   = u0 % 2;
  int16_t *sp[4]            = {LL, HL, LH, HH};
  const uint32_t vstart[4]  = {ceil_int(v0, 2), ceil_int(v0, 2), v0 / 2, v0 / 2};
  const uint32_t vstop[4]   = {ceil_int(v1, 2), ceil_int(v1, 2), v1 / 2, v1 / 2};
  const uint32_t ustart[4]  = {ceil_int(u0, 2), u0 / 2, ceil_int(u0, 2), u0 / 2};
  const uint32_t ustop[4]   = {ceil_int(u1, 2), u1 / 2, ceil_int(u1, 2), u1 / 2};
  const uint32_t voffset[4] = {v_offset, v_offset, 1 - v_offset, 1 - v_offset};
  const uint32_t uoffset[4] = {u_offset, 1 - u_offset, u_offset, 1 - u_offset};

  for (uint8_t b = 0; b < 4; ++b) {
    for (uint32_t v = 0, vb = vstart[b]; vb < vstop[b]; ++vb, ++v) {
      for (uint32_t u = 0, ub = ustart[b]; ub < ustop[b]; ++ub, ++u) {
        buf[2 * u + uoffset[b] + (2 * v + voffset[b]) * stride] = *(sp[b]++);
      }
    }
  }
}

void idwt_2d_sr_fixed(int16_t *nextLL, int16_t *LL, int16_t *HL, int16_t *LH, int16_t *HH,
                      const uint32_t u0, const uint32_t u1, const uint32_t v0, const uint32_t v1,
                      const uint8_t transformation, uint8_t normalizing_upshift) {
  const uint32_t buf_length = (u1 - u0) * (v1 - v0);
  int16_t *src              = nextLL;
  auto *dst                 = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * buf_length, 32));
  idwt_2d_interleave_fixed(dst, LL, HL, LH, HH, u0, u1, v0, v1);
  idwt_hor_sr_fixed(src, dst, u0, u1, v0, v1, transformation);
  aligned_mem_free(dst);
  idwt_ver_sr_fixed(src, u0, u1, v0, v1, transformation);

  // scaling for 16bit width fixed-point representation
  if (transformation != 1 && normalizing_upshift) {
#if defined(__AVX2__)
    uint32_t len = round_down(buf_length, SIMD_LEN_I16);
    for (uint32_t n = 0; n < len; n += 16) {
      __m256i tmp0 = _mm256_load_si256((__m256i *)(src + n));
      __m256i tmp1 = _mm256_slli_epi16(tmp0, static_cast<int32_t>(normalizing_upshift));
      _mm256_store_si256((__m256i *)(src + n), tmp1);
    }
    for (uint32_t n = len; n < buf_length; ++n) {
      // cast to unsigned to avoid undefined behavior
      src[n] = static_cast<uint16_t>(src[n]) << normalizing_upshift;
    }
#else
    for (uint32_t n = 0; n < buf_length; ++n) {
      // cast to unsigned to avoid undefined behavior
      src[n] = static_cast<uint16_t>(src[n]) << normalizing_upshift;
    }
#endif
  }
}
