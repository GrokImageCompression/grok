/*
*    Copyright (C) 2016 Grok Image Compression Inc.
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
*    Copyright (C) 2016 Grok Image Compression Inc.
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

#include "opj_includes.h"
#include "t1_luts.h"

#ifdef _OPENMP
#include <omp.h>
#endif


/** @defgroup T1 T1 - Implementation of the tier-1 coding */
/*@{*/

/** @name Local static functions */
/*@{*/


typedef uint32_t opj_flag_opt_t;

/**
Tier-1 coding (coding of code-block coefficients)
*/
typedef struct opj_t1_opt {
    opj_mqc_t *mqc;
    uint32_t  *data;
    opj_flag_opt_t *flags;
    uint32_t w;
    uint32_t h;
    uint32_t datasize;
    uint32_t flagssize;
    uint32_t flags_stride;
    bool   encoder;
} opj_t1_opt_t;




#define ENC_FLAGS(x, y) (t1->flags[(x) + 1 + (((y) >> 2) + 1) * t1->flags_stride])
#define ENC_FLAGS_ADDRESS(x, y) (t1->flags + ((x) + 1 + (((y) >> 2) + 1) * t1->flags_stride))

static inline uint8_t		opj_t1_getctxno_zc_opt(uint32_t f, uint32_t orient);
static inline uint8_t		opj_t1_getctxno_sc_opt(uint32_t fX, uint32_t pfX, uint32_t nfX, uint32_t ci);
static inline uint32_t	opj_t1_getctxno_mag_opt(uint32_t f);
static inline uint8_t		opj_t1_getspb_opt(uint32_t fX, uint32_t pfX, uint32_t nfX, uint32_t ci);
static inline void			opj_t1_updateflags_opt(opj_flag_opt_t *flagsp, uint32_t ci, uint32_t s, uint32_t stride);

/**
Encode significant pass
*/
static void opj_t1_enc_sigpass_step(opj_t1_opt_t *t1,
                                    opj_flag_opt_t *flagsp,
                                    uint32_t *datap,
                                    uint32_t orient,
                                    int32_t bpno,
                                    int32_t one,
                                    int32_t *nmsedec);

/**
Encode significant pass
*/
static void opj_t1_enc_sigpass( opj_t1_opt_t *t1,
                                int32_t bpno,
                                uint32_t orient,
                                int32_t *nmsedec);


/**
Encode refinement pass
*/
static void opj_t1_enc_refpass_step(opj_t1_opt_t *t1,
                                    opj_flag_opt_t *flagsp,
                                    uint32_t *datap,
                                    int32_t bpno,
                                    int32_t one,
                                    int32_t *nmsedec);


/**
Encode refinement pass
*/
static void opj_t1_enc_refpass( opj_t1_opt_t *t1,
                                int32_t bpno,
                                int32_t *nmsedec);



/**
Encode clean-up pass
*/
static void opj_t1_enc_clnpass_step(
    opj_t1_opt_t *t1,
    opj_flag_opt_t *flagsp,
    uint32_t *datap,
    uint32_t orient,
    int32_t bpno,
    int32_t one,
    int32_t *nmsedec,
    uint32_t agg,
    uint32_t runlen,
    uint32_t y);

/**
Encode clean-up pass
*/
static void opj_t1_enc_clnpass(
    opj_t1_opt_t *t1,
    int32_t bpno,
    uint32_t orient,
    int32_t *nmsedec);


static void opj_t1_encode_cblk(opj_t1_opt_t *t1,
                               opj_tcd_cblk_enc_t* cblk,
                               uint32_t orient,
                               uint32_t compno,
                               uint32_t level,
                               uint32_t qmfbid,
                               double stepsize,
                               uint32_t numcomps,
                               opj_tcd_tile_t * tile,
                               const double * mct_norms,
                               uint32_t mct_numcomps,
                               uint32_t max);


static bool opj_t1_allocate_buffers(   opj_t1_opt_t *t1,
                                       uint32_t w,
                                       uint32_t h);



/**
* Creates a new Tier 1 handle
* and initializes the look-up tables of the Tier-1 coder/decoder
* @return a new T1 handle if successful, returns NULL otherwise
*/
static opj_t1_opt_t* opj_t1_opt_create(bool isEncoder);

/**
* Destroys a previously created T1 handle
*
* @param p_t1 Tier 1 handle to destroy
*/
static void opj_t1_destroy(opj_t1_opt_t *p_t1);



