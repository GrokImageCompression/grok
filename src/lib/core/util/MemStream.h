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

#pragma once
namespace grk
{
#ifdef _WIN32
typedef void* grk_handle;
#else
typedef int32_t grk_handle;
#endif

/*
 * Callback function prototype for zero copy read function
 */
typedef size_t (*grk_stream_zero_copy_read_fn)(uint8_t** buffer, size_t numBytes, void* user_data);

struct MemStream
{
   MemStream(uint8_t* buffer, size_t offset, size_t length, bool owns);
   MemStream();
   ~MemStream();
   uint8_t* buf;
   size_t off;
   size_t len;
   grk_handle fd; // for file mapping
   bool ownsBuffer;
};

void set_up_mem_stream(grk_stream* stream, size_t len, bool is_read_stream);

/** Create stream from buffer
 *
 * @param buf           buffer
 * @param len    length of buffer
 * @param ownsBuffer    if true, library will delete[] buffer. Otherwise, it is the caller's
 *                      responsibility to delete the buffer
 * @param is_read_stream  whether the stream is a read stream (true) or not (false)
 */
grk_stream* create_mem_stream(uint8_t* buf, size_t len, bool ownsBuffer, bool is_read_stream);

size_t get_mem_stream_offset(grk_stream* stream);

} // namespace grk
