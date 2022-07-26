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
// includes single byte index and bytes for PLs
const uint16_t plWriteBufferLen = USHRT_MAX - 4;

PLMarkerMgr::PLMarkerMgr()
	: rawMarkers_(new PL_MARKERS()), currMarkerIter_(rawMarkers_->end()), totalBytesWritten_(0),
	  isFinal_(false), stream_(nullptr), sequential_(false), packetLen_(0), currMarkerBufIndex_(0),
	  currMarkerBuf_(nullptr), enabled_(true)
{}
// compression
PLMarkerMgr::PLMarkerMgr(IBufferedStream* strm) : PLMarkerMgr()
{
	stream_ = strm;
}
PLMarkerMgr::~PLMarkerMgr()
{
	clearMarkers();
	delete rawMarkers_;
}
void PLMarkerMgr::disable(void)
{
	enabled_ = false;
}
bool PLMarkerMgr::isEnabled(void)
{
	return enabled_;
}
void PLMarkerMgr::clearMarkers(void)
{
	for(auto it = rawMarkers_->begin(); it != rawMarkers_->end(); it++)
	{
		auto v = it->second;
		for(auto itv = v->begin(); itv != v->end(); ++itv)
			delete *itv;
		delete it->second;
	}
	rawMarkers_->clear();
	currMarkerIter_ = rawMarkers_->end();
	currMarkerBuf_ = nullptr;
}
void PLMarkerMgr::pushInit(bool isFinal)
{
	clearMarkers();
	totalBytesWritten_ = 0;
	isFinal_ = isFinal;
}
bool PLMarkerMgr::pushPL(uint32_t len)
{
	assert(len);
	// GRK_INFO("Push packet length: %u", len);
	uint32_t numbits = floorlog2(len) + 1;
	uint32_t numBytes = (numbits + 6) / 7;
	assert(numBytes <= 5);

	auto marker = rawMarkers_->empty() ? nullptr : currMarkerIter_->second;
	grk_buf8* buf = nullptr;
	bool newMarker = false;
	uint8_t newMarkerId;
	if(rawMarkers_->empty())
	{
		newMarker = true;
		newMarkerId = 0;
		if(!findMarker(newMarkerId, true))
			return false;
		marker = currMarkerIter_->second;
	}
	else if(marker->back()->offset + numBytes > marker->back()->len)
	{
		newMarker = true;
		newMarkerId = rawMarkers_->size() & 0xFF;
		if(!findMarker((uint32_t)rawMarkers_->size(), true))
			return false;
		marker = currMarkerIter_->second;
	}
	else
	{
		newMarkerId = currMarkerIter_->first & 0xFF;
		buf = marker->back();
	}
	if(newMarker)
	{
		if(isFinal_)
		{
			buf = addNewMarker(nullptr, plWriteBufferLen);
			buf->write(newMarkerId);
		}
		// account for marker header
		totalBytesWritten_ += 2 + 2 + 1;
	}
	assert(buf);
	if(isFinal_)
	{
		// write period
		// static int count = 0;
		// GRK_INFO("Wrote PLT packet %u, length %u", count++,len);
		uint8_t temp[5];
		int32_t counter = (int32_t)(numBytes - 1);
		temp[counter--] = (len & 0x7F);
		len = (uint32_t)(len >> 7);

		// write commas (backwards from LSB to MSB)
		while(len)
		{
			uint8_t b = (uint8_t)((len & 0x7F) | 0x80);
			temp[counter--] = b;
			len = (uint32_t)(len >> 7);
		}
		assert(counter == -1);
		if(!buf->write(temp, numBytes))
			return false;
	}
	totalBytesWritten_ += numBytes;

	return true;
}
uint32_t PLMarkerMgr::getTotalBytesWritten(void)
{
	return totalBytesWritten_;
}
bool PLMarkerMgr::write(void)
{
	assert(isFinal_);
	for(auto it = rawMarkers_->begin(); it != rawMarkers_->end(); ++it)
	{
		auto v = it->second;
		for(auto itv = v->begin(); itv != v->end(); ++itv)
		{
			auto b = *itv;
			if(!stream_->writeShort(J2K_MS_PLT))
				return false;
			if(!stream_->writeShort((uint16_t)(b->offset + 2)))
				return false;
			if(!stream_->writeBytes(b->buf, b->offset))
				return false;
		}
	}

	return true;
}
///////////////////////////////////////////////////////////////////////////

