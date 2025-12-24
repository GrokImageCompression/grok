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

#pragma once

namespace grk
{

// Decode Cleanup Pass

#define DEC_PASS_CLN_STEP(checkFlags, partial, flags, flagsPtr, flagsStride, data, dataStride, \
                          ciorig, ci, vsc)                                                     \
  do                                                                                           \
  {                                                                                            \
    if(!checkFlags || !(flags & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))))                       \
    {                                                                                          \
      uint8_t v;                                                                               \
      if(!partial)                                                                             \
      {                                                                                        \
        uint8_t ctxt1 = GETCTXNO_ZC(mqc, flags >> (ci));                                       \
        SETCURCTX(curctx, ctxt1);                                                              \
        DEC_SYMBOL(v, mqc, curctx, a, c, ct);                                                  \
        if(!v)                                                                                 \
        {                                                                                      \
          break;                                                                               \
        }                                                                                      \
      }                                                                                        \
      uint8_t lu = getctxtno_sc_or_spb_index(flags, flagsPtr[-1], flagsPtr[1], ci);            \
      SETCURCTX(curctx, lut_ctxno_sc[lu]);                                                     \
      DEC_SYMBOL(v, mqc, curctx, a, c, ct);                                                    \
      v = v ^ lut_spb[lu];                                                                     \
      (data)[ciorig * dataStride] = v ? -oneplushalf : oneplushalf;                            \
      UPDATE_FLAGS(flags, flagsPtr, ci, v, flagsStride, vsc);                                  \
    }                                                                                          \
  } while(0)

#define DEC_PASS_CLN_IMPL(bpno, vsc, w_, h_, flagsStride)                                          \
  {                                                                                                \
    DEC_PASS_LOCAL_VARIABLES(flagsStride)                                                          \
    const int32_t half = one >> 1;                                                                 \
    const int32_t oneplushalf = one | half;                                                        \
    uint8_t d = 0;                                                                                 \
    uint8_t runlen = 0;                                                                            \
    bool partial = false;                                                                          \
    for(k = 0; k < (h_ & ~3u); k += 4, dataPtr += 3 * w_, flagsPtr += 2)                           \
    {                                                                                              \
      for(i = 0; i < w_; ++i, ++dataPtr, ++flagsPtr)                                               \
      {                                                                                            \
        _flags = *flagsPtr;                                                                        \
        if(_flags == 0)                                                                            \
        {                                                                                          \
          partial = true;                                                                          \
          SETCURCTX(curctx, T1_CTXNO_AGG);                                                         \
          DEC_SYMBOL(d, mqc, curctx, a, c, ct);                                                    \
          if(!d)                                                                                   \
            continue;                                                                              \
          SETCURCTX(curctx, T1_CTXNO_UNI);                                                         \
          DEC_SYMBOL(runlen, mqc, curctx, a, c, ct);                                               \
          DEC_SYMBOL(d, mqc, curctx, a, c, ct);                                                    \
          runlen = (runlen << 1) | d;                                                              \
          switch(runlen)                                                                           \
          {                                                                                        \
            case 0:                                                                                \
              DEC_PASS_CLN_STEP(false, true, _flags, flagsPtr, flagsStride, dataPtr, w_, 0, 0,     \
                                vsc);                                                              \
              partial = false;                                                                     \
              /* FALLTHRU */                                                                       \
            case 1:                                                                                \
              DEC_PASS_CLN_STEP(false, partial, _flags, flagsPtr, flagsStride, dataPtr, w_, 1, 3,  \
                                false);                                                            \
              partial = false;                                                                     \
              /* FALLTHRU */                                                                       \
            case 2:                                                                                \
              DEC_PASS_CLN_STEP(false, partial, _flags, flagsPtr, flagsStride, dataPtr, w_, 2, 6,  \
                                false);                                                            \
              partial = false;                                                                     \
              /* FALLTHRU */                                                                       \
            case 3:                                                                                \
              DEC_PASS_CLN_STEP(false, partial, _flags, flagsPtr, flagsStride, dataPtr, w_, 3, 9,  \
                                false);                                                            \
              break;                                                                               \
          }                                                                                        \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, flagsStride, dataPtr, w_, 0, 0, vsc);   \
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, flagsStride, dataPtr, w_, 1, 3, false); \
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, flagsStride, dataPtr, w_, 2, 6, false); \
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
          DEC_PASS_CLN_STEP(true, false, _flags, flagsPtr, w_ + 2U, dataPtr + j * w_, 0, j, j * 3, \
                            vsc);                                                                  \
          *flagsPtr = _flags;                                                                      \
        }                                                                                          \
        *flagsPtr &= ~(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);                                     \
      }                                                                                            \
    }                                                                                              \
    POP_MQC();                                                                                     \
  }