/**
* Creates a new Tier 1 handle
* and initializes the look-up tables of the Tier-1 coder/decoder
* @return a new T1 handle if successful, returns NULL otherwise
*/
static opj_t1_opt_t* opj_t1_opt_create(bool isEncoder)
{
    opj_t1_opt_t *l_t1 = 00;

    l_t1 = (opj_t1_opt_t*)opj_calloc(1, sizeof(opj_t1_opt_t));
    if (!l_t1) {
        return 00;
    }

    /* create MQC handles */
    l_t1->mqc = opj_mqc_create();
    if (!l_t1->mqc) {
        opj_t1_destroy(l_t1);
        return 00;
    }

    l_t1->encoder = isEncoder;

    return l_t1;
}


/**
* Destroys a previously created T1 handle
*
* @param p_t1 Tier 1 handle to destroy
*/
static void opj_t1_destroy(opj_t1_opt_t *p_t1)
{
    if (!p_t1) {
        return;
    }

    /* destroy MQC handles */
    opj_mqc_destroy(p_t1->mqc);
    p_t1->mqc = 00;

    /* encoder uses tile buffer, so no need to free */
    if (p_t1->data) {
        opj_aligned_free(p_t1->data);
        p_t1->data = 00;
    }

    if (p_t1->flags) {
        opj_aligned_free(p_t1->flags);
        p_t1->flags = 00;
    }

    opj_free(p_t1);
}

static inline uint8_t opj_t1_getctxno_zc_opt(uint32_t f, uint32_t orient)
{
    return lut_ctxno_zc_opt[(orient << 9) | (f & T1_SIGMA_NEIGHBOURS)];
}


static uint8_t opj_t1_getctxno_sc_opt(uint32_t fX, uint32_t pfX, uint32_t nfX, uint32_t ci)
{
    /*
    0 pfX T1_CHI_THIS           T1_LUT_CTXNO_SGN_W
    1 tfX T1_SIGMA_1            T1_LUT_CTXNO_SIG_N
    2 nfX T1_CHI_THIS           T1_LUT_CTXNO_SGN_E
    3 tfX T1_SIGMA_3            T1_LUT_CTXNO_SIG_W
    4  fX T1_CHI_(THIS - 1)     T1_LUT_CTXNO_SGN_N
    5 tfX T1_SIGMA_5            T1_LUT_CTXNO_SIG_E
    6  fX T1_CHI_(THIS + 1)     T1_LUT_CTXNO_SGN_S
    7 tfX T1_SIGMA_7            T1_LUT_CTXNO_SIG_S
    */

    uint32_t lu = (fX >> (ci * 3)) & (T1_SIGMA_1 | T1_SIGMA_3 | T1_SIGMA_5 | T1_SIGMA_7);

    lu |= (pfX >> (T1_CHI_THIS_I + (ci * 3U))) & (1U << 0);
    lu |= (nfX >> (T1_CHI_THIS_I - 2U + (ci * 3U))) & (1U << 2);
    if (ci == 0U) {
        lu |= (fX >> (T1_CHI_0_I - 4U)) & (1U << 4);
    } else {
        lu |= (fX >> (T1_CHI_1_I - 4U + ((ci - 1U) * 3U))) & (1U << 4);
    }
    lu |= (fX >> (T1_CHI_2_I - 6U + (ci * 3U))) & (1U << 6);

    return lut_ctxno_sc_opt[lu];
}


static inline uint32_t opj_t1_getctxno_mag_opt(uint32_t f)
{
    return (f & T1_MU_THIS) ? (T1_CTXNO_MAG + 2) : ((f & T1_SIGMA_NEIGHBOURS) ? T1_CTXNO_MAG + 1 : T1_CTXNO_MAG);
}

