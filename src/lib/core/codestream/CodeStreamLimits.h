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
 *
 */

#pragma once

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

// Limits in Grok library
// We can have a maximum 31 bits in each 32 bit wavelet coefficient
// as the most significant bit is reserved for the sign.
// Since we need T1_NMSEDEC_FRACBITS fixed point fractional bits,
// we can only support a maximum of (31-T1_NMSEDEC_FRACBITS) bit planes
#define T1_NMSEDEC_BITS 7
#define T1_NMSEDEC_FRACBITS (T1_NMSEDEC_BITS - 1)
const uint32_t maxBitPlanesGRK = 31 - T1_NMSEDEC_FRACBITS;
// const uint32_t max_bit_planes_bibo = maxSupportedPrecisionGRK + GRK_MAXRLVLS * 5;
const uint16_t maxCompressLayersGRK = 100;

} // namespace grk
