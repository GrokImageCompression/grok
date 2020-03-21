/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
 *
 *    This source code incorporates work covered by the following copyright and
 *    permission notice:
 *
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2007, Jonathan Ballard <dzonatas@dzonux.net>
 * Copyright (c) 2007, Callum Lerwick <seg@haxxed.com>
 * Copyright (c) 2017, IntoPIX SA <support@intopix.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include "simd.h"
#include "grok_includes.h"

#include "sparse_array.h"
#include "dwt.h"
#include <algorithm>

using namespace std;

namespace grk {


#define GRK_WS(i) v->mem[(i)*2]
#define GRK_WD(i) v->mem[(1+(i)*2)]

/** Number of columns that we can process in parallel in the vertical pass */
#define PLL_COLS_53     (2*VREG_INT_COUNT)

/** @name Local data structures */
/*@{*/

template <typename T> struct dwt_data {
	dwt_data() : mem(nullptr),
		         dn(0),
				 sn(0),
				 cas(0),
				 win_l_x0(0),
				 win_l_x1(0),
				 win_h_x0(0),
				 win_h_x1(0)
	{}

	dwt_data(const dwt_data& rhs)
	{
		mem = nullptr;
	    dn = rhs.dn;
	    sn = rhs.sn;
	    cas = rhs.cas;
	    win_l_x0 = rhs.win_l_x0;
	    win_l_x1 = rhs.win_l_x1;
	    win_h_x0 = rhs.win_h_x0;
	    win_h_x1 = rhs.win_h_x1;
	}

	bool alloc(size_t len) {
		release();

	    /* overflow check */
		// add 10 just to be sure to we are safe from
		// segment growth overflow
	    if (len > (SIZE_MAX - 10U)) {
	        GROK_ERROR("data size overflow");
	        return false;
	    }
	    len += 10U;
	    /* overflow check */
	    if (len > (SIZE_MAX / sizeof(T))) {
	        GROK_ERROR("data size overflow");
	        return false;
	    }
		mem = (T*)grk_aligned_malloc(len * sizeof(T));
		return mem != nullptr;
	}
	void release(){
		grk_aligned_free(mem);
		mem = nullptr;
	}
    T* mem;
    int32_t dn;   /* number of elements in high pass band */
    int32_t sn;   /* number of elements in low pass band */
    int32_t cas;  /* 0 = start on even coord, 1 = start on odd coord */
    uint32_t      win_l_x0; /* start coord in low pass band */
    uint32_t      win_l_x1; /* end coord in low pass band */
    uint32_t      win_h_x0; /* start coord in high pass band */
    uint32_t      win_h_x1; /* end coord in high pass band */
};


struct  v4_data {
	v4_data() {
		f[0]=0;
		f[1]=0;
		f[2]=0;
		f[3]=0;
	}
	v4_data(float m){
		f[0]=m;
		f[1]=m;
		f[2]=m;
		f[3]=m;
	}
    float f[4];
};


template <typename T, typename S> struct decode_job{
	decode_job( S data,
				uint32_t w,
				T * GRK_RESTRICT tiledp,
				uint32_t min_j,
				uint32_t max_j) : data(data),
								w(w),
								tiledp(tiledp),
								min_j(min_j),
								max_j(max_j)
	{}

    S data;
    uint32_t w;
    T * GRK_RESTRICT tiledp;
    uint32_t min_j;
    uint32_t max_j;
} ;

static const float dwt_alpha =  1.586134342f; /*  12994 */
static const float dwt_beta  =  0.052980118f; /*    434 */
static const float dwt_gamma = -0.882911075f; /*  -7233 */
static const float dwt_delta = -0.443506852f; /*  -3633 */

static const float K      = 1.230174105f; /*  10078 */
static const float c13318 = 1.625732422f;

/*@}*/

/** @name Local static functions */
/*@{*/

/**
Inverse wavelet transform in 2-D.
*/
static bool decode_tile_53(TileComponent* tilec, uint32_t i);

/* <summary>                             */
/* Inverse 9-7 wavelet transform in 1-D. */
/* </summary>                            */
static void decode_step_97(dwt_data<v4_data>* GRK_RESTRICT dwt);

static void interleave_h_97(dwt_data<v4_data>* GRK_RESTRICT dwt,
                                   float* GRK_RESTRICT a,
                                   uint32_t width,
                                   uint32_t remaining_height);

static void interleave_v_97(dwt_data<v4_data>* GRK_RESTRICT dwt,
                                   float* GRK_RESTRICT a,
                                   uint32_t width,
                                   uint32_t nb_elts_read);

#ifdef __SSE__
static void decode_step1_sse_97(v4_data* w,
                                       uint32_t start,
                                       uint32_t end,
                                       const __m128 c);

static void decode_step2_sse_97(v4_data* l, v4_data* w,
                                       uint32_t start,
                                       uint32_t end,
                                       uint32_t m, __m128 c);

#else
static void decode_step1_97(v4_data* w,
                                   uint32_t start,
                                   uint32_t end,
                                   const float c);

static void decode_step2_97(v4_data* l, v4_data* w,
                                   uint32_t start,
                                   uint32_t end,
                                   uint32_t m,
                                   float c);

#endif

static sparse_array* alloc_sparse_array(TileComponent* tilec,
												uint32_t numres);
static bool init_sparse_array(sparse_array* a,TileComponent* tilec,
												uint32_t numres);

/* FILTER_WIDTH value matches the maximum left/right extension given in tables */
/* F.2 and F.3 of the standard. Note: in tcd_is_subband_area_of_interest() */
/* we currently use 3. */
template <typename T, uint32_t HORIZ_STEP, uint32_t VERT_STEP, uint32_t FILTER_WIDTH, typename D>
   bool decode_partial_tile(TileComponent* GRK_RESTRICT tilec, uint32_t numres);

/*@}*/

/*@}*/

#define GRK_S(i) a[(i)*2]
#define GRK_D(i) a[(1+(i)*2)]
#define GRK_S_(i) ((i)<0?GRK_S(0):((i)>=sn?GRK_S(sn-1):GRK_S(i)))
#define GRK_D_(i) ((i)<0?GRK_D(0):((i)>=dn?GRK_D(dn-1):GRK_D(i)))
/* new */
#define GRK_SS_(i) ((i)<0?GRK_S(0):((i)>=dn?GRK_S(dn-1):GRK_S(i)))
#define GRK_DD_(i) ((i)<0?GRK_D(0):((i)>=sn?GRK_D(sn-1):GRK_D(i)))


/*
==========================================================
   local functions
==========================================================
*/

static void  decode_h_cas0_53(int32_t* tmp,
                               const int32_t sn,
                               const int32_t len,
                               int32_t* tiledp){
    assert(len > 1);

    /* Improved version of the TWO_PASS_VERSION: */
    /* Performs lifting in one single iteration. Saves memory */
    /* accesses and explicit interleaving. */
    const int32_t* in_even = tiledp;
    const int32_t* in_odd = tiledp + sn;
    int32_t s1n = in_even[0];
    int32_t d1n = in_odd[0];
    int32_t s0n = s1n - ((d1n + 1) >> 1);

    int32_t i, j;
    for (i = 0, j = 1; i < (len - 3); i += 2, j++) {
    	int32_t d1c = d1n;
    	int32_t s0c = s0n;

        s1n = in_even[j];
        d1n = in_odd[j];

        s0n = s1n - ((d1c + d1n + 2) >> 2);

        tmp[i  ] = s0c;
        tmp[i + 1] = d1c + ((s0c + s0n) >> 1);
    }

    tmp[i] = s0n;

    if (len & 1) {
        tmp[len - 1] = in_even[(len - 1) >> 1] - ((d1n + 1) >> 1);
        tmp[len - 2] = d1n + ((s0n + tmp[len - 1]) >> 1);
    } else {
        tmp[len - 1] = d1n + s0n;
    }
    memcpy(tiledp, tmp, (uint32_t)len * sizeof(int32_t));
}

static void  decode_h_cas1_53(int32_t* tmp,
                               const int32_t sn,
                               const int32_t len,
                               int32_t* tiledp){
    assert(len > 2);

    /* Improved version of the TWO_PASS_VERSION:
       Performs lifting in one single iteration. Saves memory
       accesses and explicit interleaving. */
    const int32_t* in_even = tiledp + sn;
    const int32_t* in_odd = tiledp;
    int32_t s1 = in_even[1];
    int32_t dc = in_odd[0] - ((in_even[0] + s1 + 2) >> 2);
    tmp[0] = in_even[0] + dc;
    int32_t i, j;
    for (i = 1, j = 1; i < (len - 2 - !(len & 1)); i += 2, j++) {
    	int32_t s2 = in_even[j + 1];
    	int32_t dn = in_odd[j] - ((s1 + s2 + 2) >> 2);

        tmp[i  ] = dc;
        tmp[i + 1] = s1 + ((dn + dc) >> 1);

        dc = dn;
        s1 = s2;
    }

    tmp[i] = dc;

    if (!(len & 1)) {
    	int32_t dn = in_odd[len / 2 - 1] - ((s1 + 1) >> 1);
        tmp[len - 2] = s1 + ((dn + dc) >> 1);
        tmp[len - 1] = dn;
    } else {
        tmp[len - 1] = s1 + dc;
    }
    memcpy(tiledp, tmp, (uint32_t)len * sizeof(int32_t));
}

/* <summary>                            */
/* Inverse 5-3 wavelet transform in 1-D for one row. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
static void decode_h_53(const dwt_data<int32_t> *dwt,
                         int32_t* tiledp)
{
    const int32_t sn = dwt->sn;
    const int32_t len = sn + dwt->dn;
    if (dwt->cas == 0) { /* Left-most sample is on even coordinate */
        if (len > 1) {
            decode_h_cas0_53(dwt->mem, sn, len, tiledp);
        } else {
            /* Unmodified value */
        }
    } else { /* Left-most sample is on odd coordinate */
        if (len == 1) {
            tiledp[0] /= 2;
        } else if (len == 2) {
            int32_t* out = dwt->mem;
            const int32_t* in_even = &tiledp[sn];
            const int32_t* in_odd = &tiledp[0];
            out[1] = in_odd[0] - ((in_even[0] + 1) >> 1);
            out[0] = in_even[0] + out[1];
            memcpy(tiledp, dwt->mem, (uint32_t)len * sizeof(int32_t));
        } else if (len > 2) {
            decode_h_cas1_53(dwt->mem, sn, len, tiledp);
        }
    }
}

#if (defined(__SSE2__) || defined(__AVX2__))

#define ADD3(x,y,z) ADD(ADD(x,y),z)

static
void decode_v_final_memcpy_53(int32_t* tiledp_col,
                               const int32_t* tmp,
                               int32_t len,
                               size_t stride){
    int32_t i;
    for (i = 0; i < len; ++i) {
        /* A memcpy(&tiledp_col[i * stride + 0],
                    &tmp[PARALLEL_COLS_53 * i + 0],
                    PARALLEL_COLS_53 * sizeof(int32_t))
           would do but would be a tiny bit slower.
           We can take here advantage of our knowledge of alignment */
        STOREU(&tiledp_col[(size_t)i * stride + 0],              LOAD(&tmp[PLL_COLS_53 * i + 0]));
        STOREU(&tiledp_col[(size_t)i * stride + VREG_INT_COUNT], LOAD(&tmp[PLL_COLS_53 * i + VREG_INT_COUNT]));
    }
}

/** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
 * 16 in AVX2, when top-most pixel is on even coordinate */
static void decode_v_cas0_mcols_SSE2_OR_AVX2_53(int32_t* tmp,
												const int32_t sn,
												const int32_t len,
												int32_t* tiledp_col,
												const size_t stride){
    int32_t i;
    size_t j;
    const VREG two = LOAD_CST(2);

    assert(len > 1);
#if __AVX2__
    assert(PLL_COLS_53 == 16);
    assert(VREG_INT_COUNT == 8);
#else
    assert(PLL_COLS_53 == 8);
    assert(VREG_INT_COUNT == 4);
#endif

    /* Note: loads of input even/odd values must be done in a unaligned */
    /* fashion. But stores in tmp can be done with aligned store, since */
    /* the temporary buffer is properly aligned */
    assert((size_t)tmp % (sizeof(int32_t) * VREG_INT_COUNT) == 0);

    const int32_t* in_even = &tiledp_col[0];
    const int32_t* in_odd = &tiledp_col[(size_t)sn * stride];
    VREG s1n_0 = LOADU(in_even + 0);
    VREG s1n_1 = LOADU(in_even + VREG_INT_COUNT);
    VREG d1n_0 = LOADU(in_odd);
    VREG d1n_1 = LOADU(in_odd + VREG_INT_COUNT);

    /* s0n = s1n - ((d1n + 1) >> 1); <==> */
    /* s0n = s1n - ((d1n + d1n + 2) >> 2); */
    VREG s0n_0 = SUB(s1n_0, SAR(ADD3(d1n_0, d1n_0, two), 2));
    VREG s0n_1 = SUB(s1n_1, SAR(ADD3(d1n_1, d1n_1, two), 2));

    for (i = 0, j = 1; i < (len - 3); i += 2, j++) {
    	VREG d1c_0 = d1n_0;
    	VREG s0c_0 = s0n_0;
    	VREG d1c_1 = d1n_1;
    	VREG s0c_1 = s0n_1;

        s1n_0 = LOADU(in_even + j * stride);
        s1n_1 = LOADU(in_even + j * stride + VREG_INT_COUNT);
        d1n_0 = LOADU(in_odd + j * stride);
        d1n_1 = LOADU(in_odd + j * stride + VREG_INT_COUNT);

        /*s0n = s1n - ((d1c + d1n + 2) >> 2);*/
        s0n_0 = SUB(s1n_0, SAR(ADD3(d1c_0, d1n_0, two), 2));
        s0n_1 = SUB(s1n_1, SAR(ADD3(d1c_1, d1n_1, two), 2));

        STORE(tmp + PLL_COLS_53 * (i + 0), s0c_0);
        STORE(tmp + PLL_COLS_53 * (i + 0) + VREG_INT_COUNT, s0c_1);

        /* d1c + ((s0c + s0n) >> 1) */
        STORE(tmp + PLL_COLS_53 * (i + 1) + 0,              ADD(d1c_0, SAR(ADD(s0c_0, s0n_0), 1)));
        STORE(tmp + PLL_COLS_53 * (i + 1) + VREG_INT_COUNT, ADD(d1c_1, SAR(ADD(s0c_1, s0n_1), 1)));
    }

    STORE(tmp + PLL_COLS_53 * (i + 0) + 0, s0n_0);
    STORE(tmp + PLL_COLS_53 * (i + 0) + VREG_INT_COUNT, s0n_1);

    if (len & 1) {
        VREG tmp_len_minus_1;
        s1n_0 = LOADU(in_even + (size_t)((len - 1) / 2) * stride);
        /* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
        tmp_len_minus_1 = SUB(s1n_0, SAR(ADD3(d1n_0, d1n_0, two), 2));
        STORE(tmp + PLL_COLS_53 * (len - 1), tmp_len_minus_1);
        /* d1n + ((s0n + tmp_len_minus_1) >> 1) */
        STORE(tmp + PLL_COLS_53 * (len - 2), ADD(d1n_0, SAR(ADD(s0n_0, tmp_len_minus_1), 1)));

        s1n_1 = LOADU(in_even + (size_t)((len - 1) / 2) * stride + VREG_INT_COUNT);
        /* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
        tmp_len_minus_1 = SUB(s1n_1, SAR(ADD3(d1n_1, d1n_1, two), 2));
        STORE(tmp + PLL_COLS_53 * (len - 1) + VREG_INT_COUNT, tmp_len_minus_1);
        /* d1n + ((s0n + tmp_len_minus_1) >> 1) */
        STORE(tmp + PLL_COLS_53 * (len - 2) + VREG_INT_COUNT, ADD(d1n_1, SAR(ADD(s0n_1, tmp_len_minus_1), 1)));

    } else {
        STORE(tmp + PLL_COLS_53 * (len - 1) + 0,              ADD(d1n_0, s0n_0));
        STORE(tmp + PLL_COLS_53 * (len - 1) + VREG_INT_COUNT, ADD(d1n_1, s0n_1));
    }
    decode_v_final_memcpy_53(tiledp_col, tmp, len, stride);
}


/** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
 * 16 in AVX2, when top-most pixel is on odd coordinate */
static void decode_v_cas1_mcols_SSE2_OR_AVX2_53(int32_t* tmp,
												const int32_t sn,
												const int32_t len,
												int32_t* tiledp_col,
												const size_t stride){
    int32_t i;
    size_t j;

    const VREG two = LOAD_CST(2);

    assert(len > 2);
#if __AVX2__
    assert(PLL_COLS_53 == 16);
    assert(VREG_INT_COUNT == 8);
#else
    assert(PLL_COLS_53 == 8);
    assert(VREG_INT_COUNT == 4);
#endif

    /* Note: loads of input even/odd values must be done in a unaligned */
    /* fashion. But stores in tmp can be done with aligned store, since */
    /* the temporary buffer is properly aligned */
    assert((size_t)tmp % (sizeof(int32_t) * VREG_INT_COUNT) == 0);

    const int32_t* in_even = &tiledp_col[(size_t)sn * stride];
    const int32_t* in_odd = &tiledp_col[0];
    VREG s1_0 = LOADU(in_even + stride);
    /* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
    VREG dc_0 = SUB(LOADU(in_odd + 0), SAR(ADD3(LOADU(in_even + 0), s1_0, two), 2));
    STORE(tmp + PLL_COLS_53 * 0, ADD(LOADU(in_even + 0), dc_0));

    VREG s1_1 = LOADU(in_even + stride + VREG_INT_COUNT);
    /* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
    VREG dc_1 = SUB(LOADU(in_odd + VREG_INT_COUNT), SAR(ADD3(LOADU(in_even + VREG_INT_COUNT), s1_1, two), 2));
    STORE(tmp + PLL_COLS_53 * 0 + VREG_INT_COUNT,   ADD(LOADU(in_even + VREG_INT_COUNT), dc_1));

    for (i = 1, j = 1; i < (len - 2 - !(len & 1)); i += 2, j++) {

    	VREG s2_0 = LOADU(in_even + (j + 1) * stride);
    	VREG s2_1 = LOADU(in_even + (j + 1) * stride + VREG_INT_COUNT);

        /* dn = in_odd[j * stride] - ((s1 + s2 + 2) >> 2); */
    	VREG dn_0 = SUB(LOADU(in_odd + j * stride),                 SAR(ADD3(s1_0, s2_0, two), 2));
    	VREG dn_1 = SUB(LOADU(in_odd + j * stride + VREG_INT_COUNT),SAR(ADD3(s1_1, s2_1, two), 2));

        STORE(tmp + PLL_COLS_53 * i, dc_0);
        STORE(tmp + PLL_COLS_53 * i + VREG_INT_COUNT, dc_1);

        /* tmp[i + 1] = s1 + ((dn + dc) >> 1); */
        STORE(tmp + PLL_COLS_53 * (i + 1) + 0,             ADD(s1_0, SAR(ADD(dn_0, dc_0), 1)));
        STORE(tmp + PLL_COLS_53 * (i + 1) + VREG_INT_COUNT,ADD(s1_1, SAR(ADD(dn_1, dc_1), 1)));

        dc_0 = dn_0;
        s1_0 = s2_0;
        dc_1 = dn_1;
        s1_1 = s2_1;
    }
    STORE(tmp + PLL_COLS_53 * i, dc_0);
    STORE(tmp + PLL_COLS_53 * i + VREG_INT_COUNT, dc_1);

    if (!(len & 1)) {
        /*dn = in_odd[(len / 2 - 1) * stride] - ((s1 + 1) >> 1); */
    	VREG dn_0 = SUB(LOADU(in_odd + (size_t)(len / 2 - 1) * stride),SAR(ADD3(s1_0, s1_0, two), 2));
    	VREG dn_1 = SUB(LOADU(in_odd + (size_t)(len / 2 - 1) * stride + VREG_INT_COUNT), SAR(ADD3(s1_1, s1_1, two), 2));

        /* tmp[len - 2] = s1 + ((dn + dc) >> 1); */
        STORE(tmp + PLL_COLS_53 * (len - 2) + 0, ADD(s1_0, SAR(ADD(dn_0, dc_0), 1)));
        STORE(tmp + PLL_COLS_53 * (len - 2) + VREG_INT_COUNT, ADD(s1_1, SAR(ADD(dn_1, dc_1), 1)));

        STORE(tmp + PLL_COLS_53 * (len - 1) + 0, dn_0);
        STORE(tmp + PLL_COLS_53 * (len - 1) + VREG_INT_COUNT, dn_1);
    } else {
        STORE(tmp + PLL_COLS_53 * (len - 1) + 0, ADD(s1_0, dc_0));
        STORE(tmp + PLL_COLS_53 * (len - 1) + VREG_INT_COUNT,ADD(s1_1, dc_1));
    }
    decode_v_final_memcpy_53(tiledp_col, tmp, len, stride);
}

#undef VREG
#undef LOAD_CST
#undef LOADU
#undef LOAD
#undef STORE
#undef STOREU
#undef ADD
#undef ADD3
#undef SUB
#undef SAR

#endif /* (defined(__SSE2__) || defined(__AVX2__)) */

/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on even coordinate */
static void decode_v_cas0_53(int32_t* tmp,
                             const int32_t sn,
                             const int32_t len,
                             int32_t* tiledp_col,
                             const size_t stride){
    int32_t i, j;
    int32_t d1c, d1n, s1n, s0c, s0n;

    assert(len > 1);

    /* Performs lifting in one single iteration. Saves memory */
    /* accesses and explicit interleaving. */

    s1n = tiledp_col[0];
    d1n = tiledp_col[(size_t)sn * stride];
    s0n = s1n - ((d1n + 1) >> 1);

    for (i = 0, j = 0; i < (len - 3); i += 2, j++) {
        d1c = d1n;
        s0c = s0n;

        s1n = tiledp_col[(size_t)(j + 1) * stride];
        d1n = tiledp_col[(size_t)(sn + j + 1) * stride];

        s0n = s1n - ((d1c + d1n + 2) >> 2);

        tmp[i  ] = s0c;
        tmp[i + 1] = d1c + ((s0c + s0n) >> 1);
    }

    tmp[i] = s0n;

    if (len & 1) {
        tmp[len - 1] =
            tiledp_col[(size_t)((len - 1) / 2) * stride] -
            ((d1n + 1) >> 1);
        tmp[len - 2] = d1n + ((s0n + tmp[len - 1]) >> 1);
    } else {
        tmp[len - 1] = d1n + s0n;
    }

    for (i = 0; i < len; ++i) {
        tiledp_col[(size_t)i * stride] = tmp[i];
    }
}


