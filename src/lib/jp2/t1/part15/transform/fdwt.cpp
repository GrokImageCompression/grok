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

// irreversible FDWT
auto fdwt_1d_filtr_irrev97_fixed = [](int16_t *X, const int32_t left, const int32_t right,
                                      const uint32_t u_i0, const uint32_t u_i1) {
  const auto i0       = static_cast<const int32_t>(u_i0);
  const auto i1       = static_cast<const int32_t>(u_i1);
  const int32_t start = ceil_int(i0, 2);
  const int32_t stop  = ceil_int(i1, 2);

  const int32_t offset = left + i0 % 2;
  for (int32_t n = -4 + offset, i = start - 2; i < stop + 1; i++, n += 2) {
    int32_t sum = X[n];
    sum += X[n + 2];
    X[n + 1] += (int16_t)((Acoeff * sum + Aoffset) >> Ashift);
  }
  for (int32_t n = -2 + offset, i = start - 1; i < stop + 1; i++, n += 2) {
    int32_t sum = X[n - 1];
    sum += X[n + 1];
    X[n] += (int16_t)((Bcoeff * sum + Boffset) >> Bshift);
  }
  for (int32_t n = -2 + offset, i = start - 1; i < stop; i++, n += 2) {
    int32_t sum = X[n];
    sum += X[n + 2];
    X[n + 1] += (int16_t)((Ccoeff * sum + Coffset) >> Cshift);
  }
  for (int32_t n = 0 + offset, i = start; i < stop; i++, n += 2) {
    int32_t sum = X[n - 1];
    sum += X[n + 1];
    X[n] += (int16_t)((Dcoeff * sum + Doffset) >> Dshift);
  }
};

// reversible FDWT
auto fdwt_1d_filtr_rev53_fixed = [](int16_t *X, const int32_t left, const int32_t right,
                                    const uint32_t u_i0, const uint32_t u_i1) {
  const auto i0       = static_cast<const int32_t>(u_i0);
  const auto i1       = static_cast<const int32_t>(u_i1);
  const int32_t start = ceil_int(i0, 2);
  const int32_t stop  = ceil_int(i1, 2);
  // X += left - i0 % 2;
  const int32_t offset = left + i0 % 2;
  for (int32_t n = -2 + offset, i = start - 1; i < stop; ++i, n += 2) {
    int32_t sum = X[n];
    sum += X[n + 2];
    X[n + 1] -= (sum >> 1);
  }
  for (int32_t n = 0 + offset, i = start; i < stop; ++i, n += 2) {
    int32_t sum = X[n - 1];
    sum += X[n + 1];
    X[n] += ((sum + 2) >> 2);
  }
};

static fdwt_1d_filtr_func_fixed fdwt_1d_filtr_fixed[2] = {fdwt_1d_filtr_irrev97_fixed,
                                                          fdwt_1d_filtr_rev53_fixed};

// 1-dimensional FDWT
static inline void fdwt_1d_sr_fixed(int16_t *in, int16_t *out, const int32_t left, const int32_t right,
                                    const uint32_t i0, const uint32_t i1, const uint8_t transformation) {
  const uint32_t len = round_up(i1 - i0 + left + right, SIMD_LEN_I16);
  auto *Xext         = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * len, 32));
  // auto *Yext         = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * len, 32));
  dwt_1d_extr_fixed(Xext, in, left, right, i0, i1);
  fdwt_1d_filtr_fixed[transformation](Xext, left, right, i0, i1);
  memcpy(out, Xext + left, sizeof(int16_t) * (i1 - i0));
  aligned_mem_free(Xext);
  // aligned_mem_free(Yext);
}

