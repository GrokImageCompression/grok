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
#include <queue>

namespace grk
{

/**
 * @class ConcurrentQueue
 * @brief Thread-safe unbounded queue with blocking pop and close signal.
 *
 * Producers push items without blocking (except briefly for the lock).
 * Consumers block on pop() when the queue is empty.
 * close() signals that no more items will be pushed; pop() returns false
 * once the queue is both closed and empty.
 */
template<typename T>
class ConcurrentQueue
{
public:
  ConcurrentQueue() = default;

  void push(T item)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push(std::move(item));
    }
    cv_.notify_one();
  }

  bool pop(T& item)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || closed_; });
    if(queue_.empty())
      return false;
    item = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  void close()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
  }

  size_t size() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

private:
  std::queue<T> queue_;
  bool closed_ = false;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
};

} // namespace grk
