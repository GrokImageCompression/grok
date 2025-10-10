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

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cassert>

#include "grk_config_private.h"
#include <string>
#include "grok.h"

#ifdef _WIN32
#include <malloc.h>
#endif

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

namespace grk_bin
{
const size_t grkBufferAlignment = 64;

static inline void* grkAlignedAllocN(size_t alignment, size_t size)
{
  /* alignment shall be power of 2 */
  assert((alignment != 0U) && ((alignment & (alignment - 1U)) == 0U));
  /* alignment shall be at least sizeof(void*) */
  assert(alignment >= sizeof(void*));

  if(size == 0U) /* prevent implementation defined behavior */
    return nullptr;

  // make new_size a multiple of alignment
  size = ((size + alignment - 1) / alignment) * alignment;

#ifdef _WIN32
  return _aligned_malloc(size, alignment);
#else
  return std::aligned_alloc(alignment, size);
#endif
}
static void* grk_aligned_malloc(size_t size)
{
  return grkAlignedAllocN(grkBufferAlignment, size);
}
static void grk_aligned_free(void* ptr)
{
#ifdef _WIN32
  _aligned_free(ptr);
#else
  free(ptr);
#endif
}
} // namespace grk_bin

struct GrkIOBuf : public grk_io_buf
{
public:
  GrkIOBuf() : GrkIOBuf(nullptr, 0, 0, 0, false) {}
  GrkIOBuf(uint8_t* data, size_t offset, size_t dataLen, size_t allocLen, bool pooled)
  {
    this->data = data;
    this->offset = offset;
    this->len = dataLen;
    this->alloc_len = allocLen;
    this->pooled = pooled;
  }
  explicit GrkIOBuf(const grk_io_buf rhs)
  {
    data = rhs.data;
    offset = rhs.offset;
    len = rhs.len;
    alloc_len = rhs.alloc_len;
    pooled = rhs.pooled;
  }
  bool alloc(size_t dataLen)
  {
    dealloc();
    data = (uint8_t*)grk_bin::grk_aligned_malloc(dataLen);
    if(data)
    {
      // printf("Allocated  %p\n", data);
      len = dataLen;
      alloc_len = dataLen;
    }

    return data != nullptr;
  }
  void dealloc()
  {
    if(data)
    {
      grk_bin::grk_aligned_free(data);
      // printf("Deallocated  %p\n", data);
    }
    data = nullptr;
  }
};

class IFileIO
{
public:
  virtual ~IFileIO() = default;
  virtual bool open(const std::string& fileName, const std::string& mode) = 0;
  virtual bool close(void) = 0;
  virtual uint64_t write(uint8_t* buf, uint64_t offset, size_t len, size_t maxLen, bool pooled) = 0;
  virtual uint64_t write(GrkIOBuf buffer) = 0;
  virtual bool read(uint8_t* buf, size_t len) = 0;
  virtual uint64_t seek(int64_t off, int whence) = 0;
};
