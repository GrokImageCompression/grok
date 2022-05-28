/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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

namespace grk
{
struct TileProcessor;

/**
 Tier-2 decoding
 */
struct T2Decompress
{
	T2Decompress(TileProcessor* tileProc);

	/**
	 Decompress the packets of a tile from a source buffer
	 @param tileno 		number that identifies the tile for which to decompress the packets
	 @param src     source buffer
	 @return true if successful
	 */
	bool decompressPackets(uint16_t tileno, SparseBuffer* src, bool* truncated);

  private:
	TileProcessor* tileProcessor;
	/**
	 Decompress a packet of a tile from a source buffer
	 @param tcp 		Tile coding parameters
	 @param pi 			Packet iterator
	 @param src 	source buffer
	 @return  true if packet was successfully decompressed
	 */
	bool decompressPacket(TileCodingParams* tcp, const PacketIter* pi, SparseBuffer* srcBuf,
						  PacketInfo* packetInfo, bool skipData);
	bool processPacket(TileCodingParams* tcp, PacketIter* pi, SparseBuffer* src);
	bool readPacketHeader(TileCodingParams* tcp, uint16_t compno, uint8_t resno,
						  uint64_t precinctIndex, uint16_t layno, bool* tagBitsPresent,
						  uint8_t* src, uint64_t tileBytes, size_t remainingTilePartBytes,
						  uint32_t* packetHeaderBytes, uint32_t* packetDataBytes);
	bool readPacketData(Resolution* res, uint64_t precinctIndex, uint8_t* src, uint32_t maxLen,
						uint32_t* packetDataRead);
	void initSegment(DecompressCodeblock* cblk, uint32_t index, uint8_t cblk_sty, bool first);
};

} // namespace grk
