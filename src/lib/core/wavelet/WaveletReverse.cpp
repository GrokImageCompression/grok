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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"
#include <algorithm>
#include <limits>
#include <sstream>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "wavelet/WaveletReverse.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>
HWY_BEFORE_NAMESPACE();
namespace grk
{
namespace HWY_NAMESPACE
{
   using namespace hwy::HWY_NAMESPACE;

   static size_t hwy_num_lanes(void)
   {
	  const HWY_FULL(int32_t) di;
	  return Lanes(di);
   }

#define HWY_PLL_COLS_53 (2 * Lanes(di))

   static void hwy_decompress_v_final_memcpy_53(const int32_t* buf, const uint32_t height,
												int32_t* dest, const size_t strideDest)
   {
	  const HWY_FULL(int32_t) di;
	  for(uint32_t i = 0; i < height; ++i)
	  {
		 StoreU(Load(di, buf + HWY_PLL_COLS_53 * i + 0), di, &dest[(size_t)i * strideDest + 0]);
		 StoreU(Load(di, buf + HWY_PLL_COLS_53 * i + Lanes(di)), di,
				dest + (size_t)i * strideDest + Lanes(di));
	  }
   }
   /** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
	* 16 in AVX2, when top-most pixel is on even coordinate */
   static void hwy_decompress_v_parity_even_mcols_53(int32_t* buf, int32_t* bandL, /* even */
													 const uint32_t hL, const size_t strideL,
													 int32_t* bandH, /* odd */
													 const uint32_t hH, const size_t strideH,
													 int32_t* dest, const uint32_t strideDest)
   {
	  const HWY_FULL(int32_t) di;
	  auto two = Set(di, 2);

	  const uint32_t total_height = hL + hH;
	  assert(total_height > 1);

	  /* Note: loads of input even/odd values must be done in a unaligned */
	  /* fashion. But stores in tmp can be done with aligned store, since */
	  /* the temporary buffer is properly aligned */
	  assert((size_t)buf % (sizeof(int32_t) * Lanes(di)) == 0);

	  auto s1n_0 = LoadU(di, bandL + 0);
	  auto s1n_1 = LoadU(di, bandL + Lanes(di));
	  auto d1n_0 = LoadU(di, bandH);
	  auto d1n_1 = LoadU(di, bandH + Lanes(di));

	  /* s0n = s1n - ((d1n + 1) >> 1); <==> */
	  /* s0n = s1n - ((d1n + d1n + 2) >> 2); */
	  auto s0n_0 = s1n_0 - ShiftRight<2>(d1n_0 + d1n_0 + two);
	  auto s0n_1 = s1n_1 - ShiftRight<2>(d1n_1 + d1n_1 + two);

	  uint32_t i = 0;
	  if(total_height > 3)
	  {
		 uint32_t j;
		 for(i = 0, j = 1; i < (total_height - 3); i += 2, j++)
		 {
			auto d1c_0 = d1n_0;
			auto s0c_0 = s0n_0;
			auto d1c_1 = d1n_1;
			auto s0c_1 = s0n_1;

			s1n_0 = LoadU(di, bandL + j * strideL);
			s1n_1 = LoadU(di, bandL + j * strideL + Lanes(di));
			d1n_0 = LoadU(di, bandH + j * strideH);
			d1n_1 = LoadU(di, bandH + j * strideH + Lanes(di));

			/*s0n = s1n - ((d1c + d1n + 2) >> 2);*/
			s0n_0 = s1n_0 - ShiftRight<2>(d1c_0 + d1n_0 + two);
			s0n_1 = s1n_1 - ShiftRight<2>(d1c_1 + d1n_1 + two);

			Store(s0c_0, di, buf + HWY_PLL_COLS_53 * (i + 0));
			Store(s0c_1, di, buf + HWY_PLL_COLS_53 * (i + 0) + Lanes(di));

			/* d1c + ((s0c + s0n) >> 1) */
			Store(d1c_0 + ShiftRight<1>(s0c_0 + s0n_0), di, buf + HWY_PLL_COLS_53 * (i + 1) + 0);
			Store(d1c_1 + ShiftRight<1>(s0c_1 + s0n_1), di,
				  buf + HWY_PLL_COLS_53 * (i + 1) + Lanes(di));
		 }
	  }
	  Store(s0n_0, di, buf + HWY_PLL_COLS_53 * (i + 0) + 0);
	  Store(s0n_1, di, buf + HWY_PLL_COLS_53 * (i + 0) + Lanes(di));

	  if(total_height & 1)
	  {
		 s1n_0 = LoadU(di, bandL + (size_t)((total_height - 1) / 2) * strideL);
		 /* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
		 auto tmp_len_minus_1 = s1n_0 - ShiftRight<2>(d1n_0 + d1n_0 + two);
		 Store(tmp_len_minus_1, di, buf + HWY_PLL_COLS_53 * (total_height - 1));
		 /* d1n + ((s0n + tmp_len_minus_1) >> 1) */
		 Store(d1n_0 + ShiftRight<1>(s0n_0 + tmp_len_minus_1), di,
			   buf + HWY_PLL_COLS_53 * (total_height - 2));

		 s1n_1 = LoadU(di, bandL + (size_t)((total_height - 1) / 2) * strideL + Lanes(di));
		 /* tmp_len_minus_1 = s1n - ((d1n + 1) >> 1); */
		 tmp_len_minus_1 = s1n_1 - ShiftRight<2>(d1n_1 + d1n_1 + two);
		 Store(tmp_len_minus_1, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + Lanes(di));
		 /* d1n + ((s0n + tmp_len_minus_1) >> 1) */
		 Store(d1n_1 + ShiftRight<1>(s0n_1 + tmp_len_minus_1), di,
			   buf + HWY_PLL_COLS_53 * (total_height - 2) + Lanes(di));
	  }
	  else
	  {
		 Store(d1n_0 + s0n_0, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + 0);
		 Store(d1n_1 + s0n_1, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + Lanes(di));
	  }
	  hwy_decompress_v_final_memcpy_53(buf, total_height, dest, strideDest);
   }

