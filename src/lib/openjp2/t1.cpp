/*
*    Copyright (C) 2016-2017 Grok Image Compression Inc.
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
#include "t1.h"
#include "t1_luts.h"
#include "T1Encoder.h"

namespace grk {

#define T1_SIG_OTH (T1_SIG_N|T1_SIG_NE|T1_SIG_E|T1_SIG_SE|T1_SIG_S|T1_SIG_SW|T1_SIG_W|T1_SIG_NW)
#define T1_SIG_PRIM (T1_SIG_N|T1_SIG_E|T1_SIG_S|T1_SIG_W)

#define T1_SGN (T1_SGN_N|T1_SGN_E|T1_SGN_S|T1_SGN_W)

const flag_t  T1_SIG = 0x1000;
const flag_t  T1_REFINE = 0x2000;
const flag_t T1_VISIT = 0x4000;

#define MACRO_t1_flags(x,y) t1->flags[((x)*(t1->flags_stride))+(y)]

/**
Tier-1 coding (coding of code-block coefficients)
*/
static inline uint8_t t1_getctxno_zc(flag_t f, uint8_t orient);
static uint8_t t1_getctxno_sc(flag_t f);
static inline uint8_t t1_getctxno_mag(flag_t f);
static uint8_t t1_getspb(flag_t f);
static void t1_updateflags(flag_t *flagsp, uint32_t s, uint32_t stride);

/**
Decode significant pass
*/
static inline void t1_dec_sigpass_step_raw(
	t1_t *t1,
	flag_t *flagsp,
	int32_t *datap,
	int32_t oneplushalf,
	bool vsc);
static inline void t1_dec_sigpass_step_mqc(
	t1_t *t1,
	flag_t *flagsp,
	int32_t *datap,
	uint8_t orient,
	int32_t oneplushalf);
static inline void t1_dec_sigpass_step_mqc_vsc(
	t1_t *t1,
	flag_t *flagsp,
	int32_t *datap,
	uint8_t orient,
	int32_t oneplushalf,
	bool vsc);

/**
Decode significant pass
*/
static void t1_dec_sigpass_raw(
	t1_t *t1,
	int32_t bpno,
	uint32_t cblksty);
static void t1_dec_sigpass_mqc(
	t1_t *t1,
	int32_t bpno,
	uint8_t orient);
static void t1_dec_sigpass_mqc_vsc(
	t1_t *t1,
	int32_t bpno,
	uint8_t orient);

/**
Decode refinement pass
*/
static void t1_dec_refpass_raw(
	t1_t *t1,
	int32_t bpno,
	uint32_t cblksty);
static void t1_dec_refpass_mqc(
	t1_t *t1,
	int32_t bpno);
static void t1_dec_refpass_mqc_vsc(
	t1_t *t1,
	int32_t bpno);


/**
Decode refinement pass
*/
static inline void  t1_dec_refpass_step_raw(
	t1_t *t1,
	flag_t *flagsp,
	int32_t *datap,
	int32_t poshalf,
	int32_t neghalf,
	bool vsc);
static inline void t1_dec_refpass_step_mqc(
	t1_t *t1,
	flag_t *flagsp,
	int32_t *datap,
	int32_t poshalf,
	int32_t neghalf);
static inline void t1_dec_refpass_step_mqc_vsc(
	t1_t *t1,
	flag_t *flagsp,
	int32_t *datap,
	int32_t poshalf,
	bool vsc);

/**
Decode clean-up pass
*/
static void t1_dec_clnpass_step_partial(
	t1_t *t1,
	flag_t *flagsp,
	int32_t *datap,
	int32_t oneplushalf);
static void t1_dec_clnpass_step(
	t1_t *t1,
	flag_t *flagsp,
	int32_t *datap,
	uint8_t orient,
	int32_t oneplushalf);
static void t1_dec_clnpass_step_vsc(
	t1_t *t1,
	flag_t *flagsp,
	int32_t *datap,
	uint8_t orient,
	int32_t oneplushalf,
	int32_t partial,
	bool vsc);

/**
Decode clean-up pass
*/
static void t1_dec_clnpass(
	t1_t *t1,
	int32_t bpno,
	uint8_t orient,
	uint32_t cblksty);

/*@}*/

/*@}*/



static uint8_t t1_getctxno_zc(flag_t f, uint8_t orient) {
	return lut_ctxno_zc[(orient << 8) | (f & T1_SIG_OTH)];
}

