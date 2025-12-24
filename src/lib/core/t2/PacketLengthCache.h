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
   * @brief Gets next packet info
   * @param packetInfoPtr pointer to @ref Length which will
   * hold the packet length
   */
  T next(void);

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
};

template<typename T>
PacketLengthCache<T>::PacketLengthCache(CodingParams* cp) : plMarkers_(nullptr), cp_(cp)
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
}

template<typename T>
T PacketLengthCache<T>::next()
{
  // we don't currently support PLM markers,
  // so we disable packet length markers if we have both PLT and PLM
  bool usePlt = plMarkers_ && !cp_->plmMarkers_ && plMarkers_->isEnabled();
  if(usePlt)
  {
    T len = plMarkers_->pop();
    if(len == 0)
      grklog.error("PLT marker: missing packet lengths.");
    return len;
  }
  return 0;
}

template<typename T>
void PacketLengthCache<T>::rewind(void)
{
  // we don't currently support PLM markers,
  // so we disable packet length markers if we have both PLT and PLM
  if(plMarkers_ && !cp_->plmMarkers_)
    plMarkers_->rewind();
}

} // namespace grk
