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
 *
 */

#pragma once

#include <string>
#include "grok.h"
#include "MemManager.h"

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
  bool alloc(size_t len)
  {
    dealloc();
    data = (uint8_t*)grk_bin::grk_aligned_malloc(len);
    if(data)
    {
      // printf("Allocated  %p\n", data);
      this->len = len;
      alloc_len = len;
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
