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

#include "mqc.h"
#include <assert.h>
#include "t1_common.h"
#include "debug_context.h"
#include "mqc_state.h"

namespace grk
{

mqcoder::mqcoder(void) : mqcoder(false) {}

mqcoder::mqcoder(bool cached)
    : mqcoder_base(cached), start(nullptr), end(nullptr), buffers(nullptr), buffer_lengths(nullptr),
      num_buffers(0), cur_buffer_index(0), backup_(nullptr), overflow_(false),
      lut_ctxno_zc_orient(nullptr)
{
  if(cached)
    backup_ = new mqcoder_backup();
}

mqcoder::~mqcoder()
{
  delete backup_;
}

// Copy assignment operator
mqcoder& mqcoder::operator=(const mqcoder& other)
{
  if(this != &other)
  {
    // Call the base class assignment operator
    mqcoder_base::operator=(other);
    backup_ = nullptr;

    // Copy all member variables
    overflow_ = other.overflow_;
    start = other.start;
    end = other.end;
    buffers = other.buffers;
    buffer_lengths = other.buffer_lengths;
    num_buffers = other.num_buffers;
    cur_buffer_index = other.cur_buffer_index;
    lut_ctxno_zc_orient = other.lut_ctxno_zc_orient;
  }
  return *this;
}

bool mqcoder::operator==(const mqcoder& other) const
{
  if(!mqcoder_base::operator==(other))
  { // Compare base class members
    return false;
  }

  if(cur_buffer_index != other.cur_buffer_index)
  {
    return false;
  }

  return true;
}

mqcoder::mqcoder(const mqcoder& other) : mqcoder_base(other)
{
  *this = other;
}

void mqcoder::print(const std::string& msg)
{
  mqcoder_base::print(msg);
  printf("%s end=%p,buffer index=%d, num buffers=%d\n", msg.c_str(), end, cur_buffer_index,
         num_buffers);
}

void mqcoder::backup(void)
{
  if(!cached_)
    return;
  backup_->end_of_byte_stream_counter = end_of_byte_stream_counter;
  backup_->bp = bp;
  for(uint32_t i = 0; i < MQC_NUMCTXS; ++i)
    backup_->ctxs[i] = ctxs[i];
}

void mqcoder::restore(void)
{
  if(!backup_)
    return;
  end_of_byte_stream_counter = backup_->end_of_byte_stream_counter;
  bp = backup_->bp;
  for(uint32_t i = 0; i < MQC_NUMCTXS; ++i)
    ctxs[i] = backup_->ctxs[i];
}

void mqcoder::resetstates(void)
{
  for(uint32_t i = 0; i < MQC_NUMCTXS; i++)
    ctxs[i] = mqc_states;
  ctxs[T1_CTXNO_UNI] = mqc_states + (uint32_t)(46 << 1);
  ctxs[T1_CTXNO_AGG] = mqc_states + (uint32_t)(3 << 1);
  ctxs[T1_CTXNO_ZC] = mqc_states + (uint32_t)(4 << 1);
}

void mqcoder::reinit(void)
{
  mqcoder_base::reinit();
  resetstates();
}

void mqcoder::init_dec_common(uint8_t** buffers, uint32_t* buffer_lengths, uint16_t num_buffers)
{
  update_dec(buffers, buffer_lengths, num_buffers);
  cur_buffer_index = 0;
  start = buffers ? buffers[0] : nullptr;
  end = start + (buffer_lengths ? buffer_lengths[0] : 0);
  bp = start;
}

void mqcoder::init_dec(uint8_t** buffers, uint32_t* buffer_lengths, uint16_t num_buffers)
{
  /* Implements ISO 15444-1 C.3.5 Initialization of the decoder (INITDEC) */
  init_dec_common(buffers, buffer_lengths, num_buffers);
  CODER_SETCURCTX(this, 0);
  end_of_byte_stream_counter = 0;
  c = (uint32_t)(((buffers == nullptr) ? 0xff : *bp) << 16);
  if(buffers)
  {
    bool approaching_red = false;
    (void)approaching_red;
    uint8_t* adjusted_end = nullptr;
    (void)adjusted_end;
    DEC_BYTEIN(this, c, ct);
  }
  else
  {
    c += 0xff00;
    ct = 8;
    end_of_byte_stream_counter++;
  }
  c <<= 7;
  ct -= 7;
  a = A_MIN;
}

void mqcoder::update_dec(uint8_t** buffers, uint32_t* buffer_lengths, uint16_t num_buffers)
{
  this->buffers = buffers;
  this->buffer_lengths = buffer_lengths;
  this->num_buffers = num_buffers;
}

void mqcoder::raw_init_dec(uint8_t** buffers, uint32_t* buffer_lengths, uint16_t num_buffers)
{
  init_dec_common(buffers, buffer_lengths, num_buffers);
  c = 0;
  ct = 0;
}

///// ENCODE ////////////////////////////////////////////////////////////

uint16_t mqcoder::numbytes_enc(void)
{
  return (uint16_t)(bp - start);
}

void mqcoder::setbits_enc(void)
{
  uint32_t tempc = c + a;
  c |= 0xffff;
  if(c >= tempc)
    c -= 0x8000;
}

void mqcoder::init_enc(uint8_t* data)
{
  /* To avoid the curctx pointer to be dangling, but not strictly */
  /* required as the current context is always set before compressing */
  CODER_SETCURCTX(this, 0);

  /* As specified in Figure C.10 - Initialization of the compressor */
  /* (C.2.8 Initialization of the compressor (INITENC)) */
  a = 0x8000;
  c = 0;
  /* Yes, we point before the start of the buffer, but this is safe */
  /* given code_block_enc_allocate_data() */
  bp = data - 1;
  ct = 12;
  /* At this point we should test *(bp) against 0xFF, but this is not */
  /* necessary, as this is only used at the beginning of the code block */
  /* and our initial fake byte is set at 0 */
  assert(*(bp) != 0xff);

  start = data;
  end_of_byte_stream_counter = 0;
}

void mqcoder::flush_enc(void)
{
  /* C.2.9 Termination of coding (FLUSH) */
  /* Figure C.11 – FLUSH procedure */
  setbits_enc();
  c <<= ct;
  mqc_byteout(this);
  c <<= ct;
  mqc_byteout(this);

  /* Advance pointer if current byte != 0xff */
  /* (it is forbidden for a coding pass to end with 0xff) */
  if(*bp != 0xff)
    bp++;
}

void mqcoder::bypass_init_enc(void)
{
  /* This function is normally called after at least one mqcoder::flush() */
  /* which will have advance bp by at least 2 bytes beyond its */
  /* initial position */
  assert(bp >= start);
  c = 0;
  /* in theory we should initialize to 8, but use this special value */
  /* as a hint that mqcoder::bypass_enc() has never been called, so */
  /* as to avoid the 0xff 0x7f elimination trick in mqcoder::bypass_flush_enc() */
  /* to trigger when we don't have output any bit during this bypass sequence */
  /* Any value > 8 will do */
  ct = BYPASS_CT_INIT;
  /* Given that we are called after mqcoder::flush(), the previous byte */
  /* cannot be 0xff. */
  assert(bp[-1] != 0xff);
}

uint16_t mqcoder::bypass_get_extra_bytes_enc(bool erterm)
{
  return (ct < 7 || (ct == 7 && (erterm || bp[-1] != 0xff))) ? (1 + 1) : (0 + 1);
}

void mqcoder::bypass_flush_enc(bool erterm)
{
  /* Is there any bit remaining to be flushed ? */
  /* If the last output byte is 0xff, we can discard it, unless */
  /* erterm is required (I'm not completely sure why in erterm */
  /* we must output 0xff 0x2a if the last byte was 0xff instead of */
  /* discarding it, but Kakadu requires it when decoding */
  /* in -fussy mode) */
  if(ct < 7 || (ct == 7 && (erterm || bp[-1] != 0xff)))
  {
    uint8_t bit_value = 0;
    /* If so, fill the remaining lsbs with an alternating sequence of */
    /* 0,1,... */
    /* Note: it seems the standard only requires that for a ERTERM flush */
    /* and doesn't specify what to do for a regular BYPASS flush */
    while(ct > 0)
    {
      ct--;
      c += (uint32_t)(bit_value << ct);
      bit_value = (uint8_t)(1U - bit_value);
    }
    *bp = (uint8_t)c;
    /* Advance pointer so that mqcoder::numbytes() returns a valid value */
    bp++;
  }
  else if(ct == 7 && bp[-1] == 0xff)
  {
    /* Discard last 0xff */
    assert(!erterm);
    bp--;
  }
  else if(ct == 8 && !erterm && bp[-1] == 0x7f && bp[-2] == 0xff)
  {
    /* Tiny optimization: discard terminating 0xff 0x7f since it is */
    /* interpreted as 0xff 0x7f [0xff 0xff] by the decompressor, and given */
    /* the bit stuffing, in fact as 0xff 0xff [0xff ..] */
    bp -= 2;
  }

  assert(bp[-1] != 0xff);
}

void mqcoder::restart_init_enc(void)
{
  /* <Re-init part> */
  /* As specified in Figure C.10 - Initialization of the compressor */
  /* (C.2.8 Initialization of the compressor (INITENC)) */
  a = 0x8000;
  c = 0;
  ct = 12;
  /* This function is normally called after at least one mqcoder::flush() */
  /* which will have advance bp by at least 2 bytes beyond its */
  /* initial position */
  bp--;
  assert(bp >= start - 1);
  assert(*bp != 0xff);
  if(*bp == 0xff)
    ct = 13;
}

void mqcoder::erterm_enc(void)
{
  int32_t k = (int32_t)(11 - ct + 1);
  while(k > 0)
  {
    c <<= ct;
    ct = 0;
    mqc_byteout(this);
    k -= (int32_t)ct;
  }
  if(*bp != 0xff)
    mqc_byteout(this);
}

void mqcoder::segmark_enc(void)
{
  CODER_SETCURCTX(this, 18);
  for(uint32_t i = 1; i < 5; i++)
  {
    if((*curctx)->mps == (i & 1))
      mqc_codemps_macro(this, curctx, a, c, ct);
    else
      mqc_codelps_macro(this, curctx, a, c, ct);
  }
}

} // namespace grk
