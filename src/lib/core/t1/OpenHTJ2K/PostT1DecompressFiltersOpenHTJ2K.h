#pragma once

#include "grk_includes.h"

namespace openhtj2k
{
template<typename T>
class RoiShiftOpenHTJ2KFilter
{
  public:
	RoiShiftOpenHTJ2KFilter(grk::DecompressBlockExec* block)
		: roiShift(block->roishift), shift(31U - (block->k_msbs + 1U))
	{}
	inline void copy(T* dest, T* src, uint32_t len)
	{
		T thresh = 1 << roiShift;
		for(uint32_t i = 0; i < len; ++i)
		{
			T val = src[i];
			T mag = (val & 0x7FFFFFFF);
			if(mag >= thresh)
				val = (T)(((uint32_t)mag >> roiShift) & ((uint32_t)val & 0x80000000));
			int32_t val_shifted = (val & 0x7FFFFFFF) >> shift;
			dest[i] = (int32_t)(((uint32_t)val & 0x80000000) ? -val_shifted : val_shifted);
		}
	}

  private:
	uint32_t roiShift;
	uint32_t shift;
};
template<typename T>
class ShiftOpenHTJ2KFilter
{
  public:
	ShiftOpenHTJ2KFilter([[maybe_unused]] grk::DecompressBlockExec* block) {}
	inline void copy(T* dest, T* src, uint32_t len)
	{
		for(uint32_t i = 0; i < len; ++i)
		{
			dest[i] = src[i];
		}
	}
};

template<typename T>
class RoiScaleOpenHTJ2KFilter
{
  public:
	RoiScaleOpenHTJ2KFilter([[maybe_unused]] grk::DecompressBlockExec* block) {}
	inline void copy(T* dest, T* src, uint32_t len)
	{
		for(uint32_t i = 0; i < len; ++i)
		{
			((float*)dest)[i] = src[i];
		}
	}
};

template<typename T>
class ScaleOpenHTJ2KFilter
{
  public:
	ScaleOpenHTJ2KFilter(grk::DecompressBlockExec* block)
		: scale(block->stepsize / (float)(1u << (31 - (block->k_msbs + 1))))
	{
		assert(block->bandNumbps <= 31);
	}
	inline void copy(T* dest, T* src, uint32_t len)
	{
		for(uint32_t i = 0; i < len; ++i)
		{
			((float*)dest)[i] = src[i] * scale;
		}
	}

  private:
	float scale;
};

} // namespace openhtj2k
