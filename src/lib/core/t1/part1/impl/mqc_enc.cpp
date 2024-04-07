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

#include "t1_common.h"

#include <assert.h>

namespace grk
{
void mqc_byteout(mqcoder* mqc);
static void mqc_renorm_enc(mqcoder* mqc);
static void mqc_codemps_enc(mqcoder* mqc);
static void mqc_codelps_enc(mqcoder* mqc);
static void mqc_setbits_enc(mqcoder* mqc);

static const mqc_state mqc_states[47 * 2] = {
	{0x5601, 0, &mqc_states[2], &mqc_states[3]},   {0x5601, 1, &mqc_states[3], &mqc_states[2]},
	{0x3401, 0, &mqc_states[4], &mqc_states[12]},  {0x3401, 1, &mqc_states[5], &mqc_states[13]},
	{0x1801, 0, &mqc_states[6], &mqc_states[18]},  {0x1801, 1, &mqc_states[7], &mqc_states[19]},
	{0x0ac1, 0, &mqc_states[8], &mqc_states[24]},  {0x0ac1, 1, &mqc_states[9], &mqc_states[25]},
	{0x0521, 0, &mqc_states[10], &mqc_states[58]}, {0x0521, 1, &mqc_states[11], &mqc_states[59]},
	{0x0221, 0, &mqc_states[76], &mqc_states[66]}, {0x0221, 1, &mqc_states[77], &mqc_states[67]},
	{0x5601, 0, &mqc_states[14], &mqc_states[13]}, {0x5601, 1, &mqc_states[15], &mqc_states[12]},
	{0x5401, 0, &mqc_states[16], &mqc_states[28]}, {0x5401, 1, &mqc_states[17], &mqc_states[29]},
	{0x4801, 0, &mqc_states[18], &mqc_states[28]}, {0x4801, 1, &mqc_states[19], &mqc_states[29]},
	{0x3801, 0, &mqc_states[20], &mqc_states[28]}, {0x3801, 1, &mqc_states[21], &mqc_states[29]},
	{0x3001, 0, &mqc_states[22], &mqc_states[34]}, {0x3001, 1, &mqc_states[23], &mqc_states[35]},
	{0x2401, 0, &mqc_states[24], &mqc_states[36]}, {0x2401, 1, &mqc_states[25], &mqc_states[37]},
	{0x1c01, 0, &mqc_states[26], &mqc_states[40]}, {0x1c01, 1, &mqc_states[27], &mqc_states[41]},
	{0x1601, 0, &mqc_states[58], &mqc_states[42]}, {0x1601, 1, &mqc_states[59], &mqc_states[43]},
	{0x5601, 0, &mqc_states[30], &mqc_states[29]}, {0x5601, 1, &mqc_states[31], &mqc_states[28]},
	{0x5401, 0, &mqc_states[32], &mqc_states[28]}, {0x5401, 1, &mqc_states[33], &mqc_states[29]},
	{0x5101, 0, &mqc_states[34], &mqc_states[30]}, {0x5101, 1, &mqc_states[35], &mqc_states[31]},
	{0x4801, 0, &mqc_states[36], &mqc_states[32]}, {0x4801, 1, &mqc_states[37], &mqc_states[33]},
	{0x3801, 0, &mqc_states[38], &mqc_states[34]}, {0x3801, 1, &mqc_states[39], &mqc_states[35]},
	{0x3401, 0, &mqc_states[40], &mqc_states[36]}, {0x3401, 1, &mqc_states[41], &mqc_states[37]},
	{0x3001, 0, &mqc_states[42], &mqc_states[38]}, {0x3001, 1, &mqc_states[43], &mqc_states[39]},
	{0x2801, 0, &mqc_states[44], &mqc_states[38]}, {0x2801, 1, &mqc_states[45], &mqc_states[39]},
	{0x2401, 0, &mqc_states[46], &mqc_states[40]}, {0x2401, 1, &mqc_states[47], &mqc_states[41]},
	{0x2201, 0, &mqc_states[48], &mqc_states[42]}, {0x2201, 1, &mqc_states[49], &mqc_states[43]},
	{0x1c01, 0, &mqc_states[50], &mqc_states[44]}, {0x1c01, 1, &mqc_states[51], &mqc_states[45]},
	{0x1801, 0, &mqc_states[52], &mqc_states[46]}, {0x1801, 1, &mqc_states[53], &mqc_states[47]},
	{0x1601, 0, &mqc_states[54], &mqc_states[48]}, {0x1601, 1, &mqc_states[55], &mqc_states[49]},
	{0x1401, 0, &mqc_states[56], &mqc_states[50]}, {0x1401, 1, &mqc_states[57], &mqc_states[51]},
	{0x1201, 0, &mqc_states[58], &mqc_states[52]}, {0x1201, 1, &mqc_states[59], &mqc_states[53]},
	{0x1101, 0, &mqc_states[60], &mqc_states[54]}, {0x1101, 1, &mqc_states[61], &mqc_states[55]},
	{0x0ac1, 0, &mqc_states[62], &mqc_states[56]}, {0x0ac1, 1, &mqc_states[63], &mqc_states[57]},
	{0x09c1, 0, &mqc_states[64], &mqc_states[58]}, {0x09c1, 1, &mqc_states[65], &mqc_states[59]},
	{0x08a1, 0, &mqc_states[66], &mqc_states[60]}, {0x08a1, 1, &mqc_states[67], &mqc_states[61]},
	{0x0521, 0, &mqc_states[68], &mqc_states[62]}, {0x0521, 1, &mqc_states[69], &mqc_states[63]},
	{0x0441, 0, &mqc_states[70], &mqc_states[64]}, {0x0441, 1, &mqc_states[71], &mqc_states[65]},
	{0x02a1, 0, &mqc_states[72], &mqc_states[66]}, {0x02a1, 1, &mqc_states[73], &mqc_states[67]},
	{0x0221, 0, &mqc_states[74], &mqc_states[68]}, {0x0221, 1, &mqc_states[75], &mqc_states[69]},
	{0x0141, 0, &mqc_states[76], &mqc_states[70]}, {0x0141, 1, &mqc_states[77], &mqc_states[71]},
	{0x0111, 0, &mqc_states[78], &mqc_states[72]}, {0x0111, 1, &mqc_states[79], &mqc_states[73]},
	{0x0085, 0, &mqc_states[80], &mqc_states[74]}, {0x0085, 1, &mqc_states[81], &mqc_states[75]},
	{0x0049, 0, &mqc_states[82], &mqc_states[76]}, {0x0049, 1, &mqc_states[83], &mqc_states[77]},
	{0x0025, 0, &mqc_states[84], &mqc_states[78]}, {0x0025, 1, &mqc_states[85], &mqc_states[79]},
	{0x0015, 0, &mqc_states[86], &mqc_states[80]}, {0x0015, 1, &mqc_states[87], &mqc_states[81]},
	{0x0009, 0, &mqc_states[88], &mqc_states[82]}, {0x0009, 1, &mqc_states[89], &mqc_states[83]},
	{0x0005, 0, &mqc_states[90], &mqc_states[84]}, {0x0005, 1, &mqc_states[91], &mqc_states[85]},
	{0x0001, 0, &mqc_states[90], &mqc_states[86]}, {0x0001, 1, &mqc_states[91], &mqc_states[87]},
	{0x5601, 0, &mqc_states[92], &mqc_states[92]}, {0x5601, 1, &mqc_states[93], &mqc_states[93]},
};

/* ENCODE */

void mqc_byteout(mqcoder* mqc)
{
   /* bp is initialized to start - 1 in mqc_init_enc() */
   /* but this is safe, see code_block_enc_allocate_data() */
   assert(mqc->bp >= mqc->start - 1);
   if(*mqc->bp == 0xff)
   {
	  mqc->bp++;
	  *mqc->bp = (uint8_t)(mqc->c >> 20);
	  mqc->c &= 0xfffff;
	  mqc->ct = 7;
   }
   else
   {
	  if((mqc->c & 0x8000000) == 0)
	  {
		 mqc->bp++;
		 *mqc->bp = (uint8_t)(mqc->c >> 19);
		 mqc->c &= 0x7ffff;
		 mqc->ct = 8;
	  }
	  else
	  {
		 (*mqc->bp)++;
		 if(*mqc->bp == 0xff)
		 {
			mqc->c &= 0x7ffffff;
			mqc->bp++;
			*mqc->bp = (uint8_t)(mqc->c >> 20);
			mqc->c &= 0xfffff;
			mqc->ct = 7;
		 }
		 else
		 {
			mqc->bp++;
			*mqc->bp = (uint8_t)(mqc->c >> 19);
			mqc->c &= 0x7ffff;
			mqc->ct = 8;
		 }
	  }
   }
}

static void mqc_renorm_enc(mqcoder* mqc)
{
   do
   {
	  mqc->a <<= 1;
	  mqc->c <<= 1;
	  mqc->ct--;
	  if(mqc->ct == 0)
		 mqc_byteout(mqc);
   } while((mqc->a & 0x8000) == 0);
}

static void mqc_codemps_enc(mqcoder* mqc)
{
   mqc->a -= (*mqc->curctx)->qeval;
   if((mqc->a & 0x8000) == 0)
   {
	  if(mqc->a < (*mqc->curctx)->qeval)
		 mqc->a = (*mqc->curctx)->qeval;
	  else
		 mqc->c += (*mqc->curctx)->qeval;
	  *mqc->curctx = (*mqc->curctx)->nmps;
	  mqc_renorm_enc(mqc);
   }
   else
   {
	  mqc->c += (*mqc->curctx)->qeval;
   }
}

static void mqc_codelps_enc(mqcoder* mqc)
{
   mqc->a -= (*mqc->curctx)->qeval;
   if(mqc->a < (*mqc->curctx)->qeval)
	  mqc->c += (*mqc->curctx)->qeval;
   else
	  mqc->a = (*mqc->curctx)->qeval;
   *mqc->curctx = (*mqc->curctx)->nlps;
   mqc_renorm_enc(mqc);
}

static void mqc_setbits_enc(mqcoder* mqc)
{
   uint32_t tempc = mqc->c + mqc->a;
   mqc->c |= 0xffff;
   if(mqc->c >= tempc)
	  mqc->c -= 0x8000;
}
uint32_t mqc_numbytes_enc(mqcoder* mqc)
{
   return (uint32_t)(mqc->bp - mqc->start);
}

void mqc_init_enc(mqcoder* mqc, uint8_t* bp)
{
   /* To avoid the curctx pointer to be dangling, but not strictly */
   /* required as the current context is always set before compressing */
   mqc_setcurctx(mqc, 0);

   /* As specified in Figure C.10 - Initialization of the compressor */
   /* (C.2.8 Initialization of the compressor (INITENC)) */
   mqc->a = 0x8000;
   mqc->c = 0;
   /* Yes, we point before the start of the buffer, but this is safe */
   /* given code_block_enc_allocate_data() */
   mqc->bp = bp - 1;
   mqc->ct = 12;
   /* At this point we should test *(mqc->bp) against 0xFF, but this is not */
   /* necessary, as this is only used at the beginning of the code block */
   /* and our initial fake byte is set at 0 */
   assert(*(mqc->bp) != 0xff);

   mqc->start = bp;
   mqc->end_of_byte_stream_counter = 0;
}

void mqc_encode(mqcoder* mqc, uint32_t d)
{
   if((*mqc->curctx)->mps == d)
	  mqc_codemps_enc(mqc);
   else
	  mqc_codelps_enc(mqc);
}

void mqc_flush_enc(mqcoder* mqc)
{
   /* C.2.9 Termination of coding (FLUSH) */
   /* Figure C.11 – FLUSH procedure */
   mqc_setbits_enc(mqc);
   mqc->c <<= mqc->ct;
   mqc_byteout(mqc);
   mqc->c <<= mqc->ct;
   mqc_byteout(mqc);

   /* Advance pointer if current byte != 0xff */
   /* (it is forbidden that a coding pass ends with 0xff) */
   if(*mqc->bp != 0xff)
	  mqc->bp++;
}

void mqc_bypass_init_enc(mqcoder* mqc)
{
   /* This function is normally called after at least one mqc_flush() */
   /* which will have advance mqc->bp by at least 2 bytes beyond its */
   /* initial position */
   assert(mqc->bp >= mqc->start);
   mqc->c = 0;
   /* in theory we should initialize to 8, but use this special value */
   /* as a hint that mqc_bypass_enc() has never been called, so */
   /* as to avoid the 0xff 0x7f elimination trick in mqc_bypass_flush_enc() */
   /* to trigger when we don't have output any bit during this bypass sequence */
   /* Any value > 8 will do */
   mqc->ct = BYPASS_CT_INIT;
   /* Given that we are called after mqc_flush(), the previous byte */
   /* cannot be 0xff. */
   assert(mqc->bp[-1] != 0xff);
}

uint32_t mqc_bypass_get_extra_bytes_enc(mqcoder* mqc, bool erterm)
{
   return (mqc->ct < 7 || (mqc->ct == 7 && (erterm || mqc->bp[-1] != 0xff))) ? (1 + 1) : (0 + 1);
}

void mqc_bypass_flush_enc(mqcoder* mqc, bool erterm)
{
   /* Is there any bit remaining to be flushed ? */
   /* If the last output byte is 0xff, we can discard it, unless */
   /* erterm is required (I'm not completely sure why in erterm */
   /* we must output 0xff 0x2a if the last byte was 0xff instead of */
   /* discarding it, but Kakadu requires it when decoding */
   /* in -fussy mode) */
   if(mqc->ct < 7 || (mqc->ct == 7 && (erterm || mqc->bp[-1] != 0xff)))
   {
	  uint8_t bit_value = 0;
	  /* If so, fill the remaining lsbs with an alternating sequence of */
	  /* 0,1,... */
	  /* Note: it seems the standard only requires that for a ERTERM flush */
	  /* and doesn't specify what to do for a regular BYPASS flush */
	  while(mqc->ct > 0)
	  {
		 mqc->ct--;
		 mqc->c += (uint32_t)(bit_value << mqc->ct);
		 bit_value = (uint8_t)(1U - bit_value);
	  }
	  *mqc->bp = (uint8_t)mqc->c;
	  /* Advance pointer so that mqc_numbytes() returns a valid value */
	  mqc->bp++;
   }
   else if(mqc->ct == 7 && mqc->bp[-1] == 0xff)
   {
	  /* Discard last 0xff */
	  assert(!erterm);
	  mqc->bp--;
   }
   else if(mqc->ct == 8 && !erterm && mqc->bp[-1] == 0x7f && mqc->bp[-2] == 0xff)
   {
	  /* Tiny optimization: discard terminating 0xff 0x7f since it is */
	  /* interpreted as 0xff 0x7f [0xff 0xff] by the decompressor, and given */
	  /* the bit stuffing, in fact as 0xff 0xff [0xff ..] */
	  mqc->bp -= 2;
   }

   assert(mqc->bp[-1] != 0xff);
}

void mqc_restart_init_enc(mqcoder* mqc)
{
   /* <Re-init part> */
   /* As specified in Figure C.10 - Initialization of the compressor */
   /* (C.2.8 Initialization of the compressor (INITENC)) */
   mqc->a = 0x8000;
   mqc->c = 0;
   mqc->ct = 12;
   /* This function is normally called after at least one mqc_flush() */
   /* which will have advance mqc->bp by at least 2 bytes beyond its */
   /* initial position */
   mqc->bp--;
   assert(mqc->bp >= mqc->start - 1);
   assert(*mqc->bp != 0xff);
   if(*mqc->bp == 0xff)
	  mqc->ct = 13;
}

void mqc_erterm_enc(mqcoder* mqc)
{
   int32_t k = (int32_t)(11 - mqc->ct + 1);

   while(k > 0)
   {
	  mqc->c <<= mqc->ct;
	  mqc->ct = 0;
	  mqc_byteout(mqc);
	  k -= (int32_t)mqc->ct;
   }
   if(*mqc->bp != 0xff)
	  mqc_byteout(mqc);
}

void mqc_segmark_enc(mqcoder* mqc)
{
   mqc_setcurctx(mqc, 18);
   for(uint32_t i = 1; i < 5; i++)
	  mqc_encode(mqc, i % 2);
}

} // namespace grk
