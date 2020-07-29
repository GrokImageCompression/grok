/**
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
#include "grok_includes.h"

namespace grk {



grk_buf::~grk_buf() {
	dealloc();
}

void grk_buf::dealloc(){
	if (buf && owns_data)
		delete[] buf;
	buf = nullptr;
	owns_data = false;
	offset = 0;
	len = 0;
}

void grk_buf::incr_offset(ptrdiff_t off) {
	/*  we allow the offset to move to one location beyond end of buffer segment*/
	if (off > 0 ){
		if (offset > (size_t)(SIZE_MAX - (size_t)off)){
			GROK_WARN("grk_buf: overflow");
			offset = len;
		} else if (offset + (size_t)off > len){
	#ifdef DEBUG_SEG_BUF
		   GROK_WARN("grk_buf: attempt to increment buffer offset out of bounds");
	#endif
			offset = len;
		} else {
			offset = offset + (size_t)off;
		}
	}
	else if (off < 0){
		if (offset < (size_t)(-off)) {
			GROK_WARN("grk_buf: underflow");
			offset = 0;
		} else {
			offset = (size_t)((ptrdiff_t)offset + off);
		}
	}

}

uint8_t* grk_buf::curr_ptr(){
	if (!buf)
		return nullptr;
	return buf + offset;
}

}