static uint8_t t1_getctxno_sc(flag_t f) {
	return lut_ctxno_sc[(f & (T1_SIG_PRIM | T1_SGN)) >> 4];
}

static uint8_t t1_getctxno_mag(flag_t f) {
	uint8_t tmp1 = (f & T1_SIG_OTH) ? T1_CTXNO_MAG + 1 : T1_CTXNO_MAG;
	uint8_t tmp2 = (f & T1_REFINE) ? T1_CTXNO_MAG + 2 : tmp1;
	return (tmp2);
}

static uint8_t t1_getspb(flag_t f) {
	return lut_spb[(f & (T1_SIG_PRIM | T1_SGN)) >> 4];
}

static void t1_updateflags(flag_t *flagsp, uint32_t s, uint32_t stride) {
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

static inline void t1_dec_sigpass_step_raw(t1_t *t1,
											flag_t *flagsp,
											int32_t *datap,
											int32_t oneplushalf,
											bool vsc){
	raw_t *raw = t1->raw;
	// ignore locations in next stripe when VSC flag is set
	flag_t flag = vsc ? ((*flagsp) & (~(T1_SIG_S | T1_SIG_SE | T1_SIG_SW | T1_SGN_S))) : (*flagsp);
	if ((flag & T1_SIG_OTH) && !(flag & (T1_SIG))) {
		if (raw_decode(raw)) {
			uint8_t v = (int32_t)raw_decode(raw);
			*datap = v ? -oneplushalf : oneplushalf;
			t1_updateflags(flagsp, (uint32_t)v, t1->flags_stride);
		}
		*flagsp |= T1_VISIT;
	}
}
static inline void t1_dec_sigpass_step_mqc(t1_t *t1,
											flag_t *flagsp,
											int32_t *datap,
											uint8_t orient,
											int32_t oneplushalf){
	mqc_t *mqc = t1->mqc;
	flag_t flag = *flagsp;
	if ((flag & T1_SIG_OTH) && !(flag & (T1_SIG))) {
		mqc_setcurctx(mqc, t1_getctxno_zc(flag, orient));
		if (mqc_decode(mqc)) {
			mqc_setcurctx(mqc, t1_getctxno_sc(flag));
			uint8_t v = mqc_decode(mqc) ^ t1_getspb(flag);
			*datap = v ? -oneplushalf : oneplushalf;
			t1_updateflags(flagsp, (uint32_t)v, t1->flags_stride);
		}
		*flagsp |= T1_VISIT;
	}
}
static inline void t1_dec_sigpass_step_mqc_vsc(t1_t *t1,
												flag_t *flagsp,
												int32_t *datap,
												uint8_t orient,
												int32_t oneplushalf,
												bool vsc){
	mqc_t *mqc = t1->mqc;
	// ignore locations in next stripe when VSC flag is set
	flag_t flag = vsc ? (flag_t)((*flagsp) & (~(T1_SIG_S | T1_SIG_SE | T1_SIG_SW | T1_SGN_S))) : (*flagsp);
	if ((flag & T1_SIG_OTH) && !(flag & (T1_SIG))) {
		mqc_setcurctx(mqc, t1_getctxno_zc(flag, orient));
		if (mqc_decode(mqc)) {
			mqc_setcurctx(mqc, t1_getctxno_sc(flag));
			uint8_t v = mqc_decode(mqc) ^ t1_getspb(flag);
			*datap = v ? -oneplushalf : oneplushalf;
			t1_updateflags(flagsp, (uint32_t)v, t1->flags_stride);
		}
		*flagsp |= T1_VISIT;
	}
}
static void t1_dec_sigpass_raw(t1_t *t1,
								int32_t bpno,
								uint32_t cblksty){
	int32_t one = 1 << bpno;
	int32_t half = one >> 1;
	int32_t oneplushalf = one | half;
	for (uint32_t k = 0; k < t1->h; k += 4) {
		for (uint32_t i = 0; i < t1->w; ++i) {
			for (uint32_t j = k; j < k + 4 && j < t1->h; ++j) {
				// VSC flag is set for last line of stripe
				int32_t vsc = ((cblksty & J2K_CCP_CBLKSTY_VSC) && (j == k + 3 || j == t1->h - 1)) ? 1 : 0;
				t1_dec_sigpass_step_raw(
					t1,
					&t1->flags[((j + 1) * t1->flags_stride) + i + 1],
					&t1->data[(j * t1->w) + i],
					oneplushalf,
					vsc);
			}
		}
	}
}

static void t1_dec_sigpass_mqc(t1_t *t1,
								int32_t bpno,
								uint8_t orient){
	int32_t one, half, oneplushalf;
	uint32_t i, j, k;
	int32_t *data1 = t1->data;
	flag_t *flags1 = &t1->flags[1];
	one = 1 << bpno;
	half = one >> 1;
	oneplushalf = one | half;
	for (k = 0; k < (t1->h & ~3u); k += 4) {
		for (i = 0; i < t1->w; ++i) {
			int32_t *data2 = data1 + i;
			flag_t *flags2 = flags1 + i;
			flags2 += t1->flags_stride;
			t1_dec_sigpass_step_mqc(t1, flags2, data2, orient, oneplushalf);
			data2 += t1->w;
			flags2 += t1->flags_stride;
			t1_dec_sigpass_step_mqc(t1, flags2, data2, orient, oneplushalf);
			data2 += t1->w;
			flags2 += t1->flags_stride;
			t1_dec_sigpass_step_mqc(t1, flags2, data2, orient, oneplushalf);
			data2 += t1->w;
			flags2 += t1->flags_stride;
			t1_dec_sigpass_step_mqc(t1, flags2, data2, orient, oneplushalf);
			data2 += t1->w;
		}
		data1 += (size_t)t1->w << 2;
		flags1 += (size_t)t1->flags_stride << 2;
	}
	for (i = 0; i < t1->w; ++i) {
		int32_t *data2 = data1 + i;
		flag_t *flags2 = flags1 + i;
		for (j = k; j < t1->h; ++j) {
			flags2 += t1->flags_stride;
			t1_dec_sigpass_step_mqc(t1, flags2, data2, orient, oneplushalf);
			data2 += t1->w;
		}
	}
}
static void t1_dec_sigpass_mqc_vsc(t1_t *t1,
									int32_t bpno,
									uint8_t orient){
	int32_t one, half, oneplushalf, vsc;
	uint32_t i, j, k;
	one = 1 << bpno;
	half = one >> 1;
	oneplushalf = one | half;
	for (k = 0; k < t1->h; k += 4) {
		for (i = 0; i < t1->w; ++i) {
			for (j = k; j < k + 4 && j < t1->h; ++j) {
				// VSC flag is set for last line of stripe
				vsc = (j == k + 3 || j == t1->h - 1) ? 1 : 0;
				t1_dec_sigpass_step_mqc_vsc(
					t1,
					&t1->flags[((j + 1) * t1->flags_stride) + i + 1],
					&t1->data[(j * t1->w) + i],
					orient,
					oneplushalf,
					vsc);
			}
		}
	}
}

static inline void t1_dec_refpass_step_raw(t1_t *t1,
											flag_t *flagsp,
											int32_t *datap,
											int32_t poshalf,
											int32_t neghalf,
											bool vsc){
	raw_t *raw = t1->raw;
	// ignore locations in next stripe when VSC flag is set
	flag_t flag = vsc ? ((*flagsp) & (~(T1_SIG_S | T1_SIG_SE | T1_SIG_SW | T1_SGN_S))) : (*flagsp);
	if ((flag & (T1_SIG | T1_VISIT)) == T1_SIG) {
		uint8_t v = (int32_t)raw_decode(raw);
		int32_t t = v ? poshalf : neghalf;
		*datap += *datap < 0 ? -t : t;
		*flagsp |= T1_REFINE;
	}
}

static inline void t1_dec_refpass_step_mqc(t1_t *t1,
											flag_t *flagsp,
											int32_t *datap,
											int32_t poshalf,
											int32_t neghalf){
	mqc_t *mqc = t1->mqc;
	flag_t flag = *flagsp;
	if ((flag & (T1_SIG | T1_VISIT)) == T1_SIG) {
		mqc_setcurctx(mqc, t1_getctxno_mag(flag));
		uint8_t v = mqc_decode(mqc);
		int32_t t = v ? poshalf : neghalf;
		*datap += *datap < 0 ? -t : t;
		*flagsp |= T1_REFINE;
	}
}
static inline void t1_dec_refpass_step_mqc_vsc(t1_t *t1,
												flag_t *flagsp,
												int32_t *datap,
												int32_t poshalf,
												bool vsc){
	mqc_t *mqc = t1->mqc;
	// ignore locations in next stripe when VSC flag is set
	flag_t flag = vsc ? (flag_t)((*flagsp) & (~(T1_SIG_S | T1_SIG_SE | T1_SIG_SW | T1_SGN_S))) : (*flagsp);
	if ((flag & (T1_SIG | T1_VISIT)) == T1_SIG) {
		mqc_setcurctx(mqc, t1_getctxno_mag(flag));
		*datap += (mqc_decode(mqc) ^ (*datap < 0)) ? poshalf : -poshalf;
		*flagsp |= T1_REFINE;
	}
}
static void t1_dec_refpass_raw(t1_t *t1,
								int32_t bpno,
								uint32_t cblksty){
	int32_t one, poshalf, neghalf;
	uint32_t i, j, k;
	bool vsc;
	one = 1 << bpno;
	poshalf = one >> 1;
	neghalf = bpno > 0 ? -poshalf : -1;
	for (k = 0; k < t1->h; k += 4) {
		for (i = 0; i < t1->w; ++i) {
			for (j = k; j < k + 4 && j < t1->h; ++j) {
				// VSC flag is set for last line of stripe
				vsc = ((cblksty & J2K_CCP_CBLKSTY_VSC) && (j == k + 3 || j == t1->h - 1)) ? 1 : 0;
				t1_dec_refpass_step_raw(
					t1,
					&t1->flags[((j + 1) * t1->flags_stride) + i + 1],
					&t1->data[(j * t1->w) + i],
					poshalf,
					neghalf,
					vsc);
			}
		}
	}
}
static void t1_dec_refpass_mqc(t1_t *t1,
								int32_t bpno) {
	int32_t one, poshalf, neghalf;
	uint32_t i, j, k;
	int32_t *data1 = t1->data;
	flag_t *flags1 = &t1->flags[1];
	one = 1 << bpno;
	poshalf = one >> 1;
	neghalf = bpno > 0 ? -poshalf : -1;
	for (k = 0; k < (t1->h & ~3u); k += 4) {
		for (i = 0; i < t1->w; ++i) {
			int32_t *data2 = data1 + i;
			flag_t *flags2 = flags1 + i;
			flags2 += t1->flags_stride;
			t1_dec_refpass_step_mqc(t1, flags2, data2, poshalf, neghalf);
			data2 += t1->w;
			flags2 += t1->flags_stride;
			t1_dec_refpass_step_mqc(t1, flags2, data2, poshalf, neghalf);
			data2 += t1->w;
			flags2 += t1->flags_stride;
			t1_dec_refpass_step_mqc(t1, flags2, data2, poshalf, neghalf);
			data2 += t1->w;
			flags2 += t1->flags_stride;
			t1_dec_refpass_step_mqc(t1, flags2, data2, poshalf, neghalf);
			data2 += t1->w;
		}
		data1 += (size_t)t1->w << 2;
		flags1 += (size_t)t1->flags_stride << 2;
	}
	for (i = 0; i < t1->w; ++i) {
		int32_t *data2 = data1 + i;
		flag_t *flags2 = flags1 + i;
		for (j = k; j < t1->h; ++j) {
			flags2 += t1->flags_stride;
			t1_dec_refpass_step_mqc(t1, flags2, data2, poshalf, neghalf);
			data2 += t1->w;
		}
	}
}

static void t1_dec_refpass_mqc_vsc(t1_t *t1,
									int32_t bpno) {
	int32_t one = 1 << bpno;
	int32_t poshalf = one >> 1;
	for (uint32_t k = 0; k < t1->h; k += 4) {
		for (uint32_t i = 0; i < t1->w; ++i) {
			for (uint32_t j = k; j < k + 4 && j < t1->h; ++j) {
				// VSC flag is set for last line of stripe
				uint8_t vsc = ((j == k + 3 || j == t1->h - 1)) ? 1 : 0;
				t1_dec_refpass_step_mqc_vsc(
					t1,
					&t1->flags[((j + 1) * t1->flags_stride) + i + 1],
					&t1->data[(j * t1->w) + i],
					poshalf,
					vsc);
			}
		}
	}
}

static void t1_dec_clnpass_step_partial(t1_t *t1,
										flag_t *flagsp,
										int32_t *datap,
										int32_t oneplushalf) {
	mqc_t *mqc = t1->mqc;
	flag_t flag = *flagsp;
	mqc_setcurctx(mqc, t1_getctxno_sc(flag));
	uint8_t v = mqc_decode(mqc) ^ t1_getspb(flag);
	*datap = v ? -oneplushalf : oneplushalf;
	t1_updateflags(flagsp, (uint32_t)v, t1->flags_stride);
	*flagsp &= (flag_t)~T1_VISIT;
}

static void t1_dec_clnpass_step(t1_t *t1,
								flag_t *flagsp,
								int32_t *datap,
								uint8_t orient,
								int32_t oneplushalf) {
	mqc_t *mqc = t1->mqc;
	flag_t flag = *flagsp;
	if (!(flag & (T1_SIG | T1_VISIT))) {
		mqc_setcurctx(mqc, t1_getctxno_zc(flag, orient));
		if (mqc_decode(mqc)) {
			mqc_setcurctx(mqc, t1_getctxno_sc(flag));
			uint8_t v = mqc_decode(mqc) ^ t1_getspb(flag);
			*datap = v ? -oneplushalf : oneplushalf;
			t1_updateflags(flagsp, (uint32_t)v, t1->flags_stride);
		}
	}
	*flagsp &= (flag_t)~T1_VISIT;
}

static void t1_dec_clnpass_step_vsc(t1_t *t1,
									flag_t *flagsp,
									int32_t *datap,
									uint8_t orient,
									int32_t oneplushalf,
									int32_t partial,
									bool vsc) {
	mqc_t *mqc = t1->mqc;
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
			t1_updateflags(flagsp, (uint32_t)v, t1->flags_stride);
		}
	}
	*flagsp &= (flag_t)~T1_VISIT;
}

