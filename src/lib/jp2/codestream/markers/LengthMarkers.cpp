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
bool TileLengthMarkers::read(uint8_t *p_header_data, uint16_t header_size){
	if (header_size < 2) {
		GROK_ERROR("Error reading TLM marker");
		return false;
	}
	uint8_t i_TLM, L;
	uint32_t L_iT, L_LTP;

	// correct for length of marker
	header_size = (uint16_t) (header_size - 2);
	// read TLM marker segment index
	i_TLM = *p_header_data++;
	// read and parse L parameter, which indicates number of bytes used to represent
	// remaining parameters
	L = *p_header_data++;
	// 0x70 ==  1110000
	if ((L & ~0x70) != 0) {
		GROK_ERROR("Illegal L value in TLM marker");
		return false;
	}
	L_iT = ((L >> 4) & 0x3);	// 0 <= L_iT <= 2
	L_LTP = (L >> 6) & 0x1;		// 0 <= L_iTP <= 1

	uint32_t bytes_per_tile_part_length = L_LTP ? 4U : 2U;
	uint32_t quotient = bytes_per_tile_part_length + L_iT;
	if (header_size % quotient != 0) {
		GROK_ERROR("Error reading TLM marker");
		return false;
	}
	uint8_t num_tp = (uint8_t) (header_size / quotient);
	uint32_t Ttlm_i = 0, Ptlm_i = 0;
	for (uint8_t i = 0; i < num_tp; ++i) {
		if (L_iT) {
			grk_read<uint32_t>(p_header_data, &Ttlm_i, L_iT);
			p_header_data += L_iT;
		}
		grk_read<uint32_t>(p_header_data, &Ptlm_i, bytes_per_tile_part_length);
		auto info =
				L_iT ? grk_tl_info((uint16_t) Ttlm_i, Ptlm_i) : grk_tl_info(
								Ptlm_i);
		push(i_TLM, info);
		p_header_data += bytes_per_tile_part_length;
	}
	return true;
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
				0U), m_marker_bytes_written(0), m_total_bytes_written(0),
				m_marker_len_cache(0), m_stream(nullptr)
{ }
PacketLengthMarkers::PacketLengthMarkers(BufferedStream *strm) : PacketLengthMarkers()
{
	m_stream = strm;
	writeInit();
}


PacketLengthMarkers::~PacketLengthMarkers() {
	if (m_markers) {
		for (auto it = m_markers->begin(); it != m_markers->end(); it++)
			delete it->second;
		delete m_markers;
	}
}

void PacketLengthMarkers::writeInit(void) {
	readInitIndex(0);
	m_total_bytes_written = 0;
	m_marker_bytes_written = 0;
	m_marker_len_cache = 0;
}

void PacketLengthMarkers::writeNext(uint32_t len) {
	assert(len);
	m_curr_vec->push_back(len);
}
void PacketLengthMarkers::write_increment(size_t bytes) {
	m_marker_bytes_written += bytes;
	m_total_bytes_written += bytes;
}
void PacketLengthMarkers::write_marker_length(){
	// write marker length
	if (m_marker_len_cache){
		uint64_t current_pos = m_stream->tell();
		m_stream->seek(m_marker_len_cache);
		//do not include 2 bytes for marker itself
		m_stream->write_short((uint16_t) (m_marker_bytes_written - 2));
		m_stream->seek(current_pos);
		m_marker_len_cache = 0;
		m_marker_bytes_written = 0;
	}
	assert(m_marker_bytes_written == 0);
}
// check if we need to start a new marker
void PacketLengthMarkers::write_marker_header() {
	// 5 bytes worst-case to store a packet length
	if (m_total_bytes_written == 0
			|| (m_marker_bytes_written >= available_packet_len_bytes_per_plt - 5)) {

		// complete current marker
		write_marker_length();

		// begin new marker
		m_stream->write_short(J2K_MS_PLT);
		write_increment(2);

		// cache location of marker length and skip over
		m_marker_len_cache = m_stream->tell();
		m_stream->skip(2);
		write_increment(2);
	}
}
size_t PacketLengthMarkers::write() {
	write_marker_header();
	for (auto map_iter = m_markers->begin(); map_iter != m_markers->end();
			++map_iter) {

		// write index
		m_stream->write_byte(map_iter->first);
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
			assert(numbytes <= 5);

			// write period
			uint8_t temp[5];
			int32_t counter = numbytes - 1;
			temp[counter--] = (val & 0x7F);
			val = (uint32_t) (val >> 7);

			//write commas (backwards from LSB to MSB)
			while (val) {
				uint8_t b = (uint8_t) ((val & 0x7F) | 0x80);
				temp[counter--] = b;
				val = (uint32_t) (val >> 7);
			}
			assert(counter == -1);
			size_t written = m_stream->write_bytes(temp, numbytes);
			assert(written == numbytes);
			(void)written;
			write_increment(numbytes);
		}
	}
	// write final marker length
	write_marker_length();
	return m_total_bytes_written;
}

bool PacketLengthMarkers::readPLM(uint8_t *p_header_data, uint16_t header_size){
	if (header_size < 1) {
		GROK_ERROR("PLM marker segment too short");
		return false;
	}
	// Zplm
	uint8_t Zplm = *p_header_data++;
	--header_size;
	readInitIndex(Zplm);
	while (header_size > 0) {
		// Nplm
		uint8_t Nplm = *p_header_data++;
		if (header_size < (1 + Nplm)) {
			GROK_ERROR("Malformed PLM marker segment");
			return false;
		}
		for (uint32_t i = 0; i < Nplm; ++i) {
			uint8_t tmp = *p_header_data;
			++p_header_data;
			readNext(tmp);
		}
		header_size = (uint16_t)(header_size - (1 + Nplm));
		if (m_packet_len != 0) {
			GROK_ERROR("Malformed PLM marker segment");
			return false;
		}
	}
	return true;
}

bool PacketLengthMarkers::readPLT(uint8_t *p_header_data, uint16_t header_size){
	if (header_size < 1) {
		GROK_ERROR("PLT marker segment too short");
		return false;
	}

	/* Zplt */
	uint8_t Zpl;
	Zpl = *p_header_data++;
	--header_size;

	uint8_t tmp;
	readInitIndex(Zpl);
	for (uint32_t i = 0; i < header_size; ++i) {
		/* Iplt_ij */
		tmp = *p_header_data++;
		readNext(tmp);
	}
	if (m_packet_len != 0) {
		GROK_ERROR("Malformed PLT marker segment");
		return false;
	}
	return true;
}

void PacketLengthMarkers::readInitIndex(uint8_t index) {
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

void PacketLengthMarkers::readNext(uint8_t Iplm) {
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

void PacketLengthMarkers::getInit(void) {
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
uint32_t PacketLengthMarkers::getNext(void) {
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
