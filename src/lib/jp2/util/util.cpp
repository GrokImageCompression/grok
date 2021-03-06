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
 * Get band window in tile component coordinates for specified number
 * of decompositions
 *
 * Note: if num_res is zero, then the band window (and there is only one)
 * is equal to the unreduced tile component window
 *
 * Compute number of decomposition for this band. See table F-1
 * uint32_t numDecomps = (resno == 0) ?
 *		(uint32_t)(numresolutions - 1U) : (uint32_t)(numresolutions - resno);
 *
 */
grkRectU32 getTileCompBandWindow(uint32_t numDecomps,
							uint8_t orientation,
							grkRectU32 unreducedTileCompWindow){
	assert(orientation < BAND_NUM_ORIENTATIONS);
    if (numDecomps == 0)
    	return unreducedTileCompWindow;

    uint32_t tcx0 = unreducedTileCompWindow.x0;
	uint32_t tcy0 = unreducedTileCompWindow.y0;
	uint32_t tcx1 = unreducedTileCompWindow.x1;
	uint32_t tcy1 = unreducedTileCompWindow.y1;

    /* Map above tile-based coordinates to
     * sub-band-based coordinates, i.e. origin is at tile origin  */
    /* See equation B-15 of the standard. */
    uint32_t bx0 = orientation & 1;
    uint32_t by0 = (uint32_t)(orientation >> 1U);

    uint32_t bx0Shift = (1U << (numDecomps - 1)) * bx0;
    uint32_t by0Shift = (1U << (numDecomps - 1)) * by0;

    return grkRectU32((tcx0 <= bx0Shift) ? 0 : ceildivpow2<uint32_t>(tcx0 - bx0Shift, numDecomps),
					 (tcy0 <= by0Shift) ? 0 : ceildivpow2<uint32_t>(tcy0 - by0Shift, numDecomps),
					 (tcx1 <= bx0Shift) ? 0 : ceildivpow2<uint32_t>(tcx1 - bx0Shift, numDecomps),
					 (tcy1 <= by0Shift) ? 0 : ceildivpow2<uint32_t>(tcy1 - by0Shift, numDecomps));
}

/**
 * Get band window in tile component coordinates for specified number
 * of decompositions (with padding)
 *
 * Note: if num_res is zero, then the band window (and there is only one)
 * is equal to the unreduced tile component window (with padding)
 */
grkRectU32 getTileCompBandWindow(uint32_t numDecomps,
							uint8_t orientation,
							grkRectU32 unreducedTileCompWindow,
							grkRectU32 unreducedTileComp,
							uint32_t padding){
	assert(orientation < BAND_NUM_ORIENTATIONS);
    if (numDecomps == 0){
    	assert(orientation==0);
    	return unreducedTileCompWindow.grow(padding).intersection(&unreducedTileComp);
    }
	auto oneLessDecompWindow = unreducedTileCompWindow;
	auto oneLessDecompTile   = unreducedTileComp;
    if (numDecomps > 1){
    	oneLessDecompWindow = getTileCompBandWindow(numDecomps-1,0,unreducedTileCompWindow);
    	oneLessDecompTile   = getTileCompBandWindow(numDecomps-1,0,unreducedTileComp);
    }

	return getTileCompBandWindow(1,orientation,
			oneLessDecompWindow.grow(2 * padding).intersection(&oneLessDecompTile));
}

}
