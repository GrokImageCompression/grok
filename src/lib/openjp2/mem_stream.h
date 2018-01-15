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

#pragma once
namespace grk {

#ifdef _WIN32
	typedef void* grok_handle_t;
#else
	typedef int32_t grok_handle_t;
#endif


struct buf_info_t {
	buf_info_t() : buf_info_t(nullptr, 0, 0,false) {}
	buf_info_t(uint8_t *buffer,	
				int64_t offset,
				size_t length, 
				bool owns) : buf(buffer), 
							off(offset),
							len(length),
							fd(0), 
							ownsBuffer(owns)
	{}
	~buf_info_t() {
		if (ownsBuffer)
			delete[] buf;
	}
	uint8_t *buf;
	int64_t off;
	size_t len;
	grok_handle_t fd;		// for file mapping
	bool ownsBuffer;
};


opj_stream_t*  create_buffer_stream(uint8_t *buf,
                                        size_t len,
										bool ownsBuffer,
                                        bool p_is_read_stream);
size_t get_buffer_stream_offset(opj_stream_t* stream);

opj_stream_t* create_mapped_file_read_stream(const char *fname);


}