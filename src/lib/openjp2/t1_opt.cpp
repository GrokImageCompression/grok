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
#include "t1_opt.h"
#include "t1.h"
#include "t1_opt_luts.h"

namespace grk
{



#define ENC_FLAGS(x, y) (t1->flags[(x) + 1 + (((y) >> 2) + 1) * t1->flags_stride])
#define ENC_FLAGS_ADDRESS(x, y) (t1->flags + ((x) + 1 + (((y) >> 2) + 1) * t1->flags_stride))

static inline uint8_t		t1_getctxno_zc(uint32_t f, uint8_t orient);
static inline uint8_t		t1_getctxno_sc(uint32_t fX, uint32_t pfX, uint32_t nfX, uint32_t ci3);
static inline uint8_t		t1_getctxno_mag(uint32_t f);
static inline uint8_t		t1_getspb(uint32_t fX, uint32_t pfX, uint32_t nfX, uint32_t ci3);
static inline void			t1_updateflags(flag_opt_t *flagsp, uint32_t ci3, uint32_t s, uint32_t stride, uint8_t vsc);

/**
Encode significant pass
*/
static void t1_enc_sigpass_step(t1_opt_t *t1,
	flag_opt_t *flagsp,
	uint32_t *datap,
	uint8_t orient,
	int32_t bpno,
	int32_t one,
	int32_t *nmsedec, 
	uint8_t type,
	uint32_t cblksty);

/**
Encode significant pass
*/
static void t1_enc_sigpass(t1_opt_t *t1,
	int32_t bpno,
	uint8_t orient,
	int32_t *nmsedec,
	uint8_t type, 
	uint32_t cblksty);


/**
Encode refinement pass
*/
static void t1_enc_refpass_step(t1_opt_t *t1,
	flag_opt_t *flagsp,
	uint32_t *datap,
	int32_t bpno,
	int32_t one,
	int32_t *nmsedec, 
	uint8_t type);


/**
Encode refinement pass
*/
static void t1_enc_refpass(t1_opt_t *t1,
	int32_t bpno,
	int32_t *nmsedec, 
	uint8_t type);

/**
Encode clean-up pass
*/
static void t1_enc_clnpass_step(
	t1_opt_t *t1,
	flag_opt_t *flagsp,
	uint32_t *datap,
	uint8_t orient,
	int32_t bpno,
	int32_t one,
	int32_t *nmsedec,
	uint32_t agg,
	uint32_t runlen,
	uint32_t y, 
	uint32_t cblksty);

/**
Encode clean-up pass
*/
static void t1_enc_clnpass(
	t1_opt_t *t1,
	int32_t bpno,
	uint8_t orient,
	int32_t *nmsedec, 
	uint32_t cblksty);


t1_opt_t::t1_opt_t(bool isEncoder) : mqc(nullptr), 
									data(nullptr),
									flags(nullptr),
									w(0),
									h(0),
									flags_stride(0),
									encoder(false) 	{

	mqc = mqc_create();
	if (!mqc) {
		throw std::exception();
	}
	encoder = isEncoder;
}

t1_opt_t::~t1_opt_t()
{
	mqc_destroy(mqc);
	if (data) {
		grok_aligned_free(data);
	}
	if (flags) {
		grok_aligned_free(flags);
	}
}

static inline uint8_t t1_getctxno_zc(uint32_t f, uint8_t orient)
{
	return lut_ctxno_zc_opt[(orient << 9) | (f & T1_SIGMA_NEIGHBOURS)];
}


static uint8_t t1_getctxno_sc(uint32_t fX, uint32_t pfX, uint32_t nfX, uint32_t ci3)
{
	/*
	0 pfX T1_CHI_CURRENT           T1_LUT_CTXNO_SGN_W
	1 tfX T1_SIGMA_1            T1_LUT_CTXNO_SIG_N
	2 nfX T1_CHI_CURRENT           T1_LUT_CTXNO_SGN_E
	3 tfX T1_SIGMA_3            T1_LUT_CTXNO_SIG_W
	4  fX T1_CHI_(THIS - 1)     T1_LUT_CTXNO_SGN_N
	5 tfX T1_SIGMA_5            T1_LUT_CTXNO_SIG_E
	6  fX T1_CHI_(THIS + 1)     T1_LUT_CTXNO_SGN_S
	7 tfX T1_SIGMA_7            T1_LUT_CTXNO_SIG_S
	*/

	uint32_t lu = (fX >> ci3) & (T1_SIGMA_1 | T1_SIGMA_3 | T1_SIGMA_5 | T1_SIGMA_7);

	lu |= (pfX >> (T1_CHI_CURRENT_I + ci3)) & (1U << 0);
	lu |= (nfX >> (T1_CHI_CURRENT_I - 2U + ci3)) & (1U << 2);
	if (ci3 == 0U) {
		lu |= (fX >> (T1_CHI_0_I - 4U)) & (1U << 4);
	}
	else {
		lu |= (fX >> (T1_CHI_1_I - 4U + (ci3 - 3))) & (1U << 4);
	}
	lu |= (fX >> (T1_CHI_2_I - 6U + ci3)) & (1U << 6);

	return lut_ctxno_sc_opt[lu];
}


static inline uint8_t t1_getctxno_mag(uint32_t f)
{
	return (f & T1_MU_CURRENT) ? (T1_CTXNO_MAG + 2) : ((f & T1_SIGMA_NEIGHBOURS) ? T1_CTXNO_MAG + 1 : T1_CTXNO_MAG);
}

static uint8_t t1_getspb(uint32_t fX, uint32_t pfX, uint32_t nfX, uint32_t ci3)
{
	/*
	0 pfX T1_CHI_CURRENT           T1_LUT_SGN_W
	1 tfX T1_SIGMA_1            T1_LUT_SIG_N
	2 nfX T1_CHI_CURRENT           T1_LUT_SGN_E
	3 tfX T1_SIGMA_3            T1_LUT_SIG_W
	4  fX T1_CHI_(THIS - 1)     T1_LUT_SGN_N
	5 tfX T1_SIGMA_5            T1_LUT_SIG_E
	6  fX T1_CHI_(THIS + 1)     T1_LUT_SGN_S
	7 tfX T1_SIGMA_7            T1_LUT_SIG_S
	*/

	uint32_t lu = (fX >> ci3) & (T1_SIGMA_1 | T1_SIGMA_3 | T1_SIGMA_5 | T1_SIGMA_7);

	lu |= (pfX >> (T1_CHI_CURRENT_I + ci3)) & (1U << 0);
	lu |= (nfX >> (T1_CHI_CURRENT_I - 2U + ci3)) & (1U << 2);
	if (ci3 == 0U) {
		lu |= (fX >> (T1_CHI_0_I - 4U)) & (1U << 4);
	}
	else {
		lu |= (fX >> (T1_CHI_1_I - 4U + (ci3 - 3))) & (1U << 4);
	}
	lu |= (fX >> (T1_CHI_2_I - 6U + ci3)) & (1U << 6);

	return lut_spb_opt[lu];
}

#define t1_update_flags_macro(flagsp, ci3, s, stride, vsc) \
{ \
	/* update current flag */  \
    flagsp[-1] |= T1_SIGMA_5 << (ci3); \
    *flagsp |= (((s) << T1_CHI_1_I) | T1_SIGMA_4) << (ci3); \
    flagsp[1] |= T1_SIGMA_3 << (ci3); \
	/* update north flag if we are at top of column and VSC is false */ \
    if ( ((ci3) == 0U) & ((vsc)==0U)) { \
        flag_opt_t* north = flagsp - (stride); \
        *north |= ((s) << T1_CHI_5_I) | T1_SIGMA_16; \
        north[-1] |= T1_SIGMA_17; \
        north[1] |= T1_SIGMA_15; \
    } \
	/* update south flag*/  \
    if (ci3 == 9u) { \
        flag_opt_t* south = (flagsp) + (stride); \
        *south |= ((s) << T1_CHI_0_I) | T1_SIGMA_1; \
        south[-1] |= T1_SIGMA_2; \
        south[1] |= T1_SIGMA_0; \
    } \
}


static void t1_updateflags(flag_opt_t *flagsp, uint32_t ci3, uint32_t s, uint32_t stride, uint8_t vsc)
{
	t1_update_flags_macro(flagsp, ci3, s, stride, vsc);
}

static void  t1_enc_sigpass_step(t1_opt_t *t1,
	flag_opt_t *flagsp,
	uint32_t *datap,
	uint8_t orient,
	int32_t bpno,
	int32_t one,
	int32_t *nmsedec, 
	uint8_t type, 
	uint32_t cblksty)
{
	uint32_t v;
	mqc_t *mqc = t1->mqc;
	if (*flagsp == 0U) {
		return;  /* Nothing to do for any of the 4 data points */
	}
	for (uint32_t ci3 = 0U; ci3 < 12U; ci3 += 3) {
		uint32_t const shift_flags = *flagsp >> ci3;
		/* if location is not significant, has not been coded in significance pass, and is in preferred neighbourhood,
		then code in this pass: */
		if ((shift_flags & (T1_SIGMA_CURRENT | T1_PI_CURRENT)) == 0U && (shift_flags & T1_SIGMA_NEIGHBOURS) != 0U) {
			auto dataPoint = *datap;
			v = (dataPoint >> one) & 1;
			mqc_setcurctx(mqc, t1_getctxno_zc(shift_flags, orient));
			if (type == T1_TYPE_RAW) {
				mqc_bypass_enc(mqc, v);
			}
			else {
				mqc_encode(mqc, v);
			}
			if (v) {
				/* sign bit */
				v = dataPoint >> T1_DATA_SIGN_BIT_INDEX;
				if (nmsedec)
					*nmsedec += t1_getnmsedec_sig(dataPoint, (uint32_t)bpno);
				mqc_setcurctx(mqc, t1_getctxno_sc(*flagsp, flagsp[-1], flagsp[1], ci3));
				if (type == T1_TYPE_RAW) {	
					mqc_bypass_enc(mqc, v);
				}
				else {
					mqc_encode(mqc, v ^ t1_getspb(*flagsp, flagsp[-1], flagsp[1], ci3));
				}
				t1_updateflags(flagsp, ci3, v, t1->flags_stride, (ci3==0) && (cblksty & J2K_CCP_CBLKSTY_VSC));
			}
			/* set propagation pass bit for this location */
			*flagsp |= T1_PI_CURRENT << ci3;
		}
		datap += t1->w;
	}
}

static void t1_enc_sigpass(t1_opt_t *t1,
	int32_t bpno,
	uint8_t orient,
	int32_t *nmsedec, 
	uint8_t type,
	uint32_t cblksty)
{
	uint32_t i, k;
	int32_t const one = (bpno + T1_NMSEDEC_FRACBITS);
	uint32_t const flag_row_extra = t1->flags_stride - t1->w;
	uint32_t const data_row_extra = (t1->w << 2) - t1->w;

	flag_opt_t* f = ENC_FLAGS_ADDRESS(0, 0);
	uint32_t* d = t1->data;

	if (nmsedec)
		*nmsedec = 0;
	for (k = 0; k < t1->h; k += 4) {
		for (i = 0; i < t1->w; ++i) {
			t1_enc_sigpass_step(
				t1,
				f,
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

static void t1_enc_refpass_step(t1_opt_t *t1,
	flag_opt_t *flagsp,
	uint32_t *datap,
	int32_t bpno,
	int32_t one,
	int32_t *nmsedec, 
	uint8_t type)
{
	uint32_t v;
	mqc_t *mqc = t1->mqc;

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
				*nmsedec += t1_getnmsedec_ref(*datap, (uint32_t)bpno);
			v = (*datap >> one) & 1;
			mqc_setcurctx(mqc, t1_getctxno_mag(shift_flags));
			if (type == T1_TYPE_RAW) {	
				mqc_bypass_enc(mqc, v);
			}
			else {
				mqc_encode(mqc, v);
			}
			/* flip magnitude refinement bit*/
			*flagsp |= T1_MU_CURRENT << ci3;
		}
		datap += t1->w;
	}
}

static void t1_enc_refpass(
	t1_opt_t *t1,
	int32_t bpno,
	int32_t *nmsedec, 
	uint8_t type)
{
	uint32_t i, k;
	const int32_t one = (bpno + T1_NMSEDEC_FRACBITS);
	flag_opt_t* f = ENC_FLAGS_ADDRESS(0, 0);
	uint32_t const flag_row_extra = t1->flags_stride - t1->w;
	uint32_t const data_row_extra = (t1->w << 2) - t1->w;
	uint32_t* d = t1->data;

	if (nmsedec)
		*nmsedec = 0;
	for (k = 0U; k < t1->h; k += 4U) {
		for (i = 0U; i < t1->w; ++i) {
			t1_enc_refpass_step(
				t1,
				f,
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

static void t1_enc_clnpass_step(t1_opt_t *t1,
	flag_opt_t *flagsp,
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
	uint32_t v;
	mqc_t *mqc = t1->mqc;

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
	lim = 4U < (t1->h - y) ? 12U : 3 * (t1->h - y);
	for (uint32_t ci3 = runlen; ci3 < lim; ci3 += 3) {
		flag_opt_t shift_flags;
		if ((agg != 0) && (ci3 == runlen)) {
			goto LABEL_PARTIAL;
		}

		shift_flags = *flagsp >> ci3;

		if (!(shift_flags & (T1_SIGMA_CURRENT | T1_PI_CURRENT))) {
			mqc_setcurctx(mqc, t1_getctxno_zc(shift_flags, orient));
			v = (*datap >> one) & 1;
			mqc_encode(mqc, v);
			if (v) {
			LABEL_PARTIAL:
				if (nmsedec)
					*nmsedec += t1_getnmsedec_sig(*datap, (uint32_t)bpno);
				mqc_setcurctx(mqc, t1_getctxno_sc(*flagsp, flagsp[-1], flagsp[1], ci3));
				/* sign bit */
				v = *datap >> T1_DATA_SIGN_BIT_INDEX;
				mqc_encode(mqc, v ^ t1_getspb(*flagsp, flagsp[-1], flagsp[1], ci3));
				t1_updateflags(flagsp, ci3, v, t1->flags_stride, (cblksty & J2K_CCP_CBLKSTY_VSC) && (ci3 == 0));
			}
		}
		*flagsp &= ~(T1_PI_0 << ci3);
		datap += t1->w;
	}
}


static void t1_enc_clnpass(t1_opt_t *t1,
	int32_t bpno,
	uint8_t orient,
	int32_t *nmsedec, 
	uint32_t cblksty)
{
	uint32_t i, k;
	const int32_t one = (bpno + T1_NMSEDEC_FRACBITS);
	uint32_t agg, runlen;

	mqc_t *mqc = t1->mqc;

	if (nmsedec)
		*nmsedec = 0;

	for (k = 0; k < t1->h; k += 4) {
		for (i = 0; i < t1->w; ++i) {
			agg = !ENC_FLAGS(i, k);
			if (agg) {
				for (runlen = 0; runlen < 4; ++runlen) {
					if ((t1->data[((k + runlen)*t1->w) + i] >> one) & 1)
						break;
				}
				mqc_setcurctx(mqc, T1_CTXNO_AGG);
				mqc_encode(mqc, runlen != 4);
				if (runlen == 4) {
					continue;
				}
				mqc_setcurctx(mqc, T1_CTXNO_UNI);
				mqc_encode(mqc, runlen >> 1);
				mqc_encode(mqc, runlen & 1);
			}
			else {
				runlen = 0;
			}
			t1_enc_clnpass_step(
				t1,
				ENC_FLAGS_ADDRESS(i, k),
				t1->data + ((k + runlen) * t1->w) + i,
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


bool t1_opt_allocate_buffers(t1_opt_t *t1,
	uint32_t cblkw,
	uint32_t cblkh)
{
	if (!t1->data) {
		t1->data = (uint32_t*)grok_aligned_malloc(cblkw*cblkh * sizeof(int32_t));
		if (!t1->data) {
			/* FIXME event manager error callback */
			return false;
		}
	}
	if (!t1->flags) {
		auto flags_stride = cblkw + 2;
		auto flags_height = (cblkh + 3U) >> 2;
		auto flagssize = flags_stride * (flags_height + 2);
		t1->flags = (flag_opt_t*)grok_aligned_malloc(flagssize * sizeof(flag_opt_t));
		if (!t1->flags) {
			/* FIXME event manager error callback */
			return false;
		}
	}
	return true;
}

void t1_opt_init_buffers(t1_opt_t *t1,
						uint32_t w,
						uint32_t h)	{
	uint32_t x;
	flag_opt_t* p;
	if (t1->data)
		memset(t1->data, 0, w*h * sizeof(int32_t));

	t1->flags_stride = w + 2;
	auto flags_height = (h + 3U) >> 2;
	auto flagssize = t1->flags_stride * (flags_height + 2);
	memset(t1->flags, 0, flagssize * sizeof(flag_opt_t)); /* Shall we keep memset for encoder ? */

	/* BIG FAT XXX */
	p = &t1->flags[0];
	for (x = 0; x < t1->flags_stride; ++x) {
		/* magic value to hopefully stop any passes being interested in this entry */
		*p++ = (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);
	}

	p = &t1->flags[((flags_height + 1) * t1->flags_stride)];
	for (x = 0; x < t1->flags_stride; ++x) {
		/* magic value to hopefully stop any passes being interested in this entry */
		*p++ = (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);
	}

	unsigned char hMod4 = h & 3;
	if (hMod4) {
		uint32_t v = 0;
		p = &t1->flags[((flags_height)* t1->flags_stride)];
		if (hMod4 == 1) {
			v |= T1_PI_1 | T1_PI_2 | T1_PI_3;
		}
		else if (hMod4 == 2) {
			v |= T1_PI_2 | T1_PI_3;
		}
		else if (hMod4 == 3) {
			v |= T1_PI_3;
		}
		for (x = 0; x < t1->flags_stride; ++x) {
			*p++ = v;
		}
	}

	t1->w = w;
	t1->h = h;
}

double t1_opt_encode_cblk(t1_opt_t *t1,
	tcd_cblk_enc_t* cblk,
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

	mqc_t *mqc = t1->mqc;

	uint32_t passno;
	int32_t bpno;
	uint32_t passtype;
	int32_t nmsedec = 0;
	int32_t* msePtr = doRateControl ? &nmsedec : nullptr;
	double tempwmsedec=0;

	auto logMax = int_floorlog2((int32_t)max) + 1;
	cblk->numbps = (max && (logMax > T1_NMSEDEC_FRACBITS)) ? (uint32_t)(logMax - T1_NMSEDEC_FRACBITS) : 0;
	if (!cblk->numbps)
		return 0;

	bpno = (int32_t)(cblk->numbps - 1);
	passtype = 2;
	mqc_init_enc(mqc, cblk->data);
	uint32_t state = grok_plugin_get_debug_state();
	if (state & OPJ_PLUGIN_STATE_DEBUG) {
		mqc->debug_mqc.contextStream = cblk->contextStream;
	}

	bool TERMALL = (cblksty & J2K_CCP_CBLKSTY_TERMALL) ? true : false;
	bool LAZY = (cblksty & J2K_CCP_CBLKSTY_LAZY);

	for (passno = 0; bpno >= 0; ++passno) {
		tcd_pass_t *pass = &cblk->passes[passno];
		uint8_t type = T1_TYPE_MQ;
		if (LAZY && (bpno < ((int32_t)(cblk->numbps) - 4)) && (passtype < 2))
			type = T1_TYPE_RAW;

		switch (passtype) {
		case 0:
			t1_enc_sigpass(t1, bpno, orient, msePtr,type, cblksty);
			break;
		case 1:
			t1_enc_refpass(t1, bpno, msePtr,type);
			break;
		case 2:
			t1_enc_clnpass(t1, bpno, orient, msePtr,cblksty);
			/* code switch SEGMARK (i.e. SEGSYM) */
			if (cblksty & J2K_CCP_CBLKSTY_SEGSYM)
				mqc_segmark_enc(mqc);
			if (state & OPJ_PLUGIN_STATE_DEBUG) {
				mqc_next_plane(&mqc->debug_mqc);
			}
			break;
		}

		if (doRateControl) {
			tempwmsedec = t1_getwmsedec(nmsedec, compno, level, orient, bpno, qmfbid, stepsize, numcomps, mct_norms, mct_numcomps);
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
			if (LAZY && (bpno < ((int32_t)cblk->numbps - 4)) ) {
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


}
