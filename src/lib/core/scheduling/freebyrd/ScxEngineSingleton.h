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

#include "scx_scheduling_ffi.h"
#include <mutex>
#include <cstdint>

/**
 * @class ScxEngineSingleton
 * @brief Manages a global ScxEngine instance (persistent thread pool).
 *
 * Like TFSingleton, but wraps the Rust ScxEngine for domain-based scheduling.
 * The engine is created once and reused across tiles to avoid thread spawn overhead.
 */
class ScxEngineSingleton
{
public:
  /**
   * @brief Creates singleton instance with specified thread count.
   * @param numThreads Number of worker threads (0 = use all cores)
   */
  static void create(int32_t numThreads = 0)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if(instance_)
      return;
    instance_ = scx_engine_create(numThreads);
  }

  /**
   * @brief Gets the engine instance (creates with default thread count if null).
   * @return Raw pointer to ScxEngine
   */
  static ScxEngine* get()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!instance_)
      instance_ = scx_engine_create(0);
    return instance_;
  }

  /**
   * @brief Destroys the engine singleton.
   */
  static void destroy()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if(instance_)
    {
      scx_engine_destroy(instance_);
      instance_ = nullptr;
    }
  }

private:
  ScxEngineSingleton() = default;
  ScxEngineSingleton(const ScxEngineSingleton&) = delete;
  ScxEngineSingleton& operator=(const ScxEngineSingleton&) = delete;

  static ScxEngine* instance_;
  static std::mutex mutex_;
};
