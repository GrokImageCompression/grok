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

#include <vector>
#include <cstring>
#include <cstdint>

namespace grk
{

class PacketTracker
{
public:
  PacketTracker(uint16_t numComps, uint8_t numRes, uint64_t numPrec, uint16_t numLayers)
      : numComps_(numComps), numRes_(numRes), numPrec_(numPrec), numLayers_(numLayers)
  {
    auto len = get_buffer_len(numComps_, numRes_, numPrec_, numLayers_);
    bits_.resize(len, 0);
  }

  void clear()
  {
    std::fill(bits_.begin(), bits_.end(), 0);
  }

  void packet_encoded(uint16_t comp, uint8_t res, uint64_t prec, uint16_t layer)
  {
    if(!isValidIndex(comp, res, prec, layer))
      return;

    auto ind = index(comp, res, prec, layer);
    // Using right shift for division by 8 and bitwise AND for modulus 8
    bits_[ind >> 3] |= static_cast<uint8_t>(1 << (ind & 7));
  }

  bool is_packet_encoded(uint16_t comp, uint8_t res, uint64_t prec, uint16_t layer) const
  {
    if(!isValidIndex(comp, res, prec, layer))
      return true;

    auto ind = index(comp, res, prec, layer);
    return bits_[ind >> 3] &
           (1 << (ind & 7)); // Using right shift for division by 8 and bitwise AND for modulus 8
  }

private:
  std::vector<uint8_t> bits_;
  uint16_t numComps_;
  uint8_t numRes_;
  uint64_t numPrec_;
  uint16_t numLayers_;

  uint64_t get_buffer_len(uint16_t numComps, uint8_t numRes, uint64_t numPrec,
                          uint16_t numLayers) const
  {
    uint64_t totalBits = static_cast<uint64_t>(numComps) * numRes * numPrec * numLayers;
    return (totalBits + 7) >> 3; // Using right shift for division by 8
  }

  bool isValidIndex(uint16_t comp, uint8_t res, uint64_t prec, uint16_t layer) const
  {
    return comp < numComps_ && res < numRes_ && prec < numPrec_ && layer < numLayers_;
  }

  uint64_t index(uint16_t comp, uint8_t res, uint64_t prec, uint16_t layer) const
  {
    return static_cast<uint64_t>(layer) * numComps_ * numRes_ * numPrec_ +
           static_cast<uint64_t>(comp) * numRes_ * numPrec_ +
           static_cast<uint64_t>(res) * numPrec_ + prec;
  }
};

} // namespace grk
