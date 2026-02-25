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

#include <cstdint>
#include <unordered_map>
#include <queue>
#include <optional>

#include "SparseBuffer.h"

namespace grk
{

/**
 * @class PacketParser
 * @brief Parses packet header and packer data
 */
class PacketParser
{
public:
  /**
   * @brief Constructs PacketParser
   * @param tileProcessor @ref TileProcssor
   * @param packetSequenceNumber packet sequence number
   * @param compno component number
   * @param resno resolution number
   * @param precinctIndex precinct index
   * @param layno layer number
   * @param cachedLength signalled length from PLT/PLM marker, or cached length
   * from previous read of header
   * @param compressedPackets @ref SparseBuffer holding packets
   */
  PacketParser(ITileProcessor* tileProcessor, uint16_t packetSequenceNumber, uint16_t compno,
               uint8_t resno, uint64_t precinctIndex, uint16_t layno, uint32_t cachedLength,
               SparseBuffer* compressedPackets);

  /**
   * @brief Destroys PacketParser
   */
  virtual ~PacketParser(void) = default;

  /**
   * @brief Reads packet header
   * @return packet length including header and data
   */
  uint32_t readHeader(void);

  /**
   * @brief Gets length
   * @return length
   */
  uint32_t getLength(void);

  /**
   * @brief Reads packet data
   */
  void readData(void);

  /**
   * @brief Printout for debugging
   */
  void print(void);

private:
  /**
   * @brief Finalizes packet data reading after it is complete
   */
  void readDataFinalize(void);

  /**
   * @brief tile processor
   */
  ITileProcessor* tileProcessor_;

  uint16_t tileIndex_ = 0;

  /**
   * @brief packet sequence number
   *
   * This is the generated packet sequence number. We compare to the signalled
   * sequence number to detect pack stream corruption
   */
  uint16_t packetSequenceNumber_ = 0;

  /**
   * @brief component number
   */
  uint16_t compno_ = 0;

  /**
   * @brief resolution number
   */
  uint8_t resno_ = 0;

  /**
   * @brief precinct index
   */
  uint64_t precinctIndex_ = 0;

  /**
   * @brief layer number
   */
  uint16_t layno_ = 0;

  /**
   * @brief @ref SparseBuffer of all packets
   */
  SparseBuffer* packets_ = nullptr;

  /**
   * @brief packets_ current chunk pointer aka layer data
   */
  uint8_t* layerData_ = nullptr;

  /**
   * @brief all available bytes in layer (includes packet header and data)
   */
  size_t layerBytesAvailable_ = 0;

  /**
   * @brief true if tag bits present in packet header
   */
  bool tagBitsPresent_ = false;

  /**
   * @brief packet header length - does not include packed header bytes
   */
  uint32_t headerLength_ = 0;

  /**
   * @brief length of packet data as signalled in packet header
   */
  uint32_t signalledLayerDataBytes_ = 0;

  /**
   * @brief total packet length as signalled in marker (PLT/PLM)
   */
  uint32_t plLength_ = 0;

  /**
   * @brief true if header has been parsed
   */
  bool parsedHeader_ = false;

  /**
   * @brief true of there was an error reading the header
   */
  bool headerError_ = false;
};

/**
 * @class LimitedQueue
 * @brief Queue limited to maximum size
 *
 * @tparam type of element in queue
 */
template<typename T>
class LimitedQueue
{
public:
  /**
   * @brief Constructs a LimitedQueue
   * @param maxSize maximum size of queue
   */
  LimitedQueue(std::size_t maxSize)
      : elements(std::make_unique<T*[]>(maxSize)), currentSize(0), maxSize(maxSize), popIndex(-1)
  {}

  /**
   * @brief Destroys a LimitedQueue
   */
  ~LimitedQueue() = default;

  /**
   * @brief Pushes an element into the queue
   * @param ptr pointer to push into queue
   * @return true if successful
   */
  bool push(T* ptr)
  {
    if(currentSize >= maxSize)
    {
      return false; // Queue full
    }
    elements[currentSize++] = ptr; // Store pointer
    return true; // Successfully pushed
  }

  /**
   * @brief Pops an element from the queue
   *
   * There may be no element to pop
   * @return std::optional<T>
   */
  std::optional<T*> pop()
  {
    auto currentPopIndex = ++popIndex;
    if((size_t)currentPopIndex >= currentSize)
      return std::nullopt; // Queue empty
    return elements[(size_t)currentPopIndex];
  }

private:
  /**
   * @brief array of queue elements
   */
  std::unique_ptr<T*[]> elements;

  /**
   * @brief current size of queue
   */
  size_t currentSize;

  /**
   * @brief maximum size of queue
   */
  std::size_t maxSize;

  /**
   * @brief pop index
   */
  std::atomic<int32_t> popIndex;
};

/**
 * @struct AllLayersPrecinctPacketParser
 * @brief Enqueues @ref PacketParser for all layers of a given precinct, to be executed in sequence.
 * These queues of parsers will be executed concurrently across precincts
 */
struct AllLayersPrecinctPacketParser
{
  /**
   * @brief Constructs an AllLayersPrecinctPacketParser
   * @param tileProcess @ref ITileProcessor
   */
  AllLayersPrecinctPacketParser(ITileProcessor* tileProcessor);
  /**
   * @brief Destroys an AllLayersPrecinctPacketParser
   */
  ~AllLayersPrecinctPacketParser(void) = default;

  /**
   * @brief Enqueues a layer @ref PacketParser for concurrent parsing
   */
  void enqueue(PacketParser* parser);
  /**
   * @brief @ref ITileProcessor
   */
  ITileProcessor* tileProcessor_;

  /**
   * @brief Queue of @ref PacketParser
   */
  LimitedQueue<PacketParser> parserQueue_;
};

/**
 * @struct ResolutionPacketParser
 * @brief Enqueues sequence of @ref AllLayersPrecinctPacketParser for a given resolutions.
 */
struct ResolutionPacketParser
{
  /**
   * @brief Constructs a ResolutionPacketParser
   * @param tileProcess @ref ITileProcessor
   */
  ResolutionPacketParser(ITileProcessor* tileProcessor);
  /**
   * @brief Destroys a ResolutionPacketParser
   */
  ~ResolutionPacketParser();

  /**
   * @brief Clears map of @ref PrecinctParser
   */
  void clearPrecinctParsers(void);

  /**
   * @brief Enqueues a @ref PacketParser for a precinct, for concurrent parsing
   * @param precinctIndex precinct index
   * @param parser @ref PacketParser
   */
  void enqueue(uint64_t precinctIndex, PacketParser* parser);
  /**
   * @brief @ref ITileProcessor
   */
  ITileProcessor* tileProcessor_;

  /**
   * @brief map of @ref AllLayersPrecinctPacketParser, indexed by precinct index
   */
  std::unordered_map<uint64_t, std::unique_ptr<AllLayersPrecinctPacketParser>>
      allLayerPrecinctParsers_;
};

} // namespace grk
