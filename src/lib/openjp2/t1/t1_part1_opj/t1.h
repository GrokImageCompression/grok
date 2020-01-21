/*
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
 * Copyright (c) 2012, Carl Hetherington
 * Copyright (c) 2017, IntoPIX SA <support@intopix.com>
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
#ifndef OPJ_T1_H
#define OPJ_T1_H

#define T1_NMSEDEC_BITS 7

#define T1_NUMCTXS_ZC  9
#define T1_NUMCTXS_SC  5
#define T1_NUMCTXS_MAG 3
#define T1_NUMCTXS_AGG 1
#define T1_NUMCTXS_UNI 1

#define T1_CTXNO_ZC  0
#define T1_CTXNO_SC  (T1_CTXNO_ZC+T1_NUMCTXS_ZC)
#define T1_CTXNO_MAG (T1_CTXNO_SC+T1_NUMCTXS_SC)
#define T1_CTXNO_AGG (T1_CTXNO_MAG+T1_NUMCTXS_MAG)
#define T1_CTXNO_UNI (T1_CTXNO_AGG+T1_NUMCTXS_AGG)
#define T1_NUMCTXS   (T1_CTXNO_UNI+T1_NUMCTXS_UNI)

#define T1_NMSEDEC_FRACBITS (T1_NMSEDEC_BITS-1)

/* END of flags that apply to opj_flag_t */

/* ----------------------------------------------------------------------- */

/** Flags for 4 consecutive rows of a column */
typedef uint32_t opj_flag_t;

typedef struct opj_t1 {

	/** MQC component */
	opj_mqc_t mqc;

	int32_t *data;
	/** Flags used by decoder and encoder.
	 * Such that flags[1+0] is for state of col=0,row=0..3,
	 flags[1+1] for col=1, row=0..3, flags[1+flags_stride] for col=0,row=4..7, ...
	 This array avoids too much cache trashing when processing by 4 vertical samples
	 as done in the various decoding steps. */
	opj_flag_t *flags;

	uint32_t w;
	uint32_t h;
	uint32_t datasize;
	uint32_t flagssize;
	uint32_t data_stride;
	bool encoder;

	/* Thre 3 variables below are only used by the decoder */
	/* set to TRUE in multithreaded context */
	bool mustuse_cblkdatabuffer;
	/* Temporary buffer to concatenate all chunks of a codebock */
	uint8_t *cblkdatabuffer;
	/* Maximum size available in cblkdatabuffer */
	uint32_t cblkdatabuffersize;
} opj_t1_t;

bool opj_t1_decode_cblk(opj_t1_t *t1, opj_tcd_cblk_dec_t *cblk,
		uint32_t orient, uint32_t roishift, uint32_t cblksty,
		bool check_pterm);

void post_decode(opj_t1_t *t1, opj_tcd_cblk_dec_t *cblk, uint32_t roishift,
		uint32_t qmfbid, float stepsize, int32_t *tilec_data,
		int32_t tile_w, int32_t tile_h);

void opj_t1_code_block_enc_deallocate(opj_tcd_cblk_enc_t *
        p_code_block);

bool opj_t1_allocate_buffers(opj_t1_t *t1, uint32_t w,
		uint32_t h);

double opj_t1_encode_cblk(opj_t1_t *t1, opj_tcd_cblk_enc_t *cblk,
		uint32_t max,
		uint32_t orient, uint32_t compno, uint32_t level,
		uint32_t qmfbid, double stepsize, uint32_t cblksty,
		uint32_t numcomps, const double *mct_norms,
		uint32_t mct_numcomps, bool doRateControl);

opj_t1_t* opj_t1_create(bool isEncoder);
void opj_t1_destroy(opj_t1_t *p_t1);

#endif /* OPJ_T1_H */
