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

#pragma once

#include "MemAdvisor.h"

namespace grk
{

#define GROK_STREAM_STATUS_OUTPUT 0x1U
#define GROK_STREAM_STATUS_INPUT 0x2U
#define GROK_STREAM_STATUS_END 0x4U
#define GROK_STREAM_STATUS_ERROR 0x8U

/**
 * @struct BufferedStream
 * @brief Manages buffered read/write using callbacks or memory buffer
 *
 * For memory buffers, it is possible to perform zero-copy reads where
 * a suitable pointer into the buffer is returned instead of copying
 * into a second buffer.
 *
 * Note: a memory-mapped file is treated as a memory buffer.
 */
struct BufferedStream : public IStream
{
  /**
   * @brief Constructs a BufferedStream
   * @param buffer underlying buffer. If null and buffer_size is non-zero,
   * then a new double buffer will be created
   * @param initial_buffer_size initial size of double buffer
   * @param buffer_size size of buffer
   * @param is_input true if this is an input stream
   *
   */
  BufferedStream(uint8_t* buffer, size_t initial_buffer_size, size_t buffer_size, bool is_input)
      : status_(is_input ? GROK_STREAM_STATUS_INPUT : GROK_STREAM_STATUS_OUTPUT),
        originalBufferLength_(buffer_size)
  {
    if(!buffer_size)
      throw std::runtime_error("BufferedStream: buffer size cannot be zero");

    initial_buffer_size = std::max(initial_buffer_size, buffer_size);
    if(buffer)
      buf_ = std::make_unique<BufferAligned8>(buffer, initial_buffer_size, false);
    else
      buf_ = std::make_unique<BufferAligned8>(
          (uint8_t*)grk_aligned_malloc(4096, initial_buffer_size), initial_buffer_size, true);
  }

  /**
   * @brief Destroys a BufferedStream
   */
  ~BufferedStream() override
  {
    if(freeUserDataCallback_)
      freeUserDataCallback_(userData_);
    delete memAdvisor_;
  }

  IStream* bifurcate(void) override
  {
    if(!isMemStream())
      return nullptr;

    auto memStream = static_cast<MemStream*>(userData_);
    auto stream = new BufferedStream(memStream->buf_, 0, memStream->len_, true);
    stream->setFormat(format_);
    stream->setUserData(memStream, nullptr, memStream->len_);
    memStreamSetup(stream, true);
    stream->streamOffset_ = streamOffset_;
    stream->bufferedBytes_ = memStream->len_ - streamOffset_;
    stream->readBytesSeekable_ = bufferedBytes_;
    stream->buf_->set_offset(buf_->offset());

    return stream;
  }

  /**
   * @brief Sets the @ref IFetcher
   *
   * @param fetcher @ref IFetcher
   */
  void setFetcher(IFetcher* fetcher)
  {
    fetcher_ = fetcher;
  }

  /**
   * @brief Gets the @ref IFetcher
   *
   * @return IFetcher*
   */
  IFetcher* getFetcher(void) override
  {
    return fetcher_;
  }

  void setMemAdvisor(MemAdvisor* advisor)
  {
    memAdvisor_ = advisor;
  }

  void memAdvise(size_t virtual_offset, size_t length, GrkAccessPattern pattern) override
  {
    if(memAdvisor_)
      memAdvisor_->advise(virtual_offset, length, pattern);
  }

  void setChunkBuffer(std::shared_ptr<ChunkBuffer<>> chunkBuffer) override
  {
    chunk_buf_ = chunkBuffer;
    streamOffset_ = chunkBuffer->offset();
    bufferedBytes_ = chunk_buf_->size();
    readBytesSeekable_ = bufferedBytes_;
  }

  void setUserData(void* data, grk_stream_free_user_data_fn freeUserDataFun, uint64_t len) override
  {
    userData_ = data;
    freeUserDataCallback_ = freeUserDataFun;
    userDataLength_ = len;
  }

