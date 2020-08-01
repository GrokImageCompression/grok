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
#include "grok_intmath.h"

namespace grk {

inline bool mult_will_overflow(uint32_t a, uint32_t b) {
	return (b && (a > UINT_MAX / b));
}
inline bool mult64_will_overflow(uint64_t a, uint64_t b) {
	return (b && (a > UINT64_MAX / b));
}

struct grk_pt {
	grk_pt() : x(0), y(0){}
	grk_pt(int64_t _x, int64_t _y) : x(_x), y(_y){}
    int64_t x;
    int64_t y;

};

template<typename T> struct grk_rectangle {
	T x0;
    T y0;
    T x1;
    T y1;


	inline T _max(T x, T y){
		return (x) > (y) ? (x) : (y);
	}
	inline T _min(T x, T y){
		return (x) < (y) ? (x) : (y);
	}

	inline T ceildivpow2(T a, uint32_t b) {
		return (T)((a + ((T) 1 << b) - 1) >> b);
	}


    /**
     Divide an integer and round upwards
     @return a divided by b
     */
    inline T ceildiv(T a, T b) {
    	assert(b);
    	return (a + b - 1) / b;
    }


    void print(void) {
    	std::cout << "[" << x0 << "," << y0 << "," << x1 << "," << y1 << "]"
    			<< std::endl;
    }

    grk_rectangle(void) :
    		x0(0), y0(0), x1(0), y1(0) {
    }

    grk_rectangle(T x0, T y0, T x1, T y1) :
    		x0(x0), y0(y0), x1(x1), y1(y1) {
    }

    bool is_valid(void) {
    	return x0 <= x1 && y0 <= y1;
    }

    bool is_non_degenerate(void) {
    	return x0 < x1 && y0 < y1;
    }

    bool are_equal(grk_rectangle<T> *r2) {

    	if (!r2)
    		return false;

    	return x0 == r2->x0 && y0 == r2->y0 && x1 == r2->x1 && y1 == r2->y1;
    }

    bool clip(grk_rectangle<T> &r2, grk_rectangle<T> *result) {
    	bool rc;
    	grk_rectangle<T> temp;

    	if (!result)
    		return false;

    	temp.x0 = _max(x0, r2.x0);
    	temp.y0 = _max(y0, r2.y0);

    	temp.x1 = _min(x1, r2.x1);
    	temp.y1 = _min(y1, r2.y1);

    	rc = temp.is_valid();

    	if (rc)
    		*result = temp;
    	return rc;
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

    grk_rectangle<T>&  ceildivpow2(uint32_t power) {
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
				GROK_WARN("grk_buf: overflow");
				offset = len;
			} else if (offset + (size_t)off > len){
		#ifdef DEBUG_SEG_BUF
			   GROK_WARN("grk_buf: attempt to increment buffer offset out of bounds");
		#endif
				offset = len;
			} else {
				offset = offset + (size_t)off;
			}
		}
		else if (off < 0){
			if (offset < (size_t)(-off)) {
				GROK_WARN("grk_buf: underflow");
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
																					buf(buffer),
																					owns_data(ownsData),
																					stride(strd)
	{}
	grk_buffer_2d(T *buffer,bool ownsData, uint32_t w, uint32_t h) : grk_buffer_2d(buffer,ownsData,w,w,h)
	{}
	grk_buffer_2d(uint32_t w, uint32_t strd, uint32_t h) : grk_buffer_2d(nullptr,false,w,strd,h)
	{}
	grk_buffer_2d(uint32_t w, uint32_t h) : grk_buffer_2d(w,w,h)
	{}
	grk_buffer_2d(void) : grk_buffer_2d(nullptr,0,0,0,false)
	{}
	virtual ~grk_buffer_2d() {
		if (owns_data)
			grk_aligned_free(buf);
	}

	bool alloc(bool clear){
		if (!buf) {
			uint64_t data_size_needed = area() * sizeof(T);
			if (!data_size_needed)
			  return true;
			buf = (T*) grk_aligned_malloc(data_size_needed);
			if (!buf)
				return false;
			if (clear)
				memset(buf, 0, data_size_needed);
			owns_data = true;
		}

		return true;
	}

	// set data to buf without owning it
	void attach(T* buffer){
		buf = buffer;
		owns_data = false;
	}
	// set data to buf and own it
	void acquire(T* buffer){
		if (owns_data)
			grk_aligned_free(buf);
		buffer = buf;
		owns_data = true;
	}
	// transfer data to buf, and cease owning it
	void transfer(T** buffer, bool* owns){
		if (buffer && owns){
			*buffer = buf;
			buf = nullptr;
			*owns = owns_data;
			owns_data = false;
		}
	}

	T *buf;		/* internal array*/
    bool owns_data;	/* true if buffer manages the buf array */
    uint32_t stride;
} ;
using grk_buf_2d = grk_buffer_2d<uint8_t>;

}

