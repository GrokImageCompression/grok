/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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

#if defined(__GNUC__) && !defined(GROK_SKIP_POISON)
#pragma GCC poison malloc calloc realloc free
#endif

#include <cstddef>

namespace grk
{
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
void grkAlignedFree(void* ptr);
/**
 Reallocate memory blocks.
 @param m Pointer to previously allocated memory block
 @param s New size in bytes
 @return a void pointer to the reallocated (and possibly moved) memory block
 */
void* grkRealloc(void* m, size_t s);
/**
 Deallocates or frees a memory block.
 @param m Previously allocated memory block to be freed
 */
void grkFree(void* m);

template<typename T>
struct AllocatorVanilla
{
	T* alloc(size_t length)
	{
		return new T[length];
	}
	void dealloc(T* buf)
	{
		delete[] buf;
	}
};
template<typename T>
struct AllocatorAligned
{
	T* alloc(size_t length)
	{
		return (T*)grkAlignedMalloc(length * sizeof(T));
	}
	void dealloc(T* buf)
	{
		grkAlignedFree(buf);
	}
};
template<typename T, template<typename TT> typename A>
struct grk_buf : A<T>
{
	grk_buf(T* buffer, size_t off, size_t length, bool ownsData)
		: buf(buffer), offset(off), len(length), owns_data(ownsData)
	{}
	grk_buf(T* buffer, size_t length) : grk_buf(buffer, 0, length, false) {}
	grk_buf() : grk_buf(0, 0, 0, false) {}
	grk_buf(T* buffer, size_t length, bool ownsData) : grk_buf(buffer, 0, length, ownsData) {}
	virtual ~grk_buf()
	{
		dealloc();
	}
	explicit grk_buf(const grk_buf& rhs)
	{
		this->operator=(rhs);
	}
	grk_buf& operator=(const grk_buf& rhs) // copy assignment
	{
		return operator=(&rhs);
	}
	grk_buf& operator=(const grk_buf* rhs) // copy assignment
	{
		if(this != rhs)
		{ // self-assignment check expected
			buf = rhs->buf;
			offset = rhs->offset;
			len = rhs->len;
			owns_data = false;
		}
		return *this;
	}
	inline bool canRead(void)
	{
		return offset < len;
	}
	inline T read(void)
	{
		return buf[offset++];
	}
	inline bool write(T val)
	{
		if(offset == len)
			return false;
		buf[offset++] = val;

		return true;
	}
	inline bool write(T* b, size_t size)
	{
		if(offset + size > len)
			return false;
		memcpy(buf + offset, b, size);
		offset += size;

		return true;
	}
	virtual bool alloc(size_t length)
	{
		if(buf && len > length)
			return true;
		dealloc();
		buf = A<T>::alloc(length);
		if(!buf)
			return false;
		len = length;
		offset = 0;
		owns_data = true;

		return true;
	}
	virtual void dealloc()
	{
		if(owns_data)
			A<T>::dealloc(buf);
		buf = nullptr;
		owns_data = false;
		offset = 0;
		len = 0;
	}
	// set buf to buf without owning it
	void attach(T* buffer)
	{
		grk_buf<T, A>::dealloc();
		buf = buffer;
	}
	// set buf to buf and own it
	void acquire(T* buffer)
	{
		grk_buf<T, A>::dealloc();
		buffer = buf;
		owns_data = true;
	}
	// transfer buf to buffer, and cease owning it
	void transfer(T** buffer)
	{
		if(buffer)
		{
			assert(!buf || owns_data);
			*buffer = buf;
			buf = nullptr;
			owns_data = false;
		}
	}
	size_t remainingLength(void)
	{
		return len - offset;
	}
	void incrementOffset(ptrdiff_t off)
	{
		/*  we allow the offset to move to one location beyond end of buffer segment*/
		if(off > 0)
		{
			if(offset > (size_t)(SIZE_MAX - (size_t)off))
			{
				GRK_WARN("grk_buf8: overflow");
				offset = len;
			}
			else if(offset + (size_t)off > len)
			{
#ifdef DEBUG_SEG_BUF
				GRK_WARN("grk_buf8: attempt to increment buffer offset out of bounds");
#endif
				offset = len;
			}
			else
			{
				offset = offset + (size_t)off;
			}
		}
		else if(off < 0)
		{
			if(offset < (size_t)(-off))
			{
				GRK_WARN("grk_buf8: underflow");
				offset = 0;
			}
			else
			{
				offset = (size_t)((ptrdiff_t)offset + off);
			}
		}
	}
	T* currPtr(void) const
	{
		if(!buf)
			return nullptr;
		return buf + offset;
	}
	T* buf; /* internal array*/
	size_t offset; /* current offset into array */
	size_t len; /* length of array */
	bool owns_data; /* true if buffer manages the buf array */
};

using grk_buf8 = grk_buf<uint8_t, AllocatorVanilla>;
using grk_buf8_aligned = grk_buf<uint8_t, AllocatorAligned>;

template<typename T>
struct grk_buf2d_simple
{
	grk_buf2d_simple() : grk_buf2d_simple(nullptr, 0) {}
	grk_buf2d_simple(T* buf, uint32_t stride) : buf_(buf), stride_(stride) {}
	grk_buf2d_simple incX_IPL(size_t deltaX)
	{
		buf_ += deltaX;

		return *this;
	}
	grk_buf2d_simple incX(size_t deltaX)
	{
		return grk_buf2d_simple(buf_ + deltaX, stride_);
	}
	grk_buf2d_simple incY_IPL(size_t deltaY)
	{
		buf_ += deltaY * stride_;

		return *this;
	}
	grk_buf2d_simple incY(size_t deltaY)
	{
		return grk_buf2d_simple(buf_ + deltaY * stride_, stride_);
	}
	T* buf_;
	uint32_t stride_;
};

template<typename T, template<typename TT> typename A>
struct grk_buf2d : protected grk_buf<T, A>, public grk_rect32
{
	grk_buf2d(T* buffer, bool ownsData, uint32_t w, uint32_t strd, uint32_t h)
		: grk_buf<T, A>(buffer, ownsData), grk_rect32(0, 0, w, h), stride(strd)
	{}
	grk_buf2d(uint32_t w, uint32_t strd, uint32_t h) : grk_buf2d(nullptr, false, w, strd, h) {}
	grk_buf2d(uint32_t w, uint32_t h) : grk_buf2d(w, 0, h) {}
	explicit grk_buf2d(const grk_rect32* b)
		: grk_buf<T, A>(nullptr, false), grk_rect32(b->x0, b->y0, b->x1, b->y1), stride(0)
	{}
	grk_buf2d(void) : grk_buf2d(nullptr, 0, 0, 0, false) {}
	explicit grk_buf2d(const grk_buf2d& rhs)
		: grk_buf<T, A>(rhs), grk_rect32(rhs), stride(rhs.stride)
	{}
	grk_buf2d_simple<T> simple(void) const
	{
		return grk_buf2d_simple<T>(this->buf, this->stride);
	}
	grk_buf2d_simple<float> simpleF(void) const
	{
		return grk_buf2d_simple<float>((float*)this->buf, this->stride);
	}
	grk_buf2d& operator=(const grk_buf2d& rhs) // copy assignment
	{
		return operator=(&rhs);
	}
	grk_buf2d& operator=(const grk_buf2d* rhs) // copy assignment
	{
		if(this != rhs)
		{ // self-assignment check expected
			grk_buf<T, A>::operator=(rhs);
			grk_rect32::operator=(rhs);
			stride = rhs->stride;
		}
		return *this;
	}
	virtual ~grk_buf2d() = default;
	bool alloc2d(bool clear)
	{
		if(!this->buf && width() && height())
		{
			if(!stride)
				stride = grkMakeAlignedWidth(width());
			uint64_t data_size_needed = (uint64_t)stride * height() * sizeof(T);
			if(!data_size_needed)
				return true;
			if(!grk_buf<T, A>::alloc(data_size_needed))
			{
				grk::GRK_ERROR("Failed to allocate aligned memory buffer of dimensions %u x %u",
							   stride, height());
				return false;
			}
			if(clear)
				memset(this->buf, 0, data_size_needed);
		}

		return true;
	}
	// set buf to buf without owning it
	void attach(T* buffer, uint32_t strd)
	{
		grk_buf<T, A>::attach(buffer);
		stride = strd;
	}
	// set buf to buf and own it
	void acquire(T* buffer, uint32_t strd)
	{
		grk_buf<T, A>::acquire(buffer);
		stride = strd;
	}
	// transfer buf to buf, and cease owning it
	void transfer(T** buffer, uint32_t* strd)
	{
		if(buffer)
		{
			grk_buf<T, A>::transfer(buffer);
			*strd = stride;
		}
	}

