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


/*
Vector - a dynamic array.

*/

#include <vector>

struct opj_vec_t {
	opj_vec_t() : data(nullptr), owns_data(false) {}

	bool init(bool owns_data)
	{
		if (data)
			return true;

		data = (void*)(new std::vector<void*>());
		owns_data = owns_data;
		return data ? true : false;
	}

	bool push_back(void* value)
	{
		auto impl = (std::vector<void*>*)data;
		impl->push_back(value);
		return true;
	}

	void* get( size_t index)
	{
		if (!data)
			return NULL;
		auto impl = (std::vector<void*>*)data;
		assert(index < impl->size() && index >= 0);
		if (index >= impl->size()) {
			return NULL;
		}
		return impl->operator[](index);
	}

	int32_t size()
	{
		if (!data)
			return 0;
		auto impl = (std::vector<void*>*)data;
		return (int32_t)impl->size();
	}

	void* back()
	{
		if (!data)
			return NULL;
		auto impl = (std::vector<void*>*)data;
		return impl->back();
	}

	void cleanup()
	{
		if (!data)
			return;
		auto impl = (std::vector<void*>*)data;
		if (owns_data) {
			for (auto it = impl->begin(); it != impl->end(); ++it) {
				if (*it)
					opj_free(*it);
			}
		}
		delete impl;
		data = NULL;
	}
    void* data;		/* array of void* pointers */
    bool owns_data;
};

/*
Clean up vector resources and free vector itself
*/
void opj_vec_destroy(opj_vec_t *vec);


