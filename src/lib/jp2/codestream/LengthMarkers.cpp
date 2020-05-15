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

#include "grok_includes.h"

namespace grk {

TileLengthMarkers::TileLengthMarkers() :
		markers(new TL_MAP()) {
}

TileLengthMarkers::~TileLengthMarkers() {
	if (markers) {
		for (auto it = markers->begin(); it != markers->end(); it++) {
			delete it->second;
		}
		delete markers;
	}
}
void TileLengthMarkers::push(uint8_t i_TLM, grk_tl_info info) {
	auto pair = markers->find(i_TLM);
	if (pair != markers->end()) {
		pair->second->push_back(info);
	} else {
		auto vec = new TL_INFO_VEC();
		vec->push_back(info);
		markers->operator[](i_TLM) = vec;
	}
}

PacketLengthMarkers::PacketLengthMarkers() :
		m_markers(new PL_MAP()), m_Zpl(0), m_curr_vec(nullptr), m_packet_len(0), m_read_index(
				0U), m_marker_bytes_written(0), m_total_bytes_written(0), m_marker_len_cache(
				nullptr), m_write_ptr(nullptr), m_available_bytes(0) {
}

PacketLengthMarkers::~PacketLengthMarkers() {
	if (m_markers) {
		for (auto it = m_markers->begin(); it != m_markers->end(); it++)
			delete it->second;
		delete m_markers;
	}
}

void PacketLengthMarkers::encodeInit(uint8_t *tile_header_cache,
		size_t available_bytes) {
	assert(tile_header_cache && available_bytes);
	m_write_ptr = tile_header_cache;
	m_available_bytes = available_bytes;
	decodeInitIndex(0);
	m_total_bytes_written = 0;
	m_marker_bytes_written = 0;
	m_marker_len_cache = nullptr;
}

void PacketLengthMarkers::encodeNext(uint32_t len) {
	assert(len);
	m_curr_vec->push_back(len);
}
void PacketLengthMarkers::write_increment(size_t bytes) {
	m_marker_bytes_written += bytes;
	m_total_bytes_written += bytes;
	m_write_ptr += bytes;
	assert(m_total_bytes_written <= m_available_bytes);
}
// check if we need to start a new marker
void PacketLengthMarkers::write_marker_header() {
	// 5 bytes worst-case to store a packet length
	if (m_total_bytes_written == 0
			|| (m_marker_bytes_written >= available_packet_len_bytes_per_plt - 5)) {

		// write marker bytes (not including 2 bytes for marker itself)
		if (m_marker_bytes_written && m_marker_len_cache)
			grk_write<uint16_t>(m_marker_len_cache,
					(uint16_t) (m_marker_bytes_written - 2));
		m_marker_len_cache = nullptr;
		m_marker_bytes_written = 0;

		// write marker
		grk_write<uint16_t>(m_write_ptr, J2K_MS_PLT);
		write_increment(2);

		// cache location of marker length and skip over
		m_marker_len_cache = m_write_ptr;
		write_increment(2);
	}
}
void PacketLengthMarkers::write() {
	write_marker_header();
	for (auto map_iter = m_markers->begin(); map_iter != m_markers->end();
			++map_iter) {
		// write index
		*m_write_ptr = map_iter->first;
		write_increment(1);

		// write marker lengths
		for (auto val_iter = map_iter->second->begin();
				val_iter != map_iter->second->end(); ++val_iter) {
			//check if we need to start a new PLT marker segment
			write_marker_header();

			// write from MSB down to LSB
			uint32_t val = *val_iter;
			uint32_t numbits = uint_floorlog2(val) + 1;
			uint32_t numbytes = (numbits + 6) / 7;
			size_t counter = numbytes - 1;

			// write period
			*m_write_ptr = val & 0x7F;
			val = (uint32_t) (val >> 7);

			//write commas
			while (val) {
				m_write_ptr[counter--] = (uint8_t) ((val & 0x7F) | 0x80);
				val = (uint32_t) (val >> 7);
			}
			assert(counter == 0);
			write_increment(numbytes);
		}
	}
}

void PacketLengthMarkers::decodeInitIndex(uint8_t index) {
	m_Zpl = index;
	m_packet_len = 0;
	auto pair = m_markers->find(m_Zpl);
	if (pair != m_markers->end()) {
		m_curr_vec = pair->second;
	} else {
		m_curr_vec = new PL_INFO_VEC();
		m_markers->operator[](m_Zpl) = m_curr_vec;
	}
}

void PacketLengthMarkers::decodeNext(uint8_t Iplm) {
	/* take only the lower seven bits */
	m_packet_len |= (Iplm & 0x7f);
	if (Iplm & 0x80) {
		m_packet_len <<= 7;
	} else {
		assert(m_curr_vec);
		m_curr_vec->push_back(m_packet_len);
		//GROK_INFO("Packet length: (%d, %d)", Zpl, packet_len);
		m_packet_len = 0;
	}
}

bool PacketLengthMarkers::decodeHasPendingPacketLength() {
	return m_packet_len != 0;
}

void PacketLengthMarkers::readInit(void) {
	m_read_index = 0;
	m_Zpl = 0;
	m_curr_vec = nullptr;
	if (m_markers) {
		auto pair = m_markers->find(m_Zpl);
		if (pair != m_markers->end()) {
			m_curr_vec = pair->second;
		}
	}
}

// note: packet length must be at least 1, so 0 indicates
// no packet length available
uint32_t PacketLengthMarkers::readNext(void) {
	if (!m_markers)
		return 0;
	if (m_curr_vec) {
		if (m_read_index == m_curr_vec->size()) {
			m_Zpl++;
			if (m_Zpl < m_markers->size()) {
				m_curr_vec = m_markers->operator[](m_Zpl);
				m_read_index = 0;
			} else {
				m_curr_vec = nullptr;
			}
		}
		if (m_curr_vec)
			return m_curr_vec->operator[](m_read_index++);
	}
	return 0;
}

}