bool PLMarkerMgr::readPLM(uint8_t* headerData, uint16_t header_size)
{
	if(header_size < 1)
	{
		GRK_ERROR("PLM marker segment too short");
		return false;
	}
	// Zplm
	uint8_t Zplm = *headerData++;
	--header_size;
	if(rawMarkers_->size() == 256)
	{
		GRK_ERROR("PLM: only 256 PLM markers are allowed by the standard.");
		return false;
	}
	if(!findMarker(Zplm, false))
		return false;
	while(header_size > 0)
	{
		// 1. read Nplm
		uint8_t Nplm = *headerData++;
		uint16_t segmentLength = (uint16_t)Nplm + 1;
		if(header_size < segmentLength)
		{
			GRK_ERROR("Malformed PLM marker segment");
			return false;
		}
		// 2. push packets for nth tile part into current raw marker
		addNewMarker(headerData, segmentLength);

		// 3. advance src buffer
		header_size = (uint16_t)(header_size - segmentLength);
		headerData += Nplm;
	}

	return true;
}
grk_buf8* PLMarkerMgr::addNewMarker(uint8_t* data, uint16_t len)
{
	auto b = new grk_buf8();
	if(data || len)
		b->alloc(len);
	if(data)
		memcpy(b->buf, data, len);
	currMarkerIter_->second->push_back(b);

	return b;
}
bool PLMarkerMgr::readPLT(uint8_t* headerData, uint16_t header_size)
{
	if(header_size <= 1)
	{
		GRK_ERROR("PLT marker segment too short");
		return false;
	}
	/* Zplt */
	uint8_t Zpl = *headerData++;
	--header_size;
	if(!findMarker(Zpl, false))
		return false;

	addNewMarker(headerData, header_size);
#ifdef DEBUG_PLT
	GRK_INFO("PLT marker %u", Zpl);
#endif

	return true;
}
bool PLMarkerMgr::findMarker(uint32_t nextIndex, bool compress)
{
	if(!compress)
	{
		if(rawMarkers_->empty())
		{
			sequential_ = nextIndex == 0;
		}
		else
		{
			// once sequential_ becomes false, it never returns to true again
			if(sequential_)
			{
				sequential_ = (rawMarkers_->size() & 0xFF) == nextIndex;
				if(!sequential_ && rawMarkers_->size() > 256)
				{
					GRK_ERROR("PLT: sequential marker assumption has been broken.");
					return false;
				}
			}

			// The code below handles the case where there are more
			// than 256 markers, but their signaled indices are all sequential mod 256.
			// We interpret this to mean that the actual marker index is simply the marker count.
			// Therefore, we do not concatenate any of the markers, even though
			// they may share the same signaled marker index
			if(sequential_)
				nextIndex = (uint32_t)rawMarkers_->size();
		}
	}

	// update raw markers
	currMarkerIter_ = rawMarkers_->find(nextIndex);
	if(currMarkerIter_ == rawMarkers_->end())
	{
		rawMarkers_->operator[](nextIndex) = new PL_MARKER();
		currMarkerIter_ = rawMarkers_->find(nextIndex);
	}

	return true;
}
bool PLMarkerMgr::readNextByte(uint8_t Iplm, uint32_t* packetLength)
{
	/* take only the lower seven bits */
	packetLen_ |= (Iplm & 0x7f);
	if(Iplm & 0x80)
	{
		packetLen_ <<= 7;
	}
	else
	{
		if(packetLength)
			*packetLength = packetLen_;
		packetLen_ = 0;
	}

	return packetLen_ == 0;
}
uint64_t PLMarkerMgr::pop(uint64_t numPackets)
{
	uint64_t total = 0;
	for(uint64_t i = 0; i < numPackets; ++i)
		total += pop();

	return total;
}
// note: packet length must be at least 1, so 0 indicates
// no packet length available
uint32_t PLMarkerMgr::pop(void)
{
	uint32_t rc = 0;
	assert(rawMarkers_);

	if(currMarkerIter_ == rawMarkers_->end())
	{
		GRK_ERROR("Attempt to pop PLT beyond PLT marker range.");
		return 0;
	}
	if(currMarkerIter_ != rawMarkers_->end() && currMarkerBuf_)
	{
		// read next packet length
		while(currMarkerBuf_->canRead() && !readNextByte(currMarkerBuf_->read(), &rc))
		{}
		// advance to next buffer
		if(currMarkerBuf_->offset == currMarkerBuf_->len)
		{
			currMarkerBufIndex_++;
			if(currMarkerBufIndex_ < currMarkerIter_->second->size())
			{
				currMarkerBuf_ = currMarkerIter_->second->operator[](currMarkerBufIndex_);
			}
			else
			{
				currMarkerIter_++;
				if(currMarkerIter_ != rawMarkers_->end())
				{
					currMarkerBufIndex_ = 0;
					currMarkerBuf_ = currMarkerIter_->second->front();
				}
				else
				{
					currMarkerBuf_ = nullptr;
				}
			}
		}
	}

	// static int count = 0;
	// GRK_INFO("Read PLT packet %u, length %u", count++,rc);
	return rc;
}

void PLMarkerMgr::rewind(void)
{
	if(!rawMarkers_->empty())
	{
		currMarkerIter_ = rawMarkers_->begin();
		currMarkerBuf_ = currMarkerIter_->second->front();
	}
}

} // namespace grk
