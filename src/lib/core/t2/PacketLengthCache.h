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

#pragma once

#include <vector>
#include <map>

namespace grk
{

/**
 * @class PacketLengthCache
 * @brief Cached packet lengths from PLT or PLM marker
 */
template<typename T>
class PacketLengthCache
{
public:
  /**
   * @brief Constructs a PacketLengthCache
   * @param cp @ref CodingParams
   */
  PacketLengthCache(CodingParams* cp);

  /**
   * @brief Destroys a PacketLengthCache
   */
  virtual ~PacketLengthCache();

  /**
   * @brief Creates new PL markers
   * @param strm @ref IStream
   * @return pointer to @ref PLMarker
   */
  PLMarker* createMarkers(IStream* strm);

  /**
   * @brief Gets PL markers
   * @return @ref PLMarker
   */
  PLMarker* getMarkers(void) const;

  /**
   * @brief Deletes PL markers
   */
  void deleteMarkers(void);

  /**
   * @brief Reads the next packet length
   * @param packetLength set to the packet length, or 0 if packet lengths are not in use
   * @return false if packet lengths are in use but none remain
   */
  bool readNextPacketLength(T& packetLength);

  bool readPacketLengths(uint64_t numberOfPackets, uint64_t& totalLength);

  bool usesPacketLengths(void) const;

  bool hasPacketLengthError(void) const;

  /**
   * @brief rewinds state to be ready to read packet lengths from beginning
   * of Tile packet stream
   */
  void rewind(void);

private:
  /**
   * @brief @ref PLMarker storing PL markers
   */
  PLMarker* plMarkers_;

  /**
   * @brief pointer to @ref CodingParams
   */
  CodingParams* cp_;

  bool packetLengthError_;
};

template<typename T>
PacketLengthCache<T>::PacketLengthCache(CodingParams* cp)
    : plMarkers_(nullptr), cp_(cp), packetLengthError_(false)
{}
template<typename T>
PacketLengthCache<T>::~PacketLengthCache()
{
  delete plMarkers_;
}

template<typename T>
PLMarker* PacketLengthCache<T>::createMarkers(IStream* strm)
{
  if(!plMarkers_)
    plMarkers_ = strm ? new PLMarker(strm) : new PLMarker();

  return plMarkers_;
}

template<typename T>
PLMarker* PacketLengthCache<T>::getMarkers(void) const
{
  return plMarkers_;
}

template<typename T>
void PacketLengthCache<T>::deleteMarkers(void)
{
  delete plMarkers_;
  plMarkers_ = nullptr;
  packetLengthError_ = false;
}

template<typename T>
bool PacketLengthCache<T>::usesPacketLengths(void) const
{
  return plMarkers_ && !cp_->plmMarkers_ && plMarkers_->isEnabled();
}

template<typename T>
bool PacketLengthCache<T>::readNextPacketLength(T& packetLength)
{
  packetLength = 0;
  if(!usesPacketLengths())
    return true;

  packetLength = plMarkers_->pop();
  if(packetLength)
    return true;

  packetLengthError_ = true;
  grklog.error("PLT marker: missing packet length.");
  return false;
}

template<typename T>
bool PacketLengthCache<T>::readPacketLengths(uint64_t numberOfPackets, uint64_t& totalLength)
{
  totalLength = 0;
  for(uint64_t packetIndex = 0; packetIndex < numberOfPackets; ++packetIndex)
  {
    T packetLength;
    if(!readNextPacketLength(packetLength))
      return false;
    totalLength += packetLength;
  }
  return true;
}

template<typename T>
bool PacketLengthCache<T>::hasPacketLengthError(void) const
{
  return packetLengthError_;
}

template<typename T>
void PacketLengthCache<T>::rewind(void)
{
  packetLengthError_ = false;
  // we don't currently support PLM markers,
  // so we disable packet length markers if we have both PLT and PLM
  if(plMarkers_ && !cp_->plmMarkers_)
    plMarkers_->rewind();
}

} // namespace grk