/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on odd coordinate */
static void decode_v_cas1_53(int32_t* tmp,
                             const int32_t sn,
                             const int32_t len,
                             int32_t* tiledp_col,
                             const size_t stride){
    int32_t i, j;
    const int32_t* in_even = &tiledp_col[(size_t)sn * stride];
    const int32_t* in_odd = &tiledp_col[0];

    assert(len > 2);

    /* Performs lifting in one single iteration. Saves memory */
    /* accesses and explicit interleaving. */

    int32_t s1 = in_even[stride];
    int32_t dc = in_odd[0] - ((in_even[0] + s1 + 2) >> 2);
    tmp[0] = in_even[0] + dc;
    for (i = 1, j = 1; i < (len - 2 - !(len & 1)); i += 2, j++) {

    	int32_t s2 = in_even[(size_t)(j + 1) * stride];

    	int32_t dn = in_odd[(size_t)j * stride] - ((s1 + s2 + 2) >> 2);
        tmp[i  ] = dc;
        tmp[i + 1] = s1 + ((dn + dc) >> 1);

        dc = dn;
        s1 = s2;
    }
    tmp[i] = dc;
    if (!(len & 1)) {
    	int32_t dn = in_odd[(size_t)(len / 2 - 1) * stride] - ((s1 + 1) >> 1);
        tmp[len - 2] = s1 + ((dn + dc) >> 1);
        tmp[len - 1] = dn;
    } else {
        tmp[len - 1] = s1 + dc;
    }
    for (i = 0; i < len; ++i)
        tiledp_col[(size_t)i * stride] = tmp[i];
}

/* <summary>                            */
/* Inverse vertical 5-3 wavelet transform in 1-D for several columns. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
static void decode_v_53(const dwt_data<int32_t> *dwt,
                         int32_t* tiledp_col,
                         size_t stride,
                         int32_t nb_cols){
    const int32_t sn = dwt->sn;
    const int32_t len = sn + dwt->dn;
    if (dwt->cas == 0) {
        /* If len == 1, unmodified value */

#if (defined(__SSE2__) || defined(__AVX2__))
        if (len > 1 && nb_cols == PLL_COLS_53) {
            /* Same as below general case, except that thanks to SSE2/AVX2 */
            /* we can efficiently process 8/16 columns in parallel */
            decode_v_cas0_mcols_SSE2_OR_AVX2_53(dwt->mem, sn, len, tiledp_col, stride);
            return;
        }
#endif
        if (len > 1) {
            for (int32_t c = 0; c < nb_cols; c++, tiledp_col++)
                decode_v_cas0_53(dwt->mem, sn, len, tiledp_col, stride);
            return;
        }
    } else {
        if (len == 1) {
            for (int32_t c = 0; c < nb_cols; c++, tiledp_col++)
                tiledp_col[0] /= 2;
            return;
        }
        else if (len == 2) {
            int32_t* out = dwt->mem;
            for (int32_t c = 0; c < nb_cols; c++, tiledp_col++) {
                const int32_t* in_even = &tiledp_col[(size_t)sn * stride];
                const int32_t* in_odd = &tiledp_col[0];

                out[1] = in_odd[0] - ((in_even[0] + 1) >> 1);
                out[0] = in_even[0] + out[1];

                for (int32_t i = 0; i < len; ++i)
                    tiledp_col[(size_t)i * stride] = out[i];
            }
            return;
        }

#if (defined(__SSE2__) || defined(__AVX2__))
        if (len > 2 && nb_cols == PLL_COLS_53) {
            /* Same as below general case, except that thanks to SSE2/AVX2 */
            /* we can efficiently process 8/16 columns in parallel */
            decode_v_cas1_mcols_SSE2_OR_AVX2_53(dwt->mem, sn, len, tiledp_col, stride);
            return;
        }
#endif
        if (len > 2) {
            for (int32_t c = 0; c < nb_cols; c++, tiledp_col++)
                decode_v_cas1_53(dwt->mem, sn, len, tiledp_col, stride);
            return;
        }
    }
}


/* <summary>                            */
/* Inverse wavelet transform in 2-D.    */
/* </summary>                           */
static bool decode_tile_53( TileComponent* tilec, uint32_t numres){
    if (numres == 1U)
        return true;

    auto tr = tilec->resolutions;

    /* width of the resolution level computed */
    uint32_t rw = (uint32_t)(tr->x1 - tr->x0);
    /* height of the resolution level computed */
    uint32_t rh = (uint32_t)(tr->y1 - tr->y0);

    uint32_t w = (uint32_t)(tilec->resolutions[tilec->minimum_num_resolutions - 1].x1 -
                                tilec->resolutions[tilec->minimum_num_resolutions - 1].x0);

    size_t num_threads = Scheduler::g_tp->num_threads();
    size_t h_mem_size = dwt_utils::max_resolution(tr, numres);
    /* overflow check */
    if (h_mem_size > (SIZE_MAX / PLL_COLS_53 / sizeof(int32_t))) {
        GROK_ERROR("Overflow");
        return false;
    }
    /* We need PLL_COLS_53 times the height of the array, */
    /* since for the vertical pass */
    /* we process PLL_COLS_53 columns at a time */
    dwt_data<int32_t> horiz;
    dwt_data<int32_t> vert;
    h_mem_size *= PLL_COLS_53 * sizeof(int32_t);
    bool rc = true;
    int32_t * GRK_RESTRICT tiledp = tilec->buf->get_ptr( 0, 0, 0, 0);
    while (--numres) {
        ++tr;
        horiz.sn = (int32_t)rw;
        vert.sn = (int32_t)rh;

        rw = (uint32_t)(tr->x1 - tr->x0);
        rh = (uint32_t)(tr->y1 - tr->y0);

        horiz.dn = (int32_t)(rw - (uint32_t)horiz.sn);
        horiz.cas = tr->x0 % 2;

        if (num_threads <= 1 || rh <= 1) {
        	if (!horiz.mem){
        	    if (! horiz.alloc(h_mem_size)) {
        	        GROK_ERROR("Out of memory");
        	        return false;
        	    }
        	    vert.mem = horiz.mem;
        	}
            for (uint32_t j = 0; j < rh; ++j)
                decode_h_53(&horiz, &tiledp[(size_t)j * w]);
        } else {
            uint32_t num_jobs = (uint32_t)num_threads;
            if (rh < num_jobs)
                num_jobs = rh;
            uint32_t step_j = (rh / num_jobs);
			std::vector< std::future<int> > results;
			for(uint32_t j = 0; j < num_jobs; ++j) {
               auto job = new decode_job<int32_t, dwt_data<int32_t>>(horiz,
											w,
											tiledp,
											j * step_j,
											j < (num_jobs - 1U) ? (j + 1U) * step_j : rh);
                if (!job->data.alloc(h_mem_size)) {
                    GROK_ERROR("Out of memory");
                    grk_aligned_free(horiz.mem);
                    return false;
                }
				results.emplace_back(
					Scheduler::g_tp->enqueue([job] {
					    for (uint32_t j = job->min_j; j < job->max_j; j++)
					        decode_h_53(&job->data, &job->tiledp[j * job->w]);
					    grk_aligned_free(job->data.mem);
					    delete job;
						return 0;
					})
				);
			}
			for(auto && result: results)
				result.get();
        }

        vert.dn = (int32_t)(rh - (uint32_t)vert.sn);
        vert.cas = tr->y0 % 2;

        if (num_threads <= 1 || rw <= 1) {
        	if (!horiz.mem){
        	    if (! horiz.alloc(h_mem_size)) {
        	        GROK_ERROR("Out of memory");
        	        return false;
        	    }
        	    vert.mem = horiz.mem;
        	}
            uint32_t j;
            for (j = 0; j + PLL_COLS_53 <= rw; j += PLL_COLS_53)
                decode_v_53(&vert, &tiledp[j], (size_t)w, PLL_COLS_53);
            if (j < rw)
                decode_v_53(&vert, &tiledp[j], (size_t)w, (int32_t)(rw - j));
        } else {
            uint32_t num_jobs = (uint32_t)num_threads;
            if (rw < num_jobs)
                num_jobs = rw;
            uint32_t step_j = (rw / num_jobs);
			std::vector< std::future<int> > results;
            for (uint32_t j = 0; j < num_jobs; j++) {
                auto job = new decode_job<int32_t, dwt_data<int32_t>>(vert,
											w,
											tiledp,
											j * step_j,
											j < (num_jobs - 1U) ? (j + 1U) * step_j : rw);
                if (!job->data.alloc(h_mem_size)) {
                    GROK_ERROR("Out of memory");
                    grk_aligned_free(vert.mem);
                    return false;
                }
				results.emplace_back(
					Scheduler::g_tp->enqueue([job] {
						uint32_t j;
						for (j = job->min_j; j + PLL_COLS_53 <= job->max_j;	j += PLL_COLS_53)
							decode_v_53(&job->data, &job->tiledp[j], (size_t)job->w, PLL_COLS_53);
						if (j < job->max_j)
							decode_v_53(&job->data, &job->tiledp[j], (size_t)job->w, (int32_t)(job->max_j - j));
						grk_aligned_free(job->data.mem);
						delete job;
					return 0;
					})
				);
            }
			for(auto && result: results)
				result.get();
        }
    }
    grk_aligned_free(horiz.mem);

    return rc;
}

static void interleave_partial_h_53(dwt_data<int32_t> *dwt,
									sparse_array* sa,
									uint32_t sa_line)	{

	int32_t *dest = dwt->mem;
	int32_t cas = dwt->cas;
	uint32_t sn = dwt->sn;
	uint32_t win_l_x0 = dwt->win_l_x0;
	uint32_t win_l_x1 = dwt->win_l_x1;
	uint32_t win_h_x0 = dwt->win_h_x0;
	uint32_t win_h_x1 = dwt->win_h_x1;

    bool ret = sparse_array_read(sa,
                                      win_l_x0, sa_line,
                                      win_l_x1, sa_line + 1,
                                      dest + cas + 2 * win_l_x0,
                                      2, 0, true);
    assert(ret);
    ret = sparse_array_read(sa,
                                      sn + win_h_x0, sa_line,
                                      sn + win_h_x1, sa_line + 1,
                                      dest + 1 - cas + 2 * win_h_x0,
                                      2, 0, true);
    assert(ret);
    GRK_UNUSED(ret);
}


static void interleave_partial_v_53(dwt_data<int32_t> *vert,
									sparse_array* sa,
									uint32_t sa_col,
									uint32_t nb_cols){
	int32_t *dest = vert->mem;
	int32_t cas = vert->cas;
	uint32_t sn = vert->sn;
	uint32_t win_l_y0 = vert->win_l_x0;
	uint32_t win_l_y1 = vert->win_l_x1;
	uint32_t win_h_y0 = vert->win_h_x0;
	uint32_t win_h_y1 = vert->win_h_x1;

    bool ret = sparse_array_read(sa,
                                       sa_col, win_l_y0,
                                       sa_col + nb_cols, win_l_y1,
                                       dest + cas * 4 + 2 * 4 * win_l_y0,
                                       1, 2 * 4, true);
    assert(ret);
    ret = sparse_array_read(sa,
                                      sa_col, sn + win_h_y0,
                                      sa_col + nb_cols, sn + win_h_y1,
                                      dest + (1 - cas) * 4 + 2 * 4 * win_h_y0,
                                      1, 2 * 4, true);
    assert(ret);
    GRK_UNUSED(ret);
}

