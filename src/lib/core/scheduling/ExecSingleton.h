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
 * @class ExecSingleton
 * @brief Manages Taskflow Executor singleton instance
 */
class ExecSingleton
{
public:
  /**
   * @brief Creates singleton instance.
   * @param numThreads total number of threads including main thread
   * i.e. number of taskflow worker threads + 1
   */
  static void create(uint32_t numThreads)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    numThreads = numThreads ? numThreads : std::thread::hardware_concurrency() + 1;
    if(numThreads_ == numThreads)
      return;
    numThreads_ = numThreads;
    if(numThreads_ == 1)
    {
      instance_.reset();
      return;
    }
    instance_ = std::make_unique<tf::Executor>(numThreads_ - 1);
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
      uint32_t numThreads = std::thread::hardware_concurrency() + 1;
      numThreads_ = numThreads;
      if(numThreads > 1)
      {
        instance_ = std::make_unique<tf::Executor>(numThreads - 1);
      }
    }
    if(!instance_)
    {
      throw std::runtime_error("Executor not initialized (single-threaded mode)");
    }
    return *instance_;
  }

  /**
   * @brief Gets total number of threads
   *
   * @return size_t number of threads
   */
  static size_t num_threads()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return numThreads_;
  }

  /**
   * @brief Destroys ExecSingleton
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
   * the method is called from inside a TaskFlow task. Otherwise returns zero
   */
  static uint32_t workerId(void)
  {
    if(numThreads_ == 1)
      return 0;
    auto id = get().this_worker_id();
    return (id >= 0) ? (uint32_t)id : 0;
  }

private:
  // Deleted copy constructor and assignment operator
  ExecSingleton(const ExecSingleton&) = delete;
  ExecSingleton& operator=(const ExecSingleton&) = delete;
  /**
   * @brief Constructs an ExecSingleton
   */
  ExecSingleton() = default;

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
   *
   */
  static size_t numThreads_;
};