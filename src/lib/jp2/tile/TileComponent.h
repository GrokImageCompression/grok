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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */
#pragma once
#include <vector>
#include "TileProcessor.h"

namespace grk
{
struct TileComponent : public grkRectU32
{
	TileComponent();
	~TileComponent();
	bool allocSparseCanvas(uint32_t numres, bool truncatedTile);
	bool allocWindowBuffer(grkRectU32 unreducedTileCompOrImageCompWindow);
	void deallocBuffers(void);
	bool init(bool isCompressor, bool whole_tile, grkRectU32 unreducedTileComp, uint8_t prec,
			  CodingParams* cp, TileCodingParams* tcp, TileComponentCodingParams* tccp,
			  grk_plugin_tile* current_plugin_tile);
	bool subbandIntersectsAOI(uint8_t resno, eBandOrientation orient, const grkRectU32* aoi) const;

	TileComponentWindowBuffer<int32_t>* getBuffer() const;
	bool isWholeTileDecoding();
	ISparseCanvas* getSparseCanvas();
	bool postProcess(int32_t* srcData, DecompressBlockExec* block);
	bool postProcessHT(int32_t* srcData, DecompressBlockExec* block);

	Resolution* tileCompResolution; // in canvas coordinates
	uint8_t numresolutions;
	uint8_t resolutions_to_decompress; // desired number of resolutions to decompress
	uint8_t resolutions_decompressed; // actual number of resolutions decompressed
#ifdef DEBUG_LOSSLESS_T2
	Resolution* round_trip_resolutions; /* round trip resolution information */
#endif
  private:
	template<typename F>
	bool postDecompressImpl(int32_t* srcData, DecompressBlockExec* block);
	ISparseCanvas* m_sa;
	bool wholeTileDecompress;
	bool m_is_encoder;
	TileComponentWindowBuffer<int32_t>* buf;
	TileComponentCodingParams* m_tccp;
};

} // namespace grk
