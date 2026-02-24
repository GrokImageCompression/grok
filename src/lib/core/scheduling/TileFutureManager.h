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
