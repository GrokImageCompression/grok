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
#include <map>

namespace grk
{

struct Subband : public grk_rect32
{
   Subband();
   Subband(const Subband& rhs);
   virtual ~Subband() = default;
   Subband& operator=(const Subband& rhs);
   void print() const override;
   bool empty();
   Precinct* getPrecinct(uint64_t precinctIndex);
   grk_rect32 generatePrecinctBounds(uint64_t precinctIndex, grk_pt32 precinctPartitionTopLeft,
									 grk_pt32 precinctExpn, uint32_t precinctGridWidth);
   Precinct* createPrecinct(TileProcessor* tileProcessor, uint64_t precinctIndex,
							grk_pt32 precinctPartitionTopLeft, grk_pt32 precinctExpn,
							uint32_t precinctGridWidth, grk_pt32 cblk_expn);
   eBandOrientation orientation;
   std::vector<Precinct*> precincts;
   // maps global precinct index to precincts vector index
   std::map<uint64_t, uint64_t> precinctMap;
   uint64_t numPrecincts;
   uint8_t numbps;
   float stepsize;
};

} // namespace grk
