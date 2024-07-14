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

namespace grk
{

Resolution::Resolution(void)
	: initialized(false), numTileBandWindows(0), precinctGridWidth(0), precinctGridHeight(0),
	  current_plugin_tile(nullptr), parserMap_(nullptr)
{}
Resolution::~Resolution(void)
{
   delete parserMap_;
}
void Resolution::print(void) const
{
   grk_rect32::print();
   for(uint32_t i = 0; i < numTileBandWindows; ++i)
   {
	  std::cout << "band " << i << " : ";
	  tileBand[i].print();
   }
}
bool Resolution::init(TileProcessor* tileProcessor, TileComponentCodingParams* tccp, uint8_t resno)
{
   if(initialized)
	  return true;

   current_plugin_tile = tileProcessor->current_plugin_tile;

   /* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
   precinctExpn = grk_pt32(tccp->precWidthExp[resno], tccp->precHeightExp[resno]);

   /* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
   precinctPartitionTopLeft = grk_pt32(floordivpow2(x0, precinctExpn.x) << precinctExpn.x,
									   floordivpow2(y0, precinctExpn.y) << precinctExpn.y);

   uint64_t num_precincts = (uint64_t)precinctGridWidth * precinctGridHeight;
   if(resno != 0)
   {
	  precinctPartitionTopLeft = grk_pt32(ceildivpow2<uint32_t>(precinctPartitionTopLeft.x, 1),
										  ceildivpow2<uint32_t>(precinctPartitionTopLeft.y, 1));
	  precinctExpn.x--;
	  precinctExpn.y--;
   }
   cblkExpn = grk_pt32(std::min<uint32_t>(tccp->cblkw, precinctExpn.x),
					   std::min<uint32_t>(tccp->cblkh, precinctExpn.y));
   for(uint8_t bandIndex = 0; bandIndex < numTileBandWindows; ++bandIndex)
   {
	  auto curr_band = tileBand + bandIndex;
	  curr_band->num_precincts = num_precincts;
	  if(tileProcessor->isCompressor())
	  {
		 for(uint64_t precinctIndex = 0; precinctIndex < num_precincts; ++precinctIndex)
		 {
			if(!curr_band->createPrecinct(tileProcessor, precinctIndex, precinctPartitionTopLeft,
										  precinctExpn, precinctGridWidth, cblkExpn))
			   return false;
		 }
	  }
   }

   if(!tileProcessor->isCompressor())
	  parserMap_ = new ParserMap(tileProcessor);
   initialized = true;

   return true;
}

ResSimple Resolution::genResSimple(void)
{
   grk_rect32 bands[BAND_NUM_INDICES] = {tileBand[BAND_INDEX_HL], tileBand[BAND_INDEX_LH],
										 tileBand[BAND_INDEX_HH]};

   return ResSimple(this, numTileBandWindows, bands);
}

} // namespace grk
