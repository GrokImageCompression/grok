/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include "geometry.h"
#include "MemManager.h"
#include "WaveletCommon.h"

namespace grk
{

template<typename T>
struct AllocatorVanilla
{
  T* alloc(size_t elements)
  {
    return new T[elements];
  }
  void dealloc(T* buf)
  {
    delete[] buf;
  }
};
template<typename T>
struct AllocatorAligned
{
  T* alloc(size_t elements)
  {
    return (T*)grk_aligned_malloc(elements * sizeof(T));
  }
  void dealloc(T* buf)
  {
    grk_aligned_free(buf);
  }
};
template<typename T, template<typename TT> typename A>
struct Buffer : A<T>
{
  Buffer(T* buffer, size_t off, size_t length, bool ownsData)
      : buf_(buffer), offset_(off), num_elts_(length), owns_data_(ownsData)
  {}
  Buffer(T* buffer, size_t length) : Buffer(buffer, 0, length, false) {}
  Buffer() : Buffer(0, 0, 0, false) {}
  Buffer(T* buffer, size_t length, bool ownsData) : Buffer(buffer, 0, length, ownsData) {}
  virtual ~Buffer()
  {
    // destructor simply deallocates memory
    if(owns_data_)
      Buffer<T, A>::dealloc();
  }
  explicit Buffer(const Buffer& rhs)
  {
    this->operator=(rhs);
  }
  Buffer& operator=(const Buffer& rhs) // copy assignment
  {
    return operator=(&rhs);
  }
  Buffer& operator=(const Buffer* rhs) // copy assignment
  {
    if(this != rhs)
    { // self-assignment check expected
      buf_ = rhs->buf_;
      offset_ = rhs->offset();
      num_elts_ = rhs->num_elts();
      owns_data_ = false;
    }
    return *this;
  }
  inline bool canRead(void)
  {
    return offset_ < num_elts_;
  }
  inline T read(void)
  {
    return buf_[offset_++];
  }
  inline bool write(T val)
  {
    if(offset_ == num_elts_)
      return false;
    buf_[offset_++] = val;

    return true;
  }
  inline bool write(T* b, size_t size)
  {
    if(offset_ + size > num_elts_)
      return false;
    memcpy(buf_ + offset_, b, size);
    offset_ += size;

    return true;
  }
  virtual bool alloc(size_t length)
  {
    if(buf_ && num_elts_ > length)
      return true;
    dealloc();
    buf_ = A<T>::alloc(length);
    if(!buf_)
      return false;
    num_elts_ = length;
    offset_ = 0;
    owns_data_ = true;

    return true;
  }
  virtual void dealloc()
  {
    if(owns_data_)
      A<T>::dealloc(buf_);
    buf_ = nullptr;
    owns_data_ = false;
    offset_ = 0;
    num_elts_ = 0;
  }
  // set buf to buf without owning it
  void attach(T* buffer)
  {
    Buffer<T, A>::dealloc();
    buf_ = buffer;
  }
  // transfer buf to buffer, and cease owning it
  void transfer(T** buffer)
  {
    if(buffer)
    {
      assert(!buf_ || owns_data_);
      *buffer = buf_;
      buf_ = nullptr;
      owns_data_ = false;
      num_elts_ = 0;
      offset_ = 0;
    }
  }
  size_t remainingLength(void) const
  {
    return num_elts_ - offset_;
  }
  bool increment_offset(std::ptrdiff_t off)
  {
    /*  we allow the offset to move to one location beyond end of buffer segment*/
    if(off > 0)
    {
      if(offset_ > (size_t)(SIZE_MAX - (size_t)off))
      {
        grklog.warn("Buffer8: overflow");
        offset_ = num_elts_;
      }
      else if(offset_ + (size_t)off > num_elts_)
      {
#ifdef DEBUG_SEG_BUF
        grklog.warn("Buffer8: attempt to increment buffer offset out of bounds");
#endif
        offset_ = num_elts_;
        return false;
      }
      else
      {
        offset_ = offset_ + (size_t)off;
      }
    }
    else if(off < 0)
    {
      if(offset_ < (size_t)(-off))
      {
        grklog.warn("Buffer8: underflow");
        offset_ = 0;
        return false;
      }
      else
      {
        offset_ = (size_t)((std::ptrdiff_t)offset_ + off);
      }
    }

    return true;
  }
  T* currPtr(void) const
  {
    if(!buf_)
      return nullptr;
    return buf_ + offset_;
  }
  T* currPtr([[maybe_unused]] size_t required_length) const
  {
    return currPtr();
  }
  bool owns_data()
  {
    return owns_data_;
  }
  void set_owns_data(bool owns)
  {
    owns_data_ = owns;
  }
  size_t num_elts() const
  {
    return num_elts_;
  }
  size_t* num_elts_ptr()
  {
    return &num_elts_;
  }
  void set_num_elts(size_t elts)
  {
    assert(elts <= num_elts_);
    num_elts_ = elts;
  }

