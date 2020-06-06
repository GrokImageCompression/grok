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

#include "util.h"
#include "grok_intmath.h"
#include "TagTree.h"
#include "TileProcessor.h"
#include <stdexcept>

namespace grk {



/*
 Note: various coordinate systems are used to describe regions in the tile buffer.

 1) Canvas coordinate system:  JPEG 2000 global image coordinates, independent of sub-sampling

 2) Tile coordinate system:  coordinates relative to a tile's top left hand corner, with
 sub-sampling accounted for

 3) Resolution coordinate system:  coordinates relative to a resolution's top left hand corner

 4) Sub-band coordinate system: coordinates relative to a particular sub-band's top left hand corner

 */

struct TileComponentBufferResolution {
	grk_rect band_region[3]; // tile coordinates
	uint32_t num_bands;
	grk_pt origin; /* resolution origin, in tile coordinates */
	grk_pt bounds; /* width and height of resolution in tile coordinates */
};

template<typename T> struct TileComponentBuffer {
	TileComponentBuffer(grk_image *output_image,
						uint32_t dx,uint32_t dy,
						grk_rect unreduced_dim,
						grk_rect reduced_dim,
						uint32_t minimum_num_resolutions,
						uint32_t numresolutions,
						grk_resolution *tile_resolutions) :
							reduced_region_dim(reduced_dim),
							unreduced_tile_dim(unreduced_dim),
							reduced_tile_dim(reduced_dim),
							data(nullptr),
							data_size(0),
							owns_data(false)
	{
		//note: only decoder has output image
		if (output_image) {
			// tile component coordinates
			unreduced_region_dim = grk_rect(ceildiv<uint32_t>(output_image->x0, dx),
										ceildiv<uint32_t>(output_image->y0, dy),
										ceildiv<uint32_t>(output_image->x1, dx),
										ceildiv<uint32_t>(output_image->y1, dy));

			reduced_region_dim 	= unreduced_region_dim;
			reduced_region_dim.ceildivpow2(numresolutions - minimum_num_resolutions);

			/* clip output image to tile */
			reduced_tile_dim.clip(reduced_region_dim, &reduced_region_dim);
			unreduced_tile_dim.clip(unreduced_region_dim, &unreduced_region_dim);

			/* fill resolutions vector */
	        assert(numresolutions>0);
			TileComponentBufferResolution *prev_res = nullptr;
			for (int32_t resno = (int32_t) (numresolutions - 1); resno >= 0; --resno) {
				auto res = tile_resolutions + resno;
				auto tile_buffer_res = (TileComponentBufferResolution*) grk_calloc(1,
						sizeof(TileComponentBufferResolution));
				if (!tile_buffer_res)
					throw new std::runtime_error("Out of memory");

				tile_buffer_res->bounds.x = res->x1 - res->x0;
				tile_buffer_res->bounds.y = res->y1 - res->y0;
				tile_buffer_res->origin.x = res->x0;
				tile_buffer_res->origin.y = res->y0;

				// we don't reduce resolutions when encoding
				grk_rect max_image_dim = unreduced_tile_dim ;
				for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
					auto band = res->bands + bandno;
					grk_rect band_rect;
					band_rect = grk_rect(band->x0, band->y0, band->x1, band->y1);

					tile_buffer_res->band_region[bandno] =
							prev_res ? prev_res->band_region[bandno] : max_image_dim;
					if (resno > 0) {

						/*For next level down, E' = ceil((E-b)/2) where b in {0,1} identifies band
						 * see Chapter 11 of Taubman and Marcellin for more details
						 * */
						grk_pt shift;
						shift.x = -(int64_t)(band->bandno & 1);
						shift.y = -(int64_t)(band->bandno >> 1);

						tile_buffer_res->band_region[bandno].pan(&shift);
						tile_buffer_res->band_region[bandno].ceildivpow2(1);
					}
				}
				tile_buffer_res->num_bands = res->numbands;
				resolutions.push_back(tile_buffer_res);
				prev_res = tile_buffer_res;
			}
		}


	}
	~TileComponentBuffer(){
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
		uint64_t data_size_needed = unreduced_tile_dim.area() * sizeof(T);
		if (!data 	|| (data_size_needed > data_size)) {
			if (owns_data)
				grk_aligned_free(data);
			data_size = 0;
			owns_data = false;
			data = (T*) grk_aligned_malloc(data_size_needed);
			if (!data)
				return false;
			data_size = data_size_needed;
			owns_data = true;
		}

		return true;
	}
	bool alloc_component_data_decode(){
		if (!data) {
			uint64_t data_size_needed = reduced_region_dim.area() * sizeof(T);
			if (data_size_needed) {
				data = (T*) grk_aligned_malloc(data_size_needed);
				if (!data)
					return false;
				memset(data, 0, data_size_needed);
				data_size = data_size_needed;
				owns_data = true;
			}
		}
		return true;
	}

	void attach(T* buf){
		data = buf;
		owns_data = false;
	}
	void acquire(T* buf){
		if (owns_data)
			grk_aligned_free(data);
		data = buf;
		owns_data = true;
	}
	void transfer(T** buf, bool* owns){
		if (buf && owns){
			*buf = data;
			data = nullptr;
			*owns = owns_data;
			owns_data = false;
		}
	}
	// unreduced tile component coordinates of region
	grk_rect unreduced_region_dim;

	 /* reduced tile component coordinates of region  */
	grk_rect reduced_region_dim;

private:

	/* unreduced tile component coordinates of tile */
	grk_rect unreduced_tile_dim;

	/* reduced tile component coordinates of tile */
	grk_rect reduced_tile_dim;


	std::vector<TileComponentBufferResolution*> resolutions;

	T *data;
	uint64_t data_size; /* size of the data of the component */
	bool owns_data; /* true if tile buffer manages its data array, false otherwise */
};


}
