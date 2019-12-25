/*
 *    Copyright (C) 2016-2019 Grok Image Compression Inc.
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

const uint16_t A_MIN = 0x8000;

/**
 ERTERM mode switch (PTERM)
 @param mqc MQC handle
 */
static void mqc_flush_erterm(grk_mqc *mqc);


/**
 BYPASS mode switch, flush operation
 <h2>Not fully implemented and tested !!</h2>
 @param mqc MQC handle
 */
static void mqc_bypass_flush_enc(grk_mqc *mqc);

/**
 Flush the encoder, so that all remaining data is written
 @param mqc MQC handle
 */
static void mqc_flush(grk_mqc *mqc);

/**
 Output a byte, doing bit-stuffing if necessary.
 After a 0xff byte, the next byte must be smaller than 0x90.
 @param mqc MQC handle
 */
static void mqc_byteout(grk_mqc *mqc);
/**
 Renormalize mqc->A and mqc->C while encoding, so that mqc->A stays between 0x8000 and 0x10000
 @param mqc MQC handle
 */
//static void mqc_renorme(mqc_t *mqc);
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
static void mqc_setbits(grk_mqc *mqc);
/**
 Set the state of a particular context
 @param mqc MQC handle
 @param ctxno Number that identifies the context
 @param prob Number that identifies the probability of the symbols for the new state of the context
 */
static void mqc_setstate(grk_mqc *mqc, uint8_t ctxno, uint8_t prob);

/**
 FIXME DOC
 @param mqc MQC handle
 @return
 */
static inline uint8_t mqc_mpsexchange(grk_mqc *const mqc);
/**
 FIXME DOC
 @param mqc MQC handle
 @return
 */
static inline uint8_t mqc_lpsexchange(grk_mqc *const mqc);
/**
 Input a byte
 @param mqc MQC handle
 */
static inline void mqc_bytein(grk_mqc *const mqc);
/**
 Renormalize mqc->A and mqc->C while decoding
 @param mqc MQC handle
 */
static inline void mqc_renormd(grk_mqc *const mqc);
/*@}*/

/*@}*/


