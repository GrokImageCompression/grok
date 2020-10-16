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

#include "grk_includes.h"

namespace grk {

class T1Scheduler {
public:
	void scheduleEncode(TileCodingParams *tcp,
						grk_tile *tile,
						const double *mct_norms,
						uint32_t mct_numcomps, bool doRateControl);
	bool prepareScheduleDecode(TileComponent *tilec, TileComponentCodingParams *tccp,
			std::vector<DecompressBlockInfo*> *blocks);
	bool scheduleDecode(TileCodingParams *tcp,
						uint16_t blockw,
						uint16_t blockh,
						std::vector<DecompressBlockInfo*> *blocks);
};

}
