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
#include "SmallVector.h"

namespace grk
{

template<typename T, typename P>
class BlockCache
{
public:
  BlockCache(uint16_t numLayers, uint32_t maxNumBlocks, P* blockInitializer)
      : chunkSize_(std::min<uint32_t>(maxNumBlocks, 256)), blockInitializer_(blockInitializer),
        numLayers_(numLayers)
  {
    chunks_.resize((maxNumBlocks + chunkSize_ - 1) / chunkSize_);
  }
  ~BlockCache(void)
  {
    for(auto chunk : chunks_)
    {
      if(!chunk)
        continue;
      for(uint32_t i = 0; i < chunkSize_; ++i)
        chunk[i].~T();
      ::operator delete(chunk, std::align_val_t(alignof(T)));
    }
  }

  T* tryGet(uint64_t index)
  {
    auto chunk = chunks_[index / chunkSize_];
    if(!chunk)
      return nullptr;
    auto item = chunk + index % chunkSize_;
    return item->empty() ? nullptr : item;
  }

  T* get(uint64_t index)
  {
    auto& chunk = chunks_[index / chunkSize_];
    if(!chunk)
    {
      chunk = static_cast<T*>(::operator new(sizeof(T) * chunkSize_, std::align_val_t(alignof(T))));
      for(uint32_t i = 0; i < chunkSize_; ++i)
        new(chunk + i) T(numLayers_);
    }
    auto item = chunk + index % chunkSize_;
    blockInitializer_->initCodeBlock(item, (uint32_t)index);
    return item;
  }

private:
  uint32_t chunkSize_;
  SmallVector<T*, 1> chunks_;
  P* blockInitializer_;
  uint16_t numLayers_;
};

} // namespace grk
