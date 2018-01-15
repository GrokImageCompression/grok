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
 * Copyright (c) 2007, Callum Lerwick <seg@haxxed.com>
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
#include "mqc.h"
#include "t1_decode_base.h"
#include "t1_decode_opt.h"
#include "T1Encoder.h"

namespace grk {

t1_decode_opt::t1_decode_opt(uint16_t code_block_width, uint16_t code_block_height) : t1_decode_base(code_block_width,code_block_height){
	if (!allocateBuffers(code_block_width, code_block_height))
		throw std::exception();
}
t1_decode_opt::~t1_decode_opt() {
}


/*
Allocate buffers
@param cblkw	maximum width of code block
@param cblkh	maximum height of code block

*/
bool t1_decode_opt::allocateBuffers(uint16_t cblkw, uint16_t cblkh)
{
	if (!t1::allocateBuffers(cblkw, cblkh))
		return false;
	if (!dataPtr) {
		dataPtr = (int32_t*)grok_aligned_malloc(cblkw*cblkh * sizeof(int32_t));
		if (!dataPtr) {
			/* FIXME event manager error callback */
			return false;
		}
	}
	return true;
}

/*
Initialize buffers
@param w	width of code block
@param h	height of code block
*/
void t1_decode_opt::initBuffers(uint16_t cblkw, uint16_t cblkh) {
	t1::initBuffers(cblkw, cblkh);
	if (dataPtr)
		memset(dataPtr, 0, cblkw*cblkh * sizeof(int32_t));

}

inline void t1_decode_opt::sigpass_step(flag_opt_t *flagsp,
										int32_t *datap,
										uint8_t orient,
										int32_t oneplushalf,
										uint32_t maxci3,
										uint32_t cblksty) {
	for (uint32_t ci3 = 0U; ci3 < maxci3; ci3 += 3) {
		flag_opt_t const shift_flags = *flagsp >> ci3;
		if ((shift_flags & (T1_SIGMA_CURRENT | T1_PI_CURRENT)) == 0U && (shift_flags & T1_SIGMA_NEIGHBOURS) != 0U) {
			mqc_setcurctx(mqc, getZeroCodingContext(shift_flags, orient));
			if (mqc_decode(mqc)) {
				uint32_t lu = getSignCodingOrSPPByteIndex(*flagsp, flagsp[-1], flagsp[1], ci3);
				mqc_setcurctx(mqc, getSignCodingContext(lu));
				uint8_t v = mqc_decode(mqc) ^ getSPByte(lu);
				*datap = v ? -oneplushalf : oneplushalf;
				updateFlags(flagsp, ci3, v, flags_stride, (ci3 == 0) && (cblksty & J2K_CCP_CBLKSTY_VSC));
			}
			/* set propagation pass bit for this location */
			*flagsp |= T1_PI_CURRENT << ci3;
		}
		datap += w;
	}
}

void t1_decode_opt::sigpass(int32_t bpno,
	uint8_t orient,
	uint32_t cblksty) {
	int32_t one, half, oneplushalf;
	uint32_t i, k;
	one = 1 << bpno;
	half = one >> 1;
	oneplushalf = one | half;
	uint32_t const flag_row_extra = flags_stride - w;
	uint32_t const data_row_extra = (w << 2) - w;

	flag_opt_t* f = FLAGS_ADDRESS(0, 0);
	int32_t* d = dataPtr;
	for (k = 0; k < (h&~3U); k += 4) {
		for (i = 0; i < w; ++i) {
			if (*f) {
				sigpass_step(f, d, orient, oneplushalf, 12, cblksty);
			}
			++f;
			++d;
		}
		d += data_row_extra;
		f += flag_row_extra;
	}
	if (k < h) {
		for (i = 0; i < w; ++i) {
			if (*f) {
				sigpass_step(f, d, orient, oneplushalf, (h - k) * 3, cblksty);
			}
			++f;
			++d;
		}
	}
}
inline void t1_decode_opt::refpass_step(flag_opt_t *flagsp,
										int32_t *datap,
										int32_t poshalf, 
										uint32_t maxci3) {

	for (uint32_t ci3 = 0U; ci3 < maxci3; ci3 += 3) {
		uint32_t shift_flags = *flagsp >> ci3;
		/* if location is significant, but has not been coded in significance propagation pass, then code in this pass: */
		if ((shift_flags & (T1_SIGMA_CURRENT | T1_PI_CURRENT)) == T1_SIGMA_CURRENT) {
			mqc_setcurctx(mqc, getMRPContext(shift_flags));
			uint8_t v = mqc_decode(mqc);
			*datap += (v ^ (*datap < 0)) ? poshalf : -poshalf;

			/* flip magnitude refinement bit*/
			*flagsp |= T1_MU_CURRENT << ci3;
		}
		datap += w;
	}
}
void t1_decode_opt::refpass(int32_t bpno) {
	int32_t one = 1 << bpno;
	int32_t poshalf = one >> 1;
	flag_opt_t* f = FLAGS_ADDRESS(0, 0);
	uint32_t const flag_row_extra = flags_stride - w;
	uint32_t const data_row_extra = (w << 2) - w;
	int32_t* d = dataPtr;
	uint32_t i, k;
	for (k = 0U; k < (h&~3); k += 4U) {
		for (i = 0U; i < w; ++i) {
			if (*f) {
				refpass_step(f,
					d,
					poshalf,
					12);
			}
			++f;
			++d;
		}
		f += flag_row_extra;
		d += data_row_extra;
	}
	if (k < h) {
		for (uint32_t i = 0; i < w; ++i) {
			refpass_step(f,
				d,
				poshalf,
				(h - k) * 3);
			++f;
			++d;
		}
	}

}

void t1_decode_opt::clnpass_step(flag_opt_t *flagsp,
								int32_t *datap,
								uint8_t orient,
								int32_t oneplushalf,
								uint32_t agg,
								uint32_t runlen,
								uint32_t y,
								uint32_t cblksty) {

	const uint32_t check = (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13 | T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);
	if ((*flagsp & check) == check) {
		if (runlen == 0) {
			*flagsp &= ~(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);
		}
		else if (runlen == 1) {
			*flagsp &= ~(T1_PI_1 | T1_PI_2 | T1_PI_3);
		}
		else if (runlen == 2) {
			*flagsp &= ~(T1_PI_2 | T1_PI_3);
		}
		else if (runlen == 3) {
			*flagsp &= ~(T1_PI_3);
		}
		return;
	}
	runlen *= 3;
	uint32_t lim = 4U < (h - y) ? 12U : 3 * (h - y);
	flag_opt_t shift_flags;
	for (uint32_t ci3 = runlen; ci3 < lim; ci3 += 3) {
		uint8_t signCoding = 0;
		if ((agg != 0) && (ci3 == runlen)) {
			signCoding = 1;
		} else {
			shift_flags = *flagsp >> ci3;
			if (!(shift_flags & (T1_SIGMA_CURRENT | T1_PI_CURRENT))) {
				mqc_setcurctx(mqc, getZeroCodingContext(shift_flags, orient));
				signCoding = mqc_decode(mqc);
			}
		}
		if (signCoding) {
			uint32_t lu = getSignCodingOrSPPByteIndex(*flagsp, flagsp[-1], flagsp[1], ci3);
			mqc_setcurctx(mqc, getSignCodingContext(lu));
			uint8_t v = mqc_decode(mqc) ^ getSPByte(lu);
			*datap = v ? -oneplushalf : oneplushalf;
			updateFlags(flagsp, ci3, v, flags_stride, (cblksty & J2K_CCP_CBLKSTY_VSC) && (ci3 == 0));
		}
		*flagsp &= ~(T1_PI_0 << ci3);
		datap += w;
	}
}
void t1_decode_opt::clnpass(int32_t bpno,
	uint8_t orient,
	uint32_t cblksty) {
	int32_t one, half, oneplushalf;
	one = 1 << bpno;
	half = one >> 1;
	oneplushalf = one | half;
	uint32_t k;
	uint32_t agg, runlen;
	for (k = 0; k < (h&~3); k += 4) {
		for (uint32_t i = 0; i < w; ++i) {
			agg = !flags[i + 1 + ((k >> 2) + 1) * flags_stride];
			if (agg) {
				mqc_setcurctx(mqc, T1_CTXNO_AGG);
				if (!mqc_decode(mqc)) {
					continue;
				}
				mqc_setcurctx(mqc, T1_CTXNO_UNI);
				runlen = mqc_decode(mqc);
				runlen = (uint8_t)(runlen << 1) | mqc_decode(mqc);
			}
			else {
				runlen = 0;
			}
			clnpass_step(FLAGS_ADDRESS(i, k),
				dataPtr + ((k + runlen) * w) + i,
				orient,
				oneplushalf,
				agg,
				runlen,
				k,
				cblksty);
		}
	}

	if (k < h) {
		for (uint32_t i = 0; i < w; ++i) {
			clnpass_step(FLAGS_ADDRESS(i, k),
				dataPtr + (k * w) + i,
				orient,
				oneplushalf,
				0,
				0,
				k,
				cblksty);
		}
	}

	if (cblksty & J2K_CCP_CBLKSTY_SEGSYM) {
		uint8_t v = 0;
		mqc_setcurctx(mqc, T1_CTXNO_UNI);
		v = mqc_decode(mqc);
		v = (uint8_t)(v << 1) | mqc_decode(mqc);
		v = (uint8_t)(v << 1) | mqc_decode(mqc);
		v = (uint8_t)(v << 1) | mqc_decode(mqc);
		/*
		if (v!=0xa) {
			event_msg(cinfo, EVT_WARNING, "Bad segmentation symbol %x\n", v);
		}
		*/
	}
}
bool t1_decode_opt::decode_cblk(tcd_cblk_dec_t* cblk,
	uint8_t orient,
	uint32_t roishift,
	uint32_t cblksty)
{
	initBuffers((uint16_t)(cblk->x1 - cblk->x0), (uint16_t)(cblk->y1 - cblk->y0));
	if (!cblk->seg_buffers.get_len())
		return true;
	if (!allocCompressed(cblk))
		return false;
	int32_t bpno_plus_one = (int32_t)(roishift + cblk->numbps);
	uint32_t passtype = 2;
	mqc_resetstates(mqc);
	for (uint32_t segno = 0; segno < cblk->numSegments; ++segno) {
		tcd_seg_t *seg = &cblk->segs[segno];
		uint32_t synthOffset = seg->dataindex + seg->len;
		uint16_t stash = *((uint16_t*)(compressed_block + synthOffset));
		*((uint16_t*)(compressed_block + synthOffset)) = synthBytes;
		uint8_t type = ((bpno_plus_one <= ((int32_t)(cblk->numbps)) - 4) &&
			(passtype < 2) && (cblksty & J2K_CCP_CBLKSTY_LAZY)) ? T1_TYPE_RAW : T1_TYPE_MQ;
		if (type == T1_TYPE_RAW) {
			raw_init_dec(raw, compressed_block + seg->dataindex, seg->len);
		}
		else {
			mqc_init_dec(mqc, compressed_block + seg->dataindex, seg->len);
		}
		for (uint32_t passno = 0; (passno < seg->numpasses) && (bpno_plus_one >= 1); ++passno) {
			switch (passtype) {
			case 0:
				if (type == T1_TYPE_RAW) {
					//sigpass_raw(bpno_plus_one, cblksty);
				}
				else {
					if (cblksty & J2K_CCP_CBLKSTY_VSC) {
						//sigpass_vsc(bpno_plus_one, orient);
					}
					else {
						sigpass(bpno_plus_one, orient,cblksty);
					}
				}
				break;
			case 1:
				if (type == T1_TYPE_RAW) {
					//refpass_raw(bpno_plus_one, cblksty);
				}
				else {
					if (cblksty & J2K_CCP_CBLKSTY_VSC) {
						//refpass_vsc(bpno_plus_one);
					}
					else {
						refpass(bpno_plus_one);
					}
				}
				break;
			case 2:
				clnpass(bpno_plus_one, orient, cblksty);
				break;
			}

			if ((cblksty & J2K_CCP_CBLKSTY_RESET) && type == T1_TYPE_MQ) {
				mqc_resetstates(mqc);
			}


			if (++passtype == 3) {
				passtype = 0;
				bpno_plus_one--;
			}
		}
		*((uint16_t*)(compressed_block + synthOffset)) = stash;
	}
	return true;
}


void t1_decode_opt::postDecode(decodeBlockInfo* block) {
	auto t1_data = dataPtr;
	// ROI shift
	if (block->roishift) {
		int32_t threshold = 1 << block->roishift;
		for (auto j = 0U; j < h; ++j) {
			for (auto i = 0U; i < w; ++i) {
				auto value = *t1_data;
				auto magnitude = abs(value);
				if (magnitude >= threshold) {
					magnitude >>= block->roishift;
					// ((value > 0) - (value < 0)) == signum(value)
					*t1_data = ((value > 0) - (value < 0))* magnitude;
				}
				t1_data++;
			}
		}
		//reset t1_data to start of buffer
		t1_data = dataPtr;
	}

	//dequantization
	uint32_t tile_width = block->tilec->x1 - block->tilec->x0;
	if (block->qmfbid == 1) {
		int32_t* restrict tile_data = block->tiledp;
		for (auto j = 0U; j < h; ++j) {
			int32_t* restrict tile_row_data = tile_data;
			for (auto i = 0U; i < w; ++i) {
				tile_row_data[i] = *t1_data / 2;
				t1_data++;
			}
			tile_data += tile_width;
		}
	}
	else {
		float* restrict tile_data = (float*)block->tiledp;
		for (auto j = 0U; j < h; ++j) {
			float* restrict tile_row_data = tile_data;
			for (auto i = 0U; i < w; ++i) {
				tile_row_data[i] = (float)*t1_data * block->stepsize;
				t1_data++;
			}
			tile_data += tile_width;
		}
	}
}


}

