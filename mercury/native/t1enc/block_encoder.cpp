/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

#include <algorithm>
#include <cstring>

#include "block_encoder.h"
#include "t1_luts.h"
#include "mqc_enc_inl.h"

namespace mercury_native::t1enc
{


static const double dwt_norms[4][32] = {
    {1.0000000000000000,      1.4999999999999998,      2.7500000000000000,
     5.3750000000000000,      10.6875000000000000,     21.3437499999999964,
     42.6718750000000000,     85.3359375000000000,     170.6679687499999716,
     341.3339843749999432,    682.6669921875000000,    1365.3334960937502274,
     2730.6667480468750000,   5461.3333740234384095,   10922.6666870117187500,
     21845.3333435058630130,  43690.6666717529224115,  87381.3333358764793957,
     174762.6666664243675768, 349525.3333335536299273, 699050.6666691404534504,
     699050.6666691404534504, 699050.6666691404534504, 699050.6666691404534504,
     699050.6666691404534504, 699050.6666691404534504, 699050.6666691404534504,
     699050.6666691404534504, 699050.6666691404534504, 699050.6666691404534504,
     699050.6666691404534504, 699050.6666691404534504},
    {1.0383279828647594,      1.5922174003571246,      2.9196599224053479,
     5.7027825239636316,      11.3367128008477938,     22.6389242077053332,
     45.2605882343629773,     90.5125451903222995,     181.0207745928834697,
     362.0393912733142088,    724.0777035880804533,    1448.1548676965965115,
     2896.3094656533749003,   5792.6187964368355097,   11585.2375254387134191,
     23170.4750171599480382,  46340.9500174611603143,  92681.9000257543521002,
     185363.8000489076948725, 370727.6000933262985200, 370727.6000933262985200,
     370727.6000933262985200, 370727.6000933262985200, 370727.6000933262985200,
     370727.6000933262985200, 370727.6000933262985200, 370727.6000933262985200,
     370727.6000933262985200, 370727.6000933262985200, 370727.6000933262985200,
     370727.6000933262985200, 370727.6000933262985200},
    {1.0383279828647594,      1.5922174003571246,      2.9196599224053479,
     5.7027825239636316,      11.3367128008477938,     22.6389242077053332,
     45.2605882343629773,     90.5125451903222995,     181.0207745928834697,
     362.0393912733142088,    724.0777035880804533,    1448.1548676965965115,
     2896.3094656533749003,   5792.6187964368355097,   11585.2375254387134191,
     23170.4750171599480382,  46340.9500174611603143,  92681.9000257543521002,
     185363.8000489076948725, 370727.6000933262985200, 370727.6000933262985200,
     370727.6000933262985200, 370727.6000933262985200, 370727.6000933262985200,
     370727.6000933262985200, 370727.6000933262985200, 370727.6000933262985200,
     370727.6000933262985200, 370727.6000933262985200, 370727.6000933262985200,
     370727.6000933262985200, 370727.6000933262985200},
    {0.7187500000000000,      0.9218749999999999,      1.5859375000000002,
     3.0429687500000004,      6.0214843750000000,      12.0107421875000000,
     24.0053710937500000,     48.0026855468749929,     96.0013427734375000,
     192.0006713867187784,    384.0003356933593750,    768.0001678466796875,
     1536.0000839233398438,   3072.0000419616699219,   6144.0000209808340514,
     12288.0000104904156615,  24576.0000052452123782,  49152.0000022649765015,
     98304.0000015729165170,  196607.9999978125561029, 196607.9999978125561029,
     196607.9999978125561029, 196607.9999978125561029, 196607.9999978125561029,
     196607.9999978125561029, 196607.9999978125561029, 196607.9999978125561029,
     196607.9999978125561029, 196607.9999978125561029, 196607.9999978125561029,
     196607.9999978125561029}};

static const double dwt_norms_real[4][32] = {
    {1.000000,       1.965907,       4.122410,       8.416744,       16.935572,      33.924927,
     67.877165,      135.768047,     271.542961,     543.089357,     1086.180430,    2172.361720,
     4344.723869,    8689.447952,    17378.896012,   34757.792077,   69515.584181,   139031.168375,
     278062.336757,  556124.673518,  1112249.347034, 1112249.347034, 1112249.347034, 1112249.347034,
     1112249.347034, 1112249.347034, 1112249.347034, 1112249.347034, 1112249.347034, 1112249.347034,
     1112249.347034, 1112249.347034},
    {2.022573,       3.993625,       8.366735,       17.068231,      34.333452,      68.770403,
     137.593326,     275.213023,     550.439247,     1100.885098,    2201.773497,    4403.548644,
     8807.098114,    17614.196641,   35228.393489,   70456.787082,   140913.574215,  281827.148456,
     563654.296924,  1127308.593852, 1127308.593852, 1127308.593852, 1127308.593852, 1127308.593852,
     1127308.593852, 1127308.593852, 1127308.593852, 1127308.593852, 1127308.593852, 1127308.593852,
     1127308.593852, 1127308.593852},
    {2.022573,       3.993625,       8.366735,       17.068231,      34.333452,      68.770403,
     137.593326,     275.213023,     550.439247,     1100.885098,    2201.773497,    4403.548644,
     8807.098114,    17614.196641,   35228.393489,   70456.787082,   140913.574215,  281827.148456,
     563654.296924,  1127308.593852, 1127308.593852, 1127308.593852, 1127308.593852, 1127308.593852,
     1127308.593852, 1127308.593852, 1127308.593852, 1127308.593852, 1127308.593852, 1127308.593852,
     1127308.593852, 1127308.593852},
    {2.080872,       3.868863,       8.317022,       17.201929,      34.746896,      69.675396,
     139.443144,     278.932688,     557.888608,     1115.788836,    2231.583482,    4463.169870,
     8926.341193,    17852.683111,   35705.366586,   71410.733354,   142821.466798,  285642.933641,
     571285.867305,  1142571.734621, 1142571.734621, 1142571.734621, 1142571.734621, 1142571.734621,
     1142571.734621, 1142571.734621, 1142571.734621, 1142571.734621, 1142571.734621, 1142571.734621,
     1142571.734621, 1142571.734621}};

static inline int16_t getnmsedec_sig(uint32_t x, uint32_t bitpos)
{
  if(bitpos > 0)
    return lut_nmsedec_sig[(x >> (bitpos)) & ((1 << T1_NMSEDEC_BITS) - 1)];

  return lut_nmsedec_sig0[x & ((1 << T1_NMSEDEC_BITS) - 1)];
}
static inline int16_t getnmsedec_ref(uint32_t x, uint32_t bitpos)
{
  if(bitpos > 0)
    return lut_nmsedec_ref[(x >> (bitpos)) & ((1 << T1_NMSEDEC_BITS) - 1)];

  return lut_nmsedec_ref0[x & ((1 << T1_NMSEDEC_BITS) - 1)];
}

static inline void update_flags(grk_flag* flagsPtr, uint32_t ci, uint32_t s, uint32_t stride,
                                uint32_t vsc)
{
  UPDATE_FLAGS(*flagsPtr, flagsPtr, ci, s, stride, vsc);
}

double BlockEncoder::getnorm(uint32_t level, uint8_t orientation, bool reversible)
{
  assert(orientation <= 3);
  if(orientation == 0 && level > 9)
    level = 9;
  else if(orientation > 0 && level > 8)
    level = 8;

  return reversible ? dwt_norms[orientation][level] : dwt_norms_real[orientation][level];
}
/* <summary>                */
/* Get norm of 5-3 wavelet. */
/* </summary>               */
double BlockEncoder::getnorm_53(uint32_t level, uint8_t orientation)
{
  return getnorm(level, orientation, true);
}
/* <summary>                */
/* Get norm of 9-7 wavelet. */
/* </summary>               */
double BlockEncoder::getnorm_97(uint32_t level, uint8_t orientation)
{
  return getnorm(level, orientation, false);
}
uint16_t BlockEncoder::getFlagsStride(void)
{
  return w_ + 2U;
}
uint16_t BlockEncoder::getFlagsHeight(void)
{
  return (uint16_t)((h_ + 3U) >> 2);
}

void BlockEncoder::initFlags(void)
{
  uint16_t flagsStride = getFlagsStride();
  uint16_t flagsHeight = getFlagsHeight();
  memset(flags_, 0, flagsLen_ * sizeof(grk_flag));

  // Precompute the magic value used to stop passes.
  const uint32_t stopValue = T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3;

  // Initialize the top boundary.
  grk_flag* p = flags_;
  std::fill_n(p, flagsStride, stopValue);

  // Initialize the bottom boundary.
  p = flags_ + (flagsHeight + 1) * flagsStride;
  std::fill_n(p, flagsStride, stopValue);

  // Handle the partial row case if height is not a multiple of 4.
  if(h_ & 3)
  {
    uint32_t v = 0;

    // Use a lookup table for the partial row flags to eliminate branching.
    static const uint32_t partialRowFlags[4] = {0, T1_PI_1 | T1_PI_2 | T1_PI_3, T1_PI_2 | T1_PI_3,
                                                T1_PI_3};
    v = partialRowFlags[h_ & 3];

    // Initialize the partial row.
    p = flags_ + flagsHeight * flagsStride;
    std::fill_n(p, flagsStride, v);
  }
}

double BlockEncoder::getwmsedec(int32_t nmsedec, uint16_t compno, uint32_t level, uint8_t orientation,
                              int8_t bpno, uint32_t qmfbid, double stepsize,
                              const double* mct_norms, uint32_t mct_numcomps)
{
  double w1 = 1, w2, wmsedec;

  if(mct_norms && (compno < mct_numcomps))
    w1 = mct_norms[compno];

  if(qmfbid == 1)
    w2 = getnorm_53(level, orientation);
  else
    w2 = getnorm_97(level, orientation);

  wmsedec = w1 * w2 * stepsize * (1 << bpno);
  wmsedec *= wmsedec * nmsedec / 8192.0;

  return wmsedec;
}

#define enc_sigpass_step_macro(datap, ci)                                                   \
  {                                                                                          \
    if((*flagsPtr & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))) == 0U &&                         \
       (*flagsPtr & (T1_SIGMA_NEIGHBOURS << (ci))) != 0U)                                    \
    {                                                                                        \
      uint8_t ctxno = GETCTXNO_ZC(mqc, *flagsPtr >> (ci));                                   \
      uint8_t v = !!(smr_abs(*(datap)) & (uint32_t)one);                                     \
      curctx = mqc->ctxs + ctxno;                                                            \
      mqc_encode_macro(mqc, curctx, a, c, ct, v);                                            \
      if(v)                                                                                  \
      {                                                                                      \
        uint32_t lu = getctxtno_sc_or_spb_index(*flagsPtr, flagsPtr[-1], flagsPtr[1], ci);   \
        ctxno = lut_ctxno_sc[lu];                                                            \
        v = smr_sign(*(datap));                                                              \
        nmsedec += getnmsedec_sig((uint32_t)smr_abs(*(datap)), (uint32_t)bpno);              \
        curctx = mqc->ctxs + ctxno;                                                          \
        mqc_encode_macro(mqc, curctx, a, c, ct, v ^ lut_spb[lu]);                            \
        update_flags(flagsPtr, ci, v, flagsStride, 0);                                       \
      }                                                                                      \
      *flagsPtr |= T1_PI_THIS << (ci);                                                       \
    }                                                                                        \
  }
