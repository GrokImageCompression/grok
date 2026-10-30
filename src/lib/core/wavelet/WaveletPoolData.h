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
#include <mutex>
#include <vector>
#include <cstdint>
#include <cstddef>
#include "MemManager.h"

namespace grk
{

uint32_t get_PLL_COLS_53();
uint32_t get_PLL_COLS_16_53();
uint32_t get_PLL_COLS_16_97();

struct WaveletScratch
{
  struct AlignedDeleter
  {
    void operator()(uint8_t* ptr) const noexcept
    {
      grk_aligned_free(ptr);
    }
  };
  using BufferPtr = std::unique_ptr<uint8_t[], AlignedDeleter>;

  size_t maxDim = 0;
  std::unique_ptr<BufferPtr[]> horiz;
  std::unique_ptr<BufferPtr[]> vert;

  uint8_t* getHoriz(size_t threadIndex) const
  {
    return horiz[threadIndex].get();
  }
  uint8_t* getVert(size_t threadIndex) const
  {
    return vert[threadIndex].get();
  }
};

/**
 * @class WaveletPoolData
 * @brief Per-image pool of per-thread scratch buffers for the 5/3 wavelet.
 */
class WaveletPoolData
{
public:
  WaveletPoolData() = default;
  ~WaveletPoolData() = default;

  WaveletPoolData(const WaveletPoolData&) = delete;
  WaveletPoolData& operator=(const WaveletPoolData&) = delete;

  const WaveletScratch* alloc(size_t maxDim);

private:
  std::mutex mutex_;
  // smaller scratch stays alive for tiles still decoding with it
  std::vector<std::unique_ptr<WaveletScratch>> scratch_;
};

} // namespace grk
