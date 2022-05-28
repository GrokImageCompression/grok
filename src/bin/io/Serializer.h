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

#include <cstdint>
#include <string>

#include "FileIOUring.h"
#include "FileIOUnix.h"

namespace io
{

class Serializer
{
  public:
	Serializer(uint32_t threadId, bool flushOnClose);
	~Serializer(void);
	void setMaxSimulatedWrites(uint64_t maxRequests);
	void registerReclaimCallback(io_callback reclaim_callback, void* user_data);
	bool attach(Serializer* parent);
	bool open(std::string name, std::string mode, bool asynch);
	bool close(void);
	uint64_t write(uint64_t offset, IOBuf** buffers, uint32_t numBuffers);
	uint64_t write(uint8_t* buf, uint64_t size);
	uint64_t seek(int64_t off, int32_t whence);
	IOBuf* getPoolBuffer(uint64_t len);
	IBufferPool* getPool(void);
	void enableSimulateWrite(void);

  private:
	IBufferPool* pool_;
	FileIOUnix fileIO_;
	uint32_t threadId_;
};

} // namespace io
