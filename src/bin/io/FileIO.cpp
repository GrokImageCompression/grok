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

#include "FileIO.h"

namespace io
{

FileIO::FileIO(uint32_t threadId, bool flushOnClose)
	: numSimulatedWrites_(0), maxSimulatedWrites_(0), off_(0), reclaim_callback_(nullptr),
	  reclaim_user_data_(nullptr), simulateWrite_(false), flushOnClose_(flushOnClose),
	  threadId_(threadId)
{}
void FileIO::setMaxSimulatedWrites(uint64_t maxWrites)
{
	maxSimulatedWrites_ = maxWrites;
}
void FileIO::registerReclaimCallback(io_callback reclaim_callback, void* user_data)
{
	reclaim_callback_ = reclaim_callback;
	reclaim_user_data_ = user_data;
}

void FileIO::enableSimulateWrite(void)
{
	simulateWrite_ = true;
}

bool FileIO::isDirect(std::string mode)
{
	return (mode.length() > 1 && mode[1] == 'd');
}

uint64_t FileIO::bytesToWrite(IOBuf** buffers, uint32_t numBuffers, std::string mode)
{
	bool direct = isDirect(mode);
	uint64_t toWrite = 0;
	for(uint32_t i = 0; i < numBuffers; ++i)
		toWrite += direct ? buffers[i]->allocLen_ : buffers[i]->len_;

	return toWrite;
}

} // namespace io
