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

#include "grk_includes.h"
#include "t1_luts.h"
#include "BlockCoder.h"
#include "BlockCoderMacros.h"

namespace grk
{

void BlockCoder::decompressInitOrientation(uint8_t orientation)
{
  coder.lut_ctxno_zc_orient = lut_ctxno_zc + (orientation << 9);
}
void BlockCoder::decompressInitSegment(uint8_t type, uint8_t** buffers, uint32_t* buffer_lengths,
                                       uint16_t num_buffers)
{
  if(type == T1_TYPE_RAW)
    coder.raw_init_dec(buffers, buffer_lengths, num_buffers);
  else
    coder.init_dec(buffers, buffer_lengths, num_buffers);
}

void BlockCoder::decompressBackup(void)
{
  if(cacheAll(cacheStrategy_))
  {
    coder.backup();
    auto cb = coder.backup_;
    if(!cb->uncompressedBufBackup_.alloc2d(w_, w_, h_, false))
    {
      grklog.error("Out of memory");
      return;
    }
    if(!cb->flagsBackup_)
    {
      cb->flagsBackup_ = (grk_flag*)grk::grk_aligned_malloc(flagsLen_ * sizeof(grk_flag));
      if(!cb->flagsBackup_)
      {
        grklog.error("Out of memory");
        return;
      }
    }
    memcpy(cb->uncompressedBufBackup_.getBuffer(), uncompressedData_,
           (size_t)w_ * h_ * sizeof(int32_t));
    memcpy(cb->flagsBackup_, flags_, flagsLen_ * sizeof(grk_flag));
  }
}

void BlockCoder::decompressRestore(uint8_t* passno, uint8_t* passtype, uint8_t* numBpsToDecompress)
{
  if(coder.cached_ && (coder.backup_->i != BACKUP_DISABLED))
  {
    auto cb = coder.backup_;
    memcpy(uncompressedData_, cb->uncompressedBufBackup_.getBuffer(),
           (size_t)w_ * h_ * sizeof(int32_t));
    memcpy(flags_, cb->flagsBackup_, flagsLen_ * sizeof(grk_flag));
    *passno = cb->passno_;
    *passtype = cb->passtype_;
    *numBpsToDecompress = cb->numBpsToDecompress_;
    coder.restore();
  }
}

void BlockCoder::setFinalLayer(bool isFinal)
{
  coder.finalLayer_ = isFinal;
}

void BlockCoder::decompressUpdateSegment(uint8_t** buffers, uint32_t* buffer_lengths,
                                         uint16_t num_buffers)
{
  coder.update_dec(buffers, buffer_lengths, num_buffers);
}

bool BlockCoder::decompressPass(uint8_t passno, uint8_t passtype, uint8_t numBpsToDecompress,
                                uint8_t type, uint32_t cblksty)
{
  switch(passtype)
  {
    case 0:
      if(type == T1_TYPE_RAW)
      {
        dec_sigpass_raw((int8_t)numBpsToDecompress, (int32_t)cblksty);
      }
      else
      {
        if(cacheAll(cacheStrategy_))
        {
          if(coder.finalLayer_)
            dec_sigpass_diff_final((int8_t)numBpsToDecompress, passno, passtype, (int32_t)cblksty);
          else
            dec_sigpass_diff((int8_t)numBpsToDecompress, passno, passtype, (int32_t)cblksty);
        }
        else
        {
          dec_sigpass((int8_t)numBpsToDecompress, (int32_t)cblksty);
        }
      }
      break;
    case 1:
      if(type == T1_TYPE_RAW)
      {
        dec_refpass_raw((int8_t)numBpsToDecompress);
      }
      else
      {
        if(cacheAll(cacheStrategy_))
        {
          if(coder.finalLayer_)
            dec_refpass_diff_final((int8_t)numBpsToDecompress, passno, passtype);
          else
            dec_refpass_diff((int8_t)numBpsToDecompress, passno, passtype);
        }
        else
        {
          dec_refpass((int8_t)numBpsToDecompress);
        }
      }
      break;
    case 2:
      if(cacheAll(cacheStrategy_))
      {
        if(coder.finalLayer_)
          dec_clnpass_diff_final((int8_t)numBpsToDecompress, passno, passtype, (int32_t)cblksty);
        else
          dec_clnpass_diff((int8_t)numBpsToDecompress, passno, passtype, (int32_t)cblksty);
      }
      else
      {
        dec_clnpass((int8_t)numBpsToDecompress, (int32_t)cblksty);
      }
      break;
  }
  if((cblksty & GRK_CBLKSTY_RESET) && type == T1_TYPE_MQ)
    coder.resetstates();

  return !cacheAll(cacheStrategy_) || (coder.backup_->i == BACKUP_DISABLED);
}
void BlockCoder::decompressFinish(uint32_t cblksty, bool finalLayer)
{
  bool check_pterm = cblksty & GRK_CBLKSTY_PTERM;
  if(check_pterm && finalLayer)
  {
    if(coder.bp + 2 < coder.end)
      grk::grklog.warn("PTERM check failure: %u remaining bytes in code block (%u used / %u)",
                       (int)(coder.end - coder.bp) - 2, (int)(coder.bp - coder.start),
                       (int)(coder.end - coder.start));
    else if(coder.end_of_byte_stream_counter > 2)
      grk::grklog.warn("PTERM check failure: %u synthesized 0xFF markers read",
                       coder.end_of_byte_stream_counter);
  }
}

bool BlockCoder::decompress_cblk(CodeblockDecompress* cblk, uint8_t orientation, uint32_t cblksty)
{
  if(!alloc((uint8_t)cblk->width(), (uint8_t)cblk->height()))
    return false;
  if(!cacheAll(cacheStrategy_))
    coder.reinit();
  bool rc = cblk->decompress<BlockCoder>(this, orientation, cblksty);
  // disable backup if no overflow actually occurred
  if(coder.cached_ && !coder.overflow_)
    coder.backup_->i = BACKUP_DISABLED;

  // reset overflow flag for next pass
  coder.overflow_ = false;

  return rc;
}

void BlockCoder::checkSegSym(int32_t cblksty)
{
  if(cblksty & GRK_CBLKSTY_SEGSYM)
  {
    auto mqc = &coder;
    bool approaching_red;
    (void)approaching_red;
    uint8_t* adjusted_end;
    (void)adjusted_end;
    CODER_SETCURCTX(mqc, T1_CTXNO_UNI);
    uint8_t v, v2;
    DEC_SYMBOL(v, mqc, mqc->curctx, mqc->a, mqc->c, mqc->ct);
    DEC_SYMBOL(v2, mqc, mqc->curctx, mqc->a, mqc->c, mqc->ct);
    v = (v << 1) | v2;
    DEC_SYMBOL(v2, mqc, mqc->curctx, mqc->a, mqc->c, mqc->ct);
    v = (v << 1) | v2;
    DEC_SYMBOL(v2, mqc, mqc->curctx, mqc->a, mqc->c, mqc->ct);
    v = (v << 1) | v2;
    if(v != 0xa)
      grklog.warn("Bad segmentation symbol %x", v);
  }
}

template<uint16_t w_, uint16_t h_, bool vsc>
void BlockCoder::dec_clnpass(int8_t bpno)
{
  DEC_PASS_CLN_IMPL(bpno, vsc, w_, h_, w_ + 2);
}
void BlockCoder::dec_clnpass(int8_t bpno, int32_t cblksty)
{
  if(w_ == 64 && h_ == 64)
  {
    if(cblksty & GRK_CBLKSTY_VSC)
      dec_clnpass<64, 64, true>(bpno);
    else
      dec_clnpass<64, 64, false>(bpno);
  }
  else
  {
    DEC_PASS_CLN_IMPL(bpno, cblksty & GRK_CBLKSTY_VSC, w_, h_, w_ + 2);
  }
  checkSegSym(cblksty);
}

template<uint16_t w_, uint16_t h_, bool vsc>
void BlockCoder::dec_sigpass(int8_t bpno)
{
  DEC_PASS_SIG_IMPL(bpno, vsc, w_, h_, w_ + 2);
}
void BlockCoder::dec_sigpass(int8_t bpno, int32_t cblksty)
{
  if(w_ == 64 && h_ == 64)
  {
    if(cblksty & GRK_CBLKSTY_VSC)
      dec_sigpass<64, 64, true>(bpno);
    else
      dec_sigpass<64, 64, false>(bpno);
  }
  else
  {
    DEC_PASS_SIG_IMPL(bpno, cblksty & GRK_CBLKSTY_VSC, w_, h_, w_ + 2);
  }
}

template<uint16_t w_, uint16_t h_>
void BlockCoder::dec_refpass(int8_t bpno)
{
  DEC_PASS_REF_IMPL(bpno, w_, h_, w_ + 2);
}
void BlockCoder::dec_refpass(int8_t bpno)
{
  if(w_ == 64 && h_ == 64)
  {
    dec_refpass<64, 64>(bpno);
  }
  else
  {
    DEC_PASS_REF_IMPL(bpno, w_, h_, w_ + 2);
  }
}

//////////////////////////////////////////////////////////////////////////////
// Differential Decode ///////////////////////////////////////////////////////

#define DEC_PASS_CLN_IMPL_DIFF(bpno, vsc, w_, h_, flagsStride)                                     \
  {                                                                                                \
    DEC_PASS_LOCAL_VARIABLES_DIFF(flagsStride)                                                     \
    const int32_t half = one >> 1;                                                                 \
    const int32_t oneplushalf = one | half;                                                        \
    uint8_t d = 0;                                                                                 \
    uint8_t runlen = 0;                                                                            \
    bool partial = false;                                                                          \
    DEC_PASS_CLN_RESTORE(coder);                                                                   \
    for(k = 0; k < (h_ & ~3u); k += 4, dataPtr += 3 * w_, flagsPtr += 2)                           \
    {                                                                                              \
      for(i = 0; i < w_; ++i, ++dataPtr, ++flagsPtr)                                               \
      {                                                                                            \
        _flags = *flagsPtr;                                                                        \
        if(_flags == 0)                                                                            \
        {                                                                                          \
          partial = true;                                                                          \
          SETCURCTX(curctx, T1_CTXNO_AGG);                                                         \
        B1:                                                                                        \
          DEC_PASS_CLN_BACKUP(B1_POS);                                                             \
          DEC_SYMBOL(d, mqc, curctx, a, c, ct);                                                    \
          if(!d)                                                                                   \
            continue;                                                                              \
          SETCURCTX(curctx, T1_CTXNO_UNI);                                                         \
        B2:                                                                                        \
          DEC_PASS_CLN_BACKUP(B2_POS);                                                             \
          DEC_SYMBOL(runlen, mqc, curctx, a, c, ct);                                               \
        B3:                                                                                        \
          DEC_PASS_CLN_BACKUP(B3_POS);                                                             \
          DEC_SYMBOL(d, mqc, curctx, a, c, ct);                                                    \
          runlen = (runlen << 1) | d;                                                              \
          switch(runlen)                                                                           \
          {                                                                                        \
            case 0:                                                                                \
            B4:                                                                                    \
              DEC_PASS_CLN_BACKUP(B4_POS);                                                         \
              DEC_PASS_CLN_STEP(false, true, _flags, flagsPtr, flagsStride, dataPtr, w_, 0, 0,     \
                                vsc);                                                              \
              partial = false;                                                                     \
              /* FALLTHRU */                                                                       \
            case 1:                                                                                \
            B5:                                                                                    \
              DEC_PASS_CLN_BACKUP(B5_POS);                                                         \
              DEC_PASS_CLN_STEP(false, partial, _flags, flagsPtr, flagsStride, dataPtr, w_, 1, 3,  \
                                false);                                                            \
              partial = false;                                                                     \
              /* FALLTHRU */                                                                       \
            case 2:                                                                                \
            B6:                                                                                    \
              DEC_PASS_CLN_BACKUP(B6_POS);                                                         \
              DEC_PASS_CLN_STEP(false, partial, _flags, flagsPtr, flagsStride, dataPtr, w_, 2, 6,  \
                                false);                                                            \
              partial = false;                                                                     \
              /* FALLTHRU */                                                                       \
            case 3:                                                                                \
            B7:                                                                                    \
              DEC_PASS_CLN_BACKUP(B7_POS);                                                         \
              DEC_PASS_CLN_STEP(false, partial, _flags, flagsPtr, flagsStride, dataPtr, w_, 3, 9,  \
                                false);                                                            \
              break;                                                                               \
          }                                                                                        \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
        B8:                                                                                        \
          DEC_PASS_CLN_BACKUP(B8_POS);                                                             \
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, flagsStride, dataPtr, w_, 0, 0, vsc);   \
        B9:                                                                                        \
          DEC_PASS_CLN_BACKUP(B9_POS);                                                             \
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, flagsStride, dataPtr, w_, 1, 3, false); \
        B10:                                                                                       \
          DEC_PASS_CLN_BACKUP(B10_POS);                                                            \
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, flagsStride, dataPtr, w_, 2, 6, false); \
        B11:                                                                                       \
          DEC_PASS_CLN_BACKUP(B11_POS);                                                            \
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, flagsStride, dataPtr, w_, 3, 9, false); \
        }                                                                                          \
        *flagsPtr = _flags & ~(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);                             \
      }                                                                                            \
    }                                                                                              \
    if(k < h_)                                                                                     \
    {                                                                                              \
      for(i = 0; i < w_; ++i, ++flagsPtr, ++dataPtr)                                               \
      {                                                                                            \
        for(j = 0; j < h_ - k; ++j)                                                                \
        {                                                                                          \
          _flags = *flagsPtr;                                                                      \
        B12:                                                                                       \
          DEC_PASS_CLN_BACKUP(B12_POS);                                                            \
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, w_ + 2U, dataPtr + j * w_, 0, j, j * 3, \
                            vsc);                                                                  \
          *flagsPtr = _flags;                                                                      \
        }                                                                                          \
        *flagsPtr &= ~(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);                                     \
      }                                                                                            \
    }                                                                                              \
    POP_MQC();                                                                                     \
  }

