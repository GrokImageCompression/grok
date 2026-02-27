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

#include <stdexcept>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <cassert>

#include "grk_taskflow.h"

/**
 * @class TFSingleton
 * @brief Manages TFSingleton instance
 */
class TFSingleton
{
public:
  /**
   * @brief Creates singleton instance.
   * @param numThreads total number of threads including main thread
   * i.e. number of taskflow worker threads
   */
  static void create(size_t numThreads)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    numThreads = numThreads ? numThreads : std::thread::hardware_concurrency();
    if(numThreads_ == numThreads)
      return;
    numThreads_ = numThreads;
    instance_ = std::make_unique<tf::Executor>(numThreads_);
  }

  /**
   * @brief Gets current instance of the Singleton (creates with full hardware concurrency if null)
   * @return Taskflow Executor
   */
  static tf::Executor& get(void)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!instance_)
    {
      // Initialize with default thread count if instance is null
      numThreads_ = std::thread::hardware_concurrency();
      ;
      instance_ = std::make_unique<tf::Executor>(numThreads_);
    }
    assert(instance_); // Should always be valid now
    return *instance_;
  }

  /**
   * @brief Gets total number of threads (including driver thread)
   *
   * @return size_t number of threads
   */
  static size_t num_threads()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return numThreads_;
  }

  /**
   * @brief Destroys TFSingleton
   */
  static void destroy()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    instance_.reset();
  }

  /**
   * @brief Gets worker id for current worker
   *
   * @return TaskFlow thread id if more than one thread is configured AND
   * the method is called from inside a TaskFlow task. Otherwise returns zero.
   */
  static uint32_t workerId(void)
  {
    auto id = get().this_worker_id();
    return (id >= 0) ? (uint32_t)id : 0u;
  }

private:
  // Deleted copy constructor and assignment operator
  TFSingleton(const TFSingleton&) = delete;
  TFSingleton& operator=(const TFSingleton&) = delete;
  /**
   * @brief Constructs an TFSingleton
   */
  TFSingleton() = default;

  /**
   * @brief Taskflow Executor instance
   */
  static std::unique_ptr<tf::Executor> instance_;

  /**
   * @brief std::mutex to control access to instance_
   */
  static std::mutex mutex_;

  /**
   * @brief total number of threads
   */
  static size_t numThreads_;
};
