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

#include "grok_includes.h"

namespace grk {

grok_vec_t::grok_vec_t() : data(nullptr) {}

bool grok_vec_t::init()	{
	if (data)
		return true;
	data = new std::vector<min_buf_t*>();
	return data ? true : false;
}
bool grok_vec_t::push_back(min_buf_t* value){
	data->push_back(value);
	return true;
}
void* grok_vec_t::get(size_t index)	{
	if (!data)
		return nullptr;
	assert(index < data->size());
	if (index >= data->size()) {
		return nullptr;
	}
	return data->operator[](index);
}
int32_t grok_vec_t::size()	{
	if (!data)
		return 0;
	return (int32_t)data->size();
}
void* grok_vec_t::back()	{
	if (!data)
		return nullptr;
	return data->back();
}
void grok_vec_t::cleanup()	{
	if (!data)
		return;
	for (auto it = data->begin(); it != data->end(); ++it) {
		if (*it)
			delete (*it);
	}
	delete data;
	data = nullptr;
}

bool grok_vec_t::copy_to_contiguous_buffer(uint8_t* buffer)
{
	if (!buffer) {
		return false;
	}
	size_t offset = 0;
	for (int32_t i = 0; i < size(); ++i) {
		min_buf_t* seg = (min_buf_t*)get(i);
		if (seg->len)
			memcpy(buffer + offset, seg->buf, seg->len);
		offset += seg->len;
	}
	return true;
}
bool grok_vec_t::push_back(uint8_t* buf, uint16_t len)
{
	if ( !buf || !len)
		return false;

	if (!data) {
		init();
	}
	min_buf_t* seg = new min_buf_t(buf, len);
	if (!push_back(seg)) {
		delete seg;
		return false;
	}
	return true;
}

uint16_t grok_vec_t::get_len(void) {
	uint16_t len = 0;
	if (!data)
		return 0;
	for (int32_t i = 0; i < size(); ++i) {
		min_buf_t* seg = (min_buf_t*)get(i);
		if (seg)
			len = static_cast<uint16_t>((uint32_t)len + seg->len);
	}
	return len;

}



}

