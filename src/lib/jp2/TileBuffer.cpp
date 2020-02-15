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
	return data + (uint64_t) offsetx
			+ (uint64_t) offsety * (tile_dim.x1 - tile_dim.x0);
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
		int64_t area = tile_dim.get_area();
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


bool TileBuffer::hit_test(grk_rect &rect) {
	for (auto &res : resolutions) {
		grk_rect dummy;
		uint32_t j;
		for (j = 0; j < res->num_bands; ++j) {
			if ((res->band_region + j)->canvas_coords.clip(rect, &dummy))
				return true;
		}
	}
	return false;
}

grk_pt TileBuffer::get_uninterleaved_range(	uint32_t resno, bool is_even, bool is_horizontal) {
	grk_pt rc;
	TileBufferResolution *res = nullptr;
	TileBufferResolution *prev_res = nullptr;
	TileBufferBand *band = nullptr;
	memset(&rc, 0, sizeof(grk_pt));

	res = resolutions[resolutions.size() - 1 - resno];
	if (!res)
		return rc;

	prev_res = resolutions[resolutions.size() - 1 - resno + 1];

	if (resno == 0) {
		band = res->band_region;
	} else {
		if (!is_even) {
			band = res->band_region + 2;
		} else {
			band = is_horizontal ? res->band_region + 1 : res->band_region;
		}
	}

	if (is_horizontal) {
		rc.x = band->canvas_coords.x0 - prev_res->origin.x;
		rc.y = band->canvas_coords.x1 - prev_res->origin.x;
	} else {
		rc.x = band->canvas_coords.y0 - prev_res->origin.y;
		rc.y = band->canvas_coords.y1 - prev_res->origin.y;
	}

	/* clip */
	rc.x = std::max<int64_t>(0, rc.x);

	/* if resno == 0, then prev_res is null */
	if (resno == 0) {
		rc.y = std::min<int64_t>(rc.y,
				is_horizontal ? res->bounds.x : res->bounds.y);
	} else {
		if (is_even)
			rc.y = std::min<int64_t>(rc.y,
					is_horizontal ? prev_res->bounds.x : prev_res->bounds.y);
		else
			rc.y = std::min<int64_t>(rc.y,
					is_horizontal ?
							res->bounds.x - prev_res->bounds.x :
							res->bounds.y - prev_res->bounds.y);

	}

	return rc;

}

grk_pt TileBuffer::get_interleaved_range(uint32_t resno,
		bool is_horizontal) {
	grk_pt rc;
	grk_pt even;
	grk_pt odd;
	TileBufferResolution *res = nullptr;
	memset(&rc, 0, sizeof(grk_pt));

	res = resolutions[resolutions.size() - 1 - resno];
	if (!res)
		return rc;

	even = get_uninterleaved_range(resno, true, is_horizontal);
	odd = get_uninterleaved_range(resno, false, is_horizontal);

	rc.x = std::min<int64_t>((even.x << 1), (odd.x << 1) + 1);
	rc.y = std::max<int64_t>((even.y << 1), (odd.y << 1) + 1);

	/* clip to resolution bounds */
	rc.x = std::max<int64_t>(0, rc.x);
	rc.y = std::min<int64_t>(rc.y,
			is_horizontal ? res->bounds.x : res->bounds.y);
	return rc;
}

int64_t TileBuffer::get_interleaved_upper_bound() {
	if (resolutions.empty()) {
		return 0;
	}
	grk_pt horizontal = get_interleaved_range((uint32_t) resolutions.size() - 1, true);
	grk_pt vertical = get_interleaved_range((uint32_t) resolutions.size() - 1, false);

	return std::max<int64_t>(horizontal.y, vertical.y);
}


}