#define DEC_PASS_CLN_IMPL_DIFF_FINAL(bpno, vsc, w_, h_, flagsStride)                               \
  {                                                                                                \
    DEC_PASS_LOCAL_VARIABLES_DIFF(flagsStride)                                                     \
    const int32_t half = one >> 1;                                                                 \
    const int32_t oneplushalf = one | half;                                                        \
    uint8_t d = 0;                                                                                 \
    uint8_t runlen = 0;                                                                            \
    bool partial = false;                                                                          \
    DEC_PASS_CLN_RESTORE(coder);                                                                   \
    for(k = 0; k < (h_ & ~3u); k += 4, dataPtr += 3 * w_, flagsPtr += 2)                           \
    {                                                                                              \
      for(i = 0; i < w_; ++i, ++dataPtr, ++flagsPtr)                                               \
      {                                                                                            \
        _flags = *flagsPtr;                                                                        \
        if(_flags == 0)                                                                            \
        {                                                                                          \
          partial = true;                                                                          \
          SETCURCTX(curctx, T1_CTXNO_AGG);                                                         \
        B1:                                                                                        \
          DEC_SYMBOL(d, mqc, curctx, a, c, ct);                                                    \
          if(!d)                                                                                   \
            continue;                                                                              \
          SETCURCTX(curctx, T1_CTXNO_UNI);                                                         \
        B2:                                                                                        \
          DEC_SYMBOL(runlen, mqc, curctx, a, c, ct);                                               \
        B3:                                                                                        \
          DEC_SYMBOL(d, mqc, curctx, a, c, ct);                                                    \
          runlen = (runlen << 1) | d;                                                              \
          switch(runlen)                                                                           \
          {                                                                                        \
            case 0:                                                                                \
            B4:                                                                                    \
              DEC_PASS_CLN_STEP(false, true, _flags, flagsPtr, flagsStride, dataPtr, w_, 0, 0,     \
                                vsc);                                                              \
              partial = false;                                                                     \
              /* FALLTHRU */                                                                       \
            case 1:                                                                                \
            B5:                                                                                    \
              DEC_PASS_CLN_STEP(false, partial, _flags, flagsPtr, flagsStride, dataPtr, w_, 1, 3,  \
                                false);                                                            \
              partial = false;                                                                     \
              /* FALLTHRU */                                                                       \
            case 2:                                                                                \
            B6:                                                                                    \
              DEC_PASS_CLN_STEP(false, partial, _flags, flagsPtr, flagsStride, dataPtr, w_, 2, 6,  \
                                false);                                                            \
              partial = false;                                                                     \
              /* FALLTHRU */                                                                       \
            case 3:                                                                                \
            B7:                                                                                    \
              DEC_PASS_CLN_STEP(false, partial, _flags, flagsPtr, flagsStride, dataPtr, w_, 3, 9,  \
                                false);                                                            \
              break;                                                                               \
          }                                                                                        \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
        B8:                                                                                        \
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, flagsStride, dataPtr, w_, 0, 0, vsc);   \
        B9:                                                                                        \
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, flagsStride, dataPtr, w_, 1, 3, false); \
        B10:                                                                                       \
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, flagsStride, dataPtr, w_, 2, 6, false); \
        B11:                                                                                       \
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, flagsStride, dataPtr, w_, 3, 9, false); \
        }                                                                                          \
        *flagsPtr = _flags & ~(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);                             \
      }                                                                                            \
    }                                                                                              \
    if(k < h_)                                                                                     \
    {                                                                                              \
      for(i = 0; i < w_; ++i, ++flagsPtr, ++dataPtr)                                               \
      {                                                                                            \
        for(j = 0; j < h_ - k; ++j)                                                                \
        {                                                                                          \
          _flags = *flagsPtr;                                                                      \
        B12:                                                                                       \
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, w_ + 2U, dataPtr + j * w_, 0, j, j * 3, \
                            vsc);                                                                  \
          *flagsPtr = _flags;                                                                      \
        }                                                                                          \
        *flagsPtr &= ~(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);                                     \
      }                                                                                            \
    }                                                                                              \
    POP_MQC();                                                                                     \
  }

