#pragma once

#include <cstdint>
#include <map>
#include <thread>
#include <mutex>

#include "IFileIO.h"
#include "IBufferPool.h"

namespace iobench {

class BufferPool : public IBufferPool
{
  public:
	BufferPool() = default;
	virtual ~BufferPool(){
		for(std::pair<uint8_t*, IOBuf*> p : pool)
			RefReaper::unref(p.second);
	}
	IOBuf* get(uint64_t len) override{
		for(auto iter = pool.begin(); iter != pool.end(); ++iter)
		{
			if(iter->second->allocLen_ >= len)
			{
				auto b = iter->second;
				assert(b->data_);
				pool.erase(iter);
				assert(b->data_);
				return b;
			}
		}
		auto b = new IOBuf();
		b->alloc(len);
		assert(b->data_);
		return b;
	}
	void put(IOBuf *b) override{
		assert(b->data_);
		assert(pool.find(b->data_) == pool.end());
		pool[b->data_] = b;
	}
  private:
	std::map<uint8_t*, IOBuf*> pool;
};

}
