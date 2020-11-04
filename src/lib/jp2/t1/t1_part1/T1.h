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


#include "grk_includes.h"


namespace grk {

/** Flags for 4 consecutive rows of a column */
typedef uint32_t grk_flag;

struct T1 {

	T1(bool isCompressor, uint32_t maxCblkW,	uint32_t maxCblkH);
	~T1();

	bool decompress_cblk(cblk_dec *cblk,
						uint8_t orientation,
						uint32_t roishift,
						uint32_t cblksty);
	void code_block_enc_deallocate(cblk_enc *p_code_block);
	bool allocate_buffers(uint32_t w, uint32_t h);
	double compress_cblk(cblk_enc *cblk,
							uint32_t max,
							uint8_t orientation,
							uint32_t compno,
							uint32_t level,
							uint32_t qmfbid,
							double stepsize,
							uint32_t cblksty,
							const double *mct_norms,
							uint32_t mct_numcomps,
							bool doRateControl);

	/** MQC component */
	mqcoder coder;

	int32_t *data;
	uint32_t w;
	uint32_t h;
	uint32_t data_stride;
	/* Temporary buffer to concatenate all chunks of a codebock */
	uint8_t *cblkdatabuffer;
	/* Maximum size available in cblkdatabuffer */
	uint32_t cblkdatabuffersize;


private:

	/** Flags used by decompressor and compressor.
	 * Such that flags[1+0] is for state of col=0,row=0..3,
	 flags[1+1] for col=1, row=0..3, flags[1+flags_stride] for col=0,row=4..7, ...
	 This array avoids too much cache trashing when processing by 4 vertical samples
	 as done in the various decoding steps. */
	grk_flag *flags;

	uint32_t datasize;
	uint32_t flagssize;
	bool compressor;

	template <uint32_t w, uint32_t h, bool vsc> void dec_clnpass(int32_t bpno);
	void  dec_clnpass_step(grk_flag *flagsp,
							int32_t *datap,
							int32_t oneplushalf,
							uint32_t ciorig,
							uint32_t ci,
							uint32_t vsc);
	void dec_clnpass(int32_t bpno, int32_t cblksty);
	void dec_clnpass_check_segsym(int32_t cblksty);
	void dec_sigpass_raw(int32_t bpno, int32_t cblksty);
	void dec_refpass_raw(int32_t bpno);
	void dec_sigpass_mqc(int32_t bpno, int32_t cblksty);
	void dec_refpass_mqc(int32_t bpno);
	inline void	 dec_refpass_step_raw(grk_flag *flagsp,
									int32_t *datap,
									int32_t poshalf,
									uint32_t ci);
	inline void  dec_refpass_step_mqc(mqcoder *mqc,
										grk_flag *flagsp,
										int32_t *datap,
										int32_t poshalf,
										uint32_t ci);
	inline void  dec_sigpass_step_raw(grk_flag *flagsp,
										int32_t *datap,
										int32_t oneplushalf,
											uint32_t vsc,
											uint32_t ci);
	inline void  dec_sigpass_step_mqc( grk_flag *flagsp,
										int32_t *datap,
										int32_t oneplushalf,
										uint32_t ci,
										uint32_t flags_stride,
										uint32_t vsc);
	void enc_clnpass(int32_t bpno,
					int32_t *nmsedec,
					uint32_t cblksty);
	void enc_sigpass(int32_t bpno,
					int32_t *nmsedec,
					uint8_t type,
					uint32_t cblksty);
	void  enc_refpass(int32_t bpno,
						int32_t *nmsedec,
						uint8_t type);
	int enc_is_term_pass(cblk_enc *cblk,
						uint32_t cblksty,
						int32_t bpno,
						uint32_t passtype);
	bool code_block_enc_allocate(cblk_enc *p_code_block);


	/**
	 Get the norm of a wavelet function of a subband at a specified level for the reversible 5-3 DWT.
	 @param level Level of the wavelet function
	 @param orientation Band of the wavelet function
	 @return the norm of the wavelet function
	 */
	double getnorm_53(uint32_t level, uint8_t orientation);
	/**
	 Get the norm of a wavelet function of a subband at a specified level for the irreversible 9-7 DWT
	 @param level Level of the wavelet function
	 @param orientation Band of the wavelet function
	 @return the norm of the 9-7 wavelet
	 */
	double getnorm_97(uint32_t level, uint8_t orientation);

	double getnorm(uint32_t level, uint8_t orientation, bool reversible);

	double getwmsedec(int32_t nmsedec, uint32_t compno, uint32_t level,
											uint8_t orientation, int32_t bpno,
											uint32_t qmfbid, double stepsize,
											const double *mct_norms,
											uint32_t mct_numcomps);

};

}
