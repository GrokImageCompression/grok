/*
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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#pragma once

#include <stddef.h>

namespace grk {

const size_t default_align = 64;

uint32_t grkMakeAlignedWidth(uint32_t width);
/**
 Allocate an uninitialized memory block
 @param size Bytes to allocate
 @return a void pointer to the allocated space, or nullptr if there is insufficient memory available
 */
void* grkMalloc(size_t size);
/**
 Allocate a memory block with elements initialized to 0
 @param numOfElements  Blocks to allocate
 @param sizeOfElements Bytes per block to allocate
 @return a void pointer to the allocated space, or nullptr if there is insufficient memory available
 */
void* grkCalloc(size_t numOfElements, size_t sizeOfElements);
/**
 Allocate memory aligned to a 16 byte boundary
 @param size Bytes to allocate
 @return a void pointer to the allocated space, or nullptr if there is insufficient memory available
 */
void* grkAlignedMalloc(size_t size);
void grkAlignedFree(void *ptr);
/**
 Reallocate memory blocks.
 @param m Pointer to previously allocated memory block
 @param s New size in bytes
 @return a void pointer to the reallocated (and possibly moved) memory block
 */
void* grkRealloc(void *m, size_t s);
/**
 Deallocates or frees a memory block.
 @param m Previously allocated memory block to be freed
 */
void grkFree(void *m);

#if defined(__GNUC__) && !defined(GROK_SKIP_POISON)
#pragma GCC poison malloc calloc realloc free
#endif

template<typename T> struct AllocatorVanilla{
	T* alloc(size_t length) {
		return new T[length];
	}
	void dealloc(T* buf){
		delete[] buf;
	}
};
template<typename T> struct AllocatorAligned{
	T* alloc(size_t length) {
		return (T*) grkAlignedMalloc(length * sizeof(T));
	}
	void dealloc(T* buf){
		grkAlignedFree(buf);
	}
};
template <typename T, template <typename TT> typename A > struct grkBuffer : A<T> {
	grkBuffer(T *buffer, size_t off, size_t length, bool ownsData) : buf(buffer),
		offset(off),
		len(length),
		owns_data(ownsData)
	{}
	grkBuffer(T *buffer, size_t length) : grkBuffer(buffer,0,length,false)
	{}
	grkBuffer() : grkBuffer(0,0,0,false)
	{}
	grkBuffer(T *buffer, size_t length, bool ownsData) : grkBuffer(buffer,0,length,ownsData)
	{}
	virtual ~grkBuffer() {
		dealloc();
	}
	grkBuffer& operator=(const grkBuffer& rhs) // copy assignment
	{
	    return operator=(&rhs);
	}
	grkBuffer& operator=(const grkBuffer* rhs) // copy assignment
	{
	    if (this != rhs) { // self-assignment check expected
	    	buf = rhs->buf;
	    	owns_data = false;
	    }
	    return *this;
	}
	virtual bool alloc(size_t length) {
		if (buf && len > length)
			return true;
		dealloc();
		buf = A<T>::alloc(length);
		if (!buf)
			return false;
		len = length;
		offset = 0;
		owns_data = true;

		return true;
	}
	virtual void dealloc(){
		if (owns_data)
			A<T>::dealloc(buf);
		buf = nullptr;
		owns_data = false;
		offset = 0;
		len = 0;
	}
	// set buf to buf without owning it
	void attach(T* buffer){
		grkBuffer<T, A >::dealloc();
		buf = buffer;
	}
	// set buf to buf and own it
	void acquire(T* buffer){
		grkBuffer<T, A >::dealloc();
		buffer = buf;
		owns_data = true;
	}
	// transfer buf to buf, and cease owning it
	void transfer(T** buffer){
		if (buffer){
			*buffer = buf;
			buf = nullptr;
			owns_data = false;
		}
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
};

using grkBufferU8 = grkBuffer<uint8_t, AllocatorVanilla >;
using grkBufferU8Aligned = grkBuffer<uint8_t, AllocatorAligned >;

template <typename T, template <typename TT> typename A> struct grkBuffer2d : public grkBuffer<T, A >, public grkRectU32 {
	grkBuffer2d(T *buffer,bool ownsData, uint32_t w, uint32_t strd, uint32_t h) :   grkBuffer<T, A >(buffer,ownsData),
																					grkRectU32(0,0,w,h),
																					stride(strd)
	{}
	grkBuffer2d(uint32_t w, uint32_t strd, uint32_t h) : grkBuffer2d(nullptr,false,w,strd,h)
	{}
	grkBuffer2d(uint32_t w, uint32_t h) : grkBuffer2d(w,0,h)
	{}
	explicit grkBuffer2d(grkRectU32 b) : 	grkBuffer<T, A >(nullptr,false),
											grkRectU32(b.x0,b.y0,b.x1,b.y1),
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
	    	this->grkBuffer<T, A >::operator =(rhs);
	    	stride = rhs->stride;
	    	*((grkRectU32*)this) = *((grkRectU32*)rhs);
	    }
	    return *this;
	}
	virtual ~grkBuffer2d() = default;
	bool alloc2d(bool clear){
		if (!this->buf && width() && height()) {
			if (!stride)
				stride = grkMakeAlignedWidth(width());
			uint64_t data_size_needed = (uint64_t)stride * height() * sizeof(T);
			if (!data_size_needed)
			  return true;
			if (!grkBuffer<T, A >::alloc(data_size_needed)) {
				grk::GRK_ERROR("Failed to allocate aligned memory buffer of dimensions %u x %u "
						"@ alignment %d",stride, height(), grk::default_align);
				return false;
			}
			if (clear)
				memset(this->buf, 0, data_size_needed);
		}

		return true;
	}
	// set buf to buf without owning it
	void attach(T* buffer, uint32_t strd){
		grkBuffer<T, A >::attach(buffer);
		stride = strd;
	}
	// set buf to buf and own it
	void acquire(T* buffer, uint32_t strd){
		grkBuffer<T, A >::acquire(buffer);
		stride = strd;
	}
	// transfer buf to buf, and cease owning it
	void transfer(T** buffer, uint32_t *strd){
		if (buffer){
			grkBuffer<T, A >::transfer(buffer);
			*strd = stride;
		}
	}
	bool copy_data(T* dest, uint32_t dest_w, uint32_t dest_h, uint32_t dest_stride) const{
		assert(dest_w <= width());
		assert(dest_h <= height());
		assert(dest_stride <= stride);
		if (dest_w > width() || dest_h > height() || dest_stride > stride)
			return false;
		if (!this->buf)
			return false;
		auto src_ptr = this->buf;
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

		T* dest = this->buf + (inter.y0 * stride + inter.x0);
		T* src = rhs.buf + ((inter.y0 - rhs.y0) * rhs.stride + inter.x0 - rhs.x0);
		uint32_t len = inter.width();
		for (uint32_t j = inter.y0; j < inter.y1; ++j){
			filter.copy(dest,src, len);
			dest += stride;
			src += rhs.stride;
		}
	}
    uint32_t stride;
} ;

}
