#pragma once

#include <cstdint>
#include <string>

#include "FileIOUring.h"
#include "FileIOUnix.h"

namespace iobench {

class Serializer
{
public:
	Serializer(uint32_t threadId, bool flushOnClose);
	~Serializer(void);
	void setMaxPooledRequests(uint32_t maxRequests);
	void registerReclaimCallback(io_callback reclaim_callback, void* user_data);
	bool attach(Serializer *parent);
	bool open(std::string name, std::string mode, bool asynch);
	bool close(void);
	uint64_t write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers);
	uint64_t write(uint8_t* buf, uint64_t size);
	uint64_t seek(int64_t off, int32_t whence);
	IOBuf* getPoolBuffer(uint64_t len);
	IBufferPool* getPool(void);
	void enableSimulateWrite(void);
private:
	IBufferPool *pool_;
	FileIOUnix fileIO_;
	uint32_t threadId_;
};

}
