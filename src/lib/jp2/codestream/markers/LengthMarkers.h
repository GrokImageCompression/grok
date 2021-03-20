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
 */

#pragma once

namespace grk {

// tile part length
struct grkTileInfo {
	grkTileInfo() :
			has_tile_number(false), tileNumber(0), length(0) {
	}
	grkTileInfo(uint32_t len) :
			has_tile_number(false), tileNumber(0), length(len) {
	}
	grkTileInfo(uint16_t tileno, uint32_t len) :
			has_tile_number(true), tileNumber(tileno), length(len) {
	}
	bool has_tile_number;
	uint16_t tileNumber;
	uint32_t length;

};

typedef std::vector<grkTileInfo> TL_INFO_VEC;
// map of (TLM marker id) => (tile part length vector)
typedef std::map<uint8_t, TL_INFO_VEC*> TL_MAP;

struct TileLengthMarkers {
	TileLengthMarkers();
	TileLengthMarkers(BufferedStream *stream);
	~TileLengthMarkers();

	bool read(uint8_t *p_header_data, uint16_t header_size);
	void getInit(void);
	grkTileInfo getNext(void);
	bool skipTo(uint16_t skipTileIndex, BufferedStream *stream, uint64_t firstSotPos);

	bool writeBegin(uint16_t totalTileParts);
	void writeUpdate(uint16_t tileIndex, uint32_t tile_part_size);
	bool writeEnd(void);
	/**
	 Add tile header marker information
	 @param tileno       tile index number
	 @param cstr_index   Codestream information structure
	 @param type         marker type
	 @param pos          byte offset of marker segment
	 @param len          length of marker segment
	 */
	static bool addToIndex(uint16_t tileno,
							grk_codestream_index *cstr_index,
							uint16_t type,
							uint64_t pos,
							uint32_t len);
private:
	void push(uint8_t i_TLM, grkTileInfo curr_vec);
	TL_MAP *m_markers;
	uint8_t m_markerIndex;
	uint8_t m_markerTilePartIndex;
	TL_INFO_VEC *m_curr_vec;
	BufferedStream *m_stream;
	uint64_t m_tlm_start_stream_position;
};

typedef std::vector<uint32_t> PL_INFO_VEC;
// map of (PLT/PLM marker id) => (packet length vector)
typedef std::map<uint8_t, PL_INFO_VEC*> PL_MAP;

struct PacketLengthMarkers {
	PacketLengthMarkers(void);
	PacketLengthMarkers(BufferedStream *strm);
	~PacketLengthMarkers(void);

	// decompressor  packet lengths
	bool readPLT(uint8_t *p_header_data, uint16_t header_size);
	bool readPLM(uint8_t *p_header_data, uint16_t header_size);
	void getInit(void);
	uint32_t getNext(void);

	// compressor packet lengths
	void writeInit(void);
	void writeNext(uint32_t len);
	uint32_t write();
private:
	void readInitIndex(uint8_t index);
	void readNext(uint8_t Iplm);
	void writeMarkerHeader(void);
	void writeMarkerLength();
	void writeIncrement(uint32_t bytes);

	PL_MAP *m_markers;
	uint8_t m_markerIndex;
	PL_INFO_VEC *m_curr_vec;
	size_t m_packetIndex;
	uint32_t m_packet_len;
	uint32_t m_marker_bytes_written;
	uint32_t m_total_bytes_written;
	uint64_t m_marker_len_cache;
	BufferedStream *m_stream;
};

}
