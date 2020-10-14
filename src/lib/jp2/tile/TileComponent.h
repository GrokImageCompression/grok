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
#include "testing.h"
#include <vector>

#include "TileProcessor.h"

namespace grk {

// tile component
struct TileComponent : public grk_rect_u32 {
	TileComponent();
	~TileComponent();

	void create_buffer(	grk_image *output_image,uint32_t dx,uint32_t dy);
	bool init(bool isEncoder,
			bool whole_tile,
			grk_image *output_image,
			CodingParams *cp,
			TileCodingParams *tcp,
			grk_tile *tile,
			grk_image_comp* image_comp,
			TileComponentCodingParams* tccp,
			grk_plugin_tile *current_plugin_tile);

	 void allocSparseBuffer(uint32_t numres);
	 void release_mem();
	 bool subbandIntersectsAOI(uint32_t resno,
	 								uint32_t bandno,
	 								const grk_rect_u32 *aoi) const;

	uint32_t numresolutions; /* number of resolution levels */
	uint32_t resolutions_to_decompress; /* number of resolutions level to decompress (at max)*/
	grk_resolution *resolutions; /* resolutions information */
#ifdef DEBUG_LOSSLESS_T2
	grk_resolution* round_trip_resolutions;  /* round trip resolution information */
#endif
	TileComponentBuffer<int32_t> *buf;
    bool   whole_tile_decoding;
	bool m_is_encoder;
	ISparseBuffer *m_sa;
private:
	TileComponentCodingParams *m_tccp;

};

}