static uint8_t opj_t1_getspb_opt(uint32_t fX, uint32_t pfX, uint32_t nfX, uint32_t ci)
{
    /*
    0 pfX T1_CHI_THIS           T1_LUT_SGN_W
    1 tfX T1_SIGMA_1            T1_LUT_SIG_N
    2 nfX T1_CHI_THIS           T1_LUT_SGN_E
    3 tfX T1_SIGMA_3            T1_LUT_SIG_W
    4  fX T1_CHI_(THIS - 1)     T1_LUT_SGN_N
    5 tfX T1_SIGMA_5            T1_LUT_SIG_E
    6  fX T1_CHI_(THIS + 1)     T1_LUT_SGN_S
    7 tfX T1_SIGMA_7            T1_LUT_SIG_S
    */

    uint32_t lu = (fX >> (ci * 3U)) & (T1_SIGMA_1 | T1_SIGMA_3 | T1_SIGMA_5 | T1_SIGMA_7);

    lu |= (pfX >> (T1_CHI_THIS_I + (ci * 3U))) & (1U << 0);
    lu |= (nfX >> (T1_CHI_THIS_I - 2U + (ci * 3U))) & (1U << 2);
    if (ci == 0U) {
        lu |= (fX >> (T1_CHI_0_I - 4U)) & (1U << 4);
    } else {
        lu |= (fX >> (T1_CHI_1_I - 4U + ((ci - 1U) * 3U))) & (1U << 4);
    }
    lu |= (fX >> (T1_CHI_2_I - 6U + (ci * 3U))) & (1U << 6);

    return lut_spb_opt[lu];
}

static void opj_t1_updateflags_opt(opj_flag_opt_t *flagsp, uint32_t ci, uint32_t s, uint32_t stride)
{
    /* set up to point to the north and south data points' flags words, if required */
    opj_flag_opt_t* north = NULL;
    opj_flag_opt_t* south = NULL;

    /* mark target as significant */
    *flagsp |= T1_SIGMA_THIS << (3U * ci);

    /* north-west, north, north-east */
    if (ci == 0U) {
        north = flagsp - stride;
        *north |= T1_SIGMA_16;
        north[-1] |= T1_SIGMA_17;
        north[1] |= T1_SIGMA_15;
    }

    /* south-west, south, south-east */
    if (ci == 3U) {
        south = flagsp + stride;
        *south |= T1_SIGMA_1;
        south[-1] |= T1_SIGMA_2;
        south[1] |= T1_SIGMA_0;
    }

    /* east */
    flagsp[-1] |= T1_SIGMA_5 << (3U * ci);

    /* west */
    flagsp[1] |= T1_SIGMA_3 << (3U * ci);

    if (s) {
        switch (ci) {
        case 0U: {
            *flagsp |= T1_CHI_1;
            *north |= T1_CHI_5;
            break;
        }
        case 1:
            *flagsp |= T1_CHI_2;
            break;
        case 2:
            *flagsp |= T1_CHI_3;
            break;
        case 3: {
            *flagsp |= T1_CHI_4;
            *south |= T1_CHI_0;
            break;
        }

        }
    }
}

static void  opj_t1_enc_sigpass_step(   opj_t1_opt_t *t1,
                                        opj_flag_opt_t *flagsp,
                                        uint32_t *datap,
                                        uint32_t orient,
                                        int32_t bpno,
                                        int32_t one,
                                        int32_t *nmsedec)
{
    uint32_t v;
    uint32_t ci;

    opj_mqc_t *mqc = t1->mqc;
    if (*flagsp == 0U) {
        return;  /* Nothing to do for any of the 4 data points */
    }
    for (ci = 0U; ci < 4U; ++ci) {
        uint32_t const shift_flags = *flagsp >> (ci * 3U);
        /* if location is not significant, has not been coded in significance pass, and is in preferred neighbourhood,
        then code in this pass: */
        if ((shift_flags & (T1_SIGMA_THIS | T1_PI_THIS)) == 0U && (shift_flags & T1_SIGMA_NEIGHBOURS) != 0U) {
            v = (*datap >> one) & 1;
            opj_mqc_setcurctx(mqc, opj_t1_getctxno_zc_opt(shift_flags, orient));
            opj_mqc_encode(mqc, v);
            if (v) {
                /* sign bit */
                v = *datap >> T1_DATA_SIGN_BIT_INDEX;
                *nmsedec += opj_t1_getnmsedec_sig(*datap, (uint32_t)bpno);
                opj_mqc_setcurctx(mqc, opj_t1_getctxno_sc_opt(*flagsp, flagsp[-1], flagsp[1], ci));
                opj_mqc_encode(mqc, v ^ opj_t1_getspb_opt(*flagsp, flagsp[-1], flagsp[1], ci));
                opj_t1_updateflags_opt(flagsp, ci, v, t1->flags_stride);
            }
            /* set propogation pass bit for this location */
            *flagsp |= T1_PI_THIS << (ci * 3U);
        }
        datap += t1->w;
    }
}



