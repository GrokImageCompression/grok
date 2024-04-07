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
#include "grk_includes.h"

/* #define DEBUG_CHUNK_BUF */

namespace grk
{
SparseBuffer::SparseBuffer() : dataLen(0), currentChunkId(0), reachedEnd_(false) {}
SparseBuffer::~SparseBuffer()
{
   cleanup();
}
void SparseBuffer::increment()
{
   if(chunks.size() == 0 || currentChunkId == (size_t)(chunks.size() - 1))
   {
	  reachedEnd_ = true;
	  return;
   }
   auto currentChunk = chunks[currentChunkId];
   if(currentChunk->offset == currentChunk->len && currentChunkId < (size_t)(chunks.size() - 1))
   {
	  currentChunkId++;
   }
}
size_t SparseBuffer::totalLength(void) const
{
   return dataLen;
}
size_t SparseBuffer::read(void* buffer, size_t numBytes)
{
   if(buffer == nullptr || numBytes == 0)
	  return 0;
   /*don't try to read more bytes than are available */
   size_t contiguousBytesRemaining = dataLen - (size_t)getGlobalOffset();
   if(numBytes > contiguousBytesRemaining)
   {
#ifdef DEBUG_CHUNK_BUF
	  Logger::logger_.warn("attempt to read past end of chunk buffer");
#endif
	  numBytes = contiguousBytesRemaining;
   }
   size_t totalBytesRead = 0;
   size_t bytesLeftToRead = numBytes;
   while(bytesLeftToRead > 0 && currentChunkId < chunks.size())
   {
	  auto currentChunk = chunks[currentChunkId];
	  size_t bytesInCurrentChunk = (currentChunk->len - (size_t)currentChunk->offset);
	  size_t bytes_to_read =
		  (bytesLeftToRead < bytesInCurrentChunk) ? bytesLeftToRead : bytesInCurrentChunk;
	  if(buffer)
	  {
		 memcpy((uint8_t*)buffer + totalBytesRead, currentChunk->buf + currentChunk->offset,
				bytes_to_read);
	  }
	  incrementCurrentChunkOffset(bytes_to_read);
	  totalBytesRead += bytes_to_read;
	  bytesLeftToRead -= bytes_to_read;
   }
   return totalBytesRead;
}
size_t SparseBuffer::skip(size_t numBytes)
{
   if(numBytes + getGlobalOffset() > dataLen)
   {
#ifdef DEBUG_CHUNK_BUF
	  Logger::logger_.warn("attempt to skip past end of chunk buffer");
#endif
	  return numBytes;
   }
   if(numBytes == 0)
	  return 0;
   auto skipBytes = numBytes;
   while(currentChunkId < chunks.size() && skipBytes > 0)
   {
	  auto currentChunk = chunks[currentChunkId];
	  size_t bytesInCurrentChunk = (size_t)(currentChunk->len - currentChunk->offset);
	  if(skipBytes > bytesInCurrentChunk)
	  {
		 // hoover up all the bytes in this chunk, and move to the next one
		 incrementCurrentChunkOffset(bytesInCurrentChunk);
		 skipBytes -= bytesInCurrentChunk;
		 currentChunk = chunks[currentChunkId];
	  }
	  else
	  { // bingo! we found the chunk
		 incrementCurrentChunkOffset(skipBytes);
		 break;
	  }
   }
   return numBytes;
}
grk_buf8* SparseBuffer::pushBack(uint8_t* buf, size_t len, bool ownsData)
{
   // assert(len < UINT_MAX);
   auto new_chunk = new grk_buf8(buf, len, ownsData);
   pushBack(new_chunk);
   return new_chunk;
}
void SparseBuffer::pushBack(grk_buf8* chunk)
{
   if(!chunk)
	  return;
   chunks.push_back(chunk);
   currentChunkId = (size_t)(chunks.size() - 1);
   dataLen += chunk->len;
}
void SparseBuffer::cleanup(void)
{
   for(size_t i = 0; i < chunks.size(); ++i)
	  delete chunks[i];
   chunks.clear();
}
void SparseBuffer::rewind(void)
{
   for(size_t i = 0; i < chunks.size(); ++i)
   {
	  auto chunk = chunks[i];
	  if(chunk)
		 chunk->offset = 0;
   }
   currentChunkId = 0;
   reachedEnd_ = false;
}
void SparseBuffer::incrementCurrentChunkOffset(size_t delta)
{
   if(!delta)
	  return;

   if(reachedEnd_)
	  throw SparseBufferOverrunException();

   auto currentChunk = chunks[currentChunkId];
   currentChunk->incrementOffset((ptrdiff_t)delta);
   if(currentChunk->offset == currentChunk->len)
	  increment();
}
bool SparseBuffer::copyToContiguousBuffer(uint8_t* buffer)
{
   size_t offset = 0;
   if(!buffer)
	  return false;
   for(size_t i = 0; i < chunks.size(); ++i)
   {
	  auto chunk = chunks[i];
	  if(chunk->len)
		 memcpy(buffer + offset, chunk->buf, chunk->len);
	  offset += chunk->len;
   }
   return true;
}
uint8_t* SparseBuffer::getCurrentChunkPtr(void)
{
   auto currentChunk = chunks[currentChunkId];
   return (currentChunk) ? currentChunk->currPtr() : nullptr;
}
size_t SparseBuffer::getCurrentChunkLength(void)
{
   auto currentChunk = chunks[currentChunkId];
   return (currentChunk) ? currentChunk->remainingLength() : 0;
}
size_t SparseBuffer::getCurrentChunkOffset(void)
{
   auto currentChunk = chunks[currentChunkId];
   return (currentChunk) ? currentChunk->offset : 0;
}
size_t SparseBuffer::getGlobalOffset(void)
{
   size_t offset = 0;
   for(size_t i = 0; i < currentChunkId; ++i)
   {
	  auto chunk = chunks[i];
	  offset += chunk->len;
   }
   return offset + getCurrentChunkOffset();
}

} // namespace grk
