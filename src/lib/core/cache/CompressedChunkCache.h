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
#include <cstdlib>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "DiskCache.h"
#include "TPFetchSeq.h"
#include "MemStream.h"
#include "IStream.h"

namespace grk
{

/**
 * @class CompressedChunkCache
 * @brief Per-file LRU cache that manages compressed tile part data in memory.
 *
 * Approach B (minimal): the cache does NOT duplicate TPFetch data.
 * It holds shared ownership of each tile's TPFetchSeq and tracks
 * total in-memory compressed bytes. When the high water mark is
 * exceeded, the LRU tile's TPFetch::data_ buffers are serialized to
 * the DiskCache and released. On reload, the buffers and mem-streams
 * are recreated from disk.
 *
 * Cache size is controlled by the GRK_CACHEMAX or GDAL_CACHEMAX
 * environment variable. Defaults to 256 MB.
 */
class CompressedChunkCache
{
public:
  /**
   * @brief Construct the cache.
   * @param maxBytes      memory budget in bytes (0 = read from env)
   * @param diskCache     optional disk cache for eviction spill
   * @param codecFormat   codec format used to recreate mem-streams on reload
   */
  explicit CompressedChunkCache(size_t maxBytes = 0, std::shared_ptr<DiskCache> diskCache = nullptr,
                                GRK_CODEC_FORMAT codecFormat = GRK_CODEC_J2K)
      : maxBytes_(resolveMaxBytes(maxBytes)), currentBytes_(0), diskCache_(std::move(diskCache)),
        codecFormat_(codecFormat)
  {}

  /**
   * @brief Register a tile's fetched data with the cache.
   *
   * Takes shared ownership of the TPFetchSeq. The actual byte
   * buffers live in TPFetch::data_; we only track their sizes.
   */
  void put(uint16_t tileIndex, std::shared_ptr<TPFetchSeq> seq)
  {
    if(!seq || seq->empty())
      return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Remove existing entry if any
    auto it = entries_.find(tileIndex);
    if(it != entries_.end())
    {
      if(it->second.inMemory)
        currentBytes_ -= it->second.dataSize;
      removeLRU(tileIndex);
      entries_.erase(it);
    }

    Entry entry;
    entry.seq = std::move(seq);
    entry.dataSize = calcSeqDataSize(*entry.seq);
    entry.inMemory = true;
    currentBytes_ += entry.dataSize;

    entries_[tileIndex] = std::move(entry);
    pushFrontLRU(tileIndex);

    evictToFit();
  }

  /**
   * @brief Ensure a tile's compressed data is in memory.
   *
   * If the data was spilled to disk, it is reloaded and the
   * TPFetch::data_ / stream_ pointers are repopulated.
   *
   * @return the TPFetchSeq, or nullptr if tile not in cache.
   */
  std::shared_ptr<TPFetchSeq> get(uint16_t tileIndex)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = entries_.find(tileIndex);
    if(it == entries_.end())
      return nullptr;

    auto& entry = it->second;

    // Reload from disk if spilled
    if(!entry.inMemory)
    {
      if(!reloadFromDisk(tileIndex, entry))
        return nullptr;
    }

    promoteLRU(tileIndex);
    return entry.seq;
  }

  /**
   * @brief Check if a tile is tracked (memory or disk).
   */
  bool contains(uint16_t tileIndex) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.find(tileIndex) != entries_.end();
  }

  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    lruList_.clear();
    lruMap_.clear();
    currentBytes_ = 0;
    if(diskCache_)
      diskCache_->clear();
  }

  size_t currentBytes() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentBytes_;
  }
  size_t maxBytes() const
  {
    return maxBytes_;
  }
  size_t size() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
  }