   /** Vertical inverse 5x3 wavelet transform for 8 columns in SSE2, or
	* 16 in AVX2, when top-most pixel is on odd coordinate */
   static void hwy_decompress_v_parity_odd_mcols_53(int32_t* buf, int32_t* bandL, const uint32_t hL,
													const uint32_t strideL, int32_t* bandH,
													const uint32_t hH, const uint32_t strideH,
													int32_t* dest, const uint32_t strideDest)
   {
	  const HWY_FULL(int32_t) di;
	  auto two = Set(di, 2);

	  const uint32_t total_height = hL + hH;
	  assert(total_height > 2);
	  /* Note: loads of input even/odd values must be done in a unaligned */
	  /* fashion. But stores in buf can be done with aligned store, since */
	  /* the temporary buffer is properly aligned */
	  assert((size_t)buf % (sizeof(int32_t) * Lanes(di)) == 0);

	  const int32_t* in_even = bandH;
	  const int32_t* in_odd = bandL;
	  auto s1_0 = LoadU(di, in_even + strideH);

	  /* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
	  auto dc_0 = LoadU(di, in_odd + 0) - ShiftRight<2>(LoadU(di, in_even + 0) + s1_0 + two);
	  Store(LoadU(di, in_even + 0) + dc_0, di, buf + HWY_PLL_COLS_53 * 0);
	  auto s1_1 = LoadU(di, in_even + strideH + Lanes(di));

	  /* in_odd[0] - ((in_even[0] + s1 + 2) >> 2); */
	  auto dc_1 = LoadU(di, in_odd + Lanes(di)) -
				  ShiftRight<2>(LoadU(di, in_even + Lanes(di)) + s1_1 + two);
	  Store(LoadU(di, in_even + Lanes(di)) + dc_1, di, buf + HWY_PLL_COLS_53 * 0 + Lanes(di));

	  uint32_t i;
	  size_t j;
	  for(i = 1, j = 1; i < (total_height - 2 - !(total_height & 1)); i += 2, j++)
	  {
		 auto s2_0 = LoadU(di, in_even + (j + 1) * strideH);
		 auto s2_1 = LoadU(di, in_even + (j + 1) * strideH + Lanes(di));

		 /* dn = in_odd[j * stride] - ((s1 + s2 + 2) >> 2); */
		 auto dn_0 = LoadU(di, in_odd + j * strideL) - ShiftRight<2>(s1_0 + s2_0 + two);
		 auto dn_1 = LoadU(di, in_odd + j * strideL + Lanes(di)) - ShiftRight<2>(s1_1 + s2_1 + two);

		 Store(dc_0, di, buf + HWY_PLL_COLS_53 * i);
		 Store(dc_1, di, buf + HWY_PLL_COLS_53 * i + Lanes(di));

		 /* buf[i + 1] = s1 + ((dn + dc) >> 1); */
		 Store(s1_0 + ShiftRight<1>(dn_0 + dc_0), di, buf + HWY_PLL_COLS_53 * (i + 1) + 0);
		 Store(s1_1 + ShiftRight<1>(dn_1 + dc_1), di, buf + HWY_PLL_COLS_53 * (i + 1) + Lanes(di));

		 dc_0 = dn_0;
		 s1_0 = s2_0;
		 dc_1 = dn_1;
		 s1_1 = s2_1;
	  }
	  Store(dc_0, di, buf + HWY_PLL_COLS_53 * i);
	  Store(dc_1, di, buf + HWY_PLL_COLS_53 * i + Lanes(di));

	  if(!(total_height & 1))
	  {
		 /*dn = in_odd[(len / 2 - 1) * stride] - ((s1 + 1) >> 1); */
		 auto dn_0 = LoadU(di, in_odd + (size_t)(total_height / 2 - 1) * strideL) -
					 ShiftRight<2>(s1_0 + s1_0 + two);
		 auto dn_1 = LoadU(di, in_odd + (size_t)(total_height / 2 - 1) * strideL + Lanes(di)) -
					 ShiftRight<2>(s1_1 + s1_1 + two);

		 /* buf[len - 2] = s1 + ((dn + dc) >> 1); */
		 Store(s1_0 + ShiftRight<1>(dn_0 + dc_0), di,
			   buf + HWY_PLL_COLS_53 * (total_height - 2) + 0);
		 Store(s1_1 + ShiftRight<1>(dn_1 + dc_1), di,
			   buf + HWY_PLL_COLS_53 * (total_height - 2) + Lanes(di));

		 Store(dn_0, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + 0);
		 Store(dn_1, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + Lanes(di));
	  }
	  else
	  {
		 Store(s1_0 + dc_0, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + 0);
		 Store(s1_1 + dc_1, di, buf + HWY_PLL_COLS_53 * (total_height - 1) + Lanes(di));
	  }
	  hwy_decompress_v_final_memcpy_53(buf, total_height, dest, strideDest);
   }

} // namespace HWY_NAMESPACE
} // namespace grk
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace grk
{
HWY_EXPORT(hwy_num_lanes);
HWY_EXPORT(hwy_decompress_v_parity_even_mcols_53);
HWY_EXPORT(hwy_decompress_v_parity_odd_mcols_53);
/* <summary>                             */
/* Determine maximum computed resolution level for inverse wavelet transform */
/* </summary>                            */
uint32_t max_resolution(Resolution* GRK_RESTRICT r, uint32_t i)
{
   uint32_t mr = 0;
   while(--i)
   {
	  ++r;
	  uint32_t w;
	  if(mr < (w = r->x1 - r->x0))
		 mr = w;
	  if(mr < (w = r->y1 - r->y0))
		 mr = w;
   }
   return mr;
}

/**********************************************************************************
 *
 * Full 9/7 Inverse Wavelet
 *
 *
 *
 **********************************************************************************/

static const float dwt_alpha = 1.586134342f; /*  12994 */
static const float dwt_beta = 0.052980118f; /*    434 */
static const float dwt_gamma = -0.882911075f; /*  -7233 */
static const float dwt_delta = -0.443506852f; /*  -3633 */
static const float K = 1.230174105f; /*  10078 */
static const float twice_invK = 1.625732422f;

// #undef __SSE__

#ifdef __SSE__
void WaveletReverse::decompress_step1_sse_97(Params97 d, const __m128 c)
{
   // process 4 floats at a time
   auto mmData = (__m128*)d.data;
   uint32_t i;
   for(i = 0; i + 3 < d.len; i += 4, mmData += 8)
   {
	  mmData[0] = _mm_mul_ps(mmData[0], c);
	  mmData[2] = _mm_mul_ps(mmData[2], c);
	  mmData[4] = _mm_mul_ps(mmData[4], c);
	  mmData[6] = _mm_mul_ps(mmData[6], c);
   }
   for(; i < d.len; ++i, mmData += 2)
	  mmData[0] = _mm_mul_ps(mmData[0], c);
}
#endif

void WaveletReverse::decompress_step1_97(const Params97& d, const float c)
{
#ifdef __SSE__
   decompress_step1_sse_97(d, _mm_set1_ps(c));
#else
   float* GRK_RESTRICT fw = (float*)d.data;

   for(uint32_t i = 0; i < d.len; ++i, fw += 8)
   {
	  fw[0] *= c;
	  fw[1] *= c;
	  fw[2] *= c;
	  fw[3] *= c;
	  ;
   }
#endif
}

#ifdef __SSE__
static void decompress_step2_sse_97(const Params97& d, __m128 c)
{
   __m128* GRK_RESTRICT vec_data = (__m128*)d.data;

   uint32_t imax = (std::min<uint32_t>)(d.len, d.lenMax);

   // initial tmp1 value is only necessary when
   // absolute start of line is at 0
   auto tmp1 = ((__m128*)d.dataPrev)[0];
   uint32_t i = 0;
   for(; i + 3 < imax; i += 4)
   {
	  auto tmp2 = vec_data[-1];
	  auto tmp3 = vec_data[0];
	  auto tmp4 = vec_data[1];
	  auto tmp5 = vec_data[2];
	  auto tmp6 = vec_data[3];
	  auto tmp7 = vec_data[4];
	  auto tmp8 = vec_data[5];
	  auto tmp9 = vec_data[6];
	  vec_data[-1] = _mm_add_ps(tmp2, _mm_mul_ps(_mm_add_ps(tmp1, tmp3), c));
	  vec_data[1] = _mm_add_ps(tmp4, _mm_mul_ps(_mm_add_ps(tmp3, tmp5), c));
	  vec_data[3] = _mm_add_ps(tmp6, _mm_mul_ps(_mm_add_ps(tmp5, tmp7), c));
	  vec_data[5] = _mm_add_ps(tmp8, _mm_mul_ps(_mm_add_ps(tmp7, tmp9), c));
	  tmp1 = tmp9;
	  vec_data += 8;
   }

   for(; i < imax; ++i)
   {
	  auto tmp2 = vec_data[-1];
	  auto tmp3 = vec_data[0];
	  vec_data[-1] = _mm_add_ps(tmp2, _mm_mul_ps(_mm_add_ps(tmp1, tmp3), c));
	  tmp1 = tmp3;
	  vec_data += 2;
   }
   if(d.lenMax < d.len)
   {
	  assert(d.lenMax + 1 == d.len);
	  c = _mm_add_ps(c, c);
	  c = _mm_mul_ps(c, vec_data[-2]);
	  vec_data[-1] = _mm_add_ps(vec_data[-1], c);
   }
}
#endif

static void decompress_step2_97(const Params97& d, float c)
{
#ifdef __SSE__
   decompress_step2_sse_97(d, _mm_set1_ps(c));
#else

   float* dataPrev = (float*)d.dataPrev;
   float* data = (float*)d.data;

   uint32_t imax = (std::min<uint32_t>)(d.len, d.lenMax);
   for(uint32_t i = 0; i < imax; ++i)
   {
	  float tmp1_1 = dataPrev[0];
	  float tmp1_2 = dataPrev[1];
	  float tmp1_3 = dataPrev[2];
	  float tmp1_4 = dataPrev[3];
	  float tmp2_1 = data[-4];
	  float tmp2_2 = data[-3];
	  float tmp2_3 = data[-2];
	  float tmp2_4 = data[-1];
	  float tmp3_1 = data[0];
	  float tmp3_2 = data[1];
	  float tmp3_3 = data[2];
	  float tmp3_4 = data[3];
	  data[-4] = tmp2_1 + ((tmp1_1 + tmp3_1) * c);
	  data[-3] = tmp2_2 + ((tmp1_2 + tmp3_2) * c);
	  data[-2] = tmp2_3 + ((tmp1_3 + tmp3_3) * c);
	  data[-1] = tmp2_4 + ((tmp1_4 + tmp3_4) * c);
	  dataPrev = data;
	  data += 8;
   }
   if(d.lenMax < d.len)
   {
	  assert(d.lenMax + 1 == d.len);
	  c += c;
	  data[-4] = data[-4] + dataPrev[0] * c;
	  data[-3] = data[-3] + dataPrev[1] * c;
	  data[-2] = data[-2] + dataPrev[2] * c;
	  data[-1] = data[-1] + dataPrev[3] * c;
   }
#endif
}
/* <summary>                             */
/* Inverse 9-7 wavelet transform in 1-D. */
/* </summary>                            */
void WaveletReverse::decompress_step_97(dwt_data<vec4f>* GRK_RESTRICT dwt)
{
   if((!dwt->parity && dwt->dn_full == 0 && dwt->sn_full <= 1) ||
	  (dwt->parity && dwt->sn_full == 0 && dwt->dn_full >= 1))
	  return;

   decompress_step1_97(makeParams97(dwt, true, true), K);
   decompress_step1_97(makeParams97(dwt, false, true), twice_invK);
   decompress_step2_97(makeParams97(dwt, true, false), dwt_delta);
   decompress_step2_97(makeParams97(dwt, false, false), dwt_gamma);
   decompress_step2_97(makeParams97(dwt, true, false), dwt_beta);
   decompress_step2_97(makeParams97(dwt, false, false), dwt_alpha);
}
void WaveletReverse::interleave_h_97(dwt_data<vec4f>* GRK_RESTRICT dwt,
									 grk_buf2d_simple<float> winL, grk_buf2d_simple<float> winH,
									 uint32_t remaining_height)
{
   float* GRK_RESTRICT bi = (float*)(dwt->mem + dwt->parity);
   uint32_t x0 = dwt->win_l.x0;
   uint32_t x1 = dwt->win_l.x1;
   const size_t vec4f_elts = vec4f::NUM_ELTS;
   for(uint32_t k = 0; k < 2; ++k)
   {
	  auto band = (k == 0) ? winL.buf_ : winH.buf_;
	  uint32_t stride = (k == 0) ? winL.stride_ : winH.stride_;
	  if(remaining_height >= vec4f_elts && ((size_t)band & 0x0f) == 0 && ((size_t)bi & 0x0f) == 0 &&
		 (stride & 0x0f) == 0)
	  {
		 /* Fast code path */
		 for(uint32_t i = x0; i < x1; ++i, bi += vec4f_elts * 2)
		 {
			uint32_t j = i;
			bi[0] = band[j];
			j += stride;
			bi[1] = band[j];
			j += stride;
			bi[2] = band[j];
			j += stride;
			bi[3] = band[j];
		 }
	  }
	  else
	  {
		 /* Slow code path */
		 for(uint32_t i = x0; i < x1; ++i, bi += vec4f_elts * 2)
		 {
			uint32_t j = i;
			bi[0] = band[j];
			j += stride;
			if(remaining_height == 1)
			   continue;
			bi[1] = band[j];
			j += stride;
			if(remaining_height == 2)
			   continue;
			bi[2] = band[j];
			j += stride;
			if(remaining_height == 3)
			   continue;
			bi[3] = band[j];
		 }
	  }
	  bi = (float*)(dwt->mem + 1 - dwt->parity);
	  x0 = dwt->win_h.x0;
	  x1 = dwt->win_h.x1;
   }
}
void WaveletReverse::decompress_h_strip_97(dwt_data<vec4f>* GRK_RESTRICT horiz,
										   const uint32_t resHeight, grk_buf2d_simple<float> winL,
										   grk_buf2d_simple<float> winH,
										   grk_buf2d_simple<float> winDest)
{
   float* GRK_RESTRICT dest = winDest.buf_;
   const uint32_t strideDest = winDest.stride_;
   uint32_t j;
   const size_t vec4f_elts = vec4f::NUM_ELTS;
   for(j = 0; j < (resHeight & (uint32_t)(~(vec4f_elts - 1))); j += vec4f_elts)
   {
	  interleave_h_97(horiz, winL, winH, resHeight - j);
	  decompress_step_97(horiz);
	  for(uint32_t k = 0; k < horiz->sn_full + horiz->dn_full; k++)
	  {
		 dest[k] = horiz->mem[k].val[0];
		 dest[k + (size_t)strideDest] = horiz->mem[k].val[1];
		 dest[k + (size_t)strideDest * 2] = horiz->mem[k].val[2];
		 dest[k + (size_t)strideDest * 3] = horiz->mem[k].val[3];
	  }
	  winL.buf_ += winL.stride_ << 2;
	  winH.buf_ += winH.stride_ << 2;
	  dest += strideDest << 2;
   }
   if(j < resHeight)
   {
	  interleave_h_97(horiz, winL, winH, resHeight - j);
	  decompress_step_97(horiz);
	  for(uint32_t k = 0; k < horiz->sn_full + horiz->dn_full; k++)
	  {
		 switch(resHeight - j)
		 {
			case 3:
			   dest[k + (strideDest << 1)] = horiz->mem[k].val[2];
			/* FALLTHRU */
			case 2:
			   dest[k + strideDest] = horiz->mem[k].val[1];
			/* FALLTHRU */
			case 1:
			   dest[k] = horiz->mem[k].val[0];
		 }
	  }
   }
}
bool WaveletReverse::decompress_h_97(uint8_t res, uint32_t numThreads, size_t dataLength,
									 dwt_data<vec4f>& GRK_RESTRICT horiz, const uint32_t resHeight,
									 grk_buf2d_simple<float> winL, grk_buf2d_simple<float> winH,
									 grk_buf2d_simple<float> winDest)
{
   if(resHeight == 0)
	  return true;
   if(numThreads == 1)
   {
	  decompress_h_strip_97(&horiz, resHeight, winL, winH, winDest);
   }
   else
   {
	  uint32_t numTasks = numThreads;
	  if(resHeight < numTasks)
		 numTasks = resHeight;
	  uint32_t incrPerJob = resHeight / numTasks;
	  auto imageComponentFlow = scheduler_->getImageComponentFlow(compno_);
	  if(!imageComponentFlow)
	  {
		 Logger::logger_.warn("Missing image component flow");
		 return true;
	  }
	  auto resFlow = imageComponentFlow->getResFlow(res - 1);
	  for(uint32_t j = 0; j < numTasks; ++j)
	  {
		 auto indexMin = j * incrPerJob;
		 auto indexMax = (j < (numTasks - 1U) ? (j + 1U) * incrPerJob : resHeight) - indexMin;
		 auto myhoriz = new dwt_data<vec4f>(horiz);
		 if(!myhoriz->alloc(dataLength))
		 {
			Logger::logger_.error("Out of memory");
			return false;
		 }
		 resFlow->waveletHoriz_->nextTask().work([this, myhoriz, indexMax, winL, winH, winDest] {
			decompress_h_strip_97(myhoriz, indexMax, winL, winH, winDest);
			delete myhoriz;
		 });
		 winL.incY_IN_PLACE(incrPerJob);
		 winH.incY_IN_PLACE(incrPerJob);
		 winDest.incY_IN_PLACE(incrPerJob);
	  }
   }
   return true;
}
void WaveletReverse::interleave_v_97(dwt_data<vec4f>* GRK_RESTRICT dwt,
									 grk_buf2d_simple<float> winL, grk_buf2d_simple<float> winH,
									 uint32_t nb_elts_read)
{
   auto bi = dwt->mem + dwt->parity;
   auto band = winL.buf_ + dwt->win_l.x0 * winL.stride_;
   for(uint32_t i = dwt->win_l.x0; i < dwt->win_l.x1; ++i, bi += 2)
   {
	  memcpy((float*)bi, band, nb_elts_read * sizeof(float));
	  band += winL.stride_;
   }
   bi = dwt->mem + 1 - dwt->parity;
   band = winH.buf_ + dwt->win_h.x0 * winH.stride_;
   for(uint32_t i = dwt->win_h.x0; i < dwt->win_h.x1; ++i, bi += 2)
   {
	  memcpy((float*)bi, band, nb_elts_read * sizeof(float));
	  band += winH.stride_;
   }
}
void WaveletReverse::decompress_v_strip_97(dwt_data<vec4f>* GRK_RESTRICT vert,
										   const uint32_t resWidth, const uint32_t resHeight,
										   grk_buf2d_simple<float> winL,
										   grk_buf2d_simple<float> winH,
										   grk_buf2d_simple<float> winDest)
{
   uint32_t j;
   const size_t vec4f_elts = vec4f::NUM_ELTS;
   for(j = 0; j < (resWidth & (uint32_t) ~(vec4f_elts - 1)); j += vec4f_elts)
   {
	  interleave_v_97(vert, winL, winH, vec4f_elts);
	  decompress_step_97(vert);
	  auto destPtr = winDest.buf_;
	  for(uint32_t k = 0; k < resHeight; ++k)
	  {
		 memcpy(destPtr, vert->mem + k, sizeof(vec4f));
		 destPtr += winDest.stride_;
	  }
	  winL.buf_ += vec4f_elts;
	  winH.buf_ += vec4f_elts;
	  winDest.buf_ += vec4f_elts;
   }
   if(j < resWidth)
   {
	  j = resWidth & (vec4f_elts - 1);
	  interleave_v_97(vert, winL, winH, j);
	  decompress_step_97(vert);
	  auto destPtr = winDest.buf_;
	  for(uint32_t k = 0; k < resHeight; ++k)
	  {
		 memcpy(destPtr, vert->mem + k, j * sizeof(float));
		 destPtr += winDest.stride_;
	  }
   }
}
bool WaveletReverse::decompress_v_97(uint8_t res, uint32_t numThreads, size_t dataLength,
									 dwt_data<vec4f>& GRK_RESTRICT vert, const uint32_t resWidth,
									 const uint32_t resHeight, grk_buf2d_simple<float> winL,
									 grk_buf2d_simple<float> winH, grk_buf2d_simple<float> winDest)
{
   if(resWidth == 0)
	  return true;
   if(numThreads == 1)
   {
	  decompress_v_strip_97(&vert, resWidth, resHeight, winL, winH, winDest);
   }
   else
   {
	  auto numTasks = numThreads;
	  if(resWidth < numTasks)
		 numTasks = resWidth;
	  auto incrPerJob = resWidth / numTasks;
	  auto imageComponentFlow = scheduler_->getImageComponentFlow(compno_);
	  if(!imageComponentFlow)
	  {
		 Logger::logger_.warn("Missing image component flow");
		 return false;
	  }
	  auto resFlow = imageComponentFlow->getResFlow(res - 1);
	  for(uint32_t j = 0; j < numTasks; j++)
	  {
		 auto indexMin = j * incrPerJob;
		 auto indexMax = (j < (numTasks - 1U) ? (j + 1U) * incrPerJob : resWidth) - indexMin;
		 auto myvert = new dwt_data<vec4f>(vert);
		 if(!myvert->alloc(dataLength))
		 {
			Logger::logger_.error("Out of memory");
			delete myvert;
			return false;
		 }
		 resFlow->waveletVert_->nextTask().work(
			 [this, myvert, resHeight, indexMax, winL, winH, winDest] {
				decompress_v_strip_97(myvert, indexMax, resHeight, winL, winH, winDest);
				delete myvert;
			 });
		 winL.incX_IN_PLACE(incrPerJob);
		 winH.incX_IN_PLACE(incrPerJob);
		 winDest.incX_IN_PLACE(incrPerJob);
	  }
   }

   return true;
}
/* <summary>                             */
/* Inverse 9-7 wavelet transform in 2-D. */
/* </summary>                            */
bool WaveletReverse::decompress_tile_97(void)
{
   if(numres_ == 1U)
	  return true;

   auto tr = tilec_->resolutions_;
   auto buf = tilec_->getWindow();
   uint32_t resWidth = tr->width();
   uint32_t resHeight = tr->height();

   size_t dataLength = max_resolution(tr, numres_);
   if(!horizF_.alloc(dataLength))
   {
	  Logger::logger_.error("decompress_tile_97: out of memory");
	  return false;
   }
   vertF_.mem = horizF_.mem;
   uint32_t numThreads = (uint32_t)ExecSingleton::get().num_workers();
   for(uint8_t res = 1; res < numres_; ++res)
   {
	  horizF_.sn_full = resWidth;
	  vertF_.sn_full = resHeight;
	  ++tr;
	  resWidth = tr->width();
	  resHeight = tr->height();
	  if(resWidth == 0 || resHeight == 0)
		 continue;
	  horizF_.dn_full = resWidth - horizF_.sn_full;
	  horizF_.parity = tr->x0 & 1;
	  horizF_.win_l = grk_line32(0, horizF_.sn_full);
	  horizF_.win_h = grk_line32(0, horizF_.dn_full);
	  auto winSplitL = buf->getResWindowBufferSplitSimpleF(res, SPLIT_L);
	  auto winSplitH = buf->getResWindowBufferSplitSimpleF(res, SPLIT_H);
	  if(!decompress_h_97(res, numThreads, dataLength, horizF_, vertF_.sn_full,
						  buf->getResWindowBufferSimpleF(res - 1U),
						  buf->getBandWindowBufferPaddedSimpleF(res, BAND_ORIENT_HL), winSplitL))
		 return false;
	  if(!decompress_h_97(res, numThreads, dataLength, horizF_, resHeight - vertF_.sn_full,
						  buf->getBandWindowBufferPaddedSimpleF(res, BAND_ORIENT_LH),
						  buf->getBandWindowBufferPaddedSimpleF(res, BAND_ORIENT_HH), winSplitH))
		 return false;
	  vertF_.dn_full = resHeight - vertF_.sn_full;
	  vertF_.parity = tr->y0 & 1;
	  vertF_.win_l = grk_line32(0, vertF_.sn_full);
	  vertF_.win_h = grk_line32(0, vertF_.dn_full);
	  if(!decompress_v_97(res, numThreads, dataLength, vertF_, resWidth, resHeight, winSplitL,
						  winSplitH, buf->getResWindowBufferSimpleF(res)))
		 return false;
   }

   return true;
}

/**************************************************************************************
 *
 * Full 5/3 Inverse Wavelet
 *
 *
 *************************************************************************************/

void WaveletReverse::decompress_h_parity_even_53(int32_t* buf, int32_t* bandL, /* even */
												 const uint32_t wL, int32_t* bandH,
												 const uint32_t wH, int32_t* dest)
{ /* odd */
   const uint32_t total_width = wL + wH;
   assert(total_width > 1);

   /* Improved version of the TWO_PASS_VERSION: */
   /* Performs lifting in one single iteration. Saves memory */
   /* accesses and explicit interleaving. */
   int32_t s1n = bandL[0];
   int32_t d1n = bandH[0];
   int32_t s0n = s1n - ((d1n + 1) >> 1);
   uint32_t i = 0;
   if(total_width > 2)
   {
	  for(uint32_t j = 1; i < (total_width - 3); i += 2, j++)
	  {
		 int32_t d1c = d1n;
		 int32_t s0c = s0n;

		 s1n = bandL[j];
		 d1n = bandH[j];
		 s0n = s1n - ((d1c + d1n + 2) >> 2);
		 buf[i] = s0c;
		 buf[i + 1] = d1c + ((s0c + s0n) >> 1);
	  }
   }
   buf[i] = s0n;
   if(total_width & 1)
   {
	  buf[total_width - 1] = bandL[(total_width - 1) >> 1] - ((d1n + 1) >> 1);
	  buf[total_width - 2] = d1n + ((s0n + buf[total_width - 1]) >> 1);
   }
   else
   {
	  buf[total_width - 1] = d1n + s0n;
   }
   memcpy(dest, buf, total_width * sizeof(int32_t));
}

void WaveletReverse::decompress_h_parity_odd_53(int32_t* buf, int32_t* bandL, /* odd */
												const uint32_t wL, int32_t* bandH,
												const uint32_t wH, int32_t* dest)
{ /* even */
   const uint32_t total_width = wL + wH;
   assert(total_width > 2);

   /* Improved version of the TWO_PASS_VERSION:
	  Performs lifting in one single iteration. Saves memory
	  accesses and explicit interleaving. */
   int32_t s1 = bandH[1];
   int32_t dc = bandL[0] - ((bandH[0] + s1 + 2) >> 2);
   buf[0] = bandH[0] + dc;
   uint32_t i, j;
   for(i = 1, j = 1; i < (total_width - 2 - !(total_width & 1)); i += 2, j++)
   {
	  int32_t s2 = bandH[j + 1];
	  int32_t dn = bandL[j] - ((s1 + s2 + 2) >> 2);

	  buf[i] = dc;
	  buf[i + 1] = s1 + ((dn + dc) >> 1);
	  dc = dn;
	  s1 = s2;
   }
   buf[i] = dc;
   if(!(total_width & 1))
   {
	  int32_t dn = bandL[(total_width >> 1) - 1] - ((s1 + 1) >> 1);
	  buf[total_width - 2] = s1 + ((dn + dc) >> 1);
	  buf[total_width - 1] = dn;
   }
   else
   {
	  buf[total_width - 1] = s1 + dc;
   }
   memcpy(dest, buf, total_width * sizeof(int32_t));
}

/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on even coordinate */
void WaveletReverse::decompress_v_parity_even_53(int32_t* buf, int32_t* bandL, const uint32_t hL,
												 const uint32_t strideL, int32_t* bandH,
												 const uint32_t hH, const uint32_t strideH,
												 int32_t* dest, const uint32_t strideDest)
{
   const uint32_t total_height = hL + hH;
   assert(total_height > 1);

   /* Performs lifting in one single iteration. Saves memory */
   /* accesses and explicit interleaving. */
   int32_t s1n = bandL[0];
   int32_t d1n = bandH[0];
   int32_t s0n = s1n - ((d1n + 1) >> 1);

   uint32_t i = 0;
   if(total_height > 2)
   {
	  auto bL = bandL + strideL;
	  auto bH = bandH + strideH;
	  for(uint32_t j = 0; i < (total_height - 3); i += 2, j++)
	  {
		 int32_t d1c = d1n;
		 int32_t s0c = s0n;
		 s1n = *bL;
		 bL += strideL;
		 d1n = *bH;
		 bH += strideH;
		 s0n = s1n - ((d1c + d1n + 2) >> 2);
		 buf[i] = s0c;
		 buf[i + 1] = d1c + ((s0c + s0n) >> 1);
	  }
   }
   buf[i] = s0n;
   if(total_height & 1)
   {
	  buf[total_height - 1] = bandL[((total_height - 1) >> 1) * strideL] - ((d1n + 1) >> 1);
	  buf[total_height - 2] = d1n + ((s0n + buf[total_height - 1]) >> 1);
   }
   else
   {
	  buf[total_height - 1] = d1n + s0n;
   }
   for(i = 0; i < total_height; ++i)
   {
	  *dest = buf[i];
	  dest += strideDest;
   }
}
/** Vertical inverse 5x3 wavelet transform for one column, when top-most
 * pixel is on odd coordinate */
void WaveletReverse::decompress_v_parity_odd_53(int32_t* buf, int32_t* bandL, const uint32_t hL,
												const uint32_t strideL, int32_t* bandH,
												const uint32_t hH, const uint32_t strideH,
												int32_t* dest, const uint32_t strideDest)
{
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
   for(i = 1, j = 1; i < (total_height - 2 - !(total_height & 1)); i += 2, j++)
   {
	  int32_t s2 = *s2_ptr;
	  s2_ptr += strideH;

	  int32_t dn = *dn_ptr - ((s1 + s2 + 2) >> 2);
	  dn_ptr += strideL;

	  buf[i] = dc;
	  buf[i + 1] = s1 + ((dn + dc) >> 1);
	  dc = dn;
	  s1 = s2;
   }
   buf[i] = dc;
   if(!(total_height & 1))
   {
	  int32_t dn = bandL[((total_height >> 1) - 1) * strideL] - ((s1 + 1) >> 1);
	  buf[total_height - 2] = s1 + ((dn + dc) >> 1);
	  buf[total_height - 1] = dn;
   }
   else
   {
	  buf[total_height - 1] = s1 + dc;
   }
   for(i = 0; i < total_height; ++i)
   {
	  *dest = buf[i];
	  dest += strideDest;
   }
}
/* <summary>                            */
/* Inverse 5-3 wavelet transform in 1-D for one row. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
void WaveletReverse::decompress_h_53(const dwt_data<int32_t>* dwt, int32_t* bandL, int32_t* bandH,
									 int32_t* dest)
{
   const uint32_t total_width = dwt->sn_full + dwt->dn_full;
   assert(total_width != 0);
   if(dwt->parity == 0)
   { /* Left-most sample is on even coordinate */
	  if(total_width > 1)
	  {
		 decompress_h_parity_even_53(dwt->mem, bandL, dwt->sn_full, bandH, dwt->dn_full, dest);
	  }
	  else
	  {
		 assert(dwt->sn_full == 1);
		 // only L op: only one sample in L band and H band is empty
		 dest[0] = bandL[0];
	  }
   }
   else
   { /* Left-most sample is on odd coordinate */
	  if(total_width == 1)
	  {
		 assert(dwt->dn_full == 1);
		 // only H op: only one sample in H band and L band is empty
		 dest[0] = bandH[0] >> 1;
	  }
	  else if(total_width == 2)
	  {
		 dwt->mem[1] = bandL[0] - ((bandH[0] + 1) >> 1);
		 dest[0] = bandH[0] + dwt->mem[1];
		 dest[1] = dwt->mem[1];
	  }
	  else
	  {
		 decompress_h_parity_odd_53(dwt->mem, bandL, dwt->sn_full, bandH, dwt->dn_full, dest);
	  }
   }
}