static void opj_t1_enc_sigpass(opj_t1_opt_t *t1,
                               int32_t bpno,
                               uint32_t orient,
                               int32_t *nmsedec  )
{
    uint32_t i, k;
    int32_t const one =  (bpno + T1_NMSEDEC_FRACBITS);
    uint32_t const flag_row_extra = t1->flags_stride - t1->w;
    uint32_t const data_row_extra =  (t1->w << 2) - t1->w;

    opj_flag_opt_t* f = ENC_FLAGS_ADDRESS(0, 0);
    uint32_t* d = t1->data;

    *nmsedec = 0;
    for (k = 0; k < t1->h; k += 4) {
        for (i = 0; i < t1->w; ++i) {
            opj_t1_enc_sigpass_step(
                t1,
                f,
                d,
                orient,
                bpno,
                one,
                nmsedec);

            ++f;
            ++d;
        }
        d += data_row_extra;
        f += flag_row_extra;
    }
}

static void opj_t1_enc_refpass_step(   opj_t1_opt_t *t1,
                                       opj_flag_opt_t *flagsp,
                                       uint32_t *datap,
                                       int32_t bpno,
                                       int32_t one,
                                       int32_t *nmsedec)
{
    uint32_t v;
    uint32_t ci;

    opj_mqc_t *mqc = t1->mqc;

    if ((*flagsp & (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13)) == 0) {
        /* none significant */
        return;
    }
    if ((*flagsp & (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3)) == (T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3)) {
        /* all processed by sigpass */
        return;
    }

    for (ci = 0U; ci < 4U; ++ci) {
        uint32_t shift_flags = *flagsp >> (ci * 3U);
        /* if location is significant, but has not been coded in significance propagation pass, then code in this pass: */
        if ((shift_flags & (T1_SIGMA_THIS | T1_PI_THIS)) == T1_SIGMA_THIS) {
            *nmsedec += opj_t1_getnmsedec_ref(*datap, (uint32_t)bpno);
            v = (*datap >> one) & 1;
            opj_mqc_setcurctx(mqc, opj_t1_getctxno_mag_opt(shift_flags));
            opj_mqc_encode(mqc, v);
            /* flip magnitude refinement bit*/
            *flagsp |= T1_MU_THIS << (ci * 3U);
        }
        datap += t1->w;
    }
}

static void opj_t1_enc_refpass(
    opj_t1_opt_t *t1,
    int32_t bpno,
    int32_t *nmsedec)
{
    uint32_t i, k;
    const int32_t one =  (bpno + T1_NMSEDEC_FRACBITS);
    opj_flag_opt_t* f = ENC_FLAGS_ADDRESS(0, 0);
    uint32_t const flag_row_extra = t1->flags_stride - t1->w;
    uint32_t const data_row_extra = (t1->w << 2) - t1->w;
    uint32_t* d = t1->data;

    *nmsedec = 0;
    for (k = 0U; k < t1->h; k += 4U) {
        for (i = 0U; i < t1->w; ++i) {
            opj_t1_enc_refpass_step(
                t1,
                f,
                d,
                bpno,
                one,
                nmsedec);
            ++f;
            ++d;
        }
        f += flag_row_extra;
        d += data_row_extra;
    }
}

