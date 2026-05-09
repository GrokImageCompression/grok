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

#include "grok.h"

#ifdef _WIN32
#ifdef GRK_EXPORTS
#define GRK_INTERNAL __declspec(dllexport)
#else
#define GRK_INTERNAL __declspec(dllimport)
#endif
#else
#define GRK_INTERNAL __attribute__((visibility("default")))
#endif

namespace grk
{

/**
 * @brief A byte range within tile-part data
 */
struct FetchRange
{
  uint64_t offset; ///< byte offset from start of tile-part packet data (after SOD)
  uint64_t length; ///< number of bytes

  uint64_t end() const
  {
    return offset + length;
  }
};

/**
 * @brief Parameters describing the tile structure for selective fetch computation
 */
struct TilePacketInfo
{
  GRK_PROG_ORDER progression; ///< progression order
  uint16_t numComponents; ///< number of image components
  uint16_t numLayers; ///< number of quality layers
  /// number of resolutions per component
  std::vector<uint8_t> numResolutions;
  /// number of precincts per resolution per component [comp][res]
  std::vector<std::vector<uint64_t>> precinctsPerRes;
  /// PLT packet lengths in packet iteration order
  std::vector<uint32_t> pltLengths;
};

/**
 * @brief Computes byte ranges needed for selective resolution fetch
 *
 * Given PLT packet lengths and tile structure info, determines which
 * byte ranges within the tile-part data need to be fetched for a given
 * target number of resolutions to decompress.
 *
 * Adjacent/overlapping ranges are coalesced. A gap threshold can be
 * specified to merge ranges separated by small gaps (to reduce the
 * number of HTTP range requests).
 *
 * @param info tile packet structure info including PLT lengths
 * @param resolutionsToDecompress target number of resolutions (1 = lowest only)
 * @param gapThreshold merge ranges separated by gaps smaller than this (bytes)
 * @return vector of byte ranges to fetch, or empty on error
 */
GRK_INTERNAL std::vector<FetchRange> computeSelectiveFetchRanges(const TilePacketInfo& info,
                                                    uint8_t resolutionsToDecompress,
                                                    uint64_t gapThreshold = 0);

/**
 * @brief Computes the total number of packets for a tile
 *
 * @param info tile packet structure info
 * @return total packet count
 */
GRK_INTERNAL uint64_t computeTotalPackets(const TilePacketInfo& info);

} // namespace grk