private:
  struct Entry
  {
    std::shared_ptr<TPFetchSeq> seq;
    bool inMemory = true;
    size_t dataSize = 0;
  };

  static size_t calcSeqDataSize(const TPFetchSeq& seq)
  {
    size_t total = 0;
    for(size_t i = 0; i < seq.size(); ++i)
    {
      const auto& tp = seq[i];
      if(tp && tp->data_)
        total += tp->length_;
    }
    return total;
  }

  // LRU helpers
  void pushFrontLRU(uint16_t tileIndex)
  {
    lruList_.push_front(tileIndex);
    lruMap_[tileIndex] = lruList_.begin();
  }

  void removeLRU(uint16_t tileIndex)
  {
    auto it = lruMap_.find(tileIndex);
    if(it != lruMap_.end())
    {
      lruList_.erase(it->second);
      lruMap_.erase(it);
    }
  }

  void promoteLRU(uint16_t tileIndex)
  {
    auto it = lruMap_.find(tileIndex);
    if(it != lruMap_.end())
    {
      lruList_.splice(lruList_.begin(), lruList_, it->second);
    }
  }

  // Evict LRU entries until under memory budget
  void evictToFit()
  {
    while(maxBytes_ > 0 && currentBytes_ > maxBytes_ && !lruList_.empty())
    {
      auto lruTile = lruList_.back();
      auto it = entries_.find(lruTile);
      if(it == entries_.end())
      {
        // Orphaned LRU entry — just remove
        lruList_.pop_back();
        lruMap_.erase(lruTile);
        continue;
      }

      auto& entry = it->second;
      if(entry.inMemory)
        spillToDisk(lruTile, entry);
      else
      {
        // Already spilled; move to front to avoid infinite loop
        lruList_.splice(lruList_.begin(), lruList_, lruMap_[lruTile]);
        break;
      }
    }
  }

  /**
   * @brief Serialize tile part data to disk and release in-memory buffers.
   *
   * Disk format:
   *   [uint16_t numParts]
   *   For each part: [uint64_t offset][uint64_t length]
   *   For each part: [length bytes of data]
   */
  void spillToDisk(uint16_t tileIndex, Entry& entry)
  {
    if(!diskCache_ || !entry.seq)
      return;

    auto& seq = *entry.seq;
    uint16_t numParts = static_cast<uint16_t>(seq.size());

    // Calculate total serialized size
    size_t headerSize = sizeof(uint16_t) + numParts * (sizeof(uint64_t) + sizeof(uint64_t));
    size_t dataSize = 0;
    for(size_t i = 0; i < numParts; ++i)
    {
      if(seq[i] && seq[i]->data_)
        dataSize += seq[i]->length_;
    }

    std::vector<uint8_t> buf(headerSize + dataSize);
    uint8_t* ptr = buf.data();

    // Write header
    std::memcpy(ptr, &numParts, sizeof(numParts));
    ptr += sizeof(numParts);

    for(size_t i = 0; i < numParts; ++i)
    {
      uint64_t offset = seq[i] ? seq[i]->offset_ : 0;
      uint64_t length = seq[i] ? seq[i]->length_ : 0;
      std::memcpy(ptr, &offset, sizeof(offset));
      ptr += sizeof(offset);
      std::memcpy(ptr, &length, sizeof(length));
      ptr += sizeof(length);
    }

    // Write data
    for(size_t i = 0; i < numParts; ++i)
    {
      if(seq[i] && seq[i]->data_)
      {
        std::memcpy(ptr, seq[i]->data_.get(), seq[i]->length_);
        ptr += seq[i]->length_;
      }
    }

    diskCache_->store(tileIndex, buf.data(), buf.size());

    // Release in-memory buffers
    for(size_t i = 0; i < numParts; ++i)
    {
      if(seq[i])
      {
        seq[i]->data_.reset();
        seq[i]->stream_.reset();
        seq[i]->fetchOffset_ = 0;
      }
    }

    currentBytes_ -= entry.dataSize;
    entry.inMemory = false;
  }

  /**
   * @brief Reload tile part data from disk into the existing TPFetch objects.
   */
  bool reloadFromDisk(uint16_t tileIndex, Entry& entry)
  {
    if(!diskCache_ || !entry.seq)
      return false;

    auto blob = diskCache_->load(tileIndex);
    if(!blob.has_value())
      return false;

    auto& data = blob.value();
    const uint8_t* ptr = data.data();
    const uint8_t* end = ptr + data.size();

    if(static_cast<size_t>(end - ptr) < sizeof(uint16_t))
      return false;

    uint16_t numParts;
    std::memcpy(&numParts, ptr, sizeof(numParts));
    ptr += sizeof(numParts);

    auto& seq = *entry.seq;
    if(numParts != static_cast<uint16_t>(seq.size()))
      return false;

    size_t headerRemaining = numParts * (sizeof(uint64_t) + sizeof(uint64_t));
    if(static_cast<size_t>(end - ptr) < headerRemaining)
      return false;

    // Read part headers (offset, length) — skip since TPFetch already has them
    ptr += headerRemaining;

    // Read data into TPFetch objects
    for(size_t i = 0; i < numParts; ++i)
    {
      uint64_t length = seq[i] ? seq[i]->length_ : 0;
      if(length > 0)
      {
        if(static_cast<size_t>(end - ptr) < length)
          return false;

        seq[i]->data_ = std::make_unique<uint8_t[]>(length);
        std::memcpy(seq[i]->data_.get(), ptr, length);
        seq[i]->fetchOffset_ = length;
        seq[i]->stream_ = std::unique_ptr<IStream>(
            memStreamCreate(seq[i]->data_.get(), length, false, nullptr, codecFormat_, true));
        ptr += length;
      }
    }

    entry.inMemory = true;
    currentBytes_ += entry.dataSize;
    return true;
  }

  /**
   * @brief Resolve max bytes from environment or use provided value.
   *
   * Checks GRK_CACHEMAX first, then GDAL_CACHEMAX.
   */
  static size_t resolveMaxBytes(size_t provided)
  {
    if(provided > 0)
      return provided;

    const char* env = std::getenv("GRK_CACHEMAX");
    if(!env)
      env = std::getenv("GDAL_CACHEMAX");

    if(env)
    {
      std::string s(env);
      if(s.empty())
        return kDefaultMaxBytes;

      char suffix = s.back();
      if(suffix == 'M' || suffix == 'm')
        return static_cast<size_t>(std::stoull(s.substr(0, s.size() - 1))) * 1024 * 1024;
      if(suffix == 'G' || suffix == 'g')
        return static_cast<size_t>(std::stoull(s.substr(0, s.size() - 1))) * 1024 * 1024 * 1024;

      return static_cast<size_t>(std::stoull(s));
    }

    return kDefaultMaxBytes;
  }

  static constexpr size_t kDefaultMaxBytes = 256 * 1024 * 1024; // 256 MB

  size_t maxBytes_;
  size_t currentBytes_;
  std::shared_ptr<DiskCache> diskCache_;
  GRK_CODEC_FORMAT codecFormat_;

  mutable std::mutex mutex_;
  std::list<uint16_t> lruList_; // front = MRU, back = LRU
  std::unordered_map<uint16_t, std::list<uint16_t>::iterator> lruMap_;
  std::unordered_map<uint16_t, Entry> entries_;
};

} // namespace grk
