/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
 *    This source code incorporates work covered by the following copyright and
 *    permission notice:
 *
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

/* For internal use of decode_macro() */
#define mpsexchange_dec_macro(d, curctx, a) \
{ \
    if (a < (*curctx)->qeval) { \
        d = (*curctx)->mps ^ 1; \
        *curctx = (*curctx)->nlps; \
    } else { \
        d = (*curctx)->mps; \
        *curctx = (*curctx)->nmps; \
    } \
}

/* For internal use of decode_macro() */
#define lpsexchange_dec_macro(d, curctx, a) \
{ \
    if (a < (*curctx)->qeval) { \
        a = (*curctx)->qeval; \
        d = (*curctx)->mps; \
        *curctx = (*curctx)->nmps; \
    } else { \
        a = (*curctx)->qeval; \
        d = (*curctx)->mps ^ 1; \
        *curctx = (*curctx)->nlps; \
    } \
}

/**
Decode a symbol using raw-decoder. Cfr p.506 TAUBMAN
@param mqc MQC handle
@return Returns the decoded symbol (0 or 1)
*/
static INLINE uint32_t mqc_raw_decode(mqcoder *mqc){
    if (mqc->ct == 0) {
        /* Given mqc_raw_init_dec() we know that at some point we will */
        /* have a 0xFF 0xFF artificial marker */
        if (mqc->c == 0xff) {
            if (*mqc->bp  > 0x8f) {
                mqc->c = 0xff;
                mqc->ct = 8;
            } else {
                mqc->c = *mqc->bp;
                mqc->bp ++;
                mqc->ct = 7;
            }
        } else {
            mqc->c = *mqc->bp;
            mqc->bp ++;
            mqc->ct = 8;
        }
    }
    mqc->ct--;

    return ((uint32_t)mqc->c >> mqc->ct) & 0x01U;
}

#define bytein_dec_macro(mqc, c, ct) \
{ \
	/* Given mqc_init_dec() we know that at some point we will */ \
	/* have a 0xFF 0xFF artificial marker */ \
	uint32_t l_c = *(mqc->bp + 1); \
	if (*mqc->bp == 0xff) { \
		if (l_c > 0x8f) { \
			c += 0xff00; \
			ct = 8; \
			mqc->end_of_byte_stream_counter ++; \
		} else { \
			mqc->bp++; \
			c += l_c << 9; \
			ct = 7; \
		} \
	} else { \
		mqc->bp++; \
		c += l_c << 8; \
		ct = 8; \
	} \
}

/* For internal use of decode_macro() */
#define renorm_dec_macro(mqc, a, c, ct) \
{ \
    do { \
        if (ct == 0) \
            bytein_dec_macro(mqc, c, ct); \
        a <<= 1; \
        c <<= 1; \
        ct--; \
    } while (a < 0x8000); \
}

#define decode_macro(d, mqc, curctx, a, c, ct) \
{ \
    /* Implements ISO 15444-1 C.3.2 Decoding a decision (DECODE) */ \
    /* Note: alternate "J.2 - Decoding an MPS or an LPS in the */ \
    /* software-conventions decoder" has been tried, but does not bring any */ \
    /* improvement. See https://github.com/uclouvain/openjpeg/issues/921 */ \
    a -= (*curctx)->qeval;  \
    if ((c >> 16) < (*curctx)->qeval) {  \
        lpsexchange_dec_macro(d, curctx, a);  \
        renorm_dec_macro(mqc, a, c, ct);  \
    } else {  \
        c -= (*curctx)->qeval << 16;  \
        if ((a & 0x8000) == 0) { \
            mpsexchange_dec_macro(d, curctx, a); \
            renorm_dec_macro(mqc, a, c, ct); \
        } else { \
            d = (*curctx)->mps; \
        } \
    } \
}

/**
Input a byte
@param mqc MQC handle
*/
static INLINE void mqc_bytein_dec(mqcoder *const mqc){
    bytein_dec_macro(mqc, mqc->c, mqc->ct);
}

/**
Renormalize mqc->a and mqc->c while decoding
@param mqc MQC handle
*/
#define mqc_renormd(mqc) \
    renorm_dec_macro(mqc, mqc->a, mqc->c, mqc->ct)

/**
Decode a symbol
@param d uint32_t value where to store the decoded symbol
@param mqc MQC handle
@return Returns the decoded symbol (0 or 1) in d
*/
#define mqc_decode(d, mqc) \
    decode_macro(d, mqc, mqc->curctx, mqc->a, mqc->c, mqc->ct)
