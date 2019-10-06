/**
 *    Copyright (C) 2016-2019 Grok Image Compression Inc.
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

 1) Canvas coordinate system:  jpeg 2000 global image coordinates

 2) Tile coordinate system:  coordinates relative to a tile's top left hand corner

 3) Resolution coordinate system:  coordinates relative to a resolution's top left hand corner

 4) Sub-band coordinate system: coordinates relative to a particular sub-band's top left hand corner

 */

struct grk_tile_buf_band {
	grk_rect dim; /* coordinates of sub-band region (canvas coordinates)  */
	grk_rect data_dim; /* coordinates of sub-band data region, (tile coordinates ) */
};

struct grk_tile_buf_resolution {
	grk_tile_buf_band band_region[3];
	uint32_t num_bands;
	grk_pt origin; /* resolution origin, in canvas coordinates */
	grk_pt bounds; /* full width and height of resolution */
};

struct grk_tile_buf_component {
	std::vector<grk_tile_buf_resolution*> resolutions;
	int32_t *data;
	uint64_t data_size_needed; /* we may either need to allocate this amount of data,
	 or re-use image data and ignore this value */
	uint64_t data_size; /* size of the data of the component */
	bool owns_data; /* true if tile buffer manages its data array, false otherwise */

	grk_rect dim; /* canvas coordinates of region */
	grk_rect tile_dim; /* canvas coordinates of tile */

};

/* offsets are in canvas coordinate system*/
int32_t* tile_buf_get_ptr(grk_tile_buf_component *buf, uint32_t resno,
		uint32_t bandno, uint32_t offsetx, uint32_t offsety);

bool tile_buf_alloc_component_data_decode(grk_tile_buf_component *buf);

bool tile_buf_alloc_component_data_encode(grk_tile_buf_component *buf);

bool tile_buf_is_decode_region(grk_tile_buf_component *buf);

void tile_buf_destroy_component(grk_tile_buf_component *comp);

/* Check if rect overlaps with region.
 rect coordinates must be stored in canvas coordinates
 */
bool tile_buf_hit_test(grk_tile_buf_component *comp, grk_rect *rect);

/* sub-band coordinates */
grk_pt tile_buf_get_uninterleaved_range(grk_tile_buf_component *comp,
		uint32_t resno, bool is_even, bool is_horizontal);

/* resolution coordinates */
grk_pt tile_buf_get_interleaved_range(grk_tile_buf_component *comp, uint32_t resno,
		bool is_horizontal);

int64_t tile_buf_get_interleaved_upper_bound(grk_tile_buf_component *comp);

}