#define DEC_PASS_SIG_IMPL_DIFF(bpno, vsc, w_, h_, flagsStride)                                  \
  {                                                                                             \
    DEC_PASS_LOCAL_VARIABLES_DIFF(flagsStride)                                                  \
    const int32_t half = one >> 1;                                                              \
    const int32_t oneplushalf = one | half;                                                     \
    DEC_PASS_RESTORE(coder);                                                                    \
    for(k = 0; k < (h_ & ~3u); k += 4, dataPtr += 3 * w_, flagsPtr += 2)                        \
    {                                                                                           \
      for(i = 0; i < w_; ++i, ++dataPtr, ++flagsPtr)                                            \
      {                                                                                         \
        _flags = *flagsPtr;                                                                     \
        if(_flags != 0)                                                                         \
        {                                                                                       \
        B1:                                                                                     \
          DEC_PASS_BACKUP(coder, B1_POS);                                                       \
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr, w_, 0, 0, vsc);             \
        B2:                                                                                     \
          DEC_PASS_BACKUP(coder, B2_POS);                                                       \
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr, w_, 1, 3, false);           \
        B3:                                                                                     \
          DEC_PASS_BACKUP(coder, B3_POS);                                                       \
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr, w_, 2, 6, false);           \
        B4:                                                                                     \
          DEC_PASS_BACKUP(coder, B4_POS);                                                       \
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr, w_, 3, 9, false);           \
          *flagsPtr = _flags;                                                                   \
        }                                                                                       \
      }                                                                                         \
    }                                                                                           \
    if(k < h_)                                                                                  \
      for(i = 0; i < w_; ++i, ++dataPtr, ++flagsPtr)                                            \
        for(j = 0; j < h_ - k; ++j)                                                             \
        {                                                                                       \
          _flags = *flagsPtr;                                                                   \
        B5:                                                                                     \
          DEC_PASS_BACKUP(coder, B5_POS);                                                       \
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr + j * w_, 0, j, 3 * j, vsc); \
          *flagsPtr = _flags;                                                                   \
        }                                                                                       \
    POP_MQC();                                                                                  \
  }
