/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
struct TileComponent : public grk_rect32
{
   TileComponent();
   ~TileComponent();
   bool allocRegionWindow(uint32_t numres, bool truncatedTile);
   bool canCreateWindow(grk_rect32 unreducedTileCompOrImageCompWindow);
   void createWindow(grk_rect32 unreducedTileCompOrImageCompWindow);
   void dealloc(void);
   bool init(TileProcessor* tileProcessor, grk_rect32 unreducedTileComp, uint8_t prec,
			 TileComponentCodingParams* tccp);
   bool subbandIntersectsAOI(uint8_t resno, eBandOrientation orient, const grk_rect32* aoi) const;

   TileComponentWindow<int32_t>* getWindow() const;
   bool isWholeTileDecoding();
   ISparseCanvas* getRegionWindow();
   void postProcess(int32_t* srcData, DecompressBlockExec* block);
   void postProcessHT(int32_t* srcData, DecompressBlockExec* block, uint16_t stride);

   Resolution* resolutions_; // in canvas coordinates
   uint8_t numresolutions;
   uint8_t numResolutionsToDecompress; // desired number of resolutions to decompress
   std::atomic<uint8_t> highestResolutionDecompressed; // highest resolution actually decompressed
#ifdef DEBUG_LOSSLESS_T2
   Resolution* round_trip_resolutions; /* round trip resolution information */
#endif
 private:
   template<typename F>
   void postDecompressImpl(int32_t* srcData, DecompressBlockExec* block, uint16_t stride);
   ISparseCanvas* regionWindow_;
   bool wholeTileDecompress;
   bool isCompressor_;
   TileComponentWindow<int32_t>* window_;
   TileComponentCodingParams* tccp_;
};

} // namespace grk
