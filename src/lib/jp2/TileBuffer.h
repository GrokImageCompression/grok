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

 1) Canvas coordinate system:  jpeg 2000 global image coordinates, independant of sub-sampling

 2) Tile coordinate system:  coordinates relative to a tile's top left hand corner, with
 sub-sampling accounted for

 3) Resolution coordinate system:  coordinates relative to a resolution's top left hand corner

 4) Sub-band coordinate system: coordinates relative to a particular sub-band's top left hand corner

 */

struct TileBufferBand {
	grk_rect canvas_coords; /* coordinates of sub-band region (canvas coordinates)  */
};

struct TileBufferResolution {
	TileBufferBand band_region[3];
	uint32_t num_bands;
	grk_pt origin; /* resolution origin, in canvas coordinates */
	grk_pt bounds; /* full width and height of resolution */
};

struct TileBuffer {
	~TileBuffer();

	int32_t* get_ptr(uint32_t resno,uint32_t bandno, uint32_t offsetx, uint32_t offsety);
	bool alloc_component_data_encode();
	bool alloc_component_data_decode();
	grk_pt get_uninterleaved_range(	uint32_t resno, bool is_even, bool is_horizontal);
	grk_pt get_interleaved_range(uint32_t resno,bool is_horizontal) ;
	int64_t get_interleaved_upper_bound();

	std::vector<TileBufferResolution*> resolutions;
	int32_t *data;
	uint64_t data_size_needed; /* we may either need to allocate this amount of data,
	 or re-use image data and ignore this value */
	uint64_t data_size; /* size of the data of the component */
	bool owns_data; /* true if tile buffer manages its data array, false otherwise */

	grk_rect dim; /* canvas coordinates of region */
	grk_rect tile_dim; /* canvas coordinates of tile */

};

bool alloc_component_data_decode(TileBuffer *buf);

bool alloc_component_data_encode(TileBuffer *buf);


}