#define DEC_PASS_SIG_IMPL_DIFF_FINAL(bpno, vsc, w_, h_, flagsStride)                            \
  {                                                                                             \
    DEC_PASS_LOCAL_VARIABLES_DIFF(flagsStride)                                                  \
    const int32_t half = one >> 1;                                                              \
    const int32_t oneplushalf = one | half;                                                     \
    DEC_PASS_RESTORE(coder);                                                                    \
    for(k = 0; k < (h_ & ~3u); k += 4, dataPtr += 3 * w_, flagsPtr += 2)                        \
    {                                                                                           \
      for(i = 0; i < w_; ++i, ++dataPtr, ++flagsPtr)                                            \
      {                                                                                         \
        _flags = *flagsPtr;                                                                     \
        if(_flags != 0)                                                                         \
        {                                                                                       \
        B1:                                                                                     \
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr, w_, 0, 0, vsc);             \
        B2:                                                                                     \
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr, w_, 1, 3, false);           \
        B3:                                                                                     \
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr, w_, 2, 6, false);           \
        B4:                                                                                     \
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr, w_, 3, 9, false);           \
          *flagsPtr = _flags;                                                                   \
        }                                                                                       \
      }                                                                                         \
    }                                                                                           \
    if(k < h_)                                                                                  \
      for(i = 0; i < w_; ++i, ++dataPtr, ++flagsPtr)                                            \
        for(j = 0; j < h_ - k; ++j)                                                             \
        {                                                                                       \
          _flags = *flagsPtr;                                                                   \
        B5:                                                                                     \
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr + j * w_, 0, j, 3 * j, vsc); \
          *flagsPtr = _flags;                                                                   \
        }                                                                                       \
    POP_MQC();                                                                                  \
  }

