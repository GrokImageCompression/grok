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
Note: various coordinate systems are used to describe regions in the tile buffer.

1) Canvas coordinate system:  jpeg 2000 global image coordinates

2) Tile coordinate system:  coordinates relative to a tile's top left hand corner

3) Resolution coordinate system:  coordinates relative to a resolution's top left hand corner

4) Sub-band coordinate system: coordinates relative to a particular sub-band's top left hand corner

*/

typedef struct opj_tile_buf_band {
    opj_rect_t dim;			/* coordinates of sub-band region (canvas coordinates)  */
    opj_rect_t data_dim;	/* coordinates of sub-band data region, (tile coordinates ) */
} opj_tile_buf_band_t;

typedef struct opj_tile_buf_resolution {
    opj_tile_buf_band_t band_region[3];
    uint32_t num_bands;
    opj_pt_t origin;		/* resolution origin, in canvas coordinates */
    opj_pt_t bounds;		/* full width and height of resolution */
} opj_tile_buf_resolution_t;

typedef struct opj_tile_buf_component {
	std::vector<opj_tile_buf_resolution_t*> resolutions;
    int32_t *data;
    uint64_t data_size_needed;	/* we may either need to allocate this amount of data,
									   or re-use image data and ignore this value */
    uint64_t data_size;			/* size of the data of the component */
    bool owns_data;				/* true if tile buffer manages its data array, false otherwise */

    opj_rect_t dim;		  /* canvas coordinates of region */
    opj_rect_t tile_dim;  /* canvas coordinates of tile */

} opj_tile_buf_component_t;

/* offsets are in canvas coordinate system*/
int32_t* opj_tile_buf_get_ptr(opj_tile_buf_component_t* buf,
                              uint32_t resno,
                              uint32_t bandno,
                              uint32_t offsetx,
                              uint32_t offsety);

void opj_tile_buf_set_ptr(opj_tile_buf_component_t* buf, int32_t* ptr);

bool opj_tile_buf_alloc_component_data_decode(opj_tile_buf_component_t* buf);

bool opj_tile_buf_alloc_component_data_encode(opj_tile_buf_component_t* buf);

bool opj_tile_buf_is_decode_region(opj_tile_buf_component_t* buf);

void opj_tile_buf_destroy_component(opj_tile_buf_component_t* comp);

/* Check if rect overlaps with region.
   rect coordinates must be stored in canvas coordinates
*/
bool opj_tile_buf_hit_test(opj_tile_buf_component_t* comp, opj_rect_t* rect);

/* sub-band coordinates */
opj_pt_t opj_tile_buf_get_uninterleaved_range(opj_tile_buf_component_t* comp,
        uint32_t resno,
        bool is_even,
        bool is_horizontal);


/* resolution coordinates */
opj_pt_t opj_tile_buf_get_interleaved_range(opj_tile_buf_component_t* comp,
        uint32_t resno,
        bool is_horizontal);

int64_t opj_tile_buf_get_max_interleaved_range(opj_tile_buf_component_t* comp);

