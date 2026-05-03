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

#include "CodeblockCompress.h"
#include "CodeblockDecompress.h"
#include "t1_luts.h"
#include "BlockCoder.h"

namespace grk::t1
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

BlockCoder::BlockCoder(bool isCompressor, uint16_t maxCblkW, uint16_t maxCblkH,
                       uint32_t cacheStrategy)
    : cacheStrategy_(cacheStrategy), coder(cacheAll(cacheStrategy_)), w_(0), stride_(0), h_(0),
      uncompressedData_(nullptr), flags_(nullptr), flagsLen_(0), compressor(isCompressor)
{
  if(!isCompressor)
  {
    if(!cacheAll(cacheStrategy_))
    {
      alloc(maxCblkW, maxCblkH);
    }
    else
    {
      // only do this once, in constructor
      coder.resetstates();
    }
  }
}
BlockCoder::~BlockCoder()
{
  grk_aligned_free(flags_);
}
bool BlockCoder::cacheAll(uint32_t strategy)
{
  return (strategy & GRK_TILE_CACHE_ALL) == GRK_TILE_CACHE_ALL;
}
void BlockCoder::print(void)
{
  printf("Block coder state: 0x%x 0x%x 0x%x\n", coder.c, coder.a, coder.ct);
}

bool BlockCoder::alloc(uint16_t width, uint16_t height)
{
  // uncompressed data
  if(width == 0 || height == 0)
  {
    grklog.error("Unable to allocate memory for degenerate code block of dimensions %ux%u", width,
                 height);
    return false;
  }
  if(!uncompressedBuf_.alloc2d(width, width, height, !compressor))
    return false;
  // clear buffer since we reuse the block coder from a pool of coders
  if(!compressor && !cacheAll(cacheStrategy_))
    uncompressedBuf_.clear();
  uncompressedData_ = uncompressedBuf_.getBuffer();
  stride_ = (uint16_t)uncompressedBuf_.getStride();

  // if coder is cached and height and width haven't change, then return immediately
  if(cacheAll(cacheStrategy_) && w_ == width && h_ == height)
    return true;

  w_ = width;
  h_ = height;

  // flags
  uint32_t newflagssize = ((h_ + 3U) / 4U + 2U) * getFlagsStride();
  if(newflagssize > flagsLen_)
  {
    grk::grk_aligned_free(flags_);
    flags_ = (grk_flag*)grk::grk_aligned_malloc(newflagssize * sizeof(grk_flag));
    if(!flags_)
    {
      grklog.error("Out of memory");
      return false;
    }
  }
  flagsLen_ = newflagssize;
  initFlags();

  return true;
}

