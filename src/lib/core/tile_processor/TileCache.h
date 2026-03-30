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
#include <list>
#include <algorithm>

namespace grk
{

/**
 * @struct TileCacheEntry
 * @brief Store a tile cache entry
 */
struct TileCacheEntry
{
  explicit TileCacheEntry(ITileProcessor* p) : processor(p), dirty_(true) {}
  TileCacheEntry() : TileCacheEntry(nullptr) {}
  ~TileCacheEntry()
  {
    delete processor;
  }

  ITileProcessor* processor;
  bool dirty_;
};

/**
 * @class TileCache
 * @brief Cache tile processors using a simple array, initialized with the total number of tiles.
 *
 * When LRU eviction is enabled (maxActiveTiles_ > 0), accessing a tile promotes it
 * in the LRU list. When the active tile count exceeds the limit, the least-recently-used
 * tile's decompressed data is released via GRK_TILE_CACHE_LRU. The processor remains
 * so re-decompression can occur from the CompressedChunkCache.
 */
class TileCache
{
public:
  TileCache() : strategy_(GRK_TILE_CACHE_NONE), initialized_(false), maxActiveTiles_(0) {}

  ~TileCache()
  {
    for(auto* entry : cache_)
    {
      delete entry;
    }
  }

  void init(uint16_t numTiles)
  {
    if(initialized_)
    {
      return; // Prevent reinitialization
    }
    initialized_ = true;

    // Initialize cache array with nullptrs
    cache_.resize(numTiles, nullptr);
  }

  bool empty() const
  {
    return cache_.empty();
  }

  void setStrategy(uint32_t strategy)
  {
    strategy_ = strategy;
  }

  uint32_t getStrategy() const
  {
    return strategy_;
  }

  /**
   * @brief Set the maximum number of tiles that can hold decompressed data simultaneously.
   * 0 means no limit (all tiles stay active).
   */
  void setMaxActiveTiles(uint16_t maxActive)
  {
    maxActiveTiles_ = maxActive;
  }

  uint16_t getMaxActiveTiles() const
  {
    return maxActiveTiles_;
  }

  void setTruncated(void)
  {
    for(auto* entry : cache_)
    {
      if(!entry || !entry->processor)
        continue;
      entry->processor->setTruncated();
    }
  }

  void setDirty(bool dirty)
  {
    for(auto* entry : cache_)
    {
      if(!entry)
        continue;
      entry->dirty_ = dirty;
    }
  }

  void setDirty(uint16_t tileIndex, bool dirty)
  {
    if(tileIndex >= cache_.size())
      return;
    if(cache_[tileIndex])
    {
      cache_[tileIndex]->dirty_ = dirty;
    }
  }

  bool isDirty(uint16_t tileIndex)
  {
    if(tileIndex >= cache_.size())
      return false;
    return cache_[tileIndex] ? cache_[tileIndex]->dirty_ : false;
  }

  TileCacheEntry* put(uint16_t tile_index, ITileProcessor* processor)
  {
    if(tile_index >= cache_.size())
      return nullptr;
    if(!cache_[tile_index])
    {
      cache_[tile_index] = new TileCacheEntry(processor);
    }
    else
    {
      if(cache_[tile_index]->processor)
        delete cache_[tile_index]->processor;
      cache_[tile_index]->processor = processor;
    }

    // LRU: promote this tile and evict if over limit
    promoteLRU(tile_index);
    evictLRU();

    return cache_[tile_index];
  }

  TileCacheEntry* get(uint16_t tile_index)
  {
    if(tile_index >= cache_.size())
      return nullptr;

    // LRU: promote on access
    if(cache_[tile_index] && cache_[tile_index]->processor)
      promoteLRU(tile_index);

    return cache_[tile_index];
  }

  void release(uint16_t tileIndex)
  {
    if(cache_[tileIndex] && cache_[tileIndex]->processor)
      cache_[tileIndex]->processor->release(strategy_);
  }

  void resetSOTParsing()
  {
    for(auto* entry : cache_)
    {
      if(!entry || !entry->processor)
        continue;
      if(!entry->processor->isBestEffortDecompressed())
        entry->processor->resetSOTParsing();
    }
  }

  bool allSlatedSOTMarkersParsed(const std::set<uint16_t>& tilesSlatedForDecompression)
  {
    return std::all_of(tilesSlatedForDecompression.begin(), tilesSlatedForDecompression.end(),
                       [this](uint16_t tileId) {
                         if(tileId >= cache_.size())
                           return false;
                         auto* entry = cache_[tileId];
                         return entry && entry->processor &&
                                entry->processor->allSOTMarkersParsed();
                       });
  }

  bool setProgressionState(grk_progression_state state)
  {
    if(!state.single_tile)
      return false;

    auto cached = get(state.tile_index);
    if(!cached || !cached->processor)
      return false;

    uint16_t maxlayer = 0;
    for(uint8_t r = 0; r < state.num_resolutions; ++r)
    {
      maxlayer = std::max(maxlayer, state.layers_per_resolution[r]);
    }
    if(maxlayer != cached->processor->getTCP()->layersToDecompress_)
    {
      cached->dirty_ = true;
      cached->processor->getTCP()->layersToDecompress_ = maxlayer;
    }

    return true;
  }

  grk_progression_state getProgressionState(uint16_t tileIndex)
  {
    auto cached = get(tileIndex);
    if(!cached)
      return {};

    return cached->processor->getProgressionState();
  }

private:
  /**
   * @brief Move a tile to the front of the LRU list (most recently used).
   */
  void promoteLRU(uint16_t tileIndex)
  {
    if(maxActiveTiles_ == 0)
      return; // LRU disabled

    // Remove existing entry if present
    lruList_.remove(tileIndex);
    lruList_.push_front(tileIndex);
  }

  /**
   * @brief Evict least-recently-used tiles when over the active limit.
   *
   * Eviction releases the tile's decompressed data via GRK_TILE_CACHE_LRU
   * but keeps the processor for potential re-decompression.
   */
  void evictLRU()
  {
    if(maxActiveTiles_ == 0)
      return; // LRU disabled

    while(lruList_.size() > maxActiveTiles_)
    {
      uint16_t victim = lruList_.back();
      lruList_.pop_back();

      if(victim < cache_.size() && cache_[victim] && cache_[victim]->processor)
      {
        cache_[victim]->processor->release(GRK_TILE_CACHE_LRU);
      }
    }
  }

  std::vector<TileCacheEntry*> cache_; // Array of cache entries
  std::list<uint16_t> lruList_; // Front = MRU, back = LRU (tile indices with active data)
  uint32_t strategy_; // Cache strategy
  bool initialized_; // Flag to prevent reinitialization
  uint16_t maxActiveTiles_; // Max tiles with decompressed data (0 = unlimited)
};

} // namespace grk