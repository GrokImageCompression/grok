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

    void ceildivpow2(uint32_t power) {
    	x0 = ceildivpow2(x0, power);
    	y0 = ceildivpow2(y0, power);
    	x1 = ceildivpow2(x1, power);
    	y1 = ceildivpow2(y1, power);

    }

    void mulpow2(uint32_t power) {
    	if (power == 0)
    		return;
    	x0 *= 1 << power;
    	y0 *= 1 << power;
    	x1 *= 1 << power;
    	y1 *= 1 << power;

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

    void pan(grk_pt *shift) {
    	x0 += shift->x;
    	y0 += shift->y;
    	x1 += shift->x;
    	y1 += shift->y;
    }

    void subsample(uint32_t dx, uint32_t dy) {
    	x0 = ceildiv(x0, (T) dx);
    	y0 = ceildiv(y0, (T) dy);
    	x1 = ceildiv(x1, (T) dx);
    	y1 = ceildiv(y1, (T) dy);
    }

    void grow(T boundary) {
    	grow2(boundary, boundary);
    }

    void grow2(T boundaryx, T boundaryy) {

    	x0 -= boundaryx;
    	y0 -= boundaryy;
    	x1 += boundaryx;
    	y1 += boundaryy;
    }

};

using grk_rect = grk_rectangle<int64_t>;

struct grk_buf {
	grk_buf() : grk_buf(nullptr,0,false) {}
	grk_buf(uint8_t *buffer, size_t off, size_t length, bool ownsData) : buf(buffer),
		offset(off),
		len(length),
		owns_data(ownsData)
	{}
	grk_buf(uint8_t *buffer, size_t length, bool ownsData) :grk_buf(buffer,0,length,ownsData)
	{}
	void dealloc();
	~grk_buf();
	void incr_offset(ptrdiff_t off);
	uint8_t* curr_ptr();

	uint8_t *buf;		/* internal array*/
    size_t offset;	/* current offset into array */
    size_t len;		/* length of array */
    bool owns_data;	/* true if buffer manages the buf array */
} ;


}