/* <summary>                            */
/* Inverse vertical 5-3 wavelet transform in 1-D for several columns. */
/* </summary>                           */
/* Performs interleave, inverse wavelet transform and copy back to buffer */
/** Number of columns that we can process in parallel in the vertical pass */
#define PLL_COLS_53 (2 * uint32_t(HWY_DYNAMIC_DISPATCH(hwy_num_lanes)()))
void WaveletReverse::decompress_v_53(const dwt_data<int32_t>* dwt, grk_buf2d_simple<int32_t> winL,
									 grk_buf2d_simple<int32_t> winH,
									 grk_buf2d_simple<int32_t> winDest, uint32_t nb_cols)
{
   const uint32_t total_height = dwt->sn_full + dwt->dn_full;
   assert(total_height != 0);
   if(dwt->parity == 0)
   {
	  if(total_height == 1)
	  {
		 for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winDest.buf_++)
			winDest.buf_[0] = winL.buf_[0];
	  }
	  else
	  {
		 if(nb_cols == PLL_COLS_53)
		 {
			/* Same as below general case, except that thanks to SSE2/AVX2 */
			/* we can efficiently process 8/16 columns in parallel */
			HWY_DYNAMIC_DISPATCH(hwy_decompress_v_parity_even_mcols_53)
			(dwt->mem, winL.buf_, dwt->sn_full, winL.stride_, winH.buf_, dwt->dn_full, winH.stride_,
			 winDest.buf_, winDest.stride_);
		 }
		 else
		 {
			for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
			   decompress_v_parity_even_53(dwt->mem, winL.buf_, dwt->sn_full, winL.stride_,
										   winH.buf_, dwt->dn_full, winL.stride_, winDest.buf_,
										   winDest.stride_);
		 }
	  }
   }
   else
   {
	  if(total_height == 1)
	  {
		 for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winDest.buf_++)
			winDest.buf_[0] = winL.buf_[0] >> 1;
	  }
	  else if(total_height == 2)
	  {
		 auto out = dwt->mem;
		 for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
		 {
			out[1] = winL.buf_[0] - ((winH.buf_[0] + 1) >> 1);
			winDest.buf_[0] = winH.buf_[0] + out[1];
			winDest.buf_[1] = out[1];
		 }
	  }
	  else
	  {
		 if(nb_cols == PLL_COLS_53)
		 {
			/* Same as below general case, except that thanks to SSE2/AVX2 */
			/* we can efficiently process 8/16 columns in parallel */
			HWY_DYNAMIC_DISPATCH(hwy_decompress_v_parity_odd_mcols_53)
			(dwt->mem, winL.buf_, dwt->sn_full, winL.stride_, winH.buf_, dwt->dn_full, winH.stride_,
			 winDest.buf_, winDest.stride_);
		 }
		 else
		 {
			for(uint32_t c = 0; c < nb_cols; c++, winL.buf_++, winH.buf_++, winDest.buf_++)
			   decompress_v_parity_odd_53(dwt->mem, winL.buf_, dwt->sn_full, winL.stride_,
										  winH.buf_, dwt->dn_full, winH.stride_, winDest.buf_,
										  winDest.stride_);
		 }
	  }
   }
}

