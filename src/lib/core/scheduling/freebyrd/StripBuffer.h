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

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <memory>
#include <thread>
#include "buffer.h"

namespace grk
{

/**
 * @class StripBuffer
 * @brief Circular stripe buffer for cache-friendly strip-based DWT.
 *
 * Instead of allocating full-resolution intermediate buffers (SPLIT_L, SPLIT_H),
 * allocates only a small circular buffer of N stripes × stripeHeight rows.
 * Each stripe provides a Buffer2dSimple view for H-DWT output / V-DWT input.
 *
 * Memory savings: for a 4K image with 32-row stripes and 4 buffers:
 *   4 × 32 × 4096 × 4 = 2 MB vs. full-res 4096 × 4096 × 4 = 64 MB
 *
 * @tparam T Element type (int32_t for 5/3, float for 9/7)
 */
template<typename T>
class StripBuffer
{
public:
  StripBuffer() = default;

  /**
   * @brief Allocate the strip buffer.
   * @param width Row width in elements
   * @param stripeHeight Rows per stripe (including any halo)
   * @param numStripes Number of stripe slots in the circular buffer
   */
  void alloc(uint32_t width, uint32_t stripeHeight, uint32_t numStripes)
  {
    width_ = width;
    // Align stride to 16 elements for SIMD
    stride_ = (width + 15U) & ~15U;
    stripeHeight_ = stripeHeight;
    numStripes_ = numStripes;
    totalRows_ = stripeHeight * numStripes;
    size_t totalElements = (size_t)stride_ * totalRows_;
    data_ = std::make_unique<T[]>(totalElements);
    std::memset(data_.get(), 0, totalElements * sizeof(T));
  }

  /**
   * @brief Get a Buffer2dSimple view of stripe at the given index.
   * @param stripeIdx Stripe index (0-based, will be wrapped modulo numStripes)
   * @param height Number of rows to expose (may be less than stripeHeight for last strip)
   */
  Buffer2dSimple<T> getStripe(uint32_t stripeIdx, uint32_t height) const
  {
    assert(data_);
    uint32_t wrapped = stripeIdx % numStripes_;
    T* ptr = data_.get() + (size_t)wrapped * stripeHeight_ * stride_;
    return Buffer2dSimple<T>(ptr, stride_, height);
  }

  /**
   * @brief Get a Buffer2dSimple view of stripe at the given index (full height).
   */
  Buffer2dSimple<T> getStripe(uint32_t stripeIdx) const
  {
    return getStripe(stripeIdx, stripeHeight_);
  }

  uint32_t width() const
  {
    return width_;
  }
  uint32_t stride() const
  {
    return stride_;
  }
  uint32_t stripeHeight() const
  {
    return stripeHeight_;
  }
  uint32_t numStripes() const
  {
    return numStripes_;
  }

  bool allocated() const
  {
    return data_ != nullptr;
  }

private:
  std::unique_ptr<T[]> data_;
  uint32_t width_ = 0;
  uint32_t stride_ = 0;
  uint32_t stripeHeight_ = 0;
  uint32_t numStripes_ = 0;
  uint32_t totalRows_ = 0;
};

/**
 * @brief Compute ideal stripe height following sicorax's cache-tuning formula.
 *
 * The working set per strip includes:
 * - 5/3: 2 intermediate int32 buffers (L rows + H rows)
 * - 9/7: 2 intermediate float buffers (L rows + H rows) allocated by cascade_strip_97
 *
 * We target the total working set fitting in L2 cache.
 * For 9/7, the halo (4 rows per side) increases the extended strip height,
 * so we account for it when sizing.
 *
 * Additionally, we ensure enough strips per resolution for good parallelism
 * (at minimum 2 × hardware threads).
 *
 * @param compWidth Component width in pixels
 * @param halo Sub-band halo rows (1 for 5/3, 4 for 9/7)
 * @param resHeight Resolution height (for parallelism balancing, 0 = skip)
 * @param elementSize Size of each element in bytes (4 for int32/float)
 * @return Stripe height in interleaved rows (min 6, max 64, even)
 */
inline uint32_t computeIdealStripeHeight(uint32_t compWidth, uint32_t halo = 1,
                                         uint32_t resHeight = 0, uint32_t elementSize = 4)
{
  // Target: both intermediate buffers fit in ~384KB (typical L2 per-core)
  // Working set = 2 * (stripeHeight/2 + halo) * width * elementSize
  // => stripeHeight ≈ (targetBytes / (width * elementSize)) - 2*halo
  constexpr uint32_t targetCacheBytes = 384 * 1024;
  uint32_t bytesPerRow = compWidth * elementSize;
  if(bytesPerRow == 0)
    return 32;

  uint32_t totalRows = targetCacheBytes / bytesPerRow;
  // totalRows covers both L and H intermediates (each ~half the extended height)
  // Extended height = stripeHeight + 2*halo (in interleaved rows)
  // Two buffers of (stripeHeight/2 + halo) rows each ≈ stripeHeight + 2*halo rows total
  uint32_t ideal = (totalRows > 2 * halo) ? totalRows - 2 * halo : 6;

  // Clamp to reasonable range
  ideal = std::max(ideal, 6U);
  ideal = std::min(ideal, 64U);

  // Ensure enough parallelism: at least 2× nproc strips per resolution
  if(resHeight > 0)
  {
    uint32_t nproc = std::max(1U, (uint32_t)std::thread::hardware_concurrency());
    uint32_t minStrips = 2 * nproc;
    uint32_t maxHeight = resHeight / minStrips;
    if(maxHeight > 0 && maxHeight < ideal)
      ideal = std::max(maxHeight, 6U);
  }

  // Round to even for parity alignment
  ideal = (ideal + 1U) & ~1U;

  return ideal;
}

} // namespace grk