template<uint16_t w_, uint16_t h_, bool vsc>
void BlockCoder::dec_sigpass_diff(int8_t bpno, uint8_t passno, uint8_t passtype)
{
  DEC_PASS_SIG_IMPL_DIFF(bpno, vsc, w_, h_, w_ + 2);
}
void BlockCoder::dec_sigpass_diff(int8_t bpno, uint8_t passno, uint8_t passtype, int32_t cblksty)
{
  if(w_ == 64 && h_ == 64)
  {
    if(cblksty & GRK_CBLKSTY_VSC)
      dec_sigpass_diff<64, 64, true>(bpno, passno, passtype);
    else
      dec_sigpass_diff<64, 64, false>(bpno, passno, passtype);
  }
  else
  {
    DEC_PASS_SIG_IMPL_DIFF(bpno, cblksty & GRK_CBLKSTY_VSC, w_, h_, w_ + 2);
  }
}
template<uint16_t w_, uint16_t h_, bool vsc>
void BlockCoder::dec_sigpass_diff_final(int8_t bpno, uint8_t passno, uint8_t passtype)
{
  // suppress warning
  (void)passtype;
  DEC_PASS_SIG_IMPL_DIFF_FINAL(bpno, vsc, w_, h_, w_ + 2);
}
void BlockCoder::dec_sigpass_diff_final(int8_t bpno, uint8_t passno, uint8_t passtype,
                                        int32_t cblksty)
{
  if(w_ == 64 && h_ == 64)
  {
    if(cblksty & GRK_CBLKSTY_VSC)
      dec_sigpass_diff_final<64, 64, true>(bpno, passno, passtype);
    else
      dec_sigpass_diff_final<64, 64, false>(bpno, passno, passtype);
  }
  else
  {
    DEC_PASS_SIG_IMPL_DIFF_FINAL(bpno, cblksty & GRK_CBLKSTY_VSC, w_, h_, w_ + 2);
  }
}

