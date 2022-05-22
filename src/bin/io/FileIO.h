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

