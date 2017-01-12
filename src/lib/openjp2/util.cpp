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

#include "opj_includes.h"


#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

/**
Divide an integer and round upwards
@return Returns a divided by b
*/
static inline int32_t opj_int_ceildiv(int32_t a, int32_t b)
{
	assert(b);
	return (a + b - 1) / b;
}


void opj_rect_print(opj_rect_t* r)
{
    if (!r)
        printf("Null rect\n");
    else
        printf("Rectangle:  [%d,%d,%d,%d] \n", r->x0, r->y0, r->x1, r->y1);
}

void opj_rect_init(opj_rect_t* r, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (!r)
        return;
    r->x0 = x0;
    r->y0 = y0;
    r->x1 = x1;
    r->y1 = y1;
}

bool opj_rect_is_valid(opj_rect_t* rect)
{
    if (!rect)
        return false;

    return rect->x0 <= rect->x1 && rect->y0 <= rect->y1;
}

bool opj_rect_is_non_degenerate(opj_rect_t* rect)
{
    if (!rect)
        return false;

    return rect->x0 < rect->x1 && rect->y0 < rect->y1;
}

bool opj_rect_are_equal(opj_rect_t* r1, opj_rect_t* r2)
{
    if (!r1 && !r2)
        return true;

    if (!r1 || !r2)
        return false;

    return r1->x0 == r2->x0 &&
           r1->y0 == r2->y0 &&
           r1->x1 == r2->x1 &&
           r1->y1 == r2->y1;
}

bool opj_rect_clip(opj_rect_t* r1, opj_rect_t* r2, opj_rect_t* result)
{
    bool rc;
    opj_rect_t temp;

    if (!r1 || !r2 || !result)
        return false;

    temp.x0 = MAX(r1->x0, r2->x0);
    temp.y0 = MAX(r1->y0, r2->y0);

    temp.x1 = MIN(r1->x1, r2->x1);
    temp.y1 = MIN(r1->y1, r2->y1);

    rc = opj_rect_is_valid(&temp);

    if (rc)
        *result = temp;
    return rc;
}


void opj_rect_ceildivpow2(opj_rect_t* r, int32_t power)
{
    if (!r)
        return;

    r->x0 = opj_int_ceildivpow2(r->x0,power);
    r->y0 = opj_int_ceildivpow2(r->y0,power);
    r->x1 = opj_int_ceildivpow2(r->x1,power);
    r->y1 = opj_int_ceildivpow2(r->y1,power);

}


int64_t opj_rect_get_area(opj_rect_t* r)
{
    if (!r)
        return 0;
    return (int64_t)(r->x1 - r->x0) * (r->y1 - r->y0);
}

void opj_rect_pan(opj_rect_t* r, opj_pt_t* shift)
{
    if (!r)
        return;

    r->x0 += shift->x;
    r->y0 += shift->y;
    r->x1 += shift->x;
    r->y1 += shift->y;
}

void opj_rect_subsample(opj_rect_t* r, uint32_t dx, uint32_t dy)
{
    if (!r)
        return;

    r->x0 = opj_int_ceildiv(r->x0, (int32_t)dx);
    r->y0 = opj_int_ceildiv(r->y0, (int32_t)dy);
    r->x1 = opj_int_ceildiv(r->x1, (int32_t)dx);
    r->y1 = opj_int_ceildiv(r->y1, (int32_t)dy);
}

void opj_rect_grow(opj_rect_t* r, int32_t boundary)
{
    if (!r)
        return;

    opj_rect_grow2(r, boundary, boundary);
}

void opj_rect_grow2(opj_rect_t* r, int32_t boundaryx, int32_t boundaryy)
{
    if (!r)
        return;

    r->x0 -= boundaryx;
    r->y0 -= boundaryy;
    r->x1 += boundaryx;
    r->y1 += boundaryy;
}