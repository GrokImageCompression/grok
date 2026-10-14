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

#include <cassert>

#include "mqc_base.h"
#include "mqc_enc_inl.h"

namespace mercury_native::t1enc
{

void mqcoder::resetstates(void)
{
  for(uint32_t i = 0; i < MQC_NUMCTXS; i++)
    ctxs[i] = mqc_states;
  ctxs[T1_CTXNO_UNI] = mqc_states + (uint32_t)(46 << 1);
  ctxs[T1_CTXNO_AGG] = mqc_states + (uint32_t)(3 << 1);
  ctxs[T1_CTXNO_ZC] = mqc_states + (uint32_t)(4 << 1);
}

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
  curctx = ctxs;

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

} // namespace mercury_native::t1enc