void WaveletReverse::decompress_h_strip_53(const dwt_data<int32_t>* horiz, uint32_t hMin,
										   uint32_t hMax, grk_buf2d_simple<int32_t> winL,
										   grk_buf2d_simple<int32_t> winH,
										   grk_buf2d_simple<int32_t> winDest)
{
   for(uint32_t j = hMin; j < hMax; ++j)
   {
	  decompress_h_53(horiz, winL.buf_, winH.buf_, winDest.buf_);
	  winL.incY_IN_PLACE(1);
	  winH.incY_IN_PLACE(1);
	  winDest.incY_IN_PLACE(1);
   }
}
bool WaveletReverse::decompress_h_53(uint8_t res, TileComponentWindow<int32_t>* buf,
									 uint32_t resHeight, size_t dataLength)
{
   uint32_t numThreads = (uint32_t)ExecSingleton::get().num_workers();
   grk_buf2d_simple<int32_t> winL, winH, winDest;
   auto imageComponentFlow = scheduler_->getImageComponentFlow(compno_);
   auto resFlow = imageComponentFlow->getResFlow(res - 1);
   uint32_t numTasks[2] = {0, 0};
   uint32_t height[2] = {0, 0};
   for(uint32_t orient = 0; orient < 2; ++orient)
   {
	  height[orient] = (orient == 0) ? vert_.sn_full : resHeight - vert_.sn_full;
	  if(numThreads > 1)
		 numTasks[orient] = height[orient] < numThreads ? height[orient] : numThreads;
   }
   for(uint32_t orient = 0; orient < 2; ++orient)
   {
	  if(height[orient] == 0)
		 continue;
	  if(orient == 0)
	  {
		 winL = buf->getResWindowBufferSimple(res - 1U);
		 winH = buf->getBandWindowBufferPaddedSimple(res, BAND_ORIENT_HL);
		 winDest = buf->getResWindowBufferSplitSimple(res, SPLIT_L);
	  }
	  else
	  {
		 winL = buf->getBandWindowBufferPaddedSimple(res, BAND_ORIENT_LH);
		 winH = buf->getBandWindowBufferPaddedSimple(res, BAND_ORIENT_HH);
		 winDest = buf->getResWindowBufferSplitSimple(res, SPLIT_H);
	  }
	  if(numThreads == 1)
	  {
		 if(!horiz_.mem)
		 {
			if(!horiz_.alloc(dataLength))
			{
			   Logger::logger_.error("Out of memory");
			   return false;
			}
			vert_.mem = horiz_.mem;
		 }
		 decompress_h_strip_53(&horiz_, 0, height[orient], winL, winH, winDest);
	  }
	  else
	  {
		 uint32_t incrPerJob = height[orient] / numTasks[orient];
		 for(uint32_t j = 0; j < numTasks[orient]; ++j)
		 {
			auto indexMin = j * incrPerJob;
			auto indexMax = j < (numTasks[orient] - 1U) ? (j + 1U) * incrPerJob : height[orient];
			auto horiz = new dwt_data<int32_t>(horiz_);
			if(!horiz->alloc(dataLength))
			{
			   Logger::logger_.error("Out of memory");
			   delete horiz;
			   return false;
			}
			resFlow->waveletHoriz_->nextTask().work(
				[this, horiz, winL, winH, winDest, indexMin, indexMax] {
				   decompress_h_strip_53(horiz, indexMin, indexMax, winL, winH, winDest);
				   delete horiz;
				});
			winL.incY_IN_PLACE(incrPerJob);
			winH.incY_IN_PLACE(incrPerJob);
			winDest.incY_IN_PLACE(incrPerJob);
		 }
	  }
   }

   return true;
}

