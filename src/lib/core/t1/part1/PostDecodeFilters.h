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

// Narrowing dequantization filter for 16-bit fixed-point 9/7 DWT path.
// Converts Part 1 block coder int32 two's-complement output to int16 by
// applying the quantization step size (dequantization) and clamping.
//
// The block coder stores coefficients with an implicit T1_NMSEDEC_FRACBITS=1
// scaling, so the effective step is stepsize/2 — identical to ScaleFilter,
// but the result is rounded to int16 instead of converted to float.
//
// For images up to 12-bit precision with typical step sizes, the dequantized
// coefficient magnitudes are well within the int16 range (see BIBO analysis
// in doc/16BitDWT.md and wavelet/WaveletReverse97_16.cpp).
class NarrowScaleFilter16
{
public:
  // scale = stepsize/2, left-shifted by qShift to dequantize into the Q-format
  // int16 DWT representation (qShift==0 leaves the value at sample scale).
  NarrowScaleFilter16(DecompressBlockExec* block)
      : scale_(block->stepsize / 2 * (float)(1u << block->qShift))
  {}
  inline void copy(int16_t* dest, const int32_t* src, uint32_t len)
  {
    for(uint32_t i = 0; i < len; ++i)
    {
      float val = (float)src[i] * scale_;
      // Round to nearest and clamp to int16 range
      int32_t rounded = (int32_t)(val >= 0 ? val + 0.5f : val - 0.5f);
      if(rounded > 32767)
        rounded = 32767;
      else if(rounded < -32768)
        rounded = -32768;
      dest[i] = (int16_t)rounded;
    }
  }

private:
  float scale_;
};

// Same as NarrowScaleFilter16 but with ROI shift applied first.
class NarrowRoiScaleFilter16
{
public:
  NarrowRoiScaleFilter16(DecompressBlockExec* block)
      : roiShift_(block->roishift), scale_(block->stepsize / 2 * (float)(1u << block->qShift))
  {}
  inline void copy(int16_t* dest, const int32_t* src, uint32_t len)
  {
    int32_t thresh = 1 << roiShift_;
    for(uint32_t i = 0; i < len; ++i)
    {
      int32_t val = src[i];
      int32_t mag = abs(val);
      if(mag >= thresh)
      {
        mag >>= roiShift_;
        val = val < 0 ? -mag : mag;
      }
      float fval = (float)val * scale_;
      int32_t rounded = (int32_t)(fval >= 0 ? fval + 0.5f : fval - 0.5f);
      if(rounded > 32767)
        rounded = 32767;
      else if(rounded < -32768)
        rounded = -32768;
      dest[i] = (int16_t)rounded;
    }
  }

private:
  uint32_t roiShift_;
  float scale_;
};

} // namespace grk::t1
