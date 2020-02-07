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

#include <vector>

#pragma once
namespace grk {

/*  ChunkBuffer

 Store a list of buffers, or chunks, which can be treated as one single
 contiguous buffer.

 */
struct ChunkBuffer {
	ChunkBuffer();
	~ChunkBuffer();

	/*
	 Wrap existing array and add to the back of the segmented buffer.
	 */
	bool push_back(uint8_t *buf, size_t len);

	/*
	 Allocate array and add to the back of the segmented buffer
	 */
	bool alloc_and_push_back(size_t len);

	/*
	 Increment offset of current chunk
	 */
	void incr_cur_chunk_offset(uint64_t offset);

	/*
	 Get length of current chunk
	 */
	size_t get_cur_chunk_len(void);

	/*
	 Get offset of current chunk
	 */
	int64_t get_cur_chunk_offset(void);

	/*
	 Treat segmented buffer as single contiguous buffer, and get current pointer
	 */
	uint8_t* get_global_ptr(void);

	/*
	 Treat segmented buffer as single contiguous buffer, and get current offset
	 */
	int64_t get_global_offset(void);

	/*
	 Reset all offsets to zero, and set current chunk to beginning of list
	 */
	void rewind(void);

	int64_t skip(int64_t nb_bytes);

	void increment(void);

	size_t read(void *p_buffer, size_t nb_bytes);

	grk_buf* add_chunk(uint8_t *buf, size_t len, bool ownsData);
	void add_chunk(grk_buf *seg);

	/*
	 Copy all segments, in sequence, into contiguous array
	 */
	bool copy_to_contiguous_buffer(uint8_t *buffer);

	/*
	 Cleans up internal resources
	 */
	void cleanup(void);

	/*
	 Return current pointer, stored in ptr variable, and advance segmented buffer
	 offset by chunk_len
	 */
	bool zero_copy_read(uint8_t **ptr, size_t chunk_len);

	size_t data_len; /* total length of all chunks*/
	size_t cur_chunk_id; /* current index into chunk vector */
	std::vector<grk_buf*> chunks;
};

}
