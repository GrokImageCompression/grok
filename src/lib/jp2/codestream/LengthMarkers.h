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

// bytes available in PLT marker to store packet lengths
// (4 bytes are reserved for (marker + marker length), and 1 byte for index)
const uint32_t available_packet_len_bytes_per_plt = USHRT_MAX - 1 - 4;

// minimum number of packet lengths that can be stored in a full
// length PLT marker
// (5 is maximum number of bytes for a single packet length)
const uint32_t min_packets_per_full_plt = available_packet_len_bytes_per_plt / 5;

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
	~TileLengthMarkers();
	void push(uint8_t i_TLM, grk_tl_info curr_vec);
	TL_MAP *markers;
};

typedef std::vector<uint32_t> PL_INFO_VEC;
// map of (PLT/PLM marker id) => (packet length vector)
typedef std::map<uint8_t, PL_INFO_VEC*> PL_MAP;

struct PacketLengthMarkers {
	PacketLengthMarkers(void);
	PacketLengthMarkers(BufferedStream *strm);
	~PacketLengthMarkers(void);

	// decode packet lengths
	void decodeInitIndex(uint8_t index);
	void decodeNext(uint8_t Iplm);
	bool decodeHasPendingPacketLength();
	void readInit(void);
	uint32_t readNext(void);

	// encode packet lengths
	void encodeInit(void);
	void encodeNext(uint32_t len);
	size_t write();

private:
	PL_MAP *m_markers;
	uint8_t m_Zpl;
	PL_INFO_VEC *m_curr_vec;
	uint32_t m_packet_len;
	size_t m_read_index;

	void write_marker_header(void);
	void write_marker_length();
	void write_increment(size_t bytes);
	size_t m_marker_bytes_written;
	size_t m_total_bytes_written;
	uint64_t m_marker_len_cache;
	BufferedStream *m_stream;
};

}

