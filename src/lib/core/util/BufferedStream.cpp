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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */
#include "grk_includes.h"
namespace grk
{
template<typename TYPE>
void grk_write(uint8_t* buffer, TYPE value, uint32_t numBytes);
template<typename TYPE>
void grk_read(const uint8_t* buffer, TYPE* value, uint32_t numBytes);

// buffered stream
BufferedStream::BufferedStream(uint8_t* buffer, size_t buffer_size, bool is_input)
	: user_data_(nullptr), free_user_data_fn_(nullptr), user_data_length_(0), read_fn_(nullptr),
	  zero_copy_read_fn_(nullptr), write_fn_(nullptr), seek_fn_(nullptr),
	  status_(is_input ? GROK_STREAM_STATUS_INPUT : GROK_STREAM_STATUS_OUTPUT), buf_(nullptr),
	  buffered_bytes_(0), read_bytes_seekable_(0), stream_offset_(0), format_(GRK_CODEC_UNK)
{
   buf_ = new grk_buf8((!buffer && buffer_size) ? new uint8_t[buffer_size] : buffer, buffer_size,
					   buffer == nullptr);
   obj.wrapper = new GrkObjectWrapperImpl(this);
}

BufferedStream::~BufferedStream()
{
   if(free_user_data_fn_)
	  free_user_data_fn_(user_data_);
   delete buf_;
}
void BufferedStream::setFormat(GRK_CODEC_FORMAT format)
{
   format_ = format;
}
GRK_CODEC_FORMAT BufferedStream::getFormat(void)
{
   return format_;
}
void BufferedStream::setUserData(void* data, grk_stream_free_user_data_fn freeUserDataFun)
{
   user_data_ = data;
   free_user_data_fn_ = freeUserDataFun;
}
void* BufferedStream::getUserData(void)
{
   return user_data_;
}
void BufferedStream::setUserDataLength(uint64_t len)
{
   user_data_length_ = len;
}
uint32_t BufferedStream::getStatus(void)
{
   return status_;
}
void BufferedStream::setReadFunction(grk_stream_read_fn fn)
{
   read_fn_ = fn;
}
void BufferedStream::setZeroCopyReadFunction(grk_stream_zero_copy_read_fn fn)
{
   zero_copy_read_fn_ = fn;
}
void BufferedStream::setWriteFunction(grk_stream_write_fn fn)
{
   write_fn_ = fn;
}
void BufferedStream::setSeekFunction(grk_stream_seek_fn fn)
{
   seek_fn_ = fn;
}
// note: passing in nullptr for buffer will execute a zero-copy read
size_t BufferedStream::read(uint8_t* buffer, size_t p_size)
{
   if(!buffer && !supportsZeroCopy())
	  throw std::exception();
   assert(p_size);
   if(!p_size)
	  return 0;
   size_t read_nb_bytes = 0;

   // 1. if stream is at end, then return immediately
   if(status_ & GROK_STREAM_STATUS_END)
	  return 0;
   // 2. if we have enough bytes in buffer, then read from buffer and return
   if(p_size <= buffered_bytes_)
   {
	  if(buffer)
	  {
		 assert(buf_->currPtr() >= buf_->buf);
		 assert((ptrdiff_t)buf_->currPtr() - (ptrdiff_t)buf_->buf + (ptrdiff_t)p_size <=
				(ptrdiff_t)buf_->len);
		 memcpy(buffer, buf_->currPtr(), p_size);
	  }
	  buf_->incrementOffset((ptrdiff_t)p_size);
	  buffered_bytes_ -= p_size;
	  assert(buffered_bytes_ <= read_bytes_seekable_);
	  read_nb_bytes += p_size;
	  stream_offset_ += p_size;
	  assert(stream_offset_ <= user_data_length_);
	  return read_nb_bytes;
   }
   // 3. if stream is at end, then read remaining bytes in buffer and return
   if(status_ & GROK_STREAM_STATUS_END)
   {
	  read_nb_bytes += buffered_bytes_;
	  if(buffer && buffered_bytes_)
	  {
		 assert(buf_->currPtr() >= buf_->buf);
		 assert((ptrdiff_t)buf_->currPtr() - (ptrdiff_t)buf_->buf + (ptrdiff_t)buffered_bytes_ <=
				(ptrdiff_t)buf_->len);
		 memcpy(buffer, buf_->currPtr(), buffered_bytes_);
	  }
	  stream_offset_ += buffered_bytes_;
	  assert(stream_offset_ <= user_data_length_);
	  invalidate_buffer();
	  return read_nb_bytes;
   }
   // 4. read remaining bytes in buffer
   if(buffered_bytes_)
   {
	  read_nb_bytes += buffered_bytes_;
	  if(buffer)
	  {
		 assert(buf_->currPtr() >= buf_->buf);
		 assert((ptrdiff_t)buf_->currPtr() - (ptrdiff_t)buf_->buf + (ptrdiff_t)buffered_bytes_ <=
				(ptrdiff_t)buf_->len);
		 memcpy(buffer, buf_->currPtr(), buffered_bytes_);
		 buffer += buffered_bytes_;
	  }
	  p_size -= buffered_bytes_;
	  stream_offset_ += buffered_bytes_;
	  assert(stream_offset_ <= user_data_length_);
	  buffered_bytes_ = 0;
   }

   // 5. read from "media"
   invalidate_buffer();
   while(true)
   {
	  buffered_bytes_ = read_fn_(buf_->currPtr(), buf_->len, user_data_);
	  // sanity check on external read function
	  if(buffered_bytes_ > buf_->len)
	  {
		 Logger::logger_.error("Buffered stream: read length greater than buffer length");
		 return 0;
	  }
	  read_bytes_seekable_ = buffered_bytes_;
	  // i) end of stream
	  if(buffered_bytes_ == 0 || buffered_bytes_ > buf_->len)
	  {
		 invalidate_buffer();
		 status_ |= GROK_STREAM_STATUS_END;
		 return read_nb_bytes;
	  }
	  // ii) or not enough data
	  else if(buffered_bytes_ < p_size)
	  {
		 read_nb_bytes += buffered_bytes_;
		 if(buffer)
		 {
			assert(buf_->currPtr() >= buf_->buf);
			assert((ptrdiff_t)buf_->currPtr() - (ptrdiff_t)buf_->buf + (ptrdiff_t)buffered_bytes_ <=
				   (ptrdiff_t)buf_->len);
			memcpy(buffer, buf_->currPtr(), buffered_bytes_);
			buffer += buffered_bytes_;
		 }
		 p_size -= buffered_bytes_;
		 stream_offset_ += buffered_bytes_;
		 assert(stream_offset_ <= user_data_length_);
		 invalidate_buffer();
	  }
	  // iii) or we have read the exact amount requested
	  else
	  {
		 read_nb_bytes += p_size;
		 if(buffer && p_size)
		 {
			assert(buf_->currPtr() >= buf_->buf);
			assert((ptrdiff_t)buf_->currPtr() - (ptrdiff_t)buf_->buf + (ptrdiff_t)p_size <=
				   (ptrdiff_t)buf_->len);
			memcpy(buffer, buf_->currPtr(), p_size);
		 }
		 buf_->incrementOffset((ptrdiff_t)p_size);
		 buffered_bytes_ -= p_size;
		 assert(buffered_bytes_ <= read_bytes_seekable_);
		 stream_offset_ += p_size;
		 assert(stream_offset_ <= user_data_length_);
		 return read_nb_bytes;
	  }
   }
   return 0;
}
bool BufferedStream::writeByte(uint8_t value)
{
   return writeBytes(&value, 1) == 1;
}
bool BufferedStream::writeShort(uint16_t value)
{
   return write<uint16_t>(value, sizeof(uint16_t));
}
bool BufferedStream::write24(uint32_t value)
{
   return write<uint32_t>(value, 3);
}
bool BufferedStream::writeInt(uint32_t value)
{
   return write<uint32_t>(value, sizeof(uint32_t));
}
bool BufferedStream::write64(uint64_t value)
{
   return write<uint64_t>(value, sizeof(uint64_t));
}
template<typename TYPE>
bool BufferedStream::write(TYPE value, uint8_t numBytes)
{
   if(status_ & GROK_STREAM_STATUS_ERROR)
	  return false;
   if(numBytes > sizeof(TYPE))
	  return false;

   // handle case where there is no internal buffer (buffer stream)
   if(isMemStream())
   {
	  // skip first to make sure that we are not at the end of the stream
	  if(!seek_fn_(stream_offset_ + numBytes, user_data_))
		 return false;
	  grk_write(buf_->currPtr(), value, numBytes);
	  writeIncrement(numBytes);
	  return true;
   }
   size_t remaining_bytes = buf_->len - buffered_bytes_;
   if(remaining_bytes < numBytes)
   {
	  if(!flush())
		 return false;
   }
   grk_write(buf_->currPtr(), value, numBytes);
   writeIncrement(numBytes);
   return true;
}
size_t BufferedStream::writeBytes(const uint8_t* buffer, size_t p_size)
{
   assert(p_size);
   if(!p_size || !buffer)
	  return 0;

   if(status_ & GROK_STREAM_STATUS_ERROR)
	  return 0;

   // handle case where there is no internal buffer (buffer stream)
   if(isMemStream())
   {
	  /* we should do an actual write on the media */
	  auto current_write_nb_bytes = write_fn_(buffer, p_size, user_data_);
	  writeIncrement(current_write_nb_bytes);

	  return current_write_nb_bytes;
   }
   size_t write_nb_bytes = 0;
   while(true)
   {
	  size_t remaining_bytes = buf_->len - buffered_bytes_;

	  /* we have more memory than required */
	  if(remaining_bytes >= p_size)
	  {
		 write_nb_bytes += p_size;
		 memcpy(buf_->currPtr(), buffer, p_size);
		 writeIncrement(p_size);
		 return write_nb_bytes;
	  }

	  /* we copy part of data (if possible) and flush the stream */
	  if(remaining_bytes)
	  {
		 write_nb_bytes += remaining_bytes;
		 memcpy(buf_->currPtr(), buffer, remaining_bytes);
		 buf_->offset = 0;
		 buffered_bytes_ += remaining_bytes;
		 stream_offset_ += remaining_bytes;
		 buffer += remaining_bytes;
		 p_size -= remaining_bytes;
	  }
	  if(!flush())
		 return 0;
   }

   return write_nb_bytes;
}
void BufferedStream::writeIncrement(size_t p_size)
{
   buf_->incrementOffset((ptrdiff_t)p_size);
   if(!isMemStream())
	  buffered_bytes_ += p_size;
   else
	  assert(buffered_bytes_ == 0);
   stream_offset_ += p_size;
}

// force write of any remaining bytes from double buffer
bool BufferedStream::flush()
{
   if(isMemStream())
	  return true;
   /* the number of bytes written on the media. */
   buf_->offset = 0;
   while(buffered_bytes_)
   {
	  /* we should do an actual write on the media */
	  size_t current_write_nb_bytes = write_fn_(buf_->currPtr(), buffered_bytes_, user_data_);

	  if(current_write_nb_bytes != buffered_bytes_)
	  {
		 status_ |= GROK_STREAM_STATUS_ERROR;
		 Logger::logger_.error("Error on writing stream.");
		 return false;
	  }
	  buf_->incrementOffset((ptrdiff_t)current_write_nb_bytes);
	  assert(buf_->currPtr() >= buf_->buf);
	  buffered_bytes_ -= current_write_nb_bytes;
	  assert(buffered_bytes_ <= read_bytes_seekable_);
   }
   buf_->offset = 0;

   return true;
}

void BufferedStream::invalidate_buffer()
{
   buf_->offset = 0;
   buffered_bytes_ = 0;
   if(status_ & GROK_STREAM_STATUS_INPUT)
	  read_bytes_seekable_ = 0;
}
bool BufferedStream::supportsZeroCopy()
{
   return isMemStream() && (status_ & GROK_STREAM_STATUS_INPUT);
}
uint8_t* BufferedStream::getZeroCopyPtr()
{
   return buf_->currPtr();
}

bool BufferedStream::read_skip(int64_t p_size)
{
   int64_t offset = (int64_t)stream_offset_ + p_size;

   if(offset < 0)
	  return false;

   return read_seek((uint64_t)offset);
}

bool BufferedStream::write_skip(int64_t p_size)
{
   int64_t offset = (int64_t)stream_offset_ + p_size;
   if(offset < 0)
	  return false;
   return write_seek((uint64_t)offset);
}
uint64_t BufferedStream::tell()
{
   return stream_offset_;
}
uint64_t BufferedStream::numBytesLeft(void)
{
   assert(stream_offset_ <= user_data_length_);
   return user_data_length_ ? (uint64_t)(user_data_length_ - stream_offset_) : 0;
}
bool BufferedStream::skip(int64_t p_size)
{
   if(status_ & GROK_STREAM_STATUS_INPUT)
	  return read_skip(p_size);
   else
	  return write_skip(p_size);
}
// absolute seek
bool BufferedStream::read_seek(uint64_t offset)
{
   if(status_ & GROK_STREAM_STATUS_ERROR)
	  return false;

   // 1. try to seek in buffer
   if(!(status_ & GROK_STREAM_STATUS_END))
   {
	  if((offset >= stream_offset_ && offset < stream_offset_ + buffered_bytes_) ||
		 (offset < stream_offset_ &&
		  offset >= stream_offset_ - (read_bytes_seekable_ - buffered_bytes_)))
	  {
		 int64_t increment = (int64_t)offset - (int64_t)stream_offset_;
		 stream_offset_ = offset;
		 assert(stream_offset_ <= user_data_length_);
		 buf_->incrementOffset((ptrdiff_t)increment);
		 assert(buf_->currPtr() >= buf_->buf);
		 buffered_bytes_ = (size_t)((int64_t)buffered_bytes_ - increment);
		 assert(buffered_bytes_ <= read_bytes_seekable_);

		 return true;
	  }
   }

   // 2. Since we can't seek in buffer, we must invalidate
   //  buffer contents and seek in media
   invalidate_buffer();
   if(!(seek_fn_(offset, user_data_)))
   {
	  status_ |= GROK_STREAM_STATUS_END;
	  return false;
   }
   else
   {
	  status_ &= (~GROK_STREAM_STATUS_END);
	  stream_offset_ = offset;
	  if(stream_offset_ > user_data_length_)
	  {
		 status_ |= GROK_STREAM_STATUS_END;
		 return false;
	  }
   }
   return true;
}

// absolute seek in stream
bool BufferedStream::write_seek(uint64_t offset)
{
   if(status_ & GROK_STREAM_STATUS_ERROR)
	  return false;

   if(!flush())
   {
	  status_ |= GROK_STREAM_STATUS_ERROR;
	  return false;
   }
   invalidate_buffer();
   if(!seek_fn_(offset, user_data_))
   {
	  status_ |= GROK_STREAM_STATUS_ERROR;
	  return false;
   }
   else
   {
	  stream_offset_ = offset;
   }
   if(isMemStream())
	  buf_->offset = offset;
   return true;
}
bool BufferedStream::seek(uint64_t offset)
{
   if(status_ & GROK_STREAM_STATUS_INPUT)
	  return read_seek(offset);
   else
	  return write_seek(offset);
}
bool BufferedStream::hasSeek(void)
{
   return seek_fn_ != nullptr;
}

bool BufferedStream::isMemStream()
{
   return !buf_->owns_data;
}

BufferedStream* BufferedStream::getImpl(grk_stream* stream)
{
   return ((GrkObjectWrapperImpl<BufferedStream>*)stream->wrapper)->getWrappee();
}

grk_stream* BufferedStream::getWrapper(void)
{
   return &obj;
}

} // namespace grk
