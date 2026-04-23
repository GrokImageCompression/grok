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
#include "BlockExec.h"

namespace grk::t1
{
template<typename T>
class RoiShiftFilter
{
public:
  RoiShiftFilter(DecompressBlockExec* block) : roiShift(block->roishift) {}
  inline void copy(T* dest, const T* src, uint32_t len)
  {
    T thresh = 1 << roiShift;
    for(uint32_t i = 0; i < len; ++i)
    {
      T val = src[i];
      T mag = abs(val);
      if(mag >= thresh)
      {
        mag >>= roiShift;
        val = val < 0 ? -mag : mag;
      }
      dest[i] = val / 2;
    }
  }

private:
  uint32_t roiShift;
};
template<typename T>
class ShiftFilter
{
public:
  ShiftFilter([[maybe_unused]] DecompressBlockExec* block) {}
  inline void copy(T* dest, const T* src, uint32_t len)
  {
    for(uint32_t i = 0; i < len; ++i)
      dest[i] = src[i] / 2;
  }
};

template<typename T>
class RoiScaleFilter
{
public:
  RoiScaleFilter(DecompressBlockExec* block) : roiShift(block->roishift), scale(block->stepsize / 2)
  {}
  inline void copy(T* dest, const T* src, uint32_t len)
  {
    T thresh = 1 << roiShift;
    for(uint32_t i = 0; i < len; ++i)
    {
      T val = src[i];
      T mag = abs(val);
      if(mag >= thresh)
      {
        mag >>= roiShift;
        val = val < 0 ? -mag : mag;
      }
      ((float*)dest)[i] = (float)val * scale;
    }
  }

private:
  uint32_t roiShift;
  float scale;
};

template<typename T>
class ScaleFilter
{
public:
  ScaleFilter(DecompressBlockExec* block) : scale(block->stepsize / 2) {}
  inline void copy(T* dest, const T* src, uint32_t len)
  {
    for(uint32_t i = 0; i < len; ++i)
      ((float*)dest)[i] = (float)src[i] * scale;
  }

private:
  float scale;
};

// Narrowing filters for 16-bit DWT path: int32_t T1 output → int16_t band buffers
class NarrowShiftFilter
{
public:
  NarrowShiftFilter([[maybe_unused]] DecompressBlockExec* block) {}
  inline void copy(int16_t* dest, const int32_t* src, uint32_t len)
  {
    for(uint32_t i = 0; i < len; ++i)
      dest[i] = (int16_t)(src[i] / 2);
  }
};

class NarrowRoiShiftFilter
{
public:
  NarrowRoiShiftFilter(DecompressBlockExec* block) : roiShift(block->roishift) {}
  inline void copy(int16_t* dest, const int32_t* src, uint32_t len)
  {
    int32_t thresh = 1 << roiShift;
    for(uint32_t i = 0; i < len; ++i)
    {
      int32_t val = src[i];
      int32_t mag = abs(val);
      if(mag >= thresh)
      {
        mag >>= roiShift;
        val = val < 0 ? -mag : mag;
      }
      dest[i] = (int16_t)(val / 2);
    }
  }

private:
  uint32_t roiShift;
};

} // namespace grk::t1
