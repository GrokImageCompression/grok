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
    if(numThreads_ == numThreads && instance_)
      return;
    numThreads_ = numThreads;
    // numThreads == 1 => inline executor (0 workers): all work runs on the
    // calling thread, keeping the process truly single-threaded.
    instance_ = std::make_unique<tf::Executor>(numThreads_ == 1 ? 0 : numThreads_);
  }

  /**
   * @brief Gets current instance of the Singleton (creates with full hardware concurrency if null)
   * @return Taskflow Executor
   *
   * When a per-codec executor is active on this thread (see ScopedExecutor)
   * it is returned lock-free, so concurrent single-threaded decodes do not
   * contend on mutex_.  Otherwise the process-global executor is used.
   */
  static tf::Executor& get(void)
  {
    if(tlsActive_)
      return *tlsExec_;
    std::lock_guard<std::mutex> lock(mutex_);
    if(!instance_)
    {
      // Initialize with default thread count if instance is null
      numThreads_ = std::thread::hardware_concurrency();
      instance_ = std::make_unique<tf::Executor>(numThreads_ == 1 ? 0 : numThreads_);
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
    if(tlsActive_)
      return tlsNumThreads_;
    std::lock_guard<std::mutex> lock(mutex_);
    return numThreads_;
  }

  /**
   * @brief True when running single-threaded (inline executor, no workers).
   */
  static bool isSingleThreaded()
  {
    if(tlsActive_)
      return tlsNumThreads_ == 1;
    std::lock_guard<std::mutex> lock(mutex_);
    return numThreads_ == 1;
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

  /**
   * @brief Scoped override that makes get()/num_threads()/workerId() resolve to a
   * caller-owned executor on the current thread instead of the global singleton.
   *
   * Used in single-threaded mode so each codec runs on its own inline executor:
   * concurrent decodes become independent and lock-free.  Saves and restores the
   * previous thread-local values, so nesting (e.g. overviews) is safe.
   */
  class ScopedExecutor
  {
  public:
    ScopedExecutor(tf::Executor* exec, size_t numThreads)
        : prevExec_(tlsExec_), prevNumThreads_(tlsNumThreads_), prevActive_(tlsActive_)
    {
      tlsExec_ = exec;
      tlsNumThreads_ = numThreads;
      tlsActive_ = true;
    }
    ~ScopedExecutor()
    {
      tlsExec_ = prevExec_;
      tlsNumThreads_ = prevNumThreads_;
      tlsActive_ = prevActive_;
    }
    ScopedExecutor(const ScopedExecutor&) = delete;
    ScopedExecutor& operator=(const ScopedExecutor&) = delete;

  private:
    tf::Executor* prevExec_;
    size_t prevNumThreads_;
    bool prevActive_;
  };

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

  /**
   * @brief Per-thread override executor (set by ScopedExecutor), or nullptr.
   */
  static thread_local tf::Executor* tlsExec_;

  /**
   * @brief Thread count reported while a ScopedExecutor override is active.
   */
  static thread_local size_t tlsNumThreads_;

  /**
   * @brief True while a ScopedExecutor override is active on this thread.
   */
  static thread_local bool tlsActive_;
};
