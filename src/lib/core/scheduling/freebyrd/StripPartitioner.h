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
#include <vector>
#include <algorithm>

namespace grk
{

/**
 * @brief Sub-band row range needed by a strip (inclusive begin, exclusive end).
 */
struct SubbandRange
{
  uint32_t lo = 0;
  uint32_t hi = 0;
  uint32_t count() const { return hi - lo; }
};

/**
 * @brief Geometry for a single cascade DWT strip at a given resolution.
 *
 * Describes the interleaved output row range and the sub-band row ranges
 * needed (including halo) for both L (LL/HL) and H (LH/HH) bands.
 */
struct StripGeometry
{
  // Output rows in interleaved domain (no halo)
  uint32_t outStart = 0;
  uint32_t outCount = 0;

  // Extended interleaved range (with halo, clamped to image bounds)
  uint32_t extFirst = 0;
  uint32_t extLast = 0;

  // Sub-band row ranges needed (L = LL/HL, H = LH/HH)
  SubbandRange rangeL;
  SubbandRange rangeH;

  // Local parity for V-DWT lifting
  uint32_t localParity = 0;

  // Output row offset within the extended stripe
  uint32_t outputStartInStripe = 0;
};

/**
 * @class StripPartitioner
 * @brief Computes strip geometry for cascade DWT at a given resolution.
 *
 * Given the resolution height, number of L/H sub-band rows, parity, and
 * strip height, produces StripGeometry for each strip. This drives which
 * code blocks need to be decoded for each strip.
 */
class StripPartitioner
{
public:
  /**
   * @brief Compute strip geometries for a resolution level.
   *
   * @param resHeight Total interleaved height of the resolution
   * @param sn Number of L (low-pass) sub-band rows
   * @param dn Number of H (high-pass) sub-band rows
   * @param parity V-DWT parity (0 = even rows are L, 1 = even rows are H)
   * @param stripeInterleaved Strip height in interleaved rows (default 64)
   * @param halo Sub-band halo rows on each side (default 4 for 9/7)
   * @return Vector of StripGeometry, one per strip
   */
  static std::vector<StripGeometry> partition(uint32_t resHeight, uint32_t sn, uint32_t dn,
                                              uint32_t parity,
                                              uint32_t stripeInterleaved = 64,
                                              uint32_t halo = 4)
  {
    if(resHeight == 0)
      return {};

    uint32_t numStripes = (resHeight + stripeInterleaved - 1) / stripeInterleaved;
    uint32_t haloInterleaved = 2 * halo;

    std::vector<StripGeometry> strips;
    strips.reserve(numStripes);

    for(uint32_t s = 0; s < numStripes; ++s)
    {
      StripGeometry sg;
      sg.outStart = s * stripeInterleaved;
      sg.outCount = std::min(stripeInterleaved, resHeight - sg.outStart);

      // Extended range with halo (clamped to image bounds)
      sg.extFirst = (sg.outStart >= haloInterleaved) ? sg.outStart - haloInterleaved : 0;
      sg.extLast = std::min(sg.outStart + sg.outCount - 1 + haloInterleaved, resHeight - 1);

      // Map interleaved range to sub-band row ranges
      if(parity == 0)
      {
        sg.rangeL.lo = (sg.extFirst + 1) / 2;
        sg.rangeL.hi = sg.extLast / 2 + 1;
        sg.rangeH.lo = sg.extFirst / 2;
        sg.rangeH.hi = (sg.extLast + 1) / 2;
      }
      else
      {
        sg.rangeH.lo = (sg.extFirst + 1) / 2;
        sg.rangeH.hi = sg.extLast / 2 + 1;
        sg.rangeL.lo = sg.extFirst / 2;
        sg.rangeL.hi = (sg.extLast + 1) / 2;
      }

      // Clamp to actual sub-band sizes
      sg.rangeL.hi = std::min(sg.rangeL.hi, sn);
      sg.rangeH.hi = std::min(sg.rangeH.hi, dn);

      // Local parity
      sg.localParity = (sg.extFirst & 1) ^ parity;

      // Output offset within extended stripe
      sg.outputStartInStripe = sg.outStart - sg.extFirst;

      strips.push_back(sg);
    }

    return strips;
  }

  /**
   * @brief Find code blocks (by sub-band-relative y range) that overlap a sub-band range.
   *
   * @param bandY0 Band origin in canvas coordinates
   * @param range Sub-band row range needed by the strip
   * @param blockY0s Sorted list of code block y-starts (canvas coords)
   * @param blockHeights Corresponding heights of each code block
   * @param[out] blockIndices Indices of overlapping blocks
   */
  static void findOverlappingBlocks(uint32_t bandY0, const SubbandRange& range,
                                    const std::vector<uint32_t>& blockY0s,
                                    const std::vector<uint32_t>& blockHeights,
                                    std::vector<uint32_t>& blockIndices)
  {
    blockIndices.clear();
    for(uint32_t i = 0; i < blockY0s.size(); ++i)
    {
      uint32_t relY0 = blockY0s[i] - bandY0;
      uint32_t relY1 = relY0 + blockHeights[i];
      // Check overlap with [range.lo, range.hi)
      if(relY1 > range.lo && relY0 < range.hi)
        blockIndices.push_back(i);
    }
  }
};

} // namespace grk
