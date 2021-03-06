/**
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

#include "grk_includes.h"

namespace grk {

/**
 * Get band window in tile component coordinates for specified resolution
 * and band orientation
 *
 * Note: if num_res is zero, then the band window (and there is only one)
 * is equal to the unreduced tile component window
 */
grk_rect_u32 getTileCompBandWindow(uint32_t numDecomps,
							uint8_t orientation,
							grk_rect_u32 unreducedTileCompWindow){
	assert(orientation < BAND_NUM_ORIENTATIONS);

    // Compute number of decomposition for this band. See table F-1
   // uint32_t numDecomps = (resno == 0) ?
    //		(uint32_t)(numresolutions - 1U) : (uint32_t)(numresolutions - resno);

    if (numDecomps == 0)
    	return unreducedTileCompWindow;

    uint32_t tcx0 = unreducedTileCompWindow.x0;
	uint32_t tcy0 = unreducedTileCompWindow.y0;
	uint32_t tcx1 = unreducedTileCompWindow.x1;
	uint32_t tcy1 = unreducedTileCompWindow.y1;

    /* Map above tile-based coordinates to
     * sub-band-based coordinates, i.e. origin is at tile origin  */
    /* See equation B-15 of the standard. */
    uint32_t x0b = orientation & 1;
    uint32_t y0b = (uint32_t)(orientation >> 1U);

    uint32_t x0bShift = (1U << (numDecomps - 1)) * x0b;
    uint32_t y0bShift = (1U << (numDecomps - 1)) * y0b;

    auto fullDecomp =
    	grk_rect_u32((tcx0 <= x0bShift) ? 0 : ceildivpow2<uint32_t>(tcx0 - x0bShift, numDecomps),
					 (tcy0 <= y0bShift) ? 0 : ceildivpow2<uint32_t>(tcy0 - y0bShift, numDecomps),
					 (tcx1 <= x0bShift) ? 0 : ceildivpow2<uint32_t>(tcx1 - x0bShift, numDecomps),
					 (tcy1 <= y0bShift) ? 0 : ceildivpow2<uint32_t>(tcy1 - y0bShift, numDecomps));

	return fullDecomp;
}


/**
 * Get band window in tile component coordinates for specified resolution
 * and band orientation
 *
 * Note: if num_res is zero, then the band window (and there is only one)
 * is equal to the unreduced tile component window
 */
grk_rect_u32 getTileCompBandWindow(uint32_t numDecomps,
							uint8_t orientation,
							grk_rect_u32 unreducedTileCompWindow,
							grk_rect_u32 unreducedTileComp,
							uint32_t padding){
	assert(orientation < BAND_NUM_ORIENTATIONS);
    if (numDecomps == 0) {
    	return unreducedTileCompWindow.grow(padding).intersection(&unreducedTileComp);
    } else if (numDecomps == 1){
    	auto temp =  unreducedTileCompWindow.grow(2 * padding).intersection(&unreducedTileComp);
    	return getTileCompBandWindow(1,orientation,temp);
    } else {
    	auto almostFullDecompWindow = getTileCompBandWindow(numDecomps-1,orientation,unreducedTileCompWindow);
    	auto almostFullDecomp = getTileCompBandWindow(numDecomps-1,orientation,unreducedTileComp);
    	almostFullDecompWindow= almostFullDecompWindow.grow(2 * padding).intersection(&almostFullDecomp);
    	return getTileCompBandWindow(1,orientation,almostFullDecomp);
    }
}

}