/* <summary> */
/* This array defines all the possible states for a context. */
/* </summary> */
const grk_mqc_state mqc_states[] = {
		{ 0x5601,  2,3 },
		{ 0x5601 |  HIGH_BIT, 3, 2 },
		{ 0x3401,  4, 12 },
		{ 0x3401 | HIGH_BIT, 5, 13 },
		{ 0x1801,  6, 18 },
		{ 0x1801 | HIGH_BIT, 7, 19 },
		{ 0x0ac1,  8, 24 },
		{ 0x0ac1 | HIGH_BIT, 9, 25 },
		{ 0x0521,  10, 58 },
		{ 0x0521 | HIGH_BIT, 11, 59 },
		{ 0x0221,  76, 66 },
		{ 0x0221 | HIGH_BIT, 77, 67 },
		{ 0x5601,  14, 13 },
		{ 0x5601 | HIGH_BIT, 15, 12 },
		{ 0x5401,  16, 28 },
		{ 0x5401 | HIGH_BIT, 17, 29 },
		{ 0x4801,  18, 28 },
		{ 0x4801 | HIGH_BIT, 19, 29 },
		{ 0x3801,  20, 28 },
		{ 0x3801 | HIGH_BIT, 21, 29 },
		{ 0x3001,  22, 34 },
		{ 0x3001 | HIGH_BIT, 23, 35 },
		{ 0x2401,  24, 36 },
		{ 0x2401 | HIGH_BIT, 25, 37 },
		{ 0x1c01,  26, 40 },
		{ 0x1c01 | HIGH_BIT, 27, 41 },
		{ 0x1601,  58, 42 },
		{ 0x1601 | HIGH_BIT, 59, 43 },
		{ 0x5601,  30, 29 },
		{ 0x5601 | HIGH_BIT, 31, 28 },
		{ 0x5401,  32, 28 },
		{ 0x5401 | HIGH_BIT, 33, 29 },
		{ 0x5101,  34, 30 },
		{ 0x5101 | HIGH_BIT, 35, 31 },
		{ 0x4801,  36, 32 },
		{ 0x4801 | HIGH_BIT, 37, 33 },
		{ 0x3801,  38, 34 },
		{ 0x3801 | HIGH_BIT, 39, 35 },
		{ 0x3401,  40, 36 },
		{ 0x3401 | HIGH_BIT, 41, 37 },
		{ 0x3001,  42, 38 },
		{ 0x3001 | HIGH_BIT, 43, 39 },
		{ 0x2801,  44, 38 },
		{ 0x2801 | HIGH_BIT, 45, 39 },
		{ 0x2401,  46, 40 },
		{ 0x2401 | HIGH_BIT, 47, 41 },
		{ 0x2201,  48, 42 },
		{ 0x2201 | HIGH_BIT, 49, 43 },
		{ 0x1c01,  50, 44 },
		{ 0x1c01 | HIGH_BIT, 51, 45 },
		{ 0x1801,  52, 46 },
		{ 0x1801 | HIGH_BIT, 53, 47 },
		{ 0x1601,  54, 48 },
		{ 0x1601 | HIGH_BIT, 55, 49 },
		{ 0x1401,  56, 50 },
		{ 0x1401 | HIGH_BIT, 57, 51 },
		{ 0x1201,  58, 52 },
		{ 0x1201 | HIGH_BIT, 59, 53 },
		{ 0x1101,  60, 54 },
		{ 0x1101 | HIGH_BIT, 61, 55 },
		{ 0x0ac1,  62, 56 },
		{ 0x0ac1 | HIGH_BIT, 63, 57 },
		{ 0x09c1,  64, 58 },
		{ 0x09c1 | HIGH_BIT, 65, 59 },
		{ 0x08a1,  66, 60 },
		{ 0x08a1 | HIGH_BIT, 67, 61 },
		{ 0x0521,  68, 62 },
		{ 0x0521 | HIGH_BIT, 69, 63 },
		{ 0x0441,  70, 64 },
		{ 0x0441 | HIGH_BIT, 71, 65 },
		{ 0x02a1,  72, 66 },
		{ 0x02a1 | HIGH_BIT, 73, 67 },
		{ 0x0221,  74, 68 },
		{ 0x0221 | HIGH_BIT, 75, 69 },
		{ 0x0141,  76, 70 },
		{ 0x0141 | HIGH_BIT, 77, 71 },
		{ 0x0111,  78, 72 },
		{ 0x0111 | HIGH_BIT, 79, 73 },
		{ 0x0085,  80, 74 },
		{ 0x0085 | HIGH_BIT, 81, 75 },
		{ 0x0049,  82, 76 },
		{ 0x0049 | HIGH_BIT, 83, 77 },
		{ 0x0025,  84, 78 },
		{ 0x0025 | HIGH_BIT, 85, 79 },
		{ 0x0015,  86, 80 },
		{ 0x0015 | HIGH_BIT, 87, 81 },
		{ 0x0009,  88, 82 },
		{ 0x0009 | HIGH_BIT, 89, 83 },
		{ 0x0005,  90, 84 },
		{ 0x0005 | HIGH_BIT, 91, 85 },
		{ 0x0001,  90, 86 },
		{ 0x0001 | HIGH_BIT, 91, 87 },
		{ 0x5601,  92, 92 },
		{ 0x5601 | HIGH_BIT, 93, 93 }
};

/*
 ==========================================================
 local functions
 ==========================================================
 */

// easy MQ codeword termination
// See Taubman and Marcellin p 496 for details
static void mqc_flush_erterm(grk_mqc *mqc) {
	int32_t n = (int32_t) (27 - 15 - mqc->COUNT);
	mqc->C <<= mqc->COUNT;
	while (n > 0) {
		mqc_byteout(mqc);
		n -= (int32_t) mqc->COUNT;
		mqc->C <<= mqc->COUNT;
	}
	mqc_byteout(mqc);
	//increment bp so that mqc_numbytes() will now return correct result
	if (*mqc->bp != 0xff) {
		mqc->bp++;
	}
}

