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
#include <memory>
#include <set>
#include <unordered_map>
#include <atomic>

namespace grk
{

/**
 * @struct SharedPtrSeq
 * @brief Sequence of shared_ptr<T>
 * @tparam T The type of elements to be stored as shared_ptr in the sequence
 */
template<typename T>
struct SharedPtrSeq
{
  using iterator = typename std::vector<std::shared_ptr<T>>::iterator;
  using const_iterator = typename std::vector<std::shared_ptr<T>>::const_iterator;
  std::shared_ptr<T>& operator[](size_t index)
  {
    return objStore_[index];
  }
  const std::shared_ptr<T>& operator[](size_t index) const
  {
    return objStore_[index];
  }

  /**
   * @brief Returns iterator to beginning of sequence
   * @return Iterator pointing to first shared_ptr
   */
  iterator begin()
  {
    return objStore_.begin();
  }

  /**
   * @brief Returns const iterator to end of sequence
   * @return Const iterator pointing past the last shared_ptr
   */
  const_iterator end() const
  {
    return objStore_.end();
  }

  /**
   * @brief Checks if there are no objects in store
   * @return true if sequence is empty, false otherwise
   */
  bool empty() const
  {
    return objStore_.empty();
  }

  /**
   * @brief Returns number of objects in store
   * @return Current number of shared_ptr elements in the sequence
   */
  size_t size() const
  {
    return objStore_.size();
  }

  /**
   * @brief Adds a pre-existing shared_ptr to the end of the sequence
   * @param ptr The shared_ptr to be added
   */
  void push_back(std::shared_ptr<T> ptr)
  {
    objStore_.push_back(std::move(ptr));
  }

  void resize(size_t N)
  {
    objStore_.resize(N);
  }

private:
  std::vector<std::shared_ptr<T>> objStore_;
};

/**
 * @struct TPSeq
 * @brief Sequence of shared_ptr<DataSlice> tile parts
 *  parsed from either TLM or SOT marker
 */
struct TPSeq : public SharedPtrSeq<DataSlice>
{
  /**
   * @brief Destroys a TPSeq
   */
  ~TPSeq() = default;

  /**
   * @brief Pushes a new tile part to the back of the sequence
   * @param tilePart tile part index
   * @param numTileParts number of tile parts in tile
   * @param offset tile part start position
   * @param length tile part length
   * @return true if successful
   */
  bool push_back(uint8_t tilePart, uint8_t numTileParts, uint64_t offset, uint32_t length)
  {
    // Once one tile part has non-zero number of tile parts, then every
    // subsequent tile part must have matching number of tile parts
    if(signalledNumTileParts_ && numTileParts != signalledNumTileParts_)
    {
      grklog.error("Number of tile parts %d does not match previous value %d", numTileParts,
                   signalledNumTileParts_);
      return false;
    }
    signalledNumTileParts_ = numTileParts;

    if(tilePart != size())
    {
      grklog.error("Tile part %d is out of sequence with current number of tile parts %d", tilePart,
                   size());
      return false;
    }
    SharedPtrSeq<DataSlice>::push_back(std::make_shared<DataSlice>(offset, length));
    return true;
  }

  /**
   * @brief Completes calculations such as absolute tile part offsets,
   * which are not available when TLM markers are parsed
   *
   * @param tileStreamOffset tile stream offset in code stream
   */
  void complete(uint64_t tileStreamOffset)
  {
    for(auto& part : *this)
    {
      part->offset_ += tileStreamOffset;
    }
    assert(!signalledNumTileParts_ || signalledNumTileParts_ == size());
    signalledNumTileParts_ = static_cast<uint8_t>(size());
  }

private:
  /**
   * @brief Number of tile parts signalled in code stream
   * This number is either explicitly stored in SOT, or deduced
   * from TLM markers
   */
  uint8_t signalledNumTileParts_ = 0;
};

using TPSEQ_VEC = std::vector<std::unique_ptr<TPSeq>>;

/**
 * @struct TPFetch
 * @brief Stores concurrent fetch request information for a single tile part
 *
 */
struct TPFetch : public DataSlice
{
  /**
   * @brief Construct a new TPFetch object
   *
   * @param offset tile part code stream offset
   * @param length tile part length
   * @param tileIndex tile index
   */
  TPFetch(uint64_t offset, uint64_t length, uint16_t tileIndex)
      : DataSlice(offset, length), tileIndex_(tileIndex)
  {}

  /**
   * @brief Copies next fetched data chunk into data buffer
   * Chunks are guaranteed to be received in order for a single CURL request
   *
   * @param chunk chunk buffer
   * @param chunkLen chunk length
   */
  void copy(uint8_t* chunk, size_t chunkLen)
  {
    if(!data_)
    {
      data_ = std::make_unique<uint8_t[]>(length_);
    }
    std::memcpy(data_.get() + fetchOffset_, chunk, chunkLen);
    fetchOffset_ += chunkLen;
  }

  uint16_t tileIndex_ = 0;
  std::unique_ptr<uint8_t[]> data_;
  size_t fetchOffset_ = 0;
  std::unique_ptr<IStream> stream_;
};

struct TPFetchSeq : SharedPtrSeq<TPFetch>
{
  void push_back(uint16_t tileIndex, const std::unique_ptr<TPSeq>& tileParts)
  {
    for(auto& part : *tileParts)
    {
      SharedPtrSeq<TPFetch>::push_back(
          std::make_shared<TPFetch>(part->offset_, part->length_, tileIndex));
    }
  }
  void push_back(uint16_t tileIndex, const std::unique_ptr<TPSeq>& tileParts,
                 std::vector<std::shared_ptr<TPFetch>>& outParts)
  {
    for(auto& part : *tileParts)
    {
      auto fetchPtr = std::make_shared<TPFetch>(part->offset_, part->length_, tileIndex);
      SharedPtrSeq<TPFetch>::push_back(fetchPtr);
      outParts.push_back(fetchPtr);
    }
  }

  static void
      genCollections(const TPSEQ_VEC* allTileParts, std::set<uint16_t>& slated,
                     std::shared_ptr<TPFetchSeq>& tilePartFetchFlat,
                     std::shared_ptr<std::unordered_map<uint16_t, std::shared_ptr<TPFetchSeq>>>&
                         tilePartFetchByTile)
  {
    for(auto& tileIndex : slated)
    {
      const auto& tileParts = (*allTileParts)[tileIndex];
      std::vector<std::shared_ptr<TPFetch>> tileFetchParts;

      tilePartFetchFlat->push_back(tileIndex, tileParts, tileFetchParts);

      auto [it, inserted] = tilePartFetchByTile->emplace(tileIndex, std::make_unique<TPFetchSeq>());
      if(inserted)
      {
        for(auto& fetchPtr : tileFetchParts)
        {
          it->second->SharedPtrSeq<TPFetch>::push_back(fetchPtr);
        }
      }
    }
  }

  uint8_t incrementFetchCount()
  {
    std::atomic_ref<uint8_t> atomicCount(fetchCount_);
    return atomicCount.fetch_add(1, std::memory_order_seq_cst) + 1;
  }

private:
  uint8_t fetchCount_ = 0;
};

} // namespace grk