static void decode_partial_h_53(dwt_data<int32_t> *horiz){
    int32_t i;
    int32_t *a = horiz->mem;
	int32_t dn = horiz->dn;
	int32_t sn = horiz->sn;
	 int32_t cas = horiz->cas;
	 int32_t win_l_x0 = horiz->win_l_x0;
	 int32_t win_l_x1 = horiz->win_l_x1;
	 int32_t win_h_x0 = horiz->win_h_x0;
	 int32_t win_h_x1 = horiz->win_h_x1;

    if (!cas) {
        if ((dn > 0) || (sn > 1)) { /* NEW :  CASE ONE ELEMENT */

            /* Naive version is :
            for (i = win_l_x0; i < i_max; i++) {
                GRK_S(i) -= (GRK_D_(i - 1) + GRK_D_(i) + 2) >> 2;
            }
            for (i = win_h_x0; i < win_h_x1; i++) {
                GRK_D(i) += (GRK_S_(i) + GRK_S_(i + 1)) >> 1;
            }
            but the compiler doesn't manage to unroll it to avoid bound
            checking in GRK_S_ and GRK_D_ macros
            */

            i = win_l_x0;
            if (i < win_l_x1) {
                int32_t i_max;

                /* Left-most case */
                GRK_S(i) -= (GRK_D_(i - 1) + GRK_D_(i) + 2) >> 2;
                i ++;

                i_max = win_l_x1;
                if (i_max > dn) {
                    i_max = dn;
                }
                for (; i < i_max; i++) {
                    /* No bound checking */
                    GRK_S(i) -= (GRK_D(i - 1) + GRK_D(i) + 2) >> 2;
                }
                for (; i < win_l_x1; i++) {
                    /* Right-most case */
                    GRK_S(i) -= (GRK_D_(i - 1) + GRK_D_(i) + 2) >> 2;
                }
            }

            i = win_h_x0;
            if (i < win_h_x1) {
                int32_t i_max = win_h_x1;
                if (i_max >= sn) {
                    i_max = sn - 1;
                }
                for (; i < i_max; i++) {
                    /* No bound checking */
                    GRK_D(i) += (GRK_S(i) + GRK_S(i + 1)) >> 1;
                }
                for (; i < win_h_x1; i++) {
                    /* Right-most case */
                    GRK_D(i) += (GRK_S_(i) + GRK_S_(i + 1)) >> 1;
                }
            }
        }
    } else {
        if (!sn  && dn == 1) {        /* NEW :  CASE ONE ELEMENT */
            GRK_S(0) /= 2;
        } else {
            for (i = win_l_x0; i < win_l_x1; i++) {
                GRK_D(i) -= (GRK_SS_(i) + GRK_SS_(i + 1) + 2) >> 2;
            }
            for (i = win_h_x0; i < win_h_x1; i++) {
                GRK_S(i) += (GRK_DD_(i) + GRK_DD_(i - 1)) >> 1;
            }
        }
    }
}

#define GRK_S_off(i,off) a[(uint32_t)(i)*2*4+off]
#define GRK_D_off(i,off) a[(1+(uint32_t)(i)*2)*4+off]
#define GRK_S__off(i,off) ((i)<0?GRK_S_off(0,off):((i)>=sn?GRK_S_off(sn-1,off):GRK_S_off(i,off)))
#define GRK_D__off(i,off) ((i)<0?GRK_D_off(0,off):((i)>=dn?GRK_D_off(dn-1,off):GRK_D_off(i,off)))
#define GRK_SS__off(i,off) ((i)<0?GRK_S_off(0,off):((i)>=dn?GRK_S_off(dn-1,off):GRK_S_off(i,off)))
#define GRK_DD__off(i,off) ((i)<0?GRK_D_off(0,off):((i)>=sn?GRK_D_off(sn-1,off):GRK_D_off(i,off)))

static void decode_partial_v_53(dwt_data<int32_t> *vert){
    int32_t i;
    uint32_t off;
    int32_t *a = vert->mem;
	int32_t dn = vert->dn;
	int32_t sn = vert->sn;
	int32_t cas = vert->cas;
	int32_t win_l_x0 = vert->win_l_x0;
	int32_t win_l_x1 = vert->win_l_x1;
	int32_t win_h_x0 = vert->win_h_x0;
	int32_t win_h_x1 = vert->win_h_x1;


    if (!cas) {
        if ((dn > 0) || (sn > 1)) { /* NEW :  CASE ONE ELEMENT */

            /* Naive version is :
            for (i = win_l_x0; i < i_max; i++) {
                GRK_S(i) -= (GRK_D_(i - 1) + GRK_D_(i) + 2) >> 2;
            }
            for (i = win_h_x0; i < win_h_x1; i++) {
                GRK_D(i) += (GRK_S_(i) + GRK_S_(i + 1)) >> 1;
            }
            but the compiler doesn't manage to unroll it to avoid bound
            checking in GRK_S_ and GRK_D_ macros
            */

            i = win_l_x0;
            if (i < win_l_x1) {
                int32_t i_max;

                /* Left-most case */
                for (off = 0; off < 4; off++) {
                    GRK_S_off(i, off) -= (GRK_D__off(i - 1, off) + GRK_D__off(i, off) + 2) >> 2;
                }
                i ++;

                i_max = win_l_x1;
                if (i_max > dn) {
                    i_max = dn;
                }

#ifdef __SSE2__
                if (i + 1 < i_max) {
                    const __m128i two = _mm_set1_epi32(2);
                    __m128i Dm1 = _mm_load_si128((__m128i *)(a + 4 + (i - 1) * 8));
                    for (; i + 1 < i_max; i += 2) {
                        /* No bound checking */
                        __m128i S = _mm_load_si128((__m128i *)(a + i * 8));
                        __m128i D = _mm_load_si128((__m128i *)(a + 4 + i * 8));
                        __m128i S1 = _mm_load_si128((__m128i *)(a + (i + 1) * 8));
                        __m128i D1 = _mm_load_si128((__m128i *)(a + 4 + (i + 1) * 8));
                        S = _mm_sub_epi32(S,
                                          _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(Dm1, D), two), 2));
                        S1 = _mm_sub_epi32(S1,
                                           _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(D, D1), two), 2));
                        _mm_store_si128((__m128i*)(a + i * 8), S);
                        _mm_store_si128((__m128i*)(a + (i + 1) * 8), S1);
                        Dm1 = D1;
                    }
                }
#endif

                for (; i < i_max; i++) {
                    /* No bound checking */
                    for (off = 0; off < 4; off++) {
                        GRK_S_off(i, off) -= (GRK_D_off(i - 1, off) + GRK_D_off(i, off) + 2) >> 2;
                    }
                }
                for (; i < win_l_x1; i++) {
                    /* Right-most case */
                    for (off = 0; off < 4; off++) {
                        GRK_S_off(i, off) -= (GRK_D__off(i - 1, off) + GRK_D__off(i, off) + 2) >> 2;
                    }
                }
            }

            i = win_h_x0;
            if (i < win_h_x1) {
                int32_t i_max = win_h_x1;
                if (i_max >= sn) {
                    i_max = sn - 1;
                }

#ifdef __SSE2__
                if (i + 1 < i_max) {
                    __m128i S =  _mm_load_si128((__m128i *)(a + i * 8));
                    for (; i + 1 < i_max; i += 2) {
                        /* No bound checking */
                        __m128i D = _mm_load_si128((__m128i *)(a + 4 + i * 8));
                        __m128i S1 = _mm_load_si128((__m128i *)(a + (i + 1) * 8));
                        __m128i D1 = _mm_load_si128((__m128i *)(a + 4 + (i + 1) * 8));
                        __m128i S2 = _mm_load_si128((__m128i *)(a + (i + 2) * 8));
                        D = _mm_add_epi32(D, _mm_srai_epi32(_mm_add_epi32(S, S1), 1));
                        D1 = _mm_add_epi32(D1, _mm_srai_epi32(_mm_add_epi32(S1, S2), 1));
                        _mm_store_si128((__m128i*)(a + 4 + i * 8), D);
                        _mm_store_si128((__m128i*)(a + 4 + (i + 1) * 8), D1);
                        S = S2;
                    }
                }
#endif

                for (; i < i_max; i++) {
                    /* No bound checking */
                    for (off = 0; off < 4; off++) {
                        GRK_D_off(i, off) += (GRK_S_off(i, off) + GRK_S_off(i + 1, off)) >> 1;
                    }
                }
                for (; i < win_h_x1; i++) {
                    /* Right-most case */
                    for (off = 0; off < 4; off++) {
                        GRK_D_off(i, off) += (GRK_S__off(i, off) + GRK_S__off(i + 1, off)) >> 1;
                    }
                }
            }
        }
    } else {
        if (!sn  && dn == 1) {        /* NEW :  CASE ONE ELEMENT */
            for (off = 0; off < 4; off++) {
                GRK_S_off(0, off) /= 2;
            }
        } else {
            for (i = win_l_x0; i < win_l_x1; i++) {
                for (off = 0; off < 4; off++) {
                    GRK_D_off(i, off) -= (GRK_SS__off(i, off) + GRK_SS__off(i + 1, off) + 2) >> 2;
                }
            }
            for (i = win_h_x0; i < win_h_x1; i++) {
                for (off = 0; off < 4; off++) {
                    GRK_S_off(i, off) += (GRK_DD__off(i, off) + GRK_DD__off(i - 1, off)) >> 1;
                }
            }
        }
    }
}

static void get_band_coordinates(TileComponent* tilec,
								uint32_t resno,
								uint32_t bandno,
								uint32_t tcx0,
								uint32_t tcy0,
								uint32_t tcx1,
								uint32_t tcy1,
								uint32_t* tbx0,
								uint32_t* tby0,
								uint32_t* tbx1,
								uint32_t* tby1){
    /* Compute number of decomposition for this band. See table F-1 */
    uint32_t nb = (resno == 0) ?
                    tilec->numresolutions - 1 :
                    tilec->numresolutions - resno;
    /* Map above tile-based coordinates to sub-band-based coordinates per */
    /* equation B-15 of the standard */
    uint32_t x0b = bandno & 1;
    uint32_t y0b = bandno >> 1;
    if (tbx0) {
        *tbx0 = (nb == 0) ? tcx0 :
                (tcx0 <= (1U << (nb - 1)) * x0b) ? 0 :
                uint_ceildivpow2(tcx0 - (1U << (nb - 1)) * x0b, nb);
    }
    if (tby0) {
        *tby0 = (nb == 0) ? tcy0 :
                (tcy0 <= (1U << (nb - 1)) * y0b) ? 0 :
                uint_ceildivpow2(tcy0 - (1U << (nb - 1)) * y0b, nb);
    }
    if (tbx1) {
        *tbx1 = (nb == 0) ? tcx1 :
                (tcx1 <= (1U << (nb - 1)) * x0b) ? 0 :
                uint_ceildivpow2(tcx1 - (1U << (nb - 1)) * x0b, nb);
    }
    if (tby1) {
        *tby1 = (nb == 0) ? tcy1 :
                (tcy1 <= (1U << (nb - 1)) * y0b) ? 0 :
                uint_ceildivpow2(tcy1 - (1U << (nb - 1)) * y0b, nb);
    }
}

