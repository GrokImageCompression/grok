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

#include <cstddef>
#include <map>
#include <memory_resource>
#include <mutex>
#include <utility>
#include <vector>

namespace grk
{

struct TileArenaObject
{
  static void* operator new(size_t size, std::pmr::memory_resource* arena)
  {
    return arena->allocate(size);
  }
  static void operator delete(void*, std::pmr::memory_resource*) noexcept {}
  // std::pmr::monotonic_buffer_resource frees all of its memory when it is destroyed
  static void operator delete(void*) noexcept {}
};

class TileArenaChunkPool : public std::pmr::memory_resource
{
public:
  ~TileArenaChunkPool() override
  {
    for(auto& [chunkLayout, chunks] : freeChunks_)
      for(auto chunk : chunks)
        std::pmr::new_delete_resource()->deallocate(chunk, chunkLayout.first, chunkLayout.second);
  }

private:
  void* do_allocate(size_t bytes, size_t alignment) override
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto& chunks = freeChunks_[{bytes, alignment}];
      if(!chunks.empty())
      {
        auto chunk = chunks.back();
        chunks.pop_back();
        return chunk;
      }
    }
    return std::pmr::new_delete_resource()->allocate(bytes, alignment);
  }
  void do_deallocate(void* chunk, size_t bytes, size_t alignment) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    freeChunks_[{bytes, alignment}].push_back(chunk);
  }
  bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
  {
    return this == &other;
  }
  std::mutex mutex_;
  std::map<std::pair<size_t, size_t>, std::vector<void*>> freeChunks_;
};

} // namespace grk
