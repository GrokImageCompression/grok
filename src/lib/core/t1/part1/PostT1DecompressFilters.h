#pragma once

#include "grk_includes.h"

namespace grk
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
   RoiScaleFilter(DecompressBlockExec* block)
	   : roiShift(block->roishift), scale(block->stepsize / 2)
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

} // namespace grk
