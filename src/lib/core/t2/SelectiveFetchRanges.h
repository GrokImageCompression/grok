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

/**
 * @brief Result of parsing a tile-part header for selective fetch
 */
struct TilePartHeaderInfo
{
  uint64_t sodOffset; ///< byte offset of SOD marker within the tile-part
  std::vector<uint32_t> pltLengths; ///< decoded PLT packet lengths
  bool valid; ///< true if parsing succeeded
};

/**
 * @brief Extracts PLT packet lengths and SOD offset from raw tile-part header bytes
 *
 * Scans the header bytes for PLT markers (0xFF58) and SOD marker (0xFF93).
 * Decodes PLT VBR-encoded packet lengths. Does NOT require full codec infrastructure.
 *
 * @param headerData pointer to raw tile-part header bytes (starting from first marker after SOT)
 * @param headerSize number of bytes available
 * @return TilePartHeaderInfo with SOD offset and PLT lengths, or valid=false on error
 */
GRK_INTERNAL TilePartHeaderInfo extractTilePartHeaderInfo(const uint8_t* headerData,
                                                          size_t headerSize);

/**
 * @brief Computes precinct count for a single resolution of a single component
 *
 * Uses the same formula as PacketManager::getParams.
 *
 * @param tileCompBounds tile-component bounds (tile bounds scaled by component subsampling)
 * @param numResolutions total resolutions for this component
 * @param resno resolution index (0 = lowest)
 * @param precWidthExp log2 of precinct width for this resolution
 * @param precHeightExp log2 of precinct height for this resolution
 * @return number of precincts at this resolution
 */
GRK_INTERNAL uint64_t computeNumPrecincts(uint32_t tcx0, uint32_t tcy0,
                                          uint32_t tcx1, uint32_t tcy1,
                                          uint8_t numResolutions, uint8_t resno,
                                          uint8_t precWidthExp, uint8_t precHeightExp);

} // namespace grk