double BlockCoder::getnorm(uint32_t level, uint8_t orientation, bool reversible)
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
double BlockCoder::getnorm_53(uint32_t level, uint8_t orientation)
{
  return getnorm(level, orientation, true);
}
/* <summary>                */
/* Get norm of 9-7 wavelet. */
/* </summary>               */
double BlockCoder::getnorm_97(uint32_t level, uint8_t orientation)
{
  return getnorm(level, orientation, false);
}
int32_t* BlockCoder::getUncompressedData(void)
{
  return uncompressedBuf_.getBuffer();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void BlockCoder::dec_sigpass_raw(int8_t bpno, int32_t cblksty)
{
  auto flagsPtr = flags_ + 1 + (w_ + 2);
  auto dataPtr = uncompressedData_;
  const int32_t one = 1 << bpno;
  const int32_t half = one >> 1;
  const int32_t oneplushalf = one | half;

  uint32_t vscEnabled = cblksty & GRK_CBLKSTY_VSC;

  // Main processing loop: process rows in blocks of 4.
  for(uint32_t k = 0; k < (h_ & ~3U); k += 4, flagsPtr += 2, dataPtr += 3 * w_)
  {
    for(uint16_t i = 0; i < w_; ++i, ++flagsPtr, ++dataPtr)
    {
      if(*flagsPtr != 0)
      {
        dec_sigpass_step_raw(flagsPtr, dataPtr, oneplushalf, vscEnabled, 0U);
        dec_sigpass_step_raw(flagsPtr, dataPtr + w_, oneplushalf, false, 3U);
        dec_sigpass_step_raw(flagsPtr, dataPtr + 2 * w_, oneplushalf, false, 6U);
        dec_sigpass_step_raw(flagsPtr, dataPtr + 3 * w_, oneplushalf, false, 9U);
      }
    }
  }

  // Handle remaining rows (less than 4).
  uint32_t remainingRows = h_ & 3U;
  if(remainingRows > 0)
  {
    for(uint16_t i = 0; i < w_; ++i, ++flagsPtr, ++dataPtr)
    {
      for(uint16_t j = 0; j < remainingRows; ++j)
      {
        dec_sigpass_step_raw(flagsPtr, dataPtr + j * w_, oneplushalf, vscEnabled, 3 * j);
      }
    }
  }
}

inline void BlockCoder::dec_sigpass_step_raw(grk_flag* flagsPtr, int32_t* datap,
                                             int32_t oneplushalf, uint32_t vsc, uint32_t ci)
{
  uint32_t sigmaPiMask = (T1_SIGMA_THIS | T1_PI_THIS) << ci;
  uint32_t sigmaNeighboursMask = T1_SIGMA_NEIGHBOURS << ci;

  if((*flagsPtr & sigmaPiMask) == 0U && (*flagsPtr & sigmaNeighboursMask) != 0U)
  {
    uint8_t v;
    DEC_SYMBOL_RAW();

    if(v)
    {
      DEC_SYMBOL_RAW();
      *datap = v ? -oneplushalf : oneplushalf;

      // Efficiently update flags.
      update_flags(flagsPtr, ci, v, w_ + 2, vsc);
    }

    // Mark the current flag as processed.
    *flagsPtr |= T1_PI_THIS << ci;
  }
}

inline void BlockCoder::dec_refpass_step_raw(grk_flag* flagsPtr, int32_t* datap, int32_t poshalf,
                                             uint32_t ci)
{
  uint32_t shiftedSigma = T1_SIGMA_THIS << ci;
  uint32_t shiftedMu = T1_MU_THIS << ci;

  // Combine the condition and avoid recalculating shifted values.
  if((*flagsPtr & (shiftedSigma | (T1_PI_THIS << ci))) == shiftedSigma)
  {
    uint8_t v;
    DEC_SYMBOL_RAW();
    int32_t adjustment = (v ^ (*datap < 0)) ? poshalf : -poshalf;
    *datap += adjustment;
    *flagsPtr |= shiftedMu;
  }
}

void BlockCoder::dec_refpass_raw(int8_t bpno)
{
  auto dataPtr = uncompressedData_;
  auto flagsPtr = flags_ + 1 + (w_ + 2);
  const int32_t one = 1 << bpno;
  const int32_t poshalf = one >> 1;

  uint16_t i = 0, j = 0, k = 0;

  // Main loop: Process blocks of 4 rows.
  for(; k < (h_ & ~3U); k += 4, flagsPtr += 2, dataPtr += 3 * w_)
  {
    for(i = 0; i < w_; ++i, ++flagsPtr, ++dataPtr)
    {
      // Skip processing if no flags are set.
      if(*flagsPtr == 0)
        continue;

      // Inline processing for each row in the 4-row block.
      dec_refpass_step_raw(flagsPtr, dataPtr, poshalf, 0U);
      dec_refpass_step_raw(flagsPtr, dataPtr + w_, poshalf, 3U);
      dec_refpass_step_raw(flagsPtr, dataPtr + 2 * w_, poshalf, 6U);
      dec_refpass_step_raw(flagsPtr, dataPtr + 3 * w_, poshalf, 9U);
    }
  }

  // Handle the remaining rows (less than 4).
  uint16_t remainingRows = h_ - k;
  if(remainingRows > 0)
  {
    for(i = 0; i < w_; ++i, ++flagsPtr, ++dataPtr)
    {
      for(j = 0; j < remainingRows; ++j)
      {
        dec_refpass_step_raw(flagsPtr, dataPtr + j * w_, poshalf, 3 * j);
      }
    }
  }
}
uint16_t BlockCoder::getFlagsStride(void)
{
  return w_ + 2U;
}
uint16_t BlockCoder::getFlagsHeight(void)
{
  return (uint16_t)((h_ + 3U) >> 2);
}

void BlockCoder::initFlags(void)
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

/// ENCODE ////////////////////////////////////////////////////
/**
 * Deallocate the compressing data of the given precinct.
 */
void BlockCoder::code_block_enc_deallocate(cblk_enc* code_block)
{
  delete[] code_block->passes;
  code_block->passes = nullptr;
}
void BlockCoder::code_block_enc_allocate(cblk_enc* p_code_block)
{
  if(!p_code_block->passes)
    p_code_block->passes = new pass_enc[100];
}
double BlockCoder::getwmsedec(int32_t nmsedec, uint16_t compno, uint32_t level, uint8_t orientation,
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
int BlockCoder::enc_is_term_pass(cblk_enc* cblk, uint32_t cblksty, int8_t bpno, uint32_t passtype)
{
  /* Is it the last cleanup pass ? */
  if(passtype == 2 && bpno == 0)
    return true;

  if(cblksty & GRK_CBLKSTY_TERMALL)
    return true;

  if((cblksty & GRK_CBLKSTY_LAZY))
  {
    /* For bypass, terminate the 4th cleanup pass */
    if((bpno == ((int32_t)cblk->numbps - 4)) && (passtype == 2))
      return true;
    /* and beyond terminate all the magnitude refinement passes (in raw) */
    /* and cleanup passes (in MQC) */
    if((bpno < ((int32_t)(cblk->numbps) - 4)) && (passtype > 0))
      return true;
  }

  return false;
}

#define enc_sigpass_step_macro(datap, ci, vsc)                                                    \
  {                                                                                               \
    uint8_t v;                                                                                    \
    if((*flagsPtr & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))) == 0U &&                              \
       (*flagsPtr & (T1_SIGMA_NEIGHBOURS << (ci))) != 0U)                                         \
    {                                                                                             \
      uint8_t ctxno = GETCTXNO_ZC(mqc, *flagsPtr >> (ci));                                        \
      v = !!(smr_abs(*(datap)) & (uint32_t)one);                                                  \
      curctx = mqc->ctxs + ctxno;                                                                 \
      if(type == T1_TYPE_RAW)                                                                     \
        mqc_bypass_enc_macro(mqc, c, ct, v) else mqc_encode_macro(mqc, curctx, a, c, ct, v) if(v) \
        {                                                                                         \
          uint32_t lu = getctxtno_sc_or_spb_index(*flagsPtr, flagsPtr[-1], flagsPtr[1], ci);      \
          ctxno = lut_ctxno_sc[lu];                                                               \
          v = smr_sign(*(datap));                                                                 \
          if(nmsedec)                                                                             \
            *nmsedec += getnmsedec_sig((uint32_t)smr_abs(*(datap)), (uint32_t)bpno);              \
          curctx = mqc->ctxs + ctxno;                                                             \
          if(type == T1_TYPE_RAW)                                                                 \
            mqc_bypass_enc_macro(mqc, c, ct, v) else mqc_encode_macro(mqc, curctx, a, c, ct,      \
                                                                      v ^ lut_spb[lu])            \
                update_flags(flagsPtr, ci, v, w_ + 2, vsc);                                       \
        }                                                                                         \
      *flagsPtr |= T1_PI_THIS << (ci);                                                            \
    }                                                                                             \
  }
