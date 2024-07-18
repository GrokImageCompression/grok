/**
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

namespace grk
{
MemStream::MemStream(uint8_t* buffer, size_t offset, size_t length, bool owns)
	: buf(buffer), off(offset), len(length), fd(0), ownsBuffer(owns)
{}
MemStream::MemStream() : MemStream(nullptr, 0, 0, false) {}
MemStream::~MemStream()
{
   if(ownsBuffer)
	  delete[] buf;
}

static void free_mem(void* user_data)
{
   auto data = (MemStream*)user_data;
   if(data)
	  delete data;
}

static size_t zero_copy_read_from_mem(uint8_t** buffer, size_t numBytes, void* src)
{
   size_t nb_read = 0;
   auto srcStream = (MemStream*)src;

   if(((size_t)srcStream->off + numBytes) < srcStream->len)
	  nb_read = numBytes;

   *buffer = srcStream->buf + srcStream->off;
   assert(srcStream->off + nb_read <= srcStream->len);
   srcStream->off += nb_read;

   return nb_read;
}

static size_t read_from_mem(uint8_t* dest, size_t numBytes, void* src)
{
   size_t nb_read;

   if(!dest)
	  return 0;

   auto srcStream = (MemStream*)src;

   if(srcStream->off + numBytes < srcStream->len)
	  nb_read = numBytes;
   else
	  nb_read = (size_t)(srcStream->len - srcStream->off);

   if(nb_read)
   {
	  assert(srcStream->off + nb_read <= srcStream->len);
	  // (don't copy buffer into itself)
	  if(dest != srcStream->buf + srcStream->off)
		 memcpy(dest, srcStream->buf + srcStream->off, nb_read);
	  srcStream->off += nb_read;
   }

   return nb_read;
}

static size_t write_to_mem(const uint8_t* src, size_t numBytes, void* dest)
{
   auto destStream = (MemStream*)dest;
   if(destStream->off + numBytes >= destStream->len)
	  return 0;

   if(numBytes)
   {
	  memcpy(destStream->buf + (size_t)destStream->off, src, numBytes);
	  destStream->off += numBytes;
   }
   return numBytes;
}

static bool seek_from_mem(uint64_t numBytes, void* src)
{
   auto srcStream = (MemStream*)src;

   if(numBytes < srcStream->len)
	  srcStream->off = numBytes;
   else
	  srcStream->off = srcStream->len;

   return true;
}

/**
 * Set the given function to be used as a zero copy read function.
 * NOTE: this feature is only available for memory mapped and buffer backed streams,
 * not file streams
 *
 * @param		stream	stream to modify (see @ref grk_stream)
 * @param		func	function to use as read function. (see @ref grk_stream_zero_copy_read_fn)
 */
static void grk_stream_set_zero_copy_read_function(grk_stream* stream,
												   grk_stream_zero_copy_read_fn func)
{
   auto streamImpl = BufferedStream::getImpl(stream);
   if((!streamImpl) || (!(streamImpl->getStatus() & GROK_STREAM_STATUS_INPUT)))
	  return;
   streamImpl->setZeroCopyReadFunction(func);
}

void set_up_mem_stream(grk_stream* stream, size_t len, bool is_read_stream)
{
   grk_stream_set_user_data_length(stream, len);
   if(is_read_stream)
   {
	  grk_stream_set_read_function(stream, read_from_mem);
	  grk_stream_set_zero_copy_read_function(stream, zero_copy_read_from_mem);
   }
   else
	  grk_stream_set_write_function(stream, write_to_mem);
   grk_stream_set_seek_function(stream, seek_from_mem);
}

size_t get_mem_stream_offset(grk_stream* stream)
{
   if(!stream)
	  return 0;
   auto bufferedStream = BufferedStream::getImpl(stream);
   if(!bufferedStream->getUserData())
	  return 0;
   auto buf = (MemStream*)bufferedStream->getUserData();

   return buf->off;
}

grk_stream* create_mem_stream(uint8_t* buf, size_t len, bool ownsBuffer, bool is_read_stream)
{
   if(!buf || !len)
	  return nullptr;
   if(len < 12)
   {
	  Logger::logger_.error("Buffer of length %d is invalid\n", len);
	  return nullptr;
   }
   GRK_CODEC_FORMAT format;
   if(is_read_stream && !grk_decompress_buffer_detect_format(buf, len, &format))
	  return nullptr;

   auto memStream = new MemStream(buf, 0, len, ownsBuffer);
   auto streamImpl = new BufferedStream(buf, len, is_read_stream);
   if(is_read_stream)
	  streamImpl->setFormat(format);
   auto stream = streamImpl->getWrapper();
   grk_stream_set_user_data((grk_stream*)stream, memStream, free_mem);
   set_up_mem_stream((grk_stream*)stream, memStream->len, is_read_stream);

   return (grk_stream*)stream;
}

} // namespace grk