static void segment_grow(uint32_t filter_width,
						 uint32_t max_size,
						 uint32_t* start,
						 uint32_t* end){
    *start = uint_subs(*start, filter_width);
    *end = uint_adds(*end, filter_width);
    *end = min<uint32_t>(*end, max_size);
}


static sparse_array* alloc_sparse_array(TileComponent* tilec,
												uint32_t numres){
    auto tr_max = &(tilec->resolutions[numres - 1]);
	uint32_t w = (uint32_t)(tr_max->x1 - tr_max->x0);
	uint32_t h = (uint32_t)(tr_max->y1 - tr_max->y0);
	auto sa = sparse_array_create(w, h, min<uint32_t>(w, 64), min<uint32_t>(h, 64));
	if (!sa)
		return nullptr;

    if (sa == nullptr)
        return nullptr;
    for (uint32_t resno = 0; resno < numres; ++resno) {
        auto res = &tilec->resolutions[resno];

        for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
            auto band = &res->bands[bandno];

            for (uint32_t precno = 0; precno < res->pw * res->ph; ++precno) {
                auto precinct = &band->precincts[precno];

                for (uint32_t cblkno = 0; cblkno < precinct->cw * precinct->ch; ++cblkno) {
                    auto cblk = &precinct->cblks.dec[cblkno];

                    if (cblk->unencoded_data != nullptr) {
                        uint32_t x = (uint32_t)(cblk->x0 - band->x0);
                        uint32_t y = (uint32_t)(cblk->y0 - band->y0);
                        uint32_t cblk_w = (uint32_t)(cblk->x1 - cblk->x0);
                        uint32_t cblk_h = (uint32_t)(cblk->y1 - cblk->y0);

                        if (band->bandno & 1) {
                            grk_tcd_resolution* pres = &tilec->resolutions[resno - 1];
                            x += (uint32_t)(pres->x1 - pres->x0);
                        }
                        if (band->bandno & 2) {
                            grk_tcd_resolution* pres = &tilec->resolutions[resno - 1];
                            y += (uint32_t)(pres->y1 - pres->y0);
                        }

                        if (!sparse_array_alloc(sa,
												  x,
												  y,
												  x + cblk_w,
												  y + cblk_h)) {
                            sparse_array_free(sa);
                            return nullptr;
                        }
                    }
                }
            }
        }
    }

    return sa;
}


static bool init_sparse_array(	sparse_array* sa,
									TileComponent* tilec,uint32_t numres){
	if (!sa)
		return false;
    for (uint32_t resno = 0; resno < numres; ++resno) {
        auto res = &tilec->resolutions[resno];

        for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
            auto band = &res->bands[bandno];

            for (uint32_t precno = 0; precno < res->pw * res->ph; ++precno) {
                auto precinct = &band->precincts[precno];

                for (uint32_t cblkno = 0; cblkno < precinct->cw * precinct->ch; ++cblkno) {
                    auto cblk = &precinct->cblks.dec[cblkno];

                    if (cblk->unencoded_data != nullptr) {
                        uint32_t x = (uint32_t)(cblk->x0 - band->x0);
                        uint32_t y = (uint32_t)(cblk->y0 - band->y0);
                        uint32_t cblk_w = (uint32_t)(cblk->x1 - cblk->x0);
                        uint32_t cblk_h = (uint32_t)(cblk->y1 - cblk->y0);

                        if (band->bandno & 1) {
                            grk_tcd_resolution* pres = &tilec->resolutions[resno - 1];
                            x += (uint32_t)(pres->x1 - pres->x0);
                        }
                        if (band->bandno & 2) {
                            grk_tcd_resolution* pres = &tilec->resolutions[resno - 1];
                            y += (uint32_t)(pres->y1 - pres->y0);
                        }

                        if (!sparse_array_write(sa,
												  x,
												  y,
												  x + cblk_w,
												  y + cblk_h,
												  cblk->unencoded_data,
												  1,
												  cblk_w,
												  true)) {
                            sparse_array_free(sa);
                            return false;
                        }
                    }
                }
            }
        }
    }

    return true;
}


class Partial53 {
public:
	void interleave_partial_h(dwt_data<int32_t>* dwt,
								sparse_array* sa,
								uint32_t sa_line,
								uint32_t num_rows){
		(void)num_rows;
		interleave_partial_h_53(dwt,sa,sa_line);
	}
	void decode_h(dwt_data<int32_t>* dwt){
		decode_partial_h_53(dwt);
	}
	void interleave_partial_v(dwt_data<int32_t>* GRK_RESTRICT dwt,
								sparse_array* sa,
								uint32_t sa_col,
								uint32_t nb_elts_read){
		interleave_partial_v_53(dwt,sa,sa_col,nb_elts_read);
	}
	void decode_v(dwt_data<int32_t>* dwt){
		decode_partial_v_53(dwt);
	}
};

/* <summary>                            */
/* Inverse 5-3 wavelet transform in 2-D. */
/* </summary>                           */
bool decode_53(TileProcessor *p_tcd, TileComponent* tilec,
                        uint32_t numres)
{
    if (p_tcd->whole_tile_decoding) {
        return decode_tile_53(tilec,numres);
    } else {
        return decode_partial_tile<int32_t, 1, 4,2, Partial53>(tilec, numres);
    }
}


static void interleave_h_97(dwt_data<v4_data>* GRK_RESTRICT dwt,
                                   float* GRK_RESTRICT a,
                                   uint32_t width,
                                   uint32_t remaining_height){
    float* GRK_RESTRICT bi = (float*)(dwt->mem + dwt->cas);
    uint32_t i, k;
    uint32_t x0 = dwt->win_l_x0;
    uint32_t x1 = dwt->win_l_x1;

    for (k = 0; k < 2; ++k) {
        if (remaining_height >= 4 && ((size_t) a & 0x0f) == 0 &&
                ((size_t) bi & 0x0f) == 0 && (width & 0x0f) == 0) {
            /* Fast code path */
            for (i = x0; i < x1; ++i) {
                uint32_t j = i;
                bi[i * 8    ] = a[j];
                j += width;
                bi[i * 8 + 1] = a[j];
                j += width;
                bi[i * 8 + 2] = a[j];
                j += width;
                bi[i * 8 + 3] = a[j];
            }
        } else {
            /* Slow code path */
            for (i = x0; i < x1; ++i) {
                uint32_t j = i;
                bi[i * 8    ] = a[j];
                j += width;
                if (remaining_height == 1) {
                    continue;
                }
                bi[i * 8 + 1] = a[j];
                j += width;
                if (remaining_height == 2) {
                    continue;
                }
                bi[i * 8 + 2] = a[j];
                j += width;
                if (remaining_height == 3) {
                    continue;
                }
                bi[i * 8 + 3] = a[j]; /* This one*/
            }
        }

        bi = (float*)(dwt->mem + 1 - dwt->cas);
        a += dwt->sn;
        x0 = dwt->win_h_x0;
        x1 = dwt->win_h_x1;
    }
}
static void interleave_partial_h_97(dwt_data<v4_data>* dwt,
									sparse_array* sa,
									uint32_t sa_line,
									uint32_t num_rows){
    uint32_t i;
    for (i = 0; i < num_rows; i++) {
        bool ret;
        ret = sparse_array_read(sa,
							  dwt->win_l_x0,
							  sa_line + i,
							  dwt->win_l_x1,
							  sa_line + i + 1,
							  /* Nasty cast from float* to int32* */
							  (int32_t*)(dwt->mem + dwt->cas + 2 * dwt->win_l_x0) + i,
							  8, 0, true);
        assert(ret);
        ret = sparse_array_read(sa,
							  (uint32_t)dwt->sn + dwt->win_h_x0,
							  sa_line + i,
							  (uint32_t)dwt->sn + dwt->win_h_x1,
							  sa_line + i + 1,
							  /* Nasty cast from float* to int32* */
							  (int32_t*)(dwt->mem + 1 - dwt->cas + 2 * dwt->win_h_x0) + i,
							  8, 0, true);
        assert(ret);
        GRK_UNUSED(ret);
    }
}

static void interleave_v_97(dwt_data<v4_data>* GRK_RESTRICT dwt,
                                   float* GRK_RESTRICT a,
                                   uint32_t width,
                                   uint32_t nb_elts_read){
    v4_data* GRK_RESTRICT bi = dwt->mem + dwt->cas;

    for (uint32_t i = dwt->win_l_x0; i < dwt->win_l_x1; ++i) {
        memcpy((float*)&bi[i * 2], &a[i * (size_t)width],
               (size_t)nb_elts_read * sizeof(float));
    }

    a += (uint32_t)dwt->sn * (size_t)width;
    bi = dwt->mem + 1 - dwt->cas;

    for (uint32_t i = dwt->win_h_x0; i < dwt->win_h_x1; ++i) {
        memcpy((float*)&bi[i * 2], &a[i * (size_t)width],
               (size_t)nb_elts_read * sizeof(float));
    }
}

static void interleave_partial_v_97(dwt_data<v4_data>* GRK_RESTRICT dwt,
									sparse_array* sa,
									uint32_t sa_col,
									uint32_t nb_elts_read){
    bool ret;
    ret = sparse_array_read(sa,
						  sa_col, dwt->win_l_x0,
						  sa_col + nb_elts_read, dwt->win_l_x1,
						  (int32_t*)(dwt->mem + dwt->cas + 2 * dwt->win_l_x0),
						  1, 8, true);
    assert(ret);
    ret = sparse_array_read(sa,
						  sa_col, (uint32_t)dwt->sn + dwt->win_h_x0,
						  sa_col + nb_elts_read, (uint32_t)dwt->sn + dwt->win_h_x1,
						  (int32_t*)(dwt->mem + 1 - dwt->cas + 2 * dwt->win_h_x0),
						  1, 8, true);
    assert(ret);
    GRK_UNUSED(ret);
}

#ifdef __SSE__
static void decode_step1_sse_97(v4_data* w,
                                       uint32_t start,
                                       uint32_t end,
                                       const __m128 c){
    __m128* GRK_RESTRICT vw = (__m128*) w;
    uint32_t i;
    /* 4x unrolled loop */
    vw += 2 * start;
    for (i = start; i + 3 < end; i += 4, vw += 8) {
        __m128 xmm0 = _mm_mul_ps(vw[0], c);
        __m128 xmm2 = _mm_mul_ps(vw[2], c);
        __m128 xmm4 = _mm_mul_ps(vw[4], c);
        __m128 xmm6 = _mm_mul_ps(vw[6], c);
        vw[0] = xmm0;
        vw[2] = xmm2;
        vw[4] = xmm4;
        vw[6] = xmm6;
    }
    for (; i < end; ++i, vw += 2) {
        vw[0] = _mm_mul_ps(vw[0], c);
    }
}

