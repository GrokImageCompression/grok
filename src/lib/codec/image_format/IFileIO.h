/*
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
	  data_ = data;
	  offset_ = offset;
	  len_ = dataLen;
	  alloc_len_ = allocLen;
	  pooled_ = pooled;
   }
   explicit GrkIOBuf(const grk_io_buf rhs)
   {
	  data_ = rhs.data_;
	  offset_ = rhs.offset_;
	  len_ = rhs.len_;
	  alloc_len_ = rhs.alloc_len_;
	  pooled_ = rhs.pooled_;
   }
   bool alloc(size_t len)
   {
	  dealloc();
	  data_ = (uint8_t*)grk_bin::grk_aligned_malloc(len);
	  if(data_)
	  {
		 // printf("Allocated  %p\n", data);
		 len_ = len;
		 alloc_len_ = len;
	  }

	  return data_ != nullptr;
   }
   void dealloc()
   {
	  if(data_)
	  {
		 grk_bin::grk_aligned_free(data_);
		 // printf("Deallocated  %p\n", data);
	  }
	  data_ = nullptr;
   }
};

class IFileIO
{
 public:
   virtual ~IFileIO() = default;
   virtual bool open(const std::string& fileName, const std::string& mode) = 0;
   virtual bool close(void) = 0;
   virtual uint64_t write(uint8_t* buf, uint64_t offset, size_t len, size_t maxLen,
						  bool pooled) = 0;
   virtual uint64_t write(GrkIOBuf buffer) = 0;
   virtual bool read(uint8_t* buf, size_t len) = 0;
   virtual uint64_t seek(int64_t off, int whence) = 0;
};
