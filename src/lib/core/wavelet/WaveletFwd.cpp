/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"
#include <algorithm>
#include <limits>
#include <sstream>
namespace grk
{
template<typename T>
struct dwt_line
{
   T* mem;
   uint32_t dn; /* number of elements in high pass band */
   uint32_t sn; /* number of elements in low pass band */
   uint32_t parity; /* 0 = start on even coord, 1 = start on odd coord */
};

const uint32_t NB_ELTS_V8 = 8;

/* From table F.4 from the standard */
static const float alpha = -1.586134342f;
static const float beta = -0.052980118f;
static const float gamma = 0.882911075f;
static const float delta = 0.443506852f;
static const float grk_K = 1.230174105f;
static const float grk_invK = (float)(1.0 / 1.230174105);

#define GRK_S(i) a[(i) << 1]
#define GRK_D(i) a[(1 + ((i) << 1))]
#define GRK_S_(i) ((i) < 0 ? GRK_S(0) : ((i) >= sn ? GRK_S(sn - 1) : GRK_S(i)))
#define GRK_D_(i) ((i) < 0 ? GRK_D(0) : ((i) >= dn ? GRK_D(dn - 1) : GRK_D(i)))
#define GRK_SS_(i) ((i) < 0 ? GRK_S(0) : ((i) >= dn ? GRK_S(dn - 1) : GRK_S(i)))
#define GRK_DD_(i) ((i) < 0 ? GRK_D(0) : ((i) >= sn ? GRK_D(sn - 1) : GRK_D(i)))

/* <summary>                             */
/* Forward lazy transform (horizontal).  */
/* </summary>                            */
template<typename T>
void deinterleave_h(const T* GRK_RESTRICT a, T* GRK_RESTRICT b, int32_t dn, int32_t sn,
					int32_t parity)
{
   int32_t i;
   T* GRK_RESTRICT destPtr = b;
   const T* GRK_RESTRICT src = a + parity;

   for(i = 0; i < sn; ++i)
   {
	  *destPtr++ = *src;
	  src += 2;
   }

   destPtr = b + sn;
   src = a + 1 - parity;

   for(i = 0; i < dn; ++i)
   {
	  *destPtr++ = *src;
	  src += 2;
   }
}
void dwt97::encode_step1_combined(float* fw, uint32_t iters_c1, uint32_t iters_c2, const float c1,
								  const float c2)
{
   uint32_t i = 0;
   const uint32_t iters_common = std::min<uint32_t>(iters_c1, iters_c2);
   assert((((size_t)fw) & 0xf) == 0);
   assert(abs((int32_t)iters_c1 - (int32_t)iters_c2) <= 1);
   for(; i + 3 < iters_common; i += 4)
   {
#ifdef __SSE__
	  const __m128 vcst = _mm_set_ps(c2, c1, c2, c1);
	  *(__m128*)fw = _mm_mul_ps(*(__m128*)fw, vcst);
	  *(__m128*)(fw + 4) = _mm_mul_ps(*(__m128*)(fw + 4), vcst);
#else
	  fw[0] *= c1;
	  fw[1] *= c2;
	  fw[2] *= c1;
	  fw[3] *= c2;
	  fw[4] *= c1;
	  fw[5] *= c2;
	  fw[6] *= c1;
	  fw[7] *= c2;
#endif
	  fw += 8;
   }
   for(; i < iters_common; i++)
   {
	  fw[0] *= c1;
	  fw[1] *= c2;
	  fw += 2;
   }
   if(i < iters_c1)
	  fw[0] *= c1;
   else if(i < iters_c2)
	  fw[1] *= c2;
}

void dwt97::encode_step2(float* fl, float* fw, uint32_t end, uint32_t m, float c)
{
   uint32_t imax = std::min<uint32_t>(end, m);
   if(imax > 0)
   {
	  fw[-1] += (fl[0] + fw[0]) * c;
	  fw += 2;
	  uint32_t i = 1;
	  for(; i + 3 < imax; i += 4)
	  {
		 fw[-1] += (fw[-2] + fw[0]) * c;
		 fw[1] += (fw[0] + fw[2]) * c;
		 fw[3] += (fw[2] + fw[4]) * c;
		 fw[5] += (fw[4] + fw[6]) * c;
		 fw += 8;
	  }
	  for(; i < imax; ++i)
	  {
		 fw[-1] += (fw[-2] + fw[0]) * c;
		 fw += 2;
	  }
   }
   if(m < end)
   {
	  assert(m + 1 == end);
	  fw[-1] += (2 * fw[-2]) * c;
   }
}

void dwt97::encode_1_real(float* w, int32_t dn, int32_t sn, int32_t parity)
{
   int32_t a, b;
   assert(dn + sn > 1);
   if(parity == 0)
   {
	  a = 0;
	  b = 1;
   }
   else
   {
	  a = 1;
	  b = 0;
   }
   encode_step2(w + a, w + b + 1, (uint32_t)dn, (uint32_t)std::min<int32_t>(dn, sn - b), alpha);
   encode_step2(w + b, w + a + 1, (uint32_t)sn, (uint32_t)std::min<int32_t>(sn, dn - a), beta);
   encode_step2(w + a, w + b + 1, (uint32_t)dn, (uint32_t)std::min<int32_t>(dn, sn - b), gamma);
   encode_step2(w + b, w + a + 1, (uint32_t)sn, (uint32_t)std::min<int32_t>(sn, dn - a), delta);
   if(a == 0)
	  encode_step1_combined(w, (uint32_t)sn, (uint32_t)dn, grk_invK, grk_K);
   else
	  encode_step1_combined(w, (uint32_t)dn, (uint32_t)sn, grk_K, grk_invK);
}
template<typename T, typename DWT>
struct encode_h_job
{
   dwt_line<T> h;
   uint32_t rw; /* Width of the resolution to process */
   uint32_t w; /* Width of tiledp */
   T* GRK_RESTRICT tiledp;
   uint32_t min_j;
   uint32_t max_j;
   DWT dwt;
};

template<typename T, typename DWT>
void encode_h_func(encode_h_job<T, DWT>* job)
{
   uint32_t j;
   for(j = job->min_j; j < job->max_j; j++)
   {
	  T* GRK_RESTRICT aj = (T*)job->tiledp + j * job->w;
	  job->dwt.encode_and_deinterleave_h_one_row((T*)aj, (T*)job->h.mem, job->rw,
												 job->h.parity == 0 ? true : false);
   }
   grk_aligned_free(job->h.mem);
   delete job;
}

template<typename T, typename DWT>
struct encode_v_job
{
   dwt_line<T> v;
   uint32_t rh;
   uint32_t w;
   T* GRK_RESTRICT tiledp;
   uint32_t min_j;
   uint32_t max_j;
   DWT dwt;
};

template<typename T, typename DWT>
void encode_v_func(encode_v_job<T, DWT>* job)
{
   uint32_t j;
   for(j = job->min_j; j + NB_ELTS_V8 - 1 < job->max_j; j += NB_ELTS_V8)
	  job->dwt.encode_and_deinterleave_v((T*)job->tiledp + j, (T*)job->v.mem, job->rh,
										 job->v.parity == 0, job->w, NB_ELTS_V8);
   if(j < job->max_j)
	  job->dwt.encode_and_deinterleave_v((T*)job->tiledp + j, (T*)job->v.mem, job->rh,
										 job->v.parity == 0, job->w, job->max_j - j);
   grk_aligned_free(job->v.mem);
   delete job;
}

/** Fetch up to cols <= NB_ELTS_V8 for each line, and put them in tmpOut */
/* that has a NB_ELTS_V8 interleave factor. */
template<typename T>
void fetch_cols_vertical_pass(const T* array, T* tmp, uint32_t height, uint32_t stride_width,
							  uint32_t cols)
{
   if(cols == NB_ELTS_V8)
   {
	  uint32_t k;
	  for(k = 0; k < height; ++k)
		 memcpy(tmp + NB_ELTS_V8 * k, array + k * stride_width, NB_ELTS_V8 * sizeof(T));
   }
   else
   {
	  uint32_t k;
	  for(k = 0; k < height; ++k)
	  {
		 uint32_t c;
		 for(c = 0; c < cols; c++)
			tmp[NB_ELTS_V8 * k + c] = array[c + k * stride_width];
		 for(; c < NB_ELTS_V8; c++)
			tmp[NB_ELTS_V8 * k + c] = 0;
	  }
   }
}

/* Deinterleave result of forward transform, where cols <= NB_ELTS_V8 */
/* and src contains NB_ELTS_V8 consecutive values for up to NB_ELTS_V8 */
/* columns. */
template<typename T>
void deinterleave_v_cols(const T* GRK_RESTRICT src, T* GRK_RESTRICT dst, uint32_t dn, uint32_t sn,
						 uint32_t stride_width, uint32_t parity, uint32_t cols)
{
   int64_t i = sn;
   T* GRK_RESTRICT destPtr = dst;
   const T* GRK_RESTRICT srcPtr = src + parity * NB_ELTS_V8;
   uint32_t c;

   for(uint32_t k = 0; k < 2; k++)
   {
	  while(i--)
	  {
		 if(cols == NB_ELTS_V8)
		 {
			memcpy(destPtr, srcPtr, NB_ELTS_V8 * sizeof(T));
		 }
		 else
		 {
			c = 0;
			switch(cols)
			{
			   case 7:
				  destPtr[c] = srcPtr[c];
				  c++; /* fallthru */
			   case 6:
				  destPtr[c] = srcPtr[c];
				  c++; /* fallthru */
			   case 5:
				  destPtr[c] = srcPtr[c];
				  c++; /* fallthru */
			   case 4:
				  destPtr[c] = srcPtr[c];
				  c++; /* fallthru */
			   case 3:
				  destPtr[c] = srcPtr[c];
				  c++; /* fallthru */
			   case 2:
				  destPtr[c] = srcPtr[c];
				  c++; /* fallthru */
			   default:
				  destPtr[c] = srcPtr[c];
				  break;
			}
		 }
		 destPtr += stride_width;
		 srcPtr += 2 * NB_ELTS_V8;
	  }

	  destPtr = dst + (size_t)sn * (size_t)stride_width;
	  srcPtr = src + (1 - parity) * NB_ELTS_V8;
	  i = dn;
   }
}
void dwt97::grk_v8dwt_encode_step1(float* fw, uint32_t end, const float cst)
{
   uint32_t i;
#ifdef __SSE__
   __m128* vw = (__m128*)fw;
   const __m128 vcst = _mm_set1_ps(cst);
   for(i = 0; i < end; ++i)
   {
	  vw[0] = _mm_mul_ps(vw[0], vcst);
	  vw[1] = _mm_mul_ps(vw[1], vcst);
	  vw += 2 * (NB_ELTS_V8 * sizeof(float) / sizeof(__m128));
   }
#else
   uint32_t c;
   for(i = 0; i < end; ++i)
   {
	  for(c = 0; c < NB_ELTS_V8; c++)
		 fw[i * 2 * NB_ELTS_V8 + c] *= cst;
   }
#endif
}

void dwt97::grk_v8dwt_encode_step2(float* fl, float* fw, uint32_t end, uint32_t m, float cst)
{
   uint32_t i;
   uint32_t imax = std::min<uint32_t>(end, m);
#ifdef __SSE__
   __m128* vw = (__m128*)fw;
   __m128 vcst = _mm_set1_ps(cst);
   if(imax > 0)
   {
	  __m128* vl = (__m128*)fl;
	  vw[-2] = _mm_add_ps(vw[-2], _mm_mul_ps(_mm_add_ps(vl[0], vw[0]), vcst));
	  vw[-1] = _mm_add_ps(vw[-1], _mm_mul_ps(_mm_add_ps(vl[1], vw[1]), vcst));
	  vw += 2 * (NB_ELTS_V8 * sizeof(float) / sizeof(__m128));
	  i = 1;

	  for(; i < imax; ++i)
	  {
		 vw[-2] = _mm_add_ps(vw[-2], _mm_mul_ps(_mm_add_ps(vw[-4], vw[0]), vcst));
		 vw[-1] = _mm_add_ps(vw[-1], _mm_mul_ps(_mm_add_ps(vw[-3], vw[1]), vcst));
		 vw += 2 * (NB_ELTS_V8 * sizeof(float) / sizeof(__m128));
	  }
   }
   if(m < end)
   {
	  assert(m + 1 == end);
	  vcst = _mm_add_ps(vcst, vcst);
	  vw[-2] = _mm_add_ps(vw[-2], _mm_mul_ps(vw[-4], vcst));
	  vw[-1] = _mm_add_ps(vw[-1], _mm_mul_ps(vw[-3], vcst));
   }
#else
   uint32_t c;
   const int64_t NB_ELTS = (int64_t)NB_ELTS_V8;
   if(imax > 0)
   {
	  for(c = 0; c < NB_ELTS; c++)
		 fw[-1 * NB_ELTS + c] += (fl[0 * NB_ELTS + c] + fw[0 * NB_ELTS + c]) * cst;
	  fw += 2 * NB_ELTS;
	  i = 1;
	  for(; i < imax; ++i)
	  {
		 for(c = 0; c < NB_ELTS; c++)
			fw[-1 * NB_ELTS + c] += (fw[-2 * NB_ELTS + c] + fw[0 * NB_ELTS + c]) * cst;
		 fw += 2 * NB_ELTS;
	  }
   }
   if(m < end)
   {
	  assert(m + 1 == end);
	  for(c = 0; c < NB_ELTS; c++)
		 fw[-1 * NB_ELTS + c] += (2 * fw[-2 * NB_ELTS + c]) * cst;
   }
#endif
}
/* <summary>                            */
/* Forward 5-3 wavelet transform in 2-D. */
/* </summary>                           */
template<typename T, typename DWT>
bool WaveletFwdImpl::encode_procedure(TileComponent* tilec)
{
   if(tilec->numresolutions == 1U)
	  return true;

   // const int num_threads = grk_thread_pool_get_thread_count(tp);
   uint32_t stride = tilec->getWindow()->getResWindowBufferHighestSimple().stride_;
   T* GRK_RESTRICT tiledp = (T*)tilec->getWindow()->getResWindowBufferHighestSimple().buf_;

   uint8_t maxNumResolutions = (uint8_t)(tilec->numresolutions - 1);
   auto currentRes = tilec->resolutions_ + maxNumResolutions;
   auto lastRes = currentRes - 1;

   size_t dataSize = max_resolution(tilec->resolutions_, tilec->numresolutions);
   /* overflow check */
   if(dataSize > (SIZE_MAX / (NB_ELTS_V8 * sizeof(int32_t))))
   {
	  Logger::logger_.error("Forward wavelet overflow");
	  return false;
   }
   dataSize *= NB_ELTS_V8 * sizeof(int32_t);
   auto bj = (T*)grk_aligned_malloc(dataSize);
   /* dataSize is equal to 0 when numresolutions == 1 but bj is not used */
   /* in that case, so do not error out */
   if(dataSize != 0 && !bj)
	  return false;
   int32_t i = maxNumResolutions;
   uint32_t num_threads = ExecSingleton::get().num_workers() > 1 ? 2 : 1;
   DWT dwt;
   while(i--)
   {
	  // width of the resolution level computed
	  uint32_t rw = (uint32_t)(currentRes->x1 - currentRes->x0);
	  // height of the resolution level computed
	  uint32_t rh = (uint32_t)(currentRes->y1 - currentRes->y0);
	  // width of the resolution level once lower than computed one
	  uint32_t rw1 = (uint32_t)(lastRes->x1 - lastRes->x0);
	  // height of the resolution level once lower than computed one
	  uint32_t rh1 = (uint32_t)(lastRes->y1 - lastRes->y0);

	  /* 0 = non inversion on horizontal filtering 1 = inversion between low-pass and high-pass
	   * filtering */
	  uint32_t parity_row = currentRes->x0 & 1;
	  /* 0 = non inversion on vertical filtering 1 = inversion between low-pass and high-pass
	   * filtering   */
	  uint32_t parity_col = currentRes->y0 & 1;

	  uint32_t sn = rh1;
	  uint32_t dn = (uint32_t)(rh - rh1);

	  bool rc = true;

	  /* Perform vertical pass */
	  if(num_threads <= 1 || rw < 2 * NB_ELTS_V8)
	  {
		 uint32_t j;
		 for(j = 0; j + NB_ELTS_V8 - 1 < rw; j += NB_ELTS_V8)
			dwt.encode_and_deinterleave_v((T*)tiledp + j, bj, rh, parity_col == 0, stride,
										  NB_ELTS_V8);
		 if(j < rw)
			dwt.encode_and_deinterleave_v((T*)tiledp + j, bj, rh, parity_col == 0, stride, rw - j);
	  }
	  else
	  {
		 uint32_t num_jobs = (uint32_t)num_threads;
		 uint32_t step_j;

		 if(rw < num_jobs)
			num_jobs = rw;
		 step_j = ((rw / num_jobs) / NB_ELTS_V8) * NB_ELTS_V8;
		 tf::Taskflow taskflow;
		 tf::Task* node = nullptr;
		 if(num_jobs > 1)
		 {
			node = new tf::Task[num_jobs];
			for(uint64_t j = 0; j < num_jobs; j++)
			   node[j] = taskflow.placeholder();
		 }
		 for(uint32_t j = 0; j < num_jobs; j++)
		 {
			auto job = new encode_v_job<T, DWT>();
			job->v.mem = (T*)grk_aligned_malloc(dataSize);
			if(!job->v.mem)
			{
			   delete job;
			   grk_aligned_free(bj);
			   rc = false;
			   break;
			}
			job->v.dn = dn;
			job->v.sn = sn;
			job->v.parity = parity_col;
			job->rh = rh;
			job->w = stride;
			job->tiledp = tiledp;
			job->min_j = j * step_j;
			job->max_j = (j + 1 == num_jobs) ? rw : (j + 1) * step_j;
			if(node)
			{
			   node[j].work([job] {
				  encode_v_func<T>(job);
				  return 0;
			   });
			}
			else
			{
			   encode_v_func<T>(job);
			}
		 }
		 if(node)
		 {
			ExecSingleton::get().run(taskflow).wait();
			delete[] node;
		 }
		 if(!rc)
			return false;
	  }

	  sn = rw1;
	  dn = (uint32_t)(rw - rw1);

	  /* Perform horizontal pass */
	  if(num_threads <= 1 || rh <= 1)
	  {
		 uint32_t j;
		 for(j = 0; j < rh; j++)
		 {
			T* GRK_RESTRICT aj = (T*)(tiledp) + j * stride;
			dwt.encode_and_deinterleave_h_one_row(aj, bj, rw, parity_row == 0 ? true : false);
		 }
	  }
	  else
	  {
		 uint32_t num_jobs = (uint32_t)num_threads;
		 uint32_t step_j;

		 if(rh < num_jobs)
			num_jobs = rh;
		 step_j = (rh / num_jobs);
		 tf::Taskflow taskflow;
		 tf::Task* node = nullptr;
		 if(num_jobs > 1)
		 {
			node = new tf::Task[num_jobs];
			for(uint64_t j = 0; j < num_jobs; j++)
			   node[j] = taskflow.placeholder();
		 }
		 for(uint32_t j = 0; j < num_jobs; j++)
		 {
			auto job = new encode_h_job<T, DWT>();
			job->h.mem = (T*)grk_aligned_malloc(dataSize);
			if(!job->h.mem)
			{
			   delete job;
			   grk_aligned_free(bj);
			   rc = false;
			   break;
			}
			job->h.dn = dn;
			job->h.sn = sn;
			job->h.parity = parity_row;
			job->rw = rw;
			job->w = stride;
			job->tiledp = tiledp;
			job->min_j = j * step_j;
			job->max_j = (j + 1U) * step_j; // this can overflow
			if(j == (num_jobs - 1U))
			{ // this will take care of the overflow
			   job->max_j = rh;
			}
			if(node)
			   node[j].work([job] { encode_h_func<T, DWT>(job); });
			else
			   encode_h_func<T, DWT>(job);
		 }
		 if(node)
		 {
			ExecSingleton::get().run(taskflow).wait();
			delete[] node;
		 }
		 if(!rc)
			return false;
	  }
	  currentRes = lastRes;
	  --lastRes;
   }

   grk_aligned_free(bj);
   return true;
}

bool WaveletFwdImpl::compress(TileComponent* tile_comp, uint8_t qmfbid)
{
   return (qmfbid == 1) ? encode_procedure<int32_t, dwt53>(tile_comp)
						: encode_procedure<float, dwt97>(tile_comp);
}

//////////////////////////////////////////////////////////////////////////////////////////////

/* Forward 5-3 transform, for the vertical pass, processing cols columns */
/* where cols <= NB_ELTS_V8 */
void dwt53::encode_and_deinterleave_v(int32_t* arrayIn, int32_t* tmpIn, uint32_t height, bool even,
									  uint32_t stride_width, uint32_t cols)
{
   int32_t* GRK_RESTRICT array = (int32_t * GRK_RESTRICT) arrayIn;
   int32_t* GRK_RESTRICT tmp = (int32_t * GRK_RESTRICT) tmpIn;
   const uint32_t sn = (height + (even ? 1 : 0)) >> 1;
   const uint32_t dn = height - sn;

   fetch_cols_vertical_pass<int32_t>(arrayIn, tmpIn, height, stride_width, cols);

#define GRK_Sc(i) tmp[((i) << 1) * NB_ELTS_V8 + c]
#define GRK_Dc(i) tmp[((1 + ((i) << 1))) * NB_ELTS_V8 + c]

#ifdef __SSE2__
   if(height == 1)
   {
	  if(!even)
	  {
		 uint32_t c;
		 for(c = 0; c < NB_ELTS_V8; c++)
			tmp[c] *= 2;
	  }
   }
   else if(even)
   {
	  uint32_t c;
	  uint32_t i;
	  i = 0;
	  if(i + 1 < sn)
	  {
		 __m128i xmm_Si_0 = *(const __m128i*)(tmp + 4 * 0);
		 __m128i xmm_Si_1 = *(const __m128i*)(tmp + 4 * 1);
		 for(; i + 1 < sn; i++)
		 {
			__m128i xmm_Sip1_0 = *(const __m128i*)(tmp + ((i + 1) << 1) * NB_ELTS_V8 + 4 * 0);
			__m128i xmm_Sip1_1 = *(const __m128i*)(tmp + ((i + 1) << 1) * NB_ELTS_V8 + 4 * 1);
			__m128i xmm_Di_0 = *(const __m128i*)(tmp + (1 + (i << 1)) * NB_ELTS_V8 + 4 * 0);
			__m128i xmm_Di_1 = *(const __m128i*)(tmp + (1 + (i << 1)) * NB_ELTS_V8 + 4 * 1);
			xmm_Di_0 =
				_mm_sub_epi32(xmm_Di_0, _mm_srai_epi32(_mm_add_epi32(xmm_Si_0, xmm_Sip1_0), 1));
			xmm_Di_1 =
				_mm_sub_epi32(xmm_Di_1, _mm_srai_epi32(_mm_add_epi32(xmm_Si_1, xmm_Sip1_1), 1));
			*(__m128i*)(tmp + (1 + (i << 1)) * NB_ELTS_V8 + 4 * 0) = xmm_Di_0;
			*(__m128i*)(tmp + (1 + (i << 1)) * NB_ELTS_V8 + 4 * 1) = xmm_Di_1;
			xmm_Si_0 = xmm_Sip1_0;
			xmm_Si_1 = xmm_Sip1_1;
		 }
	  }
	  if(((height)&1) == 0)
	  {
		 for(c = 0; c < NB_ELTS_V8; c++)
			GRK_Dc(i) -= GRK_Sc(i);
	  }
	  for(c = 0; c < NB_ELTS_V8; c++)
		 GRK_Sc(0) += (GRK_Dc(0) + GRK_Dc(0) + 2) >> 2;
	  i = 1;
	  if(i < dn)
	  {
		 __m128i xmm_Dim1_0 = *(const __m128i*)(tmp + (1 + ((i - 1) << 1)) * NB_ELTS_V8 + 4 * 0);
		 __m128i xmm_Dim1_1 = *(const __m128i*)(tmp + (1 + ((i - 1) << 1)) * NB_ELTS_V8 + 4 * 1);
		 const __m128i xmm_two = _mm_set1_epi32(2);
		 for(; i < dn; i++)
		 {
			__m128i xmm_Di_0 = *(const __m128i*)(tmp + (1 + (i << 1)) * NB_ELTS_V8 + 4 * 0);
			__m128i xmm_Di_1 = *(const __m128i*)(tmp + (1 + (i << 1)) * NB_ELTS_V8 + 4 * 1);
			__m128i xmm_Si_0 = *(const __m128i*)(tmp + (i << 1) * NB_ELTS_V8 + 4 * 0);
			__m128i xmm_Si_1 = *(const __m128i*)(tmp + (i << 1) * NB_ELTS_V8 + 4 * 1);
			xmm_Si_0 = _mm_add_epi32(
				xmm_Si_0,
				_mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(xmm_Dim1_0, xmm_Di_0), xmm_two), 2));
			xmm_Si_1 = _mm_add_epi32(
				xmm_Si_1,
				_mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(xmm_Dim1_1, xmm_Di_1), xmm_two), 2));
			*(__m128i*)(tmp + (i << 1) * NB_ELTS_V8 + 4 * 0) = xmm_Si_0;
			*(__m128i*)(tmp + (i << 1) * NB_ELTS_V8 + 4 * 1) = xmm_Si_1;
			xmm_Dim1_0 = xmm_Di_0;
			xmm_Dim1_1 = xmm_Di_1;
		 }
	  }
	  if(((height)&1) == 1)
	  {
		 for(c = 0; c < NB_ELTS_V8; c++)
			GRK_Sc(i) += (GRK_Dc(i - 1) + GRK_Dc(i - 1) + 2) >> 2;
	  }
   }
   else
   {
	  uint32_t c;
	  uint32_t i;
	  for(c = 0; c < NB_ELTS_V8; c++)
	  {
		 GRK_Sc(0) -= GRK_Dc(0);
	  }
	  i = 1;
	  if(i < sn)
	  {
		 __m128i xmm_Dim1_0 = *(const __m128i*)(tmp + (1 + ((i - 1) << 1)) * NB_ELTS_V8 + 4 * 0);
		 __m128i xmm_Dim1_1 = *(const __m128i*)(tmp + (1 + ((i - 1) << 1)) * NB_ELTS_V8 + 4 * 1);
		 for(; i < sn; i++)
		 {
			__m128i xmm_Di_0 = *(const __m128i*)(tmp + (1 + (i << 1)) * NB_ELTS_V8 + 4 * 0);
			__m128i xmm_Di_1 = *(const __m128i*)(tmp + (1 + (i << 1)) * NB_ELTS_V8 + 4 * 1);
			__m128i xmm_Si_0 = *(const __m128i*)(tmp + (i << 1) * NB_ELTS_V8 + 4 * 0);
			__m128i xmm_Si_1 = *(const __m128i*)(tmp + (i << 1) * NB_ELTS_V8 + 4 * 1);
			xmm_Si_0 =
				_mm_sub_epi32(xmm_Si_0, _mm_srai_epi32(_mm_add_epi32(xmm_Di_0, xmm_Dim1_0), 1));
			xmm_Si_1 =
				_mm_sub_epi32(xmm_Si_1, _mm_srai_epi32(_mm_add_epi32(xmm_Di_1, xmm_Dim1_1), 1));
			*(__m128i*)(tmp + (i * 2) * NB_ELTS_V8 + 4 * 0) = xmm_Si_0;
			*(__m128i*)(tmp + (i * 2) * NB_ELTS_V8 + 4 * 1) = xmm_Si_1;
			xmm_Dim1_0 = xmm_Di_0;
			xmm_Dim1_1 = xmm_Di_1;
		 }
	  }
	  if(((height)&1) == 1)
	  {
		 for(c = 0; c < NB_ELTS_V8; c++)
			GRK_Sc(i) -= GRK_Dc(i - 1);
	  }
	  i = 0;
	  if(i + 1 < dn)
	  {
		 __m128i xmm_Si_0 = *((const __m128i*)(tmp + 4 * 0));
		 __m128i xmm_Si_1 = *((const __m128i*)(tmp + 4 * 1));
		 const __m128i xmm_two = _mm_set1_epi32(2);
		 for(; i + 1 < dn; i++)
		 {
			__m128i xmm_Sip1_0 = *(const __m128i*)(tmp + ((i + 1) << 1) * NB_ELTS_V8 + 4 * 0);
			__m128i xmm_Sip1_1 = *(const __m128i*)(tmp + ((i + 1) << 1) * NB_ELTS_V8 + 4 * 1);
			__m128i xmm_Di_0 = *(const __m128i*)(tmp + (1 + (i << 1)) * NB_ELTS_V8 + 4 * 0);
			__m128i xmm_Di_1 = *(const __m128i*)(tmp + (1 + (i << 1)) * NB_ELTS_V8 + 4 * 1);
			xmm_Di_0 = _mm_add_epi32(
				xmm_Di_0,
				_mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(xmm_Si_0, xmm_Sip1_0), xmm_two), 2));
			xmm_Di_1 = _mm_add_epi32(
				xmm_Di_1,
				_mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(xmm_Si_1, xmm_Sip1_1), xmm_two), 2));
			*(__m128i*)(tmp + (1 + (i << 1)) * NB_ELTS_V8 + 4 * 0) = xmm_Di_0;
			*(__m128i*)(tmp + (1 + (i << 1)) * NB_ELTS_V8 + 4 * 1) = xmm_Di_1;
			xmm_Si_0 = xmm_Sip1_0;
			xmm_Si_1 = xmm_Sip1_1;
		 }
	  }
	  if(((height)&1) == 0)
	  {
		 for(c = 0; c < NB_ELTS_V8; c++)
			GRK_Dc(i) += (GRK_Sc(i) + GRK_Sc(i) + 2) >> 2;
	  }
   }
