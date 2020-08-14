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
struct T2Decode : public T2 {
	T2Decode(TileProcessor *tileProc);

	/**
	 Decode the packets of a tile from a source buffer
	 @param tileno 		number that identifies the tile for which to decompress the packets
	 @param src_buf     source buffer
	 @param data_read   amount of data read
	 @return true if successful
	 */
	bool decode_packets(uint16_t tileno, ChunkBuffer *src_buf,
			uint64_t *data_read);

private:
	TileProcessor *tileProcessor;
	/**
	 Decode a packet of a tile from a source buffer
	 @param tcp 		Tile coding parameters
	 @param pi 			Packet iterator
	 @param src_buf 	source buffer
	 @param data_read   amount of data read
	 @return  true if packet was successfully decoded
	 */
	bool decode_packet(TileCodingParams *tcp, PacketIter *pi, ChunkBuffer *src_buf,
			uint64_t *data_read);

	bool skip_packet(TileCodingParams *p_tcp, PacketIter *p_pi, ChunkBuffer *src_buf,
			uint64_t *p_data_read);

	bool read_packet_header(TileCodingParams *p_tcp, PacketIter *p_pi,
			bool *p_is_data_present, ChunkBuffer *src_buf,
			uint64_t *p_data_read);

	bool read_packet_data(grk_resolution *l_res, PacketIter *p_pi,
			ChunkBuffer *src_buf, uint64_t *p_data_read);

	bool skip_packet_data(grk_resolution *l_res, PacketIter *p_pi,
			uint64_t *p_data_read, uint64_t max_length);

};

}