int32_t BlockEncoder::enc_sigpass(int8_t bpno)
{
  const int32_t one = 1 << (bpno + T1_NMSEDEC_FRACBITS);
  const uint32_t width = w_;
  const uint32_t height = h_;
  const uint32_t stride = stride_;
  const uint32_t flagsStride = width + 2;
  auto flagsPtr = flags_ + 1 + flagsStride;
  auto mqc = &(coder);
  PUSH_MQC();
  int32_t nmsedec = 0;
  uint32_t k;
  for(k = 0; k < (height & ~3U); k += 4)
  {
    const int32_t* row0 = uncompressedData_ + k * stride;
    const int32_t* row1 = row0 + stride;
    const int32_t* row2 = row1 + stride;
    const int32_t* row3 = row2 + stride;
    for(uint32_t i = 0; i < width; ++i, ++flagsPtr)
    {
      if(*flagsPtr == 0U)
        continue;
      enc_sigpass_step_macro(row0 + i, 0);
      enc_sigpass_step_macro(row1 + i, 3);
      enc_sigpass_step_macro(row2 + i, 6);
      enc_sigpass_step_macro(row3 + i, 9);
    }
    flagsPtr += 2;
  }
  if(k < height)
  {
    for(uint32_t i = 0; i < width; ++i, ++flagsPtr)
    {
      if(*flagsPtr == 0U)
        continue;
      auto pdata = uncompressedData_ + k * stride + i;
      for(uint32_t j = k; j < height; ++j, pdata += stride)
        enc_sigpass_step_macro(pdata, 3U * (j - k));
    }
  }
  POP_MQC();
  return nmsedec;
}
#define enc_refpass_step_macro(datap, ci)                                     \
  {                                                                           \
    uint32_t const shift_flags = (*flagsPtr >> (ci));                         \
    if((shift_flags & (T1_SIGMA_THIS | T1_PI_THIS)) == T1_SIGMA_THIS)         \
    {                                                                         \
      uint8_t ctxno = GETCTXNO_MAG(shift_flags);                              \
      nmsedec += getnmsedec_ref((uint32_t)smr_abs(*(datap)), (uint32_t)bpno); \
      uint8_t v = !!(smr_abs(*(datap)) & (uint32_t)one);                      \
      curctx = mqc->ctxs + ctxno;                                             \
      mqc_encode_macro(mqc, curctx, a, c, ct, v);                             \
      *flagsPtr |= T1_MU_THIS << (ci);                                        \
    }                                                                         \
  }
