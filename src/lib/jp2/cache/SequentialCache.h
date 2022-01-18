/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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
#include "grk_includes.h"

#include <vector>

namespace grk
{
template<typename T>
class SequentialCache
{
  public:
	SequentialCache(void) : SequentialCache(kSequentialChunkSize) {}
	SequentialCache(uint64_t maxChunkSize)
		: chunkSize_(std::min<uint64_t>(maxChunkSize, kSequentialChunkSize)), currChunk_(nullptr),
		  index_(0)
	{}
	virtual ~SequentialCache(void)
	{
		for(auto& ch : chunks)
		{
			for(size_t i = 0; i < chunkSize_; ++i)
				delete ch[i];
			delete[] ch;
		}
	}
	void rewind(void)
	{
		if(chunks.empty())
			return;
		index_ = 0;
		currChunk_ = chunks[0];
	}
	T* get()
	{
		uint64_t itemIndex = index_ % chunkSize_;
		uint64_t chunkIndex = index_ / chunkSize_;
		bool initialized = (currChunk_ != nullptr);
		bool lastChunk = (chunkIndex == chunks.size() - 1);
		bool endOfChunk = (itemIndex == chunkSize_ - 1);
		bool createNew = !initialized || (lastChunk && endOfChunk);
		itemIndex++;
		if(createNew || endOfChunk)
		{
			itemIndex = 0;
			chunkIndex++;
			if(createNew)
			{
				currChunk_ = new T*[chunkSize_];
				memset(currChunk_, 0, chunkSize_ * sizeof(T*));
				chunks.push_back(currChunk_);
			}
			else
			{
				currChunk_ = chunks[chunkIndex];
			}
		}
		auto item = currChunk_[itemIndex];
		if(!item)
		{
			item = create();
			currChunk_[itemIndex] = item;
		}
		if(initialized)
			index_++;
		return item;
	}

  protected:
	virtual T* create(void)
	{
		auto item = new T();
		return item;
	}

  private:
	std::vector<T**> chunks;
	uint64_t chunkSize_;
	T** currChunk_;
	uint64_t index_;
	static constexpr uint64_t kSequentialChunkSize = 1024;
};

} // namespace grk