void WaveletReverse::decompress_v_strip_53(const dwt_data<int32_t>* vert, uint32_t wMin,
										   uint32_t wMax, grk_buf2d_simple<int32_t> winL,
										   grk_buf2d_simple<int32_t> winH,
										   grk_buf2d_simple<int32_t> winDest)
{
   uint32_t j;
   for(j = wMin; j + PLL_COLS_53 <= wMax; j += PLL_COLS_53)
   {
	  decompress_v_53(vert, winL, winH, winDest, PLL_COLS_53);
	  winL.incX_IN_PLACE(PLL_COLS_53);
	  winH.incX_IN_PLACE(PLL_COLS_53);
	  winDest.incX_IN_PLACE(PLL_COLS_53);
   }
   if(j < wMax)
	  decompress_v_53(vert, winL, winH, winDest, wMax - j);
}

bool WaveletReverse::decompress_v_53(uint8_t res, TileComponentWindow<int32_t>* buf,
									 uint32_t resWidth, size_t dataLength)
{
   if(resWidth == 0)
	  return true;
   uint32_t numThreads = (uint32_t)ExecSingleton::get().num_workers();
   auto winL = buf->getResWindowBufferSplitSimple(res, SPLIT_L);
   auto winH = buf->getResWindowBufferSplitSimple(res, SPLIT_H);
   auto winDest = buf->getResWindowBufferSimple(res);
   if(numThreads == 1)
   {
	  if(!horiz_.mem)
	  {
		 if(!horiz_.alloc(dataLength))
		 {
			Logger::logger_.error("Out of memory");
			return false;
		 }
		 vert_.mem = horiz_.mem;
	  }
	  decompress_v_strip_53(&vert_, 0, resWidth, winL, winH, winDest);
   }
   else
   {
	  auto imageComponentFlow = scheduler_->getImageComponentFlow(compno_);
	  auto resFlow = imageComponentFlow->getResFlow(res - 1);
	  const uint32_t numTasks = resWidth < numThreads ? resWidth : numThreads;
	  uint32_t step = resWidth / numTasks;
	  for(uint32_t j = 0; j < numTasks; j++)
	  {
		 auto indexMin = j * step;
		 auto indexMax = j < (numTasks - 1U) ? (j + 1U) * step : resWidth;
		 auto vert = new dwt_data<int32_t>(vert_);
		 if(!vert->alloc(dataLength))
		 {
			Logger::logger_.error("Out of memory");
			delete vert;
			return false;
		 }
		 resFlow->waveletVert_->nextTask().work(
			 [this, vert, indexMin, indexMax, winL, winH, winDest] {
				decompress_v_strip_53(vert, indexMin, indexMax, winL, winH, winDest);
				delete vert;
			 });
		 winL.incX_IN_PLACE(step);
		 winH.incX_IN_PLACE(step);
		 winDest.incX_IN_PLACE(step);
	  }
   }
   return true;
}
/* <summary>                            */
/* Inverse wavelet transform in 2-D.    */
/* </summary>                           */
bool WaveletReverse::decompress_tile_53(void)
{
   if(numres_ == 1U)
	  return true;

   auto tileCompRes = tilec_->resolutions_;
   auto buf = tilec_->getWindow();
   size_t dataLength = max_resolution(tileCompRes, numres_);
   /* overflow check */
   if(dataLength > (SIZE_MAX / PLL_COLS_53 / sizeof(int32_t)))
   {
	  Logger::logger_.error("Overflow");
	  return false;
   }
   /* We need PLL_COLS_53 times the height of the array, */
   /* since for the vertical pass */
   /* we process PLL_COLS_53 columns at a time */
   dataLength *= PLL_COLS_53 * sizeof(int32_t);
   for(uint8_t res = 1; res < numres_; ++res)
   {
	  horiz_.sn_full = tileCompRes->width();
	  vert_.sn_full = tileCompRes->height();
	  ++tileCompRes;
	  auto resWidth = tileCompRes->width();
	  auto resHeight = tileCompRes->height();
	  if(resWidth == 0 || resHeight == 0)
		 continue;
	  horiz_.dn_full = resWidth - horiz_.sn_full;
	  horiz_.parity = tileCompRes->x0 & 1;
	  vert_.dn_full = resHeight - vert_.sn_full;
	  vert_.parity = tileCompRes->y0 & 1;
	  if(!decompress_h_53(res, buf, resHeight, dataLength))
		 return false;
	  if(!decompress_v_53(res, buf, resWidth, dataLength))
		 return false;
   }

   return true;
}

/*************************************************************************************
 *
 * Partial 5/3 or 9/7 Inverse Wavelet
 *
 **************************************************************************************
 *
 *
 * 5/3 operates on elements of type int32_t while 9/7 operates on elements of type vec4f
 *
 * Horizontal pass
 *
 * Each thread processes a strip running the length of the window, with height
 *   5/3
 *   Height : sizeof(T)/sizeof(int32_t)
 *
 *   9/7
 *   Height : sizeof(T)/sizeof(int32_t)
 *
 * Vertical pass
 *
 * Each thread processes a strip running the height of the window, with width
 *
 *  5/3
 *  Width :  4
 *
 *  9/7
 *  Width :  4
 *
 ****************************************************************************/