	/** Returns whether window bounds are valid (non empty and within buffer bounds)
	 *
	 * @param win window.
	 * @return true or false.
	 */
	bool isWindowValid(grk_rect32 win)
	{
		return !(win.x0 >= x1 || win.x1 <= x0 || win.x1 > x1 || win.y0 >= y1 || win.y1 <= win.y0 ||
				 win.y1 > y1);
	}

	/** Read the contents of a rectangular window into a
	 * user buffer.
	 *
	 * @param window window to read from.
	 * @param dest user buffer to fill
	 * @param spacingX spacing (in elements, not in bytes) in x dimension between consecutive
	 * elements of the user buffer.
	 * @param spacingY spacing (in elements, not in bytes) in y dimension between
	 * consecutive elements of the user buffer.
	 */
	bool read(grk_rect32 window, int32_t* dest, const uint32_t spacingX,
			  const uint32_t spacingY)
	{
		GRK_UNUSED(dest);
		GRK_UNUSED(spacingX);
		GRK_UNUSED(spacingY);

		if(!isWindowValid(window))
			return false;

		auto inter = intersection(window);

		return true;
	}

	/** Write the contents of a rectangular window from a user buffer.
	 *
	 * @param window : window to write to buffer
	 * @param src user buffer to fill.
	 * @param spacingX spacing (in elements, not in bytes) in x dimension between consecutive
	 * elements of the user buffer.
	 * @param spacingY spacing (in elements, not in bytes) in y dimension between consecutive
	 * elements of the user buffer.
	 */
	bool write(grk_rect32 srcWin, const int32_t* src, const uint32_t spacingX,
			   const uint32_t spacingY)
	{
		if(!isWindowValid(srcWin))
			return false;

		assert(spacingY != 0 || srcWin.height() == 1);
		assert((spacingY <= 1 && spacingX >= 1) || (spacingY >=1 && spacingX == 1));

		auto inter = intersection(srcWin);

		auto srcOffX = inter.x0 < x0 ? x0 - inter.x0 : 0;
		auto srcOffY = inter.y0 < y0 ? y0 - inter.y0 : 0;
		src += srcOffY * spacingY + srcOffX * spacingX;

		auto destOffX = inter.x0 < x0 ? 0 : inter.x0 - x0;
		auto destOffY = inter.y0 < y0 ? 0 : inter.y0 - y0;
		auto dest = this->buf + destOffY * this->stride + destOffX;

		for (uint32_t y = inter.y0; y < inter.y1; y++){
			uint64_t srcInd = 0;
			for (uint32_t x = inter.x0; x < inter.x1; x++){
				dest[x] = src ? src[srcInd] : 0;
				srcInd += spacingX;
			}
			if(src)
				src += spacingY;
			dest += stride;
		}

		return true;
	}
	// rhs coordinates are in "this" coordinate system
	template<typename F>
	void copy(const grk_buf2d& rhs, F filter)
	{
		auto inter = intersection(rhs);
		if(inter.empty())
			return;

		T* dest = this->buf + (inter.y0 * stride + inter.x0);
		T* src = rhs.buf + ((inter.y0 - rhs.y0) * rhs.stride + inter.x0 - rhs.x0);
		uint32_t len = inter.width();
		for(uint32_t j = inter.y0; j < inter.y1; ++j)
		{
			filter.copy(dest, src, len);
			dest += stride;
			src += rhs.stride;
		}
	}
	T* getBuffer(void) const
	{
		return this->currPtr();
	}
	uint32_t stride;
};

} // namespace grk
