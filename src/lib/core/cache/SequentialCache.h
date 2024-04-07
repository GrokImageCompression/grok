/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
// dynamic array of pointers of type T
// hybrid design : combination of std::vector and simple array
template<typename T>
class SequentialPtrCache
{
 public:
   SequentialPtrCache(void) : SequentialPtrCache(kSequentialChunkSize) {}
   SequentialPtrCache(uint64_t maxChunkSize)
	   : currChunk_(nullptr), chunkSize_(std::min<uint64_t>(maxChunkSize, kSequentialChunkSize)),
		 index_(0)
   {}
   virtual ~SequentialPtrCache(void)
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
   // get next item
   T* get()
   {
	  uint64_t itemIndex = index_ % chunkSize_;
	  uint64_t chunkIndex = index_ / chunkSize_;
	  bool isInitialized = (currChunk_ != nullptr);
	  bool isLastChunk = (chunkIndex == chunks.size() - 1);
	  bool isEndOfChunk = (itemIndex == chunkSize_ - 1);
	  bool createNewChunk = !isInitialized || (isLastChunk && isEndOfChunk);
	  itemIndex++;
	  if(createNewChunk || isEndOfChunk)
	  {
		 itemIndex = 0;
		 chunkIndex++;
		 if(createNewChunk)
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
	  // create new item if null
	  if(!item)
	  {
		 item = create();
		 currChunk_[itemIndex] = item;
	  }
	  if(isInitialized)
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
   T** currChunk_;
   uint64_t chunkSize_;
   uint64_t index_;
   static constexpr uint64_t kSequentialChunkSize = 1024;
};

} // namespace grk