#define DEC_PASS_REF_IMPL_DIFF(bpno, w_, h_, flagsStride)                \
  {                                                                      \
    DEC_PASS_LOCAL_VARIABLES_DIFF(flagsStride)                           \
    const int32_t poshalf = one >> 1;                                    \
    DEC_PASS_RESTORE(coder);                                             \
    for(k = 0; k < (h_ & ~3u); k += 4, dataPtr += 3 * w_, flagsPtr += 2) \
    {                                                                    \
      for(i = 0; i < w_; ++i, ++dataPtr, ++flagsPtr)                     \
      {                                                                  \
        _flags = *flagsPtr;                                              \
        if(_flags != 0)                                                  \
        {                                                                \
        B1:                                                              \
          DEC_PASS_BACKUP(coder, B1_POS);                                \
          DEC_PASS_REF_STEP(_flags, dataPtr, w_, 0, 0);                  \
        B2:                                                              \
          DEC_PASS_BACKUP(coder, B2_POS);                                \
          DEC_PASS_REF_STEP(_flags, dataPtr, w_, 1, 3);                  \
        B3:                                                              \
          DEC_PASS_BACKUP(coder, B3_POS);                                \
          DEC_PASS_REF_STEP(_flags, dataPtr, w_, 2, 6);                  \
        B4:                                                              \
          DEC_PASS_BACKUP(coder, B4_POS);                                \
          DEC_PASS_REF_STEP(_flags, dataPtr, w_, 3, 9);                  \
          *flagsPtr = _flags;                                            \
        }                                                                \
      }                                                                  \
    }                                                                    \
    if(k < h_)                                                           \
      for(i = 0; i < w_; ++i, ++dataPtr, ++flagsPtr)                     \
        for(j = 0; j < h_ - k; ++j)                                      \
        {                                                                \
          _flags = *flagsPtr;                                            \
        B5:                                                              \
          DEC_PASS_BACKUP(coder, B5_POS);                                \
          DEC_PASS_REF_STEP(_flags, dataPtr + j * w_, 0, j, j * 3);      \
          *flagsPtr = _flags;                                            \
        }                                                                \
    POP_MQC();                                                           \
  }
