/**
*    Copyright (C) 2016-2020 Grok Image Compression Inc.
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

#include <thread>
#include <climits>
#include <stdint.h>
#include "grk_intmath.h"
#include <limits>       // std::numeric_limits

namespace grk {

inline bool mult_will_overflow(uint32_t a, uint32_t b) {
	return (b && (a > UINT_MAX / b));
}
inline bool mult64_will_overflow(uint64_t a, uint64_t b) {
	return (b && (a > UINT64_MAX / b));
}

template<typename T> struct grk_point {
	grk_point() : x(0), y(0){}
	grk_point(T _x, T _y) : x(_x), y(_y){}
    T x;
    T y;
};
using grk_pt = grk_point<int64_t>;


template<typename T> struct grk_rectangle;
using grk_rect = grk_rectangle<int64_t>;
using grk_rect_u32 = grk_rectangle<uint32_t>;


template<typename T> T clip(int64_t val) {
	if(val < (std::numeric_limits<T>::min)())
		val = (std::numeric_limits<T>::min)();
	else if (val > (std::numeric_limits<T>::max)())
 		val = (std::numeric_limits<T>::max)();
	return (T)val;
}

template<typename T> T sat_add(int64_t lhs, int64_t rhs) {
	return clip<T>(lhs + rhs);
}

template<typename T> T sat_add(T lhs, T rhs) {
	return clip<T>((int64_t)lhs + rhs);
}

template<typename T> T sat_sub(T lhs, T rhs) {
	return clip<T>((int64_t)lhs - rhs);
}

template<typename T> T sat_sub(int64_t lhs, int64_t rhs) {
	return clip<T>(lhs - rhs);
}

template<typename T> struct grk_rectangle {
	T x0, y0,x1,y1;

    grk_rectangle(T x0, T y0, T x1, T y1) :
    		x0(x0), y0(y0), x1(x1), y1(y1) {
    }
    grk_rectangle(const grk_rectangle &rhs){
    	*this = rhs;
    }
    grk_rectangle(void) :
    		x0(0), y0(0), x1(0), y1(0) {
    }
    void print(void) const{
    	std::cout << "[" << x0 << "," << y0 << "," << x1 << "," << y1 << "]"
    			<< std::endl;
    }
    bool is_valid(void) const {
    	return x0 <= x1 && y0 <= y1;
    }
    bool is_non_degenerate(void) const{
    	return x0 < x1 && y0 < y1;
    }
    grk_rectangle<T>& operator= (const grk_rectangle<T> &rhs)
    {
    	if (this != &rhs) { // self-assignment check expected
			x0 = rhs.x0;
			y0 = rhs.y0;
			x1 = rhs.x1;
			y1 = rhs.y1;
    	}

    	return *this;
    }
    grk_rectangle<T>  rectceildivpow2(uint32_t power) const{
    	return grk_rectangle<T>(ceildivpow2(x0, power),
    			ceildivpow2(y0, power),
				ceildivpow2(x1, power),
				ceildivpow2(y1, power));
    }
    grk_rectangle<T> intersection(const grk_rectangle<T> rhs) const{
    	return intersection(&rhs);
    }
    grk_rectangle<T> intersection(const grk_rectangle<T> *rhs) const{
    	return grk_rectangle<T>(std::max<T>(x0,rhs->x0),
    							std::max<T>(y0,rhs->y0),
								std::min<T>(x1,rhs->x1),
								std::min<T>(y1,rhs->y1));
    }
    uint64_t area(void) const {
    	return (uint64_t)(x1 - x0) * (y1 - y0);
    }
    T width() const{
    	return x1 - x0;
    }
    T height() const{
    	return y1 - y0;
    }

    grk_rectangle<T> pan(int64_t x, int64_t y) const {
    	return grk_rectangle<T>( sat_add<T>((int64_t)x0, (int64_t)x),
								 sat_add<T>((int64_t)y0, (int64_t)y),
								 sat_add<T>((int64_t)x1, (int64_t)x),
								 sat_add<T>((int64_t)y1, (int64_t)y));
    }
    grk_rectangle<T>& grow(T boundary) {
    	return grow(boundary, boundary,(std::numeric_limits<T>::max)(),(std::numeric_limits<T>::max)());
    }
    grk_rectangle<T>& grow(T boundaryx, T boundaryy) {
    	return grow(boundaryx, boundaryy,(std::numeric_limits<T>::max)(),(std::numeric_limits<T>::max)());
    }
    grk_rectangle<T>& grow(T boundary, T maxX, T maxY) {
    	return grow(boundary, boundary,maxX,maxY);
    }
    grk_rectangle<T>& grow(T boundaryx, T boundaryy, T maxX, T maxY) {
    	x0 = sat_sub<T>(x0, boundaryx);
    	y0 = sat_sub<T>(y0, boundaryy);
    	x1 = sat_add<T>(x1, boundaryx);
    	y1 = sat_add<T>(y1, boundaryx);
    	if (x1 > maxX)
    		x1 = maxX;
    	if (y1 > maxY)
    		y1 = maxY;
    	return *this;
    }
};

using grk_rect = grk_rectangle<int64_t>;
using grk_rect_u32 = grk_rectangle<uint32_t>;

template <typename T> struct grk_buffer {

	grk_buffer(T *buffer, size_t off, size_t length, bool ownsData) : buf(buffer),
		offset(off),
		len(length),
		owns_data(ownsData)
	{}

	grk_buffer(T *buffer, size_t length, bool ownsData) : grk_buffer(buffer,0,length,ownsData)
	{}

	virtual ~grk_buffer() {
		if (owns_data)
			delete[] buf;
		buf = nullptr;
		owns_data = false;
		offset = 0;
		len = 0;
	}

	void incr_offset(ptrdiff_t off) {
		/*  we allow the offset to move to one location beyond end of buffer segment*/
		if (off > 0 ){
			if (offset > (size_t)(SIZE_MAX - (size_t)off)){
				GRK_WARN("grk_buf: overflow");
				offset = len;
			} else if (offset + (size_t)off > len){
		#ifdef DEBUG_SEG_BUF
			   GRK_WARN("grk_buf: attempt to increment buffer offset out of bounds");
		#endif
				offset = len;
			} else {
				offset = offset + (size_t)off;
			}
		}
		else if (off < 0){
			if (offset < (size_t)(-off)) {
				GRK_WARN("grk_buf: underflow");
				offset = 0;
			} else {
				offset = (size_t)((ptrdiff_t)offset + off);
			}
		}

	}

	T* curr_ptr(){
		if (!buf)
			return nullptr;
		return buf + offset;
	}


	T *buf;		/* internal array*/
    size_t offset;	/* current offset into array */
    size_t len;		/* length of array */
    bool owns_data;	/* true if buffer manages the buf array */
} ;
using grk_buf = grk_buffer<uint8_t>;