template<typename T, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH>
class PartialInterleaver
{
 public:
   bool interleave_h(dwt_data<T>* dwt, ISparseCanvas* sa, uint32_t y_offset, uint32_t height)
   {
	  const uint32_t stripHeight = (uint32_t)(sizeof(T) / sizeof(int32_t));
	  for(uint32_t y = 0; y < height; y++)
	  {
		 // read one row of L band
		 if(dwt->sn_full)
		 {
			bool ret =
				sa->read(dwt->resno,
						 grk_rect32(dwt->win_l.x0, y_offset + y,
									std::min<uint32_t>(dwt->win_l.x1 + FILTER_WIDTH, dwt->sn_full),
									y_offset + y + 1),
						 (int32_t*)dwt->memL + y, 2 * stripHeight, 0);
			if(!ret)
			   return false;
		 }
		 // read one row of H band
		 if(dwt->dn_full)
		 {
			bool ret =
				sa->read(dwt->resno,
						 grk_rect32(dwt->sn_full + dwt->win_h.x0, y_offset + y,
									dwt->sn_full + std::min<uint32_t>(dwt->win_h.x1 + FILTER_WIDTH,
																	  dwt->dn_full),
									y_offset + y + 1),
						 (int32_t*)dwt->memH + y, 2 * stripHeight, 0);
			if(!ret)
			   return false;
		 }
	  }

	  return true;
   }
   bool interleave_v(dwt_data<T>* GRK_RESTRICT dwt, ISparseCanvas* sa, uint32_t x_offset,
					 uint32_t xWidth)
   {
	  const uint32_t stripWidth = (sizeof(T) / sizeof(int32_t)) * VERT_PASS_WIDTH;
	  // read one vertical strip (of width xWidth <= stripWidth) of L band
	  bool ret = false;
	  if(dwt->sn_full)
	  {
		 ret = sa->read(dwt->resno,
						grk_rect32(x_offset, dwt->win_l.x0, x_offset + xWidth,
								   std::min<uint32_t>(dwt->win_l.x1 + FILTER_WIDTH, dwt->sn_full)),
						(int32_t*)dwt->memL, 1, 2 * stripWidth);
	  }
	  // read one vertical strip (of width x_num_elements <= stripWidth) of H band
	  if(dwt->dn_full)
	  {
		 ret = sa->read(dwt->resno,
						grk_rect32(x_offset, dwt->sn_full + dwt->win_h.x0, x_offset + xWidth,
								   dwt->sn_full + std::min<uint32_t>(dwt->win_h.x1 + FILTER_WIDTH,
																	 dwt->dn_full)),
						(int32_t*)dwt->memH, 1, 2 * stripWidth);
	  }

	  return ret;
   }
};
template<typename T, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH>
class Partial53 : public PartialInterleaver<T, FILTER_WIDTH, VERT_PASS_WIDTH>
{
 public:
   void decompress_h(dwt_data<T>* dwt)
   {
#ifndef GRK_DEBUG_SPARSE
#define get_S(buf, i) buf[(i) << 1]
#define get_D(buf, i) buf[(1 + ((i) << 1))]
#endif

#define S(buf, i) buf[(i) << 1]
#define D(buf, i) buf[(1 + ((i) << 1))]

// parity == 0
#define S_(buf, i) \
   ((i) < -win_l_x0 ? get_S(buf, -win_l_x0) : ((i) >= sn ? get_S(buf, sn - 1) : get_S(buf, i)))
#define D_(buf, i) \
   ((i) < -win_h_x0 ? get_D(buf, -win_h_x0) : ((i) >= dn ? get_D(buf, dn - 1) : get_D(buf, i)))

// parity == 1
#define SS_(buf, i) \
   ((i) < -win_h_x0 ? get_S(buf, -win_h_x0) : ((i) >= dn ? get_S(buf, dn - 1) : get_S(buf, i)))
#define DD_(buf, i) \
   ((i) < -win_l_x0 ? get_D(buf, -win_l_x0) : ((i) >= sn ? get_D(buf, sn - 1) : get_D(buf, i)))

	  int64_t i;
	  int64_t parity = dwt->parity;
	  int64_t win_l_x0 = dwt->win_l.x0;
	  int64_t win_l_x1 = dwt->win_l.x1;
	  int64_t win_h_x0 = dwt->win_h.x0;
	  int64_t win_h_x1 = dwt->win_h.x1;
	  assert(dwt->win_l.x0 <= dwt->sn_full);
	  int64_t sn = (int64_t)dwt->sn_full - (int64_t)dwt->win_l.x0;
	  int64_t sn_full = dwt->sn_full;
	  assert(dwt->win_h.x0 <= dwt->dn_full);
	  int64_t dn = (int64_t)dwt->dn_full - (int64_t)dwt->win_h.x0;
	  int64_t dn_full = dwt->dn_full;

	  adjust_bounds(dwt, sn_full, dn_full, &sn, &dn);

	  assert(dwt->win_l.x1 <= sn_full && dwt->win_h.x1 <= dn_full);

	  auto buf = dwt->mem;
	  if(!parity)
	  {
		 if((dn_full != 0) || (sn_full > 1))
		 {
			/* Naive version is :
			for (i = win_l_x0; i < i_max; i++) {
			  S(i) -= (D_(i - 1) + D_(i) + 2) >> 2;
			}
			for (i = win_h_x0; i < win_h_x1; i++) {
			  D(i) += (S_(i) + S_(i + 1)) >> 1;
			}
			but the compiler doesn't manage to unroll it to avoid bound
			checking in S_ and D_ macros
			*/
			i = 0;
			int64_t i_max = win_l_x1 - win_l_x0;
			if(i < i_max)
			{
			   /* Left-most case */
			   S(buf, i) -= (D_(buf, i - 1) + D_(buf, i) + 2) >> 2;
			   i++;

			   if(i_max > dn)
				  i_max = dn;
			   for(; i < i_max; i++)
				  /* No bound checking */
				  S(buf, i) -= (get_D(buf, i - 1) + get_D(buf, i) + 2) >> 2;
			   for(; i < win_l_x1 - win_l_x0; i++)
				  /* Right-most case */
				  S(buf, i) -= (D_(buf, i - 1) + D_(buf, i) + 2) >> 2;
			}
			i = 0;
			i_max = win_h_x1 - win_h_x0;
			if(i < i_max)
			{
			   if(i_max >= sn)
				  i_max = sn - 1;
			   for(; i < i_max; i++)
				  /* No bound checking */
				  D(buf, i) += (S(buf, i) + S(buf, i + 1)) >> 1;
			   for(; i < win_h_x1 - win_h_x0; i++)
				  /* Right-most case */
				  D(buf, i) += (S_(buf, i) + S_(buf, i + 1)) >> 1;
			}
		 }
	  }
	  else
	  {
		 if(sn_full == 0 && dn_full == 1)
		 {
			// only do L band (high pass)
			S(buf, 0) >>= 1;
		 }
		 else
		 {
			for(i = 0; i < win_l_x1 - win_l_x0; i++)
			   D(buf, i) -= (SS_(buf, i) + SS_(buf, i + 1) + 2) >> 2;
			for(i = 0; i < win_h_x1 - win_h_x0; i++)
			   S(buf, i) += (DD_(buf, i) + DD_(buf, i - 1)) >> 1;
		 }
	  }
   }
   void decompress_v(dwt_data<T>* dwt)
   {
#ifndef GRK_DEBUG_SPARSE
#define get_S_off(buf, i, off) buf[((i) << 1) * VERT_PASS_WIDTH + off]
#define get_D_off(buf, i, off) buf[(1 + ((i) << 1)) * VERT_PASS_WIDTH + off]
#endif

#define S_off(buf, i, off) buf[((i) << 1) * VERT_PASS_WIDTH + off]
#define D_off(buf, i, off) buf[(1 + ((i) << 1)) * VERT_PASS_WIDTH + off]

// parity == 0
#define S_off_(buf, i, off) (((i) >= sn ? get_S_off(buf, sn - 1, off) : get_S_off(buf, i, off)))
#define D_off_(buf, i, off) (((i) >= dn ? get_D_off(buf, dn - 1, off) : get_D_off(buf, i, off)))

#define S_sgnd_off_(buf, i, off) \
   (((i) < (-win_l_x0) ? get_S_off(buf, -win_l_x0, off) : S_off_(buf, i, off)))
#define D_sgnd_off_(buf, i, off) \
   (((i) < (-win_h_x0) ? get_D_off(buf, -win_h_x0, off) : D_off_(buf, i, off)))

// case == 1
#define SS_sgnd_off_(buf, i, off)                      \
   ((i) < (-win_h_x0) ? get_S_off(buf, -win_h_x0, off) \
					  : ((i) >= dn ? get_S_off(buf, dn - 1, off) : get_S_off(buf, i, off)))
#define DD_sgnd_off_(buf, i, off)                      \
   ((i) < (-win_l_x0) ? get_D_off(buf, -win_l_x0, off) \
					  : ((i) >= sn ? get_D_off(buf, sn - 1, off) : get_D_off(buf, i, off)))

#define SS_off_(buf, i, off) (((i) >= dn ? get_S_off(buf, dn - 1, off) : get_S_off(buf, i, off)))
#define DD_off_(buf, i, off) (((i) >= sn ? get_D_off(buf, sn - 1, off) : get_D_off(buf, i, off)))

	  int64_t i;
	  int64_t parity = dwt->parity;
	  int64_t win_l_x0 = dwt->win_l.x0;
	  int64_t win_l_x1 = dwt->win_l.x1;
	  int64_t win_h_x0 = dwt->win_h.x0;
	  int64_t win_h_x1 = dwt->win_h.x1;
	  int64_t sn = (int64_t)dwt->sn_full - (int64_t)dwt->win_l.x0;
	  int64_t sn_full = dwt->sn_full;
	  int64_t dn = (int64_t)dwt->dn_full - (int64_t)dwt->win_h.x0;
	  int64_t dn_full = dwt->dn_full;

	  adjust_bounds(dwt, sn_full, dn_full, &sn, &dn);

	  assert(dwt->win_l.x1 <= sn_full && dwt->win_h.x1 <= dn_full);

	  auto buf = dwt->mem;
	  if(!parity)
	  {
		 if((dn_full != 0) || (sn_full > 1))
		 {
			/* Naive version is :
			for (i = win_l_x0; i < i_max; i++) {
			  S(i) -= (D_(i - 1) + D_(i) + 2) >> 2;
			}
			for (i = win_h_x0; i < win_h_x1; i++) {
			  D(i) += (S_(i) + S_(i + 1)) >> 1;
			}
			but the compiler doesn't manage to unroll it to avoid bound
			checking in S_ and D_ macros
			*/

			// 1. low pass
			i = 0;
			int64_t i_max = win_l_x1 - win_l_x0;
			assert(win_l_x1 >= win_l_x0);
			if(i < i_max)
			{
			   /* Left-most case */
			   for(int64_t off = 0; off < VERT_PASS_WIDTH; off++)
				  S_off(buf, i, off) -=
					  (D_sgnd_off_(buf, i - 1, off) + D_off_(buf, i, off) + 2) >> 2;
			   i++;
			   if(i_max > dn)
				  i_max = dn;
#ifdef __SSE2__
			   if(i + 1 < i_max)
			   {
				  const __m128i two = _mm_set1_epi32(2);
				  auto Dm1 = _mm_load_si128((__m128i*)(buf + ((i << 1) - 1) * VERT_PASS_WIDTH));
				  for(; i + 1 < i_max; i += 2)
				  {
					 /* No bound checking */
					 auto S = _mm_load_si128((__m128i*)(buf + (i << 1) * VERT_PASS_WIDTH));
					 auto D = _mm_load_si128((__m128i*)(buf + ((i << 1) + 1) * VERT_PASS_WIDTH));
					 auto S1 = _mm_load_si128((__m128i*)(buf + ((i << 1) + 2) * VERT_PASS_WIDTH));
					 auto D1 = _mm_load_si128((__m128i*)(buf + ((i << 1) + 3) * VERT_PASS_WIDTH));
					 S = _mm_sub_epi32(
						 S, _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(Dm1, D), two), 2));
					 S1 = _mm_sub_epi32(
						 S1, _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(D, D1), two), 2));
					 _mm_store_si128((__m128i*)(buf + (i << 1) * VERT_PASS_WIDTH), S);
					 _mm_store_si128((__m128i*)(buf + ((i + 1) << 1) * VERT_PASS_WIDTH), S1);
					 Dm1 = D1;
				  }
			   }
#endif
			   for(; i < i_max; i++)
			   {
				  /* No bound checking */
				  for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
					 S_off(buf, i, off) -=
						 (D_sgnd_off_(buf, i - 1, off) + D_off(buf, i, off) + 2) >> 2;
			   }
			   for(; i < win_l_x1 - win_l_x0; i++)
			   {
				  /* Right-most case */
				  for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
					 S_off(buf, i, off) -=
						 (D_sgnd_off_(buf, i - 1, off) + D_off_(buf, i, off) + 2) >> 2;
			   }
			}

			// 2. high pass
			i = 0;
			assert(win_h_x1 >= win_h_x0);
			i_max = win_h_x1 - win_h_x0;
			if(i < i_max)
			{
			   if(i_max >= sn)
				  i_max = sn - 1;
#ifdef __SSE2__
			   if(i + 1 < i_max)
			   {
				  auto S = _mm_load_si128((__m128i*)(buf + (i << 1) * VERT_PASS_WIDTH));
				  for(; i + 1 < i_max; i += 2)
				  {
					 /* No bound checking */
					 auto D = _mm_load_si128((__m128i*)(buf + (1 + (i << 1)) * VERT_PASS_WIDTH));
					 auto S1 = _mm_load_si128((__m128i*)(buf + ((i + 1) << 1) * VERT_PASS_WIDTH));
					 auto D1 =
						 _mm_load_si128((__m128i*)(buf + (1 + ((i + 1) << 1)) * VERT_PASS_WIDTH));
					 auto S2 = _mm_load_si128((__m128i*)(buf + ((i + 2) << 1) * VERT_PASS_WIDTH));
					 D = _mm_add_epi32(D, _mm_srai_epi32(_mm_add_epi32(S, S1), 1));
					 D1 = _mm_add_epi32(D1, _mm_srai_epi32(_mm_add_epi32(S1, S2), 1));
					 _mm_store_si128((__m128i*)(buf + (1 + (i << 1)) * VERT_PASS_WIDTH), D);
					 _mm_store_si128((__m128i*)(buf + (1 + ((i + 1) << 1)) * VERT_PASS_WIDTH), D1);
					 S = S2;
				  }
			   }
#endif
			   for(; i < i_max; i++)
			   {
				  /* No bound checking */
				  for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
					 D_off(buf, i, off) += (S_off(buf, i, off) + S_off(buf, i + 1, off)) >> 1;
			   }
			   for(; i < win_h_x1 - win_h_x0; i++)
			   {
				  /* Right-most case */
				  for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
					 D_off(buf, i, off) += (S_off_(buf, i, off) + S_off_(buf, i + 1, off)) >> 1;
			   }
			}
		 }
	  }
	  else
	  {
		 if(sn_full == 0 && dn_full == 1)
		 {
			// edge case at origin
			for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
			   S_off(buf, 0, off) >>= 1;
		 }
		 else
		 {
			assert((uint64_t)(dwt->memL + (win_l_x1 - win_l_x0) * VERT_PASS_WIDTH) -
					   (uint64_t)dwt->allocatedMem <
				   dwt->lenBytes_);
			for(i = 0; i < win_l_x1 - win_l_x0; i++)
			{
			   for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
				  D_off(buf, i, off) -= (SS_off_(buf, i, off) + SS_off_(buf, i + 1, off) + 2) >> 2;
			}
			assert((uint64_t)(dwt->memH + (win_h_x1 - win_h_x0) * VERT_PASS_WIDTH) -
					   (uint64_t)dwt->allocatedMem <
				   dwt->lenBytes_);
			for(i = 0; i < win_h_x1 - win_h_x0; i++)
			{
			   for(uint32_t off = 0; off < VERT_PASS_WIDTH; off++)
				  S_off(buf, i, off) += (DD_off_(buf, i, off) + DD_sgnd_off_(buf, i - 1, off)) >> 1;
			}
		 }
	  }
   }

 private:
   void adjust_bounds(dwt_data<T>* dwt, [[maybe_unused]] int64_t sn_full,
					  [[maybe_unused]] int64_t dn_full, int64_t* sn, int64_t* dn)
   {
	  if((uint64_t)dwt->memH < (uint64_t)dwt->memL && *sn == *dn)
	  {
		 assert(dn_full == sn_full - 1);
		 (*dn)--;
	  }
	  if((uint64_t)dwt->memL < (uint64_t)dwt->memH && *sn == *dn)
	  {
		 assert(sn_full == dn_full - 1);
		 (*sn)--;
	  }
   }
