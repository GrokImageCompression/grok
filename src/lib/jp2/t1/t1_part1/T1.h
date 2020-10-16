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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

namespace grk {

/** Flags for 4 consecutive rows of a column */
typedef uint32_t grk_flag;

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


struct T1 {

	T1(bool isEncoder, uint32_t maxCblkW,	uint32_t maxCblkH);
	~T1();

	bool decode_cblk(cblk_dec *cblk,
					uint32_t orient, uint32_t roishift, uint32_t cblksty);
	void code_block_enc_deallocate(cblk_enc *p_code_block);
	bool allocate_buffers(uint32_t w, uint32_t h);
	double encode_cblk(cblk_enc *cblk,
					uint32_t max,
					uint8_t orient, uint32_t compno, uint32_t level,
					uint32_t qmfbid, double stepsize, uint32_t cblksty,
					const double *mct_norms,
					uint32_t mct_numcomps, bool doRateControl);


	/** MQC component */
	mqcoder mqc;

	int32_t *data;
	/** Flags used by decoder and encoder.
	 * Such that flags[1+0] is for state of col=0,row=0..3,
	 flags[1+1] for col=1, row=0..3, flags[1+flags_stride] for col=0,row=4..7, ...
	 This array avoids too much cache trashing when processing by 4 vertical samples
	 as done in the various decoding steps. */
	grk_flag *flags;

	uint32_t w;
	uint32_t h;
	uint32_t datasize;
	uint32_t flagssize;
	uint32_t data_stride;
	bool encoder;

	/* Temporary buffer to concatenate all chunks of a codebock */
	uint8_t *cblkdatabuffer;
	/* Maximum size available in cblkdatabuffer */
	uint32_t cblkdatabuffersize;
};



}
