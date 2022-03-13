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
#include <vector>
#include <map>

//#define DEBUG_PLT

namespace grk
{
enum PL_MARKER_TYPE
{
	GRK_PL_MARKER_PLM,
	GRK_PL_MARKER_PLT,
};

typedef std::vector<grkBufferU8*> PL_RAW_MARKER;
typedef std::map<uint32_t, PL_RAW_MARKER*> PL_RAW_MARKERS;

typedef std::vector<uint32_t> PL_MARKER;
struct PacketLengthMarkerInfo
{
	PacketLengthMarkerInfo() : PacketLengthMarkerInfo(nullptr) {}
	PacketLengthMarkerInfo(PL_MARKER* marker) : markerLength_(0), marker_(marker) {}
	uint64_t markerLength_;
	PL_MARKER* marker_;
};
typedef std::map<uint32_t, PacketLengthMarkerInfo> PL_MARKERS;

struct PacketLengthMarkers
{
	~PacketLengthMarkers(void);

	//////////////////////////////////////////
	// compress
	PacketLengthMarkers(void);
	void pushInit(void);
	void pushNextPacketLength(uint32_t len);
	uint32_t write(bool simulate);
	/////////////////////////////////////////

	/////////////////////////////////////////////
	// decompress
	PacketLengthMarkers(IBufferedStream* strm);
	bool readPLT(uint8_t* headerData, uint16_t header_size);
	bool readPLM(uint8_t* headerData, uint16_t header_size);
	void rewind(void);
	uint32_t popNextPacketLength(void);
	////////////////////////////////////////////
  private:
	////////////////////////
	// compress / decompress
	PL_MARKERS *markers_;
	uint32_t markerIndex_;
	PL_MARKER *currMarker_;
	////////////////////////

	//////////////////////////
	// decompress
	bool readInit(uint8_t index, PL_MARKER_TYPE type);
	bool readNextByte(uint8_t Iplm, uint32_t *packetLength);
	bool sequential_;
	// preprocessed
	uint32_t packetLen_;
	size_t currPacketIndex_;
	// raw
	PL_RAW_MARKERS *rawMarkers_;
	PL_RAW_MARKER *currRawMarker_;
	// index of current raw buffer
	uint32_t currRawBufIndex_;
	// current raw buffer
	grkBufferU8 *currRawBuf;
	// offset into current raw buffer
	uint16_t currRawBufOffset_;
	///////////////////////////////

	////////////////////////////////
	// compress
	void tryWriteMarkerHeader(PacketLengthMarkerInfo* markerInfo, bool simulate);
	void writeMarkerLength(PacketLengthMarkerInfo* markerInfo);
	void writeIncrement(uint32_t bytes);
	uint32_t markerBytesWritten_;
	uint32_t totalBytesWritten_;
	uint64_t markerLenLocationCache_;
	IBufferedStream* stream_;
	////////////////////////////////
};

} // namespace grk
