/*
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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

#include <cstdint>

namespace grk
{
/**
 * Quantization stepsize
 */
struct grk_stepsize
{
	grk_stepsize() : expn(0), mant(0) {}
	/** exponent - 5 bits */
	uint8_t expn;
	/** mantissa  -11 bits */
	uint16_t mant;
};

struct TileComponentCodingParams;
struct Subband;
struct TileCodingParams;

class Quantizer
{
  public:
	bool setBandStepSizeAndBps(Subband* band, uint32_t resno,
							   uint8_t bandIndex, TileComponentCodingParams* tccp,
							   uint8_t image_precision, bool compress);
};

} // namespace grk
