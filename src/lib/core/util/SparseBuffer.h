/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

#pragma once

#include <vector>
#include <stdexcept>
#include <mutex>

#include "buffer.h"

namespace grk
{

class SparseBufferIncompleteException : public std::runtime_error
{
public:
  SparseBufferIncompleteException();
};

/**
 * @class SparseBuffer
 * @brief Manages a list of buffers (named chunks) which can be treated as
 * one single contiguous buffer
 */
class SparseBuffer
{
public:
  /**
   * @brief Constructs a SparseBuffer
   */
  SparseBuffer();
  /**
   * @brief Destroys a SparseBuffer
   */
  virtual ~SparseBuffer();

  /**
   * @brief Resets all chunk offsets to zero
   *  and sets current chunk pointing to first chunk
   */
  virtual void rewind(void);

  /**
   * @brief Skips contiguous bytes
   * @param numBytes number of bytes to skip
   * @return number of bytes skipped
   */
  size_t skip(size_t numBytes);

  /**
   * @brief Reads contiguous bytes
   * @param buffer buffer to receive the read bytes
   * @param numBytes number of bytes to read
   * @return number of bytes read
   */
  size_t read(void* buffer, size_t numBytes);

  /**
   * @brief Gets contiguous length
   * @return contiguous length
   */
  size_t length(void) const;

  /**
   * @brief Pushes a new chunk (sequential mode)
   * @param buf buffer
   * @param len buffer length
   * @param ownsData true if chunk should be owned by SparseBuffer
   * @return resulting @ref Buffer8 pushed
   */
  Buffer8* push(uint8_t* buf, size_t len, bool ownsData);

  /**
   * @brief Pushes a new chunk (indexed mode)
   * @param index index
   * @param buf buffer
   * @param len buffer length
   * @param ownsData true if chunk should be owned by SparseBuffer
   * @return resulting @ref Buffer8 pushed
   */
  Buffer8* push(size_t index, uint8_t* buf, size_t len, bool ownsData);

  /**
   * @brief Increments offset of current chunk.
   *
   * An exception will be thrown if offset overruns
   * current chunk
   * @param offset offset
   */
  virtual void chunkSkip(size_t offset);

  /**
   * @brief Gets current chunk length
   * @return current chunk length
   */
  size_t chunkLength(void);

  /**
   * @brief Gets current chunk pointer
   * @return current chunk pointer
   */
  uint8_t* chunkPtr(void);

  bool empty(void) const
  {
    return chunks.empty();
  }

private:
  enum class Mode
  {
    Unset,
    Sequential,
    Indexed
  };

  /**
   * @brief Increments to next chunk if appropriate
   */
  void tryIncrement(void);

  /**
   * @brief Gets contiguous offset
   * @return offset
   */
  size_t offset(void);

  /**
   * @brief Copies all chunks, in sequence, into single buffer
   * @param buffer buffer
   * @return true if successful
   */
  bool copyDataChunksToContiguous(uint8_t* buffer);

  /**
   * @brief Cleans up resources
   */
  void cleanup(void);

  /**
   * @brief Gets offset of current chunk
   * @return current chunk offset
   */
  size_t chunkOffset(void);

  /**
   * @brief Total length of all chunks
   */
  size_t dataLen;

  /**
   * @brief Current chunk ID
   */
  size_t currentChunkId;

  /**
   * @brief Collection of chunks
   */
  std::vector<Buffer8*> chunks;

  /**
   * @brief End of contiguous buffer has been reached
   */
  bool reachedEnd_;

  /**
   * @brief Mode
   */
  Mode mode_;

  /**
   * @brief Mutex for thread safety
   */
  std::mutex mutex_;
};

} // namespace grk