#else
   if(even)
   {
	  if(height > 1)
	  {
		 uint32_t i;
		 uint32_t c;
		 for(i = 0; i + 1 < sn; i++)
		 {
			for(c = 0; c < NB_ELTS_V8; c++)
			   GRK_Dc(i) -= (GRK_Sc(i) + GRK_Sc(i + 1)) >> 1;
		 }
		 if(((height) % 2) == 0)
		 {
			for(c = 0; c < NB_ELTS_V8; c++)
			   GRK_Dc(i) -= GRK_Sc(i);
		 }
		 for(c = 0; c < NB_ELTS_V8; c++)
			GRK_Sc(0) += (GRK_Dc(0) + GRK_Dc(0) + 2) >> 2;
		 for(i = 1; i < dn; i++)
		 {
			for(c = 0; c < NB_ELTS_V8; c++)
			   GRK_Sc(i) += (GRK_Dc(i - 1) + GRK_Dc(i) + 2) >> 2;
		 }
		 if(((height) % 2) == 1)
		 {
			for(c = 0; c < NB_ELTS_V8; c++)
			   GRK_Sc(i) += (GRK_Dc(i - 1) + GRK_Dc(i - 1) + 2) >> 2;
		 }
	  }
   }
   else
   {
	  uint32_t c;
	  if(height == 1)
	  {
		 for(c = 0; c < NB_ELTS_V8; c++)
			GRK_Sc(0) *= 2;
	  }
	  else
	  {
		 uint32_t i;
		 for(c = 0; c < NB_ELTS_V8; c++)
			GRK_Sc(0) -= GRK_Dc(0);
		 for(i = 1; i < sn; i++)
		 {
			for(c = 0; c < NB_ELTS_V8; c++)
			   GRK_Sc(i) -= (GRK_Dc(i) + GRK_Dc(i - 1)) >> 1;
		 }
		 if(((height) % 2) == 1)
		 {
			for(c = 0; c < NB_ELTS_V8; c++)
			   GRK_Sc(i) -= GRK_Dc(i - 1);
		 }
		 for(i = 0; i + 1 < dn; i++)
		 {
			for(c = 0; c < NB_ELTS_V8; c++)
			   GRK_Dc(i) += (GRK_Sc(i) + GRK_Sc(i + 1) + 2) >> 2;
		 }
		 if(((height) % 2) == 0)
		 {
			for(c = 0; c < NB_ELTS_V8; c++)
			   GRK_Dc(i) += (GRK_Sc(i) + GRK_Sc(i) + 2) >> 2;
		 }
	  }
   }