template <typename T> struct grk_buffer_2d : public grk_rect_u32 {

	grk_buffer_2d(T *buffer,bool ownsData, uint32_t w, uint32_t strd, uint32_t h) : grk_rect_u32(0,0,w,h),
																					data(buffer),
																					owns_data(ownsData),
																					stride(strd)
	{}
	grk_buffer_2d(T *buffer,bool ownsData, uint32_t w, uint32_t h) : grk_buffer_2d(buffer,ownsData,w,w,h)
	{}
	grk_buffer_2d(uint32_t w, uint32_t strd, uint32_t h) : grk_buffer_2d(nullptr,false,w,strd,h)
	{}
	grk_buffer_2d(uint32_t w, uint32_t h) : grk_buffer_2d(w,w,h)
	{}
	explicit grk_buffer_2d(grk_rect_u32 b) : grk_buffer_2d(b.width(),b.width(),b.height())
	{}
	grk_buffer_2d(void) : grk_buffer_2d(nullptr,0,0,0,false)
	{}
	virtual ~grk_buffer_2d() {
		if (owns_data)
			grk_aligned_free(data);
	}

	bool alloc(bool clear){
		if (!data) {
			stride = grk_make_aligned_width(width());
			uint64_t data_size_needed = stride * height() * sizeof(T);
			if (!data_size_needed)
			  return true;
			data = (T*) grk_aligned_malloc(data_size_needed);
			if (!data)
				return false;
			if (clear)
				memset(data, 0, data_size_needed);
			owns_data = true;
		}

		return true;
	}

	// set data to buf without owning it
	void attach(T* buffer, uint32_t strd){
		if (owns_data)
			grk_aligned_free(data);
		data = buffer;
		owns_data = false;
		stride = strd;
	}
	// set data to buf and own it
	void acquire(T* buffer, uint32_t strd){
		if (owns_data)
			grk_aligned_free(data);
		buffer = data;
		owns_data = true;
		stride = strd;
	}
	// transfer data to buf, and cease owning it
	void transfer(T** buffer, bool* owns, uint32_t *strd){
		if (buffer && owns){
			*buffer = data;
			data = nullptr;
			*owns = owns_data;
			owns_data = false;
			*strd = stride;
		}
	}

	T *data;		/* internal array*/
    bool owns_data;	/* true if buffer manages the data array */
    uint32_t stride;
} ;

grk_rect_u32 grk_region_band(uint32_t num_res,
							uint32_t resno,
							uint32_t orientation,
							grk_rect_u32 unreduced_region);

}

