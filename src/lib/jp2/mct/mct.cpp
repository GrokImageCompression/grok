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

#include "CPUArch.h"
#include "grok_includes.h"

namespace grk {

/* <summary> */
/* This table contains the norms of the basis function of the reversible MCT. */
/* </summary> */
static const double mct_norms_rev[3] = { 1.732, .8292, .8292 };

/* <summary> */
/* This table contains the norms of the basis function of the irreversible MCT. */
/* </summary> */
static const double mct_norms_irrev[3] = { 1.732, 1.805, 1.573 };

const double* mct::get_norms_rev() {
	return mct_norms_rev;
}
const double* mct::get_norms_irrev() {
	return mct_norms_irrev;
}


/* <summary> */
/* Forward reversible MCT. */
/* </summary> */
void mct::encode_rev(int32_t *GRK_RESTRICT chan0, int32_t *GRK_RESTRICT chan1,
		int32_t *GRK_RESTRICT chan2, uint64_t n) {
	size_t i = 0;

	if (CPUArch::SSE2() || CPUArch::AVX2() ) {
#if (defined(__SSE2__) || defined(__AVX2__))
	size_t num_threads = ThreadPool::get()->num_threads();
    size_t chunkSize = n / num_threads;
    //ensure it is divisible by VREG_INT_COUNT
    chunkSize = (chunkSize/VREG_INT_COUNT) * VREG_INT_COUNT;
	if (chunkSize > VREG_INT_COUNT) {
		std::vector< std::future<int> > results;
	    for(uint64_t tr = 0; tr < num_threads; ++tr) {
	    	uint64_t index = tr;
			auto encoder = [index, chunkSize, chan0,chan1,chan2]()	{
				uint64_t begin = (uint64_t)index * chunkSize;
				for (auto j = begin; j < begin+chunkSize; j+=VREG_INT_COUNT ){
					VREG y, u, v;
					VREG r = LOAD((const VREG*) &chan0[j]);
					VREG g = LOAD((const VREG*) &chan1[j]);
					VREG b = LOAD((const VREG*) &chan2[j]);
					y = ADD(g, g);
					y = ADD(y, b);
					y = ADD(y, r);
					y = SAR(y, 2);
					u = SUB(b, g);
					v = SUB(r, g);
					STORE((VREG*) &chan0[j], y);
					STORE((VREG*) &chan1[j], u);
					STORE((VREG*) &chan2[j], v);
				}
				return 0;
			};

			if (num_threads > 1)
				results.emplace_back(ThreadPool::get()->enqueue(encoder));
			else
				encoder();
	    }
	    for(auto &result: results){
	        result.get();
	    }
		i = chunkSize * num_threads;
	}
#endif
	}
	for (; i < n; ++i) {
		int32_t r = chan0[i];
		int32_t g = chan1[i];
		int32_t b = chan2[i];
		int32_t y = (r + (g * 2) + b) >> 2;
		int32_t u = b - g;
		int32_t v = r - g;
		chan0[i] = y;
		chan1[i] = u;
		chan2[i] = v;
	}
}

////////////////////////////////////////////////////////////////////////////////

/* <summary> */
/* Inverse reversible MCT. */
/* </summary> */
void mct::decode_rev(int32_t *GRK_RESTRICT chan0, int32_t *GRK_RESTRICT chan1,
		int32_t *GRK_RESTRICT chan2, uint64_t n) {
	size_t i = 0;
	if (CPUArch::SSE2() || CPUArch::AVX2() ) {
#if (defined(__SSE2__) || defined(__AVX2__))
	size_t num_threads = ThreadPool::get()->num_threads();
    size_t chunkSize = n / num_threads;
    //ensure it is divisible by VREG_INT_COUNT
    chunkSize = (chunkSize/VREG_INT_COUNT) * VREG_INT_COUNT;
	if (chunkSize > VREG_INT_COUNT) {
	    std::vector< std::future<int> > results;
	    for(uint64_t threadid = 0; threadid < num_threads; ++threadid) {
	    	uint64_t index = threadid;
	    	auto decoder = [index, chunkSize,chan0,chan1,chan2](){
	    		uint64_t begin = (uint64_t)index * chunkSize;
				for (auto j = begin; j < begin+chunkSize; j+=VREG_INT_COUNT ){
					VREG y = LOAD(chan0 + j);
					VREG u = LOAD(chan1 + j);
					VREG v = LOAD(chan2 + j);
					VREG g = SUB(y, SAR(ADD(u, v), 2));
					VREG r = ADD(v, g);
					VREG b = ADD(u, g);
					STORE(chan0 + j, r);
					STORE(chan1 + j, g);
					STORE(chan2 + j, b);
				}
				return 0;
	    	};

	    	if (num_threads > 1)
	    		results.emplace_back(ThreadPool::get()->enqueue(decoder));
	    	else
	    		decoder();

	    }
	    for(auto &result: results){
	        result.get();
	    }
		i = chunkSize * num_threads;
	}
#endif
	}
	for (; i < n; ++i) {
		int32_t y = chan0[i];
		int32_t u = chan1[i];
		int32_t v = chan2[i];
		int32_t g = y - ((u + v) >> 2);
		int32_t r = v + g;
		int32_t b = u + g;
		chan0[i] = r;
		chan1[i] = g;
		chan2[i] = b;
	}
}
/* <summary> */
/* Forward irreversible MCT. */
/* </summary> */
void mct::encode_irrev( int* GRK_RESTRICT chan0,
		int* GRK_RESTRICT chan1,
		int* GRK_RESTRICT chan2,
						uint64_t n)
{
    size_t i = 0;

    const float a_r = 0.299f;
    const float a_g = 0.587f;
    const float a_b = 0.114f;
    const float cb = 0.5f/(1.0f-a_b);
    const float cr = 0.5f/(1.0f-a_r);

	if (CPUArch::AVX2() ) {
#if ( defined(__AVX2__))
	size_t num_threads = ThreadPool::get()->num_threads();
    size_t chunkSize = n / num_threads;
    //ensure it is divisible by VREG_INT_COUNT
    chunkSize = (chunkSize/VREG_INT_COUNT) * VREG_INT_COUNT;
	if (chunkSize > VREG_INT_COUNT) {
		std::vector< std::future<int> > results;
	    for(uint64_t tr = 0; tr < num_threads; ++tr) {
	    	uint64_t index = tr;
			auto encoder = [index, chunkSize, chan0,chan1,chan2]()	{
				const VREGF va_r = LOAD_CST_F(0.299f);
				const VREGF va_g = LOAD_CST_F(0.587f);
				const VREGF va_b = LOAD_CST_F(0.114f);
				const VREGF vcb = LOAD_CST_F(0.5f/(1.0f-0.114f));
				const VREGF vcr = LOAD_CST_F(0.5f/(1.0f-0.299f));

				uint64_t begin = (uint64_t)index * chunkSize;
				for (auto j = begin; j < begin+chunkSize; j+=VREG_INT_COUNT ){
					VREG ri = LOAD(chan0 + j);
					VREG gi = LOAD(chan1 + j);
					VREG bi = LOAD(chan2 + j);

					VREGF r = _mm256_cvtepi32_ps(ri);
					VREGF g = _mm256_cvtepi32_ps(gi);
					VREGF b = _mm256_cvtepi32_ps(bi);

					VREGF y = ADDF(ADDF(MULF(r, va_r),MULF(g, va_g)),MULF(b, va_b)) ;
					VREGF u = MULF(vcb, SUBF(b, y));
					VREGF v = MULF(vcr, SUBF(r, y));

					STORE(chan0 + j, _mm256_cvttps_epi32(y * (1 << 11)));
					STORE(chan1 + j, _mm256_cvttps_epi32(u * (1 << 11)));
					STORE(chan2 + j, _mm256_cvttps_epi32(v * (1 << 11)));
				}
				return 0;
			};

			if (num_threads > 1)
				results.emplace_back(ThreadPool::get()->enqueue(encoder));
			else
				encoder();
		}
		for(auto &result: results){
			result.get();
		}
		i = num_threads * chunkSize;
	}
#endif
    }
    for(; i < n; ++i) {
        float r = (float)chan0[i];
        float g = (float)chan1[i];
        float b = (float)chan2[i];

        float y = a_r * r + a_g * g + a_b * b;
        float u = cb * (b - y);
        float v = cr * (r - y);

        chan0[i] = (int32_t)(y * (1 << 11));
        chan1[i] = (int32_t)(u * (1 << 11));
        chan2[i] = (int32_t)(v * (1 << 11));
    }
}

/* <summary> */
/* Inverse irreversible MCT. */
/* </summary> */
void mct::decode_irrev(grk_tile* tile, TileComponentCodingParams *tccps,uint64_t n) {
	uint64_t i = 0;

	float *GRK_RESTRICT c0 = (float*) tile->comps[0].buf->ptr();
	float *GRK_RESTRICT c1 = (float*) tile->comps[1].buf->ptr();
	float *GRK_RESTRICT c2 = (float*) tile->comps[2].buf->ptr();
	int32_t *c0_i = (int32_t*)c0, *c1_i = (int32_t*)c1, *c2_i = (int32_t*)c2;

    int32_t shift[3];
    for (uint32_t compno =0; compno < 3; ++compno) {
    	auto tccp = tccps + compno;
    	shift[compno] = tccp->m_dc_level_shift;
    }
    assert(shift[1]==shift[0] && shift[2]==shift[1]);
    int32_t dcshift = shift[0];

	if (CPUArch::AVX2() ) {
#if defined(__AVX2__)
	size_t num_threads = ThreadPool::get()->num_threads();
	size_t chunkSize = n / num_threads;
	//ensure it is divisible by VREG_INT_COUNT
	chunkSize = (chunkSize/VREG_INT_COUNT) * VREG_INT_COUNT;
	if (chunkSize > VREG_INT_COUNT) {
		std::vector< std::future<int> > results;
		for(uint64_t threadid = 0; threadid < num_threads; ++threadid) {
			uint64_t index = threadid;
			auto decoder = [index, chunkSize, c0,c0_i,c1,c1_i,c2,c2_i, dcshift]() {
				const VREGF vrv = LOAD_CST_F(1.402f);
				const VREGF vgu = LOAD_CST_F(0.34413f);
				const VREGF vgv = LOAD_CST_F(0.71414f);
				const VREGF vbu = LOAD_CST_F(1.772f);
				const VREG  vdc = LOAD_CST(dcshift);

				uint64_t begin = (uint64_t)index * chunkSize;
				for (auto j = begin; j < begin+chunkSize; j +=VREG_INT_COUNT){
					VREGF vy, vu, vv;
					VREGF vr, vg, vb;

					vy = LOADF(c0 + j);
					vu = LOADF(c1 + j);
					vv = LOADF(c2 + j);
					vr = ADDF(vy, MULF(vv, vrv));
					vg = SUBF(SUBF(vy, MULF(vu, vgu)),MULF(vv, vgv));
					vb = ADDF(vy, MULF(vu, vbu));

					STORE(c0_i + j, ADD(_mm256_cvtps_epi32(vr),vdc));
					STORE(c1_i + j, ADD(_mm256_cvtps_epi32(vg),vdc));
					STORE(c2_i + j, ADD(_mm256_cvtps_epi32(vb),vdc));
				}
				return 0;
			};
			if (num_threads > 1)
				results.emplace_back(ThreadPool::get()->enqueue(decoder));
			else
				decoder();
		}
		for(auto &result: results){
			result.get();
		}
		i = chunkSize * num_threads;
	}
#endif
	}
	for (; i < n; ++i) {
		float y = c0[i];
		float u = c1[i];
		float v = c2[i];
		float r = y + (v * 1.402f);
		float g = y - (u * 0.34413f) - (v * (0.71414f));
		float b = y + (u * 1.772f);

		c0_i[i] = (int32_t)grok_lrintf(r) + dcshift;
		c1_i[i] = (int32_t)grok_lrintf(g) + dcshift;
		c2_i[i] = (int32_t)grok_lrintf(b) + dcshift;

	}
}

//////////////////////////////////////////////////////////////////////////////


void mct::calculate_norms(double *pNorms, uint32_t pNbComps, float *pMatrix) {
	float CurrentValue;
	double *Norms = (double*) pNorms;
	float *Matrix = (float*) pMatrix;

	uint32_t i;
	for (i = 0; i < pNbComps; ++i) {
	 Norms[i] = 0;
		uint32_t Index = i;
		uint32_t j;
		for (j = 0; j < pNbComps; ++j) {
		 CurrentValue = Matrix[Index];
		 Index += pNbComps;
		 Norms[i] += CurrentValue * CurrentValue;
		}
	 Norms[i] = sqrt(Norms[i]);
	}
}

bool mct::encode_custom(uint8_t *pCodingdata, uint64_t n, uint8_t **pData,
		uint32_t pNbComp, uint32_t isSigned) {
	auto Mct = (float*) pCodingdata;
	uint32_t NbMatCoeff = pNbComp * pNbComp;
	auto Data = (int32_t**) pData;
	uint32_t Multiplicator = 1 << 13;
	GRK_UNUSED(isSigned);

	auto CurrentData = (int32_t*) grk_malloc(
			(pNbComp + NbMatCoeff) * sizeof(int32_t));
	if (!CurrentData)
		return false;

	auto CurrentMatrix = CurrentData + pNbComp;

	for (uint64_t i = 0; i < NbMatCoeff; ++i)
	 CurrentMatrix[i] = (int32_t) (*(Mct++) * (float) Multiplicator);

	for (uint64_t i = 0; i < n; ++i) {
		for (uint32_t j = 0; j < pNbComp; ++j)
		 CurrentData[j] = (*(Data[j]));
		for (uint32_t j = 0; j < pNbComp; ++j) {
			auto MctPtr = CurrentMatrix;
			*(Data[j]) = 0;
			for (uint32_t k = 0; k < pNbComp; ++k) {
				*(Data[j]) += int_fix_mul(*MctPtr, CurrentData[k]);
				++MctPtr;
			}
			++Data[j];
		}
	}
	grk_free(CurrentData);

	return true;
}

bool mct::decode_custom(uint8_t *pDecodingData, uint64_t n, uint8_t **pData,
		uint32_t pNbComp, uint32_t isSigned) {
	auto Data = (float**) pData;

	GRK_UNUSED(isSigned);

	auto CurrentData = (float*) grk_malloc(2 * pNbComp * sizeof(float));
	if (!CurrentData)
		return false;

	auto CurrentResult = CurrentData + pNbComp;

	for (uint64_t i = 0; i < n; ++i) {
		auto Mct = (float*) pDecodingData;
		for (uint32_t j = 0; j < pNbComp; ++j) {
		 CurrentData[j] = (float) (*(Data[j]));
		}
		for (uint32_t j = 0; j < pNbComp; ++j) {
		 CurrentResult[j] = 0;
			for (uint32_t k = 0; k < pNbComp; ++k)
			 CurrentResult[j] += *(Mct++) * CurrentData[k];
			*(Data[j]++) = (float) (CurrentResult[j]);
		}
	}
	grk_free(CurrentData);
	return true;
}

}
