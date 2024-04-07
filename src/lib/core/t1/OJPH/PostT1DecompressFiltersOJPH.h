#pragma once

#include "grk_includes.h"

namespace ojph
{
template<typename T>
class RoiShiftOJPHFilter
{
 public:
   explicit RoiShiftOJPHFilter(grk::DecompressBlockExec* block)
	   : roiShift(block->roishift), shift(31U - (block->k_msbs + 1U))
   {}
   inline void copy(T* dest, const T* src, uint32_t len)
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
class ShiftOJPHFilter
{
 public:
   explicit ShiftOJPHFilter(grk::DecompressBlockExec* block) : shift(31U - (block->k_msbs + 1U)) {}
   inline void copy(T* dest, const T* src, uint32_t len)
   {
	  for(uint32_t i = 0; i < len; ++i)
	  {
		 T val = src[i];
		 T val_shifted = (val & 0x7FFFFFFF) >> shift;
		 dest[i] = (T)(((uint32_t)val & 0x80000000) ? -val_shifted : val_shifted);
	  }
   }

 private:
   uint32_t shift;
};

template<typename T>
class RoiScaleOJPHFilter
{
 public:
   explicit RoiScaleOJPHFilter(grk::DecompressBlockExec* block)
	   : roiShift(block->roishift), scale(block->stepsize / (float)(1u << (31 - block->bandNumbps)))
   {
	  assert(block->bandNumbps <= 31);
   }
   inline void copy(T* dest, const T* src, uint32_t len)
   {
	  T thresh = 1 << roiShift;
	  for(uint32_t i = 0; i < len; ++i)
	  {
		 T val = src[i];
		 T mag = (T)(val & 0x7FFFFFFF);
		 if(mag >= thresh)
			val = (T)(((uint32_t)mag >> roiShift) & ((uint32_t)val & 0x80000000));
		 float val_scaled = (float)(val & 0x7FFFFFFF) * scale;
		 ((float*)dest)[i] = ((uint32_t)val & 0x80000000) ? -val_scaled : val_scaled;
	  }
   }

 private:
   uint32_t roiShift;
   float scale;
};

template<typename T>
class ScaleOJPHFilter
{
 public:
   explicit ScaleOJPHFilter(grk::DecompressBlockExec* block)
	   : scale(block->stepsize / (float)(1u << (31 - block->bandNumbps)))
   {
	  assert(block->bandNumbps <= 31);
   }
   inline void copy(T* dest, const T* src, uint32_t len)
   {
	  for(uint32_t i = 0; i < len; ++i)
	  {
		 int32_t val = src[i];
		 float val_scaled = (float)(val & 0x7FFFFFFF) * scale;
		 ((float*)dest)[i] = ((uint32_t)val & 0x80000000) ? -val_scaled : val_scaled;
	  }
   }

 private:
   float scale;
};

} // namespace ojph
