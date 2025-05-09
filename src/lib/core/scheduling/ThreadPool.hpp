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

#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wimplicit-int-float-conversion"
#endif
#endif
#include <taskflow/taskflow.hpp>
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif

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
      create(0);
    return *instance_;
  }

  /**
   * @brief Gets total number of threads
   *
   * @return size_t number of threads
   */
  static size_t num_threads()
  {
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
   * @return worker id
   */
  static uint32_t workerId(void)
  {
    return numThreads_ > 1 ? (uint32_t)get().this_worker_id() : 0;
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
