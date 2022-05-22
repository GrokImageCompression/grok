#pragma once
#include <cstdint>

#include "config.h"
#include "FileIO.h"

namespace iobench {

class FileIOWin32 : public FileIO
{
public:
	FileIOWin32(uint32_t threadId, bool flushOnClose);
	~FileIOWin32(void);
	bool open(std::string name, std::string mode, bool asynch);
	bool close(void);
	uint64_t write(uint64_t offset, IOBuf **buffers, uint32_t numBuffers) override;
	uint64_t write(uint8_t* buf, uint64_t size);
	uint64_t seek(int64_t off, int32_t whence);
private:

};

}