#define DEC_PASS_REF_IMPL_DIFF_FINAL(bpno, w_, h_, flagsStride)          \
  {                                                                      \
    DEC_PASS_LOCAL_VARIABLES_DIFF(flagsStride)                           \
    const int32_t poshalf = one >> 1;                                    \
    DEC_PASS_RESTORE(coder);                                             \
    for(k = 0; k < (h_ & ~3u); k += 4, dataPtr += 3 * w_, flagsPtr += 2) \
    {                                                                    \
      for(i = 0; i < w_; ++i, ++dataPtr, ++flagsPtr)                     \
      {                                                                  \
        _flags = *flagsPtr;                                              \
        if(_flags != 0)                                                  \
        {                                                                \
        B1:                                                              \
          DEC_PASS_REF_STEP(_flags, dataPtr, w_, 0, 0);                  \
        B2:                                                              \
          DEC_PASS_REF_STEP(_flags, dataPtr, w_, 1, 3);                  \
        B3:                                                              \
          DEC_PASS_REF_STEP(_flags, dataPtr, w_, 2, 6);                  \
        B4:                                                              \
          DEC_PASS_REF_STEP(_flags, dataPtr, w_, 3, 9);                  \
          *flagsPtr = _flags;                                            \
        }                                                                \
      }                                                                  \
    }                                                                    \
    if(k < h_)                                                           \
      for(i = 0; i < w_; ++i, ++dataPtr, ++flagsPtr)                     \
        for(j = 0; j < h_ - k; ++j)                                      \
        {                                                                \
          _flags = *flagsPtr;                                            \
        B5:                                                              \
          DEC_PASS_REF_STEP(_flags, dataPtr + j * w_, 0, j, j * 3);      \
          *flagsPtr = _flags;                                            \
        }                                                                \
    POP_MQC();                                                           \
  }

