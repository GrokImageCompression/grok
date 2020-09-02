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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include <assert.h>
#include "CPUArch.h"
#include "grk_includes.h"
#include "dwt.h"
#include <algorithm>

namespace grk {

template <typename T, typename S> struct decode_job{
	decode_job( S data,
				T * GRK_RESTRICT LL,
				uint32_t sLL,
				T * GRK_RESTRICT HL,
				uint32_t sHL,
				T * GRK_RESTRICT LH,
				uint32_t sLH,
				T * GRK_RESTRICT HH,
				uint32_t sHH,
				T * GRK_RESTRICT destination,
				uint32_t strideDestination,
				uint32_t min_j,
				uint32_t max_j) : data(data),
								bandLL(LL),
								strideLL(sLL),
								bandHL(HL),
								strideHL(sHL),
								bandLH(LH),
								strideLH(sLH),
								bandHH(HH),
								strideHH(sHH),
								dest(destination),
								strideDest(strideDestination),
								min_j(min_j),
								max_j(max_j)
	{}
	decode_job( S data,
				uint32_t min_j,
				uint32_t max_j) :
					decode_job(data,nullptr,0,nullptr,0,nullptr,0,nullptr,0,nullptr,0,min_j, max_j)
	{}
    S data;
    T * GRK_RESTRICT bandLL;
    uint32_t strideLL;
    T * GRK_RESTRICT bandHL;
    uint32_t strideHL;
    T * GRK_RESTRICT bandLH;
    uint32_t strideLH;
    T * GRK_RESTRICT bandHH;
    uint32_t strideHH;
    T * GRK_RESTRICT dest;
    uint32_t strideDest;

    uint32_t min_j;
    uint32_t max_j;
} ;


/** Number of columns that we can process in parallel in the vertical pass */
#define PLL_COLS_53     (2*VREG_INT_COUNT)
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

	dwt_data(const dwt_data& rhs) : mem(nullptr),
									dn ( rhs.dn),
									sn ( rhs.sn),
									cas ( rhs.cas),
									win_l_x0 ( rhs.win_l_x0),
									win_l_x1 ( rhs.win_l_x1),
									win_h_x0 ( rhs.win_h_x0),
									win_h_x1 ( rhs.win_h_x1)
	{}

