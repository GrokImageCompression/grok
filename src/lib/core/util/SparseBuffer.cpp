/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

/**
 * @file SparseBuffer.cpp
 *
 * @brief SparseBuffer stores a series of non-contiguous buffers. These buffers
 *  can also be treated as a single contiguous buffer.
 */

#include "grk_includes.h"

/* #define DEBUG_CHUNK_BUF */

namespace grk
{

SparseBufferIncompleteException::SparseBufferIncompleteException()
    : std::runtime_error("Sparse buffer incomplete")
{}

SparseBuffer::SparseBuffer() : dataLen(0), currentChunkId(0), reachedEnd_(false), mode_(Mode::Unset)
{}
SparseBuffer::~SparseBuffer()
{
  cleanup();
}
void SparseBuffer::tryIncrement()
{
  if(chunks.size() == 0 || currentChunkId == (size_t)(chunks.size() - 1))
  {
    reachedEnd_ = true;
    return;
  }
  const auto* const currentChunk = chunks[currentChunkId];
  if(currentChunk->offset() == currentChunk->num_elts() &&
     currentChunkId < (size_t)(chunks.size() - 1))
  {
    currentChunkId++;
    if(chunks[currentChunkId] == nullptr)
    {
      reachedEnd_ = true;
      return;
    }
  }
}
size_t SparseBuffer::length(void) const
{
  return dataLen;
}
size_t SparseBuffer::read(void* buffer, size_t numBytes)
{
  if(buffer == nullptr || numBytes == 0)
    return 0;
  /*don't try to read more bytes than are available */
  size_t contiguousBytesRemaining = dataLen - (size_t)offset();
  if(numBytes > contiguousBytesRemaining)
  {
#ifdef DEBUG_CHUNK_BUF
    grklog.warn("attempt to read past end of chunk buffer");
#endif
    numBytes = contiguousBytesRemaining;
  }
  size_t totalBytesRead = 0;
  size_t bytesLeftToRead = numBytes;
  while(bytesLeftToRead > 0 && currentChunkId < chunks.size())
  {
    const auto* const currentChunk = chunks[currentChunkId];
    if(currentChunk == nullptr)
      throw SparseBufferIncompleteException();
    size_t bytesInCurrentChunk = (currentChunk->num_elts() - (size_t)currentChunk->offset());
    size_t bytes_to_read =
        (bytesLeftToRead < bytesInCurrentChunk) ? bytesLeftToRead : bytesInCurrentChunk;
    memcpy((uint8_t*)buffer + totalBytesRead, currentChunk->buf() + currentChunk->offset(),
           bytes_to_read);
    chunkSkip(bytes_to_read);
    totalBytesRead += bytes_to_read;
    bytesLeftToRead -= bytes_to_read;
  }
  return totalBytesRead;
}
size_t SparseBuffer::skip(size_t numBytes)
{
  if(numBytes + offset() > dataLen)
  {
#ifdef DEBUG_CHUNK_BUF
    grklog.warn("attempt to skip past end of chunk buffer");
#endif
    throw SparseBufferOverrunException();
  }
  if(numBytes == 0)
    return 0;
  auto skipBytes = numBytes;
  while(currentChunkId < chunks.size() && skipBytes > 0)
  {
    const auto* const currentChunk = chunks[currentChunkId];
    if(currentChunk == nullptr)
      throw SparseBufferIncompleteException();
    size_t bytesInCurrentChunk = (size_t)(currentChunk->num_elts() - currentChunk->offset());
    if(skipBytes > bytesInCurrentChunk)
    {
      // hoover up all the bytes in this chunk, and move to the next one
      chunkSkip(bytesInCurrentChunk);
      skipBytes -= bytesInCurrentChunk;
    }
    else
    { // bingo! we found the chunk
      chunkSkip(skipBytes);
      break;
    }
  }
  return numBytes;
}
Buffer8* SparseBuffer::push(uint8_t* buf, size_t len, bool ownsData)
{
  if(mode_ == Mode::Indexed)
    throw std::runtime_error("Cannot use sequential push in indexed mode");
  mode_ = Mode::Sequential;
  if(len == 0)
    return nullptr;
  auto new_chunk = new Buffer8(buf, len, ownsData);
  chunks.push_back(new_chunk);
  currentChunkId = chunks.size() - 1;
  dataLen += len;
  return new_chunk;
}
Buffer8* SparseBuffer::push(size_t index, uint8_t* buf, size_t len, bool ownsData)
{
  std::lock_guard<std::mutex> guard(mutex_);
  if(mode_ == Mode::Sequential)
    throw std::runtime_error("Cannot use indexed push in sequential mode");
  mode_ = Mode::Indexed;
  if(len == 0)
    return nullptr;
  auto new_chunk = new Buffer8(buf, len, ownsData);
  if(index >= chunks.size())
    chunks.resize(index + 1, nullptr);
  if(chunks[index])
  {
    dataLen -= chunks[index]->num_elts();
    delete chunks[index];
  }
  chunks[index] = new_chunk;
  dataLen += len;
  return new_chunk;
}
void SparseBuffer::cleanup(void)
{
  std::lock_guard<std::mutex> guard(mutex_);
  for(size_t i = 0; i < chunks.size(); ++i)
    delete chunks[i];
  chunks.clear();
  dataLen = 0;
}
void SparseBuffer::rewind(void)
{
  for(size_t i = 0; i < chunks.size(); ++i)
  {
    auto chunk = chunks[i];
    if(chunk)
      chunk->set_offset(0);
  }
  currentChunkId = 0;
  reachedEnd_ = false;
}
void SparseBuffer::chunkSkip(size_t delta)
{
  if(!delta)
    return;

  if(reachedEnd_)
    throw SparseBufferOverrunException();

  auto currentChunk = chunks[currentChunkId];
  if(currentChunk == nullptr)
    throw SparseBufferIncompleteException();
  if(!currentChunk->increment_offset((ptrdiff_t)delta))
    throw SparseBufferOverrunException();
  if(currentChunk->offset() == currentChunk->num_elts())
    tryIncrement();
}
bool SparseBuffer::copyDataChunksToContiguous(uint8_t* buffer)
{
  if(!buffer)
    return false;
  size_t offsetCount = 0;
  for(size_t i = 0; i < chunks.size(); ++i)
  {
    const auto* const chunk = chunks[i];
    if(chunk == nullptr)
      return false;
    if(chunk->num_elts())
      memcpy(buffer + offsetCount, chunk->buf(), chunk->num_elts());
    offsetCount += chunk->num_elts();
  }
  return true;
}
uint8_t* SparseBuffer::chunkPtr(void)
{
  const auto* const currentChunk = chunks[currentChunkId];
  if(currentChunk == nullptr)
    throw SparseBufferIncompleteException();
  return currentChunk->currPtr();
}
size_t SparseBuffer::chunkLength(void)
{
  const auto* const currentChunk = chunks[currentChunkId];
  if(currentChunk == nullptr)
    throw SparseBufferIncompleteException();
  return currentChunk->remainingLength();
}
size_t SparseBuffer::chunkOffset(void)
{
  const auto* const currentChunk = chunks[currentChunkId];
  if(currentChunk == nullptr)
    throw SparseBufferIncompleteException();
  return currentChunk->offset();
}
size_t SparseBuffer::offset(void)
{
  size_t offsetCount = 0;
  for(size_t i = 0; i < currentChunkId; ++i)
  {
    const auto* const chunk = chunks[i];
    if(chunk == nullptr)
      throw SparseBufferIncompleteException();
    offsetCount += chunk->num_elts();
  }
  return offsetCount + chunkOffset();
}

} // namespace grk