int32_t BlockEncoder::enc_refpass(int8_t bpno)
{
  const int32_t one = 1 << (bpno + T1_NMSEDEC_FRACBITS);
  const uint32_t width = w_;
  const uint32_t height = h_;
  const uint32_t stride = stride_;
  auto flagsPtr = flags_ + 1 + (width + 2);
  auto mqc = &(coder);
  PUSH_MQC();
  int32_t nmsedec = 0;
  uint32_t k;
  for(k = 0; k < (height & ~3U); k += 4)
  {
    const int32_t* row0 = uncompressedData_ + k * stride;
    const int32_t* row1 = row0 + stride;
    const int32_t* row2 = row1 + stride;
    const int32_t* row3 = row2 + stride;
    for(uint32_t i = 0; i < width; ++i, ++flagsPtr)
    {
      // none significant, or all coded by the significance pass
      if((*flagsPtr & (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13)) == 0)
        continue;
      if((*flagsPtr & (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3)) ==
         (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3))
        continue;
      enc_refpass_step_macro(row0 + i, 0);
      enc_refpass_step_macro(row1 + i, 3);
      enc_refpass_step_macro(row2 + i, 6);
      enc_refpass_step_macro(row3 + i, 9);
    }
    flagsPtr += 2;
  }
  if(k < height)
  {
    for(uint32_t i = 0; i < width; ++i, ++flagsPtr)
    {
      if((*flagsPtr & (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13)) == 0)
        continue;
      for(uint32_t j = k; j < height; ++j)
        enc_refpass_step_macro(uncompressedData_ + j * stride + i, 3 * (j - k));
    }
  }
  POP_MQC();
  return nmsedec;
}
int32_t BlockEncoder::enc_clnpass(int8_t bpno)
{
  const int32_t one = 1 << (bpno + T1_NMSEDEC_FRACBITS);
  const uint32_t width = w_;
  const uint32_t height = h_;
  const uint32_t stride = stride_;
  const uint32_t flagsStride = width + 2;
  auto mqc = &coder;
  PUSH_MQC();
  int32_t nmsedec = 0;
  auto flagsPtr = flags_ + 1 + flagsStride;
  uint32_t k;
  for(k = 0; k < (height & ~3U); k += 4, flagsPtr += 2)
  {
    const int32_t* stripe = uncompressedData_ + k * stride;
    for(uint32_t i = 0; i < width; ++i, ++flagsPtr)
    {
      uint32_t agg = !(*flagsPtr);
      uint8_t runlen = 0;
      if(agg)
      {
        for(; runlen < 4; ++runlen)
        {
          if(smr_abs(stripe[runlen * stride + i]) & (uint32_t)one)
            break;
        }
        uint8_t ctxno = T1_CTXNO_AGG;
        curctx = mqc->ctxs + ctxno;
        mqc_encode_macro(mqc, curctx, a, c, ct, runlen != 4);
        if(runlen == 4)
          continue;
        ctxno = T1_CTXNO_UNI;
        curctx = mqc->ctxs + ctxno;
        mqc_encode_macro(mqc, curctx, a, c, ct, runlen >> 1);
        mqc_encode_macro(mqc, curctx, a, c, ct, runlen & 1);
      }
      auto datap = stripe + runlen * stride + i;
      const uint32_t check = (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13 | T1_PI_0 |
                              T1_PI_1 | T1_PI_2 | T1_PI_3);
      bool stage_2 = true;
      if((*flagsPtr & check) == check)
      {
        switch(runlen)
        {
          case 0:
            *flagsPtr &= ~(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);
            break;
          case 1:
            *flagsPtr &= ~(T1_PI_1 | T1_PI_2 | T1_PI_3);
            break;
          case 2:
            *flagsPtr &= ~(T1_PI_2 | T1_PI_3);
            break;
          case 3:
            *flagsPtr &= ~(T1_PI_3);
            break;
          default:
            stage_2 = false;
            break;
        }
      }
      for(uint8_t ci = 3 * runlen; ci < 3 * 4 && stage_2; ci += 3)
      {
        bool goto_PARTIAL = false;
        if((agg != 0) && (ci == 3 * runlen))
          goto_PARTIAL = true;
        else if(!(*flagsPtr & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))))
        {
          uint8_t ctxno = GETCTXNO_ZC(mqc, *flagsPtr >> (ci));
          curctx = mqc->ctxs + ctxno;
          uint32_t v = !!(smr_abs(*datap) & (uint32_t)one);
          mqc_encode_macro(mqc, curctx, a, c, ct, v);
          goto_PARTIAL = v;
        }
        if(goto_PARTIAL)
        {
          uint32_t lu = getctxtno_sc_or_spb_index(*flagsPtr, *(flagsPtr - 1), *(flagsPtr + 1), ci);
          nmsedec += getnmsedec_sig((uint32_t)smr_abs(*datap), (uint32_t)bpno);
          uint8_t ctxno = lut_ctxno_sc[lu];
          curctx = mqc->ctxs + ctxno;
          uint8_t v = smr_sign(*datap);
          uint8_t spb = lut_spb[lu];
          mqc_encode_macro(mqc, curctx, a, c, ct, v ^ spb);
          update_flags(flagsPtr, ci, v, flagsStride, 0);
        }
        *flagsPtr &= ~(T1_PI_THIS << (ci));
        datap += stride;
      }
    }
  }
  if(k < height)
  {
    uint8_t runlen = 0;
    for(uint32_t i = 0; i < width; ++i, ++flagsPtr)
    {
      auto datap = uncompressedData_ + (k + runlen) * stride + i;
      const uint32_t check = (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13 | T1_PI_0 |
                              T1_PI_1 | T1_PI_2 | T1_PI_3);
      bool stage_2 = true;
      if((*flagsPtr & check) == check)
      {
        switch(runlen)
        {
          case 0:
            *flagsPtr &= ~(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);
            break;
          case 1:
            *flagsPtr &= ~(T1_PI_1 | T1_PI_2 | T1_PI_3);
            break;
          case 2:
            *flagsPtr &= ~(T1_PI_2 | T1_PI_3);
            break;
          case 3:
            *flagsPtr &= ~(T1_PI_3);
            break;
          default:
            stage_2 = false;
            break;
        }
      }
      const uint8_t lim = 3 * (uint8_t)(height - k);
      for(uint8_t ci = 3 * runlen; ci < lim && stage_2; ci += 3)
      {
        bool goto_PARTIAL = false;
        if(!(*flagsPtr & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))))
        {
          uint8_t ctxno = GETCTXNO_ZC(mqc, *flagsPtr >> (ci));
          curctx = mqc->ctxs + ctxno;
          uint8_t v = !!(smr_abs(*datap) & (uint32_t)one);
          mqc_encode_macro(mqc, curctx, a, c, ct, v);
          goto_PARTIAL = v;
        }
        if(goto_PARTIAL)
        {
          uint32_t lu = getctxtno_sc_or_spb_index(*flagsPtr, *(flagsPtr - 1), *(flagsPtr + 1), ci);
          nmsedec += getnmsedec_sig((uint32_t)smr_abs(*datap), (uint32_t)bpno);
          uint8_t ctxno = lut_ctxno_sc[lu];
          curctx = mqc->ctxs + ctxno;
          uint8_t v = smr_sign(*datap);
          uint8_t spb = lut_spb[lu];
          mqc_encode_macro(mqc, curctx, a, c, ct, v ^ spb);
          update_flags(flagsPtr, ci, v, flagsStride, 0);
        }
        *flagsPtr &= ~(T1_PI_THIS << (ci));
        datap += stride;
      }
    }
  }

  POP_MQC();
  return nmsedec;
}

