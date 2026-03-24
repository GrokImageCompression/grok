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

#include <memory>
#include <cstdint>
#include <cstddef>
#include "MemManager.h"

namespace grk
{

uint32_t get_PLL_COLS_53();

/**
 * @class WaveletPoolData
 * @brief Per-image pool of per-thread scratch buffers for the 5/3 wavelet.
 *
 * Replaces the former static pool in WaveletReverse so that multiple
 * images can decompress concurrently without sharing scratch memory.
 */
class WaveletPoolData
{
public:
  WaveletPoolData() = default;
  ~WaveletPoolData() = default;

  WaveletPoolData(const WaveletPoolData&) = delete;
  WaveletPoolData& operator=(const WaveletPoolData&) = delete;

  /**
   * @brief Allocate per-thread horiz/vert scratch buffers.
   *
   * If already allocated with sufficient size, this is a no-op.
   *
   * @param maxDim  maximum image dimension (width or height)
   * @return true on success
   */
  bool alloc(size_t maxDim);

  uint8_t* getHoriz(size_t threadIndex) const
  {
    return horizData_[threadIndex].get();
  }

  uint8_t* getVert(size_t threadIndex) const
  {
    return vertData_[threadIndex].get();
  }

  bool isAllocated() const
  {
    return isAllocated_;
  }

private:
  struct AlignedDeleter
  {
    void operator()(uint8_t* ptr) const noexcept
    {
      grk_aligned_free(ptr);
    }
  };

  using BufferPtr = std::unique_ptr<uint8_t[], AlignedDeleter>;
  std::unique_ptr<BufferPtr[]> horizData_;
  std::unique_ptr<BufferPtr[]> vertData_;
  bool isAllocated_ = false;
  size_t allocatedMaxDim_ = 0;
};

} // namespace grk
