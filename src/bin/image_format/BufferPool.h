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
	GrkSerializeBuf get(uint64_t len);
	void put(GrkSerializeBuf b);

  private:
	std::map<uint8_t*, GrkSerializeBuf> pool;
};
