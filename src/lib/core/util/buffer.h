/**
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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

namespace grk
{

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
	  return (T*)grk_aligned_malloc(length * sizeof(T));
   }
   void dealloc(T* buf)
   {
	  grk_aligned_free(buf);
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
			Logger::logger_.warn("grk_buf8: overflow");
			offset = len;
		 }
		 else if(offset + (size_t)off > len)
		 {
#ifdef DEBUG_SEG_BUF
			Logger::logger_.warn("grk_buf8: attempt to increment buffer offset out of bounds");
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
			Logger::logger_.warn("grk_buf8: underflow");
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
   grk_buf2d_simple() : grk_buf2d_simple(nullptr, 0, 0) {}
   grk_buf2d_simple(T* buf, uint32_t stride, uint32_t height)
	   : buf_(buf), stride_(stride), height_(height)
   {}
   grk_buf2d_simple& incX_IN_PLACE(size_t deltaX)
   {
	  buf_ += deltaX;

	  return *this;
   }
   grk_buf2d_simple& incY_IN_PLACE(size_t deltaY)
   {
	  buf_ += deltaY * stride_;

	  return *this;
   }
   T* buf_;
   uint32_t stride_;
   uint32_t height_;
};

template<typename T, template<typename TT> typename A>
struct grk_buf2d : protected grk_buf<T, A>, public grk_rect32
{
   grk_buf2d(T* buffer, bool ownsData, uint32_t w, uint32_t strd, uint32_t h)
	   : grk_buf<T, A>(buffer, ownsData), grk_rect32(0, 0, w, h), stride(strd)
   {}
   grk_buf2d(uint32_t w, uint32_t h) : grk_buf2d(nullptr, false, w, 0, h) {}
   explicit grk_buf2d(const grk_rect32* b)
	   : grk_buf<T, A>(nullptr, false), grk_rect32(b->x0, b->y0, b->x1, b->y1), stride(0)
   {}
   explicit grk_buf2d(const grk_rect32& b)
	   : grk_buf<T, A>(nullptr, false), grk_rect32(b.x0, b.y0, b.x1, b.y1), stride(0)
   {}
   grk_buf2d(const grk_rect32& b, [[maybe_unused]] bool useOrigin)
	   : grk_buf<T, A>(nullptr, false), grk_rect32(b), stride(0)
   {}
   grk_buf2d(void) : grk_buf2d(nullptr, 0, 0, 0, false) {}
   explicit grk_buf2d(const grk_buf2d& rhs)
	   : grk_buf<T, A>(rhs), grk_rect32(rhs), stride(rhs.stride)
   {}
   grk_buf2d_simple<T> simple(void) const
   {
	  return grk_buf2d_simple<T>(this->buf, this->stride, this->height());
   }
   grk_buf2d_simple<float> simpleF(void) const
   {
	  return grk_buf2d_simple<float>((float*)this->buf, this->stride, this->height());
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
			stride = grk_make_aligned_width(width());
		 uint64_t data_size_needed = (uint64_t)stride * height() * sizeof(T);
		 if(!data_size_needed)
			return true;
		 if(!grk_buf<T, A>::alloc(data_size_needed))
		 {
			grk::Logger::logger_.error(
				"Failed to allocate aligned memory buffer of dimensions %u x %u", stride, height());
			return false;
		 }
		 if(clear)
			memset(this->buf, 0, data_size_needed);
	  }

	  return true;
   }
   // set buf to buffer without owning it
   void attach(T* buffer, uint32_t strd)
   {
	  grk_buf<T, A>::attach(buffer);
	  stride = strd;
   }
   void attach(grk_buf2d& rhs, uint32_t x, uint32_t y)
   {
	  attach(&rhs, x, y);
   }
   void attach(grk_buf2d& rhs)
   {
	  attach(&rhs, 0, 0);
   }
   void attach(grk_buf2d* rhs, uint32_t x, uint32_t y)
   {
	  if(!rhs)
		 return;
	  grk_buf<T, A>::dealloc();
	  this->buf = rhs->address(x, y);
	  this->len = rhs->len;
	  this->owns_data = false;
	  stride = rhs->stride;
   }
   void attach(grk_buf2d* rhs)
   {
	  attach(rhs, 0, 0);
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
   // rhs coordinates are in "this" coordinate system
   template<typename F>
   void copyFrom(const grk_buf2d& src, F filter)
   {
	  return copyFrom(&src, filter);
   }
   // rhs coordinates are in "this" coordinate system
   template<typename F>
   void copyFrom(const grk_buf2d* src, F filter)
   {
	  auto inter = intersection(src);
	  if(inter.empty())
		 return;

	  if(!src->buf)
		 return;

	  T* ptr = this->buf + (inter.y0 * stride + inter.x0);
	  T* srcPtr = src->buf + ((inter.y0 - src->y0) * src->stride + inter.x0 - src->x0);
	  uint32_t len = inter.width();
	  for(uint32_t j = inter.y0; j < inter.y1; ++j)
	  {
		 filter.copy(ptr, srcPtr, len);
		 ptr += stride;
		 srcPtr += src->stride;
	  }
   }
   struct memcpy_from
   {
	  void copy(T* dst, T* src, uint32_t len)
	  {
		 memcpy(dst, src, len);
	  }
   };
   void copyFrom(const grk_buf2d& src)
   {
	  copy(src, memcpy_from());
   }
   T* getBuffer(void) const
   {
	  return this->currPtr();
   }
   T* address(uint32_t x, uint32_t y)
   {
	  return this->currPtr() + (uint64_t)x + (uint64_t)y * stride;
   }
   uint32_t stride;
};

} // namespace grk
