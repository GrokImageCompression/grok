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

#include <map>

namespace grk
{
template<typename T>
class SparseCache
{
 public:
   SparseCache(uint64_t maxChunkSize)
	   : chunkSize_(std::min<uint64_t>(maxChunkSize, 1024)), currChunk_(nullptr), currChunkIndex_(0)
   {}
   virtual ~SparseCache(void)
   {
	  for(auto& ch : chunks)
	  {
		 for(size_t i = 0; i < chunkSize_; ++i)
			delete ch.second[i];
		 delete[] ch.second;
	  }
   }

   T* tryGet(uint64_t index)
   {
	  uint64_t chunkIndex = index / chunkSize_;
	  uint64_t itemIndex = index % chunkSize_;
	  if(currChunk_ == nullptr || (chunkIndex != currChunkIndex_))
	  {
		 auto iter = chunks.find(chunkIndex);
		 if(iter != chunks.end())
		 {
			currChunk_ = iter->second;
			currChunkIndex_ = chunkIndex; // Update currChunkIndex_ when the chunk is found
		 }
		 else
		 {
			return nullptr;
		 }
	  }
	  return currChunk_[itemIndex];
   }

   T* get(uint64_t index)
   {
	  uint64_t chunkIndex = index / chunkSize_;
	  uint64_t itemIndex = index % chunkSize_;
	  if(currChunk_ == nullptr || (chunkIndex != currChunkIndex_))
	  {
		 currChunkIndex_ = chunkIndex;
		 auto iter = chunks.find(chunkIndex);
		 if(iter != chunks.end())
		 {
			currChunk_ = iter->second;
		 }
		 else
		 {
			currChunk_ = new T*[chunkSize_];
			memset(currChunk_, 0, chunkSize_ * sizeof(T*));
			chunks[chunkIndex] = currChunk_;
		 }
	  }
	  auto item = currChunk_[itemIndex];
	  if(!item)
	  {
		 item = create(index);
		 currChunk_[itemIndex] = item;
	  }
	  return item;
   }

 protected:
   virtual T* create(uint64_t index) = 0;

 private:
   std::map<uint64_t, T**> chunks;
   uint64_t chunkSize_;
   T** currChunk_;
   uint64_t currChunkIndex_;
};

} // namespace grk