void BlockCoder::enc_sigpass(int8_t bpno, int32_t* nmsedec, uint8_t type, uint32_t cblksty)
{
  uint16_t i, k;
  int32_t const one = 1 << (bpno + T1_NMSEDEC_FRACBITS);
  // flags top left hand corner
  auto flagsPtr = flags_ + 1 + (w_ + 2);
  auto mqc = &(coder);
  PUSH_MQC();
  uint32_t const extra = 2;
  if(nmsedec)
    *nmsedec = 0;
  for(k = 0; k < (h_ & ~3U); k += 4)
  {
    for(i = 0; i < w_; ++i)
    {
      if(*flagsPtr == 0U)
      {
        /* Nothing to do for any of the 4 data points */
        flagsPtr++;
        continue;
      }
      enc_sigpass_step_macro(uncompressedData_ + (k + 0) * stride_ + i, 0,
                             cblksty & GRK_CBLKSTY_VSC);
      enc_sigpass_step_macro(uncompressedData_ + (k + 1) * stride_ + i, 3, 0);
      enc_sigpass_step_macro(uncompressedData_ + (k + 2) * stride_ + i, 6, 0);
      enc_sigpass_step_macro(uncompressedData_ + (k + 3) * stride_ + i, 9, 0);
      ++flagsPtr;
    }
    flagsPtr += extra;
  }
  if(k < h_)
  {
    for(i = 0; i < w_; ++i)
    {
      if(*flagsPtr == 0U)
      {
        /* Nothing to do for any of the 4 data points */
        flagsPtr++;
        continue;
      }
      auto pdata = uncompressedData_ + k * stride_ + i;
      for(uint16_t j = k; j < h_; ++j)
      {
        enc_sigpass_step_macro(pdata, 3U * (j - k), (j == k && (cblksty & GRK_CBLKSTY_VSC) != 0));
        pdata += stride_;
      }
      ++flagsPtr;
    }
  }
  POP_MQC();
}
#define enc_refpass_step_macro(datap, ci)                                        \
  {                                                                              \
    uint8_t v;                                                                   \
    uint32_t const shift_flags = (*flagsPtr >> (ci));                            \
    if((shift_flags & (T1_SIGMA_THIS | T1_PI_THIS)) == T1_SIGMA_THIS)            \
    {                                                                            \
      uint8_t ctxno = GETCTXNO_MAG(shift_flags);                                 \
      if(nmsedec)                                                                \
        *nmsedec += getnmsedec_ref((uint32_t)smr_abs(*(datap)), (uint32_t)bpno); \
      v = !!(smr_abs(*(datap)) & (uint32_t)one);                                 \
      curctx = mqc->ctxs + ctxno;                                                \
      if(type == T1_TYPE_RAW)                                                    \
        mqc_bypass_enc_macro(mqc, c, ct, v) else mqc_encode_macro(               \
            mqc, curctx, a, c, ct, v)* flagsPtr |= T1_MU_THIS << (ci);           \
    }                                                                            \
  }
