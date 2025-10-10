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
#include <mutex>
#include <queue>
#include <stdexcept>

namespace grk
{

/**
 * @brief A thread-safe slab-based buffer pool with configurable slot size and number of slots.
 *
 * Manages a single contiguous slab of memory, divided into slots of a specified size, with a
 * specified number of slots. Provides allocation and recycling of buffers for tile part
 * fetching and decompression workflows, reducing memory allocation overhead.
 */
class SlabPool
{
public:
  /**
   * @brief Constructs a slab pool with a specified number of slots and slot size.
   * @param numSlots Number of slots in the slab.
   * @param slotSize Size of each slot in bytes.
   * @throws std::invalid_argument if numSlots or slotSize is 0.
   * @throws std::bad_alloc if slab allocation fails.
   */
  SlabPool(size_t numSlots, size_t slotSize)
      : numSlots_(numSlots), slotSize_(slotSize), slabSize_(numSlots * slotSize)
  {
    if(numSlots == 0)
      throw std::invalid_argument("SlabPool: numSlots must be non-zero");
    if(slotSize == 0)
      throw std::invalid_argument("SlabPool: slotSize must be non-zero");

    slab_ = std::make_unique<uint8_t[]>(slabSize_);
    if(!slab_)
      throw std::bad_alloc();

    // Initialize free list with all slots
    for(size_t i = 0; i < numSlots_; ++i)
    {
      freeBuffers_.push(&slab_[i * slotSize_]);
    }
  }

  /**
   * @brief Deleted copy constructor to prevent copying.
   */
  SlabPool(const SlabPool&) = delete;

  /**
   * @brief Deleted assignment operator to prevent copying.
   */
  SlabPool& operator=(const SlabPool&) = delete;

  /**
   * @brief Move constructor for transferring ownership.
   */
  SlabPool(SlabPool&& other) noexcept
      : numSlots_(other.numSlots_), slotSize_(other.slotSize_), slabSize_(other.slabSize_),
        slab_(std::move(other.slab_))
  {
    std::lock_guard<std::mutex> lock(other.mutex_);
    freeBuffers_ = std::move(other.freeBuffers_);
    other.numSlots_ = 0;
    other.slotSize_ = 0;
    other.slabSize_ = 0;
  }

  /**
   * @brief Move assignment operator for transferring ownership.
   */
  SlabPool& operator=(SlabPool&& other) noexcept
  {
    if(this != &other)
    {
      std::lock_guard<std::mutex> lock(mutex_);
      std::lock_guard<std::mutex> otherLock(other.mutex_);
      numSlots_ = other.numSlots_;
      slotSize_ = other.slotSize_;
      slabSize_ = other.slabSize_;
      slab_ = std::move(other.slab_);
      freeBuffers_ = std::move(other.freeBuffers_);
      other.numSlots_ = 0;
      other.slotSize_ = 0;
      other.slabSize_ = 0;
    }
    return *this;
  }

  /**
   * @brief Destructor, frees the slab memory.
   */
  ~SlabPool() = default;

  /**
   * @brief Allocates a buffer from the pool.
   * @return Pointer to a buffer of slotSize_ bytes.
   * @throws std::runtime_error if no free buffers are available.
   */
  uint8_t* allocate()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if(freeBuffers_.empty())
      throw std::runtime_error("SlabPool: No free buffers available");
    uint8_t* buffer = freeBuffers_.front();
    freeBuffers_.pop();
    return buffer;
  }

  /**
   * @brief Returns a buffer to the pool for reuse.
   * @param buffer Pointer to the buffer to recycle (must be from this pool).
   */
  void recycle(uint8_t* buffer)
  {
    if(!buffer)
      return;

    std::lock_guard<std::mutex> lock(mutex_);
    // Use uintptr_t to avoid sign issues with ptrdiff_t
    auto slabBase = reinterpret_cast<std::uintptr_t>(slab_.get());
    auto bufferAddr = reinterpret_cast<std::uintptr_t>(buffer);
    std::uintptr_t offset = bufferAddr - slabBase;

    // Validate buffer is within slab and aligned to a slot
    if(offset < slabSize_ && (offset % slotSize_) == 0)
    {
      freeBuffers_.push(buffer);
    }
    // Silently ignore invalid buffers (or log/throw if desired)
  }

  /**
   * @brief Returns the number of free buffers currently available.
   * @return Number of free buffers.
   */
  size_t freeCount() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return freeBuffers_.size();
  }

  /**
   * @brief Returns the total number of slots in the pool.
   * @return Total number of slots.
   */
  size_t totalSlots() const
  {
    return numSlots_;
  }

  /**
   * @brief Returns the size of each slot in bytes.
   * @return Slot size in bytes.
   */
  size_t slotSize() const
  {
    return slotSize_;
  }

  /**
   * @brief Returns the total size of the slab in bytes.
   * @return Total slab size (numSlots_ * slotSize_).
   */
  size_t slabSize() const
  {
    return slabSize_;
  }

private:
  size_t numSlots_; // Total number of slots
  size_t slotSize_; // Size of each slot in bytes
  size_t slabSize_; // Total size of the slab in bytes
  std::unique_ptr<uint8_t[]> slab_; // Contiguous slab memory
  mutable std::mutex mutex_; // Mutex for thread-safe access
  std::queue<uint8_t*> freeBuffers_; // Free list of available buffers
};

} // namespace grk
