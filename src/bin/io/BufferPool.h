/*
 *    Copyright (C) 2022 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>
#include <map>
#include <thread>
#include <mutex>

#include "IFileIO.h"
#include "IBufferPool.h"

namespace io
{

class BufferPool : public IBufferPool
{
  public:
	BufferPool() = default;
	virtual ~BufferPool()
	{
		for(std::pair<uint8_t*, IOBuf*> p : pool)
			RefReaper::unref(p.second);
	}
	IOBuf* get(uint64_t len) override
	{
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
	void put(IOBuf* b) override
	{
		assert(b->data_);
		assert(pool.find(b->data_) == pool.end());
		pool[b->data_] = b;
	}

  private:
	std::map<uint8_t*, IOBuf*> pool;
};

} // namespace io