	bool alloc(size_t len) {
		release();

	    /* overflow check */
		// add 10 just to be sure to we are safe from
		// segment growth overflow
	    if (len > (SIZE_MAX - 10U)) {
	        GRK_ERROR("data size overflow");
	        return false;
	    }
	    len += 10U;
	    /* overflow check */
	    if (len > (SIZE_MAX / sizeof(T))) {
	        GRK_ERROR("data size overflow");
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
    uint32_t dn;   /* number of elements in high pass band */
    uint32_t sn;   /* number of elements in low pass band */
    int32_t cas;  /* 0 = start on even coord, 1 = start on odd coord */
    uint32_t      win_l_x0; /* start coord in low pass band */
    uint32_t      win_l_x1; /* end coord in low pass band */
    uint32_t      win_h_x0; /* start coord in high pass band */
    uint32_t      win_h_x1; /* end coord in high pass band */
};

struct  vec4f {
	vec4f() : f{0}
	{}
	explicit vec4f(float m) : f{m}
	{}
    float f[4];
};

static const float dwt_alpha =  1.586134342f; /*  12994 */
static const float dwt_beta  =  0.052980118f; /*    434 */
static const float dwt_gamma = -0.882911075f; /*  -7233 */
static const float dwt_delta = -0.443506852f; /*  -3633 */

static const float K      = 1.230174105f; /*  10078 */
static const float c13318 = 1.625732422f;

static void  decode_h_cas0_53(int32_t* buf,
                               int32_t* bandL, /* even */
								const uint32_t wL,
							   int32_t* bandH,
								const uint32_t wH,
								int32_t *dest){ /* odd */
	const uint32_t total_width = wL + wH;
    assert(total_width > 1);

    /* Improved version of the TWO_PASS_VERSION: */
    /* Performs lifting in one single iteration. Saves memory */
    /* accesses and explicit interleaving. */
    int32_t s1n = bandL[0];
    int32_t d1n = bandH[0];
    int32_t s0n = s1n - ((d1n + 1) >> 1);

    uint32_t i = 0;

    if (total_width > 2) {
		for (uint32_t j = 1; i < (total_width - 3); i += 2, j++) {
			int32_t d1c = d1n;
			int32_t s0c = s0n;

			s1n = bandL[j];
			d1n = bandH[j];
			s0n = s1n - ((d1c + d1n + 2) >> 2);
			buf[i  ] = s0c;
			buf[i + 1] = d1c + ((s0c + s0n) >> 1);
		}
    }

    buf[i] = s0n;
    if (total_width & 1) {
        buf[total_width - 1] = bandL[(total_width - 1) >> 1] - ((d1n + 1) >> 1);
        buf[total_width - 2] = d1n + ((s0n + buf[total_width - 1]) >> 1);
    } else {
        buf[total_width - 1] = d1n + s0n;
    }
    memcpy(dest, buf, total_width * sizeof(int32_t));
}

static void  decode_h_cas1_53(int32_t* buf,
							   int32_t* bandL, /* odd */
                               const uint32_t wL,
							   int32_t* bandH,
                               const uint32_t wH,
							   int32_t *dest){ /* even */
	const uint32_t total_width = wL + wH;
    assert(total_width > 2);

    /* Improved version of the TWO_PASS_VERSION:
       Performs lifting in one single iteration. Saves memory
       accesses and explicit interleaving. */
    int32_t s1 = bandH[1];
    int32_t dc = bandL[0] - ((bandH[0] + s1 + 2) >> 2);
    buf[0] = bandH[0] + dc;
    uint32_t i, j;
    for (i = 1, j = 1; i < (total_width - 2 - !(total_width & 1)); i += 2, j++) {
    	int32_t s2 = bandH[j + 1];
    	int32_t dn = bandL[j] - ((s1 + s2 + 2) >> 2);

        buf[i  ] = dc;
        buf[i + 1] = s1 + ((dn + dc) >> 1);
        dc = dn;
        s1 = s2;
    }

    buf[i] = dc;

    if (!(total_width & 1)) {
    	int32_t dn = bandL[total_width / 2 - 1] - ((s1 + 1) >> 1);
        buf[total_width - 2] = s1 + ((dn + dc) >> 1);
        buf[total_width - 1] = dn;
    } else {
        buf[total_width - 1] = s1 + dc;
    }
    memcpy(dest, buf, total_width * sizeof(int32_t));
}


#if (defined(__SSE2__) || defined(__AVX2__))

static
void decode_v_final_memcpy_53( const int32_t* buf,
							const uint32_t height,
							int32_t* dest,
							const size_t strideDest){
	for (uint32_t i = 0; i < height; ++i) {
        /* A memcpy(&tiledp_col[i * stride + 0],
                    &tmp[PARALLEL_COLS_53 * i + 0],
                    PARALLEL_COLS_53 * sizeof(int32_t))
           would do but would be a tiny bit slower.
           We can take here advantage of our knowledge of alignment */
        STOREU(&dest[(size_t)i * strideDest + 0],              LOAD(&buf[PLL_COLS_53 * i + 0]));
        STOREU(&dest[(size_t)i * strideDest + VREG_INT_COUNT], LOAD(&buf[PLL_COLS_53 * i + VREG_INT_COUNT]));
    }
}

/** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
 * 16 in AVX2, when top-most pixel is on even coordinate */
static void decode_v_cas0_mcols_SSE2_OR_AVX2_53(int32_t* buf,
												int32_t* bandL, /* even */
												const uint32_t hL,
												const size_t strideL,
												int32_t *bandH, /* odd */
												const uint32_t hH,
												const size_t strideH,
												int32_t *dest,
												const uint32_t strideDest){
    const VREG two = LOAD_CST(2);

	const uint32_t total_height = hL + hH;
    assert(total_height > 1);

    /* Note: loads of input even/odd values must be done in a unaligned */
    /* fashion. But stores in tmp can be done with aligned store, since */
    /* the temporary buffer is properly aligned */
    assert((size_t)buf % (sizeof(int32_t) * VREG_INT_COUNT) == 0);

    VREG s1n_0 = LOADU(bandL + 0);
    VREG s1n_1 = LOADU(bandL + VREG_INT_COUNT);
    VREG d1n_0 = LOADU(bandH);
    VREG d1n_1 = LOADU(bandH + VREG_INT_COUNT);

    /* s0n = s1n - ((d1n + 1) >> 1); <==> */
    /* s0n = s1n - ((d1n + d1n + 2) >> 2); */
    VREG s0n_0 = SUB(s1n_0, SAR(ADD3(d1n_0, d1n_0, two), 2));
    VREG s0n_1 = SUB(s1n_1, SAR(ADD3(d1n_1, d1n_1, two), 2));

    uint32_t i = 0;
    if (total_height > 3) {
        uint32_t j;
		for (i = 0, j = 1; i < (total_height - 3); i += 2, j++) {
			VREG d1c_0 = d1n_0;
			VREG s0c_0 = s0n_0;
			VREG d1c_1 = d1n_1;
			VREG s0c_1 = s0n_1;

			s1n_0 = LOADU(bandL + j * strideL);
			s1n_1 = LOADU(bandL + j * strideL + VREG_INT_COUNT);
			d1n_0 = LOADU(bandH + j * strideH);
			d1n_1 = LOADU(bandH + j * strideH + VREG_INT_COUNT);

			/*s0n = s1n - ((d1c + d1n + 2) >> 2);*/
			s0n_0 = SUB(s1n_0, SAR(ADD3(d1c_0, d1n_0, two), 2));
			s0n_1 = SUB(s1n_1, SAR(ADD3(d1c_1, d1n_1, two), 2));

			STORE(buf + PLL_COLS_53 * (i + 0), s0c_0);
			STORE(buf + PLL_COLS_53 * (i + 0) + VREG_INT_COUNT, s0c_1);

			/* d1c + ((s0c + s0n) >> 1) */
			STORE(buf + PLL_COLS_53 * (i + 1) + 0,              ADD(d1c_0, SAR(ADD(s0c_0, s0n_0), 1)));
			STORE(buf + PLL_COLS_53 * (i + 1) + VREG_INT_COUNT, ADD(d1c_1, SAR(ADD(s0c_1, s0n_1), 1)));
		}
    }

    STORE(buf + PLL_COLS_53 * (i + 0) + 0, s0n_0);
    STORE(buf + PLL_COLS_53 * (i + 0) + VREG_INT_COUNT, s0n_1);

    if (total_height & 1) {
        VREG tmp_len_minus_1;
        s1n_0 = LOADU(bandL + (size_t)((total_height - 1) / 2) * strideL);
        /* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
        tmp_len_minus_1 = SUB(s1n_0, SAR(ADD3(d1n_0, d1n_0, two), 2));
        STORE(buf + PLL_COLS_53 * (total_height - 1), tmp_len_minus_1);
        /* d1n + ((s0n + tmp_len_minus_1) >> 1) */
        STORE(buf + PLL_COLS_53 * (total_height - 2), ADD(d1n_0, SAR(ADD(s0n_0, tmp_len_minus_1), 1)));

        s1n_1 = LOADU(bandL + (size_t)((total_height - 1) / 2) * strideL + VREG_INT_COUNT);
        /* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
        tmp_len_minus_1 = SUB(s1n_1, SAR(ADD3(d1n_1, d1n_1, two), 2));
        STORE(buf + PLL_COLS_53 * (total_height - 1) + VREG_INT_COUNT, tmp_len_minus_1);
        /* d1n + ((s0n + tmp_len_minus_1) >> 1) */
        STORE(buf + PLL_COLS_53 * (total_height - 2) + VREG_INT_COUNT, ADD(d1n_1, SAR(ADD(s0n_1, tmp_len_minus_1), 1)));

    } else {
        STORE(buf + PLL_COLS_53 * (total_height - 1) + 0,              ADD(d1n_0, s0n_0));
        STORE(buf + PLL_COLS_53 * (total_height - 1) + VREG_INT_COUNT, ADD(d1n_1, s0n_1));
    }
    decode_v_final_memcpy_53(buf,total_height, dest, strideDest);
}


/** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
 * 16 in AVX2, when top-most pixel is on odd coordinate */
static void decode_v_cas1_mcols_SSE2_OR_AVX2_53(int32_t* buf,
												int32_t* bandL,
												const uint32_t hL,
												const uint32_t strideL,
												int32_t *bandH,
												const uint32_t hH,
												const uint32_t strideH,
												int32_t *dest,
												const uint32_t strideDest){
    const VREG two = LOAD_CST(2);

    const uint32_t total_height = hL + hH;
    assert(total_height > 2);
    /* Note: loads of input even/odd values must be done in a unaligned */
    /* fashion. But stores in buf can be done with aligned store, since */
    /* the temporary buffer is properly aligned */
    assert((size_t)buf % (sizeof(int32_t) * VREG_INT_COUNT) == 0);

    const int32_t* in_even = bandH;
    const int32_t* in_odd = bandL;
    VREG s1_0 = LOADU(in_even + strideH);
    /* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
    VREG dc_0 = SUB(LOADU(in_odd + 0), SAR(ADD3(LOADU(in_even + 0), s1_0, two), 2));
    STORE(buf + PLL_COLS_53 * 0, ADD(LOADU(in_even + 0), dc_0));

    VREG s1_1 = LOADU(in_even + strideH + VREG_INT_COUNT);
    /* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
    VREG dc_1 = SUB(LOADU(in_odd + VREG_INT_COUNT), SAR(ADD3(LOADU(in_even + VREG_INT_COUNT), s1_1, two), 2));
    STORE(buf + PLL_COLS_53 * 0 + VREG_INT_COUNT,   ADD(LOADU(in_even + VREG_INT_COUNT), dc_1));

    uint32_t i;
    size_t j;
    for (i = 1, j = 1; i < (total_height - 2 - !(total_height & 1)); i += 2, j++) {

    	VREG s2_0 = LOADU(in_even + (j + 1) * strideH);
    	VREG s2_1 = LOADU(in_even + (j + 1) * strideH + VREG_INT_COUNT);

        /* dn = in_odd[j * stride] - ((s1 + s2 + 2) >> 2); */
    	VREG dn_0 = SUB(LOADU(in_odd + j * strideL),                 SAR(ADD3(s1_0, s2_0, two), 2));
    	VREG dn_1 = SUB(LOADU(in_odd + j * strideL + VREG_INT_COUNT),SAR(ADD3(s1_1, s2_1, two), 2));

        STORE(buf + PLL_COLS_53 * i, dc_0);
        STORE(buf + PLL_COLS_53 * i + VREG_INT_COUNT, dc_1);

        /* buf[i + 1] = s1 + ((dn + dc) >> 1); */
        STORE(buf + PLL_COLS_53 * (i + 1) + 0,             ADD(s1_0, SAR(ADD(dn_0, dc_0), 1)));
        STORE(buf + PLL_COLS_53 * (i + 1) + VREG_INT_COUNT,ADD(s1_1, SAR(ADD(dn_1, dc_1), 1)));

        dc_0 = dn_0;
        s1_0 = s2_0;
        dc_1 = dn_1;
        s1_1 = s2_1;
    }
    STORE(buf + PLL_COLS_53 * i, dc_0);
    STORE(buf + PLL_COLS_53 * i + VREG_INT_COUNT, dc_1);

    if (!(total_height & 1)) {
        /*dn = in_odd[(len / 2 - 1) * stride] - ((s1 + 1) >> 1); */
    	VREG dn_0 = SUB(LOADU(in_odd + (size_t)(total_height / 2 - 1) * strideL),SAR(ADD3(s1_0, s1_0, two), 2));
    	VREG dn_1 = SUB(LOADU(in_odd + (size_t)(total_height / 2 - 1) * strideL + VREG_INT_COUNT), SAR(ADD3(s1_1, s1_1, two), 2));

        /* buf[len - 2] = s1 + ((dn + dc) >> 1); */
        STORE(buf + PLL_COLS_53 * (total_height - 2) + 0, ADD(s1_0, SAR(ADD(dn_0, dc_0), 1)));
        STORE(buf + PLL_COLS_53 * (total_height - 2) + VREG_INT_COUNT, ADD(s1_1, SAR(ADD(dn_1, dc_1), 1)));

        STORE(buf + PLL_COLS_53 * (total_height - 1) + 0, dn_0);
        STORE(buf + PLL_COLS_53 * (total_height - 1) + VREG_INT_COUNT, dn_1);
    } else {
        STORE(buf + PLL_COLS_53 * (total_height - 1) + 0, ADD(s1_0, dc_0));
        STORE(buf + PLL_COLS_53 * (total_height - 1) + VREG_INT_COUNT,ADD(s1_1, dc_1));
    }
    decode_v_final_memcpy_53(buf, total_height, dest, strideDest);
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
static void decode_v_cas0_53(int32_t* buf,
                             int32_t* bandL,
							 const uint32_t hL,
							 const uint32_t strideL,
							 int32_t *bandH,
							 const uint32_t hH,
                             const uint32_t strideH,
							 int32_t *dest,
							 const uint32_t strideDest){

    const uint32_t total_height = hL + hH;
    assert(total_height > 1);

    /* Performs lifting in one single iteration. Saves memory */
    /* accesses and explicit interleaving. */
    int32_t s1n = bandL[0];
    int32_t d1n = bandH[0];
    int32_t s0n = s1n - ((d1n + 1) >> 1);

    uint32_t i = 0;
    if (total_height > 2) {
    	auto bL = bandL + strideL;
    	auto bH = bandH + strideH;
		for (uint32_t j = 0; i < (total_height - 3); i += 2, j++) {
			int32_t d1c = d1n;
			int32_t s0c = s0n;
			s1n = *bL;
			bL += strideL;
			d1n = *bH;
			bH += strideH;
			s0n = s1n - ((d1c + d1n + 2) >> 2);
			buf[i  ] = s0c;
			buf[i + 1] = d1c + ((s0c + s0n) >> 1);
		}
    }
    buf[i] = s0n;
    if (total_height & 1) {
        buf[total_height - 1] =
            bandL[((total_height - 1) / 2) * strideL] -
            ((d1n + 1) >> 1);
        buf[total_height - 2] = d1n + ((s0n + buf[total_height - 1]) >> 1);
    } else {
        buf[total_height - 1] = d1n + s0n;
    }
    for (i = 0; i < total_height; ++i) {
        *dest = buf[i];
        dest += strideDest;
    }
}

/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on odd coordinate */
static void decode_v_cas1_53(int32_t* buf,
                             int32_t *bandL,
							 const uint32_t hL,
							 const uint32_t strideL,
							 int32_t *bandH,
							 const uint32_t hH,
                             const uint32_t strideH,
							int32_t *dest,
							const uint32_t strideDest){

    const uint32_t total_height = hL + hH;
    assert(total_height > 2);

    /* Performs lifting in one single iteration. Saves memory */
    /* accesses and explicit interleaving. */
    int32_t s1 = bandH[strideH];
    int32_t dc = bandL[0] - ((bandH[0] + s1 + 2) >> 2);
    buf[0] = bandH[0] + dc;
    auto s2_ptr = bandH + (strideH << 1);
    auto dn_ptr = bandL + strideL;
    uint32_t i, j;
    for (i = 1, j = 1; i < (total_height - 2 - !(total_height & 1)); i += 2, j++) {
    	int32_t s2 = *s2_ptr;
    	s2_ptr += strideH;

    	int32_t dn = *dn_ptr - ((s1 + s2 + 2) >> 2);
    	dn_ptr += strideL;

        buf[i  ] = dc;
        buf[i + 1] = s1 + ((dn + dc) >> 1);
        dc = dn;
        s1 = s2;
    }
    buf[i] = dc;
    if (!(total_height & 1)) {
    	int32_t dn = bandL[((total_height>>1) - 1) * strideL] - ((s1 + 1) >> 1);
        buf[total_height - 2] = s1 + ((dn + dc) >> 1);
        buf[total_height - 1] = dn;
    } else {
        buf[total_height - 1] = s1 + dc;
    }
    for (i = 0; i < total_height; ++i) {
        *dest = buf[i];
        dest += strideDest;
    }
}

/* <summary>                            */
/* Inverse 5-3 wavelet transform in 1-D for one row. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
static void decode_h_53(const dwt_data<int32_t> *dwt,
                         int32_t *bandL,
						 int32_t *bandH,
						 int32_t *dest)
{
    const uint32_t total_width = dwt->sn + dwt->dn;
    if (dwt->cas == 0) { /* Left-most sample is on even coordinate */
        if (total_width > 1) {
            decode_h_cas0_53(dwt->mem,bandL,dwt->sn, bandH, dwt->dn, dest);
        } else if (total_width == 1) {
        	//FIXME - validate this calculation
        	dest[0] = bandL[0];
        }
    } else { /* Left-most sample is on odd coordinate */
        if (total_width == 1) {
        	//FIXME - validate this calculation
        	dest[0] = bandH[0]/2;
        } else if (total_width == 2) {
            dwt->mem[1] = bandL[0] - ((bandH[0] + 1) >> 1);
            dest[0] = bandH[0] + dwt->mem[1];
            dest[1] = dwt->mem[1];
        } else if (total_width > 2) {
            decode_h_cas1_53(dwt->mem, bandL, dwt->sn, bandH,dwt->dn, dest);
        }
    }
}

