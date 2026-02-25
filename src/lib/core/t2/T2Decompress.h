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

#include "PacketProgressionState.h"

namespace grk
{
struct ITileProcessor;

/**
 * @class T2Decompress
 * @brief T2 parsing of packets
 *
 */
struct T2Decompress
{
  /**
   * @brief Constructs a T2Decompress object
   * @param tileProc @ref TileProcesor
   */
  T2Decompress(ITileProcessor* tileProc);

  /**
   * @brief Destroys a T2Decompress object
   */
  virtual ~T2Decompress(void) = default;

  /**
   * @brief Parses tile packets
   * @param tileno index of tile
   * @param compressedPackets @ref PacketCache of buffers containing packets
   * @return true if packets truncated
   */
  bool parsePackets(uint16_t tileno, PacketCache* compressedPackets);

  /**
   * @brief Parses packet data
   * @param parser @ref PacketParser
   */
  static void parsePacketData(PacketParser* parser);

private:
  /**
   * @brief @ref ITileProcessor for this tile
   */
  ITileProcessor* tileProcessor;

  /**
   * @brief Parses packet
   * @param compno component number
   * @param resno resolution number
   * @param precinctIndex precinct index
   * @param layno layer number
   * @param compressedPackets @ref PacketCache of buffers containing packets
   * @return true if successful
   */
  bool parsePacket(uint16_t compno, uint8_t resno, uint64_t precinctIndex, uint16_t layno,
                   PacketCache* compressedPackets);

  /**
   * @brief Parses packet data
   * @param res @ref Resolution
   * @param parser @ref PacketParser
   * @param precinctIndex precinct index
   * @param enqueue if true, enqueue packet data for concurrent parsing. NOTE: this is
   * only possible if packet length is known before any parsing is done, for example
   * if there is a PLT/PLM marker, or packet header was previously read
   */
  void parsePacketData(Resolution* res, PacketParser* parser, uint64_t precinctIndex, bool enqueue);
};

} // namespace grk
