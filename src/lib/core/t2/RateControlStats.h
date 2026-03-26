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

#include <atomic>
#include <cfloat>
#include <cstdint>
#include <climits>
#include <memory>

namespace grk
{

/**
 * @struct RateControlStats
 * @brief Per-tile rate control statistics collected during T1 encoding.
 *
 * These stats are gathered in parallel during block encoding so that the
 * rate control bisection loop can skip the initial serial codeblock traversal.
 */
struct RateControlStats
{
  RateControlStats() : numcomps_(0), numCodeBlocks_(0) {}

  void init(uint16_t numcomps)
  {
    numcomps_ = numcomps;
    numpixByComponent_ = std::make_unique<std::atomic<uint64_t>[]>(numcomps);
    for(uint16_t i = 0; i < numcomps; ++i)
      numpixByComponent_[i].store(0);
    numCodeBlocks_.store(0);
    // feasible algorithm stats
    minimumSlope_.store(USHRT_MAX);
    maximumSlope_.store(0);
    // simple algorithm stats
    minRDSlope_.store(DBL_MAX);
    maxRDSlope_.store(-1.0);
  }

  uint16_t numcomps_;

  // Per-component pixel counts (accumulated atomically during T1)
  std::unique_ptr<std::atomic<uint64_t>[]> numpixByComponent_;

  // Total codeblock count
  std::atomic<uint64_t> numCodeBlocks_{0};

  // Feasible bisect: min/max slopes from convex hull (uint16_t log-domain)
  std::atomic<uint16_t> minimumSlope_{USHRT_MAX};
  std::atomic<uint16_t> maximumSlope_{0};

  // Simple bisect: min/max raw RD slopes (double-domain)
  std::atomic<double> minRDSlope_{DBL_MAX};
  std::atomic<double> maxRDSlope_{-1.0};

  void updateMinSlope(uint16_t val)
  {
    auto cur = minimumSlope_.load(std::memory_order_relaxed);
    while(val < cur && !minimumSlope_.compare_exchange_weak(cur, val, std::memory_order_relaxed))
    {
    }
  }

  void updateMaxSlope(uint16_t val)
  {
    auto cur = maximumSlope_.load(std::memory_order_relaxed);
    while(val > cur && !maximumSlope_.compare_exchange_weak(cur, val, std::memory_order_relaxed))
    {
    }
  }

  void updateMinRDSlope(double val)
  {
    auto cur = minRDSlope_.load(std::memory_order_relaxed);
    while(val < cur && !minRDSlope_.compare_exchange_weak(cur, val, std::memory_order_relaxed))
    {
    }
  }

  void updateMaxRDSlope(double val)
  {
    auto cur = maxRDSlope_.load(std::memory_order_relaxed);
    while(val > cur && !maxRDSlope_.compare_exchange_weak(cur, val, std::memory_order_relaxed))
    {
    }
  }
};

} // namespace grk
