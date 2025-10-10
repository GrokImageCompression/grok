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
#include <memory>
#include <vector>
#include <span> // C++20

namespace grk
{

/**
 * @struct TilePartLength
 * @brief Stores tile part's length and tile index
 */
template<typename T>
struct TilePartLength
{
  /**
   * @brief Constructs a TilePartLength
   * @param tileIndex tile index
   * @param len tile part length
   */
  TilePartLength(uint16_t tileIndex, T len) : tileIndex_(tileIndex), length_(len) {}

  uint16_t tileIndex_;

  T length_;
};

/**
 * @class TLMMarkerManager
 * @brief Manages a collection of TLM markers, assuming strictly increasing marker ids
 *
 */
class TLMMarkerManager
{
  /**
   * @brief vector of @ref TilePartLength
   * stored in sequence as they appear in the code stream
   */
  using TL_VEC = std::vector<TilePartLength<uint32_t>>;

public:
  TLMMarkerManager()
      : tilePartLengthsIter_(), // Default-initialized iterator (invalid until tilePartLengths_
                                // exists)
        lastMarkerId_(-1), valid_(true)
  {}

  /**
   * @brief Pushes a @ref TilePartLength
   * @param tilePartLength Tile part information
   */
  void push_back(TilePartLength<uint32_t> tilePartLength)
  {
    if(!valid_)
      return;

    // push tile part length
    if(!tilePartLengths_)
    {
      tilePartLengths_ = std::make_unique<TL_VEC>();
      tilePartLengthsIter_ =
          tilePartLengths_->begin(); // Set to begin (empty vector’s begin == end)
    }
    tilePartLengths_->push_back(tilePartLength);
  }

  /**
   * @brief Resets the iterator state for traversing markers
   */
  void reset() noexcept
  {
    if(tilePartLengths_)
      tilePartLengthsIter_ = tilePartLengths_->begin();
  }

  /**
   * @brief Gets the next tile part length
   * @param peek If true, do not advance to the next entry
   * @return Pointer to the next tile part, or nullptr if none or invalid
   */
  TilePartLength<uint32_t>* next(bool peek = false) noexcept
  {
    if(!valid_ || !tilePartLengths_ || tilePartLengthsIter_ == tilePartLengths_->end())
      return nullptr;

    auto result = &(*tilePartLengthsIter_);
    if(!peek)
      ++tilePartLengthsIter_;

    return result;
  }

  /**
   * @brief Checks if the manager contains no markers
   * @return True if empty, false otherwise
   */
  [[nodiscard]] bool empty() const noexcept
  {
    return !tilePartLengths_ || tilePartLengths_->empty();
  }

  /**
   * @brief Validates marker id
   * @param markerId Marker index to check
   * @return True if the marker exists, false otherwise
   */
  [[nodiscard]] bool validateMarkerId(uint8_t markerId) noexcept
  {
    if(!valid_)
      return false;
    if(markerId <= lastMarkerId_)
    {
      grklog.warn("TLM: marker id %d is greater than last marker id %d. Disabling TLM.", markerId,
                  lastMarkerId_);
      valid_ = false;
      return false;
    }
    lastMarkerId_ = (int32_t)markerId;
    return true;
  }

private:
  /**
   * @brief Single TL_VEC storing all tile part lengths in order
   */
  std::unique_ptr<TL_VEC> tilePartLengths_;

  /**
   * @brief Iterator into the single TLM tile lengths vector
   */
  TL_VEC::iterator tilePartLengthsIter_;

  /**
   * @brief stores last valid marker id. Used to ensure stricly increasing ids
   *
   */
  int32_t lastMarkerId_;

  /**
   * @brief Flag indicating if the manager is valid (i.e., marker ids are strictly increasing)
   */
  bool valid_;
};

/**
 * @struct TLMMarker
 * @brief Reads/writes TLM markers
 */
struct TLMMarker
{
  /**
   * @brief Constructs a TLMMarker
   * @param numSignalledTiles number of tiles signalled in main header
   */
  explicit TLMMarker(uint16_t numSignalledTiles);

