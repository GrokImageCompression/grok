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

const uint32_t A_MIN = 0x8000;

const uint8_t B1_POS = 1;
const uint8_t B2_POS = 2;
const uint8_t B3_POS = 3;
const uint8_t B4_POS = 4;
const uint8_t B5_POS = 5;
const uint8_t B6_POS = 6;
const uint8_t B7_POS = 7;
const uint8_t B8_POS = 8;
const uint8_t B9_POS = 9;
const uint8_t B10_POS = 10;
const uint8_t B11_POS = 11;
const uint8_t B12_POS = 12;

#define DEC_PASS_LOCAL_VARIABLES(flagsStride) \
  const int32_t one = 1 << bpno;              \
  const auto mqc = &coder;                    \
  auto dataPtr = uncompressedData_;           \
  auto flagsPtr = flags_ + flagsStride + 1;   \
  grk_flag _flags = 0;                        \
  uint8_t i = 0, j = 0, k = 0;                \
  bool approaching_red;                       \
  (void)approaching_red;                      \
  PUSH_MQC();

#define DEC_PASS_LOCAL_VARIABLES_DIFF(flagsStride)                                       \
  const int32_t one = 1 << bpno;                                                         \
  const auto mqc = &coder;                                                               \
  auto dataPtr = uncompressedData_;                                                      \
  auto flagsPtr = flags_ + flagsStride + 1;                                              \
  grk_flag _flags = 0;                                                                   \
  uint8_t i = 0, j = 0, k = 0;                                                           \
  const mqc_state** curctx;                                                              \
  uint32_t c, a;                                                                         \
  uint8_t ct;                                                                            \
  bool approaching_red = (coder.backup_->i == BACKUP_DISABLED) &&                        \
                         mqc->bp + red_zone >= mqc->end &&                               \
                         ((coder.cur_buffer_index + 1 >= coder.num_buffers) ||           \
                          coder.buffer_lengths[coder.cur_buffer_index + 1] <= red_zone); \
  (void)approaching_red;

// BACKUP/RESTORE ///////////////////////////////////////////////////////

const uint8_t red_zone = 6;

#define DEC_PASS_HAS_BACKUP_FOR_CURRENT_PASS(coder) \
  (coder.backup_->i != BACKUP_DISABLED) && (passno == coder.backup_->passno_)

#define DEC_PASS_MQC_BACKUP_LOCAL(coder)                  \
  coder.backup_->a = a;                                   \
  coder.backup_->c = c;                                   \
  coder.backup_->ct = ct;                                 \
  coder.backup_->curctx_index_ = curctx - &coder.ctxs[0]; \
  coder.backup_->curctx = &coder.backup_->ctxs[coder.backup_->curctx_index_];

#define DEC_PASS_MQC_RESTORE_LOCAL(coder)             \
  a = coder.backup_->a;                               \
  c = coder.backup_->c;                               \
  ct = coder.backup_->ct;                             \
  coder.curctx_index_ = coder.backup_->curctx_index_; \
  curctx = &coder.ctxs[coder.curctx_index_];          \
  coder.curctx = curctx;                              \
  coder.a = a;                                        \
  coder.c = c;                                        \
  coder.ct = ct;

#define DEC_PASS_DO_BACKUP(coder, p)      \
  decompressBackup();                     \
  DEC_PASS_MQC_BACKUP_LOCAL(coder);       \
  auto b = coder.backup_;                 \
  b->position = p;                        \
  b->i = i;                               \
  b->j = j;                               \
  b->k = k;                               \
  b->flagsPtr_ = flagsPtr;                \
  b->_flags = _flags;                     \
  b->dataPtr_ = dataPtr;                  \
  b->passno_ = passno;                    \
  b->passtype_ = passtype;                \
  b->numBpsToDecompress_ = (uint8_t)bpno; \
  approaching_red = false;                \
  b->layer_ = (uint16_t)coder.cur_buffer_index;

#define DEC_PASS_BACKUP(coder, p) \
  if(approaching_red)             \
  {                               \
    DEC_PASS_DO_BACKUP(coder, p)  \
  }

