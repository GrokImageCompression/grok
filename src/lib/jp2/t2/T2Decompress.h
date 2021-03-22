/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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

namespace grk {

struct TileProcessor;


/**
 Tier-2 decoding
 */
struct T2Decompress {
	T2Decompress(TileProcessor *tileProc);

	/**
	 Decompress the packets of a tile from a source buffer
	 @param tileno 		number that identifies the tile for which to decompress the packets
	 @param srcBuf     source buffer
	 @param data_read   amount of data read
	 @return true if successful
	 */
	bool decompressPackets(uint16_t tileno,
							SparseBuffer *srcBuf,
							uint32_t *data_read,
							bool *truncated);

private:
	TileProcessor *tileProcessor;
	/**
	 Decompress a packet of a tile from a source buffer
	 @param tcp 		Tile coding parameters
	 @param pi 			Packet iterator
	 @param srcBuf 	source buffer
	 @param data_read   amount of data read
	 @return  true if packet was successfully decompressed
	 */
	bool decompressPacket(TileCodingParams *tcp,
							const PacketIter *pi,
							SparseBuffer *srcBuf,
							uint32_t *data_read);

	bool processPacket(TileCodingParams *tcp,
							PacketIter *pi,
							SparseBuffer *srcBuf,
							uint32_t *data_read);
	bool skipPacket(TileCodingParams *p_tcp,
						PacketIter *p_pi,
						SparseBuffer *srcBuf,
						uint32_t *dataRead);

	bool readPacketHeader(TileCodingParams *p_tcp,
							const PacketIter *p_pi,
							bool *dataPresent,
							SparseBuffer *srcBuf,
							uint32_t *dataRead,
							uint32_t *packetDataBytes);

	bool readPacketData(Resolution *l_res,
							const PacketIter *p_pi,
							SparseBuffer *srcBuf,
							uint32_t *dataRead);

	void initSegment(DecompressCodeblock *cblk,
					uint32_t index,
					uint8_t cblk_sty,
					bool first);

};

}