  size_t offset() const
  {
    return offset_;
  }
  void set_offset(size_t off)
  {
    offset_ = off;
  }
  T* buf() const
  {
    return buf_;
  }
  T** ptr_to_buf()
  {
    return &buf_;
  }
  void set_buf(T* buf, size_t elts)
  {
    assert(!buf_);
    buf_ = buf;
    num_elts_ = elts;
  }

protected:
  T* buf_; /* internal array*/
  size_t offset_; /* current offset into array */
  size_t num_elts_; /* number of elements in array */
  bool owns_data_; /* true if buffer manages the buf array */
};

using Buffer8 = Buffer<uint8_t, AllocatorVanilla>;
using BufferAligned8 = Buffer<uint8_t, AllocatorAligned>;

template<typename T>
struct Buffer2dSimple
{
  Buffer2dSimple() : Buffer2dSimple(nullptr, 0, 0) {}
  Buffer2dSimple(T* buf, uint32_t stride, uint32_t height)
      : buf_(buf), stride_(stride), height_(height)
  {
    assert(buf || !stride);
  }
  Buffer2dSimple& incX_IN_PLACE(size_t deltaX)
  {
    buf_ += deltaX;

    return *this;
  }
  Buffer2dSimple& incY_IN_PLACE(size_t deltaY)
  {
    buf_ += deltaY * stride_;

    return *this;
  }
  T* buf_;
  uint32_t stride_;
  uint32_t height_;
};

template<typename T, template<typename TT> typename A>
struct Buffer2d : protected Buffer<T, A>, public Rect32
{
  Buffer2d(T* buffer, bool ownsData, uint32_t w, uint32_t strd, uint32_t h)
      : Buffer<T, A>(buffer, ownsData), Rect32(0, 0, w, h), stride(strd)
  {
    assert(buffer || !strd);
  }
  Buffer2d(uint32_t w, uint32_t h) : Buffer2d(nullptr, false, w, 0, h) {}
  explicit Buffer2d(const Rect32* b)
      : Buffer<T, A>(nullptr, false), Rect32(b->x0, b->y0, b->x1, b->y1), stride(0)
  {}
  explicit Buffer2d(const Rect32& b)
      : Buffer<T, A>(nullptr, false), Rect32(b.x0, b.y0, b.x1, b.y1), stride(0)
  {}
  Buffer2d(const Rect32& b, [[maybe_unused]] bool useOrigin)
      : Buffer<T, A>(nullptr, false), Rect32(b), stride(0)
  {}
  Buffer2d(void) : Buffer2d(nullptr, 0, 0, 0, false) {}
  explicit Buffer2d(const Buffer2d& rhs)
      : Buffer<T, A>(nullptr, 0, 0, false), Rect32(rhs), stride(rhs.stride)
  {
    if(rhs.buf_)
    {
      // Calculate the required buffer size based on stride and height
      uint64_t eltsNeeded = (uint64_t)rhs.stride * rhs.height();

      // Allocate memory using the allocator
      if(!Buffer<T, A>::alloc(eltsNeeded))
      {
        throw std::runtime_error("Failed to allocate memory for Buffer2d copy constructor.");
      }

      // Copy data from the source buffer
      const T* srcPtr = rhs.buf_;
      T* dstPtr = this->buf_;
      for(uint32_t j = 0; j < rhs.height(); ++j)
      {
        memcpy(dstPtr, srcPtr, rhs.width() * sizeof(T));
        srcPtr += rhs.stride; // Move source pointer to the next row
        dstPtr += rhs.stride; // Move destination pointer to the next row
      }

      this->set_owns_data(true); // This buffer owns the allocated data
    }
    else
    {
      // Handle case when rhs.buf is null
      this->stride = 0;
      this->buf_ = nullptr;
      this->set_owns_data(false);
    }
  }
  /**
   * @brief Construct a new Buffer2d object
   * Copies Rect32 and transfers buffer
   *
   * @param rhs right hand side
   * @param transferBuffer must always be true
   */
  Buffer2d(Buffer2d& rhs, bool transferBuffer) : Buffer<T, A>(nullptr, 0, 0, false), Rect32(rhs)
  {
    if(transferBuffer && rhs.buf_)
    {
      rhs.transfer(&this->buf_, &this->stride);
    }
    else
    {
      throw std::invalid_argument(
          "Buffer transfer failed: Source buffer is empty or transfer not requested.");
    }
  }

