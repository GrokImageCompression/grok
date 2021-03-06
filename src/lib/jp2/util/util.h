/**
*    Copyright (C) 2016-2021 Grok Image Compression Inc.
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

#include "grok.h"
#include "logger.h"
#include <iostream>
#include <cstdint>
#include "grk_intmath.h"
#include <limits>
#include <sstream>

namespace grk {

template<typename T> struct grkPoint {
	grkPoint() : x(0), y(0){}
	grkPoint(T _x, T _y) : x(_x), y(_y){}
    T x;
    T y;
};
using grkPointU32 = grkPoint<uint32_t>;

template<typename T> struct grkLine {
	grkLine() : x0(0), x1(0){}
	grkLine(T _x0, T _x1) : x0(_x0), x1(_x1){}
    T x0;
    T x1;
    T length(){
    	assert(x1 >= x0);
    	return (T)(x1-x0);
    }
};
using grkLineU32 = grkLine<uint32_t>;

template<typename T> struct grkRect;
using grkRectU32 = grkRect<uint32_t>;
using grkRectS64 = grkRect<int64_t>;

template<typename T> T clip(int64_t val) {
	static_assert(sizeof(T) <= 4);
	if(val < (std::numeric_limits<T>::min)())
		val = (std::numeric_limits<T>::min)();
	else if (val > (std::numeric_limits<T>::max)())
 		val = (std::numeric_limits<T>::max)();
	return (T)val;
}

template<typename T> T satAdd(int64_t lhs, int64_t rhs) {
	return clip<T>(lhs + rhs);
}

template<typename T> T satAdd(T lhs, T rhs) {
	return clip<T>((int64_t)lhs + rhs);
}

template<typename T> T satSub(T lhs, T rhs) {
	return clip<T>((int64_t)lhs - rhs);
}

template<typename T> T satSub(int64_t lhs, int64_t rhs) {
	return clip<T>(lhs - rhs);
}

template<typename T> struct grkRect {
	T x0, y0,x1,y1;

    grkRect(T x0, T y0, T x1, T y1) :
    		x0(x0), y0(y0), x1(x1), y1(y1) {
    }
    grkRect(const grkRect &rhs){
    	*this = rhs;
    }
    grkRect(void) :
    		x0(0), y0(0), x1(0), y1(0) {
    }
    void print(void) const{
    	std::cout << "[" << x0 << "," << y0 << "," << x1 << "," << y1 << "]"
    			<< std::endl;
    }
    std::string boundsString() {
    	std::ostringstream os;
    	os << "[" << x0 << "," << y0 << "," << x1 << "," << y1 << "]";
    	return os.str();
    }
    bool is_valid(void) const {
    	return x0 <= x1 && y0 <= y1;
    }
    bool non_empty(void) const{
    	return x0 < x1 && y0 < y1;
    }
    bool contains(grkPoint<T> pt){
    	return pt.x >= x0 && pt.y >= y0 && pt.x < x1 && pt.y < y1;
    }
    grkRect<T>& operator= (const grkRect<T> &rhs)  {
    	if (this != &rhs) { // self-assignment check expected
			x0 = rhs.x0;
			y0 = rhs.y0;
			x1 = rhs.x1;
			y1 = rhs.y1;
    	}

    	return *this;
    }
   bool operator== (const grkRect<T> &rhs) const    {
	   if (this == &rhs)
		   return true;
		return x0 == rhs.x0 &&	y0 == rhs.y0 && x1 == rhs.x1 && y1 == rhs.y1;
    }
	void set(grkRect<T> *rhs){
		*this = *rhs;
	}
	void set(grkRect<T> rhs){
		set(&rhs);
	}
    grkRect<T>  rectceildivpow2(uint32_t power) const{
    	return grkRect<T>(ceildivpow2(x0, power),
    			ceildivpow2(y0, power),
				ceildivpow2(x1, power),
				ceildivpow2(y1, power));
    }
    grkRect<T>  rectceildiv(uint32_t den) const{
    	return grkRect<T>(ceildiv(x0, den),
    			ceildiv(y0, den),
				ceildiv(x1, den),
				ceildiv(y1, den));
    }
    grkRect<T>  rectceildiv(uint32_t denx, uint32_t deny) const{
    	return grkRect<T>(ceildiv(x0, denx),
    			ceildiv(y0, deny),
				ceildiv(x1, denx),
				ceildiv(y1, deny));
    }
    grkRect<T> intersection(const grkRect<T> rhs) const{
    	return intersection(&rhs);
    }
    bool isContainedIn(const grkRect<T> rhs) const{
    	return (intersection(&rhs)== *this);
    }
    void clip(const grkRect<T> *rhs){
    	*this = grkRect<T>(std::max<T>(x0,rhs->x0),
    							std::max<T>(y0,rhs->y0),
								std::min<T>(x1,rhs->x1),
								std::min<T>(y1,rhs->y1));
    }
    grkRect<T> intersection(const grkRect<T> *rhs) const{
    	return grkRect<T>(std::max<T>(x0,rhs->x0),
    							std::max<T>(y0,rhs->y0),
								std::min<T>(x1,rhs->x1),
								std::min<T>(y1,rhs->y1));
    }
    inline bool non_empty_intersection(const grkRect<T> *rhs) const{
    	return std::max<T>(x0,rhs->x0) < std::min<T>(x1,rhs->x1) &&
    			std::max<T>(y0,rhs->y0) < std::min<T>(y1,rhs->y1);
    }
    grkRect<T> rectUnion(const grkRect<T> *rhs) const{
    	return grkRect<T>(std::min<T>(x0,rhs->x0),
    							std::min<T>(y0,rhs->y0),
								std::max<T>(x1,rhs->x1),
								std::max<T>(y1,rhs->y1));
    }
    grkRect<T> rectUnion(const grkRect<T> &rhs) const{
    	return rectUnion(&rhs);
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
    grkLine<T> dimX(){
    	return grkLine<T>(x0,x1);
    }
    grkLine<T> dimY(){
    	return grkLine<T>(y0,y1);
    }
    grkRect<T> pan(int64_t x, int64_t y) const {
    	auto rc = *this;
    	rc.panInplace(x,y);
    	return rc;
    }
    void panInplace(int64_t x, int64_t y) {
    	x0 =  satAdd<T>((int64_t)x0, (int64_t)x);
		y0 =  satAdd<T>((int64_t)y0, (int64_t)y);
		x1 =  satAdd<T>((int64_t)x1, (int64_t)x);
		y1 =  satAdd<T>((int64_t)y1, (int64_t)y);
    }
    grkRect<T>& grow(T boundary) {
    	return grow(boundary, boundary,(std::numeric_limits<T>::max)(),(std::numeric_limits<T>::max)());
    }
    grkRect<T>& grow(T boundaryx, T boundaryy) {
    	return grow(boundaryx, boundaryy,(std::numeric_limits<T>::max)(),(std::numeric_limits<T>::max)());
    }
    grkRect<T>& grow(T boundary, T maxX, T maxY) {
    	return grow(boundary, boundary,maxX,maxY);
    }
    grkRect<T>& grow(T boundaryx, T boundaryy, T maxX, T maxY) {
    	return grow(boundaryx,boundaryy, grkRect<T>((T)0,(T)0,maxX,maxY));
    }
    grkRect<T>& grow(T boundary, grkRect<T> bounds) {
    	return grow(boundary,boundary, bounds);
    }
    grkRect<T>& grow(T boundaryx, T boundaryy, grkRect<T> bounds) {
    	x0 = std::max<T>( satSub<T>(x0, boundaryx), bounds.x0);
    	y0 = std::max<T>( satSub<T>(y0, boundaryy), bounds.y0);
    	x1 = std::min<T>( satAdd<T>(x1, boundaryx), bounds.x1);
    	y1 = std::min<T>( satAdd<T>(y1, boundaryy), bounds.y1);

    	return *this;
    }
};

using grkRectS64 = grkRect<int64_t>;
using grkRectU32 = grkRect<uint32_t>;

template <typename T> struct grkBuffer {
	grkBuffer(T *buffer, size_t off, size_t length, bool ownsData) : buf(buffer),
		offset(off),
		len(length),
		owns_data(ownsData)
	{}
	grkBuffer() : grkBuffer(0,0,0,false)
	{}
	grkBuffer(T *buffer, size_t length, bool ownsData) : grkBuffer(buffer,0,length,ownsData)
	{}
	virtual ~grkBuffer() {
		dealloc();
	}
	void alloc(size_t length) {
		dealloc();
		buf = new T[length];
		len = length;
		offset = 0;
		owns_data = true;
	}
	virtual void dealloc(){
		if (owns_data)
			delete[] buf;
		buf = nullptr;
		owns_data = false;
		offset = 0;
		len = 0;
	}
	size_t remainingLength(void){
		return len - offset;
	}
	void incrementOffset(ptrdiff_t off) {
		/*  we allow the offset to move to one location beyond end of buffer segment*/
		if (off > 0 ){
			if (offset > (size_t)(SIZE_MAX - (size_t)off)){
				GRK_WARN("grkBufferU8: overflow");
				offset = len;
			} else if (offset + (size_t)off > len){
		#ifdef DEBUG_SEG_BUF
			   GRK_WARN("grkBufferU8: attempt to increment buffer offset out of bounds");
		#endif
				offset = len;
			} else {
				offset = offset + (size_t)off;
			}
		}
		else if (off < 0){
			if (offset < (size_t)(-off)) {
				GRK_WARN("grkBufferU8: underflow");
				offset = 0;
			} else {
				offset = (size_t)((ptrdiff_t)offset + off);
			}
		}
	}
	T* currPtr(){
		if (!buf)
			return nullptr;
		return buf + offset;
	}

	T *buf;		/* internal array*/
    size_t offset;	/* current offset into array */
    size_t len;		/* length of array */
    bool owns_data;	/* true if buffer manages the buf array */
} ;
using grkBufferU8 = grkBuffer<uint8_t>;
template <typename T> struct grkBuffer2d : public grkRectU32 {
	grkBuffer2d(T *buffer,bool ownsData, uint32_t w, uint32_t strd, uint32_t h) : grkRectU32(0,0,w,h),
																					data(buffer),
																					owns_data(ownsData),
																					stride(strd)
	{}
	grkBuffer2d(uint32_t w, uint32_t strd, uint32_t h) : grkBuffer2d(nullptr,false,w,strd,h)
	{}
	grkBuffer2d(uint32_t w, uint32_t h) : grkBuffer2d(w,0,h)
	{}
	explicit grkBuffer2d(grkRectU32 b) : grkRectU32(b.x0,b.y0,b.x1,b.y1),
											data(nullptr),
											owns_data(false),
											stride(0)
	{}
	grkBuffer2d(void) : grkBuffer2d(nullptr,0,0,0,false)
	{}
	grkBuffer2d& operator=(const grkBuffer2d& rhs) // copy assignment
	{
	    return operator=(&rhs);
	}
	grkBuffer2d& operator=(const grkBuffer2d* rhs) // copy assignment
	{
	    if (this != rhs) { // self-assignment check expected
	    	data = rhs->data;
	    	owns_data = false;
	    	stride = rhs->stride;
	    	*((grkRectU32*)this) = *((grkRectU32*)rhs);
	    }
	    return *this;
	}
	virtual ~grkBuffer2d() {
		if (owns_data)
			grkAlignedFree(data);
	}
	bool alloc(bool clear){
		if (!data && width() && height()) {
			stride = grkMakeAlignedWidth(width());
			uint64_t data_size_needed = (uint64_t)stride * height() * sizeof(T);
			if (!data_size_needed)
			  return true;
			data = (T*) grkAlignedMalloc(data_size_needed);
			if (!data) {
				grk::GRK_ERROR("Failed to allocate aligned memory buffer of dimensions %u x %u "
						"@ alignment %d",stride, height(), grk::default_align);
				return false;
			}
			if (clear)
				memset(data, 0, data_size_needed);
			owns_data = true;
		}

		return true;
	}
	// set data to buf without owning it
	void attach(T* buffer, uint32_t strd){
		if (owns_data)
			grkAlignedFree(data);
		data = buffer;
		owns_data = false;
		stride = strd;
	}
	// set data to buf and own it
	void acquire(T* buffer, uint32_t strd){
		if (owns_data)
			grkAlignedFree(data);
		buffer = data;
		owns_data = true;
		stride = strd;
	}
	// transfer data to buf, and cease owning it
	void transfer(T** buffer, uint32_t *strd){
		if (buffer){
			*buffer = data;
			data = nullptr;
			*strd = stride;
		}
	}
	bool copy_data(T* dest, uint32_t dest_w, uint32_t dest_h, uint32_t dest_stride) const{
		assert(dest_w <= width());
		assert(dest_h <= height());
		assert(dest_stride <= stride);
		if (dest_w > width() || dest_h > height() || dest_stride > stride)
			return false;
		if (!data)
			return false;
		auto src_ptr = data;
		auto dest_ptr = dest;
		for (uint32_t j = 0; j < dest_h; ++j) {
			memcpy(dest_ptr,src_ptr, dest_w* sizeof(T));
			dest_ptr += dest_stride;
			src_ptr += stride;
		}
		return true;
	}
	// rhs coordinates are in "this" coordinate system
	template<typename F> void copy(grkBuffer2d &rhs, F filter){
		auto inter = this->intersection(rhs);
		if (!inter.non_empty())
			return;

		T* dest = data + (inter.y0 * stride + inter.x0);
		T* src = rhs.data + ((inter.y0 - rhs.y0) * rhs.stride + inter.x0 - rhs.x0);
		uint32_t len = inter.width();
		for (uint32_t j = inter.y0; j < inter.y1; ++j){
			filter.copy(dest,src, len);
			dest += stride;
			src += rhs.stride;
		}
	}
	T *data;		/* internal array*/
    bool owns_data;	/* true if buffer manages the data array */
    uint32_t stride;
} ;
grkRectU32 getTileCompBandWindow(uint32_t numDecomps,
							uint8_t orientation,
							grkRectU32 unreducedTileCompWindow);
grkRectU32 getTileCompBandWindow(uint32_t numDecomps,
							uint8_t orientation,
							grkRectU32 unreducedTileCompWindow,
							grkRectU32 unreducedTileComp,
							uint32_t padding);
}
