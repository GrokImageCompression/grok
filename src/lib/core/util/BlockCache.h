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

#include <new>
#include <unordered_map>

namespace grk
{

template<typename T, typename P>
class BlockCache
{
public:
  BlockCache(uint16_t numLayers, uint32_t maxNumBlocks, P* blockInitializer)
      : chunkSize_(std::min<uint32_t>(maxNumBlocks, 256)), blockInitializer_(blockInitializer),
        numLayers_(numLayers)
  {}
  ~BlockCache(void)
  {
    for(auto& chunk : chunks_)
    {
      for(uint32_t i = 0; i < chunkSize_; ++i)
        chunk.second[i].~T();
      ::operator delete(chunk.second, std::align_val_t(alignof(T)));
    }
  }

  T* tryGet(uint64_t index)
  {
    uint64_t chunkIndex = index / chunkSize_;
    if(currChunk_ == nullptr || chunkIndex != currChunkIndex_)
    {
      auto iter = chunks_.find(chunkIndex);
      if(iter == chunks_.end())
        return nullptr;
      currChunk_ = iter->second;
      currChunkIndex_ = chunkIndex;
    }
    auto item = currChunk_ + index % chunkSize_;
    return item->empty() ? nullptr : item;
  }

  T* get(uint64_t index)
  {
    uint64_t chunkIndex = index / chunkSize_;
    if(currChunk_ == nullptr || chunkIndex != currChunkIndex_)
    {
      currChunkIndex_ = chunkIndex;
      auto iter = chunks_.find(chunkIndex);
      if(iter != chunks_.end())
      {
        currChunk_ = iter->second;
      }
      else
      {
        currChunk_ =
            static_cast<T*>(::operator new(sizeof(T) * chunkSize_, std::align_val_t(alignof(T))));
        for(uint32_t i = 0; i < chunkSize_; ++i)
          new(currChunk_ + i) T(numLayers_);
        chunks_[chunkIndex] = currChunk_;
      }
    }
    auto item = currChunk_ + index % chunkSize_;
    blockInitializer_->initCodeBlock(item, (uint32_t)index);
    return item;
  }

private:
  std::unordered_map<uint64_t, T*> chunks_;
  uint32_t chunkSize_;
  T* currChunk_ = nullptr;
  uint64_t currChunkIndex_ = 0;
  P* blockInitializer_;
  uint16_t numLayers_;
};

} // namespace grk
