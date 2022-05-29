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
 */

#pragma once

namespace grk {

struct PrecinctParsers;

class PacketParser {
public:
	PacketParser(TileProcessor* tileProcessor,
				uint16_t packetSequenceNumber,
				uint16_t compno,
				uint8_t resno,
				uint64_t precinctIndex,
				uint16_t layno,
				uint8_t *data,
				uint32_t lengthFromMarker,
				size_t tileBytes,
				size_t remainingTilePartBytes);
	virtual ~PacketParser(void) = default;
	bool readPacketHeader(void);
	bool readPacketData(void);
	uint32_t numHeaderBytes(void);
	uint32_t numSignalledDataBytes(void);
	uint32_t numSignalledBytes(void);
	uint32_t numReadDataBytes(void);
private:
	void readPacketDataFinalize(void);
	void initSegment(DecompressCodeblock* cblk, uint32_t index, uint8_t cblk_sty,
								   bool first);
	 TileProcessor* tileProcessor_;
	 uint16_t packetSequenceNumber_;
	 uint16_t compno_;
	 uint8_t resno_;
	 uint64_t precinctIndex_;
	 uint16_t layno_;
	 uint8_t *data_;
	 size_t tileBytes_;
	 size_t remainingTilePartBytes_;
	 bool tagBitsPresent_;
	 uint32_t headerBytes_;
	 uint32_t signalledDataBytes_;
	 uint32_t readDataBytes_;
	 uint32_t lengthFromMarker_;
};

struct PrecinctParsers{
	PrecinctParsers(TileProcessor* tileProcessor);
	~PrecinctParsers(void);
	TileProcessor* tileProcessor_;
	PacketParser **parsers_;
	uint16_t numParsers_;
};

}
