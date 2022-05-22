#pragma once

#include "IFileIO.h"

namespace iobench {

class IBufferPool
{
  public:
	virtual ~IBufferPool() = default;
	virtual IOBuf* get(uint64_t len) = 0;
	virtual void put(IOBuf *b) = 0;
};

}