static void t1_dec_clnpass(t1_t *t1,
							int32_t bpno,
							uint8_t orient,
							uint32_t cblksty) {
	int32_t one, half, oneplushalf, agg, vsc;
	uint8_t runlen;
	uint32_t i, j, k;
	int32_t segsym = cblksty & J2K_CCP_CBLKSTY_SEGSYM;

	mqc_t *mqc = t1->mqc;

	one = 1 << bpno;
	half = one >> 1;
	oneplushalf = one | half;
	if (cblksty & J2K_CCP_CBLKSTY_VSC) {
		for (k = 0; k < t1->h; k += 4) {
			for (i = 0; i < t1->w; ++i) {
				if (k + 3 < t1->h) {
					agg = !(MACRO_t1_flags(1 + k, 1 + i) & (T1_SIG | T1_VISIT | T1_SIG_OTH)
						|| MACRO_t1_flags(1 + k + 1, 1 + i) & (T1_SIG | T1_VISIT | T1_SIG_OTH)
						|| MACRO_t1_flags(1 + k + 2, 1 + i) & (T1_SIG | T1_VISIT | T1_SIG_OTH)
						|| (MACRO_t1_flags(1 + k + 3, 1 + i)
							& (~(T1_SIG_S | T1_SIG_SE | T1_SIG_SW | T1_SGN_S))) & (T1_SIG | T1_VISIT | T1_SIG_OTH));
				}
				else {
					agg = 0;
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
				for (j = k + (uint32_t)runlen; j < k + 4 && j < t1->h; ++j) {
					// VSC flag is set for last line of stripe
					vsc = (j == k + 3 || j == t1->h - 1) ? 1 : 0;
					t1_dec_clnpass_step_vsc(
						t1,
						&t1->flags[((j + 1) * t1->flags_stride) + i + 1],
						&t1->data[(j * t1->w) + i],
						orient,
						oneplushalf,
						agg && (j == k + (uint32_t)runlen),
						vsc);
				}
			}
		}
	}
	else {
		int32_t *data1 = t1->data;
		flag_t *flags1 = &t1->flags[1];
		for (k = 0; k < (t1->h & ~3u); k += 4) {
			for (i = 0; i < t1->w; ++i) {
				int32_t *data2 = data1 + i;
				flag_t *flags2 = flags1 + i;
				agg = !((MACRO_t1_flags(1 + k, 1 + i) |
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
					flags2 += (uint32_t)runlen * t1->flags_stride;
					data2 += (uint32_t)runlen * t1->w;
					for (j = (uint32_t)runlen; j < 4 && j < t1->h; ++j) {
						flags2 += t1->flags_stride;
						if (agg && (j == (uint32_t)runlen)) {
							t1_dec_clnpass_step_partial(t1, flags2, data2, oneplushalf);
						}
						else {
							t1_dec_clnpass_step(t1, flags2, data2, orient, oneplushalf);
						}
						data2 += t1->w;
					}
				}
				else {
					flags2 += t1->flags_stride;
					t1_dec_clnpass_step(t1, flags2, data2, orient, oneplushalf);
					data2 += t1->w;
					flags2 += t1->flags_stride;
					t1_dec_clnpass_step(t1, flags2, data2, orient, oneplushalf);
					data2 += t1->w;
					flags2 += t1->flags_stride;
					t1_dec_clnpass_step(t1, flags2, data2, orient, oneplushalf);
					data2 += t1->w;
					flags2 += t1->flags_stride;
					t1_dec_clnpass_step(t1, flags2, data2, orient, oneplushalf);
					data2 += t1->w;
				}
			}
			data1 += (size_t)t1->w << 2;
			flags1 += (size_t)t1->flags_stride << 2;
		}
		for (i = 0; i < t1->w; ++i) {
			int32_t *data2 = data1 + i;
			flag_t *flags2 = flags1 + i;
			for (j = k; j < t1->h; ++j) {
				flags2 += t1->flags_stride;
				t1_dec_clnpass_step(t1, flags2, data2, orient, oneplushalf);
				data2 += t1->w;
			}
		}
	}
	if (segsym) {
		uint8_t v = 0;
		mqc_setcurctx(mqc, T1_CTXNO_UNI);
		v = mqc_decode(mqc);
		v = (uint8_t)(v << 1) | mqc_decode(mqc);
		v = (uint8_t)(v << 1) | mqc_decode(mqc);
		v = (uint8_t)(v << 1) | mqc_decode(mqc);
		/*
		if (v!=0xa) {
			event_msg(t1->cinfo, EVT_WARNING, "Bad segmentation symbol %x\n", v);
		}
		*/
	}
}

bool t1_allocate_buffers(t1_t *t1,
						uint16_t w,
						uint16_t h) {
	/* encoder uses tile buffer, so no need to allocate */
	if (!t1->encoder) {
		uint32_t datasize = w * h;
		if (datasize > t1->datasize) {
			if (t1->data)
				grok_aligned_free(t1->data);
			t1->data = (int32_t*)grok_aligned_malloc(datasize * sizeof(int32_t));
			if (!t1->data) {
				/* FIXME event manager error callback */
				return false;
			}
			t1->datasize = datasize;
		}
		memset(t1->data, 0, datasize * sizeof(int32_t));
	}
	t1->flags_stride = w + 2;
	uint32_t flagssize = t1->flags_stride * (h + 2);
	if (flagssize > t1->flagssize) {
		if (t1->flags)
			grok_aligned_free(t1->flags);
		t1->flags = (flag_t*)grok_aligned_malloc(flagssize * sizeof(flag_t));
		if (!t1->flags) {
			/* FIXME event manager error callback */
			return false;
		}
		t1->flagssize = flagssize;
	}
	memset(t1->flags, 0, flagssize * sizeof(flag_t));
	t1->w = w;
	t1->h = h;
	return true;
}

/**
	* Creates a new Tier 1 handle
	* and initializes the look-up tables of the Tier-1 coder/decoder
	* @return a new T1 handle if successful, returns NULL otherwise
*/
t1_t::t1_t(uint16_t code_block_width, uint16_t code_block_height) :
																				compressed_block(nullptr),
																				compressed_block_size(0),
																				mqc(nullptr),
																				raw(nullptr),
																				data(nullptr),
																				flags(nullptr),
																				w(0),
																				h(0),
																				datasize(0),
																				flagssize(0),
																				flags_stride(0),
																				encoder(false) 	{
	mqc = mqc_create();
	if (!mqc) {
		throw std::exception();
	}

	raw = raw_create();
	if (!raw) {
		throw std::exception();
	}

	if (!encoder && code_block_width > 0 && code_block_height > 0) {
		compressed_block = (uint8_t*)grok_malloc((size_t)code_block_width * (size_t)code_block_height);
		if (!compressed_block) {
			throw std::exception();
		}
		compressed_block_size = (size_t)(code_block_width * code_block_height);

	}
}


/**
	* Destroys a previously created T1 handle
	*
	* @param p_t1 Tier 1 handle to destroy
*/
t1_t::~t1_t() {

	/* destroy MQC and RAW handles */
	mqc_destroy(mqc);
	raw_destroy(raw);

	/* encoder uses tile buffer, so no need to free */
	if (!encoder && data) {
		grok_aligned_free(data);
	}

	if (flags) {
		grok_aligned_free(flags);
	}
	if (compressed_block)
		grok_free(compressed_block);
}

bool t1_decode_cblk(t1_t *t1,
	tcd_cblk_dec_t* cblk,
	uint8_t orient,
	uint32_t roishift,
	uint32_t cblksty)
{
	raw_t *raw = t1->raw;
	mqc_t *mqc = t1->mqc;

	int32_t bpno_plus_one;
	uint32_t passtype;
	uint32_t segno, passno;
	uint8_t type = T1_TYPE_MQ; /* BYPASS mode */
	uint8_t* block_buffer = NULL;
	size_t total_seg_len;

	if (!t1_allocate_buffers(
		t1,
		cblk->x1 - cblk->x0,
		cblk->y1 - cblk->y0)) {
		return false;
	}

	total_seg_len = min_buf_vec_get_len(&cblk->seg_buffers);
	if (cblk->numSegments && total_seg_len) {
		/* if there is only one segment, then it is already contiguous, so no need to make a copy*/
		if (total_seg_len == 1 && cblk->seg_buffers.get(0)) {
			block_buffer = ((buf_t*)(cblk->seg_buffers.get(0)))->buf;
		}
		else {
			/* block should have been allocated on creation of t1*/
			if (!t1->compressed_block)
				return false;
			if (t1->compressed_block_size < total_seg_len) {
				uint8_t* new_block = (uint8_t*)grok_realloc(t1->compressed_block, total_seg_len);
				if (!new_block)
					return false;
				t1->compressed_block = new_block;
				t1->compressed_block_size = total_seg_len;
			}
			min_buf_vec_copy_to_contiguous_buffer(&cblk->seg_buffers, t1->compressed_block);
			block_buffer = t1->compressed_block;
		}
	}
	else {
		return true;
	}



	bpno_plus_one = (int32_t)(roishift + cblk->numbps);
	passtype = 2;

	mqc_resetstates(mqc);
	for (segno = 0; segno < cblk->numSegments; ++segno) {
		tcd_seg_t *seg = &cblk->segs[segno];

		/* BYPASS mode */
		type = ((bpno_plus_one <= ((int32_t)(cblk->numbps)) - 4) && (passtype < 2) && (cblksty & J2K_CCP_CBLKSTY_LAZY)) ? T1_TYPE_RAW : T1_TYPE_MQ;
		if (type == T1_TYPE_RAW) {
			raw_init_dec(raw, block_buffer + seg->dataindex, seg->len);
		}
		else {
			mqc_init_dec(mqc, block_buffer + seg->dataindex, seg->len);
		}

		for (passno = 0; (passno < seg->numpasses) && (bpno_plus_one >= 1); ++passno) {
			switch (passtype) {
			case 0:
				if (type == T1_TYPE_RAW) {
					t1_dec_sigpass_raw(t1, bpno_plus_one, (int32_t)cblksty);
				}
				else {
					if (cblksty & J2K_CCP_CBLKSTY_VSC) {
						t1_dec_sigpass_mqc_vsc(t1, bpno_plus_one, (int32_t)orient);
					}
					else {
						t1_dec_sigpass_mqc(t1, bpno_plus_one, (int32_t)orient);
					}
				}
				break;
			case 1:
				if (type == T1_TYPE_RAW) {
					t1_dec_refpass_raw(t1, bpno_plus_one, (int32_t)cblksty);
				}
				else {
					if (cblksty & J2K_CCP_CBLKSTY_VSC) {
						t1_dec_refpass_mqc_vsc(t1, bpno_plus_one);
					}
					else {
						t1_dec_refpass_mqc(t1, bpno_plus_one);
					}
				}
				break;
			case 2:
				t1_dec_clnpass(t1, bpno_plus_one, (int32_t)orient, (int32_t)cblksty);
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
	}
	return true;
}
		
}
