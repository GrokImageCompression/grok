/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
namespace grk {

#ifdef _WIN32
typedef void* grk_handle;
#else
typedef int32_t grk_handle;
#endif

struct buf_info {
	buf_info(uint8_t *buffer, size_t offset, size_t length, bool owns) :	buf(buffer),
																		off(offset),
																		len(length),
																		fd(0),
																		ownsBuffer(owns)
	{}
	buf_info() : buf_info(nullptr, 0, 0, false) {
	}
	~buf_info() {
		if (ownsBuffer)
			delete[] buf;
	}
	uint8_t *buf;
	size_t off;
	size_t len;
	grk_handle fd;		// for file mapping
	bool ownsBuffer;
};

void set_up_mem_stream(grk_stream *l_stream, size_t len,
		bool is_read_stream);
grk_stream  *  create_mem_stream(uint8_t *buf, size_t len, bool ownsBuffer,
		bool is_read_stream);
size_t get_mem_stream_offset( grk_stream  *stream);

/*
 * Callback function prototype for zero copy read function
 */
typedef size_t (*grk_stream_zero_copy_read_fn)(void **p_buffer, size_t nb_bytes,
		void *user_data);


}