/* <summary>                            */
/* Inverse vertical 5-3 wavelet transform in 1-D for several columns. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
static void decode_v_53(const dwt_data<int32_t> *dwt,
                         int32_t *bandL,
						 const uint32_t strideL,
						 int32_t *bandH,
						 const uint32_t strideH,
						 int32_t *dest,
						 const uint32_t strideDest,
                         uint32_t nb_cols){
    const uint32_t sn = dwt->sn;
    const uint32_t len = sn + dwt->dn;
    if (dwt->cas == 0) {
        if (len == 1) {
            for (uint32_t c = 0; c < nb_cols; c++, bandL++,dest++)
                dest[0] = bandL[0];
            return;
        }
    	if (CPUArch::SSE2() || CPUArch::AVX2() ) {
#if (defined(__SSE2__) || defined(__AVX2__))
			if (len > 1 && nb_cols == PLL_COLS_53) {
				/* Same as below general case, except that thanks to SSE2/AVX2 */
				/* we can efficiently process 8/16 columns in parallel */
				decode_v_cas0_mcols_SSE2_OR_AVX2_53(dwt->mem, bandL,sn, strideL, bandH, dwt->dn, strideH, dest, strideDest);
				return;
			}
#endif
    	}
        if (len > 1) {
            for (uint32_t c = 0; c < nb_cols; c++, bandL++, bandH++,dest++)
                decode_v_cas0_53(dwt->mem, bandL,sn, strideL,bandH,dwt->dn, strideH, dest, strideDest);
            return;
        }
    } else {
        if (len == 1) {
            for (uint32_t c = 0; c < nb_cols; c++, bandL++,dest++)
                dest[0] = bandL[0] >> 1;
            return;
        }
        else if (len == 2) {
            auto out = dwt->mem;
            for (uint32_t c = 0; c < nb_cols; c++, bandL++,bandH++,dest++) {
                out[1] = bandL[0] - ((bandH[0] + 1) >> 1);
                dest[0] = bandH[0] + out[1];
                dest[1] = out[1];
            }
            return;
        }
        if (CPUArch::SSE2() || CPUArch::AVX2() ) {
#if (defined(__SSE2__) || defined(__AVX2__))
			if (nb_cols == PLL_COLS_53) {
				/* Same as below general case, except that thanks to SSE2/AVX2 */
				/* we can efficiently process 8/16 columns in parallel */
				decode_v_cas1_mcols_SSE2_OR_AVX2_53(dwt->mem, bandL,sn, strideL,bandH,dwt->dn, strideH, dest, strideDest);
				return;
			}
#endif
        }
		for (uint32_t c = 0; c < nb_cols; c++, bandL++,bandH++,dest++)
			decode_v_cas1_53(dwt->mem, bandL,sn,strideL,bandH, dwt->dn, strideH, dest, strideDest);
    }
}

static void decode_h_strip_53(const dwt_data<int32_t> *horiz,
						 uint32_t hMin,
						 uint32_t hMax,
                         int32_t *bandL,
						 const uint32_t strideL,
						 int32_t *bandH,
						 const uint32_t strideH,
						 int32_t *dest,
						 const uint32_t strideDest) {
    for (uint32_t j = hMin; j < hMax; ++j){
        decode_h_53(horiz, bandL, bandH, dest);
        bandL += strideL;
        bandH += strideH;
        dest += strideDest;
    }
}

