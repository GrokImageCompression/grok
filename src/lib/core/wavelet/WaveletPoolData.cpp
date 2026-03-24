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

namespace grk
{

bool WaveletPoolData::alloc(size_t maxDim)
{
  if(maxDim == 0)
    return false;
  if(isAllocated_ && maxDim <= allocatedMaxDim_)
    return true;

  size_t num_threads = TFSingleton::num_threads();
  try
  {
    auto newHoriz = std::make_unique<BufferPtr[]>(num_threads);
    auto newVert = std::make_unique<BufferPtr[]>(num_threads);

    size_t buffer_size = maxDim;
    auto multiplier = std::max(sizeof(int32_t) * get_PLL_COLS_53(), sizeof(vec4f));
    buffer_size *= multiplier;
    for(size_t i = 0; i < num_threads; ++i)
    {
      void* horiz_ptr = grk_aligned_malloc(buffer_size);
      void* vert_ptr = grk_aligned_malloc(buffer_size);
      if(!horiz_ptr || !vert_ptr)
      {
        if(horiz_ptr)
          grk_aligned_free(horiz_ptr);
        if(vert_ptr)
          grk_aligned_free(vert_ptr);
        throw std::bad_alloc();
      }
      newHoriz[i] = BufferPtr(static_cast<uint8_t*>(horiz_ptr));
      newVert[i] = BufferPtr(static_cast<uint8_t*>(vert_ptr));
    }

    horizData_ = std::move(newHoriz);
    vertData_ = std::move(newVert);
    allocatedMaxDim_ = maxDim;
    isAllocated_ = true;
  }
  catch(const std::bad_alloc&)
  {
    horizData_.reset();
    vertData_.reset();
    isAllocated_ = false;
  }

  return isAllocated_;
}

} // namespace grk
