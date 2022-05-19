#pragma once

#include <cstdint>
#include <map>
#include <IFileIO.h>

class BufferPool
{
  public:
	BufferPool();
	virtual ~BufferPool();
	void init(uint64_t allocLen);
	GrkIOBuf get(uint64_t len);
	void put(GrkIOBuf b);

  private:
	std::map<uint8_t*, GrkIOBuf> pool;
};
