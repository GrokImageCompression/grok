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

#include <mutex>
#include <condition_variable>
#include <vector>
#include <map>
#include <cstdint>
#include <stdexcept>
#include <list>

namespace grk
{

/**
 * @class ChunkBuffer
 * @brief Manages a partially ordered map of buffer chunks that are
 * added asynchronously out of order.
 *
 * Behaves like a single contiguous buffer. Caller may have to wait
 * until the desired region of the "contiguous" buffer actually arrives
 * Note: supports "zero-copy"
 *
 * @tparam T chunk index type
 */
template<typename T = uint16_t>
class ChunkBuffer
{
public:
  ChunkBuffer(size_t chunkSize, size_t offset, size_t length)
      : chunkSize_(chunkSize > length ? length : chunkSize),
        offset_(offset > length ? length : offset), length_(length), initialOffset_(offset_),
        contiguous_length_(offset_)
  {}
  ~ChunkBuffer() = default;

  size_t size() const
  {
    return length_;
  }

  size_t offset() const
  {
    return offset_;
  }

  size_t chunkSize() const
  {
    return chunkSize_;
  }

  bool set_offset(size_t new_offset)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if(new_offset > length_)
    {
#ifdef DEBUG_SEG_BUF
      grklog.warn("ChunkBuffer: attempt to increment offset out of bounds");
#endif
      offset_ = length_;
      return false;
    }
    if(new_offset > contiguous_length_)
      cv_.wait(lock, [this, new_offset]() { return new_offset <= contiguous_length_; });
    offset_ = new_offset;
    return true;
  }

  bool increment_offset(std::ptrdiff_t off)
  {
    if(off < 0)
      return false;
    else if(off == 0)
      return true;
    std::unique_lock<std::mutex> lock(mutex_);

    size_t new_offset = offset_ + (size_t)off;
    if(new_offset > length_)
    {
#ifdef DEBUG_SEG_BUF
      grklog.warn("ChunkBuffer: attempt to increment offset out of bounds");
#endif
      offset_ = length_;
      return false;
    }
    if(new_offset > contiguous_length_)
      cv_.wait(lock, [this, new_offset]() { return new_offset <= contiguous_length_; });
    offset_ = new_offset;
    return true;
  }
  const uint8_t* currPtr(size_t desired_region) const
  {
    std::unique_lock<std::mutex> lock(mutex_);
    size_t relative_offset = offset_ - initialOffset_;

    // Wait until the desired region is contiguous
    if(relative_offset + desired_region > contiguous_length_ - initialOffset_)
    {
      cv_.wait(lock, [this, relative_offset, desired_region]() {
        return relative_offset + desired_region <= contiguous_length_ - initialOffset_;
      });
    }

    size_t start_chunk = relative_offset / chunkSize_;
    size_t offset_in_chunk = relative_offset % chunkSize_;
    size_t end_offset = relative_offset + desired_region;

    // 1. Handle out of bounds
    if(end_offset > length_ - initialOffset_)
    {
      grklog.warn("Out of bounds - truncating");
      end_offset = length_ - initialOffset_;
    }

    // 2. Try to find region in buffers
    auto it = buffers_.find(static_cast<T>(start_chunk));
    if(it == buffers_.end())
    {
      throw std::runtime_error("Missing chunk in contiguous sequence");
    }
    const auto& chunk_data = it->second;
    if(offset_in_chunk + desired_region <= chunk_data.size())
    {
      return chunk_data.data() + offset_in_chunk;
    }

    // 3. Create contiguous buffer
    auto& result = owned_buffers_.emplace_back(offset_, std::vector<uint8_t>());
    auto& buffer = result.second;
    buffer.reserve(desired_region);
    size_t bytes_remaining = desired_region;
    for(size_t i = start_chunk; bytes_remaining > 0 && static_cast<T>(i) <= last_contiguous_chunk_;
        ++i)
    {
      auto it = buffers_.find(static_cast<T>(i));
      if(it == buffers_.end())
      {
        throw std::runtime_error("Missing chunk in contiguous sequence");
      }
      const auto& chunk_data = it->second;
      size_t chunk_start = (i == start_chunk) ? offset_in_chunk : 0;
      size_t bytes_to_copy = std::min(bytes_remaining, chunk_data.size() - chunk_start);
      buffer.insert(buffer.end(), chunk_data.begin() + static_cast<ptrdiff_t>(chunk_start),
                    chunk_data.begin() + static_cast<ptrdiff_t>(chunk_start + bytes_to_copy));
      bytes_remaining -= bytes_to_copy;
    }
    return buffer.data();
  }
  void add(T fetch_index, const uint8_t* buffer, size_t size)
  {
    if(size > chunkSize_)
    {
      throw std::runtime_error("Buffer size exceeds chunk size");
    }

    {
      std::unique_lock<std::mutex> lock(mutex_);
      // Add the new chunk to buffers
      buffers_.emplace(fetch_index, std::vector<uint8_t>(buffer, buffer + size));
      // Update the heap and get the last contiguous chunk
      auto contiguous_chunk = bufferheap_.push_and_pop(fetch_index);

      // Update contiguous_length_ only if it grows
      size_t new_contiguous_length = initialOffset_; // Default to initial offset
      if(contiguous_chunk.has_value())
      {
        // Valid contiguous chunk exists
        last_contiguous_chunk_ = *contiguous_chunk;
        auto it = buffers_.find(last_contiguous_chunk_);
        if(it == buffers_.end())
        {
          throw std::runtime_error("Invalid contiguous chunk index returned by heap");
        }
        new_contiguous_length =
            initialOffset_ + last_contiguous_chunk_ * chunkSize_ + it->second.size();
      }
      new_contiguous_length = std::min(new_contiguous_length, length_);
      if(new_contiguous_length > contiguous_length_)
      {
        contiguous_length_ = new_contiguous_length;
      }
    }
    cv_.notify_all();
  }
  void free_before(size_t offset)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if(offset > contiguous_length_)
      offset = contiguous_length_;

    // 1. remove owned buffers whose end offset is less than or equal to offset
    owned_buffers_.remove_if([offset](const std::pair<size_t, std::vector<uint8_t>>& buf) {
      size_t buf_end = buf.first + buf.second.size();
      return buf_end <= offset;
    });

    // 2. remove chunk buffers whose end offset is less than or equal to offset
    for(auto it = buffers_.begin(); it != buffers_.end();)
    {
      size_t chunk_end = static_cast<size_t>(it->first) * chunkSize_ + it->second.size();
      if(chunk_end <= offset)
        it = buffers_.erase(it);
      else
        break;
    }
  }

private:
  size_t chunkSize_;
  size_t offset_;
  size_t length_;
  size_t initialOffset_;
  std::map<T, std::vector<uint8_t>> buffers_;
  SimpleHeap<T> bufferheap_;
  T last_contiguous_chunk_ = 0;
  size_t contiguous_length_ = 0; // Peak contiguous length, only grows
  mutable std::list<std::pair<size_t, std::vector<uint8_t>>> owned_buffers_; // Mutable for currPtr
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
};

} // namespace grk