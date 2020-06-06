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

#pragma once
namespace grk {

/*
 Note: various coordinate systems are used to describe regions in the tile buffer.

 1) Canvas coordinate system:  JPEG 2000 global image coordinates, independent of sub-sampling

 2) Tile coordinate system:  coordinates relative to a tile's top left hand corner, with
 sub-sampling accounted for

 3) Resolution coordinate system:  coordinates relative to a resolution's top left hand corner

 4) Sub-band coordinate system: coordinates relative to a particular sub-band's top left hand corner

 */

struct TileBufferResolution {
	grk_rect band_region[3]; // tile coordinates
	uint32_t num_bands;
	grk_pt origin; /* resolution origin, in tile coordinates */
	grk_pt bounds; /* width and height of resolution in tile coordinates */
};

template<typename T> struct TileBuffer {
	TileBuffer() : data(nullptr), data_size(0), owns_data(false)
	{}
	~TileBuffer(){
		if (owns_data)
			grk_aligned_free(data);
		for (auto &res : resolutions) {
			grok_free(res);
		}
	}

	T* get_ptr(uint32_t resno,uint32_t bandno, uint32_t offsetx, uint32_t offsety) const {
		(void) resno;
		(void) bandno;
		auto dims = reduced_region_dim;
		return data + (uint64_t) offsetx
				+ offsety * (uint64_t) (dims.x1 - dims.x0);
	}
	bool alloc_component_data_encode(){
		if ((data == nullptr)
				|| ((data_size_needed > data_size)
						&& (owns_data == false))) {
			data = (T*) grk_aligned_malloc(data_size_needed);
			if (!data) {
				return false;
			}
			data_size = data_size_needed;
			owns_data = true;
		} else if (data_size_needed > data_size) {
			/* We don't need to keep old data */
			grk_aligned_free(data);
			data = (T*) grk_aligned_malloc(data_size_needed);
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
	bool alloc_component_data_decode(){
		if (!data) {
			uint64_t area = (uint64_t)reduced_region_dim.area();
			if (area) {
				data = (T*) grk_aligned_malloc(area * sizeof(T));
				if (!data)
					return false;
				memset(data, 0, area * sizeof(T));
			}
			data_size = area * sizeof(T);
			data_size_needed = data_size;
			owns_data = true;
		}
		return true;
	}

	void setData(T* buf){
		data = buf;
		owns_data = false;
	}
	void transferData(T* buf){
		if (owns_data)
			grk_aligned_free(data);
		data = buf;
		owns_data = true;
	}
	void transferData(T** buf, bool* owns){
		if (buf && owns){
			*buf = data;
			data = nullptr;
			*owns = owns_data;
			owns_data = false;
		}
	}
	 /* reduced tile coordinates of region  */
	grk_rect reduced_region_dim;

	// unreduced coordinates of region
	grk_rect unreduced_region_dim;


	 /* reduced tile coordinates of tile */
	grk_rect reduced_tile_dim;

	/* unreduced coordinates of tile */
	grk_rect unreduced_tile_dim;

	std::vector<TileBufferResolution*> resolutions;

	/* we may either need to allocate this amount of data,
	 or re-use image data and ignore this value */
	uint64_t data_size_needed;
private:
	T *data;
	uint64_t data_size; /* size of the data of the component */
	bool owns_data; /* true if tile buffer manages its data array, false otherwise */
};


}
