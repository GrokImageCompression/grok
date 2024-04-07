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

#pragma once

#include "grk_config_private.h"
#include "IBitIO.h"

namespace grk
{
#define GROK_STREAM_STATUS_OUTPUT 0x1U
#define GROK_STREAM_STATUS_INPUT 0x2U
#define GROK_STREAM_STATUS_END 0x4U
#define GROK_STREAM_STATUS_ERROR 0x8U

struct BufferedStream
{
   friend GrkObjectWrapperImpl<BufferedStream>;
   BufferedStream(uint8_t* buffer, size_t buffer_size, bool l_is_input);

   static BufferedStream* getImpl(grk_stream* stream);
   grk_stream* getWrapper(void);

   void setUserData(void* data, grk_stream_free_user_data_fn freeUserDataFun);
   void* getUserData(void);
   void setUserDataLength(uint64_t len);
   uint32_t getStatus(void);
   void setReadFunction(grk_stream_read_fn fn);
   void setZeroCopyReadFunction(grk_stream_zero_copy_read_fn fn);
   void setWriteFunction(grk_stream_write_fn fn);
   void setSeekFunction(grk_stream_seek_fn fn);
   /**
	* Reads some bytes from the stream.
	* @param		buffer	pointer to the data buffer
	* 							that will receive the data.
	* @param		p_size		number of bytes to read.

	* @return		the number of bytes read
	*/
   size_t read(uint8_t* buffer, size_t p_size);

   // low-level write methods (endian taken into account)
   bool writeShort(uint16_t value);
   bool write24(uint32_t value);
   bool writeInt(uint32_t value);
   bool write64(uint64_t value);

   // low-level write methods (endian NOT taken into account)
   bool writeByte(uint8_t value);
   /**
	* Write bytes to stream (no correction for endian!).
	* @param		buffer	pointer to the data buffer holds the data
	* 							to be written.
	* @param		p_size		number of bytes to write.

	* @return		the number of bytes written
	*/
   size_t writeBytes(const uint8_t* buffer, size_t p_size);

   /**
	* Flush stream to disk

	* @return		true if the data could be flushed, otherwise false.
	*/
   bool flush();
   /**
	* Skip bytes in stream.
	* @param		p_size		the number of bytes to skip.

	* @return		true if successful, otherwise false
	*/
   bool skip(int64_t p_size);
   /**
	* Tells byte offset of stream (similar to ftell).
	*
	* @return		the current position of the stream.
	*/
   uint64_t tell(void);
   /**
	* Get the number of bytes left before end of stream
	*
	* @return		Number of bytes left before the end of the stream.
	*/
   uint64_t numBytesLeft(void);
   /**
	* Seek bytes from the stream (absolute)
	* @param		offset		the number of bytes to skip.
	*
	* @return		true if successful, otherwise false
	*/
   bool seek(uint64_t offset);
   /**
	* Check if stream is seekable.
	*/
   bool hasSeek();
   bool supportsZeroCopy();
   uint8_t* getZeroCopyPtr();

   void setFormat(GRK_CODEC_FORMAT format);
   GRK_CODEC_FORMAT getFormat(void);

 private:
   ~BufferedStream();
   /**
	* Skip bytes in write stream.
	* @param		p_size		the number of bytes to skip.

	* @return		true if successful, otherwise false
	*/
   bool write_skip(int64_t p_size);

   /**
	* Skip bytes in read stream.
	* @param		p_size		the number of bytes to skip.

	* @return		true if successful, otherwise false
	*/
   bool read_skip(int64_t p_size);

   /**
	* Absolute seek in read stream.
	* @param		offset		absolute offset

	* @return		true if successful, otherwise false
	*/
   bool read_seek(uint64_t offset);

   /**
	* Absolute seek in write stream.
	* @param		offset		absolute offset
	* @return		true if successful, otherwise false
	*/
   bool write_seek(uint64_t offset);

   void writeIncrement(size_t p_size);
   template<typename TYPE>
   bool write(TYPE value, uint8_t numBytes);
   void invalidate_buffer();

   bool isMemStream();

   grk_object obj;

   /**
	* user data
	*/
   void* user_data_;
   /**
	* Pointer to function to free user_data_ (nullptr at initialization)
	* when destroying the stream. If pointer is nullptr the function is not
	* called and the user_data_ is not freed (even if it isn't nullptr).
	*/
   grk_stream_free_user_data_fn free_user_data_fn_;
   /**
	* User data length.
	* Currently set to size of file for file read stream,
	* and size of buffer for buffer read/write stream
	*/
   uint64_t user_data_length_;
   /**
	* Pointer to actual read function (nullptr at initialization).
	*/
   grk_stream_read_fn read_fn_;
   /**
	* Pointer to actual zero copy read function (nullptr at initialization).
	*/
   grk_stream_zero_copy_read_fn zero_copy_read_fn_;
   /**
	* Pointer to actual write function (nullptr at initialization).
	*/
   grk_stream_write_fn write_fn_;
   /**
	* Pointer to actual seek function (if available).
	*/
   grk_stream_seek_fn seek_fn_;
   /**
	* Stream status flags
	*/
   uint32_t status_;

   grk_buf8* buf_;

   // number of bytes read in, or slated for write
   size_t buffered_bytes_;

   // number of seekable bytes in buffer. This will equal
   // the number of bytes
   // read in the last media read.
   // We always have buffered_bytes_ <= read_bytes_seekable_
   size_t read_bytes_seekable_;

   // number of bytes read/written from the beginning of the stream
   uint64_t stream_offset_;

   GRK_CODEC_FORMAT format_;
};

template<typename TYPE>
void grk_write(uint8_t* buffer, TYPE value, uint32_t numBytes)
{
   if(numBytes == 0)
	  return;
   assert(numBytes <= sizeof(TYPE));
#if defined(GROK_BIG_ENDIAN)
   const uint8_t* dataPtr = ((const uint8_t*)&value) + sizeof(TYPE) - numBytes;
   memcpy(buffer, dataPtr, numBytes);
#else
   const uint8_t* dataPtr = ((const uint8_t*)&value) + (size_t)(numBytes - 1);
   for(uint32_t i = 0; i < numBytes; ++i)
	  *(buffer++) = *(dataPtr--);
#endif
}

template<typename TYPE>
void grk_write(uint8_t* buffer, TYPE value)
{
   grk_write<TYPE>(buffer, value, sizeof(TYPE));
}

template<typename TYPE>
void grk_read(const uint8_t* buffer, TYPE* value, uint32_t numBytes)
{
   assert(numBytes > 0 && numBytes <= sizeof(TYPE));
#if defined(GROK_BIG_ENDIAN)
   auto dataPtr = ((uint8_t*)value);
   *value = 0;
   memcpy(dataPtr + sizeof(TYPE) - numBytes, buffer, numBytes);
#else
   auto dataPtr = ((uint8_t*)value) + numBytes - 1;
   *value = 0;
   for(uint32_t i = 0; i < numBytes; ++i)
	  *(dataPtr--) = *(buffer++);
#endif
}

template<typename TYPE>
void grk_read(const uint8_t* buffer, TYPE* value)
{
   grk_read<TYPE>(buffer, value, sizeof(TYPE));
}

} // namespace grk