static void decode_step2_sse_97(v4_data* l, v4_data* w,
                                       uint32_t start,
                                       uint32_t end,
                                       uint32_t m,
                                       __m128 c){
    __m128* GRK_RESTRICT vl = (__m128*) l;
    __m128* GRK_RESTRICT vw = (__m128*) w;
    uint32_t i;
    uint32_t imax = min<uint32_t>(end, m);
    __m128 tmp1, tmp2, tmp3;
    if (start == 0) {
        tmp1 = vl[0];
    } else {
        vw += start * 2;
        tmp1 = vw[-3];
    }

    i = start;

    /* 4x loop unrolling */
    for (; i + 3 < imax; i += 4) {
        __m128 tmp4, tmp5, tmp6, tmp7, tmp8, tmp9;
        tmp2 = vw[-1];
        tmp3 = vw[ 0];
        tmp4 = vw[ 1];
        tmp5 = vw[ 2];
        tmp6 = vw[ 3];
        tmp7 = vw[ 4];
        tmp8 = vw[ 5];
        tmp9 = vw[ 6];
        vw[-1] = _mm_add_ps(tmp2, _mm_mul_ps(_mm_add_ps(tmp1, tmp3), c));
        vw[ 1] = _mm_add_ps(tmp4, _mm_mul_ps(_mm_add_ps(tmp3, tmp5), c));
        vw[ 3] = _mm_add_ps(tmp6, _mm_mul_ps(_mm_add_ps(tmp5, tmp7), c));
        vw[ 5] = _mm_add_ps(tmp8, _mm_mul_ps(_mm_add_ps(tmp7, tmp9), c));
        tmp1 = tmp9;
        vw += 8;
    }

    for (; i < imax; ++i) {
        tmp2 = vw[-1];
        tmp3 = vw[ 0];
        vw[-1] = _mm_add_ps(tmp2, _mm_mul_ps(_mm_add_ps(tmp1, tmp3), c));
        tmp1 = tmp3;
        vw += 2;
    }
    if (m < end) {
        assert(m + 1 == end);
        c = _mm_add_ps(c, c);
        c = _mm_mul_ps(c, vw[-2]);
        vw[-1] = _mm_add_ps(vw[-1], c);
    }
}
#else
static void decode_step1_97(v4_data* w,
                                   uint32_t start,
                                   uint32_t end,
                                   const float c){
    float* GRK_RESTRICT fw = (float*) w;
    uint32_t i;
    for (i = start; i < end; ++i) {
        float tmp1 = fw[i * 8    ];
        float tmp2 = fw[i * 8 + 1];
        float tmp3 = fw[i * 8 + 2];
        float tmp4 = fw[i * 8 + 3];
        fw[i * 8    ] = tmp1 * c;
        fw[i * 8 + 1] = tmp2 * c;
        fw[i * 8 + 2] = tmp3 * c;
        fw[i * 8 + 3] = tmp4 * c;
    }
}
static void decode_step2_97(v4_data* l, v4_data* w,
                                   uint32_t start,
                                   uint32_t end,
                                   uint32_t m,
                                   float c){
    float* fl = (float*) l;
    float* fw = (float*) w;
    uint32_t i;
    uint32_t imax = min<uint32_t>(end, m);
    if (start > 0) {
        fw += 8 * start;
        fl = fw - 8;
    }
    for (i = start; i < imax; ++i) {
        float tmp1_1 = fl[0];
        float tmp1_2 = fl[1];
        float tmp1_3 = fl[2];
        float tmp1_4 = fl[3];
        float tmp2_1 = fw[-4];
        float tmp2_2 = fw[-3];
        float tmp2_3 = fw[-2];
        float tmp2_4 = fw[-1];
        float tmp3_1 = fw[0];
        float tmp3_2 = fw[1];
        float tmp3_3 = fw[2];
        float tmp3_4 = fw[3];
        fw[-4] = tmp2_1 + ((tmp1_1 + tmp3_1) * c);
        fw[-3] = tmp2_2 + ((tmp1_2 + tmp3_2) * c);
        fw[-2] = tmp2_3 + ((tmp1_3 + tmp3_3) * c);
        fw[-1] = tmp2_4 + ((tmp1_4 + tmp3_4) * c);
        fl = fw;
        fw += 8;
    }
    if (m < end) {
        assert(m + 1 == end);
        c += c;
        fw[-4] = fw[-4] + fl[0] * c;
        fw[-3] = fw[-3] + fl[1] * c;
        fw[-2] = fw[-2] + fl[2] * c;
        fw[-1] = fw[-1] + fl[3] * c;
    }
}
#endif

/* <summary>                             */
/* Inverse 9-7 wavelet transform in 1-D. */
/* </summary>                            */
static void decode_step_97(dwt_data<v4_data>* GRK_RESTRICT dwt)
{
    int32_t a, b;

    if (dwt->cas == 0) {
        if (!((dwt->dn > 0) || (dwt->sn > 1))) {
            return;
        }
        a = 0;
        b = 1;
    } else {
        if (!((dwt->sn > 0) || (dwt->dn > 1))) {
            return;
        }
        a = 1;
        b = 0;
    }
#ifdef __SSE__
    decode_step1_sse_97(dwt->mem + a, dwt->win_l_x0, dwt->win_l_x1,
                               _mm_set1_ps(K));
    decode_step1_sse_97(dwt->mem + b, dwt->win_h_x0, dwt->win_h_x1,
                               _mm_set1_ps(c13318));
    decode_step2_sse_97(dwt->mem + b, dwt->mem + a + 1,
                               dwt->win_l_x0, dwt->win_l_x1,
                               (uint32_t)min<int32_t>(dwt->sn, dwt->dn - a),
                               _mm_set1_ps(dwt_delta));
    decode_step2_sse_97(dwt->mem + a, dwt->mem + b + 1,
                               dwt->win_h_x0, dwt->win_h_x1,
                               (uint32_t)min<int32_t>(dwt->dn, dwt->sn - b),
                               _mm_set1_ps(dwt_gamma));
    decode_step2_sse_97(dwt->mem + b, dwt->mem + a + 1,
                               dwt->win_l_x0, dwt->win_l_x1,
                               (uint32_t)min<int32_t>(dwt->sn, dwt->dn - a),
                               _mm_set1_ps(dwt_beta));
    decode_step2_sse_97(dwt->mem + a, dwt->mem + b + 1,
                               dwt->win_h_x0, dwt->win_h_x1,
                               (uint32_t)min<int32_t>(dwt->dn, dwt->sn - b),
                               _mm_set1_ps(dwt_alpha));
#else
    decode_step1_97(dwt->mem + a, dwt->win_l_x0, dwt->win_l_x1,
                           K);
    decode_step1_97(dwt->mem + b, dwt->win_h_x0, dwt->win_h_x1,
                           c13318);
    decode_step2_97(dwt->mem + b, dwt->mem + a + 1,
                           dwt->win_l_x0, dwt->win_l_x1,
                           (uint32_t)min<int32_t>(dwt->sn, dwt->dn - a),
                           dwt_delta);
    decode_step2_97(dwt->mem + a, dwt->mem + b + 1,
                           dwt->win_h_x0, dwt->win_h_x1,
                           (uint32_t)min<int32_t>(dwt->dn, dwt->sn - b),
                           dwt_gamma);
    decode_step2_97(dwt->mem + b, dwt->mem + a + 1,
                           dwt->win_l_x0, dwt->win_l_x1,
                           (uint32_t)min<int32_t>(dwt->sn, dwt->dn - a),
                           dwt_beta);
    decode_step2_97(dwt->mem + a, dwt->mem + b + 1,
                           dwt->win_h_x0, dwt->win_h_x1,
                           (uint32_t)min<int32_t>(dwt->dn, dwt->sn - b),
                           dwt_alpha);
#endif
}


