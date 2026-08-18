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
#include <memory>
#include <mutex>
#include <vector>
#include <cassert>
#include <atomic>

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
    // Swap rather than destroy: codecs pin the executor via acquire(), so a
    // resize can never free an executor that still has tasks in flight.
    instance_ = std::make_shared<tf::Executor>(numThreads_ == 1 ? 0 : numThreads_);
  }

  /**
   * @brief Pins the current global executor (creating it with full hardware
   * concurrency if null).
   *
   * Codec entry points hold the returned shared_ptr for the duration of a
   * compress/decompress, so a concurrent grk_initialize resize or
   * grk_deinitialize swaps the global instance out without destroying the
   * executor this codec is running on.
   */
  static std::shared_ptr<tf::Executor> acquire(void)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!instance_)
    {
      numThreads_ = std::thread::hardware_concurrency();
      instance_ = std::make_shared<tf::Executor>(numThreads_ == 1 ? 0 : numThreads_);
    }
    return instance_;
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
    {
      if(!tlsExec_ && tlsOwnerExec_)
        tlsExec_ = tlsOwnerExec_->load(std::memory_order_acquire);
      if(tlsExec_)
        return *tlsExec_;
    }
    return *acquire();
  }

  /**
   * @brief Creates an executor owned by a single codec, with @p numThreads workers.
   *
   * Its workers resolve get()/num_threads()/workerId() to this executor, so the
   * codec sizes and indexes its per-worker scratch by its own thread count rather
   * than the global pool's.  numThreads == 1 gives an inline (0-worker) executor
   * that runs everything on the calling thread.
   */
  static std::unique_ptr<tf::Executor> makeLocalExecutor(size_t numThreads)
  {
    if(numThreads <= 1)
      return std::make_unique<tf::Executor>(0);
    auto init = std::make_shared<LocalWorkerInit>(numThreads);
    auto exec = std::make_unique<tf::Executor>(numThreads, init);
    init->executor_.store(exec.get(), std::memory_order_release);
    return exec;
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
    if(tlsActive_)
      return tlsWorkerId_;
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
    /** A null exec leaves the global lookup in place, so callers with no per-codec
        executor can declare one unconditionally. */
    ScopedExecutor(tf::Executor* exec, size_t numThreads)
        : prevExec_(tlsExec_), prevOwnerExec_(tlsOwnerExec_), prevNumThreads_(tlsNumThreads_),
          prevWorkerId_(tlsWorkerId_), prevActive_(tlsActive_)
    {
      if(!exec)
        return;
      tlsExec_ = exec;
      tlsOwnerExec_ = nullptr;
      tlsNumThreads_ = numThreads;
      tlsWorkerId_ = 0;
      tlsActive_ = true;
    }
    ~ScopedExecutor()
    {
      tlsExec_ = prevExec_;
      tlsOwnerExec_ = prevOwnerExec_;
      tlsNumThreads_ = prevNumThreads_;
      tlsWorkerId_ = prevWorkerId_;
      tlsActive_ = prevActive_;
    }
    ScopedExecutor(const ScopedExecutor&) = delete;
    ScopedExecutor& operator=(const ScopedExecutor&) = delete;

  private:
    tf::Executor* prevExec_;
    std::atomic<tf::Executor*>* prevOwnerExec_;
    size_t prevNumThreads_;
    uint32_t prevWorkerId_;
    bool prevActive_;
  };

private:
  /**
   * @brief Installs the thread-local override on each worker of a caller-owned
   * executor, so work running there resolves to that executor and to the worker's
   * own id rather than falling through to the global pool.
   *
   * The executor pointer is only known after its constructor returns, which is
   * always before the first task runs, so get() resolves it on first use.
   */
  class LocalWorkerInit : public tf::WorkerInterface
  {
  public:
    explicit LocalWorkerInit(size_t numThreads) : numThreads_(numThreads) {}
    void scheduler_prologue(tf::Worker& worker) override
    {
      tlsExec_ = nullptr;
      tlsOwnerExec_ = &executor_;
      tlsNumThreads_ = numThreads_;
      tlsWorkerId_ = (uint32_t)worker.id();
      tlsActive_ = true;
    }
    void scheduler_epilogue(tf::Worker&, std::exception_ptr) override
    {
      tlsActive_ = false;
      tlsExec_ = nullptr;
      tlsOwnerExec_ = nullptr;
    }
    std::atomic<tf::Executor*> executor_{nullptr};

  private:
    size_t numThreads_;
  };

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
  static std::shared_ptr<tf::Executor> instance_;

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
   * @brief Set on a per-codec executor's worker threads, where the executor pointer
   * is not yet published when the override is installed.
   */
  static thread_local std::atomic<tf::Executor*>* tlsOwnerExec_;

  /**
   * @brief Thread count reported while a ScopedExecutor override is active.
   */
  static thread_local size_t tlsNumThreads_;

  /**
   * @brief Worker id reported while an override is active (0 on the driver thread).
   */
  static thread_local uint32_t tlsWorkerId_;

  /**
   * @brief True while a ScopedExecutor override is active on this thread.
   */
  static thread_local bool tlsActive_;
};
