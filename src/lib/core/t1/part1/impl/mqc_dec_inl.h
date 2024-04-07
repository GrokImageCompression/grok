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

#pragma once

/* For internal use of decompress_macro() */
#define mpsexchange_dec_macro(d, curctx, a) \
   {                                        \
	  if(a < (*curctx)->qeval)              \
	  {                                     \
		 d = (*curctx)->mps ^ 1;            \
		 *curctx = (*curctx)->nlps;         \
	  }                                     \
	  else                                  \
	  {                                     \
		 d = (*curctx)->mps;                \
		 *curctx = (*curctx)->nmps;         \
	  }                                     \
   }

/* For internal use of decompress_macro() */
#define lpsexchange_dec_macro(d, curctx, a) \
   {                                        \
	  if(a < (*curctx)->qeval)              \
	  {                                     \
		 a = (*curctx)->qeval;              \
		 d = (*curctx)->mps;                \
		 *curctx = (*curctx)->nmps;         \
	  }                                     \
	  else                                  \
	  {                                     \
		 a = (*curctx)->qeval;              \
		 d = (*curctx)->mps ^ 1;            \
		 *curctx = (*curctx)->nlps;         \
	  }                                     \
   }

/**
Decompress a symbol using raw-decoder. Cfr p.506 TAUBMAN
@param mqc MQC handle
@return Returns the decoded symbol (0 or 1)
*/
static INLINE uint32_t mqc_raw_decode(mqcoder* mqc)
{
   if(mqc->ct == 0)
   {
	  /* Given mqc_raw_init_dec() we know that at some point we will */
	  /* have a 0xFF 0xFF artificial marker */
	  if(mqc->c == 0xff)
	  {
		 if(*mqc->bp > 0x8f)
		 {
			mqc->c = 0xff;
			mqc->ct = 8;
		 }
		 else
		 {
			mqc->c = *mqc->bp;
			mqc->bp++;
			mqc->ct = 7;
		 }
	  }
	  else
	  {
		 mqc->c = *mqc->bp;
		 mqc->bp++;
		 mqc->ct = 8;
	  }
   }
   mqc->ct--;

   return ((uint32_t)mqc->c >> mqc->ct) & 0x01U;
}

#define bytein_dec_macro(mqc, c, ct)                                \
   {                                                                \
	  /* Given mqc_init_dec() we know that at some point we will */ \
	  /* have a 0xFF 0xFF artificial marker */                      \
	  uint32_t l_c = *(mqc->bp + 1);                                \
	  if(*mqc->bp == 0xff)                                          \
	  {                                                             \
		 if(l_c > 0x8f)                                             \
		 {                                                          \
			c += 0xff00;                                            \
			ct = 8;                                                 \
			mqc->end_of_byte_stream_counter++;                      \
		 }                                                          \
		 else                                                       \
		 {                                                          \
			mqc->bp++;                                              \
			c += l_c << 9;                                          \
			ct = 7;                                                 \
		 }                                                          \
	  }                                                             \
	  else                                                          \
	  {                                                             \
		 mqc->bp++;                                                 \
		 c += l_c << 8;                                             \
		 ct = 8;                                                    \
	  }                                                             \
   }

/* For internal use of decompress_macro() */
#define renorm_dec_macro(mqc, a, c, ct)   \
   {                                      \
	  do                                  \
	  {                                   \
		 if(ct == 0)                      \
			bytein_dec_macro(mqc, c, ct); \
		 a <<= 1;                         \
		 c <<= 1;                         \
		 ct--;                            \
	  } while(a < A_MIN);                 \
   }

#define decompress_macro(d, mqc, curctx, a, c, ct)                         \
   {                                                                       \
	  /* Implements ISO 15444-1 C.3.2 Decompressing a decision (DECODE) */ \
	  a -= (*curctx)->qeval;                                               \
	  uint32_t qeval_shift = (*curctx)->qeval << 16;                       \
	  if(c < qeval_shift)                                                  \
	  {                                                                    \
		 lpsexchange_dec_macro(d, curctx, a);                              \
		 renorm_dec_macro(mqc, a, c, ct);                                  \
	  }                                                                    \
	  else                                                                 \
	  {                                                                    \
		 c -= qeval_shift;                                                 \
		 if(a < A_MIN)                                                     \
		 {                                                                 \
			mpsexchange_dec_macro(d, curctx, a);                           \
			renorm_dec_macro(mqc, a, c, ct);                               \
		 }                                                                 \
		 else                                                              \
		 {                                                                 \
			d = (*curctx)->mps;                                            \
		 }                                                                 \
	  }                                                                    \
   }

/**
Input a byte
@param mqc MQC handle
*/
static INLINE void mqc_bytein(mqcoder* const mqc)
{
   bytein_dec_macro(mqc, mqc->c, mqc->ct);
}

/**
Renormalize mqc->a and mqc->c while decoding
@param mqc MQC handle
*/
#define mqc_renormd(mqc) renorm_dec_macro(mqc, mqc->a, mqc->c, mqc->ct)

/**
Decompress a symbol
@param d uint32_t value where to store the decoded symbol
@param mqc MQC handle
@return Returns the decoded symbol (0 or 1) in d
*/
#define mqc_decode(d, mqc) decompress_macro(d, mqc, mqc->curctx, mqc->a, mqc->c, mqc->ct)
