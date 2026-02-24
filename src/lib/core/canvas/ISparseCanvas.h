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
#include <algorithm>

// SparseCanvas stores blocks in the canvas coordinate system. It covers the active sub-bands for
// all (reduced) resolutions

/***
 *
 * SparseCanvas stores blocks of size LBW x LBH in canvas coordinate system (with offset)
 * Blocks are only allocated for active sub-bands for reduced resolutions
 *
 * Data is passed in and out in a linear array, chunked either along the y axis
 * or along the x axis, depending on whether we are working with a horizontal strip
 * or a vertical strip of data.
 *
 *
 */

namespace grk
{
template<typename T>
class ISparseCanvas
{
public:
  virtual ~ISparseCanvas() = default;
  /**
   * Read window of data into dest buffer.
   */
  virtual bool read(uint8_t resno, Rect32 window, T* dest, const uint32_t destChunkY,
                    const uint32_t destChunkX) = 0;
  /**
   * Write window of data from src buffer
   */
  virtual bool write(uint8_t resno, Rect32 window, const T* src, const uint32_t srcChunkY,
                     const uint32_t srcChunkX) = 0;

  virtual bool alloc(Rect32 window, bool zeroOutBuffer) = 0;
};
template<typename T>
struct SparseBlock
{
  SparseBlock(void) : data(nullptr) {}
  ~SparseBlock()
  {
    delete[] data;
  }
  void alloc(uint32_t block_area, bool zeroOutBuffer)
  {
    data = new T[block_area];
    if(zeroOutBuffer)
      memset(data, 0, (size_t)block_area * sizeof(T));
  }
  T* data;
};

} // namespace grk