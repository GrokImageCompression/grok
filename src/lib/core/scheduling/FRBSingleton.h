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
#include <memory>
#include <mutex>
#include <thread>
#include <cassert>

#include <freebyrd/freebyrd.h>

/**
 * @class FRBSingleton
 * @brief Manages a global freebyrd thread pool instance for strip-based decompression.
 *
 * Mirrors the TFSingleton API so callers can swap between Taskflow and freebyrd pools.
 * The pool is created lazily on first access or explicitly via create().
 */
class FRBSingleton
{
public:
  static void create(size_t numThreads)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    numThreads = numThreads ? numThreads : std::thread::hardware_concurrency();
    if(numThreads_ == numThreads && instance_)
      return;
    numThreads_ = numThreads;
    instance_ = std::make_unique<frb::thread_pool>(frb::pool_config{.num_threads = (uint32_t)numThreads_});
  }

  static frb::thread_pool& get()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!instance_)
    {
      numThreads_ = std::thread::hardware_concurrency();
      instance_ = std::make_unique<frb::thread_pool>(frb::pool_config{.num_threads = (uint32_t)numThreads_});
    }
    assert(instance_);
    return *instance_;
  }

  static size_t num_threads()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return numThreads_;
  }

  static void destroy()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    instance_.reset();
    numThreads_ = 0;
  }

private:
  FRBSingleton() = delete;

  static inline std::unique_ptr<frb::thread_pool> instance_ = nullptr;
  static inline std::mutex mutex_;
  static inline size_t numThreads_ = 0;
};