static void mqc_byteout(grk_mqc *mqc) {
	assert(mqc->bp >= mqc->start - 1);
	if (*mqc->bp == 0xff) {
		mqc->bp++;
		*mqc->bp = (uint8_t) (mqc->C >> 20);
		mqc->C &= 0xfffff;
		mqc->COUNT = 7;
	} else {
		if ((mqc->C & 0x8000000) == 0) {
			mqc->bp++;
			*mqc->bp = (uint8_t) (mqc->C >> 19);
			mqc->C &= 0x7ffff;
			mqc->COUNT = 8;
		} else {
			(*mqc->bp)++;
			if (*mqc->bp == 0xff) {
				mqc->C &= 0x7ffffff;
				mqc->bp++;
				*mqc->bp = (uint8_t) (mqc->C >> 20);
				mqc->C &= 0xfffff;
				mqc->COUNT = 7;
			} else {
				mqc->bp++;
				*mqc->bp = (uint8_t) (mqc->C >> 19);
				mqc->C &= 0x7ffff;
				mqc->COUNT = 8;
			}
		}
	}
}

static void mqc_setbits(grk_mqc *mqc) {
	uint32_t tempc = mqc->C + mqc->A;
	mqc->C |= 0xffff;
	if (mqc->C >= tempc) {
		mqc->C -= A_MIN;
	}
}
static inline uint8_t mqc_mpsexchange(grk_mqc *const mqc) {
	uint8_t d;
	const grk_mqc_state *curctx = mqc_states + mqc->ctxs[mqc->curctx];
	if (mqc->A < (curctx->qeval & PROB_MASK)) {
		d = (uint8_t) ((curctx->qeval>>MPS_SHIFT)^1);
		mqc->ctxs[mqc->curctx] = curctx->nlps;
	} else {
		d = (uint8_t)(curctx->qeval>>MPS_SHIFT);
		mqc->ctxs[mqc->curctx] = curctx->nmps;
	}
	return d;
}
static inline uint8_t mqc_lpsexchange(grk_mqc *const mqc) {
	uint8_t d;
	const grk_mqc_state *curctx = mqc_states + mqc->ctxs[mqc->curctx];
	auto qeval = (uint16_t)(curctx->qeval & PROB_MASK);
	if (mqc->A < qeval) {
		mqc->A = qeval;
		d = (uint8_t)(curctx->qeval>>MPS_SHIFT);
		mqc->ctxs[mqc->curctx] = curctx->nmps;
	} else {
		mqc->A = qeval;
		d = (uint8_t) ((curctx->qeval>>MPS_SHIFT)^1);
		mqc->ctxs[mqc->curctx] = curctx->nlps;
	}
	return d;
}
static void mqc_bytein(grk_mqc *const mqc) {
	uint8_t nextByte = mqc->bp[1];
	if (mqc->currentByteIs0xFF) {
		if (nextByte > 0x8F) {
			// found termination marker - synthesize 1's in C register and do not increment bp
			mqc->C += 0xFF;
			mqc->COUNT = 8;
		} else {
			// bit stuff next byte and add to C register
			mqc->bp++;
			mqc->C += (uint32_t)nextByte << 1;
			mqc->COUNT = 7;
		}
	} else {
		// add next byte to C register
		mqc->bp++;
		mqc->C += nextByte;
		mqc->COUNT = 8;
	}
	mqc->currentByteIs0xFF = (nextByte == 0xFF);
}
static inline void mqc_renormd(grk_mqc *const mqc) {
#ifdef __AVX2__
  	if (mqc->COUNT == 0)
		mqc_bytein(mqc);
  	// count leading zeros for 16-bit mqc->A
#ifdef _MSC_VER
 	uint8_t NS = (uint8_t)__lzcnt16( mqc->A );
#else
	uint8_t NS = (uint8_t)__builtin_clz((uint32_t)mqc->A << 16);
#endif
	mqc->A = (uint16_t)( mqc->A << NS);
	if (mqc->COUNT <= NS){
		mqc->C = mqc->C << mqc->COUNT;
		NS = (uint8_t)(NS - mqc->COUNT);
		mqc_bytein(mqc);
		if (mqc->COUNT <= NS){
			mqc->C = mqc->C << mqc->COUNT;
			NS = (uint8_t)(NS - mqc->COUNT);
			mqc_bytein(mqc);
		}
	}
	if (NS){
		mqc->C = mqc->C << NS;
		mqc->COUNT = (uint8_t)(mqc->COUNT - NS);
	}
#else
	do {
		if (mqc->COUNT == 0) {
			mqc_bytein(mqc);
		}
		mqc->A = (uint16_t) (mqc->A << 1);
		mqc->C = (mqc->C << 1);
		mqc->COUNT--;
	} while (mqc->A < A_MIN);
#endif
}

