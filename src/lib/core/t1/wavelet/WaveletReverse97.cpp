/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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
 */

/*************************************
 *
 * Full 9/7 Inverse Wavelet
 *
 ***********************************/

#include "grk_includes.h"
#include <algorithm>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "wavelet/WaveletReverse97.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace grk
{
namespace HWY_NAMESPACE
{
  static size_t num_lanes(void)
  {
    const HWY_FULL(int32_t) di;
    return Lanes(di);
  }

} // namespace HWY_NAMESPACE
} // namespace grk
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace grk
{
HWY_EXPORT(num_lanes);

static const float dwt_alpha = 1.586134342f; /*  12994 */
static const float dwt_beta = 0.052980118f; /*    434 */
static const float dwt_gamma = -0.882911075f; /*  -7233 */
static const float dwt_delta = -0.443506852f; /*  -3633 */
static const float K = 1.230174105f; /*  10078 */
static const float twice_invK = 1.625732422f;

struct Params97
{
  Params97(void) : dataPrev(nullptr), data(nullptr), len(0), lenMax(0) {}
  vec4f* dataPrev;
  vec4f* data;
  uint32_t len;
  uint32_t lenMax;
};

// Notes:
// 1. line buffer 0 offset == dwt->win_l.x0
// 2. dwt->memL and dwt->memH are only set for partial decode
static Params97 makeParams97(dwt_scratch<vec4f>* dwt, bool isBandL, bool step1)
{
  Params97 rc;
  // band_0 specifies absolute start of line buffer
  int64_t band_0 = isBandL ? dwt->win_l.x0 : dwt->win_h.x0;
  int64_t band_1 = isBandL ? dwt->win_l.x1 : dwt->win_h.x1;
  auto memPartial = isBandL ? dwt->memL : dwt->memH;
  int64_t parityOffset = isBandL ? dwt->parity : !dwt->parity;
  int64_t lenMax = isBandL ? (std::min<int64_t>)(dwt->sn, (int64_t)dwt->dn - parityOffset)
                           : (std::min<int64_t>)(dwt->dn, (int64_t)dwt->sn - parityOffset);
  if(lenMax < 0)
    lenMax = 0;
  assert(lenMax >= band_0);
  lenMax -= band_0;
  rc.data = memPartial ? memPartial : dwt->mem;

  assert(!memPartial || (dwt->win_l.x1 <= dwt->sn && dwt->win_h.x1 <= dwt->dn));
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

#ifdef __SSE__
void step1_sse_97(Params97 d, const __m128 c)
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

static void step1_97(const Params97& d, const float c)
{
#ifdef __SSE__
  step1_sse_97(d, _mm_set1_ps(c));
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
static void step2_sse_97(const Params97& d, __m128 c)
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

static void step2_97(const Params97& d, float c)
{
#ifdef __SSE__
  step2_sse_97(d, _mm_set1_ps(c));
#else

  float* dataPrev = (float*)d.dataPrev;
  float* data = (float*)d.data;

  const uint32_t imax = (std::min<uint32_t>)(d.len, d.lenMax);
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
void WaveletReverse::step_97(dwt_scratch<vec4f>* GRK_RESTRICT scratch)
{
  if((!scratch->parity && scratch->dn == 0 && scratch->sn <= 1) ||
     (scratch->parity && scratch->sn == 0 && scratch->dn >= 1))
    return;

  step1_97(makeParams97(scratch, true, true), K);
  step1_97(makeParams97(scratch, false, true), twice_invK);
  step2_97(makeParams97(scratch, true, false), dwt_delta);
  step2_97(makeParams97(scratch, false, false), dwt_gamma);
  step2_97(makeParams97(scratch, true, false), dwt_beta);
  step2_97(makeParams97(scratch, false, false), dwt_alpha);
}
void WaveletReverse::interleave_h_97(dwt_scratch<vec4f>* GRK_RESTRICT scratch,
                                     Buffer2dSimple<float> winL, Buffer2dSimple<float> winH,
                                     uint32_t remaining_height)
{
  float* GRK_RESTRICT scratchDataL = (float*)(scratch->mem + scratch->parity);
  uint32_t x0 = scratch->win_l.x0;
  uint32_t x1 = scratch->win_l.x1;
  const size_t vec4f_elts = vec4f::NUM_ELTS;
  for(uint32_t k = 0; k < 2; ++k)
  {
    auto src = (k == 0) ? winL.buf_ : winH.buf_;
    uint32_t stride = (k == 0) ? winL.stride_ : winH.stride_;
    if(remaining_height >= vec4f_elts && ((size_t)src & 0x0f) == 0 &&
       ((size_t)scratchDataL & 0x0f) == 0 && (stride & 0x0f) == 0)
    {
      /* Fast code path */
      for(uint32_t i = x0; i < x1; ++i, scratchDataL += vec4f_elts * 2)
      {
        uint32_t j = i;
        scratchDataL[0] = src[j];
        j += stride;
        scratchDataL[1] = src[j];
        j += stride;
        scratchDataL[2] = src[j];
        j += stride;
        scratchDataL[3] = src[j];
      }
    }
    else
    {
      /* Slow code path */
      for(uint32_t i = x0; i < x1; ++i, scratchDataL += vec4f_elts * 2)
      {
        uint32_t j = i;
        scratchDataL[0] = src[j];
        j += stride;
        if(remaining_height == 1)
          continue;
        scratchDataL[1] = src[j];
        j += stride;
        if(remaining_height == 2)
          continue;
        scratchDataL[2] = src[j];
        j += stride;
        if(remaining_height == 3)
          continue;
        scratchDataL[3] = src[j];
      }
    }
    scratchDataL = (float*)(scratch->mem + 1 - scratch->parity);
    x0 = scratch->win_h.x0;
    x1 = scratch->win_h.x1;
  }
}
void WaveletReverse::h_strip_97(dwt_scratch<vec4f>* GRK_RESTRICT scratch, const uint32_t resHeight,
                                Buffer2dSimple<float> winL, Buffer2dSimple<float> winH,
                                Buffer2dSimple<float> winDest)
{
  float* GRK_RESTRICT dest = winDest.buf_;
  const uint32_t strideDest = winDest.stride_;
  uint32_t j;
  const size_t vec4f_elts = vec4f::NUM_ELTS;
  for(j = 0; j < (resHeight & (uint32_t)(~(vec4f_elts - 1))); j += vec4f_elts)
  {
    interleave_h_97(scratch, winL, winH, resHeight - j);
    step_97(scratch);
    for(uint32_t k = 0; k < scratch->sn + scratch->dn; k++)
    {
      dest[k] = scratch->mem[k].val[0];
      dest[k + (size_t)strideDest] = scratch->mem[k].val[1];
      dest[k + (size_t)strideDest * 2] = scratch->mem[k].val[2];
      dest[k + (size_t)strideDest * 3] = scratch->mem[k].val[3];
    }
    winL.buf_ += winL.stride_ << 2;
    winH.buf_ += winH.stride_ << 2;
    dest += strideDest << 2;
  }
  if(j < resHeight)
  {
    interleave_h_97(scratch, winL, winH, resHeight - j);
    step_97(scratch);
    for(uint32_t k = 0; k < scratch->sn + scratch->dn; k++)
    {
      switch(resHeight - j)
      {
        case 3:
          dest[k + (strideDest << 1)] = scratch->mem[k].val[2];
        /* FALLTHRU */
        case 2:
          dest[k + strideDest] = scratch->mem[k].val[1];
        /* FALLTHRU */
        case 1:
          dest[k] = scratch->mem[k].val[0];
      }
    }
  }
}
bool WaveletReverse::h_97(uint8_t res, uint32_t num_threads, size_t dataLength,
                          dwt_scratch<vec4f>& GRK_RESTRICT scratch, const uint32_t resHeight,
                          Buffer2dSimple<float> winL, Buffer2dSimple<float> winH,
                          Buffer2dSimple<float> winDest)
{
  if(resHeight == 0)
    return true;
  if(num_threads == 1)
  {
    h_strip_97(&scratch, resHeight, winL, winH, winDest);
  }
  else
  {
    uint32_t numTasks = num_threads;
    if(resHeight < numTasks)
      numTasks = resHeight;
    const uint32_t incrPerJob = resHeight / numTasks;
    auto imageComponentFlow = ((DecompressScheduler*)scheduler_)->getImageComponentFlow(compno_);
    // return if no code blocks were decoded for this component
    if(!imageComponentFlow)
      return true;
    auto resFlow = imageComponentFlow->getResflow(res - 1);
    for(uint32_t j = 0; j < numTasks; ++j)
    {
      auto indexMin = j * incrPerJob;
      auto indexMax = (j < (numTasks - 1U) ? (j + 1U) * incrPerJob : resHeight) - indexMin;
      auto myhoriz = new dwt_scratch<vec4f>(scratch);
      if(!myhoriz->alloc(dataLength))
      {
        grklog.error("Out of memory");
        return false;
      }
      resFlow->waveletHoriz_->nextTask().work([this, myhoriz, indexMax, winL, winH, winDest] {
        h_strip_97(myhoriz, indexMax, winL, winH, winDest);
        delete myhoriz;
      });
      winL.incY_IN_PLACE(incrPerJob);
      winH.incY_IN_PLACE(incrPerJob);
      winDest.incY_IN_PLACE(incrPerJob);
    }
  }
  return true;
}
void WaveletReverse::interleave_v_97(dwt_scratch<vec4f>* GRK_RESTRICT scratch,
                                     Buffer2dSimple<float> winL, Buffer2dSimple<float> winH,
                                     uint32_t nb_elts_read)
{
  auto bi = scratch->mem + scratch->parity;
  auto band = winL.buf_ + scratch->win_l.x0 * winL.stride_;
  for(uint32_t i = scratch->win_l.x0; i < scratch->win_l.x1; ++i, bi += 2)
  {
    memcpy((float*)bi, band, nb_elts_read * sizeof(float));
    band += winL.stride_;
  }
  bi = scratch->mem + 1 - scratch->parity;
  band = winH.buf_ + scratch->win_h.x0 * winH.stride_;
  for(uint32_t i = scratch->win_h.x0; i < scratch->win_h.x1; ++i, bi += 2)
  {
    memcpy((float*)bi, band, nb_elts_read * sizeof(float));
    band += winH.stride_;
  }
}
void WaveletReverse::v_strip_97(dwt_scratch<vec4f>* GRK_RESTRICT scratch, const uint32_t resWidth,
                                const uint32_t resHeight, Buffer2dSimple<float> winL,
                                Buffer2dSimple<float> winH, Buffer2dSimple<float> winDest)
{
  uint32_t j;
  const size_t vec4f_elts = vec4f::NUM_ELTS;
  for(j = 0; j < (resWidth & (uint32_t)~(vec4f_elts - 1)); j += vec4f_elts)
  {
    interleave_v_97(scratch, winL, winH, vec4f_elts);
    step_97(scratch);
    auto destPtr = winDest.buf_;
    for(uint32_t k = 0; k < resHeight; ++k)
    {
      memcpy(destPtr, scratch->mem + k, sizeof(vec4f));
      destPtr += winDest.stride_;
    }
    winL.buf_ += vec4f_elts;
    winH.buf_ += vec4f_elts;
    winDest.buf_ += vec4f_elts;
  }
  if(j < resWidth)
  {
    j = resWidth & (vec4f_elts - 1);
    interleave_v_97(scratch, winL, winH, j);
    step_97(scratch);
    auto destPtr = winDest.buf_;
    for(uint32_t k = 0; k < resHeight; ++k)
    {
      memcpy(destPtr, scratch->mem + k, j * sizeof(float));
      destPtr += winDest.stride_;
    }
  }
}
bool WaveletReverse::v_97(uint8_t res, uint32_t num_threads, size_t dataLength,
                          dwt_scratch<vec4f>& GRK_RESTRICT scratch, const uint32_t resWidth,
                          const uint32_t resHeight, Buffer2dSimple<float> winL,
                          Buffer2dSimple<float> winH, Buffer2dSimple<float> winDest)
{
  if(resWidth == 0)
    return true;
  if(num_threads == 1)
  {
    v_strip_97(&scratch, resWidth, resHeight, winL, winH, winDest);
  }
  else
  {
    auto numTasks = num_threads;
    if(resWidth < numTasks)
      numTasks = resWidth;
    const auto incrPerJob = resWidth / numTasks;
    auto imageComponentFlow = ((DecompressScheduler*)scheduler_)->getImageComponentFlow(compno_);
    // return if no code blocks were decoded for this component
    if(!imageComponentFlow)
      return true;
    auto resFlow = imageComponentFlow->getResflow(res - 1);
    for(uint32_t j = 0; j < numTasks; j++)
    {
      auto indexMin = j * incrPerJob;
      auto indexMax = (j < (numTasks - 1U) ? (j + 1U) * incrPerJob : resWidth) - indexMin;
      auto myvert = new dwt_scratch<vec4f>(scratch);
      if(!myvert->alloc(dataLength))
      {
        grklog.error("Out of memory");
        delete myvert;
        return false;
      }
      resFlow->waveletVert_->nextTask().work(
          [this, myvert, resHeight, indexMax, winL, winH, winDest] {
            v_strip_97(myvert, indexMax, resHeight, winL, winH, winDest);
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
bool WaveletReverse::tile_97(void)
{
  if(numres_ == 1U)
    return true;

  auto tr = tilec_->resolutions_;
  auto buf = tilec_->getWindow();
  uint32_t resWidth = tr->width();
  uint32_t resHeight = tr->height();

  size_t dataLength = max_resolution(tr, numres_);
  if(!horiz97.alloc(dataLength))
  {
    grklog.error("tile_97: out of memory");
    return false;
  }
  vert97.mem = horiz97.mem;
  uint32_t num_threads = (uint32_t)ExecSingleton::num_threads();
  for(uint8_t res = 1; res < numres_; ++res)
  {
    horiz97.sn = resWidth;
    vert97.sn = resHeight;
    ++tr;
    resWidth = tr->width();
    resHeight = tr->height();
    if(resWidth == 0 || resHeight == 0)
      continue;
    horiz97.dn = resWidth - horiz97.sn;
    horiz97.parity = tr->x0 & 1;
    horiz97.win_l = Line32(0, horiz97.sn);
    horiz97.win_h = Line32(0, horiz97.dn);
    auto winSplitL = buf->getResWindowBufferSplitSimpleF(res, SPLIT_L);
    auto winSplitH = buf->getResWindowBufferSplitSimpleF(res, SPLIT_H);
    if(!h_97(res, num_threads, dataLength, horiz97, vert97.sn,
             buf->getResWindowBufferSimpleF((uint8_t)(res - 1U)),
             buf->getBandWindowBufferPaddedSimpleF(res, BAND_ORIENT_HL), winSplitL))
      return false;
    if(!h_97(res, num_threads, dataLength, horiz97, resHeight - vert97.sn,
             buf->getBandWindowBufferPaddedSimpleF(res, BAND_ORIENT_LH),
             buf->getBandWindowBufferPaddedSimpleF(res, BAND_ORIENT_HH), winSplitH))
      return false;
    vert97.dn = resHeight - vert97.sn;
    vert97.parity = tr->y0 & 1;
    vert97.win_l = Line32(0, vert97.sn);
    vert97.win_h = Line32(0, vert97.dn);
    if(!v_97(res, num_threads, dataLength, vert97, resWidth, resHeight, winSplitL, winSplitH,
             buf->getResWindowBufferSimpleF(res)))
      return false;
  }

  return true;
}

} // namespace grk
#endif