#ifdef GRK_DEBUG_SPARSE
   inline T get_S(T* const buf, int64_t i)
   {
	  auto ret = buf[(i) << 1];
	  assert(abs(ret) < 0xFFFFFFF);
	  return ret;
   }
   inline T get_D(T* const buf, int64_t i)
   {
	  auto ret = buf[(1 + ((i) << 1))];
	  assert(abs(ret) < 0xFFFFFFF);
	  return ret;
   }
   inline T get_S_off(T* const buf, int64_t i, int64_t off)
   {
	  auto ret = buf[(i)*2 * VERT_PASS_WIDTH + off];
	  assert(abs(ret) < 0xFFFFFFF);
	  return ret;
   }
   inline T get_D_off(T* const buf, int64_t i, int64_t off)
   {
	  auto ret = buf[(1 + (i)*2) * VERT_PASS_WIDTH + off];
	  assert(abs(ret) < 0xFFFFFFF);
	  return ret;
   }
#endif
};
template<typename T, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH>
class Partial97 : public PartialInterleaver<T, FILTER_WIDTH, VERT_PASS_WIDTH>
{
 public:
   void decompress_h(dwt_data<T>* dwt)
   {
	  WaveletReverse::decompress_step_97(dwt);
   }
   void decompress_v(dwt_data<T>* dwt)
   {
	  WaveletReverse::decompress_step_97(dwt);
   }
};
// Notes:
// 1. line buffer 0 offset == dwt->win_l.x0
// 2. dwt->memL and dwt->memH are only set for partial decode
Params97 WaveletReverse::makeParams97(dwt_data<vec4f>* dwt, bool isBandL, bool step1)
{
   Params97 rc;
   // band_0 specifies absolute start of line buffer
   int64_t band_0 = isBandL ? dwt->win_l.x0 : dwt->win_h.x0;
   int64_t band_1 = isBandL ? dwt->win_l.x1 : dwt->win_h.x1;
   auto memPartial = isBandL ? dwt->memL : dwt->memH;
   int64_t parityOffset = isBandL ? dwt->parity : !dwt->parity;
   int64_t lenMax = isBandL
						? (std::min<int64_t>)(dwt->sn_full, (int64_t)dwt->dn_full - parityOffset)
						: (std::min<int64_t>)(dwt->dn_full, (int64_t)dwt->sn_full - parityOffset);
   if(lenMax < 0)
	  lenMax = 0;
   assert(lenMax >= band_0);
   lenMax -= band_0;
   rc.data = memPartial ? memPartial : dwt->mem;

   assert(!memPartial || (dwt->win_l.x1 <= dwt->sn_full && dwt->win_h.x1 <= dwt->dn_full));
   assert(band_1 >= band_0);

   rc.data += parityOffset + band_0 - dwt->win_l.x0;
   rc.len = (uint32_t)(band_1 - band_0);
   if(!step1)
   {
	  rc.data += 1;
	  rc.dataPrev = parityOffset ? rc.data - 2 : rc.data;
	  rc.lenMax = (uint32_t)lenMax;
   }
   if(memPartial)
   {
	  assert((uint64_t)rc.data >= (uint64_t)dwt->allocatedMem);
	  assert((uint64_t)rc.data <= (uint64_t)dwt->allocatedMem + dwt->lenBytes_);
   }

   return rc;
};

template<uint32_t FILTER_WIDTH>
struct PartialBandInfo
{
   // 1. set up windows for horizontal and vertical passes
   grk_rect32 bandWindowREL_[BAND_NUM_ORIENTATIONS];
   // two windows formed by horizontal pass and used as input for vertical pass
   grk_rect32 splitWindowREL_[SPLIT_NUM_ORIENTATIONS];
   grk_rect32 resWindowREL_;

   bool alloc(ISparseCanvas* sa, uint8_t resno, Resolution* fullRes,
			  TileComponentWindow<int32_t>* tileWindow)
   {
	  bandWindowREL_[BAND_ORIENT_LL] =
		  tileWindow->getBandWindowBufferPaddedREL(resno, BAND_ORIENT_LL);
	  bandWindowREL_[BAND_ORIENT_HL] =
		  tileWindow->getBandWindowBufferPaddedREL(resno, BAND_ORIENT_HL);
	  bandWindowREL_[BAND_ORIENT_LH] =
		  tileWindow->getBandWindowBufferPaddedREL(resno, BAND_ORIENT_LH);
	  bandWindowREL_[BAND_ORIENT_HH] =
		  tileWindow->getBandWindowBufferPaddedREL(resno, BAND_ORIENT_HH);

	  // band windows in band coordinates - needed to pre-allocate sparse blocks
	  grk_rect32 tileBandWindowREL[BAND_NUM_ORIENTATIONS];

	  tileBandWindowREL[BAND_ORIENT_LL] = bandWindowREL_[BAND_ORIENT_LL];
	  tileBandWindowREL[BAND_ORIENT_HL] =
		  bandWindowREL_[BAND_ORIENT_HL].pan(fullRes->tileBand[BAND_INDEX_LH].width(), 0);
	  tileBandWindowREL[BAND_ORIENT_LH] =
		  bandWindowREL_[BAND_ORIENT_LH].pan(0, fullRes->tileBand[BAND_INDEX_HL].height());
	  tileBandWindowREL[BAND_ORIENT_HH] = bandWindowREL_[BAND_ORIENT_HH].pan(
		  fullRes->tileBand[BAND_INDEX_LH].width(), fullRes->tileBand[BAND_INDEX_HL].height());
	  // 2. pre-allocate sparse blocks
	  for(uint32_t i = 0; i < BAND_NUM_ORIENTATIONS; ++i)
	  {
		 auto temp = tileBandWindowREL[i];
		 if(!sa->alloc(temp.grow_IN_PLACE(2 * FILTER_WIDTH, fullRes->width(), fullRes->height()),
					   true))
			return false;
	  }
	  resWindowREL_ = tileWindow->getResWindowBufferREL(resno);
	  if(!sa->alloc(resWindowREL_, true))
		 return false;
	  splitWindowREL_[SPLIT_L] = tileWindow->getResWindowBufferSplitREL(resno, SPLIT_L);
	  splitWindowREL_[SPLIT_H] = tileWindow->getResWindowBufferSplitREL(resno, SPLIT_H);

	  auto fullResNext = fullRes + 1;
	  for(uint32_t k = 0; k < SPLIT_NUM_ORIENTATIONS; ++k)
	  {
		 auto temp = splitWindowREL_[k];
		 if(!sa->alloc(
				temp.grow_IN_PLACE(2 * FILTER_WIDTH, fullResNext->width(), fullResNext->height()),
				true))
			return false;
	  }

	  return true;
   }
};

/**
 * ************************************************************************************
 *
 * 5/3 operates on elements of type int32_t while 9/7 operates on elements of type vec4f
 *
 * Horizontal pass
 *
 * Each thread processes a strip running the length of the window, of the following dimensions:
 *
 *   5/3
 *   Height : 1
 *
 *   9/7
 *   Height : 4
 *
 * Vertical pass
 *
 *  5/3
 *  Width :  4
 *
 *  9/7
 *  Height : 1
 *
 ****************************************************************************
 *
 * FILTER_WIDTH value matches the maximum left/right extension given in tables
 * F.2 and F.3 of the standard
 */
template<typename T, uint32_t FILTER_WIDTH, uint32_t VERT_PASS_WIDTH, typename D>

