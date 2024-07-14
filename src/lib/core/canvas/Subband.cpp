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
#include "grk_includes.h"
#include <map>

namespace grk
{

Subband::Subband() : orientation(BAND_ORIENT_LL), num_precincts(0), numbps(0), stepsize(0) {}
// note: don't copy precinct array
Subband::Subband(const Subband& rhs)
	: grk_rect32(rhs), orientation(rhs.orientation), num_precincts(0), numbps(rhs.numbps),
	  stepsize(rhs.stepsize)
{}
Subband& Subband::operator=(const Subband& rhs)
{
   if(this != &rhs)
   { // self-assignment check expected
	  *this = Subband(rhs);
   }
   return *this;
}
void Subband::print() const
{
   grk_rect32::print();
}
bool Subband::empty()
{
   return ((x1 - x0 == 0) || (y1 - y0 == 0));
}
Precinct* Subband::getPrecinct(uint64_t precinctIndex)
{
   if(precinctMap.find(precinctIndex) == precinctMap.end())
	  return nullptr;
   uint64_t index = precinctMap[precinctIndex];

   return precincts[index];
}
grk_rect32 Subband::generatePrecinctBounds(uint64_t precinctIndex,
										   grk_pt32 precinctPartitionTopLeft, grk_pt32 precinctExpn,
										   uint32_t precinctGridWidth)
{
   auto precinctTopLeft =
	   grk_pt32(precinctPartitionTopLeft.x +
					(uint32_t)((precinctIndex % precinctGridWidth) << precinctExpn.x),
				precinctPartitionTopLeft.y +
					(uint32_t)((precinctIndex / precinctGridWidth) << precinctExpn.y));
   return grk_rect32(precinctTopLeft.x, precinctTopLeft.y,
					 precinctTopLeft.x + (1U << precinctExpn.x),
					 precinctTopLeft.y + (1U << precinctExpn.y))
	   .intersection(this);
}
Precinct* Subband::createPrecinct(TileProcessor* tileProcessor, uint64_t precinctIndex,
								  grk_pt32 precinctPartitionTopLeft, grk_pt32 precinctExpn,
								  uint32_t precinctGridWidth, grk_pt32 cblk_expn)
{
   auto temp = precinctMap.find(precinctIndex);
   if(temp != precinctMap.end())
	  return precincts[temp->second];

   auto bounds = generatePrecinctBounds(precinctIndex, precinctPartitionTopLeft, precinctExpn,
										precinctGridWidth);
   if(!bounds.valid())
   {
	  Logger::logger_.error("createPrecinct: invalid precinct bounds.");
	  return nullptr;
   }
   auto currPrec = new Precinct(tileProcessor, bounds, cblk_expn);
   currPrec->precinctIndex = precinctIndex;
   precincts.push_back(currPrec);
   precinctMap[precinctIndex] = precincts.size() - 1;

   return currPrec;
}

} // namespace grk