static bool decode_h_mt_53(uint32_t num_threads,
						size_t data_size,
						 dwt_data<int32_t> &horiz,
		 	 	 	 	 dwt_data<int32_t> &vert,
						 uint32_t rh,
                         int32_t *bandL,
						 const uint32_t strideL,
						 int32_t *bandH,
						 const uint32_t strideH,
						 int32_t *dest,
						 const uint32_t strideDest) {
    if (num_threads == 1 || rh <= 1) {
    	if (!horiz.mem){
    	    if (! horiz.alloc(data_size)) {
    	        GRK_ERROR("Out of memory");
    	        return false;
    	    }
    	    vert.mem = horiz.mem;
    	}
    	decode_h_strip_53(&horiz,0,rh,bandL,strideL,bandH,strideH, dest, strideDest);
    } else {
        uint32_t num_jobs = (uint32_t)num_threads;
        if (rh < num_jobs)
            num_jobs = rh;
        uint32_t step_j = (rh / num_jobs);
		std::vector< std::future<int> > results;
		for(uint32_t j = 0; j < num_jobs; ++j) {
		   auto min_j = j * step_j;
           auto job = new decode_job<int32_t, dwt_data<int32_t>>(horiz,
										bandL + min_j * strideL,
										strideL,
										bandH + min_j * strideH,
										strideH,
										nullptr,0,
										nullptr,0,
										dest + min_j * strideDest,
										strideDest,
										j * step_j,
										j < (num_jobs - 1U) ? (j + 1U) * step_j : rh);
            if (!job->data.alloc(data_size)) {
                GRK_ERROR("Out of memory");
                horiz.release();
                return false;
            }
			results.emplace_back(
				ThreadPool::get()->enqueue([job] {
					decode_h_strip_53(&job->data,
							job->min_j,
							job->max_j,
							job->bandLL,
							job->strideLL,
							job->bandHL,
							job->strideHL,
							job->dest,
							job->strideDest);
				    job->data.release();
				    delete job;
					return 0;
				})
			);
		}
		for(auto &result: results)
			result.get();
    }
    return true;
}

static void decode_v_strip_53(const dwt_data<int32_t> *vert,
						 uint32_t wMin,
						 uint32_t wMax,
                         int32_t *bandL,
						 const uint32_t strideL,
						 int32_t *bandH,
						 const uint32_t strideH,
						 int32_t *dest,
						 const uint32_t strideDest) {


    uint32_t j;
    for (j = wMin; j + PLL_COLS_53 <= wMax; j += PLL_COLS_53){
        decode_v_53(vert, bandL, strideL, bandH, strideH,dest, strideDest, PLL_COLS_53);
		bandL += PLL_COLS_53;
		bandH += PLL_COLS_53;
		dest  += PLL_COLS_53;
    }
    if (j < wMax)
        decode_v_53(vert, bandL, strideL, bandH, strideH, dest, strideDest, wMax - j);
}

static bool decode_v_mt_53(uint32_t num_threads,
						size_t data_size,
						 dwt_data<int32_t> &horiz,
		 	 	 	 	 dwt_data<int32_t> &vert,
						 uint32_t rw,
                         int32_t *bandL,
						 const uint32_t strideL,
						 int32_t *bandH,
						 const uint32_t strideH,
						 int32_t *dest,
						 const uint32_t strideDest) {
    if (num_threads == 1 || rw <= 1) {
    	if (!horiz.mem){
    	    if (! horiz.alloc(data_size)) {
    	        GRK_ERROR("Out of memory");
    	        return false;
    	    }
    	    vert.mem = horiz.mem;
    	}
    	decode_v_strip_53(&vert, 0, rw, bandL, strideL, bandH, strideH, dest, strideDest);
    } else {
        uint32_t num_jobs = (uint32_t)num_threads;
        if (rw < num_jobs)
            num_jobs = rw;
        uint32_t step_j = (rw / num_jobs);
		std::vector< std::future<int> > results;
        for (uint32_t j = 0; j < num_jobs; j++) {
			    auto min_j = j * step_j;
            auto job = new decode_job<int32_t, dwt_data<int32_t>>(vert,
										bandL + min_j,
										strideL,
										nullptr,
										0,
										bandH + min_j,
										strideH,
										nullptr,
										0,
										dest + min_j,
										strideDest,
										j * step_j,
										j < (num_jobs - 1U) ? (j + 1U) * step_j : rw);
            if (!job->data.alloc(data_size)) {
                GRK_ERROR("Out of memory");
                vert.release();
                return false;
            }
			results.emplace_back(
				ThreadPool::get()->enqueue([job] {
					decode_v_strip_53(&job->data,
							job->min_j,
							job->max_j,
							job->bandLL,
							job->strideLL,
							job->bandLH,
							job->strideLH,
							job->dest,
							job->strideDest);
					job->data.release();
					delete job;
				return 0;
				})
			);
        }
		for(auto &result: results)
			result.get();
    }
    return true;
}


/* <summary>                            */
/* Inverse wavelet transform in 2-D.    */
/* </summary>                           */
static bool decode_tile_53( TileComponent* tilec, uint32_t numres){
    if (numres == 1U)
        return true;

    auto tr = tilec->resolutions;
    uint32_t rw = tr->width();
    uint32_t rh = tr->height();

    uint32_t num_threads = (uint32_t)ThreadPool::get()->num_threads();
    size_t data_size = dwt_utils::max_resolution(tr, numres);
    /* overflow check */
    if (data_size > (SIZE_MAX / PLL_COLS_53 / sizeof(int32_t))) {
        GRK_ERROR("Overflow");
        return false;
    }
    /* We need PLL_COLS_53 times the height of the array, */
    /* since for the vertical pass */
    /* we process PLL_COLS_53 columns at a time */
    dwt_data<int32_t> horiz;
    dwt_data<int32_t> vert;
    data_size *= PLL_COLS_53 * sizeof(int32_t);
    bool rc = true;
    for (uint32_t res = 1; res < numres; ++res){
        horiz.sn = rw;
        vert.sn = rh;
        ++tr;
        rw = tr->width();
        rh = tr->height();
        if (rw == 0 || rh == 0)
        	continue;
        horiz.dn = rw - horiz.sn;
        horiz.cas = tr->x0 & 1;
    	if (!decode_h_mt_53(num_threads,
    						data_size,
							horiz,
							vert,
							vert.sn,
							tilec->buf->ptr(res-1),
							tilec->buf->stride(res-1),
							tilec->buf->ptr(res, 0),
							tilec->buf->stride(res,0),
							tilec->buf->ptr(res),
							tilec->buf->stride(res)))
    		return false;
    	if (!decode_h_mt_53(num_threads,
    						data_size,
							horiz,
							vert,
							rh -  vert.sn,
							tilec->buf->ptr(res, 1),
							tilec->buf->stride(res,1),
							tilec->buf->ptr(res, 2),
							tilec->buf->stride(res,2),
    						tilec->buf->ptr(res) + vert.sn *tilec->buf->stride(res) ,
    						tilec->buf->stride(res) ))
    		return false;
        vert.dn = rh - vert.sn;
        vert.cas = tr->y0 & 1;
    	if (!decode_v_mt_53(num_threads,
    						data_size,
							horiz,
							vert,
							rw,
							tilec->buf->ptr(res),
							tilec->buf->stride(res),
							tilec->buf->ptr(res)+ vert.sn *tilec->buf->stride(res) ,
							tilec->buf->stride(res),
							tilec->buf->ptr(res),
							tilec->buf->stride(res)))
    		return false;
    }
    horiz.release();
    return rc;
}

#ifdef __SSE__
static void decode_step1_sse_97(vec4f* w,
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
    for (; i < end; ++i, vw += 2)
        vw[0] = _mm_mul_ps(vw[0], c);
}

