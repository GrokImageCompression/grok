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
#include <cfloat>
#include <cmath>

#define ALPHA_R 0.299
#define ALPHA_B 0.114
#define ALPHA_RB (ALPHA_R + ALPHA_B)
#define ALPHA_G (1 - ALPHA_RB)
#define CR_FACT_R (2 * (1 - ALPHA_R))
#define CB_FACT_B (2 * (1 - ALPHA_B))
#define CR_FACT_G (2 * ALPHA_R * (1 - ALPHA_R) / ALPHA_G)
#define CB_FACT_G (2 * ALPHA_B * (1 - ALPHA_B) / ALPHA_G)

typedef void (*cvt_color_func)(int32_t *, int32_t *, int32_t *, uint32_t);
#if defined(__AVX2__)
void cvt_rgb_to_ycbcr_rev_avx2(int32_t *sp0, int32_t *sp1, int32_t *sp2, uint32_t num_tc_samples);
void cvt_rgb_to_ycbcr_irrev_avx2(int32_t *sp0, int32_t *sp1, int32_t *sp2, uint32_t num_tc_samples);
void cvt_ycbcr_to_rgb_rev_avx2(int32_t *sp0, int32_t *sp1, int32_t *sp2, uint32_t num_tc_samples);
void cvt_ycbcr_to_rgb_irrev_avx2(int32_t *sp0, int32_t *sp1, int32_t *sp2, uint32_t num_tc_samples);
static cvt_color_func cvt_ycbcr_to_rgb[2] = {cvt_ycbcr_to_rgb_irrev_avx2, cvt_ycbcr_to_rgb_rev_avx2};
static cvt_color_func cvt_rgb_to_ycbcr[2] = {cvt_rgb_to_ycbcr_irrev_avx2, cvt_rgb_to_ycbcr_rev_avx2};
#else
void cvt_rgb_to_ycbcr_rev(int32_t *sp0, int32_t *sp1, int32_t *sp2, uint32_t num_tc_samples);
void cvt_rgb_to_ycbcr_irrev(int32_t *sp0, int32_t *sp1, int32_t *sp2, uint32_t num_tc_samples);
void cvt_ycbcr_to_rgb_rev(int32_t *sp0, int32_t *sp1, int32_t *sp2, uint32_t num_tc_samples);
void cvt_ycbcr_to_rgb_irrev(int32_t *sp0, int32_t *sp1, int32_t *sp2, uint32_t num_tc_samples);
static cvt_color_func cvt_ycbcr_to_rgb[2] = {cvt_ycbcr_to_rgb_irrev, cvt_ycbcr_to_rgb_rev};
static cvt_color_func cvt_rgb_to_ycbcr[2] = {cvt_rgb_to_ycbcr_irrev, cvt_rgb_to_ycbcr_rev};
#endif

inline int32_t round_d(double val) {
  if (fabs(val) < DBL_EPSILON) {
    return 0;
  } else {
    return val + ((val > 0) ? 0.5 : -0.5);
  }
}
