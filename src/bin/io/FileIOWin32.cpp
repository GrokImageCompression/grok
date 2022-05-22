#include <cstring>
#include <cassert>

#include "FileIOWin32.h"


namespace iobench {

FileIOWin32::FileIOWin32(uint32_t threadId, bool flushOnClose) : FileIO(threadId, flushOnClose)
{
}
FileIOWin32::~FileIOWin32(void){
	close();
}

bool FileIOWin32::open(std::string name, std::string mode, bool asynch)
{
	(void)name;
	(void)mode;
	(void)asynch;
	if (!close())
		return false;
	return true;
}
bool FileIOWin32::close(void)
{
	return true;
}
uint64_t FileIOWin32::seek(int64_t off, int32_t whence)
{
	(void)off;
	(void)whence;
	if (simulateWrite_)
		return off_;

	return 0;
}
uint64_t FileIOWin32::write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers){
	(void)offset;
	if (!buffers || !numBuffers)
		return 0;
	uint64_t bytesWritten = 0;
	for (uint32_t i = 0; i < numBuffers; ++i){
		auto b = buffers[i];
		assert(reclaim_callback_);
		reclaim_callback_(threadId_, b, reclaim_user_data_);
	}
	return bytesWritten;
}
uint64_t FileIOWin32::write(uint8_t* buf, uint64_t bytes_total)
{
	(void)buf;
	if (simulateWrite_){
		// offset 0 write is for file header
		if (off_ != 0) {
			if(++numSimulatedWrites_ == maxSimulatedWrites_)
				simulateWrite_ = false;
		}
		off_ += bytes_total;
		return bytes_total;
	}
	uint64_t bytes_written = 0;
	return bytes_written;
}

}
