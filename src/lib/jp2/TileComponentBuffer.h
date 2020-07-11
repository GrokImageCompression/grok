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
	grk_pt origin; /* resolution origin, in tile coordinates */
	grk_pt bounds; /* width and height of resolution in tile coordinates */
	uint32_t num_bands;
	grk_rect bands[3]; // tile coordinates
};

template<typename T> struct TileComponentBuffer {
	TileComponentBuffer(grk_image *output_image,
						uint32_t dx,uint32_t dy,
						grk_rect unreduced_dim,
						grk_rect reduced_dim,
						uint32_t reduced_num_resolutions,
						uint32_t numresolutions,
						grk_resolution *tile_comp_resolutions) :
							reduced_region_dim(reduced_dim),
							unreduced_tile_comp_dim(unreduced_dim),
							data(nullptr),
							data_size(0),
							owns_data(false),
							m_encode(output_image==nullptr)
	{
		//note: only decoder has output image
		if (output_image) {
			// tile component coordinates
			unreduced_region_dim = grk_rect(ceildiv<uint32_t>(output_image->x0, dx),
										ceildiv<uint32_t>(output_image->y0, dy),
										ceildiv<uint32_t>(output_image->x1, dx),
										ceildiv<uint32_t>(output_image->y1, dy));

			reduced_region_dim 	= unreduced_region_dim;
			reduced_region_dim.ceildivpow2(numresolutions - reduced_num_resolutions);

			/* clip region dimensions against tile */
			reduced_dim.clip(reduced_region_dim, &reduced_region_dim);
			unreduced_tile_comp_dim.clip(unreduced_region_dim, &unreduced_region_dim);

			/* fill resolutions vector */
	        assert(reduced_num_resolutions>0);

	        for (uint32_t resno = 0; resno < reduced_num_resolutions; ++resno)
	        	resolutions.push_back(tile_comp_resolutions+resno);
		}
	}
	~TileComponentBuffer(){
		if (owns_data)
			grk_aligned_free(data);
	}

	T* get_ptr(uint32_t resno,uint32_t bandno, uint32_t offsetx, uint32_t offsety) const {
		(void) resno;
		(void) bandno;
		return data + (uint64_t) offsetx
				+ offsety * (uint64_t) (reduced_region_dim.x1 - reduced_region_dim.x0);
	}
	bool alloc(){
		uint64_t data_size_needed = data_dim().area() * sizeof(T);
		if (!data && !data_size_needed)
			return true;
		if (!data) {
			if (owns_data)
				grk_aligned_free(data);
			data_size = 0;
			owns_data = false;
			data = (T*) grk_aligned_malloc(data_size_needed);
			if (!data)
				return false;
			if (!m_encode)
				memset(data, 0, data_size_needed);
			data_size = data_size_needed;
			owns_data = true;
		}

		return true;
	}

	grk_rect data_dim(){
		return m_encode ? unreduced_tile_comp_dim : reduced_region_dim;
	}
	// set data to buf without owning it
	void attach(T* buf){
		data = buf;
		owns_data = false;
	}
	// set data to buf and own it
	void acquire(T* buf){
		if (owns_data)
			grk_aligned_free(data);
		data = buf;
		owns_data = true;
	}
	// transfer data to buf, and cease owning it
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
	grk_rect unreduced_tile_comp_dim;

	std::vector<grk_resolution*> resolutions;

	T *data;
	uint64_t data_size; /* size of the data of the component */
	bool owns_data; /* true if tile buffer manages its data array, false otherwise */
	bool m_encode;
};


}
