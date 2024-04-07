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
 */

#pragma once

#include <cstdint>
#include <map>

namespace grk
{

struct TileProcessor;

class PacketParser
{
 public:
   PacketParser(TileProcessor* tileProcessor, uint16_t packetSequenceNumber, uint16_t compno,
				uint8_t resno, uint64_t precinctIndex, uint16_t layno, uint8_t* data,
				uint32_t lengthFromMarker, size_t tileBytes, size_t remainingTilePartBytes);
   virtual ~PacketParser(void) = default;
   void readHeader(void);
   void readData(void);
   uint32_t numHeaderBytes(void);
   uint32_t numSignalledDataBytes(void);
   uint32_t numSignalledBytes(void);
   uint32_t numReadDataBytes(void);
   void print(void);

 private:
   void readDataFinalize(void);
   void initSegment(DecompressCodeblock* cblk, uint32_t index, uint8_t cblk_sty, bool first);
   TileProcessor* tileProcessor_;
   uint16_t packetSequenceNumber_;
   uint16_t compno_;
   uint8_t resno_;
   uint64_t precinctIndex_;
   uint16_t layno_;
   uint8_t* data_;
   size_t tileBytes_;
   size_t remainingTilePartBytes_;
   bool tagBitsPresent_;
   // header bytes in packet - doesn't include packed header bytes
   uint32_t packetHeaderBytes_;
   uint32_t signalledDataBytes_;
   uint32_t readDataBytes_;
   uint32_t lengthFromMarker_;
   bool parsedHeader_;
   bool headerError_;
};

struct PrecinctPacketParsers
{
   PrecinctPacketParsers(TileProcessor* tileProcessor);
   ~PrecinctPacketParsers(void);
   void pushParser(PacketParser* parser);
   TileProcessor* tileProcessor_;
   PacketParser** parsers_;
   uint16_t numParsers_;
   uint16_t allocatedParsers_;
};

struct TileProcessor;

struct ParserMap
{
   ParserMap(TileProcessor* tileProcessor);
   ~ParserMap();
   void pushParser(uint64_t precinctIndex, PacketParser* parser);

   TileProcessor* tileProcessor_;
   std::map<uint64_t, PrecinctPacketParsers*> precinctParsers_;
};

} // namespace grk