  void setCallbacks(StreamCallbacks& callbacks) override
  {
    callbacks_ = callbacks;
  }

  /**
   * @brief Read bytes. Do zero copy read if buffer is null
   *
   * @param buffer buffer for non-zero-copy read
   * @param zero_copy_buffer buffer for zero-copy read
   * @param len number of bytes
   * @return size_t number of bytes read
   */
  size_t read(uint8_t* buffer, uint8_t** zero_copy_buffer, size_t len) override
  {
    if(buffer)
      return read(buffer, len);
    if(!zero_copy_buffer)
      throw std::runtime_error("Missing zero copy buffer.");
    *zero_copy_buffer = chunk_buf_ ? (uint8_t*)chunk_buf_->currPtr(len) : buf_->currPtr(len);

    return read(nullptr, len);
  }
  bool write24u(uint32_t value) override
  {
    return write_non_template((const uint8_t*)&value, sizeof(uint32_t), 3);
  }

  bool write8u(uint8_t value) override
  {
    return writeBytes(&value, 1) == 1;
  }

  size_t writeBytes(const uint8_t* buffer, size_t len) override
  {
    assert(len);
    if(!len || !buffer)
      return 0;

    if(status_ & GROK_STREAM_STATUS_ERROR)
      return 0;

    // handle case where there is no internal buffer (memory stream)
    if(isMemStream())
    {
      /* we should do an actual write on the media */
      auto current_write_nb_bytes = callbacks_.writeCallback_(buffer, len, userData_);
      writeIncrement(current_write_nb_bytes);

      return current_write_nb_bytes;
    }
    size_t write_nb_bytes = 0;
    while(true)
    {
      size_t available_bytes = buf_->num_elts() - bufferedBytes_;

      // we can copy all write bytes to double buffer
      if(available_bytes >= len)
      {
        memcpy(buf_->currPtr(len), buffer, len);
        write_nb_bytes += len;
        writeIncrement(len);
        return write_nb_bytes;
      }

      // we fill the double buffer with write bytes
      if(available_bytes)
      {
        write_nb_bytes += available_bytes;
        memcpy(buf_->currPtr(available_bytes), buffer, available_bytes);
        buf_->set_offset(0);
        bufferedBytes_ += available_bytes;
        streamOffset_ += available_bytes;
        buffer += available_bytes;
        len -= available_bytes;
      }
      // now we can flush the double buffer, and try to write
      // more bytes
      if(!flush())
        return 0;
    }

    return write_nb_bytes;
  }

  bool flush() override
  {
    if(isMemStream())
      return true;
    /* the number of bytes written on the media. */
    buf_->set_offset(0);
    if(bufferedBytes_)
    {
      /* we should do an actual write on the media */
      size_t current_write_nb_bytes =
          callbacks_.writeCallback_(buf_->currPtr(bufferedBytes_), bufferedBytes_, userData_);

      if(current_write_nb_bytes != bufferedBytes_)
      {
        status_ |= GROK_STREAM_STATUS_ERROR;
        grklog.error("Error on writing stream.");
        return false;
      }
      buf_->increment_offset((ptrdiff_t)current_write_nb_bytes);
      bufferedBytes_ = 0;
    }
    buf_->set_offset(0);

    return true;
  }

  bool skip(int64_t len) override
  {
    return (status_ & GROK_STREAM_STATUS_INPUT) ? readSkip(len) : writeSkip(len);
  }

  uint64_t tell(void) override
  {
    return streamOffset_;
  }

  uint64_t numBytesLeft(void) override
  {
    assert(streamOffset_ <= userDataLength_);
    return userDataLength_ ? (uint64_t)(userDataLength_ - streamOffset_) : 0;
  }

  bool seek(uint64_t offset) override
  {
    return (status_ & GROK_STREAM_STATUS_INPUT) ? readSeek(offset) : writeSeek(offset);
  }

  /**
   * @brief Checks if stream is seekable
   * @return true if seekable
   */
  bool hasSeek() override
  {
    return callbacks_.seekCallback_ != nullptr;
  }