static void opj_t1_enc_clnpass_step(
    opj_t1_opt_t *t1,
    opj_flag_opt_t *flagsp,
    uint32_t *datap,
    uint32_t orient,
    int32_t bpno,
    int32_t one,
    int32_t *nmsedec,
    uint32_t agg,
    uint32_t runlen,
    uint32_t y)
{
    uint32_t v;
    uint32_t ci;
    opj_mqc_t *mqc = t1->mqc;

    uint32_t lim;
    const uint32_t check = (T1_SIGMA_4 | T1_SIGMA_7 | T1_SIGMA_10 | T1_SIGMA_13 | T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);

    if ((*flagsp & check) == check) {
        if (runlen == 0) {
            *flagsp &= ~(T1_PI_0 | T1_PI_1 | T1_PI_2 | T1_PI_3);
        } else if (runlen == 1) {
            *flagsp &= ~(T1_PI_1 | T1_PI_2 | T1_PI_3);
        } else if (runlen == 2) {
            *flagsp &= ~(T1_PI_2 | T1_PI_3);
        } else if (runlen == 3) {
            *flagsp &= ~(T1_PI_3);
        }
        return;
    }

    lim = 4U < (t1->h - y) ? 4U : (t1->h - y);
    for (ci = runlen; ci < lim; ++ci) {
        opj_flag_opt_t shift_flags;
        if ((agg != 0) && (ci == runlen)) {
            goto LABEL_PARTIAL;
        }

        shift_flags = *flagsp >> (ci * 3U);

        if (!(shift_flags & (T1_SIGMA_THIS | T1_PI_THIS))) {
            opj_mqc_setcurctx(mqc, opj_t1_getctxno_zc_opt(shift_flags, orient));
            v = (*datap >> one) & 1;
            opj_mqc_encode(mqc, v);
            if (v) {
LABEL_PARTIAL:
                *nmsedec += opj_t1_getnmsedec_sig(*datap, (uint32_t)bpno);
                opj_mqc_setcurctx(mqc, opj_t1_getctxno_sc_opt(*flagsp, flagsp[-1], flagsp[1], ci));
                /* sign bit */
                v = *datap >> T1_DATA_SIGN_BIT_INDEX;
                opj_mqc_encode(mqc, v ^ opj_t1_getspb_opt(*flagsp, flagsp[-1], flagsp[1], ci));
                opj_t1_updateflags_opt(flagsp, ci, v, t1->flags_stride);
            }
        }
        *flagsp &= ~(T1_PI_0 << (3U * ci));
        datap += t1->w;
    }
}


static void opj_t1_enc_clnpass(
    opj_t1_opt_t *t1,
    int32_t bpno,
    uint32_t orient,
    int32_t *nmsedec)
{
    uint32_t i, k;
    const int32_t one = (bpno + T1_NMSEDEC_FRACBITS);
    uint32_t agg, runlen;

    opj_mqc_t *mqc = t1->mqc;

    *nmsedec = 0;

    for (k = 0; k < t1->h; k += 4) {
        for (i = 0; i < t1->w; ++i) {
            agg = !ENC_FLAGS(i, k);
            if (agg) {
                for (runlen = 0; runlen < 4; ++runlen) {
                    if ( (t1->data[((k + runlen)*t1->w) + i] >> one) & 1)
                        break;
                }
                opj_mqc_setcurctx(mqc, T1_CTXNO_AGG);
                opj_mqc_encode(mqc, runlen != 4);
                if (runlen == 4) {
                    continue;
                }
                opj_mqc_setcurctx(mqc, T1_CTXNO_UNI);
                opj_mqc_encode(mqc, runlen >> 1);
                opj_mqc_encode(mqc, runlen & 1);
            } else {
                runlen = 0;
            }
            opj_t1_enc_clnpass_step(
                t1,
                ENC_FLAGS_ADDRESS(i, k),
                t1->data + ((k + runlen) * t1->w) + i,
                orient,
                bpno,
                one,
                nmsedec,
                agg,
                runlen,
                k);
        }
    }
}


static bool opj_t1_allocate_buffers(
    opj_t1_opt_t *t1,
    uint32_t w,
    uint32_t h)
{
    uint32_t datasize = w * h;
    uint32_t flagssize;
    uint32_t x;
    opj_flag_opt_t* p;
    uint32_t flags_height;

    if (datasize > t1->datasize) {
        opj_aligned_free(t1->data);
        t1->data = (uint32_t*)opj_aligned_malloc(datasize * sizeof(int32_t));
        if (!t1->data) {
            /* FIXME event manager error callback */
            return false;
        }
        t1->datasize = datasize;
    }
    memset(t1->data, 0, datasize * sizeof(int32_t));


    t1->flags_stride = w + 2;
    flags_height = (h + 3U) / 4U;
    flagssize = t1->flags_stride * (flags_height + 2);
    if (flagssize > t1->flagssize) {
        opj_aligned_free(t1->flags);
        t1->flags = (opj_flag_opt_t*)opj_aligned_malloc(flagssize * sizeof(opj_flag_opt_t));
        if (!t1->flags) {
            /* FIXME event manager error callback */
            return false;
        }
        t1->flagssize = flagssize;
    }
    memset(t1->flags, 0, flagssize * sizeof(opj_flag_opt_t)); /* Shall we keep memset for encoder ? */


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

    if (h % 4) {
        uint32_t v = 0;
        p = &t1->flags[((flags_height)* t1->flags_stride)];
        if (h % 4 == 1) {
            v |= T1_PI_1 | T1_PI_2 | T1_PI_3;
        } else if (h % 4 == 2) {
            v |= T1_PI_2 | T1_PI_3;
        } else if (h % 4 == 3) {
            v |= T1_PI_3;
        }
        for (x = 0; x < t1->flags_stride; ++x) {
            *p++ = v;
        }
    }


    t1->w = w;
    t1->h = h;

    return true;
}

