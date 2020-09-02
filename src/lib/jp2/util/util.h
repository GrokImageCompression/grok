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

template<typename T> struct grk_rectangle {
	T x0;
    T y0;
    T x1;
    T y1;

    grk_rectangle(T x0, T y0, T x1, T y1) :
    		x0(x0), y0(y0), x1(x1), y1(y1) {
    }

    grk_rectangle(const grk_rectangle &rhs){
    	*this = rhs;
    }

    grk_rect_u32 to_u32(){
		return grk_rect_u32((uint32_t)x0,(uint32_t)y0,(uint32_t)x1,(uint32_t)y1);
	}


    void print(void) {
    	std::cout << "[" << x0 << "," << y0 << "," << x1 << "," << y1 << "]"
    			<< std::endl;
    }

    grk_rectangle(void) :
    		x0(0), y0(0), x1(0), y1(0) {
    }

    bool is_valid(void) {
    	return x0 <= x1 && y0 <= y1;
    }

    bool is_non_degenerate(void) {
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


    grk_rectangle<T>& operator- (const grk_rectangle<T> &rhs)
    {
        x0 -= rhs.x0;
        y0 -= rhs.y0;
        x1 -= rhs.x1;
        y1 -= rhs.y1;

        return *this;
    }
    grk_rectangle<T>& operator-= (const grk_rectangle<T> &rhs)
    {
        x0 -= rhs.x0;
        y0 -= rhs.y0;
        x1 -= rhs.x1;
        y1 -= rhs.y1;

        return *this;
    }

    grk_rectangle<T>&  rectceildivpow2(uint32_t power) {
    	x0 = ceildivpow2(x0, power);
    	y0 = ceildivpow2(y0, power);
    	x1 = ceildivpow2(x1, power);
    	y1 = ceildivpow2(y1, power);

    	return *this;

    }

    grk_rectangle<T>&  mulpow2(uint32_t power) {
		x0 *= 1 << power;
		y0 *= 1 << power;
		x1 *= 1 << power;
		y1 *= 1 << power;

    	return *this;

    }

    grk_rectangle<T>& intersection(const grk_rectangle<T> rhs){
    	x0 = std::max<T>(x0,rhs.x0);
		y0 = std::max<T>(y0,rhs.y0);
		x1 = std::min<T>(x1,rhs.x1);
		y1 = std::min<T>(y1,rhs.y1);

		return *this;
    }
    grk_rectangle<T>& r_union(const grk_rectangle<T> rhs){
    	x0 = std::min<T>(x0,rhs.x0);
		y0 = std::min<T>(y0,rhs.y0);
		x1 = std::max<T>(x1,rhs.x1);
		y1 = std::max<T>(y1,rhs.y1);

		return *this;
    }


    uint64_t area(void) {
    	return (uint64_t)(x1 - x0) * (y1 - y0);
    }

    T width(){
    	return x1 - x0;
    }
    T height(){
    	return y1 - y0;
    }


    grk_rectangle<T>& pan(T x, T y) {
    	x0 += x;
    	y0 += y;
    	x1 += x;
    	y1 += y;

    	return *this;
    }
    grk_rectangle<T>& subsample(uint32_t dx, uint32_t dy) {
    	x0 = ceildiv(x0, (T) dx);
    	y0 = ceildiv(y0, (T) dy);
    	x1 = ceildiv(x1, (T) dx);
    	y1 = ceildiv(y1, (T) dy);
    }

    grk_rectangle<T>& grow(T boundary) {
    	return grow(boundary, boundary);
    }

    grk_rectangle<T>& grow(T boundaryx, T boundaryy) {

    	x0 -= boundaryx;
    	y0 -= boundaryy;
    	x1 += boundaryx;
    	y1 += boundaryy;

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
    bool owns_data;	/* true if buffer manages the buf array */
    uint32_t stride;
} ;

}

