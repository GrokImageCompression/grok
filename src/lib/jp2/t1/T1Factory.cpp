
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
 */
#include "simd.h"
#include "grk_includes.h"
#include "T1Part1.h"
#include "htconfig.h"

namespace grk
{
T1Interface* T1Factory::makeT1(bool isCompressor, TileCodingParams* tcp, uint32_t maxCblkW,
							   uint32_t maxCblkH)
{
	if(tcp->isHT())
	{
#ifdef OPENHTJ2K
		return (T1Interface*)(new openhtj2k::T1OpenHTJ2K(isCompressor, tcp, maxCblkW, maxCblkH));
#else
		return (T1Interface*)(new ojph::T1OJPH(isCompressor, tcp, maxCblkW, maxCblkH));
#endif
	}
	return (T1Interface*)(new t1_part1::T1Part1(isCompressor, maxCblkW, maxCblkH));
}

Quantizer* T1Factory::makeQuantizer(bool ht, bool reversible, uint8_t guardBits)
{
	if(ht)
	{
#ifdef OPENHTJ2K
		return (Quantizer*)(new openhtj2k::QuantizerOpenHTJ2K(reversible, guardBits));
#else
		return (Quantizer*)(new ojph::QuantizerOJPH(reversible, guardBits));
#endif
	}
	return new Quantizer(reversible, guardBits);
}

} // namespace grk