  bool supportsZeroCopy() override
  {
    return isMemStream() && (status_ & GROK_STREAM_STATUS_INPUT);
  }

  void setFormat(GRK_CODEC_FORMAT format) override
  {
    format_ = format;
  }

  GRK_CODEC_FORMAT getFormat(void) override
  {
    return format_;
  }

  uint8_t* currPtr(void) override
  {
    return buf_->currPtr(0);
  }

  bool isMemStream() override
  {
    return !buf_->owns_data();
  }

private:
  /**
   * @brief Reads bytes from stream.
   * passing in nullptr for buffer will execute a zero-copy read
   * @param	buffer	pointer to the data buffer that will receive the data.
   * @param	len	number of bytes to read.
   * @return the number of bytes read
   */
  size_t read(uint8_t* buffer, size_t len)
  {
    if(!buffer && !supportsZeroCopy())
      throw std::exception();
    assert(len);
    if(!len)
      return 0;
    size_t read_nb_bytes = 0;

    // 1. if stream is at end, then return immediately
    if(status_ & GROK_STREAM_STATUS_END)
      return 0;
    // 2. if we have enough bytes in buffer, then read from buffer and return
    if(len <= bufferedBytes_)
    {
      if(buffer)
        memcpy(buffer, chunk_buf_ ? chunk_buf_->currPtr(len) : buf_->currPtr(len), len);
      bool rc = chunk_buf_ ? chunk_buf_->increment_offset((ptrdiff_t)len)
                           : buf_->increment_offset((ptrdiff_t)len);
      if(!rc)
      {
        status_ |= GROK_STREAM_STATUS_ERROR;
        return 0;
      }
      bufferedBytes_ -= len;
      assert(bufferedBytes_ <= readBytesSeekable_);
      read_nb_bytes += len;
      streamOffset_ += len;
      assert(streamOffset_ <= userDataLength_);
      return read_nb_bytes;
    }
    // 3. if stream is at end, then read remaining bytes in buffer and return
    if(status_ & GROK_STREAM_STATUS_END)
    {
      read_nb_bytes += bufferedBytes_;
      if(buffer && bufferedBytes_)
        memcpy(buffer, buf_->currPtr(bufferedBytes_), bufferedBytes_);
      streamOffset_ += bufferedBytes_;
      assert(streamOffset_ <= userDataLength_);
      invalidateBuffer();
      return read_nb_bytes;
    }
    // 4. read remaining bytes in buffer
    if(bufferedBytes_)
    {
      read_nb_bytes += bufferedBytes_;
      if(buffer)
      {
        memcpy(buffer, buf_->currPtr(bufferedBytes_), bufferedBytes_);
        buffer += bufferedBytes_;
      }
      len -= bufferedBytes_;
      streamOffset_ += bufferedBytes_;
      assert(streamOffset_ <= userDataLength_);
      bufferedBytes_ = 0;
    }

    // 5. read from "media"
    invalidateBuffer();
    // direct read into buffer
    if(len > buf_->num_elts())
    {
      auto b_read = readDirect(buffer, len);
      return read_nb_bytes + b_read;
    }
    if(!firstCache_)
      buf_->set_num_elts(originalBufferLength_);
    while(true)
    {
      bufferedBytes_ =
          callbacks_.readCallback_(buf_->currPtr(buf_->num_elts()), buf_->num_elts(), userData_);
      // sanity check on external read function
      if(bufferedBytes_ > buf_->num_elts())
      {
        grklog.error("Buffered stream: read length greater than buffer length");
        break;
      }
      readBytesSeekable_ = bufferedBytes_;
      // i) end of stream
      if(bufferedBytes_ == 0)
      {
        invalidateBuffer();
        status_ |= GROK_STREAM_STATUS_END;
        break;
      }
      // ii) or not enough data
      else if(bufferedBytes_ < len)
      {
        read_nb_bytes += bufferedBytes_;
        if(buffer)
        {
          memcpy(buffer, buf_->currPtr(bufferedBytes_), bufferedBytes_);
          buffer += bufferedBytes_;
        }
        len -= bufferedBytes_;
        streamOffset_ += bufferedBytes_;
        assert(streamOffset_ <= userDataLength_);
        invalidateBuffer();
      }
      // iii) or we have read the exact amount requested
      else
      {
        read_nb_bytes += len;
        if(buffer && len)
          memcpy(buffer, buf_->currPtr(len), len);
        buf_->increment_offset((ptrdiff_t)len);
        bufferedBytes_ -= len;
        assert(bufferedBytes_ <= readBytesSeekable_);
        streamOffset_ += len;
        assert(streamOffset_ <= userDataLength_);
        break;
      }
    }
    firstCache_ = false;
    return read_nb_bytes;
  }