#endif

   if(cols == NB_ELTS_V8)
	  deinterleave_v_cols(tmp, array, dn, sn, stride_width, even ? 0 : 1, NB_ELTS_V8);
   else
	  deinterleave_v_cols(tmp, array, dn, sn, stride_width, even ? 0 : 1, cols);
}

/** Process one line for the horizontal pass of the 5x3 forward transform */
void dwt53::encode_and_deinterleave_h_one_row(int32_t* rowIn, int32_t* tmpIn, uint32_t width,
											  bool even)
{
   int32_t* GRK_RESTRICT row = (int32_t*)rowIn;
   int32_t* GRK_RESTRICT tmp = (int32_t*)tmpIn;
   const int32_t sn = (int32_t)((width + (even ? 1 : 0)) >> 1);
   const int32_t dn = (int32_t)(width - (uint32_t)sn);

   if(even)
   {
	  if(width > 1)
	  {
		 int32_t i;
		 for(i = 0; i < sn - 1; i++)
			tmp[sn + i] = row[(i << 1) + 1] - ((row[i << 1] + row[(i + 1) << 1]) >> 1);
		 if((width & 1) == 0)
			tmp[sn + i] = row[(i << 1) + 1] - row[i << 1];
		 row[0] += (tmp[sn] + tmp[sn] + 2) >> 2;
		 for(i = 1; i < dn; i++)
			row[i] = row[i << 1] + ((tmp[sn + (i - 1)] + tmp[sn + i] + 2) >> 2);
		 if((width & 1) == 1)
			row[i] = row[i << 1] + ((tmp[sn + (i - 1)] + tmp[sn + (i - 1)] + 2) >> 2);
		 memcpy(row + sn, tmp + sn, (size_t)dn * sizeof(int32_t));
	  }
   }
   else
   {
	  if(width == 1)
	  {
		 row[0] = row[0] << 1;
	  }
	  else
	  {
		 int32_t i;
		 tmp[sn + 0] = row[0] - row[1];
		 for(i = 1; i < sn; i++)
			tmp[sn + i] = row[i << 1] - ((row[(i << 1) + 1] + row[((i - 1) << 1) + 1]) >> 1);
		 if((width & 1) == 1)
			tmp[sn + i] = row[i << 1] - row[((i - 1) << 1) + 1];
		 for(i = 0; i < dn - 1; i++)
			row[i] = row[(i << 1) + 1] + ((tmp[sn + i] + tmp[sn + i + 1] + 2) >> 2);
		 if((width & 1) == 0)
			row[i] = row[(i << 1) + 1] + ((tmp[sn + i] + tmp[sn + i] + 2) >> 2);
		 memcpy(row + sn, tmp + sn, (size_t)dn * sizeof(int32_t));
	  }
   }
}

