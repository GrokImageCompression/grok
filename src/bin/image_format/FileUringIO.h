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

#include "grk_apps_config.h"
#ifdef GROK_HAVE_URING

#include "IFileIO.h"
#include <liburing.h>
#include <liburing/io_uring.h>

struct io_data
{
	io_data() : reclaimable(false),
				offset(0),
				iov{0, 0}
	{}
	bool reclaimable;
	uint64_t offset;
	iovec iov;
};

class FileUringIO : public IFileIO
{
  public:
	FileUringIO();
	virtual ~FileUringIO() override;
	bool open(std::string fileName, std::string mode) override;
	bool attach(std::string fileName, std::string mode, int fd);
	bool close(void) override;
	bool write(uint8_t* buf, uint64_t offset, bool reclaimable, size_t len) override;
	bool read(uint8_t* buf, size_t len) override;
	bool seek(int64_t pos) override;
	io_data* retrieveCompletion(bool peek);
  private:
	io_uring ring;
	int m_fd;
	bool ownsDescriptor;
	std::string m_fileName;
	size_t m_queueCount;
	int getMode(const char* mode);
	void enqueue(io_uring* ring, io_data* data, bool readop, int fd);
	bool initQueue(void);

	const uint32_t QD = 1024;
	const uint32_t  BS =  (32 * 1024);
};

#endif