/* <summary>                             */
/* Inverse 9-7 wavelet transform in 2-D. */
/* </summary>                            */
static
bool decode_tile_97(TileComponent* GRK_RESTRICT tilec,uint32_t numres){
    if (numres == 1U)
        return true;

    auto res = tilec->resolutions;
    /* width of the resolution level computed */
    uint32_t rw = (uint32_t)(res->x1 - res->x0);
    /* height of the resolution level computed */
    uint32_t rh = (uint32_t)(res->y1 - res->y0);
    uint32_t w = (uint32_t)(tilec->resolutions[tilec->minimum_num_resolutions - 1].x1 -
                            tilec->resolutions[tilec->minimum_num_resolutions - 1].x0);

    size_t data_size = dwt_utils::max_resolution(res, numres);
    dwt_data<v4_data> horiz;
    dwt_data<v4_data> vert;
    if (!horiz.alloc(data_size)) {
        GROK_ERROR("Out of memory");
        return false;
    }
    vert.mem = horiz.mem;
    size_t num_threads = Scheduler::g_tp->num_threads();
    while (--numres) {
        horiz.sn = (int32_t)rw;
        vert.sn = (int32_t)rh;
        ++res;
        /* width of the resolution level computed */
        rw = (uint32_t)(res->x1 -  res->x0);
        /* height of the resolution level computed */
        rh = (uint32_t)(res->y1 -  res->y0);
        horiz.dn = (int32_t)(rw - (uint32_t)horiz.sn);
        horiz.cas = res->x0 % 2;
        horiz.win_l_x0 = 0;
        horiz.win_l_x1 = (uint32_t)horiz.sn;
        horiz.win_h_x0 = 0;
        horiz.win_h_x1 = (uint32_t)horiz.dn;
        uint32_t j;
        float * GRK_RESTRICT tiledp = (float*) tilec->buf->get_ptr( 0, 0, 0, 0);
        uint32_t num_jobs = (uint32_t)num_threads;
        if (rh < num_jobs)
            num_jobs = rh;
        uint32_t step_j = num_jobs ? (rh / num_jobs) : 0;
        if (step_j < 4) {
			for (j = 0; j + 3 < rh; j += 4) {
				interleave_h_97(&horiz, tiledp, w, rh - j);
				decode_step_97(&horiz);
				for (uint32_t k = 0; k < rw; k++) {
					tiledp[k      ] 			= horiz.mem[k].f[0];
					tiledp[k + (size_t)w  ] 	= horiz.mem[k].f[1];
					tiledp[k + (size_t)w * 2] 	= horiz.mem[k].f[2];
					tiledp[k + (size_t)w * 3] 	= horiz.mem[k].f[3];
				}
				tiledp += w * 4;
			}
			if (j < rh) {
				interleave_h_97(&horiz, tiledp, w, rh - j);
				decode_step_97(&horiz);
				for (uint32_t k = 0; k < rw; k++) {
					switch (rh - j) {
					case 3:
						tiledp[k + (size_t)w * 2] = horiz.mem[k].f[2];
					/* FALLTHRU */
					case 2:
						tiledp[k + (size_t)w  ] = horiz.mem[k].f[1];
					/* FALLTHRU */
					case 1:
						tiledp[k] = horiz.mem[k].f[0];
					}
				}
			}
        } else {
			std::vector< std::future<int> > results;
			for(uint32_t j = 0; j < num_jobs; ++j) {
			   auto job = new decode_job<float, dwt_data<v4_data>>(horiz,
											w,
											tiledp,
											j * step_j,
											j < (num_jobs - 1U) ? (j + 1U) * step_j : rh);
				if (!job->data.alloc(data_size)) {
					GROK_ERROR("Out of memory");
					horiz.release();
					return false;
				}
				results.emplace_back(
					Scheduler::g_tp->enqueue([job,w,rw] {
					    float* tdp = nullptr;
					    uint32_t j;
						for (j = job->min_j; j + 3 < job->max_j; j+=4){
							tdp = &job->tiledp[j * job->w];
							interleave_h_97(&job->data, tdp, w, job->max_j - j);
							decode_step_97(&job->data);
							for (uint32_t k = 0; k < rw; k++) {
								tdp[k      ] 			= job->data.mem[k].f[0];
								tdp[k + (size_t)w  ] 	= job->data.mem[k].f[1];
								tdp[k + (size_t)w * 2] 	= job->data.mem[k].f[2];
								tdp[k + (size_t)w * 3] 	= job->data.mem[k].f[3];
							}
						}
						if (j < job->max_j) {
							tdp += 4 * job->w;
							interleave_h_97(&job->data, tdp, w, job->max_j - j);
							decode_step_97(&job->data);
							for (uint32_t k = 0; k < rw; k++) {
								switch (job->max_j - j) {
								case 3:
									tdp[k + (size_t)w * 2] = job->data.mem[k].f[2];
								/* FALLTHRU */
								case 2:
									tdp[k + (size_t)w  ] = job->data.mem[k].f[1];
								/* FALLTHRU */
								case 1:
									tdp[k] = job->data.mem[k].f[0];
								}
							}
						}
						job->data.release();
						delete job;
						return 0;
					})
				);
			}
			for(auto && result: results)
				result.get();
        }
        vert.dn = (int32_t)(rh - vert.sn);
        vert.cas = res->y0 % 2;
        vert.win_l_x0 = 0;
        vert.win_l_x1 = (uint32_t)vert.sn;
        vert.win_h_x0 = 0;
        vert.win_h_x1 = (uint32_t)vert.dn;
        tiledp = (float*) tilec->buf->get_ptr( 0, 0, 0, 0);
        num_jobs = (uint32_t)num_threads;
        if (rw < num_jobs)
            num_jobs = rw;
        step_j = num_jobs ? (rw / num_jobs) : 0;
        if (step_j < 4) {
			for (j = 0; j + 3 < rw; j += 4) {
				interleave_v_97(&vert, tiledp, w, 4);
				decode_step_97(&vert);
				for (uint32_t k = 0; k < rh; ++k)
					memcpy(&tiledp[k * (size_t)w], &vert.mem[k], 4 * sizeof(float));
				 tiledp += 4;
			}
			if (j < rw) {
				j = rw & 0x03;
				interleave_v_97(&vert, tiledp, w, j);
				decode_step_97(&vert);
				for (uint32_t k = 0; k < rh; ++k)
					memcpy(&tiledp[k * (size_t)w], &vert.mem[k],(size_t)j * sizeof(float));
			}
        } else {
			std::vector< std::future<int> > results;
            for (uint32_t j = 0; j < num_jobs; j++) {
            	auto job = new decode_job<float, dwt_data<v4_data>>(vert,
            												w,
            												tiledp,
            												j * step_j,
            												j < (num_jobs - 1U) ? (j + 1U) * step_j : rw);
				if (!job->data.alloc(data_size)) {
					GROK_ERROR("Out of memory");
					horiz.release();
					return false;
				}
				results.emplace_back(
					Scheduler::g_tp->enqueue([job,rh] {
						float* tdp = job->tiledp + job->min_j;
						uint32_t w = job->w;
						uint32_t j;
						for (j = job->min_j; j + 3 < job->max_j; j+=4){
							interleave_v_97(&job->data, tdp, w, 4);
							decode_step_97(&job->data);
							for (uint32_t k = 0; k < rh; ++k)
								memcpy(&tdp[k * (size_t)job->w], &job->data.mem[k], 4 * sizeof(float));
							tdp += 4;
						}
						if (j < job->max_j) {
							j = job->max_j - j;
							interleave_v_97(&job->data, tdp, w, j);
							decode_step_97(&job->data);
							for (uint32_t k = 0; k < rh; ++k)
								memcpy(&tdp[k * (size_t)w], &job->data.mem[k],(size_t)j * sizeof(float));
						}
						job->data.release();
						delete job;
						return 0;
					})
				);
            }
			for(auto && result: results)
				result.get();
        }
    }
    horiz.release();

    return true;
}

class Partial97 {
public:
	void interleave_partial_h(dwt_data<v4_data>* dwt,
								sparse_array* sa,
								uint32_t sa_line,
								uint32_t num_rows){
		interleave_partial_h_97(dwt,sa,sa_line,num_rows);
	}
	void decode_h(dwt_data<v4_data>* dwt){
		decode_step_97(dwt);
	}
	void interleave_partial_v(dwt_data<v4_data>* GRK_RESTRICT dwt,
								sparse_array* sa,
								uint32_t sa_col,
								uint32_t nb_elts_read){
		interleave_partial_v_97(dwt,sa,sa_col,nb_elts_read);
	}
	void decode_v(dwt_data<v4_data>* dwt){
		decode_step_97(dwt);
	}
};


