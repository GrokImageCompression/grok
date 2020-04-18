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
#include "stdint.h"
#include "testing.h"
#include "Tier1.h"
#include "TileProcessor.h"
#include "T1Interface.h"
#include "t1_common.h"
#include "t1.h"



namespace grk {

namespace t1_part1 {

class T1Part1: public T1Interface {
public:
	T1Part1(bool isEncoder, grk_tcp *tcp, uint32_t maxCblkW, uint32_t maxCblkH);
	virtual ~T1Part1();

	void preEncode(encodeBlockInfo *block, grk_tcd_tile *tile, uint32_t &max);
	double compress(encodeBlockInfo *block, grk_tcd_tile *tile, uint32_t max,
			bool doRateControl);

	bool decompress(decodeBlockInfo *block);
	void postDecode(decodeBlockInfo *block);

private:
	t1_info *t1;

	void post_decode(t1_info *t1, tcd_cblk_dec_t *cblk,decodeBlockInfo *block);
};
}
}