#define DEC_PASS_CLN_BACKUP(p)        \
  if(approaching_red)                 \
  {                                   \
    DEC_PASS_DO_BACKUP(coder, p);     \
    coder.backup_->runlen = runlen;   \
    coder.backup_->partial = partial; \
  }

#define DEC_PASS_RESTORE(coder)                                                                 \
  do                                                                                            \
  {                                                                                             \
    if(DEC_PASS_HAS_BACKUP_FOR_CURRENT_PASS(coder))                                             \
    {                                                                                           \
      DEC_PASS_MQC_RESTORE_LOCAL(coder);                                                        \
      auto b = coder.backup_;                                                                   \
      i = b->i;                                                                                 \
      j = b->j;                                                                                 \
      k = b->k;                                                                                 \
      flagsPtr = b->flagsPtr_;                                                                  \
      _flags = b->_flags;                                                                       \
      dataPtr = b->dataPtr_;                                                                    \
      coder.cur_buffer_index = b->layer_;                                                       \
      coder.end =                                                                               \
          coder.buffers[coder.cur_buffer_index] + coder.buffer_lengths[coder.cur_buffer_index]; \
      approaching_red = mqc->bp + red_zone >= mqc->end &&                                       \
                        ((coder.cur_buffer_index + 1 >= coder.num_buffers) ||                   \
                         coder.buffer_lengths[coder.cur_buffer_index + 1] <= red_zone);         \
      b->i = BACKUP_DISABLED;                                                                   \
      assert(b->passno_ == passno);                                                             \
      assert(b->passtype_ == passtype);                                                         \
      assert(b->numBpsToDecompress_ == (uint8_t)bpno);                                          \
      switch(b->position)                                                                       \
      {                                                                                         \
        case B1_POS:                                                                            \
          goto B1;                                                                              \
        case B2_POS:                                                                            \
          goto B2;                                                                              \
        case B3_POS:                                                                            \
          goto B3;                                                                              \
        case B4_POS:                                                                            \
          goto B4;                                                                              \
        case B5_POS:                                                                            \
          goto B5;                                                                              \
      }                                                                                         \
    }                                                                                           \
    else                                                                                        \
    {                                                                                           \
      curctx = coder.curctx;                                                                    \
      c = coder.c;                                                                              \
      a = coder.a;                                                                              \
      ct = coder.ct;                                                                            \
    }                                                                                           \
  } while(0)

#define DEC_PASS_CLN_RESTORE(coder)                                                             \
  do                                                                                            \
  {                                                                                             \
    if(DEC_PASS_HAS_BACKUP_FOR_CURRENT_PASS(coder))                                             \
    {                                                                                           \
      DEC_PASS_MQC_RESTORE_LOCAL(coder);                                                        \
      auto b = coder.backup_;                                                                   \
      i = b->i;                                                                                 \
      j = b->j;                                                                                 \
      k = b->k;                                                                                 \
      runlen = coder.backup_->runlen;                                                           \
      partial = coder.backup_->partial;                                                         \
      flagsPtr = b->flagsPtr_;                                                                  \
      _flags = b->_flags;                                                                       \
      dataPtr = b->dataPtr_;                                                                    \
      coder.cur_buffer_index = b->layer_;                                                       \
      coder.end =                                                                               \
          coder.buffers[coder.cur_buffer_index] + coder.buffer_lengths[coder.cur_buffer_index]; \
      approaching_red = mqc->bp + red_zone >= mqc->end &&                                       \
                        ((coder.cur_buffer_index + 1 >= coder.num_buffers) ||                   \
                         coder.buffer_lengths[coder.cur_buffer_index + 1] <= red_zone);         \
      b->i = BACKUP_DISABLED;                                                                   \
      assert(b->passno_ == passno);                                                             \
      assert(b->passtype_ == passtype);                                                         \
      assert(b->numBpsToDecompress_ == (uint8_t)bpno);                                          \
      switch(b->position)                                                                       \
      {                                                                                         \
        case B1_POS:                                                                            \
          goto B1;                                                                              \
        case B2_POS:                                                                            \
          goto B2;                                                                              \
        case B3_POS:                                                                            \
          goto B3;                                                                              \
        case B4_POS:                                                                            \
          goto B4;                                                                              \
        case B5_POS:                                                                            \
          goto B5;                                                                              \
        case B6_POS:                                                                            \
          goto B6;                                                                              \
        case B7_POS:                                                                            \
          goto B7;                                                                              \
        case B8_POS:                                                                            \
          goto B8;                                                                              \
        case B9_POS:                                                                            \
          goto B9;                                                                              \
        case B10_POS:                                                                           \
          goto B10;                                                                             \
        case B11_POS:                                                                           \
          goto B11;                                                                             \
        case B12_POS:                                                                           \
          goto B12;                                                                             \
      }                                                                                         \
    }                                                                                           \
    else                                                                                        \
    {                                                                                           \
      curctx = coder.curctx;                                                                    \
      c = coder.c;                                                                              \
      a = coder.a;                                                                              \
      ct = coder.ct;                                                                            \
    }                                                                                           \
  } while(0)

