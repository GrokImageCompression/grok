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

// decompression
PacketLengthMarkers::PacketLengthMarkers()
	: markers_(new PL_MARKERS()), currMarker_(nullptr), markerBytesWritten_(0), totalBytesWritten_(0),
	  cachedMarkerLenLocation_(0), stream_(nullptr),
	  sequential_(false), packetLen_(0), rawMarkers_(new PL_RAW_MARKERS()), currRawMarkerIter_(rawMarkers_->end()),
	  currRawMarkerBufIndex_(0), currRawMarkerBuf_(nullptr)
{}
// compression
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
	if(rawMarkers_)
	{
		for(auto it = rawMarkers_->begin(); it != rawMarkers_->end(); it++){
			auto v = it->second;
			for (auto itv = v->begin(); itv != v->end(); ++itv)
				delete *itv;
			delete it->second;
		}
		delete rawMarkers_;
	}
}
void PacketLengthMarkers::pushInit(void)
{
	markers_->clear();

	currMarker_ = new PL_MARKER();
	markers_->operator[](0) = PacketLengthMarkerInfo(currMarker_);

	totalBytesWritten_ = 0;
	markerBytesWritten_ = 0;
	cachedMarkerLenLocation_ = 0;
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
	else if(cachedMarkerLenLocation_)
	{
		uint64_t current_pos = stream_->tell();
		stream_->seek(cachedMarkerLenLocation_);
		// do not include 2 bytes for marker itself
		stream_->writeShort((uint16_t)(markerBytesWritten_ - 2));
		stream_->seek(current_pos);
		cachedMarkerLenLocation_ = 0;
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
				cachedMarkerLenLocation_ = stream_->tell();
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
			stream_->writeByte((uint8_t)map_iter->first);
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
////////////////////////////////////////////////////////////////////////////////


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
	if(!readInit(Zplm, GRK_PL_MARKER_PLM))
		return false;
	uint32_t len;
	while(header_size > 0)
	{
		// Nplm
		uint8_t Nplm = *headerData++;
		if(header_size < (1 + Nplm))
		{
			GRK_ERROR("Malformed PLM marker segment");
			return false;
		}
		uint32_t i = 0;
		while (i < header_size){
			while (!readNextByte(*headerData++, &len) && (i < header_size))
				i++;
			i++;
		}
		header_size = (uint16_t)(header_size - (1 + Nplm));
		if(packetLen_ != 0)
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
	if(!readInit(Zpl, GRK_PL_MARKER_PLT))
		return false;

	auto b = new grkBufferU8();
	b->alloc(header_size);
	memcpy(b->buf, headerData, header_size);
	currRawMarkerIter_->second->push_back(b);
#ifdef DEBUG_PLT
	GRK_INFO("PLT marker %d", Zpl);
#endif

	return true;
}
bool PacketLengthMarkers::readInit(uint32_t nextIndex, PL_MARKER_TYPE type)
{
	if(rawMarkers_->size() == 255 && type == GRK_PL_MARKER_PLM)
	{
		GRK_ERROR("PLM: only 255 PLM markers are supported.");
		return false;
	}
	if(rawMarkers_->empty())
	{
		sequential_ = nextIndex == 0;
	}
	else
	{
		// once sequential_ becomes false, it never returns to true again
		if(sequential_)
		{
			sequential_ = (rawMarkers_->size() % 256) == nextIndex;
			if(!sequential_ && rawMarkers_->size() > 256)
			{
				GRK_ERROR("PLT: sequential marker assumption has been broken.");
				return false;
			}
		}

		// The code below handles the non-standard case where there are more
		// than 256 markers, but their signaled indices are all sequential mod 256.
		// Although this is an abuse of the standard, we interpret this to mean
		// that the actual marker index is simply the marker count.
		// Therefore, we do not concatenate any of the markers, even though
		// they may share the same signaled marker index
		if(sequential_)
		{
			nextIndex = (uint32_t)rawMarkers_->size();
			if(rawMarkers_->size() == 256)
			{
				GRK_WARN(
					"PLT: 256+1 markers, with all 256+1 PLT marker indices sequential mod 256.");
				GRK_WARN("We will make the assumption that **all** PLT markers are sequential");
				GRK_WARN("and therefore will ignore the signaled PLT marker index,");
				GRK_WARN("and use the marker count instead as the marker index.");
				GRK_WARN("Decompression will fail if this assumption is broken for subsequent PLT "
						 "markers.");
			}
		}
	}
	// 2. update raw markers
	currRawMarkerIter_ = rawMarkers_->find(nextIndex);
	if(currRawMarkerIter_ == rawMarkers_->end())
	{
		rawMarkers_->operator[](nextIndex) = new PL_RAW_MARKER();
		currRawMarkerIter_ = rawMarkers_->find(nextIndex);
	}

	return true;
}
bool PacketLengthMarkers::readNextByte(uint8_t Iplm, uint32_t *packetLength)
{
	/* take only the lower seven bits */
	packetLen_ |= (Iplm & 0x7f);
	if(Iplm & 0x80)
	{
		packetLen_ <<= 7;
	}
	else
	{
		if (packetLength)
			*packetLength = packetLen_;
		packetLen_ = 0;
	}

	return packetLen_ == 0;
}

uint64_t PacketLengthMarkers::pop(uint64_t numPackets){
	uint32_t total = 0;
	for (uint64_t i = 0; i < numPackets; ++i)
		total += pop();

	return total;
}

// note: packet length must be at least 1, so 0 indicates
// no packet length available
uint32_t PacketLengthMarkers::pop(void)
{
	uint32_t rc = 0;
	if(currRawMarkerIter_ != rawMarkers_->end() && currRawMarkerBuf_){

		// read next packet length
		while (currRawMarkerBuf_->canRead() && !readNextByte(currRawMarkerBuf_->read(), &rc)) {
		}
		// advance to next buffer
		if (currRawMarkerBuf_->offset == currRawMarkerBuf_->len){
			currRawMarkerBufIndex_++;
			if(currRawMarkerBufIndex_ < currRawMarkerIter_->second->size())
			{
				currRawMarkerBuf_ = currRawMarkerIter_->second->operator[](currRawMarkerBufIndex_);
			}
			else
			{
				// advance to next marker
				currRawMarkerIter_++;
				if(currRawMarkerIter_ != rawMarkers_->end())
				{
					currRawMarkerBufIndex_ = 0;
					currRawMarkerBuf_ = currRawMarkerIter_->second->front();
				}
				else
				{
					// shouldn't get here
					currRawMarkerIter_ = rawMarkers_->end();
					currRawMarkerBuf_ = nullptr;
					GRK_WARN("Attempt to pop PLT beyond PLT marker range.");
				}
			}
		}
	}

	// GRK_INFO("Read packet length: %d", rc);
	return rc;
}

void PacketLengthMarkers::rewind(void)
{
	if(rawMarkers_ && !rawMarkers_->empty())
	{
		currRawMarkerIter_ = rawMarkers_->begin();
		currRawMarkerBuf_ = currRawMarkerIter_->second->front();
	}
}


} // namespace grk
