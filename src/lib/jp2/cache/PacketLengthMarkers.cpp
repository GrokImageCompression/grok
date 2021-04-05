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

#include "grk_includes.h"

namespace grk
{
// bytes available in PLT marker to store packet lengths
// (4 bytes are reserved for (marker + marker length), and 1 byte for index)
const uint32_t available_packet_len_bytes_per_plt = USHRT_MAX - 1 - 4;

// minimum number of packet lengths that can be stored in a full
// length PLT marker
// (5 is maximum number of bytes for a single packet length)
const uint32_t min_packets_per_full_plt = available_packet_len_bytes_per_plt / 5;

PacketLengthMarkers::PacketLengthMarkers()
	: m_markers(new PL_MAP()), m_markerIndex(0), m_curr_vec(nullptr), m_packetIndex(0),
	  m_packet_len(0), m_marker_bytes_written(0), m_total_bytes_written(0), m_marker_len_cache(0),
	  m_stream(nullptr)
{}
PacketLengthMarkers::PacketLengthMarkers(IBufferedStream* strm) : PacketLengthMarkers()
{
	m_stream = strm;
	writeInit();
}
PacketLengthMarkers::~PacketLengthMarkers()
{
	if(m_markers)
	{
		for(auto it = m_markers->begin(); it != m_markers->end(); it++)
			delete it->second;
		delete m_markers;
	}
}
void PacketLengthMarkers::writeInit(void)
{
	readInitIndex(0);
	m_total_bytes_written = 0;
	m_marker_bytes_written = 0;
	m_marker_len_cache = 0;
}
void PacketLengthMarkers::writeNext(uint32_t len)
{
	assert(len);
	m_curr_vec->push_back(len);
}
void PacketLengthMarkers::writeIncrement(uint32_t bytes)
{
	m_marker_bytes_written += bytes;
	m_total_bytes_written += bytes;
}
void PacketLengthMarkers::writeMarkerLength()
{
	// write marker length
	if(m_marker_len_cache)
	{
		uint64_t current_pos = m_stream->tell();
		m_stream->seek(m_marker_len_cache);
		// do not include 2 bytes for marker itself
		m_stream->writeShort((uint16_t)(m_marker_bytes_written - 2));
		m_stream->seek(current_pos);
		m_marker_len_cache = 0;
		m_marker_bytes_written = 0;
	}
	assert(m_marker_bytes_written == 0);
}
// check if we need to start a new marker
void PacketLengthMarkers::writeMarkerHeader()
{
	// 5 bytes worst-case to store a packet length
	if(m_total_bytes_written == 0 ||
	   (m_marker_bytes_written >= available_packet_len_bytes_per_plt - 5))
	{
		// complete current marker
		writeMarkerLength();

		// begin new marker
		m_stream->writeShort(J2K_MS_PLT);
		writeIncrement(2);

		// cache location of marker length and skip over
		m_marker_len_cache = m_stream->tell();
		m_stream->skip(2);
		writeIncrement(2);
	}
}
uint32_t PacketLengthMarkers::write()
{
	writeMarkerHeader();
	for(auto map_iter = m_markers->begin(); map_iter != m_markers->end(); ++map_iter)
	{
		// write index
		m_stream->writeByte(map_iter->first);
		writeIncrement(1);

		// write marker lengths
		for(auto val_iter = map_iter->second->begin(); val_iter != map_iter->second->end();
			++val_iter)
		{
			// check if we need to start a new PLT marker segment
			writeMarkerHeader();

			// write from MSB down to LSB
			uint32_t val = *val_iter;
			uint32_t numbits = floorlog2(val) + 1;
			uint32_t numbytes = (numbits + 6) / 7;
			assert(numbytes <= 5);

			// write period
			uint8_t temp[5];
			int32_t counter = (int32_t)(numbytes - 1);
			temp[counter--] = (val & 0x7F);
			val = (uint32_t)(val >> 7);

			// write commas (backwards from LSB to MSB)
			while(val)
			{
				uint8_t b = (uint8_t)((val & 0x7F) | 0x80);
				temp[counter--] = b;
				val = (uint32_t)(val >> 7);
			}
			assert(counter == -1);
			uint32_t written = (uint32_t)m_stream->writeBytes(temp, numbytes);
			assert(written == numbytes);
			(void)written;
			writeIncrement(numbytes);
		}
	}
	// write final marker length
	writeMarkerLength();
	return m_total_bytes_written;
}

bool PacketLengthMarkers::readPLM(uint8_t* p_header_data, uint16_t header_size)
{
	if(header_size < 1)
	{
		GRK_ERROR("PLM marker segment too short");
		return false;
	}
	// Zplm
	uint8_t Zplm = *p_header_data++;
	--header_size;
	readInitIndex(Zplm);
	while(header_size > 0)
	{
		// Nplm
		uint8_t Nplm = *p_header_data++;
		if(header_size < (1 + Nplm))
		{
			GRK_ERROR("Malformed PLM marker segment");
			return false;
		}
		for(uint32_t i = 0; i < Nplm; ++i)
		{
			uint8_t tmp = *p_header_data;
			++p_header_data;
			readNext(tmp);
		}
		header_size = (uint16_t)(header_size - (1 + Nplm));
		if(m_packet_len != 0)
		{
			GRK_ERROR("Malformed PLM marker segment");
			return false;
		}
	}
	return true;
}
bool PacketLengthMarkers::readPLT(uint8_t* p_header_data, uint16_t header_size)
{
	if(header_size < 1)
	{
		GRK_ERROR("PLT marker segment too short");
		return false;
	}

	/* Zplt */
	uint8_t Zpl = *p_header_data++;
	--header_size;
	readInitIndex(Zpl);
	for(uint32_t i = 0; i < header_size; ++i)
	{
		/* Iplt_ij */
		uint8_t tmp = *p_header_data++;
		readNext(tmp);
	}
	if(m_packet_len != 0)
	{
		GRK_ERROR("Malformed PLT marker segment");
		return false;
	}
	return true;
}
void PacketLengthMarkers::readInitIndex(uint8_t index)
{
	m_markerIndex = index;
	m_packet_len = 0;
	auto pair = m_markers->find(m_markerIndex);
	if(pair != m_markers->end())
	{
		m_curr_vec = pair->second;
	}
	else
	{
		m_curr_vec = new PL_INFO_VEC();
		m_markers->operator[](m_markerIndex) = m_curr_vec;
	}
}
void PacketLengthMarkers::readNext(uint8_t Iplm)
{
	/* take only the lower seven bits */
	m_packet_len |= (Iplm & 0x7f);
	if(Iplm & 0x80)
	{
		m_packet_len <<= 7;
	}
	else
	{
		assert(m_curr_vec);
		m_curr_vec->push_back(m_packet_len);
		// GRK_INFO("Packet length: (%u, %u)", Zpl, packet_len);
		m_packet_len = 0;
	}
}
void PacketLengthMarkers::rewind(void)
{
	m_packetIndex = 0;
	m_markerIndex = 0;
	m_curr_vec = nullptr;
	if(m_markers)
	{
		auto pair = m_markers->find(0);
		if(pair != m_markers->end())
		{
			m_curr_vec = pair->second;
		}
	}
}
// note: packet length must be at least 1, so 0 indicates
// no packet length available
uint32_t PacketLengthMarkers::getNext(void)
{
	if(!m_markers)
		return 0;
	if(m_curr_vec)
	{
		if(m_packetIndex == m_curr_vec->size())
		{
			m_markerIndex++;
			if(m_markerIndex < m_markers->size())
			{
				m_curr_vec = m_markers->operator[](m_markerIndex);
				m_packetIndex = 0;
			}
			else
			{
				m_curr_vec = nullptr;
			}
		}
		if(m_curr_vec)
			return m_curr_vec->operator[](m_packetIndex++);
	}
	return 0;
}

} // namespace grk
