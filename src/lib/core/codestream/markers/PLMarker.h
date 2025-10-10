/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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
#include <unordered_map>

namespace grk
{

/**
 * @brief collection of raw marker data stored in @ref Buffer8
 *
 * Packet lengths are stored using comma code
 */
typedef std::vector<Buffer8*> RAW_PL_MARKER;

/**
 * @brief collection of @ref RAW_PL_MARKER pointers
 *
 * RAW_PL_MARKERs are index by map key. According to the standard
 * this key should be <= 255 but in practice it is allowed to be
 * larger
 *
 * Note: order is important for this map
 *
 */
typedef std::map<uint32_t, RAW_PL_MARKER*> RAW_PL_MARKER_MAP;

/**
 * @struct PLMarker
 * @brief Manages raw (uncompressed) PLT and PLM markers
 *
 * Marker data is stored raw, and decompressed on the fly. Pop methods are used
 * to get length of next packet(s)
 */
struct PLMarker
{
  /**
   * @brief Constructs a PLMarker
   */
  PLMarker(void);
  /**
   * @brief Destroys a PLMarker
   */
  ~PLMarker(void);

  /**
   * @brief Disables object in event of corrupt PL marker
   */
  void disable(void);

  /**
   * @brief Checks if object is enabled
   * @return true if enabled
   */
  bool isEnabled(void);

  // compress
  /**
   * @brief Prepares for pushing markers
   * @param isFinal true if this is the final Packet Length
   */
  void pushInit(bool isFinal);

  /**
   * @brief Pushes packet length
   * @len length of packet
   * @return true if successful
   */
  bool pushPL(uint32_t len);

  /**
   * @brief Writes marker to stream
   * @return true if successful
   */
  bool write(void);

  /**
   * @brief Gets total bytes written
   * @return total bytes written
   */
  uint32_t getTotalBytesWritten(void);

  // decompress
  /**
   * @brief Constructs a PLMarker
   * @param strm @ref IStream for code stream
   */
  PLMarker(IStream* strm);

  /**
   * @brief Reads PLT marker
   * @param headerData header data
   * @param headerSize header size
   * @param tilePartIndex tile part index (optional, -1 to push)
   * @return true if successful
   */
  bool readPLT(uint8_t* headerData, uint16_t headerSize, int16_t tilePartIndex = -1);

  /**
   * @brief Reads PLM marker
   * @param headerData header data
   * @param headerSize header size
   * @return true if successful
   */
  bool readPLM(uint8_t* headerData, uint16_t headerSize);

  /**
   * @brief Resets object for reading packet lengths
   */
  void rewind(void);

  /**
   * @brief pop next packet length
   * @return next packet length
   */
  uint32_t pop(void);

  /**
   * @brief pop length of next set of consecutive packets
   * @param numPackets number of packets to generate length for
   * @return length of consecutive packets
   */
  uint64_t pop(uint64_t numPackets);

private:
  void clearMarkers(void);
  bool findMarker(uint32_t index, bool compress);
  Buffer8* addNewMarker(uint8_t* data, uint16_t len, int16_t tilePartIndex = -1);
  RAW_PL_MARKER_MAP* rawMarkers_;
  RAW_PL_MARKER_MAP::iterator currMarkerIter_;

  ////////////////////////////////
  // compress
  uint32_t totalBytesWritten_;
  bool isFinal_;
  IStream* stream_;
  ////////////////////////////////

  //////////////////////////
  // decompress
  bool readNextByte(uint8_t Iplm, uint32_t* packetLength);
  bool sequential_;
  uint32_t packetLen_;
  uint32_t currMarkerBufIndex_;
  Buffer8* currMarkerBuf_;
  ///////////////////////////////

  bool enabled_;
};

} // namespace grk