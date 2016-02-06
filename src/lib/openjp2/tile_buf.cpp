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


#include "opj_includes.h"

/*
Create region manager.

Note: because this method uses a tcd struct, and we can't forward declare the struct
in region_mgr.h header file, this method's declaration can be found in the tcd.h
header file.

*/
bool opj_tile_buf_create_component(opj_tcd_tilecomp_t* tilec,
                                   bool irreversible,
                                   uint32_t cblkw,
                                   uint32_t cblkh,
                                   opj_image_t* output_image,
                                   uint32_t dx,
                                   uint32_t dy)
{
    int32_t resno = 0;
    opj_rect_t	component_output_rect;
    opj_tile_buf_component_t* comp = NULL;

    if (!tilec)
        return false;

    /* create region component struct*/
	comp = new opj_tile_buf_component_t();
    if (!comp) {
        return false;
    }
	comp->data = NULL;

    opj_rect_init(&comp->tile_dim,
                  tilec->x0,
                  tilec->y0,
                  tilec->x1,
                  tilec->y1);

    if (output_image) {
        opj_rect_init(&comp->dim,
                      opj_int_ceildiv(output_image->x0,dx),
                      opj_int_ceildiv(output_image->y0,dy),
                      opj_int_ceildiv(output_image->x1,dx),
                      opj_int_ceildiv(output_image->y1,dy));

        /* clip output image to tile */
        opj_rect_clip(&comp->tile_dim, &comp->dim, &comp->dim);

    } else {
        comp->dim = comp->tile_dim;
    }


    /* for encode, we don't need to allocate resolutions */
    if (!output_image) {
        opj_tile_buf_destroy_component(tilec->buf);
        tilec->buf = comp;
        return true;
    }


    component_output_rect = comp->dim;


    /* fill resolutions vector */
    for (resno = (int32_t)(tilec->numresolutions-1); resno >= 0; --resno) {
        uint32_t bandno;
        opj_tcd_resolution_t*  tcd_res = tilec->resolutions + resno;
        opj_tile_buf_resolution_t* res = (opj_tile_buf_resolution_t*)opj_calloc(1, sizeof(opj_tile_buf_resolution_t));
        if (!res) {
            opj_tile_buf_destroy_component(comp);
            return false;
        }

        res->bounds.x = tcd_res->x1 - tcd_res->x0;
        res->bounds.y = tcd_res->y1 - tcd_res->y0;
        res->origin.x = tcd_res->x0;
        res->origin.y = tcd_res->y0;

        for (bandno = 0; bandno < tcd_res->numbands; ++bandno) {
            opj_tcd_band_t* band = tcd_res->bands + bandno;
            opj_rect_t  band_rect;
            opj_rect_init(&band_rect,
                          band->x0,
                          band->y0,
                          band->x1,
                          band->y1
                         );

            res->band_region[bandno].dim = component_output_rect;
            if (resno > 0) {

                /*For next level down, E' = ceil((E-b)/2) where b in {0,1} identifies band  */
                opj_pt_t shift;
                shift.x = band->bandno & 1;
                shift.y = band->bandno & 2;

                opj_rect_pan(&res->band_region[bandno].dim, &shift);
                opj_rect_ceildivpow2(&res->band_region[bandno].dim, 1);

                /* boundary padding */
                opj_rect_grow(&res->band_region[bandno].dim, irreversible ? 3 : 2);

            }

            /* add code block padding around region */
            (res->band_region + bandno)->data_dim = (res->band_region + bandno)->dim;
            opj_rect_grow2(&(res->band_region + bandno)->data_dim, cblkw, cblkh);

        }
        component_output_rect = res->band_region[0].dim;
        res->num_bands = tcd_res->numbands;
        comp->resolutions.push_back(res);
    }

    opj_tile_buf_destroy_component(tilec->buf);
    tilec->buf = comp;

    return true;
}

bool opj_tile_buf_is_decode_region(opj_tile_buf_component_t* buf)
{
    if (!buf)
        return false;
    return !opj_rect_are_equal(&buf->dim, &buf->tile_dim);
}

int32_t* opj_tile_buf_get_ptr(opj_tile_buf_component_t* buf,
                              uint32_t resno,
                              uint32_t bandno,
                              uint32_t offsetx,
                              uint32_t offsety)
{
    return buf->data + (uint32_t)offsetx + (uint32_t)offsety* (buf->tile_dim.x1 - buf->tile_dim.x0);

}

void opj_tile_buf_set_ptr(opj_tile_buf_component_t* buf, int32_t* ptr)
{
    buf->data = ptr;
    buf->owns_data = false;
}