  /**
   * @brief Reads directly from callback
   *
   * @param buffer buffer
   * @param len number of bytes to read
   * @return size_t number of bytes read
   */
  size_t readDirect(uint8_t* buffer, size_t len)
  {
    size_t read_nb_bytes = 0;
    size_t remaining = len;
    while(true)
    {
      auto buffered_bytes = callbacks_.readCallback_(buffer, remaining, userData_);
      // sanity check on external read function
      if(buffered_bytes > remaining)
      {
        grklog.error("Buffered stream: read length greater than buffer length");
        return 0;
      }
      // i) end of stream
      if(buffered_bytes == 0)
      {
        status_ |= GROK_STREAM_STATUS_END;
        return read_nb_bytes;
      }
      else
      {
        read_nb_bytes += buffered_bytes;
        buffer += buffered_bytes;
        remaining -= buffered_bytes;
        streamOffset_ += buffered_bytes;
        assert(streamOffset_ <= userDataLength_);
        if(read_nb_bytes == len)
          return read_nb_bytes;
      }
    }
    return 0;
  }

  /**
   * @brief Skips bytes in write stream.
   * @param	len	number of bytes to skip.
   * @return true if successful
   */
  bool writeSkip(int64_t len)
  {
    auto offset = (int64_t)streamOffset_ + len;
    if(offset < 0)
      return false;
    return writeSeek((uint64_t)offset);
  }
  /**
   * @brief Skip bytes in read stream.
   * @param	len	the number of bytes to skip.
   * @return true if successful
   */
  bool readSkip(int64_t len)
  {
    auto offset = (int64_t)streamOffset_ + len;
    if(offset < 0)
      return false;

    return readSeek((uint64_t)offset);
  }

  /**
   * @brief Performs absolute seek in read stream.
   * @param	offset	absolute offset
   * @return true if successful
   */
  bool readSeek(uint64_t offset)
  {
    if(status_ & GROK_STREAM_STATUS_ERROR)
      return false;

    if(chunk_buf_)
    {
      bool rc = chunk_buf_->set_offset(offset);
      if(rc)
        streamOffset_ = chunk_buf_->offset();
      return rc;
    }

    // 1. try to seek in buffer
    if(!(status_ & GROK_STREAM_STATUS_END))
    {
      if((offset >= streamOffset_ && offset < streamOffset_ + bufferedBytes_) ||
         (offset < streamOffset_ &&
          offset >= streamOffset_ - (readBytesSeekable_ - bufferedBytes_)))
      {
        auto increment = (int64_t)offset - (int64_t)streamOffset_;
        streamOffset_ = offset;
        assert(streamOffset_ <= userDataLength_);
        buf_->increment_offset((ptrdiff_t)increment);
        bufferedBytes_ = (size_t)((int64_t)bufferedBytes_ - increment);
        assert(bufferedBytes_ <= readBytesSeekable_);

        return true;
      }
    }

    // 2. Since we can't seek in buffer, we must invalidate
    //  buffer contents and seek in media
    invalidateBuffer();
    if(!(callbacks_.seekCallback_(offset, userData_)))
    {
      status_ |= GROK_STREAM_STATUS_END;
      return false;
    }
    else
    {
      status_ &= (~GROK_STREAM_STATUS_END);
      streamOffset_ = offset;
      if(streamOffset_ > userDataLength_)
      {
        status_ |= GROK_STREAM_STATUS_END;
        return false;
      }
    }
    return true;
  }

