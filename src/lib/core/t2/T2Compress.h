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

namespace grk
{
struct TileProcessor;

/**
 Tier-2 coding
 */
struct T2Compress
{
  T2Compress(TileProcessorCompress* tileProc);

  /*
   Encode the packets of a tile to a destination buffer
   @param tileno           number of the tile encoded
   @param maxlayers        maximum number of layers
   @param dest             the destination buffer
   @param p_data_written   amount of data written
   @param first_poc_tile_part true if first POC tile part, otherwise false
   @param newTilePartProgressionPosition            The position of the tile part flag in the
   progression order
   @param prog_iter_num             packet iterator number
   */
  bool compressPackets(uint16_t tileno, uint16_t maxlayers, IStream* stream,
                       uint32_t* p_data_written, bool first_poc_tile_part,
                       uint8_t newTilePartProgressionPosition, uint32_t prog_iter_num);

  /**
   * \brief Simulate compressing packets of a tile to a destination buffer
   * @param tileno           number of the tile encoded
   * @param maxlayers        maximum number of layers
   * @param p_data_written   amount of data written
   * @param max_len          the max length of the destination buffer
   * @param newTilePartProgressionPosition            position of the tile part flag in the
   *                                                  progression order
   * @param markers			 markers (see @ref PLMarker)
   * @param isFinal          true if this is final T2 pass
   * @param debug            true if in debug mode
   */
  bool compressPacketsSimulate(uint16_t tileno, uint16_t maxlayers, uint32_t* p_data_written,
                               uint32_t max_len, uint8_t newTilePartProgressionPosition,
                               PLMarker* markers, bool isFinal, bool debug);

private:
  TileProcessorCompress* tileProcessor;

  /**
   Encode a packet of a tile to a destination buffer
   @param tcp 			Tile coding parameters
   @param pi 				packet iterator
   @param stream 			stream
   @param p_data_written  amount of data written
   @return
   */
  bool compressPacket(TileCodingParams* tcp, PacketIter* pi, IStream* stream,
                      uint32_t* p_data_written);

  /**
   * Encode a packet of a tile to a destination buffer
   * @param tcp 			Tile coding parameters
   * @param pi 				packet iterator
   * @param p_data_written  amount of data written
   * @param len 			length of the destination buffer
   * @param markers			packet length markers
   * @param debug			true if in debug mode
   *
   * @return true if successful
   */
  bool compressPacketSimulate(TileCodingParams* tcp, PacketIter* pi, uint32_t* p_data_written,
                              uint32_t len, PLMarker* markers, bool debug);

  bool compressHeader(t1_t2::BitIO* bio, Resolution* res, uint16_t layno, uint64_t precinctIndex);
};

} // namespace grk