/* FILTER_WIDTH value matches the maximum left/right extension given in tables */
/* F.2 and F.3 of the standard. Note: in tcd_is_subband_area_of_interest() */
/* we currently use 3. */
template <typename T, uint32_t HORIZ_STEP, uint32_t VERT_STEP, uint32_t FILTER_WIDTH, typename D>
   bool decode_partial_tile(TileComponent* GRK_RESTRICT tilec, uint32_t numres)
{
	dwt_data<T> horiz;
	dwt_data<T> vert;
    uint32_t resno;

    auto tr = tilec->resolutions;
    auto tr_max = &(tilec->resolutions[numres - 1]);

    /* width of the resolution level computed */
    uint32_t rw = (uint32_t)(tr->x1 - tr->x0);
    /* height of the resolution level computed */
    uint32_t rh = (uint32_t)(tr->y1 - tr->y0);

    /* Compute the intersection of the area of interest, expressed in tile coordinates */
    /* with the tile coordinates */
    auto dim = tilec->buf->unreduced_image_dim;

    uint32_t win_tcx0 = (uint32_t)dim.x0;
    uint32_t win_tcy0 = (uint32_t)dim.y0;
    uint32_t win_tcx1 = (uint32_t)dim.x1;
    uint32_t win_tcy1 = (uint32_t)dim.y1;

    if (tr_max->x0 == tr_max->x1 || tr_max->y0 == tr_max->y1)
        return true;

    auto sa = alloc_sparse_array(tilec, numres);
    if (!sa)
        return false;
    if (!init_sparse_array(sa, tilec, numres))
    	return false;

    if (numres == 1U) {
        bool ret = sparse_array_read(sa,
                       tr_max->win_x0 - (uint32_t)tr_max->x0,
                       tr_max->win_y0 - (uint32_t)tr_max->y0,
                       tr_max->win_x1 - (uint32_t)tr_max->x0,
                       tr_max->win_y1 - (uint32_t)tr_max->y0,
					   tilec->buf->get_ptr(0,0,0,0),
                       1, tr_max->win_x1 - tr_max->win_x0,
                       true);
        assert(ret);
        GRK_UNUSED(ret);
        sparse_array_free(sa);
        return true;
    }

    // in 53 vertical pass, we process 4 vertical columns at a time
    const uint32_t data_multiplier = (sizeof(T) == 4) ? 4 : 1;
    size_t data_size = dwt_utils::max_resolution(tr, numres) * data_multiplier;
    if (!horiz.alloc(data_size)) {
        GROK_ERROR("Out of memory");
        sparse_array_free(sa);
        return false;
    }
    vert.mem = horiz.mem;
    D decoder;
    size_t num_threads = Scheduler::g_tp->num_threads();

    for (resno = 1; resno < numres; resno ++) {
        uint32_t j;
        /* Window of interest sub-band-based coordinates */
        uint32_t win_ll_x0, win_ll_y0;
        uint32_t win_ll_x1, win_ll_y1;
        uint32_t win_hl_x0, win_hl_x1;
        uint32_t win_lh_y0, win_lh_y1;
        /* Window of interest tile-resolution-based coordinates */
        uint32_t win_tr_x0, win_tr_x1, win_tr_y0, win_tr_y1;
        /* Tile-resolution sub-band-based coordinates */
        uint32_t tr_ll_x0, tr_ll_y0, tr_hl_x0, tr_lh_y0;

        horiz.sn = (int32_t)rw;
        vert.sn = (int32_t)rh;

        ++tr;
        rw = (uint32_t)(tr->x1 - tr->x0);
        rh = (uint32_t)(tr->y1 - tr->y0);

        horiz.dn = (int32_t)(rw - (uint32_t)horiz.sn);
        horiz.cas = tr->x0 % 2;

        vert.dn = (int32_t)(rh - (uint32_t)vert.sn);
        vert.cas = tr->y0 % 2;

        /* Get the sub-band coordinates for the window of interest */
        /* LL band */
        get_band_coordinates(tilec, resno, 0,
                                     win_tcx0, win_tcy0, win_tcx1, win_tcy1,
                                     &win_ll_x0, &win_ll_y0,
                                     &win_ll_x1, &win_ll_y1);
        /* HL band */
        get_band_coordinates(tilec, resno, 1,
                                     win_tcx0, win_tcy0, win_tcx1, win_tcy1,
                                     &win_hl_x0, nullptr, &win_hl_x1, nullptr);
        /* LH band */
        get_band_coordinates(tilec, resno, 2,
                                     win_tcx0, win_tcy0, win_tcx1, win_tcy1,
                                     nullptr, &win_lh_y0, nullptr, &win_lh_y1);

        /* Beware: band index for non-LL0 resolution are 0=HL, 1=LH and 2=HH */
        tr_ll_x0 = (uint32_t)tr->bands[1].x0;
        tr_ll_y0 = (uint32_t)tr->bands[0].y0;
        tr_hl_x0 = (uint32_t)tr->bands[0].x0;
        tr_lh_y0 = (uint32_t)tr->bands[1].y0;

        /* Subtract the origin of the bands for this tile, to the sub-window */
        /* of interest band coordinates, so as to get them relative to the */
        /* tile */
        win_ll_x0 = uint_subs(win_ll_x0, tr_ll_x0);
        win_ll_y0 = uint_subs(win_ll_y0, tr_ll_y0);
        win_ll_x1 = uint_subs(win_ll_x1, tr_ll_x0);
        win_ll_y1 = uint_subs(win_ll_y1, tr_ll_y0);
        win_hl_x0 = uint_subs(win_hl_x0, tr_hl_x0);
        win_hl_x1 = uint_subs(win_hl_x1, tr_hl_x0);
        win_lh_y0 = uint_subs(win_lh_y0, tr_lh_y0);
        win_lh_y1 = uint_subs(win_lh_y1, tr_lh_y0);

        segment_grow(FILTER_WIDTH, (uint32_t)horiz.sn, &win_ll_x0, &win_ll_x1);
        segment_grow(FILTER_WIDTH, (uint32_t)horiz.dn, &win_hl_x0, &win_hl_x1);

        segment_grow(FILTER_WIDTH, (uint32_t)vert.sn, &win_ll_y0, &win_ll_y1);
        segment_grow(FILTER_WIDTH, (uint32_t)vert.dn, &win_lh_y0, &win_lh_y1);

        /* Compute the tile-resolution-based coordinates for the window of interest */
        if (horiz.cas == 0) {
            win_tr_x0 = min<uint32_t>(2 * win_ll_x0, 2 * win_hl_x0 + 1);
            win_tr_x1 = min<uint32_t>(max<uint32_t>(2 * win_ll_x1, 2 * win_hl_x1 + 1), rw);
        } else {
            win_tr_x0 = min<uint32_t>(2 * win_hl_x0, 2 * win_ll_x0 + 1);
            win_tr_x1 = min<uint32_t>(max<uint32_t>(2 * win_hl_x1, 2 * win_ll_x1 + 1), rw);
        }
        if (vert.cas == 0) {
            win_tr_y0 = min<uint32_t>(2 * win_ll_y0, 2 * win_lh_y0 + 1);
            win_tr_y1 = min<uint32_t>(max<uint32_t>(2 * win_ll_y1, 2 * win_lh_y1 + 1), rh);
        } else {
            win_tr_y0 = min<uint32_t>(2 * win_lh_y0, 2 * win_ll_y0 + 1);
            win_tr_y1 = min<uint32_t>(max<uint32_t>(2 * win_lh_y1, 2 * win_ll_y1 + 1), rh);
        }
        // two windows only overlap at most at the boundary
        uint32_t bounds[2][2] ={
        						{
        						  uint_subs(win_ll_y0, HORIZ_STEP),
        		                  win_ll_y1
        						},
							    {
							      max<uint32_t>(win_ll_y1, uint_subs(min<uint32_t>(win_lh_y0 + (uint32_t)vert.sn,rh),HORIZ_STEP)),
							      min<uint32_t>(win_lh_y1 + (uint32_t)vert.sn, rh)}
        						};

        // allocate all sparse array blocks in advance
        if (!sparse_array_alloc(sa,
								  win_tr_x0,
								  win_tr_y0,
								  win_tr_x1,
								  win_tr_y1)) {
			 sparse_array_free(sa);
			 return false;
		 }
		for (uint32_t k = 0; k < 2; ++k) {
			 if (!sparse_array_alloc(sa,
									  win_tr_x0,
									  bounds[k][0],
									  win_tr_x1,
									  bounds[k][1])) {
				 sparse_array_free(sa);
				 return false;
			 }
		}

        horiz.win_l_x0 = win_ll_x0;
        horiz.win_l_x1 = win_ll_x1;
        horiz.win_h_x0 = win_hl_x0;
        horiz.win_h_x1 = win_hl_x1;
		for (uint32_t k = 0; k < 2; ++k) {
	        /* Avoids dwt.c:1584:44 (in dwt_decode_partial_1): runtime error: */
	        /* signed integer overflow: -1094795586 + -1094795586 cannot be represented in type 'int' */
	        /* on decompress -i  ../../openjpeg/MAPA.jp2 -o out.tif -d 0,0,256,256 */
	        /* This is less extreme than memsetting the whole buffer to 0 */
	        /* although we could potentially do better with better handling of edge conditions */
	        if (win_tr_x1 >= 1 && win_tr_x1 < rw)
	            horiz.mem[win_tr_x1 - 1] = 0;
	        if (win_tr_x1 < rw)
	            horiz.mem[win_tr_x1] = 0;

			uint32_t num_jobs = (uint32_t)num_threads;
			uint32_t num_cols = bounds[k][1] - bounds[k][0] + 1;
			if (num_cols < num_jobs)
				num_jobs = num_cols;
			uint32_t step_j = num_jobs ? ( num_cols / num_jobs) : 0;
			if (step_j < HORIZ_STEP){
			 for (j = bounds[k][0]; j + HORIZ_STEP-1 < bounds[k][1]; j += HORIZ_STEP) {
				 decoder.interleave_partial_h(&horiz, sa, j,HORIZ_STEP);
				 decoder.decode_h(&horiz);
				 if (!sparse_array_write(sa,
									  win_tr_x0,
									  j,
									  win_tr_x1,
									  j + HORIZ_STEP,
									  (int32_t*)(horiz.mem + win_tr_x0),
									  HORIZ_STEP,
									  1,
									  true)) {
					 GROK_ERROR("sparse array write failure");
					 sparse_array_free(sa);
					 horiz.release();
					 return false;
				 }
			 }
			 if (j < bounds[k][1] ) {
				 decoder.interleave_partial_h(&horiz, sa, j, bounds[k][1] - j);
				 decoder.decode_h(&horiz);
				 if (!sparse_array_write(sa,
									  win_tr_x0,
									  j,
									  win_tr_x1,
									  bounds[k][1],
									  (int32_t*)(horiz.mem + win_tr_x0),
									  HORIZ_STEP,
									  1,
									  true)) {
					 GROK_ERROR("Sparse array write failure");
					 sparse_array_free(sa);
					 horiz.release();
					 return false;
				 }
			 }
		}else{
			std::vector< std::future<int> > results;
			for(uint32_t j = 0; j < num_jobs; ++j) {
			   auto job = new decode_job<float, dwt_data<T>>(horiz,
											0,
											nullptr,
											bounds[k][0] + j * step_j,
											j < (num_jobs - 1U) ? bounds[k][0] + (j + 1U) * step_j : bounds[k][1]);
				if (!job->data.alloc(data_size)) {
					GROK_ERROR("Out of memory");
					horiz.release();
					return false;
				}
				results.emplace_back(
					Scheduler::g_tp->enqueue([job,sa, win_tr_x0, win_tr_x1, &decoder] {
					 uint32_t j;
					 for (j = job->min_j; j + HORIZ_STEP-1 < job->max_j; j += HORIZ_STEP) {
						 decoder.interleave_partial_h(&job->data, sa, j,HORIZ_STEP);
						 decoder.decode_h(&job->data);
						 if (!sparse_array_write(sa,
											  win_tr_x0,
											  j,
											  win_tr_x1,
											  j + HORIZ_STEP,
											  (int32_t*)(job->data.mem + win_tr_x0),
											  HORIZ_STEP,
											  1,
											  true)) {
							 GROK_ERROR("sparse array write failure");
							 sparse_array_free(sa);
							 job->data.release();
							 return 0;
						 }
					 }
					 if (j < job->max_j ) {
						 decoder.interleave_partial_h(&job->data, sa, j, job->max_j - j);
						 decoder.decode_h(&job->data);
						 if (!sparse_array_write(sa,
											  win_tr_x0,
											  j,
											  win_tr_x1,
											  job->max_j,
											  (int32_t*)(job->data.mem + win_tr_x0),
											  HORIZ_STEP,
											  1,
											  true)) {
							 GROK_ERROR("Sparse array write failure");
							 sparse_array_free(sa);
							 job->data.release();
							 return 0;
						 }
					  }
					  job->data.release();
					  delete job;
					  return 0;
					})
				);
			}
			for(auto && result: results)
				result.get();
		   }
        }

		vert.win_l_x0 = win_ll_y0;
        vert.win_l_x1 = win_ll_y1;
        vert.win_h_x0 = win_lh_y0;
        vert.win_h_x1 = win_lh_y1;

        uint32_t num_jobs = (uint32_t)num_threads;
        uint32_t num_cols = win_tr_x1 - win_tr_x0 + 1;
		if (num_cols < num_jobs)
			num_jobs = num_cols;
		uint32_t step_j = num_jobs ? ( num_cols / num_jobs) : 0;
		if (step_j < VERT_STEP){
			for (j = win_tr_x0; j + VERT_STEP < win_tr_x1; j += VERT_STEP) {
				decoder.interleave_partial_v(&vert, sa, j, VERT_STEP);
				decoder.decode_v(&vert);
				if (!sparse_array_write(sa,
										  j,
										  win_tr_y0,
										  j + VERT_STEP,
										  win_tr_y1,
										  (int32_t*)vert.mem + VERT_STEP * win_tr_y0,
										  1,
										  VERT_STEP,
										  true)) {
					GROK_ERROR("Sparse array write failure");
					sparse_array_free(sa);
					horiz.release();
					return false;
				}
			}
			if (j < win_tr_x1) {
				decoder.interleave_partial_v(&vert, sa, j, win_tr_x1 - j);
				decoder.decode_v(&vert);
				if (!sparse_array_write(sa,
										  j,
										  win_tr_y0,
										  win_tr_x1,
										  win_tr_y1,
										  (int32_t*)vert.mem + VERT_STEP * win_tr_y0,
										  1,
										  VERT_STEP,
										  true)) {
					GROK_ERROR("Sparse array write failure");
					sparse_array_free(sa);
					horiz.release();
					return false;
				}
			}
		} else {
			std::vector< std::future<int> > results;
			for(uint32_t j = 0; j < num_jobs; ++j) {
			   auto job = new decode_job<float, dwt_data<T>>(vert,
											0,
											nullptr,
											win_tr_x0 + j * step_j,
											j < (num_jobs - 1U) ? win_tr_x0 + (j + 1U) * step_j : win_tr_x1);
				if (!job->data.alloc(data_size)) {
					GROK_ERROR("Out of memory");
					horiz.release();
					return false;
				}
				results.emplace_back(
					Scheduler::g_tp->enqueue([job,sa, win_tr_y0, win_tr_y1, &decoder] {
					 uint32_t j;
					 for (j = job->min_j; j + VERT_STEP-1 < job->max_j; j += VERT_STEP) {
						decoder.interleave_partial_v(&job->data, sa, j, VERT_STEP);
						decoder.decode_v(&job->data);
						if (!sparse_array_write(sa,
												  j,
												  win_tr_y0,
												  j + VERT_STEP,
												  win_tr_y1,
												  (int32_t*)job->data.mem + VERT_STEP * win_tr_y0,
												  1,
												  VERT_STEP,
												  true)) {
							GROK_ERROR("Sparse array write failure");
							sparse_array_free(sa);
							job->data.release();
							return 0;
						}
					 }
					 if (j <  job->max_j) {
						decoder.interleave_partial_v(&job->data, sa, j,  job->max_j - j);
						decoder.decode_v(&job->data);
						if (!sparse_array_write(sa,
												  j,
												  win_tr_y0,
												  job->max_j,
												  win_tr_y1,
												  (int32_t*)job->data.mem + VERT_STEP * win_tr_y0,
												  1,
												  VERT_STEP,
												  true)) {
							GROK_ERROR("Sparse array write failure");
							sparse_array_free(sa);
							job->data.release();
							return 0;
						}
					}

				  job->data.release();
				  delete job;
				  return 0;
				})
				);
			}
			for(auto && result: results)
				result.get();
		}
    }

    //final read into tile buffer
	bool ret = sparse_array_read(sa,
							   tr_max->win_x0 - (uint32_t)tr_max->x0,
							   tr_max->win_y0 - (uint32_t)tr_max->y0,
							   tr_max->win_x1 - (uint32_t)tr_max->x0,
							   tr_max->win_y1 - (uint32_t)tr_max->y0,
							   tilec->buf->get_ptr(0,0,0,0),
							   1,
							   tr_max->win_x1 - tr_max->win_x0,
							   true);
	assert(ret);
	GRK_UNUSED(ret);
    sparse_array_free(sa);
    horiz.release();

    return true;
}

bool decode_97(TileProcessor *p_tcd,
                TileComponent* GRK_RESTRICT tilec,
                uint32_t numres){
    if (p_tcd->whole_tile_decoding) {
        return decode_tile_97(tilec, numres);
    } else {
        return decode_partial_tile<v4_data,4,4,4, Partial97>(tilec, numres);
    }
}

}
