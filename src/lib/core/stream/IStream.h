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

#include <memory>

#include "grok.h"

#include "IMemAdvisor.h"
#include "IWriter.h"
#include "ChunkBuffer.h"

namespace grk
{

/*
 * Callback function prototype for zero copy read function
 */
typedef size_t (*readZeroCopyCallback)(uint8_t** buffer, size_t numBytes, void* user_data);

/**
 * @struct StreamCallbacks
 * @brief Stores callbacks
 *
 */
struct StreamCallbacks
{
  explicit StreamCallbacks(grk_stream_params* streamParams)
      : StreamCallbacks(streamParams->read_fn, nullptr, streamParams->seek_fn,
                        streamParams->write_fn)
  {}
  StreamCallbacks(grk_stream_read_fn rcb, readZeroCopyCallback zcrcb, grk_stream_seek_fn scb,
                  grk_stream_write_fn wcb)
      : readCallback_(rcb), readZeroCopyCallback_(zcrcb), seekCallback_(scb), writeCallback_(wcb)
  {}
  StreamCallbacks() : StreamCallbacks(nullptr, nullptr, nullptr, nullptr) {}

  grk_stream_read_fn readCallback_;
  readZeroCopyCallback readZeroCopyCallback_;
  grk_stream_seek_fn seekCallback_;
  grk_stream_write_fn writeCallback_;
};

class IFetcher;

struct IStream : public IWriter
{
  virtual ~IStream() = default;

  /**
   * @brief Sets user data
   * @param data user data
   * @param freeUserDataFun @ref grk_stream_free_user_data_fn used to
   * free data when stream is closed
   * @param len data length (for read stream)
   */
  virtual void setUserData(void* data, grk_stream_free_user_data_fn freeUserDataFun,
                           uint64_t len) = 0;

  /**
   * @brief Sets callbacks
   *
   * @param callbacks @ref StreamCallbacks
   */
  virtual void setCallbacks(StreamCallbacks& callbacks) = 0;

  /**
  * @brief Read bytes from stream.
  * @param	buffer	pointer to the data buffer that will receive the data.
  *  If null, do zero copy read
  * @param zero_copy_buffer	optional pointer to the buffer address
  * that will receive current zero copy pointer.
  * If null, do regular read
  * @param	len	number of bytes to read.

  * @return number of bytes read
  */
  virtual size_t read(uint8_t* buffer, uint8_t** zero_copy_buffer, size_t len) = 0;

  /**
   * @brief Writes 3 bytes from uint32_t as big endian
   * @param value uint32_t to write
   * @return true if successful
   */
  virtual bool write24u(uint32_t value) = 0;

  /**
   * @brief Writes byte
   *
   * Endian is NOT taken into account
   * @param value byte to write
   * @return true if successful
   */
  virtual bool write8u(uint8_t value) = 0;

  /**
   * @brief Writes bytes to stream (no correction for endian!).
   * @param	buffer pointer to the data buffer holds the data
   * to be written.
   * @param	len	number of bytes to write.
   * @return number of bytes written
   */
  virtual size_t writeBytes(const uint8_t* buffer, size_t len) = 0;

  /**
   * @brief Flushes stream to disk
   * @return true if the data could be flushed
   */
  virtual bool flush() = 0;

  /**
   * @brief Skips bytes in stream.
   * @param	len	number of bytes to skip.
   * @return true if successful
   */
  virtual bool skip(int64_t len) = 0;
  /**
   * @brief query byte offset of stream (similar to ftell).
   *
   * @return		the current position of the stream.
   */
  virtual uint64_t tell(void) = 0;
  /**
   * @brief Gets the number of bytes left before end of stream
   * @return number of bytes left before the end of the stream.
   */
  virtual uint64_t numBytesLeft(void) = 0;
  /**
   * @brief Seek bytes from the stream (absolute)
   * @param		offset		the number of bytes to skip.
   *
   * @return		true if successful, otherwise false
   */
  virtual bool seek(uint64_t offset) = 0;

  /**
   * @brief Checks if stream is seekable
   * @return true if seekable
   */
  virtual bool hasSeek() = 0;

  /**
   * @brief Checks is stream supports zero copy
   * @return true if zero copy supported
   */
  virtual bool supportsZeroCopy() = 0;

  /**
   * @brief Stores codec format J2K/JP2/MJ2
   *
   * This is needed when deciding what type of code stream object
   * to create based on stream
   * @param format @ref GRK_CODEC_FORMAT
   */
  virtual void setFormat(GRK_CODEC_FORMAT format) = 0;

  /**
   * @brief Gets codec format (J2K/JP2/MJ2)
   * @return @ref GRK_CODEC_FORMAT
   */
  virtual GRK_CODEC_FORMAT getFormat(void) = 0;

  /**
   * @brief Gets current pointer (used for zero copy)
   * @return current pointer
   */
  virtual uint8_t* currPtr(void) = 0;

  /**
   * @brief Checks if stream is memory stream i.e. from mapped file or buffer
   *
   * @return true
   * @return false
   */
  virtual bool isMemStream() = 0;

  /**
   * @brief Gets the @ref IFetcher
   *
   * @return IFetcher*
   */
  virtual IFetcher* getFetcher(void) = 0;

  virtual IStream* bifurcate(void) = 0;

  virtual void setChunkBuffer(std::shared_ptr<ChunkBuffer<>> chunkBuffer) = 0;

  virtual void memAdvise(size_t virtual_offset, size_t length, GrkAccessPattern pattern) = 0;
};

} // namespace grk