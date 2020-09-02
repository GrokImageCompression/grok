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

#include "grk_includes.h"

namespace grk {

// TLM(2) + Ltlm(2) + Ztlm(1) + Stlm(1)
const uint32_t tlm_marker_start_bytes = 6;

TileLengthMarkers::TileLengthMarkers() :
		m_markers(new TL_MAP()),
		m_markerIndex(0),
		m_tilePartIndex(0),
		m_curr_vec(nullptr),
		m_stream(nullptr),
		m_tlm_start_stream_position(0)
{
}

TileLengthMarkers::TileLengthMarkers(BufferedStream *stream): TileLengthMarkers()
{
	m_stream = stream;
}

TileLengthMarkers::~TileLengthMarkers() {
	if (m_markers) {
		for (auto it = m_markers->begin(); it != m_markers->end(); it++) {
			delete it->second;
		}
		delete m_markers;
	}
}
bool TileLengthMarkers::read(uint8_t *p_header_data, uint16_t header_size){
	if (header_size < tlm_marker_start_bytes) {
		GRK_ERROR("Error reading TLM marker");
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
		GRK_ERROR("Illegal L value in TLM marker");
		return false;
	}
	/*
	 * 0 <= L_LTP <= 1
	 *
	 * 0 => 16 bit tile part lengths
	 * 1 => 32 bit tile part lengths
	 */
	L_LTP = (L >> 6) & 0x1;
	uint32_t bytes_per_tile_part_length = L_LTP ? 4U : 2U;
	/*
	* 0 <= L_iT <= 2
	*
	* 0 => no tile part indices
	* 1 => 1 byte tile part indices
	* 2 => 2 byte tile part indices
	*/
	L_iT = ((L >> 4) & 0x3);
	uint32_t quotient = bytes_per_tile_part_length + L_iT;
	if (header_size % quotient != 0) {
		GRK_ERROR("Error reading TLM marker");
		return false;
	}
	// note: each tile can have max 255 tile parts, but
	// the whole image with multiple tiles can have more than
	// 255
	size_t num_tp = (uint8_t) (header_size / quotient);

	uint32_t Ttlm_i = 0, Ptlm_i = 0;
	for (size_t i = 0; i < num_tp; ++i) {
		// read (global) tile part index
		if (L_iT) {
			grk_read<uint32_t>(p_header_data, &Ttlm_i, L_iT);
			p_header_data += L_iT;
		}
		// read tile part length
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
	auto pair = m_markers->find(i_TLM);

	if (pair != m_markers->end()) {
		pair->second->push_back(info);
	} else {
		auto vec = new TL_INFO_VEC();
		vec->push_back(info);
		m_markers->operator[](i_TLM) = vec;
	}
}

void TileLengthMarkers::getInit(void){
	m_markerIndex = 0;
	m_tilePartIndex = 0;
	m_curr_vec = nullptr;
	if (m_markers) {
		auto pair = m_markers->find(0);
		if (pair != m_markers->end())
			m_curr_vec = pair->second;
	}
}
grk_tl_info TileLengthMarkers::getNext(void){
	if (!m_markers)
		return 0;
	if (m_curr_vec) {
		if (m_tilePartIndex == m_curr_vec->size()) {
			m_markerIndex++;
			if (m_markerIndex < m_markers->size()) {
				m_curr_vec = m_markers->operator[](m_markerIndex);
				m_tilePartIndex = 0;
			} else {
				m_curr_vec = nullptr;
			}
		}
		if (m_curr_vec)
			return m_curr_vec->operator[](m_tilePartIndex++);
	}
	return 0;
}

bool TileLengthMarkers::write_begin(uint16_t totalTileParts) {
	uint32_t tlm_size = tlm_marker_start_bytes 	+ tlm_len_per_tile_part * totalTileParts;

	m_tlm_start_stream_position = m_stream->tell();

	/* TLM */
	if (!m_stream->write_short(J2K_MS_TLM))
		return false;

	/* Ltlm */
	if (!m_stream->write_short((uint16_t) (tlm_size - 2)))
		return false;

	/* Ztlm=0*/
	if (!m_stream->write_byte(0))
		return false;

	/* Stlm ST=1(8bits-255 tiles max),SP=1(Ptlm=32bits) */
	if (!m_stream->write_byte(0x50))
		return false;

	/* make room for tile part lengths */
	return m_stream->skip(tlm_len_per_tile_part	* totalTileParts);
}

void TileLengthMarkers::write_update(uint16_t tileIndex, uint32_t tile_part_size) {
	assert(tileIndex <= 255);
    push(m_markerIndex,	grk_tl_info((uint8_t)tileIndex,	tile_part_size));
}

bool TileLengthMarkers::write_end(void) {
    uint64_t tlm_position = m_tlm_start_stream_position+ tlm_marker_start_bytes;
	uint64_t current_position = m_stream->tell();

	if (!m_stream->seek(tlm_position))
		return false;

	for (auto it = m_markers->begin(); it != m_markers->end(); it++) {
		auto lengths = it->second;
		for (auto info = lengths->begin(); info != lengths->end(); ++info ){
			if (info->has_tile_number) {
				assert(info->tile_number <= 255);
				m_stream->write_byte((uint8_t)info->tile_number);
			}
			m_stream->write_int(info->length);
		}
	}

	return m_stream->seek(current_position);
}


bool TileLengthMarkers::add_to_index(uint16_t tileno, grk_codestream_index *cstr_index,
		uint32_t type, uint64_t pos, uint32_t len) {
	assert(cstr_index != nullptr);
	assert(cstr_index->tile_index != nullptr);

	/* expand the list? */
	if ((cstr_index->tile_index[tileno].marknum + 1)
			> cstr_index->tile_index[tileno].maxmarknum) {
		grk_marker_info *new_marker;
		cstr_index->tile_index[tileno].maxmarknum = (uint32_t) (100
				+ (float) cstr_index->tile_index[tileno].maxmarknum);
		new_marker = (grk_marker_info*) grk_realloc(
				cstr_index->tile_index[tileno].marker,
				cstr_index->tile_index[tileno].maxmarknum
						* sizeof(grk_marker_info));
		if (!new_marker) {
			grk_free(cstr_index->tile_index[tileno].marker);
			cstr_index->tile_index[tileno].marker = nullptr;
			cstr_index->tile_index[tileno].maxmarknum = 0;
			cstr_index->tile_index[tileno].marknum = 0;
			GRK_ERROR("Not enough memory to add tl marker");
			return false;
		}
		cstr_index->tile_index[tileno].marker = new_marker;
	}

	/* add the marker */
	cstr_index->tile_index[tileno].marker[cstr_index->tile_index[tileno].marknum].type =
			(uint16_t) type;
	cstr_index->tile_index[tileno].marker[cstr_index->tile_index[tileno].marknum].pos =
			pos;
	cstr_index->tile_index[tileno].marker[cstr_index->tile_index[tileno].marknum].len =
			(uint32_t) len;
	cstr_index->tile_index[tileno].marknum++;

	if (type == J2K_MS_SOT) {
		uint32_t current_tile_part =
				cstr_index->tile_index[tileno].current_tpsno;

		if (cstr_index->tile_index[tileno].tp_index)
			cstr_index->tile_index[tileno].tp_index[current_tile_part].start_pos =
					pos;

	}
	return true;
}


PacketLengthMarkers::PacketLengthMarkers() :
		m_markers(new PL_MAP()),
		m_markerIndex(0),
		m_curr_vec(nullptr),
		m_packetIndex(0),
		m_packet_len(0),
		m_marker_bytes_written(0),
		m_total_bytes_written(0),
		m_marker_len_cache(0),
		m_stream(nullptr)
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
void PacketLengthMarkers::write_increment(uint32_t bytes) {
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
uint32_t PacketLengthMarkers::write() {
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
			uint32_t numbits = floorlog2<uint32_t>(val) + 1;
			uint32_t numbytes = (numbits + 6) / 7;
			assert(numbytes <= 5);

			// write period
			uint8_t temp[5];
			int32_t counter = (int32_t)(numbytes - 1);
			temp[counter--] = (val & 0x7F);
			val = (uint32_t) (val >> 7);

			//write commas (backwards from LSB to MSB)
			while (val) {
				uint8_t b = (uint8_t) ((val & 0x7F) | 0x80);
				temp[counter--] = b;
				val = (uint32_t) (val >> 7);
			}
			assert(counter == -1);
			uint32_t written = (uint32_t)m_stream->write_bytes(temp, numbytes);
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
		GRK_ERROR("PLM marker segment too short");
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
			GRK_ERROR("Malformed PLM marker segment");
			return false;
		}
		for (uint32_t i = 0; i < Nplm; ++i) {
			uint8_t tmp = *p_header_data;
			++p_header_data;
			readNext(tmp);
		}
		header_size = (uint16_t)(header_size - (1 + Nplm));
		if (m_packet_len != 0) {
			GRK_ERROR("Malformed PLM marker segment");
			return false;
		}
	}
	return true;
}

bool PacketLengthMarkers::readPLT(uint8_t *p_header_data, uint16_t header_size){
	if (header_size < 1) {
		GRK_ERROR("PLT marker segment too short");
		return false;
	}

	/* Zplt */
	uint8_t Zpl;
	Zpl = *p_header_data++;
	--header_size;

	readInitIndex(Zpl);
	for (uint32_t i = 0; i < header_size; ++i) {
		/* Iplt_ij */
		uint8_t tmp = *p_header_data++;
		readNext(tmp);
	}
	if (m_packet_len != 0) {
		GRK_ERROR("Malformed PLT marker segment");
		return false;
	}
	return true;
}

void PacketLengthMarkers::readInitIndex(uint8_t index) {
	m_markerIndex = index;
	m_packet_len = 0;
	auto pair = m_markers->find(m_markerIndex);
	if (pair != m_markers->end()) {
		m_curr_vec = pair->second;
	} else {
		m_curr_vec = new PL_INFO_VEC();
		m_markers->operator[](m_markerIndex) = m_curr_vec;
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
		//GRK_INFO("Packet length: (%u, %u)", Zpl, packet_len);
		m_packet_len = 0;
	}
}

void PacketLengthMarkers::getInit(void) {
	m_packetIndex = 0;
	m_markerIndex = 0;
	m_curr_vec = nullptr;
	if (m_markers) {
		auto pair = m_markers->find(0);
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
		if (m_packetIndex == m_curr_vec->size()) {
			m_markerIndex++;
			if (m_markerIndex < m_markers->size()) {
				m_curr_vec = m_markers->operator[](m_markerIndex);
				m_packetIndex = 0;
			} else {
				m_curr_vec = nullptr;
			}
		}
		if (m_curr_vec)
			return m_curr_vec->operator[](m_packetIndex++);
	}
	return 0;
}

}
