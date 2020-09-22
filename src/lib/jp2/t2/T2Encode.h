/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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

#include <T2.h>

namespace grk {

struct TileProcessor;

/**
 @file T2.h
 @brief Implementation of a tier-2 coding (packetization of code-block data) (T2)

 */

/** @defgroup T2 T2 - Implementation of a tier-2 coding */
/*@{*/

/**
 Tier-2 coding
 */
struct T2Encode : public T2 {
	T2Encode(TileProcessor *tileProc);

	/*
	 Encode the packets of a tile to a destination buffer
	 @param tileno           number of the tile encoded
	 @param maxlayers        maximum number of layers
	 @param dest             the destination buffer
	 @param p_data_written   amount of data written
	 @param tpnum            Tile part number of the current tile
	 @param tppos            The position of the tile part flag in the progression order
	 @param pino             packet iterator number
	 */
	bool encode_packets(uint16_t tileno, uint16_t maxlayers,
			BufferedStream *stream, uint32_t *p_data_written,
			uint32_t tpnum, uint32_t tppos,
			uint32_t pino);

	/**
	 Simulate encoding packets of a tile to a destination buffer
	 @param tileno           number of the tile encoded
	 @param maxlayers        maximum number of layers
	 @param p_data_written   amount of data written
	 @param max_len          the max length of the destination buffer
	 @param tppos            position of the tile part flag in the progression order
	 @param markers			 markers
	 */
	bool encode_packets_simulate(uint16_t tileno, uint16_t maxlayers,
			uint32_t *p_data_written, uint32_t max_len, uint32_t tppos,
			PacketLengthMarkers *markers);

private:
	TileProcessor *tileProcessor;

	/**
	 Encode a packet of a tile to a destination buffer
	 @param tcp 			Tile coding parameters
	 @param pi 				packet iterator
	 @param stream 			stream
	 @param p_data_written  amount of data written
	 @return
	 */
	bool encode_packet(TileCodingParams *tcp, PacketIter *pi,
			BufferedStream *stream, uint32_t *p_data_written);

	/**
	 Encode a packet of a tile to a destination buffer
	 @param tcp 			Tile coding parameters
	 @param pi 				packet iterator
	 @param p_data_written  amount of data written
	 @param len Length of the destination buffer
	 @return
	 */
	bool encode_packet_simulate(TileCodingParams *tcp, PacketIter *pi,
			uint32_t *p_data_written, uint32_t len,
			PacketLengthMarkers *markers);

};

}
