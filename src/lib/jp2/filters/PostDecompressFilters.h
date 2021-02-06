#pragma once

#include "grk_includes.h"

namespace grk {


template<typename T> class RoiShiftFilter {
public:
	RoiShiftFilter(DecompressBlockExec *block) : roiShift(block->roishift){}
	inline void copy(T* dest,T* src, uint32_t len){
		T thresh = 1 << roiShift;
		for (uint32_t i = 0; i < len; ++i){
			T val = src[i];
			T mag = abs(val);
			if (mag >= thresh) {
				mag >>= roiShift;
				val = val < 0 ? -mag : mag;
			}
			dest[i] = val/2;
		}
	}
private:
	uint32_t roiShift;
};
template<typename T> class ShiftFilter {
public:
	ShiftFilter(DecompressBlockExec *block){
		(void)block;
	}
	inline void copy(T* dest,T* src, uint32_t len){
		for (uint32_t i = 0; i < len; ++i)
			dest[i] = src[i]/2;
	}
};


template<typename T> class RoiScaleFilter {
public:
	RoiScaleFilter(DecompressBlockExec *block) : roiShift(block->roishift),
												scale(block->stepsize/2)
	{}
	inline void copy(T* dest,T* src, uint32_t len){
		T thresh = 1 << roiShift;
		for (uint32_t i = 0; i < len; ++i){
			T val = src[i];
			T mag = abs(val);
			if (mag >= thresh) {
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

template<typename T> class ScaleFilter {
public:
	ScaleFilter(DecompressBlockExec *block) : scale(block->stepsize/2)
	{}
	inline void copy(T* dest,T* src, uint32_t len){
		for (uint32_t i = 0; i < len; ++i){
			((float*)dest)[i] = (float)src[i] * scale;
		}
	}
private:
	float scale;
};


template<typename T> class RoiShiftHTFilter {
public:
	RoiShiftHTFilter(DecompressBlockExec *block) : roiShift(block->roishift),
													 shift(31U - (block->k_msbs + 1U))
	{}
	inline void copy(T* dest,T* src, uint32_t len){
		T thresh = 1 << roiShift;
		for (uint32_t i = 0; i < len; ++i){
			T val = src[i];
			T mag = (val & 0x7FFFFFFF);
			if (mag >= thresh)
				val = (T)(((uint32_t)mag >> roiShift) & ((uint32_t)val & 0x80000000));
			int32_t val_shifted = (val & 0x7FFFFFFF) >> shift;
			dest[i] = (int32_t)(((uint32_t)val & 0x80000000) ? -val_shifted : val_shifted);
		}
	}
private:
	uint32_t roiShift;
	uint32_t shift;
};
template<typename T> class ShiftHTFilter {
public:
	ShiftHTFilter(DecompressBlockExec *block) :  shift(31U - (block->k_msbs + 1U)){}
	inline void copy(T* dest,T* src, uint32_t len){
		for (uint32_t i = 0; i < len; ++i){
			T val = src[i];
			T val_shifted = (val & 0x7FFFFFFF) >> shift;
			dest[i] = (T)(((uint32_t)val & 0x80000000) ? -val_shifted : val_shifted);
		}
	}
private:
	uint32_t shift;
};

template<typename T> class RoiScaleHTFilter {
public:
	RoiScaleHTFilter(DecompressBlockExec *block) : roiShift(block->roishift), scale(block->stepsize)	{}
	inline void copy(T* dest,T* src, uint32_t len){
		T thresh = 1 << roiShift;
		for (uint32_t i = 0; i < len; ++i){
			T val = src[i];
			T mag = (T)(val & 0x7FFFFFFF);
			if (mag >= thresh)
				val = (T)(((uint32_t)mag >> roiShift) & ((uint32_t)val & 0x80000000));
		    float val_scaled = (float)(val & 0x7FFFFFFF) * scale;
		    ((float*)dest)[i] = ((uint32_t)val & 0x80000000) ? -val_scaled : val_scaled;
		}
	}
private:
	uint32_t roiShift;
	float scale;
};


template<typename T> class ScaleHTFilter {
public:
	ScaleHTFilter(DecompressBlockExec *block) : scale(block->stepsize)	{}
	inline void copy(T* dest,T* src, uint32_t len){
		for (uint32_t i = 0; i < len; ++i){
			int32_t val = src[i];
		    float val_scaled = (float)(val & 0x7FFFFFFF) * scale;
		    ((float*)dest)[i] = ((uint32_t)val & 0x80000000) ? -val_scaled : val_scaled;
		}
	}
private:
	float scale;
};

}
