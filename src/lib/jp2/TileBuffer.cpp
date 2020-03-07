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

TileBuffer::~TileBuffer(){
	if (data && owns_data)
		grok_aligned_free(data);
	data = nullptr;
	data_size = 0;
	data_size_needed = 0;
	for (auto &res : resolutions) {
		grok_free(res);
	}
	resolutions.clear();
}


int32_t* TileBuffer::get_ptr(uint32_t resno,
		uint32_t bandno, uint32_t offsetx, uint32_t offsety) {
	(void) resno;
	(void) bandno;
	auto dims = reduced_image_dim;
	return data + (uint64_t) offsetx
			+ (uint64_t) offsety * (dims.x1 - dims.x0);
}


bool TileBuffer::alloc_component_data_encode() {
	if ((data == nullptr)
			|| ((data_size_needed > data_size)
					&& (owns_data == false))) {
		data = (int32_t*) grok_aligned_malloc(data_size_needed);
		if (!data) {
			return false;
		}
		data_size = data_size_needed;
		owns_data = true;
	} else if (data_size_needed > data_size) {
		/* We don't need to keep old data */
		grok_aligned_free(data);
		data = (int32_t*) grok_aligned_malloc(data_size_needed);
		if (!data) {
			data_size = 0;
			data_size_needed = 0;
			owns_data = false;
			return false;
		}

		data_size = data_size_needed;
		owns_data = true;
	}
	return true;
}

bool TileBuffer::alloc_component_data_decode() {
	if (!data) {
		int64_t area = reduced_image_dim.area();
		if (area) {
			data = (int32_t*) grok_aligned_malloc(area * sizeof(int32_t));
			if (!data) {
				return false;
			}
			memset(data, 0, area * sizeof(int32_t));
		}
		data_size = area * sizeof(int32_t);
		data_size_needed = data_size;
		owns_data = true;
	}
	return true;
}

}
