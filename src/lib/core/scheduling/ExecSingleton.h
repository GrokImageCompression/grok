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

#include <stdexcept>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <cassert>
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

class TileFutureManager
{
public:
  TileFutureManager() = default;

  // Prevent copying or moving
  TileFutureManager(const TileFutureManager&) = delete;
  TileFutureManager& operator=(const TileFutureManager&) = delete;

  // Add a future to the map for a given tile ID
  void add(uint16_t tile_id, tf::Future<void> future)
  {
    std::unique_lock lock(mutex_);
    tileFutures_.emplace(tile_id, std::move(future));
  }

  // Wait for all futures to complete
  void wait()
  {
    std::vector<std::reference_wrapper<tf::Future<void>>> futures;
    {
      std::unique_lock lock(mutex_);
      futures.reserve(tileFutures_.size());
      for(auto& [id, future] : tileFutures_)
      {
        futures.emplace_back(future);
      }
      // Lock is released here as scope ends
    }

    for(auto& future_ref : futures)
    {
      future_ref.get().wait();
    }
  }

  // Wait for a specific future by tile ID, return true if found and waited
  bool wait(uint16_t tile_id)
  {
    std::unique_lock lock(mutex_);
    auto it = tileFutures_.find(tile_id);
    if(it == tileFutures_.end())
    {
      return false;
    }
    auto& future = it->second;
    lock.unlock();
    future.wait();
    return true;
  }

  // Clear the map immediately (no waiting)
  void clear()
  {
    std::unique_lock lock(mutex_);
    tileFutures_.clear();
  }

  // Wait for all futures to complete, then clear the map
  void waitAndClear()
  {
    wait();
    std::unique_lock lock(mutex_);
    tileFutures_.clear();
  }

  void waitAndClear(uint16_t tileIndex)
  {
    std::unique_lock lock(mutex_);
    assert(tileFutures_.empty() || tileFutures_.size() == 1);
    if(!tileFutures_.empty())
    {
      auto it = tileFutures_.find(tileIndex);
      assert(it != tileFutures_.end()); // Ensure tileIndex exists if map isn't empty
      auto& future = it->second;
      lock.unlock();
      future.wait();
      lock.lock();
      tileFutures_.clear();
    }
  }

  // Check if the map is empty
  bool empty() const
  {
    std::unique_lock lock(mutex_);
    return tileFutures_.empty();
  }

private:
  mutable std::mutex mutex_;
  std::unordered_map<uint16_t, tf::Future<void>> tileFutures_;
};

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
    int id = get().this_worker_id();
    return (numThreads_ > 1 && id >= 0) ? (uint32_t)id : 0;
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