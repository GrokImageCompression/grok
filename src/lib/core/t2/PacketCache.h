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

#include "PacketParser.h"

namespace grk
{

/**
 * @class PacketCache
 * @brief Manages packet buffers and associated packet parsers
 */
class PacketCache : public SparseBuffer
{
public:
  /**
   * @brief Constructs a PacketCache
   */
  PacketCache();

  /**
   * @brief Destroys a PacketCache
   */
  ~PacketCache();

  /**
   * @brief Moves to next chunk / packet buffer and associated @ref PacketParser if present.
   * If no parser is available, then nullptr is pushed to list of parsers
   * @param offset remaining bytes in current chunk
   */
  void next(size_t offset);

  /**
   * @brief Resets state to beginning of packet list, and beginning
   * of parser list
   */
  void rewind(void) override;

  /**
   * @brief Generates a PacketParser if it doesn't exist
   * @param tileProcessor @ref TileProcssor
   * @param packetSequenceNumber packet sequence number
   * @param compno component number
   * @param resno resolution number
   * @param precinctIndex precinct index
   * @param layno layer number
   * @param cachedLength signalled length from PLT/PLM marker, or cached length
   * from previous read of header
   */
  PacketParser* gen(TileProcessor* tileProcessor, uint16_t packetSequenceNumber, uint16_t compno,
                    uint8_t resno, uint64_t precinctIndex, uint16_t layno, uint32_t cachedLength);

private:
  /**
   * @brief Creates next parser, set to nullptr as placeholder,
   * if the parser iterator is at the end. Otherwise the iterator
   * is incremented
   */
  void next(void);

  std::vector<PacketParser*> parsers_;
  std::vector<PacketParser*>::iterator iter_;
};

} // namespace grk
