/**
*    Copyright (C) 2016 Grok Image Compression Inc.
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

typedef struct opj_pt {
    int32_t x;
    int32_t y;

} opj_pt_t;

typedef struct opj_rect {

    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

} opj_rect_t;

void opj_rect_init(opj_rect_t* r, int32_t x0, int32_t y0, int32_t x1, int32_t y1);

/* valid if x0 <= x1 && y0 <= y1. Can include degenerate rectangles: line and point*/
bool opj_rect_is_valid(opj_rect_t* rect);

int64_t opj_rect_get_area(opj_rect_t* r);

bool opj_rect_is_non_degenerate(opj_rect_t* rect);

bool opj_rect_is_valid(opj_rect_t* rect);

bool opj_rect_are_equal(opj_rect_t* r1, opj_rect_t* r2);

bool opj_rect_clip(opj_rect_t* r1, opj_rect_t* r2, opj_rect_t* result);

void opj_rect_ceildivpow2(opj_rect_t* r, int32_t power);

void opj_rect_grow(opj_rect_t* r, int32_t boundary);

void opj_rect_grow2(opj_rect_t* r, int32_t boundaryx, int32_t boundaryy);

void opj_rect_subsample(opj_rect_t* r, uint32_t dx, uint32_t dy);

void opj_rect_pan(opj_rect_t* r, opj_pt_t* shift);

void opj_rect_print(opj_rect_t* r);

typedef struct opj_buf {
    uint8_t *buf;		/* internal array*/
    int64_t offset;	/* current offset into array */
    size_t len;		/* length of array */
    bool owns_data;	/* true if buffer manages the buf array */
} opj_buf_t;

