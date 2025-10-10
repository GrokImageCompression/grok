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
#include <vector>

namespace grk
{

/**
 * @struct PacketProgressionState
 *
 * @brief Stores the maximum number of layers read corresponding to
 * packet data that has been read, for each resolution
 */
struct PacketProgressionState
{
  /**
   * @brief Constructs a PacketProgressionState
   */
  PacketProgressionState() : PacketProgressionState(0) {}
  /**
   * @brief Constructs a PacketProgressionState
   * @param totalResolutions total number of resolutions in code stream
   */
  explicit PacketProgressionState(uint8_t totalResolutions)
      : totalResolutions_(totalResolutions), resLayers_(totalResolutions, 0)
  {}

  /**
   * @brief Calculates maximum number of resolutions read, by packet
   * @return maximum number of resolutions
   */
  uint8_t numResolutionsRead(void) const
  {
    // Start by assuming no non-zero element is found
    int maxIndex = -1;

    // Iterate over the vector to find the last non-zero element's index
    for(uint8_t i = 0; i < resLayers_.size(); ++i)
    {
      if(resLayers_[i] != 0U)
        maxIndex = i; // Update maxIndex with the current non-zero element's index
    }

    // If no non-zero element was found, return 0. Otherwise, return maxIndex + 1
    return (maxIndex != -1) ? static_cast<uint8_t>(maxIndex + 1) : 0;
  }

  /**
   * @brief total number of resolutions in code stream
   */
  uint8_t totalResolutions_;
  /**
   * @brief maximum layers read, by packet, for each resolution.
   */
  std::vector<uint16_t> resLayers_;
};

} // namespace grk
