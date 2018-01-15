/**
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

#pragma once
#include <vector>

namespace grk {

struct min_buf_t;

struct grok_vec_t {
	grok_vec_t();
	bool init();
	bool push_back(min_buf_t* value);
	void* get(size_t index);
	int32_t size();
	void* back();
	void cleanup();

	/*
	Copy all segments, in sequence, into contiguous array
	*/
	bool copy_to_contiguous_buffer(uint8_t* buffer);

	/*
	Push buffer to back of min buf vector
	*/
	bool push_back(uint8_t* buf, uint16_t len);

	/*
	Sum lengths of all buffers
	*/

	uint16_t get_len(void);




	std::vector<min_buf_t*>* data;
};


}