  Buffer2dSimple<T> simple(void) const
  {
    return Buffer2dSimple<T>(this->buf_, this->stride, this->height());
  }
  Buffer2dSimple<float> simpleF(void) const
  {
    return Buffer2dSimple<float>((float*)this->buf_, this->stride, this->height());
  }
  Buffer2d& operator=(const Buffer2d& rhs) // copy assignment
  {
    return operator=(&rhs);
  }
  Buffer2d& operator=(const Buffer2d* rhs) // copy assignment
  {
    if(this != rhs)
    { // self-assignment check expected
      Buffer<T, A>::operator=(rhs);
      Rect32::operator=(rhs);
      if(rhs->buf_)
        stride = rhs->stride;
    }
    return *this;
  }
  virtual ~Buffer2d() = default;
  size_t length(void)
  {
    return this->num_elts();
  }
  bool alloc2d(uint32_t w, uint32_t str, uint32_t h, bool clear)
  {
    setRect(Rect32(0, 0, w, h));
    stride = str;
    bool rc = alloc2d(clear);
    if(rc)
      stride = str;

    return rc;
  }
  void clear(void)
  {
    uint64_t bytesNeeded = (uint64_t)stride * height() * sizeof(T);
    memset(this->buf_, 0, bytesNeeded);
  }
  bool alloc2d(bool clear)
  {
    if(!height() || !width())
      return true;
    uint32_t newStride = stride ? stride : alignedBufferWidth(width());
    uint64_t eltsNeeded = (uint64_t)newStride * height();
    // avoid reallocation
    if(!eltsNeeded || eltsNeeded <= this->num_elts())
      return true;

    if(!Buffer<T, A>::alloc(eltsNeeded))
    {
      grk::grklog.error("Failed to allocate aligned memory buffer of dimensions %u x %u", newStride,
                        height());
      return false;
    }
    stride = newStride;
    if(clear)
      this->clear();

    return true;
  }
  void dealloc() override
  {
    Buffer<T, A>::dealloc();
    stride = 0;
  }
  // set buf to buffer without owning it
  void attach(T* buffer, uint32_t strd)
  {
    Buffer<T, A>::attach(buffer);
    if(buffer)
      stride = strd;
  }
  void attach(Buffer2d& rhs, uint32_t x, uint32_t y)
  {
    attach(&rhs, x, y);
  }
  void attach(Buffer2d& rhs)
  {
    attach(&rhs, 0, 0);
  }
  void attach(Buffer2d* rhs, uint32_t x, uint32_t y)
  {
    if(!rhs)
      return;
    Buffer<T, A>::dealloc();
    this->buf_ = rhs->address(x, y);
    this->num_elts_ = rhs->num_elts();
    this->owns_data_ = false;
    if(this->buf_)
      stride = rhs->stride;
  }
  void attach(Buffer2d* rhs)
  {
    attach(rhs, 0, 0);
  }
  // transfer buf to buffer, and cease owning it
  void transfer(T** buffer, uint32_t* strd)
  {
    if(buffer)
    {
      Buffer<T, A>::transfer(buffer);
      *strd = stride;
      stride = 0;
    }
  }

  // rhs coordinates are in "this" coordinate system
  template<typename F>
  void copyFrom(const Buffer2d& src, F filter)
  {
    return copyFrom(&src, filter);
  }
  // rhs coordinates are in "this" coordinate system
  template<typename F>
  void copyFrom(const Buffer2d* src, F filter)
  {
    auto inter = intersection(src);
    if(inter.empty())
      return;

    if(!src->buf_)
      return;

    T* ptr = this->buf_ + (inter.y0 * stride + inter.x0);
    T* srcPtr = src->buf_ + ((inter.y0 - src->y0) * src->stride + inter.x0 - src->x0);
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
    void copy(T* dst, const T* src, uint32_t len)
    {
      memcpy(dst, src, len);
    }
  };
  void copyFrom(const Buffer2d& src)
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
  uint32_t getStride()
  {
    assert(this->buf() || !stride);
    return stride;
  }

protected:
  uint32_t stride;
};

using Buffer2dAligned32 = Buffer2d<int32_t, AllocatorAligned>;

} // namespace grk
