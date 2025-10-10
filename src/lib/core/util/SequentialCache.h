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

#include <vector>

namespace grk
{

/**
 * @class SequentialCache
 * @brief Dynamic array of pointers of type T
 *
 * This class uses a hybrid design : combination of std::vector and simple array
 *
 * @tparam T pointer type
 */
template<typename T>
class SequentialCache
{
public:
  /**
   * @brief Constructs a SequentialCache
   *
   */
  SequentialCache(void) : SequentialCache(kSequentialChunkSize) {}

  /**
   * @brief Constructs a SequentialCache
   * @param maxChunkSize maximum chunk size
   */
  SequentialCache(uint64_t maxChunkSize)
      : currChunk_(nullptr), chunkSize_(std::min<uint64_t>(maxChunkSize, kSequentialChunkSize)),
        index_(0), firstElement_(true)
  {}

  /**
   * @brief Destroys a SequentialCache
   */
  virtual ~SequentialCache(void)
  {
    for(auto& ch : chunks)
    {
      for(size_t i = 0; i < chunkSize_; ++i)
        delete ch[i];
      delete[] ch;
    }
  }

  /**
   * @brief Rewinds state of cache, in order to read from beginning
   */
  void rewind(void)
  {
    if(chunks.empty())
      return;
    index_ = 0;
    currChunk_ = chunks[0];
    firstElement_ = true;
  }

  /**
   * @brief Gets next pointer in cache
   */
  T* next()
  {
    uint64_t itemIndex = index_ % chunkSize_;
    uint64_t chunkIndex = index_ / chunkSize_;
    bool isInitialized = (currChunk_ != nullptr);
    bool isLastChunk = (chunkIndex == chunks.size() - 1);
    bool isEndOfChunk = (itemIndex == chunkSize_ - 1);
    bool createNewChunk = !isInitialized || (isLastChunk && isEndOfChunk);
    if(!firstElement_)
      itemIndex++;
    if(createNewChunk || isEndOfChunk)
    {
      itemIndex = 0;
      if(!firstElement_)
        chunkIndex++;
      if(createNewChunk)
      {
        currChunk_ = new T*[chunkSize_];
        memset(currChunk_, 0, chunkSize_ * sizeof(T*));
        chunks.push_back(currChunk_);
      }
      else
      {
        currChunk_ = chunks[chunkIndex];
      }
    }
    auto item = currChunk_[itemIndex];
    // create new item if null
    if(!item)
    {
      item = create();
      currChunk_[itemIndex] = item;
    }
    if(!firstElement_)
      index_++;
    firstElement_ = false;
    return item;
  }

protected:
  virtual T* create(void)
  {
    return new T();
  }

private:
  std::vector<T**> chunks;
  T** currChunk_;
  uint64_t chunkSize_;
  uint64_t index_;
  bool firstElement_;
  static constexpr uint64_t kSequentialChunkSize = 1024;
};

} // namespace grk