  /**
   * @brief Performs absolute seek in write stream.
   * @param	offset	absolute offset
   * @return true if successful
   */
  bool writeSeek(uint64_t offset)
  {
    if(status_ & GROK_STREAM_STATUS_ERROR)
      return false;

    if(!flush())
    {
      status_ |= GROK_STREAM_STATUS_ERROR;
      return false;
    }
    invalidateBuffer();
    if(!callbacks_.seekCallback_(offset, userData_))
    {
      status_ |= GROK_STREAM_STATUS_ERROR;
      return false;
    }
    else
    {
      streamOffset_ = offset;
    }
    if(isMemStream())
      buf_->set_offset(offset);
    return true;
  }

  void writeIncrement(size_t len)
  {
    buf_->increment_offset((ptrdiff_t)len);
    if(!isMemStream())
      bufferedBytes_ += len;
    else
      assert(bufferedBytes_ == 0);
    streamOffset_ += len;
  }

  bool write_non_template(const uint8_t* value, uint8_t sizeOfType, uint8_t numBytes) override
  {
    if(status_ & GROK_STREAM_STATUS_ERROR)
      return false;
    if(numBytes > sizeOfType)
      return false;

    // handle case where there is no internal buffer (buffer stream)
    if(isMemStream())
    {
      // skip first to make sure that we are not at the end of the stream
      if(!callbacks_.seekCallback_(streamOffset_ + numBytes, userData_))
        return false;
      grk_write(buf_->currPtr(), value, sizeOfType, numBytes);
      writeIncrement(numBytes);
      return true;
    }
    size_t remaining_bytes = buf_->num_elts() - bufferedBytes_;
    if(remaining_bytes < numBytes)
    {
      if(!flush())
        return false;
    }
    grk_write(buf_->currPtr(), value, sizeOfType, numBytes);
    writeIncrement(numBytes);
    return true;
  }
  void invalidateBuffer()
  {
    buf_->set_offset(0);
    bufferedBytes_ = 0;
    if(status_ & GROK_STREAM_STATUS_INPUT)
      readBytesSeekable_ = 0;
  }

  /**
   * @brief user data
   */
  void* userData_ = nullptr;
  /**
   * Pointer to function to free user_data_ (nullptr at initialization)
   * when destroying the stream. If pointer is nullptr the function is not
   * called and the user_data_ is not freed (even if it isn't nullptr).
   */
  grk_stream_free_user_data_fn freeUserDataCallback_;
  /**
   * User data length.
   * Currently set to size of file for file read stream,
   * and size of buffer for buffer read/write stream
   */
  uint64_t userDataLength_ = 0;

  StreamCallbacks callbacks_;
  /**
   * Stream status flags
   */
  uint32_t status_ = 0;

  /**
   * @brief Backing buffer
   */
  std::unique_ptr<BufferAligned8> buf_;
  std::shared_ptr<ChunkBuffer<>> chunk_buf_;

  // number of bytes read in, or slated for write
  size_t bufferedBytes_ = 0;

  // number of seekable bytes in buffer. This will equal
  // the number of bytes
  // read in the last media read.
  // We always have buffered_bytes_ <= read_bytes_seekable_
  size_t readBytesSeekable_ = 0;

  // number of bytes read/written from the beginning of the stream
  uint64_t streamOffset_ = 0;

  GRK_CODEC_FORMAT format_ = GRK_CODEC_UNK;

  bool firstCache_ = true;
  size_t originalBufferLength_ = 0;

  IFetcher* fetcher_ = nullptr;
  MemAdvisor* memAdvisor_ = nullptr;
};

} // namespace grk
