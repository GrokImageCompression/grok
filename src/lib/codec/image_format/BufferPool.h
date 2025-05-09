#pragma once

#include <cstdint>
#include <map>
#include <IFileIO.h>

class BufferPool
{
public:
  BufferPool();
  virtual ~BufferPool();
  GrkIOBuf get(size_t len);
  void put(GrkIOBuf b);

private:
  std::map<uint8_t*, GrkIOBuf> pool;
};
