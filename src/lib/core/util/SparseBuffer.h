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
 *
 */
#include <vector>

#pragma once
namespace grk
{
/*  SparseBuffer

 Manage a list of buffers, which can be treated as one single
 contiguous buffer.
 */
struct SparseBuffer
{
   SparseBuffer();
   ~SparseBuffer();
   grk_buf8* pushBack(uint8_t* buf, size_t len, bool ownsData);
   void incrementCurrentChunkOffset(size_t offset);
   size_t getCurrentChunkLength(void);
   // Treat segmented buffer as single contiguous buffer, and get current pointer
   uint8_t* getCurrentChunkPtr(void);
   // Reset all offsets to zero, and set current chunk to beginning of list
   void rewind(void);
   size_t skip(size_t numBytes);
   void increment(void);
   size_t read(void* buffer, size_t numBytes);
   size_t totalLength(void) const;

 private:
   // Treat segmented buffer as single contiguous buffer, and get current offset
   size_t getGlobalOffset(void);
   // Copy all chunks, in sequence, into contiguous array
   bool copyToContiguousBuffer(uint8_t* buffer);
   // Clean up internal resources
   void cleanup(void);
   size_t getCurrentChunkOffset(void);
   void pushBack(grk_buf8* chunk);
   size_t dataLen; /* total length of all chunks*/
   size_t currentChunkId; /* current index into chunk vector */
   std::vector<grk_buf8*> chunks;
   bool reachedEnd_;
};

} // namespace grk
