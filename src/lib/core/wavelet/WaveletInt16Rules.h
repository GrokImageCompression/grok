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

#pragma once

#include <cstdint>

namespace grk
{

struct TileCodingParams;
class GrkImage;

inline bool wavelet53FitsInt16(uint8_t prec, bool isRctChroma, uint8_t numLevels)
{
  // worst case 5/3 analysis gain is 7.95 up to five levels and 8.07 at six
  uint32_t gainBits = numLevels <= 5 ? 3U : 4U;
  // RCT chroma B - G spans one bit more than the samples
  uint32_t rctChromaBits = isRctChroma ? 1U : 0U;
  return prec + gainBits + rctChromaBits <= 16;
}

inline bool inverse97FitsInt16(uint8_t prec)
{
  // 9/7 synthesis gain reaches 2^2.79, plus 3 fraction bits in the int16 lifting
  return prec + 6U <= 16;
}

inline bool forward97FitsInt16(uint8_t prec)
{
  return prec + 6U <= 16;
}

inline uint8_t wavelet97Int16FractionBits(uint8_t prec)
{
  // int16 9/7 samples are scaled so the full sample range spans 2^13
  return (uint8_t)(13 - prec);
}

int32_t forward97Int16LevelFactor(uint8_t level);
double forward97Int16BandScale(uint8_t level, uint8_t orientation);

bool wholeImageUses16BitDwt(const TileCodingParams* tcp, const GrkImage* image,
                            bool irreversibleInt16);

} // namespace grk