/* ----------------------------------------------------------------------- */

/* ----------------------------------------------------------------------- */

bool opj_t1_opt_encode_cblks(   opj_tcd_tile_t *tile,
                                opj_tcp_t *tcp,
                                const double * mct_norms,
                                uint32_t mct_numcomps )
{
    uint32_t compno, resno, bandno, precno;
    bool rc = true;
    tile->distotile = 0;		/* fixed_quality */
    for (compno = 0; compno < tile->numcomps; ++compno) {
        opj_tcd_tilecomp_t* tilec = &tile->comps[compno];
        opj_tccp_t* tccp = &tcp->tccps[compno];
        uint32_t tile_w = (uint32_t)(tilec->x1 - tilec->x0);

        for (resno = 0; resno < tilec->numresolutions; ++resno) {
            opj_tcd_resolution_t *res = &tilec->resolutions[resno];

            for (bandno = 0; bandno < res->numbands; ++bandno) {
                opj_tcd_band_t* restrict band = &res->bands[bandno];
                int32_t bandconst = 8192 * 8192 / ((int32_t) floor(band->stepsize * 8192));

                for (precno = 0; precno < res->pw * res->ph; ++precno) {
                    opj_tcd_precinct_t *prc = &band->precincts[precno];
                    int32_t cblkno;
                    int32_t bandOdd = band->bandno & 1;
                    int32_t bandModTwo = band->bandno & 2;

#ifdef _OPENMP
                    #pragma omp parallel default(none) private(cblkno) shared(band, bandOdd, bandModTwo, prc, tilec, tccp, mct_norms, mct_numcomps, bandconst,compno, tile, tile_w, resno, rc)
                    {

                        #pragma omp for
#endif
                        for (cblkno = 0; cblkno < (int32_t)(prc->cw * prc->ch); ++cblkno) {
                            int32_t* restrict tiledp;
                            opj_tcd_cblk_enc_t* cblk = prc->cblks.enc + cblkno;
                            uint32_t cblk_w;
                            uint32_t cblk_h;
                            uint32_t i, j, tileIndex=0, tileLineAdvance;
                            uint32_t cblk_index = 0;
                            opj_t1_opt_t * t1 = 00;
                            int32_t x = cblk->x0 - band->x0;
                            int32_t y = cblk->y0 - band->y0;
                            uint32_t max=0;

                            if (bandOdd) {
                                opj_tcd_resolution_t *pres = &tilec->resolutions[resno - 1];
                                x += pres->x1 - pres->x0;
                            }
                            if (bandModTwo) {
                                opj_tcd_resolution_t *pres = &tilec->resolutions[resno - 1];
                                y += pres->y1 - pres->y0;
                            }
                            t1 = opj_t1_opt_create(true);
                            if (!t1) {
                                rc = false;
                                continue;
                            }
                            if(!opj_t1_allocate_buffers(
                                        t1,
                                        (uint32_t)(cblk->x1 - cblk->x0),
                                        (uint32_t)(cblk->y1 - cblk->y0))) {
                                opj_t1_destroy(t1);
                                rc = false;
                                continue;
                            }
                            cblk_w = t1->w;
                            cblk_h = t1->h;
                            tileLineAdvance = tile_w - cblk_w;

                            tiledp=
                                opj_tile_buf_get_ptr(tilec->buf, resno, band->bandno, (uint32_t)x, (uint32_t)y);

                            if (tccp->qmfbid == 1) {
                                for (j = 0; j < cblk_h; ++j) {
                                    for (i = 0; i < cblk_w; ++i) {
                                        int32_t tmp = tiledp[tileIndex] << T1_NMSEDEC_FRACBITS;
                                        uint32_t mag = (uint32_t)opj_int_abs(tmp);
                                        max = opj_uint_max(max, mag);
                                        t1->data[cblk_index] = mag | ((uint32_t)(tmp < 0) << T1_DATA_SIGN_BIT_INDEX);
                                        tileIndex++;
                                        cblk_index++;
                                    }
                                    tileIndex += tileLineAdvance;
                                }
                            } else {		/* if (tccp->qmfbid == 0) */
                                for (j = 0; j < cblk_h; ++j) {
                                    for (i = 0; i < cblk_w; ++i) {
                                        int32_t tmp = opj_int_fix_mul_t1(tiledp[tileIndex], bandconst);
                                        uint32_t mag = (uint32_t)opj_int_abs(tmp);
                                        uint32_t sign_mag = mag | ((uint32_t)(tmp < 0) << T1_DATA_SIGN_BIT_INDEX);
                                        max = opj_uint_max(max, mag);
                                        t1->data[cblk_index] = sign_mag;
                                        tileIndex++;
                                        cblk_index++;
                                    }
                                    tileIndex += tileLineAdvance;
                                }
                            }

                            opj_t1_encode_cblk(
                                t1,
                                cblk,
                                band->bandno,
                                compno,
                                tilec->numresolutions - 1 - resno,
                                tccp->qmfbid,
                                band->stepsize,
                                tile->numcomps,
                                tile,
                                mct_norms,
                                mct_numcomps,
                                max);
                            opj_t1_destroy(t1);

                        } /* cblkno */
#ifdef _OPENMP
                    }
#endif
                } /* precno */
            } /* bandno */
        } /* resno  */
    } /* compno  */
    return rc;
}

