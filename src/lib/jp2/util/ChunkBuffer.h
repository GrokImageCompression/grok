/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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



	grkBufferU8* push_back(uint8_t *buf, size_t len, bool ownsData);

	/*
	 Allocate array and add to the back of the chunk buffer
	 */
	bool alloc_and_push_back(size_t len);

	/*
	 Increment offset of current chunk
	 */
	void incr_cur_chunk_offset(size_t offset);

	/*
	 Get length of current chunk
	 */
	size_t get_cur_chunk_len(void);

	/*
	 Treat segmented buffer as single contiguous buffer, and get current pointer
	 */
	uint8_t* get_cur_chunk_ptr(void);

	/*
	 Reset all offsets to zero, and set current chunk to beginning of list
	 */
	void rewind(void);

	size_t skip(size_t nb_bytes);

	void increment(void);

	size_t read(void *p_buffer, size_t nb_bytes);

private:
	/*
	 Treat segmented buffer as single contiguous buffer, and get current offset
	 */
	size_t get_global_offset(void);


	/*
	 Copy all chunks, in sequence, into contiguous array
	 */
	bool copyToContiguousBuffer(uint8_t *buffer);

	/*
	 Clean up internal resources
	 */
	void cleanup(void);


	/*
	 Return current pointer, stored in ptr variable, and advance chunk buffer
	 offset by chunk_len
	 */
	bool zero_copy_read(uint8_t **ptr, size_t chunk_len);


	/*
	 Get offset of current chunk
	 */
	size_t get_cur_chunk_offset(void);

	void push_back(grkBufferU8 *chunk);

	size_t data_len; /* total length of all chunks*/
	size_t cur_chunk_id; /* current index into chunk vector */
	std::vector<grkBufferU8*> chunks;
};

}
