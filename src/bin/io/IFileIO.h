/*
 *    Copyright (C) 2022 Grok Image Compression Inc.
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
 */

#pragma once

#include <string>
#include <cstdlib>
#include <cstring>
#include <cassert>
#ifdef _WIN32
#include <malloc.h>
#endif

#include "RefCounted.h"

namespace io
{

#define K 1024
#define ALIGNMENT (512)
#define WRTSIZE (32 * K)

const int32_t invalid_fd = -1;

typedef struct _io_buf
{
	uint32_t index_;
	uint64_t skip_;
	uint64_t offset_;
	uint8_t* data_;
	uint64_t len_;
	uint64_t allocLen_;
} io_buf;

typedef bool (*io_callback)(uint32_t threadId, io_buf* buffer, void* io_user_data);
typedef void (*io_register_client_callback)(io_callback reclaim_callback, void* io_user_data,
											void* reclaim_user_data);

struct IOBuf : public io_buf, public RefCounted
{
  public:
	IOBuf()
	{
		index_ = 0;
		skip_ = 0;
		offset_ = 0;
		data_ = 0;
		len_ = 0;
		allocLen_ = 0;
	}

  private:
	~IOBuf()
	{
		dealloc();
	}

  public:
	static bool isAlignedToWriteSize(uint64_t off)
	{
		return (off & (WRTSIZE - 1)) == 0;
	}
	bool aligned(void)
	{
		return alignedOffset() && alignedLength();
	}
	bool alignedOffset(void)
	{
		return isAlignedToWriteSize(offset_);
	}
	bool alignedLength(void)
	{
		return isAlignedToWriteSize(len_);
	}
	static uint8_t* alignedAlloc(size_t alignment, size_t length)
	{
#ifdef _WIN32
		return (uint8_t*)_aligned_malloc(length, alignment);
#else
		return (uint8_t*)std::aligned_alloc(alignment, length);
#endif
	}
	bool alloc(uint64_t len)
	{
		if(len < allocLen_)
			return true;

		if(data_)
			dealloc();
		data_ = alignedAlloc(ALIGNMENT, len);
		if(data_)
		{
			len_ = len;
			allocLen_ = len;
		}
		assert(data_);

		return data_ != nullptr;
	}
	void updateLen(uint64_t len)
	{
		assert(len <= allocLen_);
		if(data_ && len <= allocLen_)
			len_ = len;
	}
	void dealloc()
	{
		free(data_);
		data_ = nullptr;
		len_ = 0;
		allocLen_ = 0;
	}
};

// mirror of iovec struct
struct io
{
	void* iov_base;
	size_t iov_len;
};

struct IOScheduleData
{
	IOScheduleData(uint64_t offset, IOBuf** buffers, uint32_t numBuffers)
		: offset_(offset), numBuffers_(numBuffers), buffers_(nullptr), iov_(new io[numBuffers_]),
		  totalBytes_(0)
	{
		assert(numBuffers);
		buffers_ = new IOBuf*[numBuffers];
		for(uint32_t i = 0; i < numBuffers_; ++i)
		{
			buffers_[i] = buffers[i];
			auto b = buffers_[i];
			auto v = iov_ + i;
			v->iov_base = b->data_;
			v->iov_len = b->len_;
			totalBytes_ += b->len_;
		}
	}
	~IOScheduleData()
	{
		delete[] buffers_;
		delete[] iov_;
	}
	uint64_t offset_;
	uint32_t numBuffers_;
	IOBuf** buffers_;
	io* iov_;
	uint64_t totalBytes_;
};

class IFileIO
{
  public:
	virtual ~IFileIO() = default;
	virtual bool close(void) = 0;
	virtual uint64_t write(uint64_t offset, IOBuf** buffers, uint32_t numBuffers) = 0;
};

} // namespace io