static void mqc_flush(grk_mqc *mqc) {
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
static void mqc_bypass_flush_enc(grk_mqc *mqc) {
	assert(mqc->bp >= mqc->start - 1);
	uint8_t bit_padding = 0;
	if (mqc->COUNT != 8) {
		while (mqc->COUNT > 0) {
			mqc->COUNT--;
			mqc->C += (uint32_t) (bit_padding << mqc->COUNT);
			bit_padding = (bit_padding + 1) & 0x01;
		}
		mqc->bp++;
		*mqc->bp = (uint8_t) mqc->C;
	}
	if (*mqc->bp != 0xff) {
		mqc->bp++;
	}
}

static void mqc_setstate(grk_mqc *mqc, uint8_t ctxno, uint8_t prob) {
	mqc->ctxs[ctxno] = (uint8_t)((prob << 1));
}

/*
 ==========================================================
 MQ-Coder interface
 ==========================================================
 */

grk_raw* raw_create(void) {
	grk_raw *raw = (grk_raw*) grok_malloc(sizeof(grk_raw));
	return raw;
}
void raw_destroy(grk_raw *raw) {
	if (raw) {
		grok_free(raw);
	}
}
void raw_init_dec(grk_raw *raw, uint8_t *bp, uint32_t len) {
	raw->start = bp;
	raw->lenmax = len;
	raw->len = 0;
	raw->C = 0;
	raw->COUNT = 0;
}
uint8_t raw_decode(grk_raw *raw) {
	if (raw->COUNT == 0) {
		raw->COUNT = 8;
		if (raw->len == raw->lenmax) {
			raw->C = 0xff;
		} else {
			if (raw->C == 0xff) {
				raw->COUNT = 7;
			}
			raw->C = raw->start[raw->len];
			raw->len++;
		}
	}
	raw->COUNT--;
	return ((uint32_t) raw->C >> raw->COUNT) & 0x01;
}
void mqc_setcurctx(grk_mqc *mqc, uint8_t ctxno) {
#ifdef PLUGIN_DEBUG_ENCODE
	if (mqc->debug_mqc.debug_state & GROK_PLUGIN_STATE_DEBUG) {
		mqc->debug_mqc.context_number = ctxno;
	}
#endif
	mqc->curctx = ctxno;
}
grk_mqc* mqc_create(void) {
	return (grk_mqc*) grok_calloc(1, sizeof(grk_mqc));
}
void mqc_destroy(grk_mqc *mqc) {
	if (mqc) {
		grok_free(mqc);
	}
}
// beware: always outputs ONE LESS than actual number of encoded bytes, until after flush is called.
// After flush, the result returned is correct.
int32_t mqc_numbytes(grk_mqc *mqc) {
	ptrdiff_t diff = mqc->bp - mqc->start;
	assert(diff >= -1);
	return (int32_t) diff;
}
void mqc_init_enc(grk_mqc *mqc, uint8_t *bp) {
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
void mqc_encode(grk_mqc *mqc, uint8_t d) {
#ifdef PLUGIN_DEBUG_ENCODE
	if ((mqc->debug_mqc.debug_state  & GROK_PLUGIN_STATE_DEBUG) &&
		!(mqc->debug_mqc.debug_state & GROK_PLUGIN_STATE_PRE_TR1)) {
		nextCXD(&mqc->debug_mqc, d);
	}
#endif
	const grk_mqc_state *curctx = mqc_states + mqc->ctxs[mqc->curctx];
	if ((curctx->qeval>>MPS_SHIFT)== d) {
		//BEGIN codemps
		auto qeval = (uint16_t)(curctx->qeval & PROB_MASK);
		mqc->A = (uint16_t) (mqc->A - qeval);
		if (mqc->A < A_MIN) {
			if (mqc->A < qeval) {
				mqc->A = qeval;
			} else {
				mqc->C += qeval;
			}
			mqc->ctxs[mqc->curctx] = curctx->nmps;
			//BEGIN renorme
			do {
				mqc->A = (uint16_t) (mqc->A << 1);
				mqc->C = (mqc->C << 1);
				mqc->COUNT--;
				if (mqc->COUNT == 0) {
					//BEGIN byteout
					assert(mqc->bp >= mqc->start - 1);
					if (*mqc->bp == 0xff) {
						mqc->bp++;
						*mqc->bp = (uint8_t) (mqc->C >> 20);
						mqc->C &= 0xfffff;
						mqc->COUNT = 7;
					} else {
						if ((mqc->C & 0x8000000) == 0) {
							mqc->bp++;
							*mqc->bp = (uint8_t) (mqc->C >> 19);
							mqc->C &= 0x7ffff;
							mqc->COUNT = 8;
						} else {
							(*mqc->bp)++;
							if (*mqc->bp == 0xff) {
								mqc->C &= 0x7ffffff;
								mqc->bp++;
								*mqc->bp = (uint8_t) (mqc->C >> 20);
								mqc->C &= 0xfffff;
								mqc->COUNT = 7;
							} else {
								mqc->bp++;
								*mqc->bp = (uint8_t) (mqc->C >> 19);
								mqc->C &= 0x7ffff;
								mqc->COUNT = 8;
							}
						}
					}
					//END byteout
				}
			} while (mqc->A < A_MIN);
			//END renorme
		} else {
			mqc->C += qeval;
		}
		//END codemps
	} else {
		//BEGIN codelps
		auto qeval = (uint16_t)(curctx->qeval & PROB_MASK);
		mqc->A = (uint16_t) (mqc->A - qeval);
		if (mqc->A < qeval) {
			mqc->C += qeval;
		} else {
			mqc->A = qeval;
		}
		mqc->ctxs[mqc->curctx] = curctx->nlps;
		//BEGIN renorme
		do {
			mqc->A = (uint16_t) (mqc->A << 1);
			mqc->C = (mqc->C << 1);
			mqc->COUNT--;
			if (mqc->COUNT == 0) {
				//BEGIN byteout
				assert(mqc->bp >= mqc->start - 1);
				if (*mqc->bp == 0xff) {
					mqc->bp++;
					*mqc->bp = (uint8_t) (mqc->C >> 20);
					mqc->C &= 0xfffff;
					mqc->COUNT = 7;
				} else {
					if ((mqc->C & 0x8000000) == 0) {
						mqc->bp++;
						*mqc->bp = (uint8_t) (mqc->C >> 19);
						mqc->C &= 0x7ffff;
						mqc->COUNT = 8;
					} else {
						(*mqc->bp)++;
						if (*mqc->bp == 0xff) {
							mqc->C &= 0x7ffffff;
							mqc->bp++;
							*mqc->bp = (uint8_t) (mqc->C >> 20);
							mqc->C &= 0xfffff;
							mqc->COUNT = 7;
						} else {
							mqc->bp++;
							*mqc->bp = (uint8_t) (mqc->C >> 19);
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

void mqc_big_flush(grk_mqc *mqc, uint32_t cblk_sty, bool bypassFlush) {
	if (bypassFlush) {
		mqc_bypass_flush_enc(mqc);
	}
	/* Code switch "ERTERM" (i.e. PTERM) */
	else if (cblk_sty & J2K_CCP_CBLKSTY_PTERM)
		mqc_flush_erterm(mqc);
	else
		mqc_flush(mqc);
}

void mqc_bypass_init_enc(grk_mqc *mqc) {
	mqc->C = 0;
	mqc->COUNT = 8;
	//note: mqc->bp is guaranteed to be greater than mqc->start, since we have already performed
	// at least one flush
	mqc->bp--;
	if (*mqc->bp == 0xff) {
		mqc->COUNT = 7;
	}
}
void mqc_bypass_enc(grk_mqc *mqc, uint8_t d) {
	mqc->COUNT--;
	mqc->C += (uint32_t) d << mqc->COUNT;
	if (mqc->COUNT == 0) {
		mqc->bp++;
		*mqc->bp = (uint8_t) mqc->C;
		mqc->COUNT = 8;
		// bit stuffing ensures that most significant bit equals zero
		// for byte following 0xFF
		if (*mqc->bp == 0xff) {
			mqc->COUNT = 7;
		}
		mqc->C = 0;
	}
}

void mqc_restart_init_enc(grk_mqc *mqc) {
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

void mqc_segmark_enc(grk_mqc *mqc) {
	mqc_setcurctx(mqc, 18);
	for (uint8_t i = 1; i < 5; i++) {
		mqc_encode(mqc, i & 1);
	}
}
void mqc_init_dec(grk_mqc *mqc, uint8_t *bp, uint32_t len) {
	mqc_setcurctx(mqc, 0);
	mqc->start = bp;
	mqc->bp = bp;
	uint8_t currentByte = (len > 0) ? *mqc->bp : 0xFF;
	mqc->currentByteIs0xFF = currentByte == 0xFF;
	mqc->C = (uint32_t) (currentByte << 8);
	mqc_bytein(mqc);
	mqc->C <<= 7;
	mqc->COUNT = (uint8_t) (mqc->COUNT - 7);
	mqc->A = A_MIN;
	mqc->MIN_A_C = 0;
	mqc->Q_SUM = 0;
}
uint8_t mqc_decode(grk_mqc *const mqc) {
	uint32_t Q_SUM = (uint16_t) (mqc->Q_SUM + ((mqc_states + mqc->ctxs[mqc->curctx])->qeval & PROB_MASK) );
	if (mqc->MIN_A_C >= (uint16_t) Q_SUM) {
		mqc->Q_SUM = (uint16_t) Q_SUM;
		return (uint8_t)((mqc_states + mqc->ctxs[mqc->curctx])->qeval >> MPS_SHIFT);
	}
	mqc->A = (uint16_t) (mqc->A - Q_SUM);
	Q_SUM <<= 8;
	uint8_t d = 0;
	if (mqc->C < Q_SUM) {
		mqc->C -= (mqc->Q_SUM << 8);
		d = mqc_lpsexchange(mqc);
	} else {
		mqc->C -= Q_SUM;
		if (mqc->A < A_MIN) {
			d = mqc_mpsexchange(mqc);
		}
	}
	mqc_renormd(mqc);
	mqc->MIN_A_C = std::min<uint16_t>((uint16_t) (mqc->A - A_MIN),
			(uint16_t) (mqc->C >> 8));
	mqc->Q_SUM = 0;
	return d;
}

void mqc_resetstates(grk_mqc *mqc) {
	uint32_t i;
	for (i = 0; i < MQC_NUMCTXS; i++) {
		mqc->ctxs[i] = 0;
	}
	mqc_setstate(mqc, T1_CTXNO_UNI, 46);
	mqc_setstate(mqc, T1_CTXNO_AGG, 3);
	mqc_setstate(mqc, T1_CTXNO_ZC, 4);
}



}

