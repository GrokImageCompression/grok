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
 */
#pragma once

#include "grk_includes.h"

namespace grk
{

struct Resolution : public grk_rect32
{
   Resolution(void);
   ~Resolution(void);
   virtual void print() const override;
   bool init(TileProcessor* tileProcessor, TileComponentCodingParams* tccp, uint8_t resno);
   ResSimple genResSimple(void);

   bool initialized;
   Subband tileBand[BAND_NUM_INDICES]; // unreduced tile component bands in canvas coordinates
   uint8_t numTileBandWindows; // 1 or 3
   uint32_t precinctGridWidth, precinctGridHeight; /* dimensions of precinct grid */
   grk_pt32 cblkExpn;
   grk_pt32 precinctPartitionTopLeft;
   grk_pt32 precinctExpn;
   grk_plugin_tile* current_plugin_tile;
   ParserMap* parserMap_;
};

} // namespace grk
