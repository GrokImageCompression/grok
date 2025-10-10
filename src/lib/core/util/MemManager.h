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

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <unordered_map>

#ifdef _WIN32
#include <malloc.h>
#endif

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

namespace grk
{

const size_t grk_buffer_alignment = 64;

template<typename T>
uint32_t grk_make_aligned_width(uint32_t width)
{
  assert(width);
  assert(sizeof(T) <= grk_buffer_alignment);
  size_t align = grk_buffer_alignment / sizeof(T);
  return (uint32_t)((((uint64_t)width + align - 1) / align) * align);
}

class MemoryManager
{
public:
  static MemoryManager& get()
  {
    static MemoryManager instance;
    return instance;
  }

  void* malloc(size_t size)
  {
    if(size == 0)
      return nullptr;
    void* ptr = std::malloc(size);
    if(ptr && track_stats)
    {
      std::lock_guard<std::mutex> lock(stats_mutex);
      allocations++;
      total_allocated += size;
      current_allocated += size;
      peak_allocated = std::max(peak_allocated, current_allocated);
      if(track_details)
        allocation_map[ptr] = size;
    }
    return ptr;
  }

  void* calloc(size_t num, size_t size)
  {
    if(num == 0 || size == 0)
      return nullptr;
    void* ptr = std::calloc(num, size);
    if(ptr && track_stats)
    {
      std::lock_guard<std::mutex> lock(stats_mutex);
      allocations++;
      size_t total_size = num * size;
      total_allocated += total_size;
      current_allocated += total_size;
      peak_allocated = std::max(peak_allocated, current_allocated);
      if(track_details)
        allocation_map[ptr] = total_size;
    }
    return ptr;
  }

  void* aligned_malloc(size_t bytes)
  {
    return aligned_malloc(grk_buffer_alignment, bytes);
  }

  void* aligned_malloc(size_t alignment, size_t bytes)
  {
    assert((alignment != 0U) && ((alignment & (alignment - 1U)) == 0U));
    assert(alignment >= sizeof(void*));

    if(bytes == 0)
      return nullptr;

    bytes = ((bytes + alignment - 1) / alignment) * alignment;

#ifdef _WIN32
    void* ptr = _aligned_malloc(bytes, alignment);
#else
    void* ptr = std::aligned_alloc(alignment, bytes);
#endif

    if(ptr && track_stats)
    {
      std::lock_guard<std::mutex> lock(stats_mutex);
      allocations++;
      total_allocated += bytes;
      current_allocated += bytes;
      peak_allocated = std::max(peak_allocated, current_allocated);
      if(track_details)
        allocation_map[ptr] = bytes;
    }
    return ptr;
  }

  void* realloc(void* ptr, size_t new_size)
  {
    if(new_size == 0)
      return nullptr;

    size_t old_size = 0;
    bool was_allocated = false;
    if(track_stats && ptr && track_details)
    {
      std::lock_guard<std::mutex> lock(stats_mutex);
      auto it = allocation_map.find(ptr);
      if(it != allocation_map.end())
      {
        old_size = it->second;
        allocation_map.erase(it); // Erase before realloc
        was_allocated = true;
      }
    }

    void* new_ptr = std::realloc(ptr, new_size);
    if(new_ptr && track_stats)
    {
      std::lock_guard<std::mutex> lock(stats_mutex);
      if(ptr)
      {
        reallocations++;
        if(was_allocated)
          current_allocated -= old_size;
      }
      else
      {
        allocations++;
      }
      current_allocated += new_size;
      peak_allocated = std::max(peak_allocated, current_allocated);
      total_allocated += new_size;
      if(track_details)
        allocation_map[new_ptr] = new_size;
    }
    return new_ptr;
  }

  void free(void* ptr)
  {
    if(!ptr)
      return;
    if(track_stats)
    {
      std::lock_guard<std::mutex> lock(stats_mutex);
      deallocations++;
      if(track_details)
      {
        auto it = allocation_map.find(ptr);
        if(it != allocation_map.end())
        {
          current_allocated -= it->second;
          allocation_map.erase(it);
        }
      }
    }
    std::free(ptr);
  }

