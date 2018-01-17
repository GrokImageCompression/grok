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
#include "t1_decode.h"
#include "t1_luts.h"
#include "T1Encoder.h"

namespace grk {


#define T1_SIG_OTH (T1_SIG_N|T1_SIG_NE|T1_SIG_E|T1_SIG_SE|T1_SIG_S|T1_SIG_SW|T1_SIG_W|T1_SIG_NW)
#define T1_SIG_PRIM (T1_SIG_N|T1_SIG_E|T1_SIG_S|T1_SIG_W)

#define T1_SGN (T1_SGN_N|T1_SGN_E|T1_SGN_S|T1_SGN_W)

		const flag_t  T1_SIG = 0x1000;
		const flag_t  T1_REFINE = 0x2000;
		const flag_t T1_VISIT = 0x4000;

#define MACRO_t1_flags(x,y) flags[((x)*(flags_stride))+(y)]

/**
Tier-1 coding (coding of code-block coefficients)
*/
static inline uint8_t t1_getctxno_zc(flag_t f, uint8_t orient);
static inline uint8_t t1_getctxno_sc(flag_t f);
static inline uint8_t t1_getctxno_mag(flag_t f);
static inline uint8_t t1_getspb(flag_t f);


static uint8_t t1_getctxno_zc(flag_t f, uint8_t orient) {
	return lut_ctxno_zc[(orient << 8) | (f & T1_SIG_OTH)];
}

static inline uint8_t t1_getctxno_sc(flag_t f) {
	return lut_ctxno_sc[(f & (T1_SIG_PRIM | T1_SGN)) >> 4];
}

static inline uint8_t t1_getctxno_mag(flag_t f) {
	uint8_t tmp1 = (f & T1_SIG_OTH) ? T1_CTXNO_MAG + 1 : T1_CTXNO_MAG;
	uint8_t tmp2 = (f & T1_REFINE) ? T1_CTXNO_MAG + 2 : tmp1;
	return (tmp2);
}

static inline uint8_t t1_getspb(flag_t f) {
	return lut_spb[(f & (T1_SIG_PRIM | T1_SGN)) >> 4];
}

void t1_decode::updateflags(flag_t *flagsp, uint32_t s, uint32_t stride) {
	flag_t *np = flagsp - stride;
	flag_t *sp = flagsp + stride;

	static const flag_t mod[] = {
		T1_SIG_S, T1_SIG_S | T1_SGN_S,
		T1_SIG_E, T1_SIG_E | T1_SGN_E,
		T1_SIG_W, T1_SIG_W | T1_SGN_W,
		T1_SIG_N, T1_SIG_N | T1_SGN_N
	};

	np[-1] |= T1_SIG_SE;
	np[0] |= mod[s];
	np[1] |= T1_SIG_SW;

	flagsp[-1] |= mod[s + 2];
	flagsp[0] |= T1_SIG;
	flagsp[1] |= mod[s + 4];

	sp[-1] |= T1_SIG_NE;
	sp[0] |= mod[s + 6];
	sp[1] |= T1_SIG_NW;
}


t1_decode::t1_decode(uint16_t code_block_width, uint16_t code_block_height) : t1_decode_base(code_block_width, code_block_height), 
																			flags(nullptr),
																			flags_stride(0) {
	if (!allocateBuffers(code_block_width, code_block_height))
		throw std::exception();
}
t1_decode::~t1_decode() {
	if (flags)
		grok_aligned_free(flags);
}

inline void t1_decode::sigpass_step(flag_t *flagsp,
	int32_t *datap,
	uint8_t orient,
	int32_t oneplushalf) {
	flag_t flag = *flagsp;
	if ((flag & T1_SIG_OTH) && !(flag & (T1_SIG))) {
		mqc_setcurctx(mqc, t1_getctxno_zc(flag, orient));
		if (mqc_decode(mqc)) {
			mqc_setcurctx(mqc, t1_getctxno_sc(flag));
			uint8_t v = mqc_decode(mqc) ^ t1_getspb(flag);
			*datap = v ? -oneplushalf : oneplushalf;
			updateflags(flagsp, (uint32_t)v, flags_stride);
		}
		*flagsp |= T1_VISIT;
	}
}

void t1_decode::sigpass(int32_t bpno, uint8_t orient) {
	int32_t one, half, oneplushalf;
	uint32_t i, j, k;
	int32_t *data1 = dataPtr;
	flag_t *flags1 = &flags[1];
	one = 1 << bpno;
	half = one >> 1;
	oneplushalf = one | half;
	for (k = 0; k < (h & ~3u); k += 4) {
		for (i = 0; i < w; ++i) {
			int32_t *data2 = data1 + i;
			flag_t *flags2 = flags1 + i;
			flags2 += flags_stride;
			sigpass_step(flags2, data2, orient, oneplushalf);
			data2 += w;
			flags2 += flags_stride;
			sigpass_step(flags2, data2, orient, oneplushalf);
			data2 += w;
			flags2 += flags_stride;
			sigpass_step(flags2, data2, orient, oneplushalf);
			data2 += w;
			flags2 += flags_stride;
			sigpass_step(flags2, data2, orient, oneplushalf);
			data2 += w;
		}
		data1 += (size_t)w << 2;
		flags1 += (size_t)flags_stride << 2;
	}
	for (i = 0; i < w; ++i) {
		int32_t *data2 = data1 + i;
		flag_t *flags2 = flags1 + i;
		for (j = k; j < h; ++j) {
			flags2 += flags_stride;
			sigpass_step(flags2, data2, orient, oneplushalf);
			data2 += w;
		}
	}
}
inline void t1_decode::sigpass_step_raw(flag_t *flagsp,
	int32_t *datap,
	int32_t oneplushalf,
	bool vsc) {
	// ignore locations in next stripe when VSC flag is set
	flag_t flag = vsc ? (flag_t)((*flagsp) & (~(T1_SIG_S | T1_SIG_SE | T1_SIG_SW | T1_SGN_S))) : (*flagsp);
	if ((flag & T1_SIG_OTH) && !(flag & (T1_SIG))) {
		if (raw_decode(raw)) {
			uint8_t v = raw_decode(raw);
			*datap = v ? -oneplushalf : oneplushalf;
			updateflags(flagsp, (uint32_t)v, flags_stride);
		}
		*flagsp |= T1_VISIT;
	}
}
void t1_decode::sigpass_raw(int32_t bpno, uint32_t cblksty) {
	int32_t one = 1 << bpno;
	int32_t half = one >> 1;
	int32_t oneplushalf = one | half;
	for (uint32_t k = 0; k < h; k += 4) {
		for (uint32_t i = 0; i < w; ++i) {
			for (uint32_t j = k; j < k + 4 && j < h; ++j) {
				// VSC flag is set for last line of stripe
				bool vsc = ((cblksty & J2K_CCP_CBLKSTY_VSC) && (j == k + 3 || j == (uint32_t)(h - 1))) ? true : false;
				sigpass_step_raw(&flags[((j + 1) * flags_stride) + i + 1],
					&dataPtr[(j * w) + i],
					oneplushalf,
					vsc);
			}
		}
	}
}
inline void t1_decode::refpass_step_raw(flag_t *flagsp,
	int32_t *datap,
	int32_t poshalf,
	bool vsc) {
	// ignore locations in next stripe when VSC flag is set
	flag_t flag = vsc ? (flag_t)((*flagsp) & (~(T1_SIG_S | T1_SIG_SE | T1_SIG_SW | T1_SGN_S))) : (*flagsp);
	if ((flag & (T1_SIG | T1_VISIT)) == T1_SIG) {
		uint8_t v = raw_decode(raw);
		*datap += (v ^ (*datap < 0)) ? poshalf : -poshalf;
		*flagsp |= T1_REFINE;
	}
}
void t1_decode::refpass_raw(int32_t bpno,
	uint32_t cblksty) {
	int32_t one, poshalf;
	uint32_t i, j, k;
	bool vsc;
	one = 1 << bpno;
	poshalf = one >> 1;
	for (k = 0; k < h; k += 4) {
		for (i = 0; i < w; ++i) {
			for (j = k; j < k + 4 && j < h; ++j) {
				// VSC flag is set for last line of stripe
				vsc = ((cblksty & J2K_CCP_CBLKSTY_VSC) && (j == k + 3 || j == (uint32_t)(h - 1))) ? 1 : 0;
				refpass_step_raw(&flags[((j + 1) * flags_stride) + i + 1],
					&dataPtr[(j * w) + i],
					poshalf,
					vsc);
			}
		}
	}
}
inline void t1_decode::sigpass_step_vsc(flag_t *flagsp,
	int32_t *datap,
	uint8_t orient,
	int32_t oneplushalf,
	bool vsc) {
	// ignore locations in next stripe when VSC flag is set
	flag_t flag = vsc ? (flag_t)((*flagsp) & (~(T1_SIG_S | T1_SIG_SE | T1_SIG_SW | T1_SGN_S))) : (*flagsp);
	if ((flag & T1_SIG_OTH) && !(flag & (T1_SIG))) {
		mqc_setcurctx(mqc, t1_getctxno_zc(flag, orient));
		if (mqc_decode(mqc)) {
			mqc_setcurctx(mqc, t1_getctxno_sc(flag));
			uint8_t v = mqc_decode(mqc) ^ t1_getspb(flag);
			*datap = v ? -oneplushalf : oneplushalf;
			updateflags(flagsp, (uint32_t)v, flags_stride);
		}
		*flagsp |= T1_VISIT;
	}
}
void t1_decode::sigpass_vsc(int32_t bpno, uint8_t orient) {
	int32_t one, half, oneplushalf, vsc;
	uint32_t i, j, k;
	one = 1 << bpno;
	half = one >> 1;
	oneplushalf = one | half;
	for (k = 0; k < h; k += 4) {
		for (i = 0; i < w; ++i) {
			for (j = k; j < k + 4 && j < h; ++j) {
				// VSC flag is set for last line of stripe
				vsc = (j == k + 3 || j == (uint32_t)(h - 1)) ? 1 : 0;
				sigpass_step_vsc(&flags[((j + 1) * flags_stride) + i + 1],
					&dataPtr[(j * w) + i],
					orient,
					oneplushalf,
					vsc);
			}
		}
	}
}
inline void t1_decode::refpass_step(flag_t *flagsp,
	int32_t *datap,
	int32_t poshalf) {
	flag_t flag = *flagsp;
	if ((flag & (T1_SIG | T1_VISIT)) == T1_SIG) {
		mqc_setcurctx(mqc, t1_getctxno_mag(flag));
		uint8_t v = mqc_decode(mqc);
		*datap += (v ^ (*datap < 0)) ? poshalf : -poshalf;
		*flagsp |= T1_REFINE;
	}
}
void t1_decode::refpass(int32_t bpno) {
	int32_t one, poshalf;
	uint32_t i, j, k;
	int32_t *data1 = dataPtr;
	flag_t *flags1 = &flags[1];
	one = 1 << bpno;
	poshalf = one >> 1;
	for (k = 0; k < (h & ~3u); k += 4) {
		for (i = 0; i < w; ++i) {
			int32_t *data2 = data1 + i;
			flag_t *flags2 = flags1 + i;
			flags2 += flags_stride;
			refpass_step(flags2, data2, poshalf);
			data2 += w;
			flags2 += flags_stride;
			refpass_step(flags2, data2, poshalf);
			data2 += w;
			flags2 += flags_stride;
			refpass_step(flags2, data2, poshalf);
			data2 += w;
			flags2 += flags_stride;
			refpass_step(flags2, data2, poshalf);
			data2 += w;
		}
		data1 += (size_t)w << 2;
		flags1 += (size_t)flags_stride << 2;
	}
	for (i = 0; i < w; ++i) {
		int32_t *data2 = data1 + i;
		flag_t *flags2 = flags1 + i;
		for (j = k; j < h; ++j) {
			flags2 += flags_stride;
			refpass_step(flags2, data2, poshalf);
			data2 += w;
		}
	}
}
inline void t1_decode::refpass_step_vsc(flag_t *flagsp,
	int32_t *datap,
	int32_t poshalf,
	bool vsc) {
	// ignore locations in next stripe when VSC flag is set
	flag_t flag = vsc ? (flag_t)((*flagsp) & (~(T1_SIG_S | T1_SIG_SE | T1_SIG_SW | T1_SGN_S))) : (*flagsp);
	if ((flag & (T1_SIG | T1_VISIT)) == T1_SIG) {
		mqc_setcurctx(mqc, t1_getctxno_mag(flag));
		*datap += (mqc_decode(mqc) ^ (*datap < 0)) ? poshalf : -poshalf;
		*flagsp |= T1_REFINE;
	}
}
void t1_decode::refpass_vsc(int32_t bpno) {
	int32_t one = 1 << bpno;
	int32_t poshalf = one >> 1;
	for (uint32_t k = 0; k < h; k += 4) {
		for (uint32_t i = 0; i < w; ++i) {
			for (uint32_t j = k; j < k + 4 && j < h; ++j) {
				// VSC flag is set for last line of stripe
				uint8_t vsc = ((j == k + 3 || j == (uint32_t)(h - 1))) ? 1 : 0;
				refpass_step_vsc(&flags[((j + 1) * flags_stride) + i + 1],
					&dataPtr[(j * w) + i],
					poshalf,
					vsc);
			}
		}
	}
}
void t1_decode::clnpass_step_partial(flag_t *flagsp,
	int32_t *datap,
	int32_t oneplushalf) {
	flag_t flag = *flagsp;
	mqc_setcurctx(mqc, t1_getctxno_sc(flag));
	uint8_t v = mqc_decode(mqc) ^ t1_getspb(flag);
	*datap = v ? -oneplushalf : oneplushalf;
	updateflags(flagsp, (uint32_t)v, flags_stride);
	*flagsp &= (flag_t)~T1_VISIT;
}

void t1_decode::clnpass_step(flag_t *flagsp,
	int32_t *datap,
	uint8_t orient,
	int32_t oneplushalf) {
	flag_t flag = *flagsp;
	if (!(flag & (T1_SIG | T1_VISIT))) {
		mqc_setcurctx(mqc, t1_getctxno_zc(flag, orient));
		if (mqc_decode(mqc)) {
			mqc_setcurctx(mqc, t1_getctxno_sc(flag));
			uint8_t v = mqc_decode(mqc) ^ t1_getspb(flag);
			*datap = v ? -oneplushalf : oneplushalf;
			updateflags(flagsp, (uint32_t)v, flags_stride);
		}
	}
	*flagsp &= (flag_t)~T1_VISIT;
}

void t1_decode::clnpass_step_vsc(flag_t *flagsp,
	int32_t *datap,
	uint8_t orient,
	int32_t oneplushalf,
	int32_t partial,
	bool vsc) {
	// ignore locations in next stripe when VSC flag is set
	flag_t flag = vsc ? (flag_t)((*flagsp) & (~(T1_SIG_S | T1_SIG_SE | T1_SIG_SW | T1_SGN_S))) : (*flagsp);
	if (partial) {
		goto LABEL_PARTIAL;
	}
	if (!(flag & (T1_SIG | T1_VISIT))) {
		mqc_setcurctx(mqc, t1_getctxno_zc(flag, orient));
		if (mqc_decode(mqc)) {
		LABEL_PARTIAL:
			mqc_setcurctx(mqc, t1_getctxno_sc(flag));
			uint8_t v = mqc_decode(mqc) ^ t1_getspb(flag);
			*datap = v ? -oneplushalf : oneplushalf;
			updateflags(flagsp, (uint32_t)v, flags_stride);
		}
	}
	*flagsp &= (flag_t)~T1_VISIT;
}

void t1_decode::clnpass(int32_t bpno,
	uint8_t orient,
	uint32_t cblksty) {
	int32_t one, half, oneplushalf, vsc;
	uint8_t runlen;
	uint32_t i, j, k;
	one = 1 << bpno;
	half = one >> 1;
	oneplushalf = one | half;
	if (cblksty & J2K_CCP_CBLKSTY_VSC) {
		for (k = 0; k < h; k += 4) {
			for (i = 0; i < w; ++i) {
				int32_t agg = 0;
				if (k + 3 < h) {
					agg = !(MACRO_t1_flags(1 + k, 1 + i) & (T1_SIG | T1_VISIT | T1_SIG_OTH)
						|| MACRO_t1_flags(1 + k + 1, 1 + i) & (T1_SIG | T1_VISIT | T1_SIG_OTH)
						|| MACRO_t1_flags(1 + k + 2, 1 + i) & (T1_SIG | T1_VISIT | T1_SIG_OTH)
						|| (MACRO_t1_flags(1 + k + 3, 1 + i)
							& (~(T1_SIG_S | T1_SIG_SE | T1_SIG_SW | T1_SGN_S))) & (T1_SIG | T1_VISIT | T1_SIG_OTH));
				}
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
				for (j = k + (uint32_t)runlen; j < k + 4 && j < h; ++j) {
					// VSC flag is set for last line of stripe
					vsc = (j == k + 3 || j == (uint32_t)(h - 1)) ? 1 : 0;
					clnpass_step_vsc(&flags[((j + 1) * flags_stride) + i + 1],
						&dataPtr[(j * w) + i],
						orient,
						oneplushalf,
						agg && (j == k + (uint32_t)runlen),
						vsc);
				}
			}
		}
	}
	else {
		int32_t *data1 = dataPtr;
		flag_t *flags1 = &flags[1];
		for (k = 0; k < (h & ~3u); k += 4) {
			for (i = 0; i < w; ++i) {
				int32_t *data2 = data1 + i;
				flag_t *flags2 = flags1 + i;
				int32_t agg = !((MACRO_t1_flags(1 + k, 1 + i) |
					MACRO_t1_flags(1 + k + 1, 1 + i) |
					MACRO_t1_flags(1 + k + 2, 1 + i) |
					MACRO_t1_flags(1 + k + 3, 1 + i)) & (T1_SIG | T1_VISIT | T1_SIG_OTH));
				if (agg) {
					mqc_setcurctx(mqc, T1_CTXNO_AGG);
					if (!mqc_decode(mqc)) {
						continue;
					}
					mqc_setcurctx(mqc, T1_CTXNO_UNI);
					runlen = mqc_decode(mqc);
					runlen = (uint8_t)(runlen << 1) | mqc_decode(mqc);
					flags2 += (uint32_t)runlen * flags_stride;
					data2 += (uint32_t)runlen * w;
					for (j = (uint32_t)runlen; j < 4 && j < h; ++j) {
						flags2 += flags_stride;
						if (agg && (j == (uint32_t)runlen)) {
							clnpass_step_partial(flags2, data2, oneplushalf);
						}
						else {
							clnpass_step(flags2, data2, orient, oneplushalf);
						}
						data2 += w;
					}
				}
				else {
					flags2 += flags_stride;
					clnpass_step(flags2, data2, orient, oneplushalf);
					data2 += w;
					flags2 += flags_stride;
					clnpass_step(flags2, data2, orient, oneplushalf);
					data2 += w;
					flags2 += flags_stride;
					clnpass_step(flags2, data2, orient, oneplushalf);
					data2 += w;
					flags2 += flags_stride;
					clnpass_step(flags2, data2, orient, oneplushalf);
					data2 += w;
				}
			}
			data1 += (size_t)w << 2;
			flags1 += (size_t)flags_stride << 2;
		}
		for (i = 0; i < w; ++i) {
			int32_t *data2 = data1 + i;
			flag_t *flags2 = flags1 + i;
			for (j = k; j < h; ++j) {
				flags2 += flags_stride;
				clnpass_step(flags2, data2, orient, oneplushalf);
				data2 += w;
			}
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
bool t1_decode::allocateBuffers(uint16_t w, uint16_t h) {
	/* encoder uses tile buffer, so no need to allocate */
	if (!dataPtr) {
		dataPtr = (int32_t*)grok_aligned_malloc(w*h * sizeof(int32_t));
		if (!dataPtr) {
			/* FIXME event manager error callback */
			return false;
		}
	}
	flags_stride = (uint16_t)(w + 2);
	uint32_t new_flagssize = flags_stride * (h + 2);
	if (flags)
		grok_aligned_free(flags);
	flags = (flag_t*)grok_aligned_malloc(new_flagssize * sizeof(flag_t));
	if (!flags) {
		/* FIXME event manager error callback */
		return false;
	}
	initBuffers(w, h);
	return true;
}
void t1_decode::initBuffers(uint16_t cblkw, uint16_t cblkh) {
	w = cblkw;
	h = cblkh;
	flags_stride = (uint16_t)(w + 2);
	memset(flags, 0, flags_stride * (h + 2) * sizeof(flag_t));
	if (dataPtr)
		memset(dataPtr, 0, w * h * sizeof(int32_t));
}
bool t1_decode::decode_cblk(tcd_cblk_dec_t* cblk,
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
		*((uint16_t*)(compressed_block + synthOffset))=0xFFFF;
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
					sigpass_raw(bpno_plus_one, cblksty);
				}
				else {
					if (cblksty & J2K_CCP_CBLKSTY_VSC) {
						sigpass_vsc(bpno_plus_one, orient);
					}
					else {
						sigpass(bpno_plus_one, orient);
					}
				}
				break;
			case 1:
				if (type == T1_TYPE_RAW) {
					refpass_raw(bpno_plus_one, cblksty);
				}
				else {
					if (cblksty & J2K_CCP_CBLKSTY_VSC) {
						refpass_vsc(bpno_plus_one);
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

void t1_decode::postDecode(decodeBlockInfo* block) {
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