bool opj_tile_buf_alloc_component_data_encode(opj_tile_buf_component_t* buf)
{
    if (!buf)
        return false;

    if ((buf->data == 00) || ((buf->data_size_needed > buf->data_size) && (buf->owns_data == false))) {
        buf->data = (int32_t *)opj_aligned_malloc(buf->data_size_needed);
        if (!buf->data) {
            return false;
        }
        buf->data_size = buf->data_size_needed;
        buf->owns_data = true;
    } else if (buf->data_size_needed > buf->data_size) {
        /* We don't need to keep old data */
        opj_aligned_free(buf->data);
        buf->data = (int32_t *)opj_aligned_malloc(buf->data_size_needed);
        if (!buf->data) {
            buf->data_size = 0;
            buf->data_size_needed = 0;
            buf->owns_data = false;
            return false;
        }

        buf->data_size = buf->data_size_needed;
        buf->owns_data = true;
    }
    return true;
}


bool opj_tile_buf_alloc_component_data_decode(opj_tile_buf_component_t* buf)
{
    if (!buf)
        return false;

    if (!buf->data ) {
        int32_t area = opj_rect_get_area(&buf->tile_dim);
        if (!area)
            return false;
        buf->data = (int32_t *)opj_aligned_malloc( area * sizeof(int32_t));
        if (!buf->data) {
            return false;
        }
        buf->data_size = area * sizeof(int32_t);
        buf->data_size_needed = buf->data_size;
        buf->owns_data = true;
    }

    return true;
}


void opj_tile_buf_destroy_component(opj_tile_buf_component_t* comp)
{
    if (!comp)
        return;
    if (comp->data && comp->owns_data)
        opj_aligned_free(comp->data);
    comp->data = NULL;
    comp->data_size = 0;
    comp->data_size_needed = 0;
    delete comp;
}

bool opj_tile_buf_hit_test(opj_tile_buf_component_t* comp, opj_rect_t* rect)
{
    if (!comp || !rect)
        return false;
    for (auto& res : comp->resolutions) {
        opj_rect_t dummy;
        uint32_t j;
        for (j = 0; j < res->num_bands; ++j) {
            if (opj_rect_clip(&(res->band_region + j)->dim, rect, &dummy))
                return true;
        }
    }
    return false;
}

opj_pt_t opj_tile_buf_get_uninterleaved_range(opj_tile_buf_component_t* comp,
        int32_t resno,
        bool is_even,
        bool is_horizontal)
{
    opj_pt_t rc;
    opj_tile_buf_resolution_t* res= NULL;
    opj_tile_buf_resolution_t* prev_res = NULL;
    opj_tile_buf_band_t *band= NULL;
    memset(&rc, 0, sizeof(opj_pt_t));
    if (!comp)
        return rc;

    res = comp->resolutions[comp->resolutions.size() - 1 - resno];
    if (!res)
        return rc;

    prev_res = comp->resolutions[comp->resolutions.size() - 1 - resno+1];

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
        rc.x = band->dim.x0 - prev_res->origin.x;
        rc.y = band->dim.x1 - prev_res->origin.x;
    } else {
        rc.x = band->dim.y0 - prev_res->origin.y;
        rc.y = band->dim.y1 - prev_res->origin.y;
    }

    /* clip */
    rc.x = opj_int_max(0, rc.x);

    /* if resno == 0, then prev_res is null */
    if (resno == 0) {
        rc.y = opj_int_min(rc.y, is_horizontal ? res->bounds.x : res->bounds.y);
    } else {
        if (is_even)
            rc.y = opj_int_min(rc.y, is_horizontal ? prev_res->bounds.x : prev_res->bounds.y);
        else
            rc.y = opj_int_min(rc.y,
                               is_horizontal ? res->bounds.x - prev_res->bounds.x : res->bounds.y - prev_res->bounds.y);

    }

    return rc;

}

opj_pt_t opj_tile_buf_get_interleaved_range(opj_tile_buf_component_t* comp,
        int32_t resno,
        bool is_horizontal)
{
    opj_pt_t rc;
    opj_pt_t even;
    opj_pt_t odd;
    opj_tile_buf_resolution_t* res = NULL;
    memset(&rc, 0, sizeof(opj_pt_t));
    if (!comp)
        return rc;

    res = comp->resolutions[comp->resolutions.size()- 1 - resno];
    if (!res)
        return rc;

    even = opj_tile_buf_get_uninterleaved_range(comp, resno, true, is_horizontal);
    odd = opj_tile_buf_get_uninterleaved_range(comp, resno, false, is_horizontal);

    rc.x = opj_int_min( (even.x <<1), (odd.x << 1) + 1 );
    rc.y = opj_int_max( (even.y<< 1),  (odd.y << 1) + 1);

    /* clip to resolution bounds */
    rc.x = opj_int_max(0, rc.x);
    rc.y = opj_int_min(rc.y, is_horizontal ? res->bounds.x : res->bounds.y);
    return rc;
}

int32_t opj_tile_buf_get_max_interleaved_range(opj_tile_buf_component_t* comp)
{
    opj_pt_t even, odd;
    if (!comp || comp->resolutions.empty())
        return 0;
    even = opj_tile_buf_get_interleaved_range(comp,comp->resolutions.size() - 1, true);
    odd = opj_tile_buf_get_interleaved_range(comp, comp->resolutions.size() - 1, false);

    return opj_int_max(even.y - even.x, odd.y - odd.x);
}

