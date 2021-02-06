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
 * Note: for 0th resolution, the band window (and there is only one)
 * is equal to the resolution window
 */
grk_rect_u32 getTileCompBandWindow(uint8_t num_res,
							uint8_t resno,
							uint8_t orientation,
							grk_rect_u32 unreducedTileCompWindow){
	assert(orientation < BAND_NUM_ORIENTATIONS);
	assert(resno > 0 || orientation == 0);

    // Compute number of decomposition for this band. See table F-1
    uint32_t num_decomps = (resno == 0) ? (uint32_t)(num_res - 1U) : (uint32_t)(num_res - resno);

    uint32_t tcx0 = unreducedTileCompWindow.x0;
	uint32_t tcy0 = unreducedTileCompWindow.y0;
	uint32_t tcx1 = unreducedTileCompWindow.x1;
	uint32_t tcy1 = unreducedTileCompWindow.y1;
    /* Map above tile-based coordinates to
     * sub-band-based coordinates, i.e. origin is at tile origin  */
    /* See equation B-15 of the standard. */
    uint32_t x0b = orientation & 1;
    uint32_t y0b = (uint32_t)(orientation >> 1U);
	uint32_t band_x0 = (num_decomps == 0) ? tcx0 :
			(tcx0 <= (1U << (num_decomps - 1)) * x0b) ? 0 :
			ceildivpow2<uint32_t>(tcx0 - (1U << (num_decomps - 1)) * x0b, num_decomps);

	uint32_t band_y0 = (num_decomps == 0) ? tcy0 :
			(tcy0 <= (1U << (num_decomps - 1)) * y0b) ? 0 :
			ceildivpow2<uint32_t>(tcy0 - (1U << (num_decomps - 1)) * y0b, num_decomps);

	uint32_t band_x1 = (num_decomps == 0) ? tcx1 :
			(tcx1 <= (1U << (num_decomps - 1)) * x0b) ? 0 :
			ceildivpow2<uint32_t>(tcx1 - (1U << (num_decomps - 1)) * x0b, num_decomps);

	uint32_t band_y1 = (num_decomps == 0) ? tcy1 :
			(tcy1 <= (1U << (num_decomps - 1)) * y0b) ? 0 :
			ceildivpow2<uint32_t>(tcy1 - (1U << (num_decomps - 1)) * y0b, num_decomps);


	return grk_rect_u32(band_x0,band_y0,band_x1,band_y1);
}


}