  void aligned_free(void* ptr)
  {
    if(!ptr)
      return;
    if(track_stats)
    {
      std::lock_guard<std::mutex> lock(stats_mutex);
      deallocations++;
      if(track_details)
      {
        auto it = allocation_map.find(ptr);
        if(it != allocation_map.end())
        {
          current_allocated -= it->second;
          allocation_map.erase(it);
        }
      }
    }
#ifdef _WIN32
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
  }

  struct Stats
  {
    size_t allocations = 0;
    size_t deallocations = 0;
    size_t reallocations = 0;
    size_t total_allocated = 0;
    size_t current_allocated = 0;
    size_t peak_allocated = 0;
  };

  Stats get_stats() const
  {
    std::lock_guard<std::mutex> lock(stats_mutex);
    return {allocations,     deallocations,     reallocations,
            total_allocated, current_allocated, peak_allocated};
  }

  void print_stats() const
  {
    if(!track_stats)
      return;

    auto stats = get_stats();
    double total_mb = static_cast<double>(stats.total_allocated) / (1024 * 1024);
    double current_mb = static_cast<double>(stats.current_allocated) / (1024 * 1024);
    double peak_mb = static_cast<double>(stats.peak_allocated) / (1024 * 1024);

    std::printf("Memory Statistics:\n");
    std::printf("  Allocations: %zu\n", stats.allocations);
    std::printf("  Deallocations: %zu\n", stats.deallocations);
    std::printf("  Reallocations: %zu\n", stats.reallocations);
    std::printf("  Total Allocated: %.2f MB\n", total_mb);
    std::printf("  Current Allocated: %.2f MB\n", current_mb);
    std::printf("  Peak Allocated: %.2f MB\n", peak_mb);
    std::printf("  Current Active Allocations: %zu\n", stats.allocations - stats.deallocations);
  }

private:
  MemoryManager()
  {
    const char* debug_env = std::getenv("GRK_DEBUG");
    track_stats = (debug_env && std::atoi(debug_env) == 5);
    track_details = track_stats; // Could be separated with another env var if desired
  }

  ~MemoryManager()
  {
    if(track_stats)
    {
      print_stats();
    }
  }

  MemoryManager(const MemoryManager&) = delete;
  MemoryManager& operator=(const MemoryManager&) = delete;

  bool track_stats = false;
  bool track_details = false; // Controls whether to use allocation_map
  mutable std::mutex stats_mutex;
  std::atomic<size_t> allocations{0};
  std::atomic<size_t> deallocations{0};
  std::atomic<size_t> reallocations{0};
  std::atomic<size_t> total_allocated{0};
  size_t current_allocated = 0; // Not atomic, protected by mutex
  size_t peak_allocated = 0; // Not atomic, protected by mutex
  std::unordered_map<void*, size_t> allocation_map; // Tracks size of each allocation
};

// Inline functions for convenience
inline void* grk_malloc(size_t size)
{
  return MemoryManager::get().malloc(size);
}
inline void* grk_calloc(size_t num, size_t size)
{
  return MemoryManager::get().calloc(num, size);
}
inline void* grk_aligned_malloc(size_t bytes)
{
  return MemoryManager::get().aligned_malloc(bytes);
}
inline void* grk_aligned_malloc(size_t alignment, size_t bytes)
{
  return MemoryManager::get().aligned_malloc(alignment, bytes);
}
inline void* grk_realloc(void* ptr, size_t new_size)
{
  return MemoryManager::get().realloc(ptr, new_size);
}
inline void grk_free(void* ptr)
{
  MemoryManager::get().free(ptr);
}
inline void grk_aligned_free(void* ptr)
{
  MemoryManager::get().aligned_free(ptr);
}
inline void grk_print_memory_stats()
{
  MemoryManager::get().print_stats();
}

} // namespace grk