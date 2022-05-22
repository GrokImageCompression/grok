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

#include "config.h"
#include "FileIO.h"
#include "FileIOUring.h"
#include "BufferPool.h"

namespace io {

class FileIOUnix : public FileIO
{
public:
	FileIOUnix(uint32_t threadId, bool flushOnClose);
	~FileIOUnix(void);
	void registerReclaimCallback(io_callback reclaim_callback, void* user_data) override;
	bool attach(FileIOUnix* parent);
	bool open(std::string name, std::string mode, bool asynch);
	bool close(void) override;
	uint64_t write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers) override;
	uint64_t write(uint8_t* buf, uint64_t size);
	uint64_t seek(int64_t off, int32_t whence);
private:
#ifdef IOBENCH_HAVE_URING
	FileIOUring uring;
#endif
	int getMode(std::string mode);
	int fd_;
	bool ownsFileDescriptor_;
};

}
