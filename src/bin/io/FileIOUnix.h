#pragma once
#include <cstdint>

#include "config.h"
#include "FileIO.h"
#include "FileIOUring.h"
#include "BufferPool.h"

namespace iobench {

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
