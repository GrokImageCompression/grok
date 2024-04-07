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

/**
Output a byte, doing bit-stuffing if necessary.
After a 0xff byte, the next byte must be smaller than 0x90.
@param mqc MQC handle
*/
void mqc_byteout(mqcoder* mqc);

/**
Renormalize mqc->a and mqc->c while compressing, so that mqc->a stays between 0x8000 and 0x10000
@param mqc MQC handle
@param a_ value of mqc->a
@param c_ value of mqc->c_
@param ct_ value of mqc->ct_
*/
#define mqc_renorme_macro(mqc, a_, c_, ct_) \
   {                                        \
	  do                                    \
	  {                                     \
		 a_ <<= 1;                          \
		 c_ <<= 1;                          \
		 ct_--;                             \
		 if(ct_ == 0)                       \
		 {                                  \
			mqc->c = c_;                    \
			mqc_byteout(mqc);               \
			c_ = mqc->c;                    \
			ct_ = mqc->ct;                  \
		 }                                  \
	  } while((a_ & 0x8000) == 0);          \
   }

#define mqc_codemps_macro(mqc, curctx, a, c, ct) \
   {                                             \
	  a -= (*curctx)->qeval;                     \
	  if((a & 0x8000) == 0)                      \
	  {                                          \
		 if(a < (*curctx)->qeval)                \
			a = (*curctx)->qeval;                \
		 else                                    \
			c += (*curctx)->qeval;               \
		 *curctx = (*curctx)->nmps;              \
		 mqc_renorme_macro(mqc, a, c, ct);       \
	  }                                          \
	  else                                       \
	  {                                          \
		 c += (*curctx)->qeval;                  \
	  }                                          \
   }

#define mqc_codelps_macro(mqc, curctx, a, c, ct) \
   {                                             \
	  a -= (*curctx)->qeval;                     \
	  if(a < (*curctx)->qeval)                   \
		 c += (*curctx)->qeval;                  \
	  else                                       \
		 a = (*curctx)->qeval;                   \
	  *curctx = (*curctx)->nlps;                 \
	  mqc_renorme_macro(mqc, a, c, ct);          \
   }

#ifdef PLUGIN_DEBUG_ENCODE
#define mqc_encode_macro(mqc, curctx, a, c, ct, d)                                              \
   {                                                                                            \
	  (mqc)->debug_mqc.context_number = ctxno;                                                  \
	  nextCXD(&(mqc)->debug_mqc, d);                                                            \
	  if((*curctx)->mps == (d))                                                                 \
		 mqc_codemps_macro(mqc, curctx, a, c, ct) else mqc_codelps_macro(mqc, curctx, a, c, ct) \
   }
#else
#define mqc_encode_macro(mqc, curctx, a, c, ct, d)                                              \
   {                                                                                            \
	  if((*curctx)->mps == (d))                                                                 \
		 mqc_codemps_macro(mqc, curctx, a, c, ct) else mqc_codelps_macro(mqc, curctx, a, c, ct) \
   }
#endif
#define mqc_bypass_enc_macro(mqc, c, ct, d)                                    \
   {                                                                           \
	  if(ct == BYPASS_CT_INIT)                                                 \
		 ct = 8;                                                               \
	  ct--;                                                                    \
	  c = c + ((d) << ct);                                                     \
	  if(ct == 0)                                                              \
	  {                                                                        \
		 *mqc->bp = (uint8_t)c;                                                \
		 ct = 8;                                                               \
		 /* If the previous byte was 0xff, make sure that the next msb is 0 */ \
		 if(*mqc->bp == 0xff)                                                  \
			ct = 7;                                                            \
		 mqc->bp++;                                                            \
		 c = 0;                                                                \
	  }                                                                        \
   }