/////////////////////////////////////////////////////////////

/**
 * @brief Returns next byte in code stream.
 * A list of encoded buffers is managed to act like a single contiguous encoded buffer
 * @param mqc MQ coder
 * @param c MQ c variable
 * @param ct MQ ct variable
 */
#define DEC_BYTEIN(mqc, c, ct)                                                          \
  {                                                                                     \
    uint8_t cur = 0xff;                                                                 \
    if(mqc->bp >= mqc->end)                                                             \
    {                                                                                   \
      if(mqc->cur_buffer_index + 1 < mqc->num_buffers)                                  \
      {                                                                                 \
        /* Move to the next buffer */                                                   \
        mqc->cur_buffer_index++;                                                        \
        mqc->bp = mqc->buffers[mqc->cur_buffer_index];                                  \
        mqc->end = mqc->bp + mqc->buffer_lengths[mqc->cur_buffer_index];                \
        cur = *mqc->bp;                                                                 \
        approaching_red = mqc->backup_ && (mqc->backup_->i == BACKUP_DISABLED) &&       \
                          mqc->bp + red_zone >= mqc->end &&                             \
                          ((mqc->cur_buffer_index + 1 >= mqc->num_buffers) ||           \
                           mqc->buffer_lengths[mqc->cur_buffer_index + 1] <= red_zone); \
      }                                                                                 \
    }                                                                                   \
    else                                                                                \
    {                                                                                   \
      cur = *mqc->bp;                                                                   \
    }                                                                                   \
    uint8_t next = 0xff;                                                                \
    if(mqc->bp + 1 >= mqc->end)                                                         \
    {                                                                                   \
      if(mqc->cur_buffer_index + 1 < mqc->num_buffers)                                  \
      {                                                                                 \
        /* Peek into the next buffer for the next byte */                               \
        next = *mqc->buffers[mqc->cur_buffer_index + 1];                                \
      }                                                                                 \
      else                                                                              \
      {                                                                                 \
        mqc->overflow_ = true;                                                          \
      }                                                                                 \
    }                                                                                   \
    else                                                                                \
    {                                                                                   \
      next = *(mqc->bp + 1);                                                            \
    }                                                                                   \
    uint8_t curff = (cur == 0xff);                                                      \
    uint8_t is_end = curff & (next > 0x8f);                                             \
    if(is_end)                                                                          \
    {                                                                                   \
      c += 0xff00;                                                                      \
      ct = 8;                                                                           \
      mqc->end_of_byte_stream_counter++;                                                \
    }                                                                                   \
    else                                                                                \
    {                                                                                   \
      mqc->bp++;                                                                        \
      approaching_red = mqc->backup_ && (mqc->backup_->i == BACKUP_DISABLED) &&         \
                        mqc->bp + red_zone >= mqc->end &&                               \
                        ((mqc->cur_buffer_index + 1 >= mqc->num_buffers) ||             \
                         mqc->buffer_lengths[mqc->cur_buffer_index + 1] <= red_zone);   \
      c += next << (8 + curff);                                                         \
      ct = 8 - curff;                                                                   \
    }                                                                                   \
  }