template<uint16_t w_, uint16_t h_>
void BlockCoder::dec_refpass_diff(int8_t bpno, uint8_t passno, uint8_t passtype)
{
  DEC_PASS_REF_IMPL_DIFF(bpno, w_, h_, w_ + 2);
}
void BlockCoder::dec_refpass_diff(int8_t bpno, uint8_t passno, uint8_t passtype)
{
  if(w_ == 64 && h_ == 64)
  {
    dec_refpass_diff<64, 64>(bpno, passno, passtype);
  }
  else
  {
    DEC_PASS_REF_IMPL_DIFF(bpno, w_, h_, w_ + 2);
  }
}
template<uint16_t w_, uint16_t h_>
void BlockCoder::dec_refpass_diff_final(int8_t bpno, uint8_t passno, uint8_t passtype)
{
  // suppress warning
  (void)passtype;
  DEC_PASS_REF_IMPL_DIFF_FINAL(bpno, w_, h_, w_ + 2);
}
void BlockCoder::dec_refpass_diff_final(int8_t bpno, uint8_t passno, uint8_t passtype)
{
  if(w_ == 64 && h_ == 64)
  {
    dec_refpass_diff_final<64, 64>(bpno, passno, passtype);
  }
  else
  {
    DEC_PASS_REF_IMPL_DIFF_FINAL(bpno, w_, h_, w_ + 2);
  }
}

template<uint16_t w_, uint16_t h_, bool vsc>
void BlockCoder::dec_clnpass_diff(int8_t bpno, uint8_t passno, uint8_t passtype)
{
  DEC_PASS_CLN_IMPL_DIFF(bpno, vsc, w_, h_, w_ + 2);
}
void BlockCoder::dec_clnpass_diff(int8_t bpno, uint8_t passno, uint8_t passtype, int32_t cblksty)
{
  if(w_ == 64 && h_ == 64)
  {
    if(cblksty & GRK_CBLKSTY_VSC)
      dec_clnpass_diff<64, 64, true>(bpno, passno, passtype);
    else
      dec_clnpass_diff<64, 64, false>(bpno, passno, passtype);
  }
  else
  {
    DEC_PASS_CLN_IMPL_DIFF(bpno, cblksty & GRK_CBLKSTY_VSC, w_, h_, w_ + 2);
  }
  checkSegSym(cblksty);
}
template<uint16_t w_, uint16_t h_, bool vsc>
void BlockCoder::dec_clnpass_diff_final(int8_t bpno, uint8_t passno, uint8_t passtype)
{
  // suppress warning
  (void)passtype;
  DEC_PASS_CLN_IMPL_DIFF_FINAL(bpno, vsc, w_, h_, w_ + 2);
}
void BlockCoder::dec_clnpass_diff_final(int8_t bpno, uint8_t passno, uint8_t passtype,
                                        int32_t cblksty)
{
  if(w_ == 64 && h_ == 64)
  {
    if(cblksty & GRK_CBLKSTY_VSC)
      dec_clnpass_diff_final<64, 64, true>(bpno, passno, passtype);
    else
      dec_clnpass_diff_final<64, 64, false>(bpno, passno, passtype);
  }
  else
  {
    DEC_PASS_CLN_IMPL_DIFF_FINAL(bpno, cblksty & GRK_CBLKSTY_VSC, w_, h_, w_ + 2);
  }
  checkSegSym(cblksty);
}

} // namespace grk
