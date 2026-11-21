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

#include "t1_common.h"
#include "Part2.h"

namespace grk
{

struct ResSimple : public Rect32
{
  ResSimple(void) : numTileBandWindows(0) {}
  ResSimple(Rect32* res, uint8_t numTileBandWindows, Rect32 (&tileBand)[t1::BAND_NUM_INDICES])
  {
    setRect(res);
    this->numTileBandWindows = numTileBandWindows;
    for(uint8_t i = 0; i < numTileBandWindows; i++)
      this->tileBand[i] = tileBand[i];
  }
  ResSimple(Rect32 currentRes, bool finalResolution)
      : ResSimple(currentRes, DecompositionSplit::both, finalResolution)
  {}
  /**
   * Resolution with the bands its level splits off, stored by orientation slot
   * (HL, LH, HH) so absent bands leave an empty slot
   */
  ResSimple(Rect32 currentRes, DecompositionSplit split, bool finalResolution)
  {
    setRect(currentRes);
    if(finalResolution)
    {
      numTileBandWindows = 1;
      tileBand[0] = currentRes;
      return;
    }
    numTileBandWindows = 0;
    for(uint8_t orientation = t1::BAND_ORIENT_HL; orientation < t1::BAND_NUM_ORIENTATIONS;
        ++orientation)
    {
      if(!hasBand(split, orientation))
        continue;
      tileBand[orientation - 1] =
          getBandWindow(splitsHorizontally(split) ? 1 : 0, splitsVertically(split) ? 1 : 0,
                        orientation, currentRes);
      numTileBandWindows++;
    }
  }
  static bool hasBand(DecompositionSplit split, uint8_t orientation)
  {
    bool highX = orientation & 1;
    bool highY = orientation >> 1;
    return (!highX || splitsHorizontally(split)) && (!highY || splitsVertically(split)) &&
           orientation != t1::BAND_ORIENT_LL;
  }
  /**
   * Get band window (in tile component coordinates) for specified number
   * of decompositions
   *
   * Note: if numDecomps is zero, then the band window (and there is only one)
   * is equal to the unreduced tile component window
   *
   * See table F-1 in JPEG 2000 standard
   *
   */
  static Rect32 getBandWindow(uint8_t numDecomps, uint8_t orientation,
                              Rect32 tileCompWindowUnreduced)
  {
    return getBandWindow(numDecomps, numDecomps, orientation, tileCompWindowUnreduced);
  }
  /**
   * Band window after numDecompsX horizontal and numDecompsY vertical decompositions,
   * equation B-15 of the standard applied per axis (Part 2 levels may split one axis only)
   */
  static Rect32 getBandWindow(uint8_t numDecompsX, uint8_t numDecompsY, uint8_t orientation,
                              Rect32 tileCompWindowUnreduced)
  {
    assert(orientation < t1::BAND_NUM_ORIENTATIONS);
    uint32_t highX = orientation & 1;
    uint32_t highY = (uint32_t)(orientation >> 1U);
    auto tc = tileCompWindowUnreduced;
    return Rect32(
        bandCoordinate(tc.origin_x0, numDecompsX, highX),
        bandCoordinate(tc.origin_y0, numDecompsY, highY), bandCoordinate(tc.x0, numDecompsX, highX),
        bandCoordinate(tc.y0, numDecompsY, highY), bandCoordinate(tc.x1, numDecompsX, highX),
        bandCoordinate(tc.y1, numDecompsY, highY));
  }
  static uint32_t bandCoordinate(uint32_t coordinate, uint8_t numDecomps, uint32_t high)
  {
    if(numDecomps == 0)
      return coordinate;
    uint32_t offset = (1U << (numDecomps - 1)) * high;
    return coordinate <= offset ? 0 : ceildivpow2<uint32_t>(coordinate - offset, numDecomps);
  }

  // unreduced tile component bands in canvas coordinates, by orientation slot
  Rect32 tileBand[t1::BAND_NUM_INDICES];
  // bands present: 1 for the lowest resolution, otherwise 1 to 3
  uint8_t numTileBandWindows;
};

} // namespace grk
