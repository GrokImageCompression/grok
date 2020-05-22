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
 */

#pragma once

namespace grk {

// tile part length
struct grk_tl_info {
	grk_tl_info() :
			has_tile_number(false), tile_number(0), length(0) {
	}
	grk_tl_info(uint32_t len) :
			has_tile_number(false), tile_number(0), length(len) {
	}
	grk_tl_info(uint16_t tileno, uint32_t len) :
			has_tile_number(true), tile_number(tileno), length(len) {
	}
	bool has_tile_number;
	uint16_t tile_number;
	uint32_t length;

};

typedef std::vector<grk_tl_info> TL_INFO_VEC;
// map of (TLM marker id) => (tile part length vector)
typedef std::map<uint8_t, TL_INFO_VEC*> TL_MAP;

struct TileLengthMarkers {
	TileLengthMarkers();
	TileLengthMarkers(BufferedStream *stream);
	~TileLengthMarkers();

	bool read(uint8_t *p_header_data, uint16_t header_size);
	void getInit(void);
	grk_tl_info getNext(void);

	bool write_begin(uint16_t totalTileParts);
	void write_update(uint16_t tileIndex, uint32_t tile_part_size);
	bool write_end(void);
	/**
	 Add tile header marker information
	 @param tileno       tile index number
	 @param cstr_index   Codestream information structure
	 @param type         marker type
	 @param pos          byte offset of marker segment
	 @param len          length of marker segment
	 */
	static bool add_to_index(uint16_t tileno, grk_codestream_index *cstr_index,
			uint32_t type, uint64_t pos, uint32_t len);
private:
	void push(uint8_t i_TLM, grk_tl_info curr_vec);
	TL_MAP *m_markers;
	uint8_t m_markerIndex;
	uint8_t m_tilePartIndex;
	TL_INFO_VEC *m_curr_vec;
	BufferedStream *m_stream;
	uint64_t m_tlm_start_stream_position;

};

// bytes available in PLT marker to store packet lengths
// (4 bytes are reserved for (marker + marker length), and 1 byte for index)
const uint32_t available_packet_len_bytes_per_plt = USHRT_MAX - 1 - 4;

// minimum number of packet lengths that can be stored in a full
// length PLT marker
// (5 is maximum number of bytes for a single packet length)
const uint32_t min_packets_per_full_plt = available_packet_len_bytes_per_plt / 5;

typedef std::vector<uint32_t> PL_INFO_VEC;
// map of (PLT/PLM marker id) => (packet length vector)
typedef std::map<uint8_t, PL_INFO_VEC*> PL_MAP;

struct PacketLengthMarkers {
	PacketLengthMarkers(void);
	PacketLengthMarkers(BufferedStream *strm);
	~PacketLengthMarkers(void);

	// decode packet lengths
	bool readPLT(uint8_t *p_header_data, uint16_t header_size);
	bool readPLM(uint8_t *p_header_data, uint16_t header_size);

	// get decoded packet lengths
	void getInit(void);
	uint32_t getNext(void);

	// encode packet lengths
	void writeInit(void);
	void writeNext(uint32_t len);
	// write marker to stream
	uint32_t write();

private:
	PL_MAP *m_markers;
	uint8_t m_markerIndex;
	PL_INFO_VEC *m_curr_vec;
	size_t m_packetIndex;
	uint32_t m_packet_len;

	void readInitIndex(uint8_t index);
	void readNext(uint8_t Iplm);

	void write_marker_header(void);
	void write_marker_length();
	void write_increment(uint32_t bytes);
	uint32_t m_marker_bytes_written;
	uint32_t m_total_bytes_written;
	uint64_t m_marker_len_cache;
	BufferedStream *m_stream;
};

}

