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
#include <vector>
#include "T1Interface.h"

namespace grk {

class Tier1 {
public:

	bool encodeCodeblocks(grk_tcp *tcp, grk_tcd_tile *tile, const double *mct_norms,
			uint32_t mct_numcomps, bool doRateControl);

	bool prepareDecodeCodeblocks(TileComponent *tilec, grk_tccp *tccp,
			std::vector<decodeBlockInfo*> *blocks);

	bool decodeCodeblocks(grk_coding_parameters *cp,
							grk_tcp *tcp,
							uint16_t blockw,
							uint16_t blockh,
							std::vector<decodeBlockInfo*> *blocks);

};

}
