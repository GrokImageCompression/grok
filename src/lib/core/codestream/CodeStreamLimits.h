/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include <cstdint>
#include <climits>

namespace grk
{
// Limits defined in JPEG 2000 standard
const uint16_t maxNumComponentsJ2K = 16384;
const uint8_t maxPrecisionJ2K = 38;
const uint8_t maxPassesPerSegmentJ2K = (maxPrecisionJ2K - 1) * 3 + 1;
const uint16_t maxNumTilesJ2K = 65535;
const uint8_t maxTilePartsPerTileJ2K = 255;
const uint16_t maxTotalTilePartsJ2K = 65535;
// note: includes tile part header
const uint32_t maxTilePartSizeJ2K = UINT_MAX;
const uint16_t maxNumLayersJ2K = 65535;
const uint32_t maxBitPlanesJ2K = 30;

// Limits in Grok library
#define T1_NMSEDEC_BITS 7
#define T1_NMSEDEC_FRACBITS (T1_NMSEDEC_BITS - 1)
const uint16_t maxCompressLayersGRK = 100;

} // namespace grk
