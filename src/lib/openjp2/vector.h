/**
*    Copyright (C) 2016-2017 Grok Image Compression Inc.
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
	grok_vec_t() : data(nullptr) {}

	bool init()
	{
		if (data)
			return true;
		data = new std::vector<min_buf_t*>();
		return data ? true : false;
	}

	bool push_back(min_buf_t* value)
	{
		data->push_back(value);
		return true;
	}

	void* get(size_t index)
	{
		if (!data)
			return nullptr;
		assert(index < data->size());
		if (index >= data->size()) {
			return nullptr;
		}
		return data->operator[](index);
	}

	int32_t size()
	{
		if (!data)
			return 0;
		return (int32_t)data->size();
	}

	void* back()
	{
		if (!data)
			return nullptr;
		return data->back();
	}

	void cleanup()
	{
		if (!data)
			return;
		for (auto it = data->begin(); it != data->end(); ++it) {
			if (*it)
				grok_free(*it);
		}
		delete data;
		data = nullptr;
	}
	std::vector<min_buf_t*>* data;		/* array of void* pointers */
};


}