/** mod fixed_quality */
static void opj_t1_encode_cblk(opj_t1_opt_t *t1,
                               opj_tcd_cblk_enc_t* cblk,
                               uint32_t orient,
                               uint32_t compno,
                               uint32_t level,
                               uint32_t qmfbid,
                               double stepsize,
                               uint32_t numcomps,
                               opj_tcd_tile_t * tile,
                               const double * mct_norms,
                               uint32_t mct_numcomps,
                               uint32_t max)
{
    double cumwmsedec = 0.0;

    opj_mqc_t *mqc = t1->mqc;

    uint32_t passno;
    int32_t bpno;
    uint32_t passtype;
    int32_t nmsedec = 0;
    double tempwmsedec;

    cblk->numbps = max ? (uint32_t)((opj_int_floorlog2((int32_t)max) + 1) - T1_NMSEDEC_FRACBITS) : 0;

    bpno = (int32_t)(cblk->numbps - 1);
    passtype = 2;

    opj_mqc_resetstates(mqc);
    opj_mqc_setstate(mqc, T1_CTXNO_UNI, 0, 46);
    opj_mqc_setstate(mqc, T1_CTXNO_AGG, 0, 3);
    opj_mqc_setstate(mqc, T1_CTXNO_ZC, 0, 4);
    opj_mqc_init_enc(mqc, cblk->data);

    for (passno = 0; bpno >= 0; ++passno) {
        opj_tcd_pass_t *pass = &cblk->passes[passno];
        uint32_t correction = 3;
        switch (passtype) {
        case 0:
            opj_t1_enc_sigpass(t1, bpno, orient, &nmsedec);
            break;
        case 1:
            opj_t1_enc_refpass(t1, bpno, &nmsedec);
            break;
        case 2:
            opj_t1_enc_clnpass(t1, bpno, orient, &nmsedec);
            break;
        }

        /* fixed_quality */
        tempwmsedec = opj_t1_getwmsedec(nmsedec, compno, level, orient, bpno, qmfbid, stepsize, numcomps,mct_norms, mct_numcomps) ;
        cumwmsedec += tempwmsedec;
        tile->distotile += tempwmsedec;
        pass->term = 0;
        if (++passtype == 3) {
            passtype = 0;
            bpno--;
        }
        pass->distortiondec = cumwmsedec;
        pass->rate = opj_mqc_numbytes(mqc) + correction;
    }

    opj_mqc_flush(mqc);
    cblk->totalpasses = passno;

    for (passno = 0; passno<cblk->totalpasses; passno++) {
        opj_tcd_pass_t *pass = &cblk->passes[passno];
        if (pass->rate > opj_mqc_numbytes(mqc))
            pass->rate = opj_mqc_numbytes(mqc);
        /*Preventing generation of FF as last data byte of a pass*/
        if((pass->rate>1) && (cblk->data[pass->rate - 1] == 0xFF)) {
            pass->rate--;
        }
        pass->len = pass->rate - (passno == 0 ? 0 : cblk->passes[passno - 1].rate);
    }
}