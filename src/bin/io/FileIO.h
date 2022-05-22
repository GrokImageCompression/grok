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

#include "config.h"
#include "IFileIO.h"

namespace iobench {

class FileIO : public IFileIO {
public:
	FileIO(uint32_t threadId, bool flushOnClose);
	virtual ~FileIO() = default;
	void enableSimulateWrite(void);
	void setMaxPooledRequests(uint32_t maxRequests);
	virtual void registerReclaimCallback(io_callback reclaim_callback, void* user_data);
protected:
	uint32_t numSimulatedWrites_;
	uint32_t maxSimulatedWrites_;
	uint64_t off_;
	io_callback reclaim_callback_;
	void* reclaim_user_data_;
	std::string filename_;
	std::string mode_;
	bool simulateWrite_;
	bool flushOnClose_;
	uint32_t threadId_;
};

}