// grok's PrecinctCodeblockStorage minimum, and the two bytes before the stream the coder writes
static const size_t MIN_STREAM_BYTES = 4096;
static const size_t STREAM_PAD = 2;
static const uint8_t CLEANUP_PASS = 2;

void BlockEncoder::alloc(uint16_t width, uint16_t height)
{
  w_ = width;
  h_ = height;
  stride_ = width;
  flagsLen_ = (uint32_t)(getFlagsHeight() + 2U) * getFlagsStride();
  if(flagStorage_.size() < flagsLen_)
    flagStorage_.resize(flagsLen_);
  flags_ = flagStorage_.data();
  initFlags();
  size_t streamBytes =
      std::max<size_t>((size_t)width * height * sizeof(uint32_t), MIN_STREAM_BYTES) + STREAM_PAD;
  if(stream_.size() < streamBytes)
    stream_.resize(streamBytes);
  stream_[0] = 0;
  stream_[1] = 0;
}

int32_t BlockEncoder::encode(const int32_t* samples, uint16_t width, uint16_t height,
                             uint32_t maxMagnitude, uint8_t orientation, uint16_t compno,
                             uint8_t level, double stepsize, const double* mctNorms,
                             uint16_t mctNumcomps, const EncodedPasses& out)
{
  alloc(width, height);
  uncompressedData_ = samples;
  uint8_t numbps = 0;
  if(maxMagnitude)
  {
    uint8_t bits = (uint8_t)(32 - __builtin_clz(maxMagnitude));
    numbps = bits <= T1_NMSEDEC_FRACBITS ? 0 : (uint8_t)(bits - T1_NMSEDEC_FRACBITS);
  }
  *out.numbps = numbps;
  if(numbps == 0)
    return 0;

  uint8_t* data = stream_.data() + STREAM_PAD;
  coder.lut_ctxno_zc_orient = lut_ctxno_zc + (orientation << 9);
  coder.resetstates();
  coder.init_enc(data);

  double cumulative = 0;
  int8_t bpno = (int8_t)(numbps - 1);
  uint8_t passtype = CLEANUP_PASS;
  uint8_t passno = 0;
  for(; bpno >= 0; ++passno)
  {
    int32_t nmsedec;
    if(passtype == 0)
      nmsedec = enc_sigpass(bpno);
    else if(passtype == 1)
      nmsedec = enc_refpass(bpno);
    else
      nmsedec = enc_clnpass(bpno);
    cumulative +=
        getwmsedec(nmsedec, compno, level, orientation, bpno, 0, stepsize, mctNorms, mctNumcomps);
    out.distortions[passno] = cumulative;
    bool lastPass = passtype == CLEANUP_PASS && bpno == 0;
    if(lastPass)
    {
      coder.flush_enc();
      out.rates[passno] = coder.numbytes_enc();
    }
    else
    {
      uint16_t extraBytes = coder.ct < 5 ? 6 : 5;
      out.rates[passno] = (uint16_t)(coder.numbytes_enc() + extraBytes);
    }
    out.terminated[passno] = lastPass ? 1 : 0;
    if(++passtype == 3)
    {
      passtype = 0;
      bpno--;
    }
  }

  // grok's post pass: rates never decrease, and no pass ends on 0xFF
  uint16_t laterRate = coder.numbytes_enc();
  for(int32_t p = passno - 1; p >= 0; --p)
  {
    if(out.rates[p] > laterRate)
      out.rates[p] = laterRate;
    else
      laterRate = out.rates[p];
  }
  for(int32_t p = 0; p < passno; ++p)
  {
    if(data[out.rates[p] - 1] == 0xFF)
      out.rates[p]--;
  }
  *out.bytes = data;
  return passno;
}

