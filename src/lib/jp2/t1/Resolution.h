/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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

struct Resolution : public grkRectU32 {
	Resolution() :
			initialized(false),
			numTileBandWindows(0),
			precinctGridWidth(0),
			precinctGridHeight(0),
			current_plugin_tile(nullptr)
	{}
	void print(){
		grkRectU32::print();
		for (uint32_t i = 0; i < numTileBandWindows; ++i){
			std::cout << "band " << i << " : ";
			tileBand[i].print();
		}
	}
	bool init(bool isCompressor,
			TileComponentCodingParams *tccp,
			uint8_t resno,
			grk_plugin_tile *current_plugin_tile){
		if (initialized)
			return true;

		this->current_plugin_tile = current_plugin_tile;

		/* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
		precinctExpn = grkPointU32(tccp->precinctWidthExp[resno],tccp->precinctHeightExp[resno]);

		/* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
		precinctStart = grkPointU32(floordivpow2(x0, precinctExpn.x) << precinctExpn.x,
								floordivpow2(y0, precinctExpn.y) << precinctExpn.y);

		uint64_t num_precincts = (uint64_t)precinctGridWidth * precinctGridHeight;
		if (resno != 0) {
			precinctStart=  grkPointU32(ceildivpow2<uint32_t>(precinctStart.x, 1),
									ceildivpow2<uint32_t>(precinctStart.y, 1));
			precinctExpn.x--;
			precinctExpn.y--;
		}
		cblkExpn    =  grkPointU32(std::min<uint32_t>(tccp->cblkw, precinctExpn.x),
							   std::min<uint32_t>(tccp->cblkh, precinctExpn.y));
		for (uint8_t bandIndex = 0; bandIndex < numTileBandWindows; ++bandIndex) {
			auto curr_band = tileBand + bandIndex;
			curr_band->numPrecincts = num_precincts;
			if (isCompressor) {
				for (uint64_t precinctIndex = 0; precinctIndex < num_precincts; ++precinctIndex) {
					if (!curr_band->createPrecinct(true,
										precinctIndex,
										precinctStart,
										precinctExpn,
										precinctGridWidth,
										cblkExpn))
						return false;

				}
			}
		}
		initialized = true;

		return true;
	}

	bool initialized;
	Subband tileBand[BAND_NUM_INDICES]; // unreduced tile component bands in canvas coordinates
	uint32_t numTileBandWindows;  // 1 or 3
	uint32_t precinctGridWidth, precinctGridHeight; 	/* dimensions of precinct grid */
	grkPointU32 cblkExpn;
	grkPointU32 precinctStart;
	grkPointU32 precinctExpn;
	grk_plugin_tile *current_plugin_tile;
};

}
