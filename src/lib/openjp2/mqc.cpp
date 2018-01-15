/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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

#include "grok_includes.h"

// tier 1 interface
#include "t1_decode_base.h"
#include "t1_decode.h"
#include "mqc.h"


namespace grk {

/**
Output a byte, doing bit-stuffing if necessary.
After a 0xff byte, the next byte must be smaller than 0x90.
@param mqc MQC handle
*/
static void mqc_byteout(mqc_t *mqc);
/**
Renormalize mqc->A and mqc->C while encoding, so that mqc->A stays between 0x8000 and 0x10000
@param mqc MQC handle
*/
static void mqc_renorme(mqc_t *mqc);
/**
Encode the most probable symbol
@param mqc MQC handle
*/
//static void mqc_codemps(mqc_t *mqc);
/**
Encode the most least symbol
@param mqc MQC handle
*/
//static void mqc_codelps(mqc_t *mqc);
/**
Fill mqc->C with 1's for flushing
@param mqc MQC handle
*/
static void mqc_setbits(mqc_t *mqc);
/**
Set the state of a particular context
@param mqc MQC handle
@param ctxno Number that identifies the context
@param msb The MSB of the new state of the context
@param prob Number that identifies the probability of the symbols for the new state of the context
*/
static void mqc_setstate(mqc_t *mqc, uint32_t ctxno, uint32_t msb, int32_t prob);

/**
FIXME DOC
@param mqc MQC handle
@return
*/
static inline uint8_t mqc_mpsexchange(mqc_t *const mqc);
/**
FIXME DOC
@param mqc MQC handle
@return
*/
static inline uint8_t mqc_lpsexchange(mqc_t *const mqc);
/**
Input a byte
@param mqc MQC handle
*/
static inline void mqc_bytein(mqc_t *const mqc);
/**
Renormalize mqc->A and mqc->C while decoding
@param mqc MQC handle
*/
static inline void mqc_renormd(mqc_t *const mqc);
/*@}*/

/*@}*/

/* <summary> */
/* This array defines all the possible states for a context. */
/* </summary> */
mqc_state_t mqc_states[totalNumContextStates] = {
	{0x5601, 0, &mqc_states[2], &mqc_states[3]},
	{0x5601, 1, &mqc_states[3], &mqc_states[2]},
	{0x3401, 0, &mqc_states[4], &mqc_states[12]},
	{0x3401, 1, &mqc_states[5], &mqc_states[13]},
	{0x1801, 0, &mqc_states[6], &mqc_states[18]},
	{0x1801, 1, &mqc_states[7], &mqc_states[19]},
	{0x0ac1, 0, &mqc_states[8], &mqc_states[24]},
	{0x0ac1, 1, &mqc_states[9], &mqc_states[25]},
	{0x0521, 0, &mqc_states[10], &mqc_states[58]},
	{0x0521, 1, &mqc_states[11], &mqc_states[59]},
	{0x0221, 0, &mqc_states[76], &mqc_states[66]},
	{0x0221, 1, &mqc_states[77], &mqc_states[67]},
	{0x5601, 0, &mqc_states[14], &mqc_states[13]},
	{0x5601, 1, &mqc_states[15], &mqc_states[12]},
	{0x5401, 0, &mqc_states[16], &mqc_states[28]},
	{0x5401, 1, &mqc_states[17], &mqc_states[29]},
	{0x4801, 0, &mqc_states[18], &mqc_states[28]},
	{0x4801, 1, &mqc_states[19], &mqc_states[29]},
	{0x3801, 0, &mqc_states[20], &mqc_states[28]},
	{0x3801, 1, &mqc_states[21], &mqc_states[29]},
	{0x3001, 0, &mqc_states[22], &mqc_states[34]},
	{0x3001, 1, &mqc_states[23], &mqc_states[35]},
	{0x2401, 0, &mqc_states[24], &mqc_states[36]},
	{0x2401, 1, &mqc_states[25], &mqc_states[37]},
	{0x1c01, 0, &mqc_states[26], &mqc_states[40]},
	{0x1c01, 1, &mqc_states[27], &mqc_states[41]},
	{0x1601, 0, &mqc_states[58], &mqc_states[42]},
	{0x1601, 1, &mqc_states[59], &mqc_states[43]},
	{0x5601, 0, &mqc_states[30], &mqc_states[29]},
	{0x5601, 1, &mqc_states[31], &mqc_states[28]},
	{0x5401, 0, &mqc_states[32], &mqc_states[28]},
	{0x5401, 1, &mqc_states[33], &mqc_states[29]},
	{0x5101, 0, &mqc_states[34], &mqc_states[30]},
	{0x5101, 1, &mqc_states[35], &mqc_states[31]},
	{0x4801, 0, &mqc_states[36], &mqc_states[32]},
	{0x4801, 1, &mqc_states[37], &mqc_states[33]},
	{0x3801, 0, &mqc_states[38], &mqc_states[34]},
	{0x3801, 1, &mqc_states[39], &mqc_states[35]},
	{0x3401, 0, &mqc_states[40], &mqc_states[36]},
	{0x3401, 1, &mqc_states[41], &mqc_states[37]},
	{0x3001, 0, &mqc_states[42], &mqc_states[38]},
	{0x3001, 1, &mqc_states[43], &mqc_states[39]},
	{0x2801, 0, &mqc_states[44], &mqc_states[38]},
	{0x2801, 1, &mqc_states[45], &mqc_states[39]},
	{0x2401, 0, &mqc_states[46], &mqc_states[40]},
	{0x2401, 1, &mqc_states[47], &mqc_states[41]},
	{0x2201, 0, &mqc_states[48], &mqc_states[42]},
	{0x2201, 1, &mqc_states[49], &mqc_states[43]},
	{0x1c01, 0, &mqc_states[50], &mqc_states[44]},
	{0x1c01, 1, &mqc_states[51], &mqc_states[45]},
	{0x1801, 0, &mqc_states[52], &mqc_states[46]},
	{0x1801, 1, &mqc_states[53], &mqc_states[47]},
	{0x1601, 0, &mqc_states[54], &mqc_states[48]},
	{0x1601, 1, &mqc_states[55], &mqc_states[49]},
	{0x1401, 0, &mqc_states[56], &mqc_states[50]},
	{0x1401, 1, &mqc_states[57], &mqc_states[51]},
	{0x1201, 0, &mqc_states[58], &mqc_states[52]},
	{0x1201, 1, &mqc_states[59], &mqc_states[53]},
	{0x1101, 0, &mqc_states[60], &mqc_states[54]},
	{0x1101, 1, &mqc_states[61], &mqc_states[55]},
	{0x0ac1, 0, &mqc_states[62], &mqc_states[56]},
	{0x0ac1, 1, &mqc_states[63], &mqc_states[57]},
	{0x09c1, 0, &mqc_states[64], &mqc_states[58]},
	{0x09c1, 1, &mqc_states[65], &mqc_states[59]},
	{0x08a1, 0, &mqc_states[66], &mqc_states[60]},
	{0x08a1, 1, &mqc_states[67], &mqc_states[61]},
	{0x0521, 0, &mqc_states[68], &mqc_states[62]},
	{0x0521, 1, &mqc_states[69], &mqc_states[63]},
	{0x0441, 0, &mqc_states[70], &mqc_states[64]},
	{0x0441, 1, &mqc_states[71], &mqc_states[65]},
	{0x02a1, 0, &mqc_states[72], &mqc_states[66]},
	{0x02a1, 1, &mqc_states[73], &mqc_states[67]},
	{0x0221, 0, &mqc_states[74], &mqc_states[68]},
	{0x0221, 1, &mqc_states[75], &mqc_states[69]},
	{0x0141, 0, &mqc_states[76], &mqc_states[70]},
	{0x0141, 1, &mqc_states[77], &mqc_states[71]},
	{0x0111, 0, &mqc_states[78], &mqc_states[72]},
	{0x0111, 1, &mqc_states[79], &mqc_states[73]},
	{0x0085, 0, &mqc_states[80], &mqc_states[74]},
	{0x0085, 1, &mqc_states[81], &mqc_states[75]},
	{0x0049, 0, &mqc_states[82], &mqc_states[76]},
	{0x0049, 1, &mqc_states[83], &mqc_states[77]},
	{0x0025, 0, &mqc_states[84], &mqc_states[78]},
	{0x0025, 1, &mqc_states[85], &mqc_states[79]},
	{0x0015, 0, &mqc_states[86], &mqc_states[80]},
	{0x0015, 1, &mqc_states[87], &mqc_states[81]},
	{0x0009, 0, &mqc_states[88], &mqc_states[82]},
	{0x0009, 1, &mqc_states[89], &mqc_states[83]},
	{0x0005, 0, &mqc_states[90], &mqc_states[84]},
	{0x0005, 1, &mqc_states[91], &mqc_states[85]},
	{0x0001, 0, &mqc_states[90], &mqc_states[86]},
	{0x0001, 1, &mqc_states[91], &mqc_states[87]},
	{0x5601, 0, &mqc_states[92], &mqc_states[92]},
	{0x5601, 1, &mqc_states[93], &mqc_states[93]},
};

/*
==========================================================
	local functions
==========================================================
*/

static void mqc_byteout(mqc_t *mqc) {
	assert(mqc->bp >= mqc->start - 1);
	if (*mqc->bp == 0xff) {
		mqc->bp++;
		*mqc->bp = (uint8_t)(mqc->C >> 20);
		mqc->C &= 0xfffff;
		mqc->COUNT = 7;
	}
	else {
		if ((mqc->C & 0x8000000) == 0) {
			mqc->bp++;
			*mqc->bp = (uint8_t)(mqc->C >> 19);
			mqc->C &= 0x7ffff;
			mqc->COUNT = 8;
		}
		else {
			(*mqc->bp)++;
			if (*mqc->bp == 0xff) {
				mqc->C &= 0x7ffffff;
				mqc->bp++;
				*mqc->bp = (uint8_t)(mqc->C >> 20);
				mqc->C &= 0xfffff;
				mqc->COUNT = 7;
			}
			else {
				mqc->bp++;
				*mqc->bp = (uint8_t)(mqc->C >> 19);
				mqc->C &= 0x7ffff;
				mqc->COUNT = 8;
			}
		}
	}
}
static void mqc_renorme(mqc_t *mqc) {
	do {
		mqc->A = (uint16_t)(mqc->A << 1);
		mqc->C = (mqc->C << 1);
		mqc->COUNT--;
		if (mqc->COUNT == 0) {
			mqc_byteout(mqc);
		}
	} while (mqc->A < A_MIN);
}
/*
static void mqc_codemps(mqc_t *mqc) {
	auto curctx = *mqc->curctx;
	auto qeval = curctx->qeval;
	mqc->A = (uint16_t)(mqc->A - qeval);
	if (mqc->A < A_MIN) {
		if (mqc->A < qeval) {
			mqc->A = qeval;
		}
		else {
			mqc->C += qeval;
		}
		*mqc->curctx = curctx->nmps;
		mqc_renorme(mqc);
	}
	else {
		mqc->C += qeval;
	}
}
static void mqc_codelps(mqc_t *mqc) {
	auto curctx = *mqc->curctx;
	auto qeval = curctx->qeval;
	mqc->A = (uint16_t)(mqc->A - qeval);
	if (mqc->A < qeval) {
		mqc->C += qeval;
	}
	else {
		mqc->A = qeval;
	}
	*mqc->curctx = curctx->nlps;
	mqc_renorme(mqc);
}
*/
static void mqc_setbits(mqc_t *mqc) {
	uint32_t tempc = mqc->C + mqc->A;
	mqc->C |= 0xffff;
	if (mqc->C >= tempc) {
		mqc->C -= A_MIN;
	}
}
static inline uint8_t mqc_mpsexchange(mqc_t *const mqc)
{
	uint8_t d;
	auto curctx = *mqc->curctx;
	if (mqc->A < curctx->qeval) {
		d = (uint8_t)(1 - curctx->mps);
		*mqc->curctx = curctx->nlps;
	}
	else {
		d = curctx->mps;
		*mqc->curctx = curctx->nmps;
	}
	return d;
}
static inline uint8_t mqc_lpsexchange(mqc_t *const mqc) {
	uint8_t d;
	auto curctx = *mqc->curctx;
	auto qeval = curctx->qeval;
	if (mqc->A < qeval) {
		mqc->A = qeval;
		d = curctx->mps;
		*mqc->curctx = curctx->nmps;
	}
	else {
		mqc->A = qeval;
		d = (uint8_t)(1 - curctx->mps);
		*mqc->curctx = curctx->nlps;
	}
	return d;
}
static void mqc_bytein(mqc_t *const mqc) {
	uint8_t nextByte = (mqc->bp + 1 < mqc->end) ? *(mqc->bp + 1) : 0xFF;
	if (mqc->currentByteIs0xFF) {
		if (nextByte > 0x8F) {
			// found termination marker - synthesize 1's in C register and do not increment bp
			mqc->C += 0xFF;
			mqc->COUNT = 8;
		}
		else {
			// bit stuff next byte and add to C register
			mqc->bp++;
			mqc->C += nextByte << 1;
			mqc->COUNT = 7;
		}
	}
	else {
		// add next byte to C register
		mqc->bp++;
		mqc->C += nextByte;
		mqc->COUNT = 8;
	}
	mqc->currentByteIs0xFF = nextByte == 0xFF;
}
static inline void mqc_renormd(mqc_t *const mqc) {
	do {
		if (mqc->COUNT == 0) {
			mqc_bytein(mqc);
		}
		mqc->A = (uint16_t)(mqc->A << 1);
		mqc->C = (mqc->C << 1);
		mqc->COUNT--;
	} while (mqc->A < A_MIN);
}

/*
==========================================================
	MQ-Coder interface
==========================================================
*/

raw_t* raw_create(void) {
	raw_t *raw = (raw_t*)grok_malloc(sizeof(raw_t));
	return raw;
}
void raw_destroy(raw_t *raw) {
	if (raw) {
		grok_free(raw);
	}
}
void raw_init_dec(raw_t *raw, uint8_t *bp, uint32_t len) {
	raw->start = bp;
	raw->lenmax = len;
	raw->len = 0;
	raw->C = 0;
	raw->COUNT = 0;
}
uint8_t raw_decode(raw_t *raw) {
	if (raw->COUNT == 0) {
		raw->COUNT = 8;
		if (raw->len == raw->lenmax) {
			raw->C = 0xff;
		}
		else {
			if (raw->C == 0xff) {
				raw->COUNT = 7;
			}
			raw->C = raw->start[raw->len];
			raw->len++;
		}
	}
	raw->COUNT--;
	return  ((uint32_t)raw->C >> raw->COUNT) & 0x01;
}
void mqc_setcurctx(mqc_t *mqc, uint8_t ctxno) {
#ifdef PLUGIN_DEBUG_ENCODE
	if (mqc->debug_mqc.debug_state & GROK_PLUGIN_STATE_DEBUG) {
		mqc->debug_mqc.context_number = ctxno;
	}
#endif
	mqc->curctx = &mqc->ctxs[(uint32_t)ctxno];
}
mqc_t* mqc_create(void) {
	return (mqc_t*)grok_calloc(1, sizeof(mqc_t));
}
void mqc_destroy(mqc_t *mqc) {
	if (mqc) {
		grok_free(mqc);
	}
}
// beware: always outputs ONE LESS than actual number of encoded bytes, until after flush is called.
// After flush, the result returned is correct.
int32_t mqc_numbytes(mqc_t *mqc) {
	ptrdiff_t diff = mqc->bp - mqc->start;
	return (int32_t)diff;
}
void mqc_init_enc(mqc_t *mqc, uint8_t *bp) {
	mqc_resetstates(mqc);
	mqc_setcurctx(mqc, 0);
	mqc->A = A_MIN;
	mqc->C = 0;
	mqc->bp = bp - 1;
	*mqc->bp = 0;
	mqc->COUNT = 12;
	mqc->start = bp;
#ifdef PLUGIN_DEBUG_ENCODE
	if (grok_plugin_get_debug_state() & GROK_PLUGIN_STATE_DEBUG) {
		mqc->debug_mqc.contextStream = nullptr;
		mqc->debug_mqc.contextCacheCount = 0;
		mqc->debug_mqc.contextStreamByteCount = 0;
		mqc->debug_mqc.debug_state = grok_plugin_get_debug_state();
	}
#endif
}
void mqc_encode(mqc_t *mqc, uint8_t d) {
#ifdef PLUGIN_DEBUG_ENCODE
	if ((mqc->debug_mqc.debug_state  & GROK_PLUGIN_STATE_DEBUG) &&
		!(mqc->debug_mqc.debug_state & GROK_PLUGIN_STATE_PRE_TR1)) {
		nextCXD(&mqc->debug_mqc, d);
	}
#endif
	auto curctx = *mqc->curctx;
	if (curctx->mps == d) {
		//BEGIN codemps
		auto qeval = curctx->qeval;
		mqc->A = (uint16_t)(mqc->A - qeval);
		if (mqc->A < A_MIN) {
			if (mqc->A < qeval) {
				mqc->A = qeval;
			}
			else {
				mqc->C += qeval;
			}
			*mqc->curctx = curctx->nmps;
			//BEGIN renorme
			do {
				mqc->A = (uint16_t)(mqc->A << 1);
				mqc->C = (mqc->C << 1);
				mqc->COUNT--;
				if (mqc->COUNT == 0) {
					//BEGIN byteout
					assert(mqc->bp >= mqc->start - 1);
					if (*mqc->bp == 0xff) {
						mqc->bp++;
						*mqc->bp = (uint8_t)(mqc->C >> 20);
						mqc->C &= 0xfffff;
						mqc->COUNT = 7;
					}
					else {
						if ((mqc->C & 0x8000000) == 0) {
							mqc->bp++;
							*mqc->bp = (uint8_t)(mqc->C >> 19);
							mqc->C &= 0x7ffff;
							mqc->COUNT = 8;
						}
						else {
							(*mqc->bp)++;
							if (*mqc->bp == 0xff) {
								mqc->C &= 0x7ffffff;
								mqc->bp++;
								*mqc->bp = (uint8_t)(mqc->C >> 20);
								mqc->C &= 0xfffff;
								mqc->COUNT = 7;
							}
							else {
								mqc->bp++;
								*mqc->bp = (uint8_t)(mqc->C >> 19);
								mqc->C &= 0x7ffff;
								mqc->COUNT = 8;
							}
						}
					}
					//END byteout
				}
			} while (mqc->A < A_MIN);
			//END renorme
		}
		else {
			mqc->C += qeval;
		}
		//END codemps
	}
	else {
		//BEGIN codelps
		auto qeval = curctx->qeval;
		mqc->A = (uint16_t)(mqc->A - qeval);
		if (mqc->A < qeval) {
			mqc->C += qeval;
		}
		else {
			mqc->A = qeval;
		}
		*mqc->curctx = curctx->nlps;
		//BEGIN renorme
		do {
			mqc->A = (uint16_t)(mqc->A << 1);
			mqc->C = (mqc->C << 1);
			mqc->COUNT--;
			if (mqc->COUNT == 0) {
				//BEGIN byteout
				assert(mqc->bp >= mqc->start - 1);
				if (*mqc->bp == 0xff) {
					mqc->bp++;
					*mqc->bp = (uint8_t)(mqc->C >> 20);
					mqc->C &= 0xfffff;
					mqc->COUNT = 7;
				}
				else {
					if ((mqc->C & 0x8000000) == 0) {
						mqc->bp++;
						*mqc->bp = (uint8_t)(mqc->C >> 19);
						mqc->C &= 0x7ffff;
						mqc->COUNT = 8;
					}
					else {
						(*mqc->bp)++;
						if (*mqc->bp == 0xff) {
							mqc->C &= 0x7ffffff;
							mqc->bp++;
							*mqc->bp = (uint8_t)(mqc->C >> 20);
							mqc->C &= 0xfffff;
							mqc->COUNT = 7;
						}
						else {
							mqc->bp++;
							*mqc->bp = (uint8_t)(mqc->C >> 19);
							mqc->C &= 0x7ffff;
							mqc->COUNT = 8;
						}
					}
				}
				//END byteout
			}
		} while (mqc->A < A_MIN);
		//END renorme
		//END codelps
	}
}
void mqc_flush(mqc_t *mqc) {
	mqc_setbits(mqc);
	mqc->C <<= mqc->COUNT;
	mqc_byteout(mqc);
	mqc->C <<= mqc->COUNT;
	mqc_byteout(mqc);
	//increment bp so that mqc_numbytes() will now return correct result
	if (*mqc->bp != 0xff) {
		mqc->bp++;
	}
}
void mqc_big_flush(mqc_t *mqc, uint32_t cblksty, bool bypassFlush) {
	if (bypassFlush) {
		mqc_bypass_flush_enc(mqc);
	}
	/* Code switch "ERTERM" (i.e. PTERM) */
	else if (cblksty & J2K_CCP_CBLKSTY_PTERM)
		mqc_flush_erterm(mqc);
	else
		mqc_flush(mqc);
}

void mqc_bypass_init_enc(mqc_t *mqc) {
	mqc->C = 0;
	mqc->COUNT = 8;
	//note: mqc->bp is guaranteed to be greater than mqc->start, since we have already performed
	// at least one flush
	mqc->bp--;
	if (*mqc->bp == 0xff) {
		mqc->COUNT = 7;
	}
}
void mqc_bypass_enc(mqc_t *mqc, uint8_t d)
{
	mqc->COUNT--;
	mqc->C += (uint32_t)d << mqc->COUNT;
	if (mqc->COUNT == 0) {
		mqc->bp++;
		*mqc->bp = (uint8_t)mqc->C;
		mqc->COUNT = 8;
		// bit stuffing ensures that most significant bit equals zero
		// for byte following 0xFF
		if (*mqc->bp == 0xff) {
			mqc->COUNT = 7;
		}
		mqc->C = 0;
	}
}
void mqc_bypass_flush_enc(mqc_t *mqc) {
	assert(mqc->bp >= mqc->start - 1);
	uint8_t bit_padding = 0;
	if (mqc->COUNT != 8) {
		while (mqc->COUNT > 0) {
			mqc->COUNT--;
			mqc->C += (uint32_t)(bit_padding << mqc->COUNT);
			bit_padding = (bit_padding + 1) & 0x01;
		}
		mqc->bp++;
		*mqc->bp = (uint8_t)mqc->C;
	}
	if (*mqc->bp != 0xff) {
		mqc->bp++;
	}
}
void mqc_restart_init_enc(mqc_t *mqc) {
	mqc_setcurctx(mqc, 0);
	mqc->A = A_MIN;
	mqc->C = 0;
	mqc->COUNT = 12;
	if (mqc->bp >= mqc->start) {
		mqc->bp--;
		if (*mqc->bp == 0xff) {
			mqc->COUNT = 13;
		}
	}
}
// easy MQ codeword termination
// See Taubman and Marcellin p 496 for details
void mqc_flush_erterm(mqc_t *mqc) {
	int32_t n = (int32_t)(27 - 15 - mqc->COUNT);
	mqc->C <<= mqc->COUNT;
	while (n > 0) {
		mqc_byteout(mqc);
		n -= (int32_t)mqc->COUNT;
		mqc->C <<= mqc->COUNT;
	}
	mqc_byteout(mqc);
	//increment bp so that mqc_numbytes() will now return correct result
	if (*mqc->bp != 0xff) {
		mqc->bp++;
	}
}
void mqc_segmark_enc(mqc_t *mqc) {
	mqc_setcurctx(mqc, 18);
	for (uint8_t i = 1; i < 5; i++) {
		mqc_encode(mqc, i & 1);
	}
}
void mqc_init_dec(mqc_t *mqc, uint8_t *bp, uint32_t len) {
	mqc_setcurctx(mqc, 0);
	mqc->start = bp;
	mqc->end = bp + len;
	mqc->bp = bp;
	uint8_t currentByte = (len > 0) ? *mqc->bp : 0xFF;
	mqc->currentByteIs0xFF = currentByte == 0xFF;
	mqc->C = (uint32_t)(currentByte << 8);
	mqc_bytein(mqc);
	mqc->C <<= 7;
	mqc->COUNT = (uint8_t)(mqc->COUNT - 7);
	mqc->A = A_MIN;
	mqc->MIN_A_C = 0;
	mqc->Q_SUM = 0;
}
uint8_t mqc_decode(mqc_t *const mqc) {
	uint32_t Q_SUM = (uint16_t)(mqc->Q_SUM + (*mqc->curctx)->qeval);
	if (mqc->MIN_A_C >= (uint16_t)Q_SUM) {
		mqc->Q_SUM = (uint16_t)Q_SUM;
		return (*mqc->curctx)->mps;
	}
	mqc->A = (uint16_t)(mqc->A - Q_SUM);
	Q_SUM <<= 8;
	uint8_t d = 0;
	if (mqc->C < Q_SUM) {
		mqc->C -= (mqc->Q_SUM << 8);
		d = mqc_lpsexchange(mqc);
	}
	else {
		mqc->C -= Q_SUM;
		if (mqc->A < A_MIN) {
			d = mqc_mpsexchange(mqc);
		}
	}
	mqc_renormd(mqc);
	mqc->MIN_A_C = std::min<uint16_t>((uint16_t)(mqc->A - A_MIN), (uint16_t)(mqc->C >> 8));
	mqc->Q_SUM = 0;
	return d;
}

void mqc_resetstates(mqc_t *mqc) {
	uint32_t i;
	for (i = 0; i < MQC_NUMCTXS; i++) {
		mqc->ctxs[i] = mqc_states;
	}
	mqc_setstate(mqc, T1_CTXNO_UNI, 0, 46);
	mqc_setstate(mqc, T1_CTXNO_AGG, 0, 3);
	mqc_setstate(mqc, T1_CTXNO_ZC, 0, 4);
}

void mqc_setstate(mqc_t *mqc, uint32_t ctxno, uint32_t msb, int32_t prob) {
	mqc->ctxs[ctxno] = &mqc_states[msb + (uint32_t)(prob << 1)];
}

}

