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

#include <list>
#include <unordered_map>
#include <mutex>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>

namespace grk
{

/**
 * @class LRUCache
 * @brief Thread-safe LRU cache with configurable capacity and eviction callback.
 *
 * Key must be hashable. Value must be moveable.
 * The sizeFunc callback returns the cost (in bytes) of a value, enabling
 * memory-based eviction rather than count-based.
 *
 * @tparam Key   key type (must be hashable)
 * @tparam Value value type (must be moveable)
 */
template<typename Key, typename Value>
class LRUCache
{
public:
  using SizeFunc = std::function<size_t(const Value&)>;
  using EvictFunc = std::function<void(const Key&, Value&&)>;

  /**
   * @brief Construct an LRU cache
   * @param maxBytes   high water mark in bytes (0 = unlimited)
   * @param sizeFunc   returns size in bytes for a given value
   * @param evictFunc  called when an entry is evicted (optional)
   */
  LRUCache(size_t maxBytes, SizeFunc sizeFunc, EvictFunc evictFunc = nullptr)
      : maxBytes_(maxBytes), currentBytes_(0), sizeFunc_(std::move(sizeFunc)),
        evictFunc_(std::move(evictFunc))
  {}

  ~LRUCache() = default;

  // non-copyable, moveable
  LRUCache(const LRUCache&) = delete;
  LRUCache& operator=(const LRUCache&) = delete;
  LRUCache(LRUCache&&) = default;
  LRUCache& operator=(LRUCache&&) = default;

  /**
   * @brief Insert or update an entry. Evicts LRU entries if over capacity.
   * @param key   cache key
   * @param value cache value (moved in)
   */
  void put(const Key& key, Value value)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if(it != map_.end())
    {
      // Update existing entry: remove old size, move to front
      currentBytes_ -= sizeFunc_(it->second->second);
      list_.erase(it->second);
      map_.erase(it);
    }

    size_t entrySize = sizeFunc_(value);
    list_.emplace_front(key, std::move(value));
    map_[key] = list_.begin();
    currentBytes_ += entrySize;

    evictWhileOverCapacity();
  }

  /**
   * @brief Look up an entry. Moves it to the front (most recently used).
   * @param key  cache key
   * @return pointer to the value, or nullptr if not found.
   *         Pointer is valid until next put/erase/clear.
   */
  Value* get(const Key& key)
  {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if(it == map_.end())
      return nullptr;

    // Move to front (most recently used)
    list_.splice(list_.begin(), list_, it->second);
    return &(it->second->second);
  }

  /**
   * @brief Check if a key exists without promoting it in the LRU order.
   */
  bool contains(const Key& key) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_.find(key) != map_.end();
  }

  /**
   * @brief Remove a specific entry.
   */
  bool erase(const Key& key)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = map_.find(key);
    if(it == map_.end())
      return false;

    currentBytes_ -= sizeFunc_(it->second->second);
    list_.erase(it->second);
    map_.erase(it);
    return true;
  }

  /**
   * @brief Clear all entries.
   */
  void clear()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    list_.clear();
    map_.clear();
    currentBytes_ = 0;
  }

  size_t size() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_.size();
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

  void setMaxBytes(size_t maxBytes)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    maxBytes_ = maxBytes;
    evictWhileOverCapacity();
  }

private:
  void evictWhileOverCapacity()
  {
    while(maxBytes_ > 0 && currentBytes_ > maxBytes_ && !list_.empty())
    {
      auto& back = list_.back();
      size_t entrySize = sizeFunc_(back.second);
      if(evictFunc_)
        evictFunc_(back.first, std::move(back.second));
      map_.erase(back.first);
      list_.pop_back();
      currentBytes_ -= entrySize;
    }
  }

  using ListType = std::list<std::pair<Key, Value>>;

  size_t maxBytes_;
  size_t currentBytes_;
  SizeFunc sizeFunc_;
  EvictFunc evictFunc_;
  ListType list_; // front = MRU, back = LRU
  std::unordered_map<Key, typename ListType::iterator> map_; // key → list iterator
  mutable std::mutex mutex_;
};

} // namespace grk
