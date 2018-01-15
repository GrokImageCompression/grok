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
*/

#pragma once

#include "grok_includes.h"
#include "testing.h"

namespace grk {

struct decodeBlockInfo {
	decodeBlockInfo() : tilec(nullptr),
		tiledp(nullptr),
		cblk(nullptr),
		resno(0),
		bandno(0),
		stepsize(0),
		roishift(0),
		cblksty(0),
		qmfbid(0),
		x(0),
		y(0)
	{  }
	tcd_tilecomp_t* tilec;
	int32_t* tiledp;
	tcd_cblk_dec_t* cblk;
	uint32_t resno;
	uint32_t bandno;
	float stepsize;
	uint32_t roishift;
	uint32_t cblksty;
	uint32_t qmfbid;
	uint32_t x, y;		/* relative code block offset */
};



struct encodeBlockInfo {
	encodeBlockInfo() : tiledp(nullptr),
		cblk(nullptr),
		compno(0),
		resno(0),
		bandno(0),
		precno(0),
		cblkno(0),
		bandconst(0),
		stepsize(0),
		cblksty(0),
		qmfbid(0),
		x(0),
		y(0),
		mct_norms(nullptr),
#ifdef DEBUG_LOSSLESS_T1
		unencodedData(nullptr),
#endif
		mct_numcomps(0)
	{  }
	int32_t* tiledp;
	tcd_cblk_enc_t* cblk;
	uint32_t compno;
	uint32_t resno;
	uint32_t bandno;
	uint32_t precno;
	uint32_t cblkno;
	int32_t bandconst;
	float stepsize;
	uint32_t cblksty;
	uint32_t qmfbid;
	uint32_t x, y;		/* relative code block offset */
	const double * mct_norms;
#ifdef DEBUG_LOSSLESS_T1
	int32_t* unencodedData;
#endif
	uint32_t mct_numcomps;
};


class t1_interface
{
public:
	virtual ~t1_interface() {}

	virtual void preEncode(encodeBlockInfo* block, tcd_tile_t *tile, uint32_t& max) = 0;
	virtual double encode(encodeBlockInfo* block, tcd_tile_t *tile, uint32_t max, bool doRateControl)=0;
	
	virtual bool decode(decodeBlockInfo* block)=0;
	virtual void postDecode(decodeBlockInfo* block)=0;
};


}