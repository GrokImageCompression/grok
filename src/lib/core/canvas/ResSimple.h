/**
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

namespace grk
{

enum eBandOrientation
{
   BAND_ORIENT_LL,
   BAND_ORIENT_HL,
   BAND_ORIENT_LH,
   BAND_ORIENT_HH,
   BAND_NUM_ORIENTATIONS
};
// LL band index when resolution == 0
const uint32_t BAND_RES_ZERO_INDEX_LL = 0;

// band indices when resolution > 0
enum eBandIndex
{
   BAND_INDEX_HL,
   BAND_INDEX_LH,
   BAND_INDEX_HH,
   BAND_NUM_INDICES
};

struct ResSimple : public grk_rect32
{
   ResSimple(void) : numTileBandWindows(0) {}
   ResSimple(grk_rect32* res, uint8_t numTileBandWindows, grk_rect32 (&tileBand)[BAND_NUM_INDICES])
   {
	  setRect(res);
	  this->numTileBandWindows = numTileBandWindows;
	  for(uint8_t i = 0; i < numTileBandWindows; i++)
		 this->tileBand[i] = tileBand[i];
   }
   ResSimple(grk_rect32 currentRes, bool finalResolution)
   {
	  setRect(currentRes);
	  if(finalResolution)
	  {
		 numTileBandWindows = 1;
		 tileBand[0] = currentRes;
	  }
	  else
	  {
		 numTileBandWindows = 3;
		 for(uint8_t j = 0; j < numTileBandWindows; ++j)
			tileBand[j] = getBandWindow(1, j + 1, currentRes);
	  }
   }
   /**
	* Get band window (in tile component coordinates) for specified number
	* of decompositions
	*
	* Note: if numDecomps is zero, then the band window (and there is only one)
	* is equal to the unreduced tile component window
	*
	* See table F-1 in JPEG 2000 standard
	*
	*/
   static grk_rect32 getBandWindow(uint8_t numDecomps, uint8_t orientation,
								   grk_rect32 tileCompWindowUnreduced)
   {
	  assert(orientation < BAND_NUM_ORIENTATIONS);
	  if(numDecomps == 0)
		 return tileCompWindowUnreduced;

	  /* project window onto sub-band generated by `numDecomps` decompositions */
	  /* See equation B-15 of the standard. */
	  uint32_t bx0 = orientation & 1;
	  uint32_t by0 = (uint32_t)(orientation >> 1U);

	  uint32_t bx0Offset = (1U << (numDecomps - 1)) * bx0;
	  uint32_t by0Offset = (1U << (numDecomps - 1)) * by0;

	  uint32_t tc_originx0 = tileCompWindowUnreduced.origin_x0;
	  uint32_t tc_originy0 = tileCompWindowUnreduced.origin_y0;
	  uint32_t tcx0 = tileCompWindowUnreduced.x0;
	  uint32_t tcy0 = tileCompWindowUnreduced.y0;
	  uint32_t tcx1 = tileCompWindowUnreduced.x1;
	  uint32_t tcy1 = tileCompWindowUnreduced.y1;

	  return grk_rect32(
		  (tc_originx0 <= bx0Offset) ? 0
									 : ceildivpow2<uint32_t>(tc_originx0 - bx0Offset, numDecomps),
		  (tc_originy0 <= by0Offset) ? 0
									 : ceildivpow2<uint32_t>(tc_originy0 - by0Offset, numDecomps),
		  (tcx0 <= bx0Offset) ? 0 : ceildivpow2<uint32_t>(tcx0 - bx0Offset, numDecomps),
		  (tcy0 <= by0Offset) ? 0 : ceildivpow2<uint32_t>(tcy0 - by0Offset, numDecomps),
		  (tcx1 <= bx0Offset) ? 0 : ceildivpow2<uint32_t>(tcx1 - bx0Offset, numDecomps),
		  (tcy1 <= by0Offset) ? 0 : ceildivpow2<uint32_t>(tcy1 - by0Offset, numDecomps));
   }

   grk_rect32 tileBand[BAND_NUM_INDICES]; // unreduced tile component bands in canvas coordinates
   uint8_t numTileBandWindows; // 1 or 3
};

} // namespace grk