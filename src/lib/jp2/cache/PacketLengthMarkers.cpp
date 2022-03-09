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

#include "grk_includes.h"

namespace grk
{
// bytes available in PLT marker to store packet lengths
// (4 bytes are reserved for (marker + marker length), and 1 byte for index)
const uint32_t available_packet_len_bytes_per_plt = USHRT_MAX - 1 - 4;

// minimum number of packet lengths that can be stored in a full
// length PLT marker
// (5 is maximum number of bytes for a single packet length)
// const uint32_t min_packets_per_full_plt = available_packet_len_bytes_per_plt / 5;

PacketLengthMarkers::PacketLengthMarkers()
	: markers_(new PL_MARKERS()), markerIndex_(0), currMarker_(nullptr), packetIndex_(0), packet_len_(0),
	  markerBytesWritten_(0), totalBytesWritten_(0), marker_len_cache_(0), stream_(nullptr)
{}
PacketLengthMarkers::PacketLengthMarkers(IBufferedStream* strm) : PacketLengthMarkers()
{
	stream_ = strm;
	pushInit();
}
PacketLengthMarkers::~PacketLengthMarkers()
{
	if(markers_)
	{
		for(auto it = markers_->begin(); it != markers_->end(); it++)
			delete it->second.marker_;
		delete markers_;
	}
}
void PacketLengthMarkers::pushInit(void)
{
	markers_->clear();
	readInit(0);
	totalBytesWritten_ = 0;
	markerBytesWritten_ = 0;
	marker_len_cache_ = 0;
}
void PacketLengthMarkers::pushNextPacketLength(uint32_t len)
{
	assert(len);
	// GRK_INFO("Push packet length: %d", len);
	currMarker_->push_back(len);
}
void PacketLengthMarkers::writeIncrement(uint32_t bytes)
{
	markerBytesWritten_ += bytes;
	totalBytesWritten_ += bytes;
}
void PacketLengthMarkers::writeMarkerLength(PacketLengthMarkerInfo* markerInfo)
{
	if(!markerBytesWritten_)
		return;

	if(markerInfo)
	{
		markerInfo->markerLength_ = markerBytesWritten_;
	}
	else if(marker_len_cache_)
	{
		uint64_t current_pos = stream_->tell();
		stream_->seek(marker_len_cache_);
		// do not include 2 bytes for marker itself
		stream_->writeShort((uint16_t)(markerBytesWritten_ - 2));
		stream_->seek(current_pos);
		marker_len_cache_ = 0;
	}
}
// check if we need to start a new marker
void PacketLengthMarkers::tryWriteMarkerHeader(PacketLengthMarkerInfo* markerInfo, bool simulate)
{
	bool firstMarker = totalBytesWritten_ == 0;
	// 5 bytes worst-case to store a packet length
	if(firstMarker || (markerBytesWritten_ >= available_packet_len_bytes_per_plt - 5))
	{
		writeMarkerLength(simulate ? markerInfo : nullptr);

		// begin new marker
		markerBytesWritten_ = 0;
		if(!simulate)
			stream_->writeShort(J2K_MS_PLT);
		writeIncrement(2);

		if(!simulate)
		{
			if(markerInfo->markerLength_)
			{
				stream_->writeShort((uint16_t)(markerInfo->markerLength_ - 2));
			}
			else
			{
				// cache location of marker length and skip over
				marker_len_cache_ = stream_->tell();
				stream_->skip(2);
			}
		}
		writeIncrement(2);
	}
}
uint32_t PacketLengthMarkers::write(bool simulate)
{
	if(markers_->empty())
		return 0;

	totalBytesWritten_ = 0;
	markerBytesWritten_ = 0;
	// write first marker header
	tryWriteMarkerHeader(&markers_->begin()->second, simulate);
	PacketLengthMarkerInfo* finalMarkerInfo = nullptr;
	for(auto map_iter = markers_->begin(); map_iter != markers_->end(); ++map_iter)
	{
		// write marker index
		if(!simulate)
			stream_->writeByte(map_iter->first);
		writeIncrement(1);

		// write packet lengths
		for(auto val_iter = map_iter->second.marker_->begin();
			val_iter != map_iter->second.marker_->end(); ++val_iter)
		{
			// check if we need to start a new PLT marker
			tryWriteMarkerHeader(&map_iter->second, simulate);

			// write from MSB down to LSB
			uint32_t val = *val_iter;
			uint32_t numbits = floorlog2(val) + 1;
			uint32_t numBytes = (numbits + 6) / 7;
			assert(numBytes <= 5);

			if(!simulate)
			{
				// write period
				uint8_t temp[5];
				int32_t counter = (int32_t)(numBytes - 1);
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
				uint32_t written = (uint32_t)stream_->writeBytes(temp, numBytes);
				assert(written == numBytes);
				GRK_UNUSED(written);
			}
			writeIncrement(numBytes);
		}
		finalMarkerInfo = &map_iter->second;
	}
	if(!markers_->empty())
		writeMarkerLength(simulate ? finalMarkerInfo : nullptr);

	return totalBytesWritten_;
}

bool PacketLengthMarkers::readPLM(uint8_t* headerData, uint16_t header_size)
{
	if(header_size < 1)
	{
		GRK_ERROR("PLM marker segment too short");
		return false;
	}
	// Zplm
	uint8_t Zplm = *headerData++;
	--header_size;
	readInit(Zplm);
	while(header_size > 0)
	{
		// Nplm
		uint8_t Nplm = *headerData++;
		if(header_size < (1 + Nplm))
		{
			GRK_ERROR("Malformed PLM marker segment");
			return false;
		}
		for(uint32_t i = 0; i < Nplm; ++i)
		{
			uint8_t tmp = *headerData;
			++headerData;
			readNext(tmp);
		}
		header_size = (uint16_t)(header_size - (1 + Nplm));
		if(packet_len_ != 0)
		{
			GRK_ERROR("Malformed PLM marker segment");
			return false;
		}
	}
	return true;
}
bool PacketLengthMarkers::readPLT(uint8_t* headerData, uint16_t header_size)
{
	if(header_size < 1)
	{
		GRK_ERROR("PLT marker segment too short");
		return false;
	}

	/* Zplt */
	uint8_t Zpl = *headerData++;
	--header_size;
	readInit(Zpl);
	//GRK_INFO("%d",Zpl);
	for(uint32_t i = 0; i < header_size; ++i)
	{
		/* Iplt_ij */
		uint8_t tmp = *headerData++;
		readNext(tmp);
	}
	if(packet_len_ != 0)
	{
		GRK_ERROR("Malformed PLT marker segment");
		return false;
	}
	return true;
}
void PacketLengthMarkers::readInit(uint8_t index)
{
	markerIndex_ = index;
	packet_len_ = 0;
	auto pair = markers_->find(markerIndex_);
	if(pair != markers_->end())
	{
		currMarker_ = pair->second.marker_;
	}
	else
	{
		currMarker_ = new PL_MARKER();
		markers_->operator[](markerIndex_) = PacketLengthMarkerInfo(currMarker_);
	}
}
void PacketLengthMarkers::readNext(uint8_t Iplm)
{
	/* take only the lower seven bits */
	packet_len_ |= (Iplm & 0x7f);
	if(Iplm & 0x80)
	{
		packet_len_ <<= 7;
	}
	else
	{
		assert(currMarker_);
		currMarker_->push_back(packet_len_);
		// GRK_INFO("Packet length: %u", packet_len_);
		packet_len_ = 0;
	}
}
void PacketLengthMarkers::rewind(void)
{
	packetIndex_ = 0;
	markerIndex_ = 0;
	currMarker_ = nullptr;
	if(markers_)
	{
		auto pair = markers_->find(0);
		if(pair != markers_->end())
			currMarker_ = pair->second.marker_;
	}
}
// note: packet length must be at least 1, so 0 indicates
// no packet length available
uint32_t PacketLengthMarkers::popNextPacketLength(void)
{
	if(!markers_)
		return 0;
	uint32_t rc = 0;
	if(currMarker_)
	{
		if(packetIndex_ == currMarker_->size())
		{
			markerIndex_++;
			if(markerIndex_ < markers_->size())
			{
				currMarker_ = markers_->operator[](markerIndex_).marker_;
				packetIndex_ = 0;
			}
			else
			{
				currMarker_ = nullptr;
				GRK_WARN("Attempt to pop PLT length beyond PLT marker range.");
			}
		}
		if(currMarker_)
			rc = currMarker_->operator[](packetIndex_++);
	}
	// GRK_INFO("Read packet length: %d", rc);
	return rc;
}

} // namespace grk