/* Forward 9-7 transform, for the vertical pass, processing cols columns */
/* where cols <= NB_ELTS_V8 */
void dwt97::encode_and_deinterleave_v(float* arrayIn, float* tmpIn, uint32_t height, bool even,
									  uint32_t stride_width, uint32_t cols)
{
   float* GRK_RESTRICT array = (float* GRK_RESTRICT)arrayIn;
   float* GRK_RESTRICT tmp = (float* GRK_RESTRICT)tmpIn;
   const uint32_t sn = (height + (even ? 1 : 0)) >> 1;
   const uint32_t dn = height - sn;
   uint32_t a, b;

   if(height == 1)
	  return;

   fetch_cols_vertical_pass(arrayIn, tmpIn, height, stride_width, cols);
   if(even)
   {
	  a = 0;
	  b = 1;
   }
   else
   {
	  a = 1;
	  b = 0;
   }
   grk_v8dwt_encode_step2(tmp + a * NB_ELTS_V8, tmp + (b + 1) * NB_ELTS_V8, dn,
						  std::min<uint32_t>(dn, sn - b), alpha);
   grk_v8dwt_encode_step2(tmp + b * NB_ELTS_V8, tmp + (a + 1) * NB_ELTS_V8, sn,
						  std::min<uint32_t>(sn, dn - a), beta);
   grk_v8dwt_encode_step2(tmp + a * NB_ELTS_V8, tmp + (b + 1) * NB_ELTS_V8, dn,
						  std::min<uint32_t>(dn, sn - b), gamma);
   grk_v8dwt_encode_step2(tmp + b * NB_ELTS_V8, tmp + (a + 1) * NB_ELTS_V8, sn,
						  std::min<uint32_t>(sn, dn - a), delta);
   grk_v8dwt_encode_step1(tmp + b * NB_ELTS_V8, (uint32_t)dn, grk_K);
   grk_v8dwt_encode_step1(tmp + a * NB_ELTS_V8, (uint32_t)sn, grk_invK);

   if(cols == NB_ELTS_V8)
	  deinterleave_v_cols(tmp, array, dn, sn, stride_width, even ? 0 : 1, NB_ELTS_V8);
   else
	  deinterleave_v_cols(tmp, array, dn, sn, stride_width, even ? 0 : 1, cols);
}

/** Process one line for the horizontal pass of the 9x7 forward transform */
void dwt97::encode_and_deinterleave_h_one_row(float* rowIn, float* tmpIn, uint32_t width, bool even)
{
   float* GRK_RESTRICT row = (float*)rowIn;
   float* GRK_RESTRICT tmp = (float*)tmpIn;
   const int32_t sn = (int32_t)((width + (even ? 1 : 0)) >> 1);
   const int32_t dn = (int32_t)(width - (uint32_t)sn);
   if(width == 1)
	  return;
   memcpy(tmp, row, width * sizeof(float));
   encode_1_real(tmp, dn, sn, even ? 0 : 1);
   deinterleave_h(tmp, row, dn, sn, even ? 0 : 1);
}

} // namespace grk
