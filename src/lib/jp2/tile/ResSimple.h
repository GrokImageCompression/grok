/**
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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

	grk_rect32 tileBand[BAND_NUM_INDICES]; // unreduced tile component bands in canvas coordinates
	uint8_t numTileBandWindows; // 1 or 3
};

} // namespace grk