#define DEC_PASS_SIG_STEP(flags, flagsPtr, flagsStride, data, dataStride, ciorig, ci, vsc) \
  do                                                                                       \
  {                                                                                        \
    if((flags & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))) == 0U &&                           \
       (flags & (T1_SIGMA_NEIGHBOURS << (ci))) != 0U)                                      \
    {                                                                                      \
      uint8_t ctxt1 = GETCTXNO_ZC(mqc, flags >> (ci));                                     \
      SETCURCTX(curctx, ctxt1);                                                            \
      uint8_t v;                                                                           \
      DEC_SYMBOL(v, mqc, curctx, a, c, ct);                                                \
      if(v)                                                                                \
      {                                                                                    \
        uint8_t lu = getctxtno_sc_or_spb_index(flags, flagsPtr[-1], flagsPtr[1], ci);      \
        uint8_t ctxt2 = lut_ctxno_sc[lu];                                                  \
        uint8_t spb = lut_spb[lu];                                                         \
        SETCURCTX(curctx, ctxt2);                                                          \
        DEC_SYMBOL(v, mqc, curctx, a, c, ct);                                              \
        v = v ^ spb;                                                                       \
        (data)[(ciorig) * dataStride] = v ? -oneplushalf : oneplushalf;                    \
        UPDATE_FLAGS(flags, flagsPtr, ci, v, flagsStride, vsc);                            \
      }                                                                                    \
      flags |= T1_PI_THIS << (ci);                                                         \
    }                                                                                      \
  } while(0)

#define DEC_PASS_SIG_IMPL(bpno, vsc, w_, h_, flagsStride)                                       \
  {                                                                                             \
    DEC_PASS_LOCAL_VARIABLES(flagsStride)                                                       \
    const int32_t half = one >> 1;                                                              \
    const int32_t oneplushalf = one | half;                                                     \
    for(k = 0; k < (h_ & ~3u); k += 4, dataPtr += 3 * w_, flagsPtr += 2)                        \
    {                                                                                           \
      for(i = 0; i < w_; ++i, ++dataPtr, ++flagsPtr)                                            \
      {                                                                                         \
        _flags = *flagsPtr;                                                                     \
        if(_flags != 0)                                                                         \
        {                                                                                       \
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr, w_, 0, 0, vsc);             \
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr, w_, 1, 3, false);           \
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr, w_, 2, 6, false);           \
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
          DEC_PASS_SIG_STEP(_flags, flagsPtr, flagsStride, dataPtr + j * w_, 0, j, 3 * j, vsc); \
          *flagsPtr = _flags;                                                                   \
        }                                                                                       \
    POP_MQC();                                                                                  \
  }

// Decode Magnitude Refinement Pass
#define DEC_PASS_REF_STEP(flags, data, dataStride, ciorig, ci)                                     \
  do                                                                                               \
  {                                                                                                \
    if((flags & ((T1_SIGMA_THIS | T1_PI_THIS) << (ci))) == (T1_SIGMA_THIS << (ci)))                \
    {                                                                                              \
      uint8_t ctxno = GETCTXNO_MAG(flags >> (ci));                                                 \
      SETCURCTX(curctx, ctxno);                                                                    \
      uint8_t v;                                                                                   \
      DEC_SYMBOL(v, mqc, curctx, a, c, ct);                                                        \
      (data)[ciorig * dataStride] += (v ^ ((data)[ciorig * dataStride] < 0)) ? poshalf : -poshalf; \
      flags |= T1_MU_THIS << (ci);                                                                 \
    }                                                                                              \
  } while(0)

#define DEC_PASS_REF_IMPL(bpno, w_, h_, flagsStride)                     \
  {                                                                      \
    DEC_PASS_LOCAL_VARIABLES(flagsStride)                                \
    const int32_t poshalf = one >> 1;                                    \
    for(k = 0; k < (h_ & ~3u); k += 4, dataPtr += 3 * w_, flagsPtr += 2) \
    {                                                                    \
      for(i = 0; i < w_; ++i, ++dataPtr, ++flagsPtr)                     \
      {                                                                  \
        _flags = *flagsPtr;                                              \
        if(_flags != 0)                                                  \
        {                                                                \
          DEC_PASS_REF_STEP(_flags, dataPtr, w_, 0, 0);                  \
          DEC_PASS_REF_STEP(_flags, dataPtr, w_, 1, 3);                  \
          DEC_PASS_REF_STEP(_flags, dataPtr, w_, 2, 6);                  \
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
          DEC_PASS_REF_STEP(_flags, dataPtr + j * w_, 0, j, j * 3);      \
          *flagsPtr = _flags;                                            \
        }                                                                \
    POP_MQC();                                                           \
  }

} // namespace grk