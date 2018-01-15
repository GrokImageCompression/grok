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
#include "mqc.h"
#include "t1.h"
#include "t1_encode.h"
#include "t1_luts.h"


namespace grk {

t1_encode::t1_encode() : data(nullptr),
	mqc(nullptr) {
	mqc = mqc_create();
	if (!mqc) {
		throw std::exception();
	}
}
t1_encode::~t1_encode() {
	mqc_destroy(mqc);
	if (data) {
		grok_aligned_free(data);
	}
}

/*
Allocate buffers
@param cblkw	maximum width of code block
@param cblkh	maximum height of code block

*/
bool t1_encode::allocateBuffers(uint16_t cblkw, uint16_t cblkh)
{
	if (!t1::allocateBuffers(cblkw, cblkh))
		return false;
	if (!data) {
		data = (uint32_t*)grok_aligned_malloc(cblkw*cblkh * sizeof(int32_t));
		if (!data) {
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
void t1_encode::initBuffers(uint16_t w, uint16_t h) {
	t1::initBuffers(w, h);
	if (data)
		memset(data, 0, w*h * sizeof(int32_t));
}
void  t1_encode::sigpass_step(flag_opt_t *flagsp,
	uint32_t *datap,
	uint8_t orient,
	int32_t bpno,
	int32_t one,
	int32_t *nmsedec,
	uint8_t type,
	uint32_t cblksty) {
	uint8_t v;
	if (*flagsp == 0U) {
		return;  /* Nothing to do for any of the 4 data points */
	}
	for (uint32_t ci3 = 0U; ci3 < 12U; ci3 += 3) {
		flag_opt_t const shift_flags = *flagsp >> ci3;
		/* if location is not significant, has not been coded in significance pass, and is in preferred neighbourhood,
		then code in this pass: */
		if ((shift_flags & (T1_SIGMA_CURRENT | T1_PI_CURRENT)) == 0U && (shift_flags & T1_SIGMA_NEIGHBOURS) != 0U) {
			auto dataPoint = *datap;
			v = (dataPoint >> one) & 1;
			mqc_setcurctx(mqc, getZeroCodingContext(shift_flags, orient));
			if (type == T1_TYPE_RAW) {
				mqc_bypass_enc(mqc, v);
			}
			else {
				mqc_encode(mqc, v);
			}
			if (v) {
				/* sign bit */
				v = (uint8_t)(dataPoint >> T1_DATA_SIGN_BIT_INDEX);
				if (nmsedec)
					*nmsedec += getnmsedec_sig(dataPoint, (uint32_t)bpno);
				uint32_t lu = getSignCodingOrSPPByteIndex(*flagsp, flagsp[-1], flagsp[1], ci3);
				mqc_setcurctx(mqc, getSignCodingContext(lu));
				if (type == T1_TYPE_RAW) {
					mqc_bypass_enc(mqc, v);
				}
				else {
					mqc_encode(mqc, v ^ getSPByte(lu));
				}
				updateFlags(flagsp, ci3, v, flags_stride, (ci3 == 0) && (cblksty & J2K_CCP_CBLKSTY_VSC));
			}
			/* set propagation pass bit for this location */
			*flagsp |= T1_PI_CURRENT << ci3;
		}
		datap += w;
	}
}
void t1_encode::sigpass(int32_t bpno,
	uint8_t orient,
	int32_t *nmsedec,
	uint8_t type,
	uint32_t cblksty) {
	uint32_t i, k;
	int32_t const one = (bpno + T1_NMSEDEC_FRACBITS);
	uint32_t const flag_row_extra = flags_stride - w;
	uint32_t const data_row_extra = (w << 2) - w;
	flag_opt_t* f = FLAGS_ADDRESS(0, 0);
	uint32_t* d = data;

	if (nmsedec)
		*nmsedec = 0;
	for (k = 0; k < h; k += 4) {
		for (i = 0; i < w; ++i) {
			sigpass_step(f,
				d,
				orient,
				bpno,
				one,
				nmsedec,
				type,
				cblksty);

			++f;
			++d;
		}
		d += data_row_extra;
		f += flag_row_extra;
	}
}
void t1_encode::refpass_step(flag_opt_t *flagsp,
	uint32_t *datap,
	int32_t bpno,
	int32_t one,
	int32_t *nmsedec,
	uint8_t type) {
	uint8_t v;
	if ((*flagsp & (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13)) == 0) {
		/* none significant */
		return;
	}
	if ((*flagsp & (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3)) == (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3)) {
		/* all processed by sigpass */
		return;
	}
	for (uint32_t ci3 = 0U; ci3 < 12U; ci3 += 3) {
		uint32_t shift_flags = *flagsp >> ci3;
		/* if location is significant, but has not been coded in significance propagation pass, then code in this pass: */
		if ((shift_flags & (T1_SIGMA_CURRENT | T1_PI_CURRENT)) == T1_SIGMA_CURRENT) {
			if (nmsedec)
				*nmsedec += getnmsedec_ref(*datap, (uint32_t)bpno);
			v = (*datap >> one) & 1;
			mqc_setcurctx(mqc, getMRPContext(shift_flags));
			if (type == T1_TYPE_RAW) {
				mqc_bypass_enc(mqc, v);
			}
			else {
				mqc_encode(mqc, v);
			}
			/* flip magnitude refinement bit*/
			*flagsp |= T1_MU_CURRENT << ci3;
		}
		datap += w;
	}
}
void t1_encode::refpass(int32_t bpno,
	int32_t *nmsedec,
	uint8_t type) {
	uint32_t i, k;
	const int32_t one = (bpno + T1_NMSEDEC_FRACBITS);
	flag_opt_t* f = FLAGS_ADDRESS(0, 0);
	uint32_t const flag_row_extra = flags_stride - w;
	uint32_t const data_row_extra = (w << 2) - w;
	uint32_t* d = data;

	if (nmsedec)
		*nmsedec = 0;
	for (k = 0U; k < h; k += 4U) {
		for (i = 0U; i < w; ++i) {
			refpass_step(f,
				d,
				bpno,
				one,
				nmsedec,
				type);
			++f;
			++d;
		}
		f += flag_row_extra;
		d += data_row_extra;
	}
}
void t1_encode::clnpass_step(flag_opt_t *flagsp,
	uint32_t *datap,
	uint8_t orient,
	int32_t bpno,
	int32_t one,
	int32_t *nmsedec,
	uint32_t agg,
	uint32_t runlen,
	uint32_t y,
	uint32_t cblksty)
{
	uint8_t v;
	uint32_t lim;
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
	lim = 4U < (h - y) ? 12U : 3 * (h - y);
	for (uint32_t ci3 = runlen; ci3 < lim; ci3 += 3) {
		flag_opt_t shift_flags;
		if ((agg != 0) && (ci3 == runlen)) {
			goto LABEL_PARTIAL;
		}

		shift_flags = *flagsp >> ci3;

		if (!(shift_flags & (T1_SIGMA_CURRENT | T1_PI_CURRENT))) {
			mqc_setcurctx(mqc, getZeroCodingContext(shift_flags, orient));
			v = (*datap >> one) & 1;
			mqc_encode(mqc, v);
			if (v) {
			LABEL_PARTIAL:
				if (nmsedec)
					*nmsedec += getnmsedec_sig(*datap, (uint32_t)bpno);
				uint32_t lu = getSignCodingOrSPPByteIndex(*flagsp, flagsp[-1], flagsp[1], ci3);
				mqc_setcurctx(mqc, getSignCodingContext(lu));
				/* sign bit */
				v = (uint8_t)(*datap >> T1_DATA_SIGN_BIT_INDEX);
				mqc_encode(mqc, v ^ getSPByte(lu));
				updateFlags(flagsp, ci3, v, flags_stride, (cblksty & J2K_CCP_CBLKSTY_VSC) && (ci3 == 0));
			}
		}
		*flagsp &= ~(T1_PI_0 << ci3);
		datap += w;
	}
}
void t1_encode::clnpass(int32_t bpno,
	uint8_t orient,
	int32_t *nmsedec,
	uint32_t cblksty) {
	uint32_t i, k;
	const int32_t one = (bpno + T1_NMSEDEC_FRACBITS);
	uint32_t agg;
	uint8_t runlen;

	if (nmsedec)
		*nmsedec = 0;

	for (k = 0; k < h; k += 4) {
		for (i = 0; i < w; ++i) {
			agg = !flags[i + 1 + ((k >> 2) + 1) * flags_stride];
			if (agg) {
				for (runlen = 0; runlen < 4; ++runlen) {
					if ((data[((k + runlen)*w) + i] >> one) & 1)
						break;
				}
				mqc_setcurctx(mqc, T1_CTXNO_AGG);
				mqc_encode(mqc, runlen != 4);
				if (runlen == 4) {
					continue;
				}
				mqc_setcurctx(mqc, T1_CTXNO_UNI);
				mqc_encode(mqc, (uint8_t)(runlen >> 1));
				mqc_encode(mqc, runlen & 1);
			}
			else {
				runlen = 0;
			}
			clnpass_step(FLAGS_ADDRESS(i, k),
				data + ((k + runlen) * w) + i,
				orient,
				bpno,
				one,
				nmsedec,
				agg,
				runlen,
				k,
				cblksty);
		}
	}
}
double t1_encode::getwmsedec(int32_t nmsedec,
	uint32_t compno,
	uint32_t level,
	uint8_t orient,
	int32_t bpno,
	uint32_t qmfbid,
	double stepsize,
	uint32_t numcomps,
	const double * mct_norms,
	uint32_t mct_numcomps) {
	double w1 = 1, w2, wmsedec;
	ARG_NOT_USED(numcomps);

	if (mct_norms && (compno < mct_numcomps)) {
		w1 = mct_norms[compno];
	}
	if (qmfbid == 1) {
		w2 = dwt_getnorm(level, orient);
	}
	else {	/* if (qmfbid == 0) */
		w2 = dwt_getnorm_real(level, orient);
	}
	wmsedec = w1 * w2 * stepsize * (double)((size_t)1 << bpno);
	wmsedec *= wmsedec * nmsedec / 8192.0;
	return wmsedec;
}

int16_t t1_encode::getnmsedec_sig(uint32_t x, uint32_t bitpos) {
	if (bitpos > 0) {
		return lut_nmsedec_sig[(x >> (bitpos)) & ((1 << T1_NMSEDEC_BITS) - 1)];
	}
	return lut_nmsedec_sig0[x & ((1 << T1_NMSEDEC_BITS) - 1)];
}

int16_t t1_encode::getnmsedec_ref(uint32_t x, uint32_t bitpos) {
	if (bitpos > 0) {
		return lut_nmsedec_ref[(x >> (bitpos)) & ((1 << T1_NMSEDEC_BITS) - 1)];
	}
	return lut_nmsedec_ref0[x & ((1 << T1_NMSEDEC_BITS) - 1)];
}

double t1_encode::encode_cblk(tcd_cblk_enc_t* cblk,
	uint8_t orient,
	uint32_t compno,
	uint32_t level,
	uint32_t qmfbid,
	double stepsize,
	uint32_t cblksty,
	uint32_t numcomps,
	const double * mct_norms,
	uint32_t mct_numcomps,
	uint32_t max,
	bool doRateControl)
{
	double cumwmsedec = 0.0;

	uint32_t passno;
	int32_t bpno;
	uint32_t passtype;
	int32_t nmsedec = 0;
	int32_t* msePtr = doRateControl ? &nmsedec : nullptr;
	double tempwmsedec = 0;

	auto logMax = int_floorlog2((int32_t)max) + 1;
	cblk->numbps = (max && (logMax > T1_NMSEDEC_FRACBITS)) ? (uint32_t)(logMax - T1_NMSEDEC_FRACBITS) : 0;
	if (!cblk->numbps)
		return 0;

	bpno = (int32_t)(cblk->numbps - 1);
	passtype = 2;
	mqc_init_enc(mqc, cblk->data);
#ifdef PLUGIN_DEBUG_ENCODE
	uint32_t state = grok_plugin_get_debug_state();
	if (state & GROK_PLUGIN_STATE_DEBUG) {
		mqc->debug_mqc.contextStream = cblk->contextStream;
		mqc->debug_mqc.orient = orient;
		mqc->debug_mqc.compno = compno;
		mqc->debug_mqc.level =  level;
	}
#endif

	bool TERMALL = (cblksty & J2K_CCP_CBLKSTY_TERMALL) ? true : false;
	bool LAZY = (cblksty & J2K_CCP_CBLKSTY_LAZY);

	for (passno = 0; bpno >= 0; ++passno) {
		tcd_pass_t *pass = &cblk->passes[passno];
		uint8_t type = T1_TYPE_MQ;
		if (LAZY && (bpno < ((int32_t)(cblk->numbps) - 4)) && (passtype < 2))
			type = T1_TYPE_RAW;

		switch (passtype) {
		case 0:
			sigpass(bpno, orient, msePtr, type, cblksty);
			break;
		case 1:
			refpass(bpno, msePtr, type);
			break;
		case 2:
			clnpass(bpno, orient, msePtr, cblksty);
			/* code switch SEGMARK (i.e. SEGSYM) */
			if (cblksty & J2K_CCP_CBLKSTY_SEGSYM)
				mqc_segmark_enc(mqc);
#ifdef PLUGIN_DEBUG_ENCODE
			if (state & GROK_PLUGIN_STATE_DEBUG) {
				mqc_next_plane(&mqc->debug_mqc);
			}
#endif
			break;
		}

		if (doRateControl) {
			tempwmsedec = getwmsedec(nmsedec,
				compno,
				level,
				orient,
				bpno,
				qmfbid,
				stepsize,
				numcomps,
				mct_norms,
				mct_numcomps);
			cumwmsedec += tempwmsedec;
		}

		// correction term is used for non-terminated passes, to ensure that maximal bits are
		// extracted from the partial segment when code block is truncated at this pass
		// See page 498 of Taubman and Marcellin for more details
		// note: we add 1 because rates for non-terminated passes are based on mqc_numbytes(mqc),
		// which is always 1 less than actual rate
		uint32_t correction = 4 + 1;

		// ** Terminate certain passes **
		// In LAZY mode, we need to terminate pass 2 from fourth bit plane, 
		// and passes 1 and 2 from subsequent bit planes. Pass 0 in lazy region
		// does not get terminated unless TERMALL is also set
		if (TERMALL ||
			(LAZY && ((bpno < ((int32_t)cblk->numbps - 4) && (passtype > 0)) ||
			((bpno == ((int32_t)cblk->numbps - 4)) && (passtype == 2))))) {

			correction = 0;
			auto bypassFlush = false;
			if (LAZY) {
				if (TERMALL) {
					bypassFlush = (bpno < ((int32_t)(cblk->numbps) - 4)) && (passtype < 2);
				}
				else {
					bypassFlush = passtype == 1;
				}
			}
			mqc_big_flush(mqc, cblksty, bypassFlush);
			pass->term = 1;
		}
		else {
			// SPP in raw region requires only a correction of one, 
			// since there are never more than 7 bits in C register
			if (LAZY && (bpno < ((int32_t)cblk->numbps - 4))) {
				correction = (mqc->COUNT < 8 ? 1 : 0) + 1;
			}
			else if (mqc->COUNT < 5)
				correction++;
			pass->term = 0;
		}

		if (++passtype == 3) {
			passtype = 0;
			bpno--;
		}

		pass->distortiondec = cumwmsedec;
		pass->rate = (uint16_t)(mqc_numbytes(mqc) + correction);

		//note: passtype and bpno have already been updated to next pass,
		// while pass pointer still points to current pass
		if (bpno >= 0) {
			if (pass->term) {
				type = T1_TYPE_MQ;
				if (LAZY && (bpno < ((int32_t)(cblk->numbps) - 4)) && (passtype < 2))
					type = T1_TYPE_RAW;
				if (type == T1_TYPE_RAW)
					mqc_bypass_init_enc(mqc);
				else
					mqc_restart_init_enc(mqc);
			}

			/* Code-switch "RESET" */
			if (cblksty & J2K_CCP_CBLKSTY_RESET)
				mqc_resetstates(mqc);
		}
	}

	tcd_pass_t *finalPass = &cblk->passes[passno - 1];
	if (!finalPass->term) {
		mqc_big_flush(mqc, cblksty, false);
	}

	cblk->num_passes_encoded = passno;
	for (passno = 0; passno < cblk->num_passes_encoded; passno++) {
		auto pass = cblk->passes + passno;
		if (!pass->term) {

			// maximum bytes in block
			uint32_t maxBytes = mqc_numbytes(mqc);

			if (LAZY) {
				// find next term pass
				for (uint32_t k = passno + 1; k < cblk->num_passes_encoded; ++k) {
					tcd_pass_t* nextTerm = cblk->passes + k;
					if (nextTerm->term) {
						// this rate is correct, since it has been flushed
						auto nextRate = nextTerm->rate;
						if (nextTerm->rate > 0 && (cblk->data[nextTerm->rate - 1] == 0xFF)) {
							nextRate--;
						}
						maxBytes = std::min<uint32_t>(maxBytes, nextRate);
						break;
					}
				}
			}
			if (pass->rate > (uint16_t)maxBytes)
				pass->rate = (uint16_t)maxBytes;
			// prevent generation of FF as last data byte of a pass 
			if (cblk->data[pass->rate - 1] == 0xFF) {
				pass->rate--;
			}
		}
#ifndef NDEBUG
		int32_t diff = (int32_t)(pass->rate - (passno == 0 ? 0 : cblk->passes[passno - 1].rate));
		assert(diff >= 0);
#endif
		pass->len = (uint16_t)(pass->rate - (passno == 0 ? 0 : cblk->passes[passno - 1].rate));
	}
	return cumwmsedec;
}

static inline int32_t int_fix_mul_t1(int32_t a, int32_t b)
{
#if defined(_MSC_VER) && (_MSC_VER >= 1400) && !defined(__INTEL_COMPILER) && defined(_M_IX86)
	int64_t temp = __emul(a, b);
#else
	int64_t temp = (int64_t)a * (int64_t)b;
#endif
	temp += 4096;
	assert((temp >> (13 + 11 - T1_NMSEDEC_FRACBITS)) <= (int64_t)0x7FFFFFFF);
	assert((temp >> (13 + 11 - T1_NMSEDEC_FRACBITS)) >= (-(int64_t)0x7FFFFFFF - (int64_t)1));
	return (int32_t)(temp >> (13 + 11 - T1_NMSEDEC_FRACBITS));
}

void t1_encode::preEncode(encodeBlockInfo* block, tcd_tile_t *tile, uint32_t& max) {
	auto state = grok_plugin_get_debug_state();
	//1. prepare low-level encode
	auto tilec = tile->comps + block->compno;
	initBuffers((uint16_t)(block->cblk->x1 - block->cblk->x0),
		(uint16_t)(block->cblk->y1 - block->cblk->y0));

	uint32_t tile_width = (tilec->x1 - tilec->x0);
	auto tileLineAdvance = tile_width - w;
	auto tiledp = block->tiledp;
#ifdef DEBUG_LOSSLESS_T1
	block->unencodedData = new int32_t[t1_encoder->w * t1_encoder->h];
#endif
	uint32_t tileIndex = 0;
	max = 0;
	uint32_t cblk_index = 0;
	if (block->qmfbid == 1) {
		for (auto j = 0U; j < h; ++j) {
			for (auto i = 0U; i < w; ++i) {
#ifdef DEBUG_LOSSLESS_T1
				block->unencodedData[cblk_index] = block->tiledp[tileIndex];
#endif
				// should we disable multiplication by (1 << T1_NMSEDEC_FRACBITS)
				// when ((state & GROK_PLUGIN_STATE_DEBUG) && !(state & GROK_PLUGIN_STATE_PRE_TR1)) is true ?
				// Disabling multiplication was messing up post-encode comparison
				// between plugin and grok open source
				int32_t tmp = (block->tiledp[tileIndex] *= (1 << T1_NMSEDEC_FRACBITS));
				uint32_t mag = (uint32_t)abs(tmp);
				max = std::max<uint32_t>(max, mag);
				data[cblk_index] = mag | ((uint32_t)(tmp < 0) << T1_DATA_SIGN_BIT_INDEX);
				tileIndex++;
				cblk_index++;
			}
			tileIndex += tileLineAdvance;
		}
	}
	else {
		for (auto j = 0U; j < h; ++j) {
			for (auto i = 0U; i < w; ++i) {
				// In lossy mode, we do a direct pass through of the image data in two cases while in debug encode mode:
				// 1. plugin is being used for full T1 encoding, so no need to quantize in grok
				// 2. plugin is only being used for pre T1 encoding, and we are applying quantization
				//    in the plugin DWT step
				int32_t tmp = 0;
				if (!(state & GROK_PLUGIN_STATE_DEBUG) ||
					((state & GROK_PLUGIN_STATE_PRE_TR1) && !(state & GROK_PLUGIN_STATE_DWT_QUANTIZATION))) {
					tmp = int_fix_mul_t1(tiledp[tileIndex], block->bandconst);
				}
				else {
					tmp = tiledp[tileIndex];
				}
				uint32_t mag = (uint32_t)abs(tmp);
				uint32_t sign_mag = mag | ((uint32_t)(tmp < 0) << T1_DATA_SIGN_BIT_INDEX);
				max = std::max<uint32_t>(max, mag);
				data[cblk_index] = sign_mag;
				tileIndex++;
				cblk_index++;
			}
			tileIndex += tileLineAdvance;
		}
	}
}

}
