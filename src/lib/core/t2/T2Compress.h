/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
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
   T2Compress(TileProcessor* tileProc);

   /*
	Encode the packets of a tile to a destination buffer
	@param tileno           number of the tile encoded
	@param maxlayers        maximum number of layers
	@param dest             the destination buffer
	@param p_data_written   amount of data written
	@param first_poc_tile_part true if first POC tile part, otherwise false
	@param tppos            The position of the tile part flag in the progression order
	@param pino             packet iterator number
	*/
   bool compressPackets(uint16_t tileno, uint16_t maxlayers, BufferedStream* stream,
						uint32_t* p_data_written, bool first_poc_tile_part, uint32_t tppos,
						uint32_t pino);

   /**
	Simulate compressing packets of a tile to a destination buffer
	@param tileno           number of the tile encoded
	@param maxlayers        maximum number of layers
	@param p_data_written   amount of data written
	@param max_len          the max length of the destination buffer
	@param tppos            position of the tile part flag in the progression order
	@param markers			 markers
	*/
   bool compressPacketsSimulate(uint16_t tileno, uint16_t maxlayers, uint32_t* p_data_written,
								uint32_t max_len, uint32_t tppos, PLMarkerMgr* markers,
								bool isFinal, bool debug);

 private:
   TileProcessor* tileProcessor;

   /**
	Encode a packet of a tile to a destination buffer
	@param tcp 			Tile coding parameters
	@param pi 				packet iterator
	@param stream 			stream
	@param p_data_written  amount of data written
	@return
	*/
   bool compressPacket(TileCodingParams* tcp, PacketIter* pi, BufferedStream* stream,
					   uint32_t* p_data_written);

   /**
	Encode a packet of a tile to a destination buffer
	@param tcp 			Tile coding parameters
	@param pi 				packet iterator
	@param p_data_written  amount of data written
	@param len 			length of the destination buffer
	@param markers			packet length markers
	@return
	*/
   bool compressPacketSimulate(TileCodingParams* tcp, PacketIter* pi, uint32_t* p_data_written,
							   uint32_t len, PLMarkerMgr* markers, bool debug);

   bool compressHeader(BitIO* bio, Resolution* res, uint16_t layno, uint64_t precinctIndex);
};

} // namespace grk
