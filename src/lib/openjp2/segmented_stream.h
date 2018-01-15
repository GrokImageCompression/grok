/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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
*    This source code incorporates work covered by the following copyright and
*    permission notice:
*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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


/*
Smart wrapper to low level C array
*/

struct min_buf_t {
	min_buf_t() : buf(nullptr), len(0) {}
	min_buf_t(uint8_t* buffer, uint16_t length) : buf(buffer), 
												  len(length) {}
    uint8_t *buf;		/* internal array*/
    uint16_t len;		/* length of array */
};


/*  Segmented Buffer Interface

A segmented buffer stores a list of buffers, or segments, but can be treated as one single
contiguous buffer.

*/
struct seg_buf_t {
	seg_buf_t();
	~seg_buf_t();

	/*
	Wrap existing array and add to the back of the segmented buffer.
	*/
	bool push_back( uint8_t* buf, size_t len);

	/*
	Allocate array and add to the back of the segmented buffer
	*/
	bool alloc_and_push_back( size_t len);

	/*
	Increment offset of current segment
	*/
	void incr_cur_seg_offset( uint64_t offset);

	/*
	Get length of current segment
	*/
	size_t get_cur_seg_len(void);

	/*
	Get offset of current segment
	*/
	int64_t get_cur_seg_offset(void);

	/*
	Treat segmented buffer as single contiguous buffer, and get current pointer
	*/
	uint8_t* get_global_ptr(void);

	/*
	Treat segmented buffer as single contiguous buffer, and get current offset
	*/
	int64_t get_global_offset(void);

	/*
	Reset all offsets to zero, and set current segment to beginning of list
	*/
	void rewind(void);

	void increment(void);

	size_t read(void * p_buffer, size_t p_nb_bytes);

	buf_t* add_segment(uint8_t* buf, size_t len, bool ownsData);
	void	add_segment(buf_t* seg);

	/*
	Copy all segments, in sequence, into contiguous array
	*/
	bool copy_to_contiguous_buffer( uint8_t* buffer);

	/*
	Cleans up internal resources
	*/
	void	cleanup(void);

	/*
	Return current pointer, stored in ptr variable, and advance segmented buffer
	offset by chunk_len
	*/
	bool zero_copy_read(uint8_t** ptr,	size_t chunk_len);


    size_t data_len;	/* total length of all segments*/
    size_t cur_seg_id;	/* current index into segments vector */
	std::vector<buf_t*> segments;
};



}