static void decode_step2_sse_97(vec4f* l, vec4f* w,
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
static void decode_step1_97(vec4f* w,
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
static void decode_step2_97(vec4f* l, vec4f* w,
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
static void decode_step_97(dwt_data<vec4f>* GRK_RESTRICT dwt)
{
    int32_t a, b;

    if (dwt->cas == 0) {
        if (!((dwt->dn > 0) || (dwt->sn > 1)))
            return;
        a = 0;
        b = 1;
    } else {
        if (!((dwt->sn > 0) || (dwt->dn > 1)))
            return;
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


static void interleave_h_97(dwt_data<vec4f>* GRK_RESTRICT dwt,
                                   float* GRK_RESTRICT bandL,
								   const uint32_t strideL,
								   float* GRK_RESTRICT bandH,
                                   const uint32_t strideH,
                                   uint32_t remaining_height){
    float* GRK_RESTRICT bi = (float*)(dwt->mem + dwt->cas);
    uint32_t x0 = dwt->win_l_x0;
    uint32_t x1 = dwt->win_l_x1;

    for (uint32_t k = 0; k < 2; ++k) {
    	auto band = (k == 0) ? bandL : bandH;
    	uint32_t stride = (k == 0) ? strideL : strideH;
        if (remaining_height >= 4 && ((size_t) band & 0x0f) == 0 &&
                ((size_t) bi & 0x0f) == 0 && (stride & 0x0f) == 0) {
            /* Fast code path */
            for (uint32_t i = x0; i < x1; ++i, bi+=8) {
                uint32_t j = i;
                bi[0] = band[j];
                j += stride;
                bi[1] = band[j];
                j += stride;
                bi[2] = band[j];
                j += stride;
                bi[3] = band[j];
             }
        } else {
            /* Slow code path */
            for (uint32_t i = x0; i < x1; ++i, bi+=8) {
                uint32_t j = i;
                bi[0] = band[j];
                j += stride;
                if (remaining_height == 1)
                    continue;
                bi[1] = band[j];
                j += stride;
                if (remaining_height == 2)
                    continue;
                 bi[2] = band[j];
                j += stride;
                if (remaining_height == 3)
                    continue;
                bi[3] = band[j];
            }
        }

        bi = (float*)(dwt->mem + 1 - dwt->cas);
        x0 = dwt->win_h_x0;
        x1 = dwt->win_h_x1;
    }
}

static void decode_h_strip_97(dwt_data<vec4f>* GRK_RESTRICT horiz,
								   const uint32_t rh,
                                   float* GRK_RESTRICT bandL,
								   const uint32_t strideL,
								   float* GRK_RESTRICT bandH,
                                   const uint32_t strideH,
								   float* dest,
								   const size_t strideDest){
	uint32_t j;
	for (j = 0; j< (rh & ~3); j += 4) {
		interleave_h_97(horiz, bandL,strideL, bandH, strideH, rh - j);
		decode_step_97(horiz);
		for (uint32_t k = 0; k <  horiz->sn + horiz->dn; k++) {
			dest[k      ] 					= horiz->mem[k].f[0];
			dest[k + (size_t)strideDest  ] 	= horiz->mem[k].f[1];
			dest[k + (size_t)strideDest * 2] 	= horiz->mem[k].f[2];
			dest[k + (size_t)strideDest * 3] 	= horiz->mem[k].f[3];
		}
		bandL += strideL << 2;
		bandH += strideH << 2;
		dest  += strideDest << 2;
	}
	if (j < rh) {
		interleave_h_97(horiz, bandL,strideL,bandH, strideH, rh - j);
		decode_step_97(horiz);
		for (uint32_t k = 0; k < horiz->sn + horiz->dn; k++) {
			switch (rh - j) {
			case 3:
				dest[k + strideDest * 2] = horiz->mem[k].f[2];
			/* FALLTHRU */
			case 2:
				dest[k + strideDest  ] = horiz->mem[k].f[1];
			/* FALLTHRU */
			case 1:
				dest[k] = horiz->mem[k].f[0];
			}
		}
	}
}
static bool decode_h_mt_97(uint32_t num_threads,
							size_t data_size,
							dwt_data<vec4f> &GRK_RESTRICT horiz,
						   const uint32_t rh,
						   float* GRK_RESTRICT bandL,
						   const uint32_t strideL,
						   float* GRK_RESTRICT bandH,
						   const uint32_t strideH,
						   float* GRK_RESTRICT dest,
						   const uint32_t strideDest){
	uint32_t num_jobs = num_threads;
    if (rh < num_jobs)
        num_jobs = rh;
    uint32_t step_j = num_jobs ? (rh / num_jobs) : 0;
    if (num_threads == 1 || step_j < 4) {
    	decode_h_strip_97(&horiz, rh, bandL,strideL, bandH, strideH, dest, strideDest);
    } else {
		std::vector< std::future<int> > results;
		for(uint32_t j = 0; j < num_jobs; ++j) {
		   auto min_j = j * step_j;
		   auto job = new decode_job<float, dwt_data<vec4f>>(horiz,
										bandL + min_j * strideL,
										strideL,
										bandH + min_j * strideH,
										strideH,
										nullptr,
										0,
										nullptr,
										0,
										dest + min_j * strideDest,
										strideDest,
										0,
										(j < (num_jobs - 1U) ? (j + 1U) * step_j : rh) - min_j);
			if (!job->data.alloc(data_size)) {
				GRK_ERROR("Out of memory");
				horiz.release();
				return false;
			}
			results.emplace_back(
				ThreadPool::get()->enqueue([job] {
	        		decode_h_strip_97(&job->data,
	        				job->max_j,
							job->bandLL,
							job->strideLL,
							job->bandHL,
							job->strideHL,
							job->dest,
							job->strideDest);
					job->data.release();
					delete job;
					return 0;
				})
			);
		}
		for(auto &result: results)
			result.get();
    }
    return true;
}

static void interleave_v_97(dwt_data<vec4f>* GRK_RESTRICT dwt,
                                   float* GRK_RESTRICT bandL,
								   const uint32_t strideL,
								   float* GRK_RESTRICT bandH,
                                   const uint32_t strideH,
                                   uint32_t nb_elts_read){
    vec4f* GRK_RESTRICT bi = dwt->mem + dwt->cas;
    auto band = bandL + dwt->win_l_x0 * strideL;
    for (uint32_t i = dwt->win_l_x0; i < dwt->win_l_x1; ++i, bi+=2) {
        memcpy((float*)bi, band, nb_elts_read * sizeof(float));
        band +=strideL;
    }

    bi = dwt->mem + 1 - dwt->cas;
    band = bandH + dwt->win_h_x0 * strideH;
    for (uint32_t i = dwt->win_h_x0; i < dwt->win_h_x1; ++i, bi+=2) {
        memcpy((float*)bi, band, nb_elts_read * sizeof(float));
        band += strideH;
    }
}
static void decode_v_strip_97(dwt_data<vec4f>* GRK_RESTRICT vert,
								   const uint32_t rw,
								   const uint32_t rh,
                                   float* GRK_RESTRICT bandL,
								   const uint32_t strideL,
								   float* GRK_RESTRICT bandH,
                                   const uint32_t strideH,
								   float* GRK_RESTRICT dest,
								   const uint32_t strideDest){
    uint32_t j;
	for (j = 0; j < (rw & ~3); j += 4) {
		interleave_v_97(vert, bandL,strideL, bandH,strideH, 4);
		decode_step_97(vert);
		auto destPtr = dest;
		for (uint32_t k = 0; k < rh; ++k){
			memcpy(destPtr, vert->mem+k, 4 * sizeof(float));
			destPtr += strideDest;
		}
		bandL += 4;
		bandH += 4;
		dest  += 4;
	}
	if (j < rw) {
		j = rw & 3;
		interleave_v_97(vert, bandL, strideL,bandH, strideH, j);
		decode_step_97(vert);
		auto destPtr = dest;
		for (uint32_t k = 0; k < rh; ++k) {
			memcpy(destPtr, vert->mem+k,j * sizeof(float));
			destPtr += strideDest;
		}
	}
}

static bool decode_v_mt_97(uint32_t num_threads,
							size_t data_size,
							dwt_data<vec4f> &GRK_RESTRICT vert,
							const uint32_t rw,
						   const uint32_t rh,
						   float* GRK_RESTRICT bandL,
						   const uint32_t strideL,
						   float* GRK_RESTRICT bandH,
						   const uint32_t strideH,
						   float* GRK_RESTRICT dest,
						   const uint32_t strideDest){
	auto num_jobs = (uint32_t)num_threads;
	if (rw < num_jobs)
		num_jobs = rw;
	auto step_j = num_jobs ? (rw / num_jobs) : 0;
	if (num_threads == 1 || step_j < 4) {
		decode_v_strip_97(&vert,
							rw,
							rh,
							bandL,
							strideL,
							bandH,
							strideH,
							dest,
							strideDest);
	} else {
		std::vector< std::future<int> > results;
		for (uint32_t j = 0; j < num_jobs; j++) {
			auto min_j = j * step_j;
			auto job = new decode_job<float, dwt_data<vec4f>>(vert,
														bandL + min_j,
														strideL,
														nullptr,
														0,
														bandH + min_j,
														strideH,
														nullptr,
														0,
														dest + min_j,
														strideDest,
														0,
														(j < (num_jobs - 1U) ? (j + 1U) * step_j : rw) - min_j);
			if (!job->data.alloc(data_size)) {
				GRK_ERROR("Out of memory");
				vert.release();
				return false;
			}
			results.emplace_back(
				ThreadPool::get()->enqueue([job,rh] {
					decode_v_strip_97(&job->data,
									job->max_j,
									rh,
									job->bandLL,
									job->strideLL,
									job->bandLH,
									job->strideLH,
									job->dest,
									job->strideDest);
					job->data.release();
					delete job;
					return 0;
				})
			);
		}
		for(auto &result: results)
			result.get();
	}

	return true;
}

/* <summary>                             */
/* Inverse 9-7 wavelet transform in 2-D. */
/* </summary>                            */
static
bool decode_tile_97(TileComponent* GRK_RESTRICT tilec,uint32_t numres){
    if (numres == 1U)
        return true;

    auto tr = tilec->resolutions;
    uint32_t rw = tr->width();
    uint32_t rh = tr->height();

    size_t data_size = dwt_utils::max_resolution(tr, numres);
    dwt_data<vec4f> horiz;
    dwt_data<vec4f> vert;
    if (!horiz.alloc(data_size)) {
        GRK_ERROR("Out of memory");
        return false;
    }
    vert.mem = horiz.mem;
    uint32_t num_threads = (uint32_t)ThreadPool::get()->num_threads();
    for (uint32_t res = 1; res < numres; ++res) {
        horiz.sn = rw;
        vert.sn = rh;
        ++tr;
        rw = tr->width();
        rh = tr->height();
        if (rw == 0 || rh == 0)
        	continue;
        horiz.dn = rw - horiz.sn;
        horiz.cas = tr->x0 & 1;
        horiz.win_l_x0 = 0;
        horiz.win_l_x1 = horiz.sn;
        horiz.win_h_x0 = 0;
        horiz.win_h_x1 = horiz.dn;
        if (!decode_h_mt_97(num_threads,
        					data_size,
							horiz,
							vert.sn,
							(float*) tilec->buf->ptr(res-1),
							tilec->buf->stride(res-1),
							(float*) tilec->buf->ptr(res, 0),
							tilec->buf->stride(res,0),
							(float*) tilec->buf->ptr(res),
							tilec->buf->stride(res)))
        	return false;
        if (!decode_h_mt_97(num_threads,
        					data_size,
							horiz,
							rh-vert.sn,
							(float*) tilec->buf->ptr(res, 1),
							tilec->buf->stride(res,1),
							(float*) tilec->buf->ptr(res, 2),
							tilec->buf->stride(res,2),
							(float*) tilec->buf->ptr(res) +  + vert.sn *tilec->buf->stride(res),
							tilec->buf->stride(res) ))
        	return false;
        vert.dn = rh - vert.sn;
        vert.cas = tr->y0 & 1;
        vert.win_l_x0 = 0;
        vert.win_l_x1 = vert.sn;
        vert.win_h_x0 = 0;
        vert.win_h_x1 = vert.dn;
        if (!decode_v_mt_97(num_threads,
        					data_size,
							vert,
							rw,
							rh,
							(float*) tilec->buf->ptr(res),
							tilec->buf->stride(res),
							(float*) tilec->buf->ptr(res) +  + vert.sn *tilec->buf->stride(res),
							tilec->buf->stride(res),
							(float*) tilec->buf->ptr(res),
							tilec->buf->stride(res)))
        	return false;
    }
    horiz.release();
    return true;
}

static void interleave_partial_h_53(dwt_data<int32_t> *dwt,
									sparse_array* sa,
									uint32_t sa_line)	{
	auto dest = dwt->mem;
	int32_t cas = dwt->cas;
	uint32_t win_l_x0 = dwt->win_l_x0;
	uint32_t win_l_x1 = dwt->win_l_x1;
    bool ret = sa->read( win_l_x0,
    					sa_line,
						win_l_x1,
						sa_line + 1,
						dest + cas + 2 * win_l_x0,
						2, 0, true);
    assert(ret);

	uint32_t sn = dwt->sn;
	uint32_t win_h_x0 = dwt->win_h_x0;
	uint32_t win_h_x1 = dwt->win_h_x1;
    ret = sa->read(sn + win_h_x0,
    				sa_line,
					sn + win_h_x1,
					sa_line + 1,
					dest + 1 - cas + 2 * win_h_x0,
					2, 0, true);
    assert(ret);
    GRK_UNUSED(ret);
}


static void interleave_partial_v_53(dwt_data<int32_t> *vert,
									sparse_array* sa,
									uint32_t sa_col,
									uint32_t nb_cols){
	auto dest = vert->mem;
	int32_t cas = vert->cas;
	uint32_t win_l_y0 = vert->win_l_x0;
	uint32_t win_l_y1 = vert->win_l_x1;
    bool ret = sa->read(sa_col, win_l_y0,
					   sa_col + nb_cols, win_l_y1,
					   dest + cas * 4 + 2 * 4 * win_l_y0,
					   1, 2 * 4, true);
    assert(ret);

	uint32_t sn = vert->sn;
	uint32_t win_h_y0 = vert->win_h_x0;
	uint32_t win_h_y1 = vert->win_h_x1;
	ret = sa->read( sa_col, sn + win_h_y0,
					  sa_col + nb_cols, sn + win_h_y1,
					  dest + (1 - cas) * 4 + 2 * 4 * win_h_y0,
					  1, 2 * 4, true);
    assert(ret);
    GRK_UNUSED(ret);
}

#define GRK_S(i) a[(i)<<1]
#define GRK_D(i) a[(1+((i)<<1))]
#define GRK_S_(i) ((i)<0?GRK_S(0):((i)>=sn?GRK_S(sn-1):GRK_S(i)))
#define GRK_D_(i) ((i)<0?GRK_D(0):((i)>=dn?GRK_D(dn-1):GRK_D(i)))
#define GRK_SS_(i) ((i)<0?GRK_S(0):((i)>=dn?GRK_S(dn-1):GRK_S(i)))
#define GRK_DD_(i) ((i)<0?GRK_D(0):((i)>=sn?GRK_D(sn-1):GRK_D(i)))

static void decode_partial_h_53(dwt_data<int32_t> *horiz){
    int32_t i;
    int32_t *a = horiz->mem;
	int32_t dn = horiz->dn;
	int32_t sn = horiz->sn;
	int32_t cas = horiz->cas;
	int32_t win_l_x0 = (int32_t)horiz->win_l_x0;
	int32_t win_l_x1 = (int32_t)horiz->win_l_x1;
	int32_t win_h_x0 = (int32_t)horiz->win_h_x0;
	int32_t win_h_x1 = (int32_t)horiz->win_h_x1;

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
                if (i_max > dn)
                    i_max = dn;
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
                if (i_max >= sn)
                    i_max = sn - 1;
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
            for (i = win_l_x0; i < win_l_x1; i++)
                GRK_D(i) -= (GRK_SS_(i) + GRK_SS_(i + 1) + 2) >> 2;
            for (i = win_h_x0; i < win_h_x1; i++)
                GRK_S(i) += (GRK_DD_(i) + GRK_DD_(i - 1)) >> 1;
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
	int32_t win_l_x0 = (int32_t)vert->win_l_x0;
	int32_t win_l_x1 = (int32_t)vert->win_l_x1;
	int32_t win_h_x0 = (int32_t)vert->win_h_x0;
	int32_t win_h_x1 = (int32_t)vert->win_h_x1;

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
                for (off = 0; off < 4; off++)
                    GRK_S_off(i, off) -= (GRK_D__off(i - 1, off) + GRK_D__off(i, off) + 2) >> 2;
                i ++;

                i_max = win_l_x1;
                if (i_max > dn)
                    i_max = dn;
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
                    for (off = 0; off < 4; off++)
                        GRK_S_off(i, off) -= (GRK_D_off(i - 1, off) + GRK_D_off(i, off) + 2) >> 2;
                }
                for (; i < win_l_x1; i++) {
                    /* Right-most case */
                    for (off = 0; off < 4; off++)
                        GRK_S_off(i, off) -= (GRK_D__off(i - 1, off) + GRK_D__off(i, off) + 2) >> 2;
                }
            }
            i = win_h_x0;
            if (i < win_h_x1) {
                int32_t i_max = win_h_x1;
                if (i_max >= sn)
                    i_max = sn - 1;
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
                    for (off = 0; off < 4; off++)
                        GRK_D_off(i, off) += (GRK_S_off(i, off) + GRK_S_off(i + 1, off)) >> 1;
                }
                for (; i < win_h_x1; i++) {
                    /* Right-most case */
                    for (off = 0; off < 4; off++)
                        GRK_D_off(i, off) += (GRK_S__off(i, off) + GRK_S__off(i + 1, off)) >> 1;
                }
            }
        }
    } else {
        if (!sn  && dn == 1) {        /* NEW :  CASE ONE ELEMENT */
            for (off = 0; off < 4; off++)
                GRK_S_off(0, off) /= 2;
        } else {
            for (i = win_l_x0; i < win_l_x1; i++) {
                for (off = 0; off < 4; off++)
                    GRK_D_off(i, off) -=
                    		(GRK_SS__off(i, off) + GRK_SS__off(i + 1, off) + 2) >> 2;
            }
            for (i = win_h_x0; i < win_h_x1; i++) {
                for (off = 0; off < 4; off++)
                    GRK_S_off(i, off) +=
                    		(GRK_DD__off(i, off) + GRK_DD__off(i - 1, off)) >> 1;
            }
        }
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

static void interleave_partial_h_97(dwt_data<vec4f>* dwt,
									sparse_array* sa,
									uint32_t sa_line,
									uint32_t num_rows){
    for (uint32_t i = 0; i < num_rows; i++) {
        bool ret = sa->read(dwt->win_l_x0,
						  sa_line + i,
						  dwt->win_l_x1,
						  sa_line + i + 1,
						  /* Nasty cast from float* to int32* */
						  (int32_t*)(dwt->mem + dwt->cas + 2 * dwt->win_l_x0) + i,
						  8, 0, true);
        assert(ret);
        ret = sa->read(dwt->sn + dwt->win_h_x0,
						  sa_line + i,
						  dwt->sn + dwt->win_h_x1,
						  sa_line + i + 1,
						  /* Nasty cast from float* to int32* */
						  (int32_t*)(dwt->mem + 1 - dwt->cas + 2 * dwt->win_h_x0) + i,
						  8, 0, true);
        assert(ret);
        GRK_UNUSED(ret);
    }
}


static void interleave_partial_v_97(dwt_data<vec4f>* GRK_RESTRICT dwt,
									sparse_array* sa,
									uint32_t sa_col,
									uint32_t nb_elts_read){
    bool ret = sa->read(sa_col,
    					dwt->win_l_x0,
						sa_col + nb_elts_read, dwt->win_l_x1,
						(int32_t*)(dwt->mem + dwt->cas + 2 * dwt->win_l_x0),
						1, 8, true);
    assert(ret);
    ret = sa->read(sa_col,
    				dwt->sn + dwt->win_h_x0,
					sa_col + nb_elts_read,
					dwt->sn + dwt->win_h_x1,
					(int32_t*)(dwt->mem + 1 - dwt->cas + 2 * dwt->win_h_x0),
					1, 8, true);
    assert(ret);
    GRK_UNUSED(ret);
}

class Partial97 {
public:
	void interleave_partial_h(dwt_data<vec4f>* dwt,
								sparse_array* sa,
								uint32_t sa_line,
								uint32_t num_rows){
		interleave_partial_h_97(dwt,sa,sa_line,num_rows);
	}
	void decode_h(dwt_data<vec4f>* dwt){
		decode_step_97(dwt);
	}
	void interleave_partial_v(dwt_data<vec4f>* GRK_RESTRICT dwt,
								sparse_array* sa,
								uint32_t sa_col,
								uint32_t nb_elts_read){
		interleave_partial_v_97(dwt,sa,sa_col,nb_elts_read);
	}
	void decode_v(dwt_data<vec4f>* dwt){
		decode_step_97(dwt);
	}
};

/* FILTER_WIDTH value matches the maximum left/right extension given in tables */
/* F.2 and F.3 of the standard. Note: in TileComponent::is_subband_area_of_interest() */
/* we currently use 3. */
template <typename T, uint32_t HORIZ_STEP, uint32_t VERT_STEP, uint32_t FILTER_WIDTH, typename D>
   bool decode_partial_tile(TileComponent* GRK_RESTRICT tilec, uint32_t numres, sparse_array *sa) {
    auto tr = tilec->resolutions;
    auto tr_max = &(tilec->resolutions[numres - 1]);
    if (tr_max->width() == 0 || tr_max->height() == 0)
        return true;

    if (numres == 1U) {
        auto win_bounds = tr_max->win_bounds.pan(-tr_max->x0,-tr_max->y0);
    	bool ret = sa->read(win_bounds,
					   tilec->buf->ptr(),
                       1,
					   tilec->buf->stride(),
                       true);
        assert(ret);
        GRK_UNUSED(ret);
        return true;
    }

    /* width of the resolution level computed */
    uint32_t rw = tr->width();
    /* height of the resolution level computed */
    uint32_t rh = tr->height();

    // in 53 vertical pass, we process 4 vertical columns at a time
    const uint32_t data_multiplier = (sizeof(T) == 4) ? 4 : 1;
    size_t data_size = dwt_utils::max_resolution(tr, numres) * data_multiplier;
	dwt_data<T> horiz;
    if (!horiz.alloc(data_size)) {
        GRK_ERROR("Out of memory");
        return false;
    }
	dwt_data<T> vert;
    vert.mem = horiz.mem;
    D decoder;
    size_t num_threads = ThreadPool::get()->num_threads();

    for (uint32_t resno = 1; resno < numres; resno ++) {
        horiz.sn = (int32_t)rw;
        vert.sn = (int32_t)rh;

        ++tr;
        rw = tr->width();
        rh = tr->height();

        horiz.dn = (int32_t)(rw - (uint32_t)horiz.sn);
        horiz.cas = tr->x0 & 1;

        vert.dn = (int32_t)(rh - (uint32_t)vert.sn);
        vert.cas = tr->y0 & 1;

        /* Get the sub-band coordinates for the window of interest */
        /* LL band */
        /* Window of interest sub-band-based coordinates */
        uint32_t win_ll_x0, win_ll_y0;
        uint32_t win_ll_x1, win_ll_y1;
        tilec->buf->get_region_band_coordinates(resno, 0,
                                     &win_ll_x0, &win_ll_y0,
                                     &win_ll_x1, &win_ll_y1);
        /* HL band */
        uint32_t win_hl_x0, win_hl_x1;
        tilec->buf->get_region_band_coordinates(resno, 1,
                                      &win_hl_x0, nullptr, &win_hl_x1, nullptr);
        /* LH band */
        uint32_t win_lh_y0, win_lh_y1;
        tilec->buf->get_region_band_coordinates(resno, 2,
                                     nullptr, &win_lh_y0, nullptr, &win_lh_y1);

        /* band coordinates */
        /* Beware: band index for non-LL0 resolution are 0=HL, 1=LH and 2=HH */
        uint32_t tr_ll_x0 = (uint32_t)tr->bands[1].x0;
        uint32_t tr_ll_y0 = (uint32_t)tr->bands[0].y0;
        uint32_t tr_hl_x0 = (uint32_t)tr->bands[0].x0;
        uint32_t tr_lh_y0 = (uint32_t)tr->bands[1].y0;

        /* Transform window of interest relative to band*/
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

        /* Compute resolution coordinates for window of interest */
        uint32_t win_tr_x0, win_tr_x1;
        if (horiz.cas == 0) {
            win_tr_x0 = min<uint32_t>(2 * win_ll_x0, 2 * win_hl_x0 + 1);
            win_tr_x1 = min<uint32_t>(max<uint32_t>(2 * win_ll_x1, 2 * win_hl_x1 + 1), rw);
        } else {
            win_tr_x0 = min<uint32_t>(2 * win_hl_x0, 2 * win_ll_x0 + 1);
            win_tr_x1 = min<uint32_t>(max<uint32_t>(2 * win_hl_x1, 2 * win_ll_x1 + 1), rw);
        }
        uint32_t win_tr_y0, win_tr_y1;
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
        if (!sa->alloc(win_tr_x0,
					  win_tr_y0,
					  win_tr_x1,
					  win_tr_y1))
			 return false;
		for (uint32_t k = 0; k < 2; ++k) {
			 if (!sa->alloc(win_tr_x0,
						  bounds[k][0],
						  win_tr_x1,
						  bounds[k][1]))
				 return false;
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
	            horiz.mem[win_tr_x1 - 1] = T(0);
	        if (win_tr_x1 < rw)
	            horiz.mem[win_tr_x1] = T(0);

			uint32_t num_jobs = (uint32_t)num_threads;
			uint32_t num_cols = bounds[k][1] - bounds[k][0] + 1;
			if (num_cols < num_jobs)
				num_jobs = num_cols;
			uint32_t step_j = num_jobs ? ( num_cols / num_jobs) : 0;
			if (num_threads == 1 ||step_j < HORIZ_STEP){
		     uint32_t j;
			 for (j = bounds[k][0]; j + HORIZ_STEP-1 < bounds[k][1]; j += HORIZ_STEP) {
				 decoder.interleave_partial_h(&horiz, sa, j,HORIZ_STEP);
				 decoder.decode_h(&horiz);
				 if (!sa->write( win_tr_x0,
								  j,
								  win_tr_x1,
								  j + HORIZ_STEP,
								  (int32_t*)(horiz.mem + win_tr_x0),
								  HORIZ_STEP,
								  1,
								  true)) {
					 GRK_ERROR("sparse array write failure");
					 horiz.release();
					 return false;
				 }
			 }
			 if (j < bounds[k][1] ) {
				 decoder.interleave_partial_h(&horiz, sa, j, bounds[k][1] - j);
				 decoder.decode_h(&horiz);
				 if (!sa->write( win_tr_x0,
								  j,
								  win_tr_x1,
								  bounds[k][1],
								  (int32_t*)(horiz.mem + win_tr_x0),
								  HORIZ_STEP,
								  1,
								  true)) {
					 GRK_ERROR("Sparse array write failure");
					 horiz.release();
					 return false;
				 }
			 }
		}else{
			std::vector< std::future<int> > results;
			for(uint32_t j = 0; j < num_jobs; ++j) {
			   auto job = new decode_job<float, dwt_data<T>>(horiz,
											bounds[k][0] + j * step_j,
											j < (num_jobs - 1U) ? bounds[k][0] + (j + 1U) * step_j : bounds[k][1]);
				if (!job->data.alloc(data_size)) {
					GRK_ERROR("Out of memory");
					horiz.release();
					return false;
				}
				results.emplace_back(
					ThreadPool::get()->enqueue([job,sa, win_tr_x0, win_tr_x1, &decoder] {
					 uint32_t j;
					 for (j = job->min_j; j + HORIZ_STEP-1 < job->max_j; j += HORIZ_STEP) {
						 decoder.interleave_partial_h(&job->data, sa, j,HORIZ_STEP);
						 decoder.decode_h(&job->data);
						 if (!sa->write( win_tr_x0,
										  j,
										  win_tr_x1,
										  j + HORIZ_STEP,
										  (int32_t*)(job->data.mem + win_tr_x0),
										  HORIZ_STEP,
										  1,
										  true)) {
							 GRK_ERROR("sparse array write failure");
							 job->data.release();
							 return 0;
						 }
					 }
					 if (j < job->max_j ) {
						 decoder.interleave_partial_h(&job->data, sa, j, job->max_j - j);
						 decoder.decode_h(&job->data);
						 if (!sa->write( win_tr_x0,
										  j,
										  win_tr_x1,
										  job->max_j,
										  (int32_t*)(job->data.mem + win_tr_x0),
										  HORIZ_STEP,
										  1,
										  true)) {
							 GRK_ERROR("Sparse array write failure");
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
			for(auto &result: results)
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
		if (num_threads == 1 || step_j < VERT_STEP){
	        uint32_t j;
			for (j = win_tr_x0; j + VERT_STEP < win_tr_x1; j += VERT_STEP) {
				decoder.interleave_partial_v(&vert, sa, j, VERT_STEP);
				decoder.decode_v(&vert);
				if (!sa->write(j,
							  win_tr_y0,
							  j + VERT_STEP,
							  win_tr_y1,
							  (int32_t*)vert.mem + VERT_STEP * win_tr_y0,
							  1,
							  VERT_STEP,
							  true)) {
					GRK_ERROR("Sparse array write failure");
					horiz.release();
					return false;
				}
			}
			if (j < win_tr_x1) {
				decoder.interleave_partial_v(&vert, sa, j, win_tr_x1 - j);
				decoder.decode_v(&vert);
				if (!sa->write( j,
								  win_tr_y0,
								  win_tr_x1,
								  win_tr_y1,
								  (int32_t*)vert.mem + VERT_STEP * win_tr_y0,
								  1,
								  VERT_STEP,
								  true)) {
					GRK_ERROR("Sparse array write failure");
					horiz.release();
					return false;
				}
			}
		} else {
			std::vector< std::future<int> > results;
			for(uint32_t j = 0; j < num_jobs; ++j) {
			   auto job = new decode_job<float, dwt_data<T>>(vert,
											win_tr_x0 + j * step_j,
											j < (num_jobs - 1U) ? win_tr_x0 + (j + 1U) * step_j : win_tr_x1);
				if (!job->data.alloc(data_size)) {
					GRK_ERROR("Out of memory");
					horiz.release();
					return false;
				}
				results.emplace_back(
					ThreadPool::get()->enqueue([job,sa, win_tr_y0, win_tr_y1, &decoder] {
					 uint32_t j;
					 for (j = job->min_j; j + VERT_STEP-1 < job->max_j; j += VERT_STEP) {
						decoder.interleave_partial_v(&job->data, sa, j, VERT_STEP);
						decoder.decode_v(&job->data);
						if (!sa->write(j,
									  win_tr_y0,
									  j + VERT_STEP,
									  win_tr_y1,
									  (int32_t*)job->data.mem + VERT_STEP * win_tr_y0,
									  1,
									  VERT_STEP,
									  true)) {
							GRK_ERROR("Sparse array write failure");
							job->data.release();
							return 0;
						}
					 }
					 if (j <  job->max_j) {
						decoder.interleave_partial_v(&job->data, sa, j,  job->max_j - j);
						decoder.decode_v(&job->data);
						if (!sa->write(			  j,
												  win_tr_y0,
												  job->max_j,
												  win_tr_y1,
												  (int32_t*)job->data.mem + VERT_STEP * win_tr_y0,
												  1,
												  VERT_STEP,
												  true)) {
							GRK_ERROR("Sparse array write failure");
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
			for(auto &result: results)
				result.get();
		}
    }
    //final read into tile buffer
    auto win_bounds = tr_max->win_bounds.pan(-tr_max->x0,-tr_max->y0);
	bool ret = sa->read(win_bounds,
					   tilec->buf->ptr(),
					   1,
					   tilec->buf->stride(),
					   true);
assert(ret);
	GRK_UNUSED(ret);
    horiz.release();

    return true;
}

/* <summary>                            */
/* Inverse 5-3 wavelet transform in 2-D. */
/* </summary>                           */
bool decode_53(TileProcessor *p_tcd, TileComponent* tilec,
                        uint32_t numres)
{
    if (p_tcd->whole_tile_decoding)
        return decode_tile_53(tilec,numres);
    else
        return decode_partial_tile<int32_t, 1, 4,2, Partial53>(tilec, numres, tilec->m_sa);
}

bool decode_97(TileProcessor *p_tcd,
                TileComponent* GRK_RESTRICT tilec,
                uint32_t numres){
    if (p_tcd->whole_tile_decoding)
        return decode_tile_97(tilec, numres);
    else
        return decode_partial_tile<vec4f,4,4,4, Partial97>(tilec, numres, tilec->m_sa);
}

}