void BlockCoder::enc_refpass(int8_t bpno, int32_t* nmsedec, uint8_t type)
{
  const int32_t one = 1 << (bpno + T1_NMSEDEC_FRACBITS);
  auto flagsPtr = flags_ + 1 + (w_ + 2);
  auto mqc = &(coder);
  PUSH_MQC();
  const uint8_t extra = 2U;
  if(nmsedec)
    *nmsedec = 0;
  uint16_t k;
  for(k = 0; k < (h_ & ~3U); k += 4)
  {
    for(uint16_t i = 0; i < w_; ++i)
    {
      if((*flagsPtr & (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13)) == 0)
      { /* none significant */
        flagsPtr++;
        continue;
      }
      if((*flagsPtr & (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3)) ==
         (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3))
      { /* all processed by sigpass */
        flagsPtr++;
        continue;
      }
      enc_refpass_step_macro(uncompressedData_ + (k + 0) * stride_ + i, 0);
      enc_refpass_step_macro(uncompressedData_ + (k + 1) * stride_ + i, 3);
      enc_refpass_step_macro(uncompressedData_ + (k + 2) * stride_ + i, 6);
      enc_refpass_step_macro(uncompressedData_ + (k + 3) * stride_ + i, 9);
      ++flagsPtr;
    }
    flagsPtr += extra;
  }
  if(k < h_)
  {
    for(uint16_t i = 0; i < w_; ++i)
    {
      if((*flagsPtr & (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13)) == 0)
      {
        /* none significant */
        flagsPtr++;
        continue;
      }
      for(uint16_t j = k; j < h_; ++j)
        enc_refpass_step_macro(uncompressedData_ + j * stride_ + i, 3 * (j - k));
      ++flagsPtr;
    }
  }
  POP_MQC();
}
void BlockCoder::enc_clnpass(int8_t bpno, int32_t* nmsedec, uint32_t cblksty)
{
  const int32_t one = 1 << (bpno + T1_NMSEDEC_FRACBITS);
  auto mqc = &coder;
  PUSH_MQC();
  if(nmsedec)
    *nmsedec = 0;
  auto flagsPtr = flags_ + 1 + (w_ + 2);
  uint16_t k;
  for(k = 0; k < (h_ & ~3U); k += 4, flagsPtr += 2)
  {
    for(uint16_t i = 0; i < w_; ++i, ++flagsPtr)
    {
      uint32_t agg = !(*flagsPtr);
      uint8_t runlen = 0;
      if(agg)
      {
        for(; runlen < 4; ++runlen)
        {
          if(smr_abs(uncompressedData_[((k + runlen) * stride_) + i]) & (uint32_t)one)
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
      auto datap = uncompressedData_ + ((k + runlen) * stride_) + i;
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
          if(nmsedec)
            *nmsedec += getnmsedec_sig((uint32_t)smr_abs(*datap), (uint32_t)bpno);
          uint8_t ctxno = lut_ctxno_sc[lu];
          curctx = mqc->ctxs + ctxno;
          uint8_t v = smr_sign(*datap);
          uint8_t spb = lut_spb[lu];
          mqc_encode_macro(mqc, curctx, a, c, ct, v ^ spb);
          uint32_t vsc = ((cblksty & GRK_CBLKSTY_VSC) && (ci == 0)) ? 1 : 0;
          update_flags(flagsPtr, ci, v, w_ + 2U, vsc);
        }
        *flagsPtr &= ~(T1_PI_THIS << (ci));
        datap += stride_;
      }
    }
  }
  if(k < h_)
  {
    uint8_t runlen = 0;
    for(uint16_t i = 0; i < w_; ++i, ++flagsPtr)
    {
      auto datap = uncompressedData_ + ((k + runlen) * stride_) + i;
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
      const uint8_t lim = 3 * (uint8_t)(h_ - k);
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
          if(nmsedec)
            *nmsedec += getnmsedec_sig((uint32_t)smr_abs(*datap), (uint32_t)bpno);
          uint8_t ctxno = lut_ctxno_sc[lu];
          curctx = mqc->ctxs + ctxno;
          uint8_t v = smr_sign(*datap);
          uint8_t spb = lut_spb[lu];
          mqc_encode_macro(mqc, curctx, a, c, ct, v ^ spb);
          uint8_t vsc = ((cblksty & GRK_CBLKSTY_VSC) && (ci == 0)) ? 1 : 0;
          update_flags(flagsPtr, ci, v, w_ + 2U, vsc);
        }
        *flagsPtr &= ~(T1_PI_THIS << (ci));
        datap += stride_;
      }
    }
  }

  POP_MQC();
}
double BlockCoder::compress_cblk(cblk_enc* cblk, uint32_t max, uint8_t orientation, uint16_t compno,
                                 uint8_t level, uint8_t qmfbid, double stepsize, uint32_t cblksty,
                                 const double* mct_norms, uint16_t mct_numcomps, bool doRateControl,
                                 uint16_t earlyStopSlope)
{
  code_block_enc_allocate(cblk);
  auto mqc = &coder;
  coder.init_enc(cblk->data);

  int32_t nmsedec = 0;
  auto p_nmsdedec = doRateControl ? &nmsedec : nullptr;
  double tempwmsedec;

  mqc->lut_ctxno_zc_orient = lut_ctxno_zc + (orientation << 9);
  cblk->numbps = 0;
  if(max)
  {
    uint8_t temp = floorlog2(max) + 1;
    if(temp <= T1_NMSEDEC_FRACBITS)
      cblk->numbps = 0;
    else
      cblk->numbps = temp - T1_NMSEDEC_FRACBITS;
  }
  if(cblk->numbps == 0)
  {
    cblk->numPassesTotal = 0;
    return 0;
  }
  int8_t bpno = (int8_t)(cblk->numbps - 1);
  uint8_t passtype = 2;
  mqc->resetstates();
  coder.init_enc(cblk->data);
#ifdef PLUGIN_DEBUG_ENCODE
  // uint32_t state = Plugin::getDebugState();
  // if (state & GROK_PLUGIN_STATE_DEBUG) {
  mqc->debug_mqc.context_stream = cblk->context_stream;
  mqc->debug_mqc.orientation = orientation;
  mqc->debug_mqc.compno = compno;
  mqc->debug_mqc.level = level;
  //}
#endif

  // INCREMENTAL CONVEX HULL EARLY TERMINATION
  //
  // Convert the log-domain slope threshold to linear domain for use in the
  // per-pass heuristic below. This conversion is the inverse of RateControl::slopeToLog():
  //   slopeToLog(s) = ln(s) * (256/ln2) - ln(2^64) * (256/ln2) + 65536
  //                 = log2(s) * 256 + 49152
  // Inverse:
  //   s = 2^((logSlope - 49152) / 256)
  //     = exp((logSlope - 65536) * ln2/256) * 2^64
  //
  // We exclude the LL band from early termination because LL subband blocks
  // have non-zero-mean samples, causing distortion to vary unpredictably with
  // bit-plane depth. This can produce consecutive non-hull passes that would
  // cause premature termination. All detail subbands (HL, LH, HH) have
  // approximately zero-mean samples and well-behaved monotone slope decay.
  float linearSlopeThreshold = -1.0f;
  if(earlyStopSlope > 1 && doRateControl && orientation != BAND_ORIENT_LL)
  {
    // Convert from grok's log-domain slope representation to linear domain.
    // grok encodes slopes as: logSlope = ln(s/2^64) * (256/ln2) + 65536
    //   where s is the linear slope (distortion-decrease per byte).
    // Inversion: s = exp((logSlope - 65536) * ln2/256) * 2^64
    //
    // 65536 = midpoint offset chosen so the uint16_t range [0, 65535] maps to
    //   slopes spanning roughly [2^(-256)·2^64, 2^(+255)·2^64] in linear domain,
    //   covering the full practical range of JPEG 2000 coding pass slopes.
    // 256 = scale factor giving 256 log-slope units per octave (factor-of-2 in
    //   linear slope). This provides fine granularity for PCRD-opt threshold
    //   selection while fitting in uint16_t.
    // 2^64 = normalization constant. Slopes are stored relative to 2^64 so that
    //   typical slopes (distortion per byte, where distortion uses MSE in
    //   fixed-point with 2^64 headroom) map to the useful uint16_t range.
    const float logScale = 0.693147180559945f / 256.0f;  // ln(2)/256
    float logVal = (static_cast<float>(earlyStopSlope) - 65536.0f) * logScale;
    const float maxSlope = static_cast<float>(1ULL << 63) * 2.0f;  // 2^64 normalization
    linearSlopeThreshold = expf(logVal) * maxSlope;
  }

  double cumwmsedec = 0.0;
  uint8_t passno;
  for(passno = 0; bpno >= 0; ++passno)
  {
    auto* pass = cblk->passes + passno;
    uint8_t type =
        ((bpno < ((int32_t)(cblk->numbps) - 4)) && (passtype < 2) && (cblksty & GRK_CBLKSTY_LAZY))
            ? T1_TYPE_RAW
            : T1_TYPE_MQ;

    /* If the previous pass was terminating, we need to reset the compressor */
    if(passno > 0 && cblk->getPass(passno - 1)->term)
    {
      if(type == T1_TYPE_RAW)
        mqc->bypass_init_enc();
      else
        mqc->restart_init_enc();
    }

    switch(passtype)
    {
      case 0:
        enc_sigpass(bpno, p_nmsdedec, type, cblksty);
        break;
      case 1:
        enc_refpass(bpno, p_nmsdedec, type);
        break;
      case 2:
        enc_clnpass(bpno, p_nmsdedec, cblksty);
        if(cblksty & GRK_CBLKSTY_SEGSYM)
          mqc->segmark_enc();
#ifdef PLUGIN_DEBUG_ENCODE
        // if (state & GROK_PLUGIN_STATE_DEBUG) {
        mqc_next_plane(&mqc->debug_mqc);
        //}
#endif
        break;
    }
    if(doRateControl)
    {
      tempwmsedec = getwmsedec(nmsedec, compno, level, orientation, bpno, qmfbid, stepsize,
                               mct_norms, mct_numcomps);
      cumwmsedec += tempwmsedec;
      pass->distortiondec = cumwmsedec;
    }
    if(enc_is_term_pass(cblk, cblksty, bpno, passtype))
    {
      if(type == T1_TYPE_RAW)
      {
        mqc->bypass_flush_enc(cblksty & GRK_CBLKSTY_PTERM);
      }
      else
      {
        if(cblksty & GRK_CBLKSTY_PTERM)
          mqc->erterm_enc();
        else
          mqc->flush_enc();
      }
      pass->term = true;
      pass->rate = mqc->numbytes_enc();
    }
    else
    {
      /* Non terminated pass */
      // correction term is used for non-terminated passes,
      // to ensure that maximal bits are
      // extracted from the partial segment when code block
      // is truncated at this pass
      // See page 498 of Taubman and Marcellin for more details
      // note: we add 1 because rates for non-terminated passes
      // are based on mqc_numbytes(mqc),
      // which is always 1 less than actual rate
      uint16_t rate_extra_bytes;
      if(type == T1_TYPE_RAW)
      {
        rate_extra_bytes = mqc->bypass_get_extra_bytes_enc((cblksty & GRK_CBLKSTY_PTERM));
      }
      else
      {
        rate_extra_bytes = 4 + 1;
        if(mqc->ct < 5)
          rate_extra_bytes++;
      }
      pass->term = false;
      pass->rate = mqc->numbytes_enc() + rate_extra_bytes;
    }
    if(++passtype == 3)
    {
      passtype = 0;
      bpno--;

      // INCREMENTAL CONVEX HULL TERMINATION HEURISTIC
      //
      // At each bit-plane boundary, we evaluate whether encoding further passes
      // is worthwhile by estimating whether any recent pass could plausibly lie
      // on the R-D convex hull above the threshold slope.
      //
      // THEORY (Taubman & Marcellin, Ch. 8):
      // The PCRD-opt algorithm assigns each coding pass a slope equal to the
      // marginal distortion decrease per byte on the convex hull. A pass is
      // included in a quality layer iff its hull slope exceeds the layer's
      // threshold λ*. For well-behaved subbands, slopes decrease approximately
      // geometrically with bit-plane depth (factor ~4× per plane for uniform
      // statistics). If all recent passes have slopes well below threshold,
      // subsequent passes will too.
      //
      // ALGORITHM:
      // For each candidate point z0 (scanning backwards from the current pass z):
      //   1. Compute the MINIMUM accumulated slope in a backward window of up to
      //      7 passes starting at z0. The minimum (rather than maximum) is used
      //      because it represents the most conservative estimate of the slope
      //      that z0 would have if it were on the convex hull — a lower bound
      //      on the true hull slope. If even this lower bound exceeds the
      //      threshold, the pass is definitely useful and we must not terminate.
      //
      //   2. Compare against an ESCALATING threshold:
      //        S_z0 < threshold × 3^floor((z - z0) / 3)
      //      The factor 3^floor(q/3) raises the required slope by 3× for each
      //      full bit-plane of distance from the current pass. This accounts for
      //      the fact that slopes decrease ~4× per bit-plane at high rates — using
      //      3× instead of 4× provides conservatism (we under-estimate the expected
      //      decay, making termination harder to trigger incorrectly).
      //
      //   3. Track "potential hull points" — candidates whose slopes form a
      //      monotonically decreasing sequence with positive slope and rate.
      //      These approximate what the convex hull would look like.
      //
      // TERMINATION CONDITIONS (all must hold simultaneously):
      //   a) The interval [z0, z] spans at least 3 passes (one full bit-plane
      //      of evidence that slopes are below threshold).
      //   b) At least 2 potential hull points were identified (ensures the R-D
      //      curve has enough structure for a meaningful slope estimate).
      //   c) ALL passes in the interval satisfy condition (2) above — no pass
      //      has a conservative slope estimate exceeding its scaled threshold.
      //
      // WHY THIS IS SAFE:
      // - The minimum-slope backward window provides a LOWER BOUND on hull slope.
      //   If the lower bound is below threshold, the true slope may still be above
      //   (we don't terminate). Only when even the lower bound is below do we
      //   consider termination.
      // - The 3× escalating factor gives older passes progressively more slack,
      //   because their slopes are naturally expected to be higher (they encode
      //   more significant bit-planes). Using 3× rather than the theoretical 4×
      //   decay rate adds a safety margin.
      // - Requiring 2 hull points and 3+ passes prevents spurious termination
      //   from noise in a single pass's statistics.
      // passno >= 5 ensures we have at least 2 full bit-planes of data (passes 0-2
      // for first bit-plane + passes 3-5 for second). This guarantees enough R-D
      // samples for a meaningful slope estimate; slopes from a single bit-plane
      // can be noisy and unrepresentative of the overall trend.
      if(linearSlopeThreshold > 0.0f && passno >= 5)
      {
        int z = static_cast<int>(passno) - 1;  // index of just-completed cleanup pass
        int numHullPoints = 0;
        int z0;
        float lastDeltaD = 0.0f, lastDeltaL = 0.0f;
        float bestDeltaD = 0.0f, bestDeltaL = 0.0f;

        // numHullPoints < 3: we only need 2 hull points for a valid termination
        // decision, but scan up to 3 to accumulate evidence. Once 3 are found, the
        // backwards scan can stop — additional points would only confirm what we
        // already know. This bounds the scan cost to O(3 × 7) = O(21) iterations
        // per bit-plane boundary.
        for(z0 = z; z0 >= 0 && numHullPoints < 3; z0--)
        {
          // Backward window of up to 7 passes: this spans slightly more than
          // 2 full bit-planes (2 × 3 = 6 passes). Using 7 instead of 6 gives
          // one extra pass of context, which smooths out noise in slope estimation
          // without significantly increasing computation. The window accumulates
          // a "running best" (minimum slope) which represents the most conservative
          // estimate of the true hull slope at this candidate point.
          float deltaL = 0.0f, deltaD = 0.0f;
          bestDeltaD = 0.0f;
          bestDeltaL = 0.0f;
          for(int u = z0; u >= 0 && u > (z0 - 7); u--)  // window depth = 7 (see above)
          {
            // Incremental rate and distortion for pass u
            float incL = static_cast<float>(
                cblk->passes[u].rate - (u > 0 ? cblk->passes[u - 1].rate : 0));
            float incD = static_cast<float>(
                cblk->passes[u].distortiondec - (u > 0 ? cblk->passes[u - 1].distortiondec : 0.0));
            deltaL += incL;
            deltaD += incD;

            // Keep the window with LOWEST slope (most conservative estimate).
            // On first iteration (u == z0), initialize. Subsequently, update
            // when the new accumulated slope is lower than previous best.
            // Comparison: bestDeltaD/bestDeltaL > deltaD/deltaL
            // Rearranged to avoid division: bestDeltaD * deltaL > deltaD * bestDeltaL
            if(u == z0 || (bestDeltaD * deltaL > deltaD * bestDeltaL))
            {
              bestDeltaD = deltaD;
              bestDeltaL = deltaL;
            }
          }

          // Apply escalating threshold: alpha(z - z0) = 3^floor((z - z0) / 3)
          //
          // Rationale for 3× per bit-plane:
          // At high compression ratios, slopes decay approximately 4× per bit-plane
          // (because each successive plane encodes ~1/4 the energy of the previous).
          // Using 3× instead of the theoretical 4× provides a conservative safety
          // margin: we demand less slope decay than theory predicts before concluding
          // a pass is below threshold. This reduces the false-termination rate at
          // the cost of slightly less aggressive pruning.
          //
          // The "dist -= 3" step corresponds to one full bit-plane (3 coding passes:
          // significance, refinement, cleanup). We only begin escalating after the
          // first bit-plane of distance (dist > 2) because within a single plane,
          // slopes don't follow the geometric decay model.
          float ref = linearSlopeThreshold * bestDeltaL;
          for(int dist = z - z0; dist > 2; dist -= 3)
            ref *= 3.0f;

          // Condition 3.c: if the best (minimum) slope at z0 exceeds the scaled
          // threshold, this pass might be on the hull — abort termination check.
          if(bestDeltaD > ref)
          {
            numHullPoints = 0;  // Signal: do not terminate
            break;
          }

          // Check if this point qualifies as a potential convex hull point:
          // requires positive slope, and slopes must be monotonically decreasing
          // (consistent with convex hull property).
          if(bestDeltaD > 0.0f && bestDeltaL > 0.0f)
          {
            if(numHullPoints > 0 &&
               (bestDeltaD * lastDeltaL <= lastDeltaD * bestDeltaL))
              continue;  // Slope not decreasing — cannot be a hull point
            lastDeltaD = bestDeltaD;
            lastDeltaL = bestDeltaL;
            numHullPoints++;
          }
        }

        // Terminate encoding if:
        // - numHullPoints >= 2: at least two R-D points form a decreasing slope
        //   sequence, confirming we have enough structure for a reliable estimate.
        //   A single hull point could be a fluke; two establish a trend.
        // - (z - z0) >= 3: the backward scan covered at least one full bit-plane
        //   (3 passes). This ensures the slope evidence spans a meaningful coding
        //   depth. Without this guard, termination could trigger from only 1-2
        //   passes of below-threshold slopes, which is insufficient evidence.
        if(numHullPoints >= 2 && (z - z0) >= 3)
        {
          break;
        }
      }
    }
    if(cblksty & GRK_CBLKSTY_RESET)
      mqc->resetstates();
  }
  cblk->numPassesTotal = passno;
  if(cblk->numPassesTotal)
  {
    /* Make sure that pass rates are increasing */
    uint16_t last_pass_rate = mqc->numbytes_enc();
    for(passno = cblk->numPassesTotal; passno > 0;)
    {
      auto* pass = cblk->passes + --passno;
      if(pass->rate > last_pass_rate)
        pass->rate = last_pass_rate;
      else
        last_pass_rate = pass->rate;
    }
  }
  for(passno = 0; passno < cblk->numPassesTotal; passno++)
  {
    auto pass = cblk->passes + passno;

    /* Prevent generation of FF as last data byte of a pass*/
    /* For terminating passes, the flushing procedure ensured this already */
    assert(pass->rate > 0);
    if(cblk->data[pass->rate - 1] == 0xFF)
      pass->rate--;
    pass->len = (uint16_t)(pass->rate - (passno == 0 ? 0 : cblk->getPass(passno - 1)->rate));
  }
  return cumwmsedec;
}

} // namespace grk::t1
