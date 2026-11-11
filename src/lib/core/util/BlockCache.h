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

#include <memory_resource>
#include <new>
#include <unordered_map>
#include "SmallVector.h"
#include "intmath.h"

namespace grk
{

template<typename T, typename P>
class BlockCache
{
public:
  BlockCache(uint16_t numLayers, uint32_t gridWidth, uint32_t gridHeight, P* blockInitializer,
             std::pmr::memory_resource* arena)
      : gridWidth_(gridWidth), sparseBlocks_(arena), blockInitializer_(blockInitializer),
        arena_(arena), numLayers_(numLayers)
  {
    chunksPerRow_ = ceildiv<uint32_t>(gridWidth, 16);
    chunkWidth_ = ceildiv<uint32_t>(gridWidth, chunksPerRow_);
    uint32_t chunksPerColumn = ceildiv<uint32_t>(gridHeight, 256 / chunkWidth_);
    chunkHeight_ = ceildiv<uint32_t>(gridHeight, chunksPerColumn);
    chunkSize_ = chunkWidth_ * chunkHeight_;
    constexpr size_t maxDenseTableBytes = 4096;
    size_t numChunks = (size_t)chunksPerRow_ * chunksPerColumn;
    // dense tables over huge precinct grids run out of memory
    if(numChunks * sizeof(T*) <= maxDenseTableBytes)
      chunks_.resize(numChunks);
  }
  ~BlockCache(void)
  {
    for(auto chunk : chunks_)
    {
      if(!chunk)
        continue;
      for(uint32_t i = 0; i < chunkSize_; ++i)
        chunk[i].~T();
      arena_->deallocate(chunk, sizeof(T) * chunkSize_, alignof(T));
    }
  }

private:
  struct ChunkSlot
  {
    size_t chunk;
    uint32_t offset;
  };
  ChunkSlot locate(uint64_t index) const
  {
    auto x = (uint32_t)(index % gridWidth_);
    auto y = (uint32_t)(index / gridWidth_);
    return {(size_t)(y / chunkHeight_) * chunksPerRow_ + x / chunkWidth_,
            (y % chunkHeight_) * chunkWidth_ + x % chunkWidth_};
  }

public:
  T* tryGet(uint64_t index)
  {
    T* item;
    if(chunks_.empty())
    {
      auto iter = sparseBlocks_.find(index);
      if(iter == sparseBlocks_.end())
        return nullptr;
      item = &iter->second;
    }
    else
    {
      auto slot = locate(index);
      auto chunk = chunks_[slot.chunk];
      if(!chunk)
        return nullptr;
      item = chunk + slot.offset;
    }
    return item->empty() ? nullptr : item;
  }

  T* get(uint64_t index)
  {
    T* item;
    if(chunks_.empty())
    {
      item = &sparseBlocks_.try_emplace(index, numLayers_).first->second;
    }
    else
    {
      auto slot = locate(index);
      auto& chunk = chunks_[slot.chunk];
      if(!chunk)
      {
        chunk = static_cast<T*>(arena_->allocate(sizeof(T) * chunkSize_, alignof(T)));
        for(uint32_t i = 0; i < chunkSize_; ++i)
          new(chunk + i) T(numLayers_);
      }
      item = chunk + slot.offset;
    }
    blockInitializer_->initCodeBlock(item, (uint32_t)index);
    return item;
  }

private:
  uint32_t gridWidth_;
  uint32_t chunkWidth_;
  uint32_t chunkHeight_;
  uint32_t chunkSize_;
  uint32_t chunksPerRow_;
  SmallVector<T*, 1> chunks_;
  std::pmr::unordered_map<uint64_t, T> sparseBlocks_;
  P* blockInitializer_;
  std::pmr::memory_resource* arena_;
  uint16_t numLayers_;
};

} // namespace grk