static const double mct_norms_irrev[] = {std::sqrt(3.0000), std::sqrt(3.2584), std::sqrt(2.4755)};

} // namespace mercury_native::t1enc

#if defined(_WIN32)
#define MERCURY_T1ENC_API __declspec(dllexport)
#else
#define MERCURY_T1ENC_API __attribute__((visibility("default")))
#endif

// grok's Quantizer::generate for one irreversible band, in its band order
extern "C" MERCURY_T1ENC_API void mercury_t1_band_step(uint8_t numresolutions,
                                                       uint8_t max_bit_depth, uint8_t bandno,
                                                       double stepScale, uint8_t* exponent,
                                                       uint16_t* mantissa)
{
  using namespace mercury_native::t1enc;
  const bool isReversible = false;
  auto floorlog2Step = [](uint32_t step) { return 31 - __builtin_clz(step); };
  uint8_t resno = (bandno == 0) ? 0 : (uint8_t)(((bandno - 1) / 3 + 1));
  uint8_t orient = (uint8_t)((bandno == 0) ? 0 : ((bandno - 1) % 3 + 1));
  uint8_t level = (uint8_t)(numresolutions - 1 - resno);
  uint32_t gain =
      (!isReversible) ? 0 : ((orient == 0) ? 0 : (((orient == 1) || (orient == 2)) ? 1 : 2));

  double stepsize = 1.0;
  if(!isReversible)
    stepsize = stepScale * (1 << (gain)) / BlockEncoder::getnorm(level, orient, false);
  uint32_t step = (uint32_t)floor(stepsize * 8192.0);
  int32_t p, n;
  p = floorlog2Step(step) - 13;
  n = 11 - floorlog2Step(step);
  uint32_t mant = (n < 0 ? step >> -n : step << n) & 0x7ff;
  int32_t expn = (int32_t)(max_bit_depth + gain) - p;
  assert(expn >= 0);
  *exponent = (uint8_t)expn;
  *mantissa = (uint16_t)mant;
}

// mct: the samples are a component of grok's irreversible colour transform
extern "C" MERCURY_T1ENC_API int32_t mercury_t1_encode_block(
    const int32_t* samples, int32_t num_cols, int32_t num_rows, uint32_t max_magnitude,
    int32_t orientation, int32_t compno, int32_t level, double stepsize, int32_t mct,
    const uint8_t** coded_data, uint16_t* pass_rates, double* pass_distortions,
    uint8_t* pass_terminated, int32_t* numbps)
{
  using namespace mercury_native::t1enc;
  thread_local BlockEncoder encoder;
  const uint16_t mctComponents = 3;
  return encoder.encode(samples, (uint16_t)num_cols, (uint16_t)num_rows, max_magnitude,
                        (uint8_t)orientation, (uint16_t)compno, (uint8_t)level, stepsize,
                        mct ? mct_norms_irrev : nullptr, mct ? mctComponents : 0,
                        {pass_rates, pass_distortions, pass_terminated, coded_data, numbps});
}
