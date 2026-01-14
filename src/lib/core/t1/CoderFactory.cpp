/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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
#include "Coder.h"
#include "htconfig.h"

namespace grk
{
ICoder* CoderFactory::makeCoder(bool isHT, bool isCompressor, uint16_t maxCblkW, uint16_t maxCblkH,
                                uint32_t cacheStrategy)
{
  if(isHT)
    return new ojph::T1OJPH(isCompressor, maxCblkW, maxCblkH);
  else
    return new t1_part1::Coder(isCompressor, maxCblkW, maxCblkH, cacheStrategy);
}

Quantizer* CoderFactory::makeQuantizer(bool ht, bool reversible, uint8_t guardBits)
{
  if(ht)
    return new ojph::QuantizerOJPH(reversible, guardBits);
  else
    return new Quantizer(reversible, guardBits);
}

} // namespace grk
