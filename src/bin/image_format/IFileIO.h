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
 */

#pragma once

#include <string>
#include "grok.h"
#include "MemManager.h"

typedef bool (*process_read_func)(void);


struct GrkSerializeBuf : public grk_serialize_buf {
public:
	GrkSerializeBuf() : GrkSerializeBuf(nullptr,0,0,0,false)
	{
	}
	GrkSerializeBuf(uint8_t *data,
					uint64_t offset,
					uint64_t dataLen,
					uint64_t allocLen,
					bool pooled)
	{
		this->data = data;
		this->offset = offset;
		this->dataLen = dataLen;
		this->allocLen = allocLen;
		this->pooled = pooled;
	}
	explicit GrkSerializeBuf(const grk_serialize_buf rhs){
		data = rhs.data;
		offset = rhs.offset;
		dataLen = rhs.dataLen;
		allocLen = rhs.allocLen;
		pooled = rhs.pooled;
	}
	bool alloc(uint64_t len){
		dealloc();
		data = (uint8_t*)grk::grkAlignedMalloc(len);
		if (data) {
			//printf("Allocated  %p\n", data);
			dataLen = len;
			allocLen = len;
		}

		return data != nullptr;;
	}
	void dealloc(){
		if (data) {
			grk::grkAlignedFree(data);
			//printf("Deallocated  %p\n", data);
		}
		data = nullptr;
	}
};

class IFileIO
{
  public:
	virtual ~IFileIO() = default;
	virtual bool open(std::string fileName, std::string mode) = 0;
	virtual bool close(void) = 0;
	virtual bool write(uint8_t* buf, uint64_t offset, size_t len, size_t maxLen,bool pooled) = 0;
	virtual bool write(GrkSerializeBuf buffer,
						grk_serialize_buf* reclaimed,
						uint32_t max_reclaimed,
						uint32_t *num_reclaimed) = 0;
	virtual bool read(uint8_t* buf, size_t len) = 0;
	virtual bool seek(int64_t pos) = 0;
};