  /**
   * @brief Constructs a TLMMarker
   * @param numSignalledTiles number of tiles signalled in main header
   */
  TLMMarker(std::string& filePath, uint16_t numSignalledTiles, uint64_t tileStreamStart);

  /**
   * @brief Constructs a TLMMarker
   * @param stream @ref IStream for code stream
   */
  explicit TLMMarker(IStream* stream);

  /**
   * @brief Destroys a TLMMarker
   */
  ~TLMMarker() = default;

  /**
   * @brief Reads a TLM marker
   * @param headerData header data
   * @param headerSize size of header
   * @return true if success
   */
  bool read(std::span<uint8_t> headerData, uint16_t headerSize);

  /**
   * @brief Rewinds state so that tile part lengths can be read
   */
  void rewind() noexcept;

  /**
   * @brief Gets the number of tile parts for a given tile index
   * @param tileIndex Index of the tile
   * @return Number of tile parts, or 0 if invalid
   */
  uint8_t getNumTileParts(uint16_t tileIndex) const noexcept;

  /**
   * @brief Gets next @ref TilePartLength
   * @param peek - if true then do not advance to next @ref TilePartLength
   * @return @ref TilePartLength
   */
  TilePartLength<uint32_t>* next(bool peek);

  /**
   * @brief Marks the object as invalid, in case of corrupt TLM marker
   */
  void invalidate() noexcept;

  /**
   * @brief Checks if the object is valid
   * @return true if valid
   */
  [[nodiscard]] bool valid() const noexcept;

  /**
   * @brief Seeks to next scheduled tile part
   * @param tilesToDecompress tile window
   * @param stream @ref IStream
   */
  void seekNextSlated(TileWindow* tilesToDecompress, TileCache* tileCache, IStream* stream);

  /**
   * @brief Prepares to write TLM marker to code stream
   * @param numTilePartsTotal total number of tile parts in image
   * @return true if successful
   */
  bool writeBegin(uint32_t numTilePartsTotal);

  /**
   * @brief Adds a new tile part(decompression)
   *
   * @param info
   */
  void add(TilePartLength<uint32_t> info);

  /**
   * @brief Adds a new tile part (compression)
   *
   * Marker index is always 0 todo: fix
   * @param tile_index tile index
   * @param tile_part_size size of tile part
   */
  void add(uint16_t tile_index, uint32_t tile_part_size) noexcept;

  /**
   * @brief Finishes writing TLM marker
   * @return true if successful
   */
  bool writeEnd();

  /**
   * @brief Completes calculations such as absolute tile part start position
   * for all tile parts in tilePartsPerTile_
   *
   * @param tileStreamStart tile stream start
   */
  void readComplete(uint64_t tileStreamStart) noexcept;

  const TPSEQ_VEC& getTileParts(void) const;

private:
  std::unique_ptr<TLMMarkerManager> markerManager_;

  /**
   * @brief stores tile part info sequence for each tile
   * Useful if SOT markers don't store number of tile parts per tile
   */
  TPSEQ_VEC tilePartsPerTile_;

  /**
   * @brief @ref IStream
   */
  IStream* stream_ = nullptr;

  /**
   * @brief cached start of stream before writing TLM markers
   */
  uint64_t streamStart = 0;

  /**
   * @brief true if TLM markers are valid
   */
  bool valid_ = true;

  /**
   * @brief true if TLM markers store tile indices. If not,
   * then there must be only one tile part per tile,
   * and the tile parts must be stored in the stream
   * in order of tile index
   */
  bool hasTileIndices_ = false;

  /**
   * @brief used to track tile index when there are no tile indices
   * stored in markers
   */
  uint16_t tileCount_ = 0;

  /**
   * @brief number of tiles signalled in main header
   */
  uint16_t numSignalledTiles_ = 0;

  /**
   * @brief calculated start position for current tile part
   * parsed from markers
   */
  uint64_t tilePartStart_ = 0;

  std::string filePath_;
};

} // namespace grk