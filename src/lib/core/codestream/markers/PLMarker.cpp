/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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
 *
 */

#include "grk_includes.h"

// #define DEBUG_PLT

namespace grk
{
// includes single byte index and bytes for PLs
const uint16_t plWriteBufferLen = USHRT_MAX - 4;

PLMarker::PLMarker()
    : rawMarkers_(new RAW_PL_MARKER_MAP()), currMarkerIter_(rawMarkers_->end()),
      totalBytesWritten_(0), isFinal_(false), stream_(nullptr), sequential_(false), packetLen_(0),
      currMarkerBufIndex_(0), currMarkerBuf_(nullptr), enabled_(true)
{}
// compression
PLMarker::PLMarker(IStream* strm) : PLMarker()
{
  stream_ = strm;
}
PLMarker::~PLMarker()
{
  clearMarkers();
  delete rawMarkers_;
}
void PLMarker::disable(void)
{
  enabled_ = false;
}
bool PLMarker::isEnabled(void)
{
  return enabled_;
}
void PLMarker::clearMarkers(void)
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
void PLMarker::pushInit(bool isFinal)
{
  clearMarkers();
  totalBytesWritten_ = 0;
  isFinal_ = isFinal;
}
bool PLMarker::pushPL(uint32_t len)
{
  assert(len);
  // grklog.info("Push packet length: %u", len);
  uint32_t numbits = floorlog2(len) + 1;
  uint32_t numBytes = (numbits + 6) / 7;
  assert(numBytes <= 5);

  if(rawMarkers_->empty() || (currMarkerIter_->second->back()->offset() + numBytes >
                              currMarkerIter_->second->back()->num_elts()))
  {
    uint8_t markerId = rawMarkers_->size() & 0xFF;
    if(!findMarker((uint32_t)rawMarkers_->size(), true))
      return false;
    auto buf = addNewMarker(nullptr, isFinal_ ? plWriteBufferLen : 0);
    if(isFinal_ && !buf->write(markerId))
      return false;
    // account for marker header
    totalBytesWritten_ += 2 + 2 + 1;
  }

  if(isFinal_)
  {
    // write period
    // static int count = 0;
    // grklog.info("Wrote PLT packet %u, length %u", count++,len);
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
    auto buf = currMarkerIter_->second->back();
    if(!buf->write(temp, numBytes))
      return false;
  }
  totalBytesWritten_ += numBytes;

  return true;
}
uint32_t PLMarker::getTotalBytesWritten(void)
{
  return totalBytesWritten_;
}
bool PLMarker::write(void)
{
  assert(isFinal_);
  for(auto it = rawMarkers_->begin(); it != rawMarkers_->end(); ++it)
  {
    auto v = it->second;
    for(auto itv = v->begin(); itv != v->end(); ++itv)
    {
      auto b = *itv;
      if(!stream_->write(PLT))
        return false;
      if(!stream_->write((uint16_t)(b->offset() + 2)))
        return false;
      if(!stream_->writeBytes(b->buf(), b->offset()))
        return false;
    }
  }

  return true;
}
///////////////////////////////////////////////////////////////////////////

bool PLMarker::readPLM(uint8_t* headerData, uint16_t headerSize)
{
  if(headerSize < 1)
  {
    grklog.warn("PLM marker segment too short. Ignoring PLM.");
    return true;
  }
  // Zplm
  uint8_t Zplm = *headerData++;
  --headerSize;
  if(rawMarkers_->size() == 256)
  {
    grklog.warn("PLM: only 256 PLM markers are allowed by the standard. Ignoring PLM.");
    return true;
  }
  if(!findMarker(Zplm, false))
  {
    clearMarkers();
    return true;
  }
  while(headerSize > 0)
  {
    // 1. read Nplm
    uint8_t Nplm = *headerData++;
    uint16_t tilePartPacketInfoLength = (uint16_t)Nplm + 1;
    if(headerSize < tilePartPacketInfoLength)
    {
      grklog.warn("Malformed PLM marker segment: length of tile part packet info "
                  "%d is greater than available bytes %d. Ignoring PLM",
                  tilePartPacketInfoLength, headerSize);
      clearMarkers();
      return true;
    }
    // 2. push packets for nth tile part into current raw marker
    addNewMarker(headerData, tilePartPacketInfoLength, -1);

    // 3. advance src buffer
    headerSize = (uint16_t)(headerSize - tilePartPacketInfoLength);
    headerData += Nplm;
  }

  return true;
}
Buffer8* PLMarker::addNewMarker(uint8_t* data, uint16_t len, int16_t tilePartIndex)
{
  auto b = new Buffer8();
  if(data || len)
    b->alloc(len);
  if(data)
    memcpy(b->buf(), data, len);

  auto& vec = *currMarkerIter_->second;
  if(tilePartIndex == -1)
  {
    vec.push_back(b);
#ifdef DEBUG_PLT
    grklog.info("Tile part %d, length %u", tilePartIndex, len);
#endif
  }
  else
  {
    if(tilePartIndex < -1)
    { // Safety check for invalid negative indices
      delete b;
      grklog.error("Invalid tile part index %d for adding marker.", tilePartIndex);
      return nullptr;
    }
    if(static_cast<size_t>(tilePartIndex) >= vec.size())
    {
      vec.resize(static_cast<size_t>(tilePartIndex) + 1, nullptr);
    }
    if(vec[(size_t)tilePartIndex] != nullptr)
    {
      delete b;
      grklog.error("Tile part index %d already occupied for marker key %u.", tilePartIndex,
                   currMarkerIter_->first);
      return nullptr;
    }
    vec[(size_t)tilePartIndex] = b;

#ifdef DEBUG_PLT
    grklog.info("Tile part %d, length %u", tilePartIndex, len);
#endif
  }

  return b;
}
bool PLMarker::readPLT(uint8_t* headerData, uint16_t headerSize, int16_t tilePartIndex)
{
  if(headerSize <= 1)
  {
    grklog.error("PLT marker segment too short");
    return false;
  }
  /* Zplt */
  uint8_t Zpl = *headerData++;
  --headerSize;
  if(!findMarker(Zpl, false))
    return false;

  addNewMarker(headerData, headerSize, tilePartIndex);
#ifdef DEBUG_PLT
  grklog.info("Tile part %d, PLT marker %u, length %u", tilePartIndex, Zpl, headerSize);
#endif

  return true;
}
bool PLMarker::findMarker(uint32_t nextIndex, bool compress)
{
  if(!compress)
  {
    // detect sequential markers.
    // Note: once sequential_ becomes false, it never returns to true again

    // 1. always start with assumption that markers are sequential
    if(rawMarkers_->empty())
    {
      sequential_ = nextIndex == 0;
    }
    else
    {
      // 2. check if next index is also sequential
      if(sequential_)
      {
        sequential_ = (rawMarkers_->size() & 0xFF) == nextIndex;

        // 3. sanity check
        if(!sequential_)
        {
          if(rawMarkers_->size() > 256)
          {
            grklog.error("PLT: sequential marker assumption has been broken.");
            return false;
          }
        }
        else
        {
          // The code below handles the case where there are more
          // than 256 markers, but their signalled indices are all sequential mod 256.
          // We interpret this to mean that the actual marker index is simply the marker
          // count. Therefore, we do not concatenate any of the markers, even though they
          // may share the same signalled marker index
          nextIndex = (uint32_t)rawMarkers_->size();
        }
      }
    }
  }

  // update raw markers
  currMarkerIter_ = rawMarkers_->find(nextIndex);
  if(currMarkerIter_ == rawMarkers_->end())
  {
    rawMarkers_->operator[](nextIndex) = new RAW_PL_MARKER();
    currMarkerIter_ = rawMarkers_->find(nextIndex);
  }

  return true;
}
bool PLMarker::readNextByte(uint8_t Iplm, uint32_t* packetLength)
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
uint64_t PLMarker::pop(uint64_t numPackets)
{
  uint64_t total = 0;
  for(uint64_t i = 0; i < numPackets; ++i)
    total += pop();

  return total;
}
// note: packet length must be at least 1, so 0 indicates
// no packet length available
uint32_t PLMarker::pop(void)
{
  uint32_t rc = 0;
  assert(rawMarkers_);

  if(currMarkerIter_ == rawMarkers_->end())
  {
    grklog.error("Attempt to pop PLT beyond PLT marker range.");
    return 0;
  }
  if(currMarkerIter_ != rawMarkers_->end() && currMarkerBuf_)
  {
    if(currMarkerBuf_ == nullptr)
    {
      grklog.error("Encountered nullptr buffer in PL marker pop.");
      return 0;
    }
    // read next packet length
    while(currMarkerBuf_->canRead() && !readNextByte(currMarkerBuf_->read(), &rc))
    {
    }
    // advance to next buffer
    if(currMarkerBuf_->offset() == currMarkerBuf_->num_elts())
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
  // grklog.info("Read PLT packet %u, length %u", count++,rc);
  return rc;
}

void PLMarker::rewind(void)
{
  if(!rawMarkers_->empty())
  {
    currMarkerIter_ = rawMarkers_->begin();
    for(const auto& entry : *rawMarkers_)
    {
      RAW_PL_MARKER* markerVector = entry.second;
      size_t idx = 0;
      for(auto* buffer : *markerVector)
      {
        if(buffer == nullptr)
        {
          grklog.error(
              "Non-contiguous PL marker vector for key %u (nullptr at index %zu). Disabling.",
              entry.first, idx);
          disable();
          return;
        }
        buffer->set_offset(0);
        ++idx;
      }
    }
    currMarkerBufIndex_ = 0;
    currMarkerBuf_ = currMarkerIter_->second->front();
  }
}

} // namespace grk