#define DEC_RENORM(mqc, a, c, ct)            \
  {                                          \
    do                                       \
    {                                        \
      if(ct == 0)                            \
      {                                      \
        if(mqc->start == mqc->end)           \
        {                                    \
          c += 0xff00;                       \
          ct = 8;                            \
          mqc->end_of_byte_stream_counter++; \
        }                                    \
        else                                 \
          DEC_BYTEIN(mqc, c, ct);            \
      }                                      \
      a <<= 1;                               \
      c <<= 1;                               \
      ct--;                                  \
    } while(a < A_MIN);                      \
  }

/**
 * @brief  Implements ISO 15444-1 C.3.2 Decompressing a decision (DECODE)
 * @param d variable to hold decoded symbol
 * @param mqc MQ coder
 * @param curctx current context
 * @param a MQ variable a
 * @param c MQ variable c
 * @param ct MQ variable ct
 */
#define DEC_SYMBOL(d, mqc, curctx, a, c, ct)         \
  do                                                 \
  {                                                  \
    auto ctx = *(curctx);                            \
    a -= ctx->qeval;                                 \
    uint32_t qeval_shift = ctx->qeval << 16;         \
    if(c < qeval_shift)                              \
    {                                                \
      uint32_t mask = (uint32_t)-(a < ctx->qeval);   \
      a = ctx->qeval;                                \
      d = ctx->mps ^ (~mask & 1);                    \
      *curctx = mask ? ctx->nmps : ctx->nlps;        \
      DEC_RENORM(mqc, a, c, ct);                     \
    }                                                \
    else                                             \
    {                                                \
      c -= qeval_shift;                              \
      if(a < A_MIN)                                  \
      {                                              \
        uint32_t mask = (uint32_t)-(a < ctx->qeval); \
        d = ctx->mps ^ (mask & 1);                   \
        *curctx = mask ? ctx->nlps : ctx->nmps;      \
        DEC_RENORM(mqc, a, c, ct);                   \
      }                                              \
      else                                           \
      {                                              \
        d = ctx->mps;                                \
      }                                              \
    }                                                \
  } while(0)

////////////////////////////////////////////////////////////////////
/**
 * @brief Decodes a Raw-encoded pass
 */
#define DEC_SYMBOL_RAW()                                                       \
  {                                                                            \
    if(coder.ct == 0)                                                          \
    {                                                                          \
      uint8_t curr = 0xff;                                                     \
      if(coder.bp >= coder.end)                                                \
      {                                                                        \
        if(coder.cur_buffer_index + 1 < coder.num_buffers)                     \
        {                                                                      \
          coder.cur_buffer_index++;                                            \
          coder.bp = coder.buffers[coder.cur_buffer_index];                    \
          coder.end = coder.bp + coder.buffer_lengths[coder.cur_buffer_index]; \
          curr = *coder.bp;                                                    \
        }                                                                      \
      }                                                                        \
      else                                                                     \
      {                                                                        \
        curr = *coder.bp;                                                      \
      }                                                                        \
      if(coder.c == 0xff)                                                      \
      {                                                                        \
        if(curr > 0x8f)                                                        \
        {                                                                      \
          coder.c = 0xff;                                                      \
          coder.ct = 8;                                                        \
        }                                                                      \
        else                                                                   \
        {                                                                      \
          coder.c = curr;                                                      \
          coder.bp++;                                                          \
          coder.ct = 7;                                                        \
        }                                                                      \
      }                                                                        \
      else                                                                     \
      {                                                                        \
        coder.c = curr;                                                        \
        coder.bp++;                                                            \
        coder.ct = 8;                                                          \
      }                                                                        \
    }                                                                          \
    coder.ct--;                                                                \
    v = ((uint8_t)coder.c >> coder.ct) & 0x1U;                                 \
  }