bool WaveletReverse::decompress_partial_tile(ISparseCanvas* sa,
											 std::vector<TaskInfo<T, dwt_data<T>>*>& tasks)
{
   uint8_t numresolutions = tilec_->numresolutions;
   auto buf = tilec_->getWindow();
   auto simpleBuf = buf->getResWindowBufferHighestSimple();
   auto fullRes = tilec_->resolutions_;
   auto fullResTopLevel = tilec_->resolutions_ + numres_ - 1;
   if(!fullResTopLevel->width() || !fullResTopLevel->height())
	  return true;

   [[maybe_unused]] const uint16_t debug_compno = 0;
   const uint32_t HORIZ_PASS_HEIGHT = sizeof(T) / sizeof(int32_t);
   const uint32_t pad = FILTER_WIDTH * std::max<uint32_t>(HORIZ_PASS_HEIGHT, VERT_PASS_WIDTH) *
						sizeof(T) / sizeof(int32_t);
   // reduce window
   auto synthesisWindow = unreducedWindow_.scaleDownCeilPow2(numresolutions - numres_);
   assert(fullResTopLevel->intersection(synthesisWindow) == synthesisWindow);
   // shift to relative coordinates
   synthesisWindow =
	   synthesisWindow.pan(-(int64_t)fullResTopLevel->x0, -(int64_t)fullResTopLevel->y0);
   if(synthesisWindow.empty())
	  return true;
   uint32_t numThreads = (uint32_t)ExecSingleton::get().num_workers();
   auto imageComponentFlow = scheduler_->getImageComponentFlow(compno_);
   // imageComponentFlow == nullptr ==> no blocks were decompressed for this component
   if(!imageComponentFlow)
	  return true;
   if(numres_ == 1U)
   {
	  auto final_read = [sa, synthesisWindow, simpleBuf]() {
		 // final read into tile buffer
		 bool ret = sa->read(0, synthesisWindow, simpleBuf.buf_, 1, simpleBuf.stride_);

		 return ret;
	  };
	  if(numThreads > 1)
		 imageComponentFlow->waveletFinalCopy_->nextTask().work([final_read] { final_read(); });
	  else
		 final_read();

	  return true;
   }
   auto final_read = [this, sa, synthesisWindow, simpleBuf]() {
	  // final read into tile buffer
	  bool ret = sa->read(numres_ - 1, synthesisWindow, simpleBuf.buf_, 1, simpleBuf.stride_);

	  return ret;
   };
   // pre-allocate all blocks
   std::vector<PartialBandInfo<FILTER_WIDTH>> resBandInfo;
   for(uint8_t resno = 1; resno < numres_; resno++)
   {
	  PartialBandInfo<FILTER_WIDTH> bandInfo;
	  if(!bandInfo.alloc(sa, resno, fullRes + resno - 1, buf))
		 return false;
	  resBandInfo.push_back(bandInfo);
   }
   D decompressor;
   for(uint8_t resno = 1; resno < numres_; resno++)
   {
	  dwt_data<T> horiz;
	  dwt_data<T> vert;
	  horiz.sn_full = fullRes->width();
	  vert.sn_full = fullRes->height();
	  fullRes++;
	  horiz.dn_full = fullRes->width() - horiz.sn_full;
	  horiz.parity = fullRes->x0 & 1;
	  vert.dn_full = fullRes->height() - vert.sn_full;
	  vert.parity = fullRes->y0 & 1;
	  PartialBandInfo<FILTER_WIDTH>& bandInfo = resBandInfo[resno - 1];

	  auto executor_h = [resno, sa, bandInfo, &decompressor](TaskInfo<T, dwt_data<T>>* taskInfo) {
		 for(uint32_t yPos = taskInfo->indexMin_; yPos < taskInfo->indexMax_;
			 yPos += HORIZ_PASS_HEIGHT)
		 {
			auto height =
				std::min<uint32_t>((uint32_t)HORIZ_PASS_HEIGHT, taskInfo->indexMax_ - yPos);
			taskInfo->data.memL = taskInfo->data.mem + taskInfo->data.parity;
			taskInfo->data.memH =
				taskInfo->data.mem + (int64_t)(!taskInfo->data.parity) +
				2 * ((int64_t)taskInfo->data.win_h.x0 - (int64_t)taskInfo->data.win_l.x0);
			if(!decompressor.interleave_h(&taskInfo->data, sa, yPos, height))
			{
			   return false;
			}
			taskInfo->data.memL = taskInfo->data.mem;
			taskInfo->data.memH = taskInfo->data.mem + ((int64_t)taskInfo->data.win_h.x0 -
														(int64_t)taskInfo->data.win_l.x0);
			decompressor.decompress_h(&taskInfo->data);
			if(!sa->write(resno,
						  grk_rect32(bandInfo.resWindowREL_.x0, yPos, bandInfo.resWindowREL_.x1,
									 yPos + height),
						  (int32_t*)(taskInfo->data.mem + (int64_t)bandInfo.resWindowREL_.x0 -
									 2 * (int64_t)taskInfo->data.win_l.x0),
						  HORIZ_PASS_HEIGHT, 1))
			{
			   return false;
			}
		 }

		 return true;
	  };
	  auto executor_v = [resno, sa, bandInfo, &decompressor](TaskInfo<T, dwt_data<T>>* taskInfo) {
		 for(uint32_t xPos = taskInfo->indexMin_; xPos < taskInfo->indexMax_;
			 xPos += VERT_PASS_WIDTH)
		 {
			auto width = std::min<uint32_t>(VERT_PASS_WIDTH, (taskInfo->indexMax_ - xPos));
			taskInfo->data.memL = taskInfo->data.mem + (taskInfo->data.parity) * VERT_PASS_WIDTH;
			taskInfo->data.memH =
				taskInfo->data.mem +
				((!taskInfo->data.parity) +
				 2 * ((int64_t)taskInfo->data.win_h.x0 - (int64_t)taskInfo->data.win_l.x0)) *
					VERT_PASS_WIDTH;
			if(!decompressor.interleave_v(&taskInfo->data, sa, xPos, width))
			{
			   return false;
			}
			taskInfo->data.memL = taskInfo->data.mem;
			taskInfo->data.memH = taskInfo->data.mem + ((int64_t)taskInfo->data.win_h.x0 -
														(int64_t)taskInfo->data.win_l.x0) *
														   VERT_PASS_WIDTH;
			decompressor.decompress_v(&taskInfo->data);
			// write to buffer for final res
			if(!sa->write(resno,
						  grk_rect32(xPos, bandInfo.resWindowREL_.y0, xPos + width,
									 bandInfo.resWindowREL_.y0 + taskInfo->data.win_l.length() +
										 taskInfo->data.win_h.length()),
						  (int32_t*)(taskInfo->data.mem + ((int64_t)bandInfo.resWindowREL_.y0 -
														   2 * (int64_t)taskInfo->data.win_l.x0) *
															  VERT_PASS_WIDTH),
						  1, VERT_PASS_WIDTH * (sizeof(T) / sizeof(int32_t))))
			{
			   Logger::logger_.error("Sparse array write failure");
			   return false;
			}
		 }

		 return true;
	  };

	  // 3. calculate synthesis
	  horiz.win_l = bandInfo.bandWindowREL_[BAND_ORIENT_LL].dimX();
	  horiz.win_h = bandInfo.bandWindowREL_[BAND_ORIENT_HL].dimX();
	  horiz.resno = resno;
	  size_t dataLength =
		  (bandInfo.splitWindowREL_[0].width() + 2 * FILTER_WIDTH) * HORIZ_PASS_HEIGHT;
	  auto resFlow = imageComponentFlow->getResFlow(resno - 1);
	  for(uint32_t k = 0; k < 2 && dataLength; ++k)
	  {
		 uint32_t numTasks = numThreads;
		 uint32_t num_rows = bandInfo.splitWindowREL_[k].height();
		 if(num_rows < numTasks)
			numTasks = num_rows;
		 uint32_t incrPerJob = numTasks ? (num_rows / numTasks) : 0;
		 if(numThreads == 1)
			numTasks = 1;
		 if(incrPerJob == 0)
			continue;
		 for(uint32_t j = 0; j < numTasks; ++j)
		 {
			uint32_t indexMin = bandInfo.splitWindowREL_[k].y0 + j * incrPerJob;
			uint32_t indexMax = j < (numTasks - 1U)
									? bandInfo.splitWindowREL_[k].y0 + (j + 1U) * incrPerJob
									: bandInfo.splitWindowREL_[k].y1;
			if(indexMin == indexMax)
			   continue;
			auto taskInfo = new TaskInfo<T, dwt_data<T>>(horiz, indexMin, indexMax);
			if(!taskInfo->data.alloc(dataLength, pad))
			{
			   delete taskInfo;
			   return false;
			}
			tasks.push_back(taskInfo);
			if(numThreads > 1)
			   resFlow->waveletHoriz_->nextTask().work(
				   [taskInfo, executor_h] { executor_h(taskInfo); });
			else
			   executor_h(taskInfo);
		 }
	  }
	  dataLength = (bandInfo.resWindowREL_.height() + 2 * FILTER_WIDTH) * VERT_PASS_WIDTH *
				   sizeof(T) / sizeof(int32_t);
	  vert.win_l = bandInfo.bandWindowREL_[BAND_ORIENT_LL].dimY();
	  vert.win_h = bandInfo.bandWindowREL_[BAND_ORIENT_LH].dimY();
	  vert.resno = resno;
	  uint32_t numTasks = numThreads;
	  uint32_t numColumns = bandInfo.resWindowREL_.width();
	  if(numColumns < numTasks)
		 numTasks = numColumns;
	  uint32_t incrPerJob = numTasks ? (numColumns / numTasks) : 0;
	  if(numThreads == 1)
		 numTasks = 1;
	  for(uint32_t j = 0; j < numTasks && incrPerJob > 0 && dataLength; ++j)
	  {
		 uint32_t indexMin = bandInfo.resWindowREL_.x0 + j * incrPerJob;
		 uint32_t indexMax = j < (numTasks - 1U) ? bandInfo.resWindowREL_.x0 + (j + 1U) * incrPerJob
												 : bandInfo.resWindowREL_.x1;
		 if(indexMin == indexMax)
			continue;
		 auto taskInfo = new TaskInfo<T, dwt_data<T>>(vert, indexMin, indexMax);
		 if(!taskInfo->data.alloc(dataLength, pad))
		 {
			delete taskInfo;
			return false;
		 }
		 tasks.push_back(taskInfo);
		 if(numThreads > 1)
			resFlow->waveletVert_->nextTask().work(
				[taskInfo, executor_v] { executor_v(taskInfo); });
		 else
			executor_v(taskInfo);
	  }
   }

   if(numThreads > 1)
	  imageComponentFlow->waveletFinalCopy_->nextTask().work([final_read] { final_read(); });
   else
	  final_read();

   return true;
}
WaveletReverse::WaveletReverse(TileProcessor* tileProcessor, TileComponent* tilec, uint16_t compno,
							   grk_rect32 unreducedWindow, uint8_t numres, uint8_t qmfbid)
	: tileProcessor_(tileProcessor), scheduler_(tileProcessor->getScheduler()), tilec_(tilec),
	  compno_(compno), unreducedWindow_(unreducedWindow), numres_(numres), qmfbid_(qmfbid)
{}
WaveletReverse::~WaveletReverse(void)
{
   for(const auto& t : tasks_)
	  delete t;
   for(const auto& t : tasksF_)
	  delete t;
}
bool WaveletReverse::decompress(void)
{
   if(qmfbid_ == 1)
   {
	  if(tileProcessor_->cp_->wholeTileDecompress_)
		 return decompress_tile_53();
	  else
	  {
		 constexpr uint32_t VERT_PASS_WIDTH = 4;
		 return decompress_partial_tile<
			 int32_t, getFilterPad<uint32_t>(true), VERT_PASS_WIDTH,
			 Partial53<int32_t, getFilterPad<uint32_t>(false), VERT_PASS_WIDTH>>(
			 tilec_->getRegionWindow(), tasks_);
	  }
   }
   else
   {
	  if(tileProcessor_->cp_->wholeTileDecompress_)
		 return decompress_tile_97();
	  else
	  {
		 constexpr uint32_t VERT_PASS_WIDTH = 1;
		 return decompress_partial_tile<
			 vec4f, getFilterPad<uint32_t>(false), VERT_PASS_WIDTH,
			 Partial97<vec4f, getFilterPad<uint32_t>(false), VERT_PASS_WIDTH>>(
			 tilec_->getRegionWindow(), tasksF_);
	  }
   }
}

} // namespace grk
#endif