// FDWT for horizontal direction
static void fdwt_hor_sr_fixed(int16_t *out, int16_t *in, const uint32_t u0, const uint32_t u1,
                              const uint32_t v0, const uint32_t v1, const uint8_t transformation) {
  const uint32_t stride              = u1 - u0;
  constexpr int32_t num_pse_i0[2][2] = {{4, 2}, {3, 1}};
  constexpr int32_t num_pse_i1[2][2] = {{3, 1}, {4, 2}};
  const int32_t left                 = num_pse_i0[u0 % 2][transformation];
  const int32_t right                = num_pse_i1[u1 % 2][transformation];

  if (u0 == u1 - 1) {
    // one sample case
    const float K  = (transformation) ? 1 : 1.2301741 / 2;  // 04914001;
    const float K1 = (transformation) ? 1 : 0.8128931;      // 066115961;
    for (uint32_t row = 0; row < v1 - v0; ++row) {
      if (u0 % 2 == 0) {
        out[row] = (transformation) ? in[row] : (int16_t)roundf(static_cast<float>(in[row]) * K1);
      } else {
        out[row] = (transformation) ? in[row] << 1 : (int16_t)roundf(static_cast<float>(in[row]) * 2 * K);
      }
    }
  } else {
    // need to perform symmetric extension
    //#pragma omp parallel for
    for (uint32_t row = 0; row < v1 - v0; ++row) {
      fdwt_1d_sr_fixed(&in[row * stride], &out[row * stride], left, right, u0, u1, transformation);
    }
  }
}

