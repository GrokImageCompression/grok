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

grk_vec::grk_vec() :
		data(nullptr) {
}

bool grk_vec::init() {
	if (data)
		return true;
	data = new std::vector<grk_buf*>();
	return data ? true : false;
}
bool grk_vec::push_back(grk_buf *value) {
	data->push_back(value);
	return true;
}
void* grk_vec::get(size_t index) {
	if (!data)
		return nullptr;
	assert(index < data->size());
	if (index >= data->size()) {
		return nullptr;
	}
	return data->operator[](index);
}
int32_t grk_vec::size() {
	if (!data)
		return 0;
	return (int32_t) data->size();
}
void* grk_vec::back() {
	if (!data)
		return nullptr;
	return data->back();
}
void grk_vec::cleanup() {
	if (!data)
		return;
	for (auto it = data->begin(); it != data->end(); ++it) {
		if (*it)
			delete (*it);
	}
	delete data;
	data = nullptr;
}

bool grk_vec::copy_to_contiguous_buffer(uint8_t *buffer) {
	if (!buffer) {
		return false;
	}
	size_t offset = 0;
	for (int32_t i = 0; i < size(); ++i) {
		grk_buf *seg = (grk_buf*) get(i);
		if (seg->len)
			memcpy(buffer + offset, seg->buf, seg->len);
		offset += seg->len;
	}
	return true;
}
bool grk_vec::push_back(uint8_t *buf, size_t len) {
	if (!buf || !len)
		return false;

	if (!data) {
		init();
	}
	grk_buf *seg = new grk_buf(buf, len, false);
	if (!push_back(seg)) {
		delete seg;
		return false;
	}
	return true;
}

size_t grk_vec::get_len(void) {
	size_t len = 0;
	if (!data)
		return 0;
	for (int32_t i = 0; i < size(); ++i) {
		grk_buf *seg = (grk_buf*) get(i);
		if (seg)
			len += seg->len;
	}
	return len;

}

}

