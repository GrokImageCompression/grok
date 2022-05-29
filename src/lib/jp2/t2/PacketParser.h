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
	PacketParser(PrecinctParsers *container,
				uint16_t packetSequenceNumber,
				uint16_t compno,
				uint8_t resno,
				uint64_t precinctIndex,
				uint16_t layno,
				uint8_t *data,
				size_t tileBytes,
				size_t remainingTilePartBytes);
	virtual ~PacketParser() = default;
	bool readPacketHeader(bool* tagBitsPresent,
						  uint32_t* packetHeaderBytes, uint32_t* packetDataBytes);
	bool readPacketData(uint32_t* packetDataRead);
private:
	void initSegment(DecompressCodeblock* cblk, uint32_t index, uint8_t cblk_sty,
								   bool first);
	 PrecinctParsers *container_;
	 uint16_t packetSequenceNumber_;
	 uint16_t compno_;
	 uint8_t resno_;
	 uint64_t precinctIndex_;
	 uint16_t layno_;
	 uint8_t *data_;
	 size_t tileBytes_;
	 size_t remainingTilePartBytes_;
};

struct PrecinctParsers{
	PrecinctParsers(TileProcessor* tileProcessor);
	~PrecinctParsers(void);
	TileProcessor* tileProcessor_;
	PacketParser **parsers_;
	uint16_t numParsers_;
};

}
