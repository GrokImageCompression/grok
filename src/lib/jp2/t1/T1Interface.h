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
 */

#pragma once

#include "grok_includes.h"
#include "testing.h"

namespace grk {

struct decodeBlockInfo {
	decodeBlockInfo() :
			tilec(nullptr),
			tiledp(nullptr),
			cblk(nullptr),
			resno(0),
			bandno(0),
			stepsize(0),
			roishift(0),
			cblk_sty(0),
			qmfbid(0),
			x(0),
			y(0),
			k_msbs(0)
	{	}
	TileComponent *tilec;
	int32_t *tiledp;
	grk_tcd_cblk_dec *cblk;
	uint32_t resno;
	uint32_t bandno;
	float stepsize;
	uint32_t roishift;
	uint32_t cblk_sty;
	uint32_t qmfbid;
	/* relative code block offset */
	uint32_t x;
	uint32_t y;
	uint8_t k_msbs;
};

struct encodeBlockInfo {
	encodeBlockInfo() :	tiledp(nullptr),
			            cblk(nullptr),
						compno(0),
						resno(0),
						bandno(0),
						precno(0),
						cblkno(0),
						inv_step(0),
						inv_step_ht(0),
						stepsize(0),
						cblk_sty(0),
						qmfbid(0),
						x(0),
						y(0),
						mct_norms(nullptr),
#ifdef DEBUG_LOSSLESS_T1
		unencodedData(nullptr),
#endif
					mct_numcomps(0),
					k_msbs(0)
	{
	}
	int32_t *tiledp;
	grk_tcd_cblk_enc *cblk;
	uint32_t compno;
	uint32_t resno;
	uint8_t bandno;
	uint32_t precno;
	uint32_t cblkno;
	// inverse step size in 13 bit fixed point
	int32_t inv_step;
	float inv_step_ht;
	float stepsize;
	uint8_t cblk_sty;
	uint8_t qmfbid;
	uint32_t x, y; /* relative code block offset */
	const double *mct_norms;
#ifdef DEBUG_LOSSLESS_T1
	int32_t* unencodedData;
#endif
	uint32_t mct_numcomps;
	uint8_t k_msbs;
};

class T1Interface {
public:
	virtual ~T1Interface() {
	}

	virtual void preEncode(encodeBlockInfo *block, grk_tcd_tile *tile,
			uint32_t &max) = 0;
	virtual double compress(encodeBlockInfo *block, grk_tcd_tile *tile,
			uint32_t max, bool doRateControl)=0;

	virtual bool decompress(decodeBlockInfo *block)=0;
	virtual void postDecode(decodeBlockInfo *block)=0;
};

}