auto fdwt_irrev_ver_sr_fixed = [](int16_t *in, const uint32_t u0, const uint32_t u1, const uint32_t v0,
                                  const uint32_t v1) {
  const uint32_t stride           = u1 - u0;
  constexpr int32_t num_pse_i0[2] = {4, 3};
  constexpr int32_t num_pse_i1[2] = {3, 4};
  const int32_t top               = num_pse_i0[v0 % 2];
  const int32_t bottom            = num_pse_i1[v1 % 2];

  if (v0 == v1 - 1) {
    // one sample case
    constexpr float K  = 1.2301741 / 2;  // 04914001;
    constexpr float K1 = 0.8128931;      // 066115961;
    for (uint32_t col = 0; col < u1 - u0; ++col) {
      if (v0 % 2 == 0) {
        in[col] = (int16_t)roundf(static_cast<float>(in[col]) * K1);
      } else {
        in[col] = (int16_t)roundf(static_cast<float>(in[col]) * 2 * K);
      }
    }
  } else {
    const uint32_t len = round_up(stride, SIMD_LEN_I16);
    auto **buf         = new int16_t *[top + v1 - v0 + bottom];
    for (uint32_t i = 1; i <= top; ++i) {
      buf[top - i] = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * len, 32));
      memcpy(buf[top - i], &in[(PSEo(v0 - i, v0, v1) - v0) * stride], sizeof(int16_t) * stride);
      // buf[top - i] = &in[(PSEo(v0 - i, v0, v1) - v0) * stride];
    }
    for (uint32_t row = 0; row < v1 - v0; ++row) {
      buf[top + row] = &in[row * stride];
    }
    for (uint32_t i = 1; i <= bottom; i++) {
      buf[top + (v1 - v0) + i - 1] = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * len, 32));
      memcpy(buf[top + (v1 - v0) + i - 1], &in[(PSEo(v1 - v0 + i - 1 + v0, v0, v1) - v0) * stride],
             sizeof(int16_t) * stride);
    }
    const int32_t start  = ceil_int(v0, 2);
    const int32_t stop   = ceil_int(v1, 2);
    const int32_t offset = top + v0 % 2;

    for (int32_t n = -4 + offset, i = start - 2; i < stop + 1; i++, n += 2) {
      for (uint32_t col = 0; col < u1 - u0; ++col) {
        int32_t sum = buf[n][col];
        sum += buf[n + 2][col];
        buf[n + 1][col] += (int16_t)((Acoeff * sum + Aoffset) >> Ashift);
      }
    }
    for (int32_t n = -2 + offset, i = start - 1; i < stop + 1; i++, n += 2) {
      for (uint32_t col = 0; col < u1 - u0; ++col) {
        int32_t sum = buf[n - 1][col];
        sum += buf[n + 1][col];
        buf[n][col] += (int16_t)((Bcoeff * sum + Boffset) >> Bshift);
      }
    }
    for (int32_t n = -2 + offset, i = start - 1; i < stop; i++, n += 2) {
      for (uint32_t col = 0; col < u1 - u0; ++col) {
        int32_t sum = buf[n][col];
        sum += buf[n + 2][col];
        buf[n + 1][col] += (int16_t)((Ccoeff * sum + Coffset) >> Cshift);
      }
    }
    for (int32_t n = 0 + offset, i = start; i < stop; i++, n += 2) {
      for (uint32_t col = 0; col < u1 - u0; ++col) {
        int32_t sum = buf[n - 1][col];
        sum += buf[n + 1][col];
        buf[n][col] += (int16_t)((Dcoeff * sum + Doffset) >> Dshift);
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
};

auto fdwt_rev_ver_sr_fixed = [](int16_t *in, const uint32_t u0, const uint32_t u1, const uint32_t v0,
                                const uint32_t v1) {
  const uint32_t stride           = u1 - u0;
  constexpr int32_t num_pse_i0[2] = {2, 1};
  constexpr int32_t num_pse_i1[2] = {1, 2};
  const int32_t top               = num_pse_i0[v0 % 2];
  const int32_t bottom            = num_pse_i1[v1 % 2];

  if (v0 == v1 - 1) {
    // one sample case
    for (uint32_t col = 0; col < u1 - u0; ++col) {
      if (v0 % 2 == 0) {
        in[col] = in[col];
      } else {
        in[col] = in[col] << 1;
      }
    }
  } else {
    const uint32_t len = round_up(stride, SIMD_LEN_I16);
    auto **buf         = new int16_t *[top + v1 - v0 + bottom];
    for (uint32_t i = 1; i <= top; ++i) {
      buf[top - i] = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * len, 32));
      memcpy(buf[top - i], &in[(PSEo(v0 - i, v0, v1) - v0) * stride], sizeof(int16_t) * stride);
      // buf[top - i] = &in[(PSEo(v0 - i, v0, v1) - v0) * stride];
    }
    for (uint32_t row = 0; row < v1 - v0; ++row) {
      buf[top + row] = &in[row * stride];
    }
    for (uint32_t i = 1; i <= bottom; i++) {
      buf[top + (v1 - v0) + i - 1] = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * len, 32));
      memcpy(buf[top + (v1 - v0) + i - 1], &in[(PSEo(v1 - v0 + i - 1 + v0, v0, v1) - v0) * stride],
             sizeof(int16_t) * stride);
    }
    const int32_t start  = ceil_int(v0, 2);
    const int32_t stop   = ceil_int(v1, 2);
    const int32_t offset = top + v0 % 2;

    for (int32_t n = -2 + offset, i = start - 1; i < stop; ++i, n += 2) {
      for (uint32_t col = 0; col < u1 - u0; ++col) {
        int32_t sum = buf[n][col];
        sum += buf[n + 2][col];
        buf[n + 1][col] -= (sum >> 1);
      }
    }
    for (int32_t n = 0 + offset, i = start; i < stop; ++i, n += 2) {
      for (uint32_t col = 0; col < u1 - u0; ++col) {
        int32_t sum = buf[n - 1][col];
        sum += buf[n + 1][col];
        buf[n][col] += ((sum + 2) >> 2);
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
};

static fdwt_ver_filtr_func_fixed fdwt_ver_sr_fixed[2] = {fdwt_irrev_ver_sr_fixed, fdwt_rev_ver_sr_fixed};

// Deinterleaving to devide coefficients into subbands
static void fdwt_2d_deinterleave_fixed(const int16_t *buf, int16_t *const LL, int16_t *const HL,
                                       int16_t *const LH, int16_t *const HH, const uint32_t u0,
                                       const uint32_t u1, const uint32_t v0, const uint32_t v1,
                                       const uint8_t transformation) {
  const uint32_t stride     = u1 - u0;
  const uint32_t v_offset   = v0 % 2;
  const uint32_t u_offset   = u0 % 2;
  int16_t *dp[4]            = {LL, HL, LH, HH};
  const uint32_t vstart[4]  = {ceil_int(v0, 2), ceil_int(v0, 2), v0 / 2, v0 / 2};
  const uint32_t vstop[4]   = {ceil_int(v1, 2), ceil_int(v1, 2), v1 / 2, v1 / 2};
  const uint32_t ustart[4]  = {ceil_int(u0, 2), u0 / 2, ceil_int(u0, 2), u0 / 2};
  const uint32_t ustop[4]   = {ceil_int(u1, 2), u1 / 2, ceil_int(u1, 2), u1 / 2};
  const uint32_t voffset[4] = {v_offset, v_offset, 1 - v_offset, 1 - v_offset};
  const uint32_t uoffset[4] = {u_offset, 1 - u_offset, u_offset, 1 - u_offset};

  for (uint8_t b = 0; b < 4; ++b) {
    for (uint32_t v = 0, vb = vstart[b]; vb < vstop[b]; ++vb, ++v) {
      for (uint32_t u = 0, ub = ustart[b]; ub < ustop[b]; ++ub, ++u) {
        *(dp[b]++) = buf[2 * u + uoffset[b] + (2 * v + voffset[b]) * stride];
      }
    }
  }
#ifdef BETTERQUANT
  // TODO: One sample case shall be considered. Currently implementation is not correct for 1xn or nx1 or
  // 1x1..
  constexpr float K     = 1.2301741 / 2;
  constexpr float K1    = 0.8128931;
  constexpr float KK[4] = {K1 * K1, K * K1, K1 * K, K * K};
  if (transformation) {
  #pragma omp parallel for  //default(none) \
    shared(dp, vstart, ustart, vstop, ustop, uoffset, voffset, stride, buf)
    for (uint8_t b = 0; b < 4; ++b) {
      for (uint32_t v = 0, vb = vstart[b]; vb < vstop[b]; ++vb, ++v) {
        for (uint32_t u = 0, ub = ustart[b]; ub < ustop[b]; ++ub, ++u) {
          *(dp[b]++) = buf[2 * u + uoffset[b] + (2 * v + voffset[b]) * stride];
        }
      }
    }
  } else {
  #pragma omp parallel for  //default(none) \
    shared(dp, vstart, ustart, vstop, ustop, uoffset, voffset, stride, buf)
    for (uint8_t b = 0; b < 4; ++b) {
      for (uint32_t v = 0, vb = vstart[b]; vb < vstop[b]; ++vb, ++v) {
        for (uint32_t u = 0, ub = ustart[b]; ub < ustop[b]; ++ub, ++u) {
          int16_t val  = buf[2 * u + uoffset[b] + (2 * v + voffset[b]) * stride];
          int16_t sign = val & 0x8000;
          val          = (val < 0) ? -val & 0x7FFF : val;
          val          = static_cast<int16_t>(val * KK[b] + 0.5);
          if (sign) {
            val = -val;
          }
          *(dp[b]++) = val;
          // TODO: LL band shall be scaled for 16bit fixed-point implementation
        }
      }
    }
  }
#endif
}

// 2D FDWT function
void fdwt_2d_sr_fixed(int16_t *previousLL, int16_t *LL, int16_t *HL, int16_t *LH, int16_t *HH,
                      const uint32_t u0, const uint32_t u1, const uint32_t v0, const uint32_t v1,
                      const uint8_t transformation) {
  const uint32_t buf_length = (u1 - u0) * (v1 - v0);
  int16_t *src              = previousLL;
  auto *dst                 = static_cast<int16_t *>(aligned_mem_alloc(sizeof(int16_t) * buf_length, 32));
  fdwt_ver_sr_fixed[transformation](src, u0, u1, v0, v1);
  fdwt_hor_sr_fixed(dst, src, u0, u1, v0, v1, transformation);
  fdwt_2d_deinterleave_fixed(dst, LL, HL, LH, HH, u0, u1, v0, v1, transformation);
  aligned_mem_free(dst);
}