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

namespace grk
{
typedef std::vector<uint32_t> PL_INFO_VEC;

struct PacketLengthMarkerInfo
{
	PacketLengthMarkerInfo() : PacketLengthMarkerInfo(nullptr) {}
	PacketLengthMarkerInfo(PL_INFO_VEC* packetLengthVec)
		: markerLength(0), packetLength(packetLengthVec)
	{}
	uint64_t markerLength;
	PL_INFO_VEC* packetLength;
};

// map of (PLT/PLM marker id) => (packet length vector)
typedef std::map<uint8_t, PacketLengthMarkerInfo> PL_MAP;

struct PacketLengthMarkers
{
	PacketLengthMarkers(void);
	PacketLengthMarkers(IBufferedStream* strm);
	~PacketLengthMarkers(void);

	// decompressor  packet lengths
	bool readPLT(uint8_t* headerData, uint16_t header_size);
	bool readPLM(uint8_t* headerData, uint16_t header_size);
	void rewind(void);
	uint32_t popNextPacketLength(void);

	// compressor packet lengths
	void pushInit(void);
	void pushNextPacketLength(uint32_t len);
	uint32_t write(bool simulate);

  private:
	void readInit(uint8_t index);
	void readNext(uint8_t Iplm);
	void tryWriteMarkerHeader(PacketLengthMarkerInfo* markerInfo, bool simulate);
	void writeMarkerLength(PacketLengthMarkerInfo* markerInfo);
	void writeIncrement(uint32_t bytes);

	PL_MAP* m_markers;
	uint8_t m_markerIndex;
	PL_INFO_VEC* m_curr_vec;
	size_t m_packetIndex;
	uint32_t m_packet_len;
	uint32_t m_markerBytesWritten;
	uint32_t m_totalBytesWritten;
	uint64_t m_marker_len_cache;
	IBufferedStream* m_stream;
	bool preCalculatedMarkerLengths;
};

} // namespace grk
