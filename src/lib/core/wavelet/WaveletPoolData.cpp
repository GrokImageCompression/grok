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

#include "WaveletPoolData.h"
#include "TFSingleton.h"
#include "simd.h"
#include "WaveletCommon.h"
#include "Logger.h"

namespace grk
{

const WaveletScratch* WaveletPoolData::alloc(size_t maxDim)
{
  if(maxDim == 0)
    return nullptr;
  std::lock_guard<std::mutex> lock(mutex_);
  if(!scratch_.empty() && maxDim <= scratch_.back()->maxDim)
    return scratch_.back().get();

  size_t num_threads = TFSingleton::num_threads();
  try
  {
    auto scratch = std::make_unique<WaveletScratch>();
    scratch->horiz = std::make_unique<WaveletScratch::BufferPtr[]>(num_threads);
    scratch->vert = std::make_unique<WaveletScratch::BufferPtr[]>(num_threads);

    size_t buffer_size = maxDim;
    auto multiplier = std::max(sizeof(int32_t) * get_PLL_COLS_53(), sizeof(vec4f));
    // overflow guard (the sibling dwt_scratch::alloc already has one)
    if(multiplier && maxDim > SIZE_MAX / multiplier)
    {
      grklog.error("WaveletPoolData: scratch size overflow for dimension %zu", maxDim);
      return nullptr;
    }
    buffer_size *= multiplier;
    for(size_t i = 0; i < num_threads; ++i)
    {
      scratch->horiz[i].reset(static_cast<uint8_t*>(grk_aligned_malloc(buffer_size)));
      scratch->vert[i].reset(static_cast<uint8_t*>(grk_aligned_malloc(buffer_size)));
      if(!scratch->horiz[i] || !scratch->vert[i])
        return nullptr;
    }
    scratch->maxDim = maxDim;
    scratch_.push_back(std::move(scratch));
  }
  catch(const std::bad_alloc&)
  {
    return nullptr;
  }

  return scratch_.back().get();
}

} // namespace grk
