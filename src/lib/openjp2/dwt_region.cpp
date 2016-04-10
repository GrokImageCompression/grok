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
*	 This source code incorporates work covered by the following copyright and
*    permission notice:
*
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2007, Jonathan Ballard <dzonatas@dzonux.net>
 * Copyright (c) 2007, Callum Lerwick <seg@haxxed.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include "opj_includes.h"

/*

=================================================================================
Synthesis DWT Transform for a region wholly contained inside of a tile component
=================================================================================

Notes on DWT tranform:

The first step in the synthesis transform is interleaving, where sub-bands are transformed
into resolution space by interleaving even and odd coordinates
(i.e. low and high pass filtered samples).

Low-pass filtered samples in sub-bands are mapped to even coordinates in the resolution
coordinate system, and high-pass filtered samples are mapped to odd coordinates
in the resolution coordinate system.

The letter s is used to denote even canvas coordinates (after interleaving),
while the letter d is used to denote odd coordinates (after interleaving).
s_n denotes the number of even locations at a given resolution, while d_n denotes the number
of odd locations.


5/3 Implementation:

For each specified resolution, starting with the first resolution, the transform
proceeds as follows:

1. For each row region, samples are interleaved in the horizontal axis, and stored in a
one dimension buffer. Important: the 0th location in the buffer is mapped to the first interleaved
location in the resolution, which could be either even or odd.  So, based on the parity of the resolution's
top left hand corner, the even buffer locations are either mapped to low pass or high pass samples
in the sub-bands. (if even locations are low pass, then odd locations are high pass, and vice versa).

2. horizontal lifting in buffer

3. copy data to tile buffer

4. repeat for vertical axis


*/


/** @defgroup DWT DWT - Implementation of a synthesis discrete wavelet transform */
/*@{*/


/** @name Local data structures */
/*@{*/


typedef struct opj_dwt53 {
    int32_t* data;
    int32_t d_n;
    int32_t s_n;
    opj_pt_t	range_even;
    opj_pt_t	range_odd;
    int32_t  interleaved_offset;
    int32_t odd_top_left_bit;
} opj_dwt53_t;

/* process four coefficients at a time*/
typedef union {
    float	f[4];
} opj_coeff97_t;

typedef struct opj_dwt97 {
    opj_coeff97_t*	data ;
    int32_t		d_n ;
    int32_t		s_n ;
    opj_pt_t		range_even;
    opj_pt_t		range_odd;
    int32_t		interleaved_offset;
    int32_t		odd_top_left_bit ;
} opj_dwt97_t ;

static const float opj_dwt_alpha =  1.586134342f; /*  12994 */
static const float opj_dwt_beta  =  0.052980118f; /*    434 */
static const float opj_dwt_gamma = -0.882911075f; /*  -7233 */
static const float opj_dwt_delta = -0.443506852f; /*  -3633 */

static const float opj_K      = 1.230174105f; /*  10078 */
static const float opj_c13318 = 1.625732422f;

/*@}*/


/** @name Local static functions */
/*@{*/
/**
Inverse lazy transform (horizontal)
*/
static void opj_dwt_region_interleave53_h(opj_dwt53_t* buffer_h,
        int32_t *tile_data);
/**
Inverse lazy transform (vertical)
*/
static void opj_dwt_region_interleave53_v(opj_dwt53_t* buffer_v,
        int32_t *tile_data,
        int32_t stride);
/**
Inverse 5-3 data transform in 1-D
*/
static void opj_dwt_region_decode53_1d(opj_dwt53_t *buffer_v);



/* <summary>                             */
/* Inverse 9-7 data transform in 1-D. */
/* </summary>                            */
static void opj_region_decode97(opj_dwt97_t* restrict dwt);

static void opj_region_interleave97_h(opj_dwt97_t* restrict w,
                                      float* restrict tile_data,
                                      int32_t stride,
                                      int32_t size);

static void opj_region_interleave97_v(opj_dwt97_t* restrict buffer_v ,
                                      float* restrict tile_data ,
                                      int32_t stride,
                                      int32_t nb_elts_read);

static void opj_region_decode97_scale(opj_coeff97_t* w,
                                      opj_pt_t range,
                                      const float scale);

static void opj_region_decode97_lift(opj_coeff97_t* l,
                                     opj_coeff97_t* w,
                                     opj_pt_t range,
                                     int32_t count,
                                     int32_t max,
                                     float scale);



/*@}*/

/*@}*/



/*
==========================================================
   local functions
==========================================================
*/

/***************************************************************************************

5/3 Synthesis Wavelet Transform

*****************************************************************************************/



/* <summary>                             */
/* Inverse lazy transform (horizontal).  */
/* </summary>                            */
static void opj_dwt_region_interleave53_h(opj_dwt53_t* buffer_h, int32_t *tile_data)
{
    int32_t *tile_data_ptr = tile_data;
    int32_t *buffer_data_ptr = buffer_h->data - buffer_h->interleaved_offset + buffer_h->odd_top_left_bit;
    int32_t i;
    for (i = buffer_h->range_even.x; i < buffer_h->range_even.y; ++i) {
        buffer_data_ptr[i << 1] = tile_data_ptr[i];
    }
    tile_data_ptr	= tile_data + buffer_h->s_n;
    buffer_data_ptr = buffer_h->data - buffer_h->interleaved_offset + (buffer_h->odd_top_left_bit^1);

    for (i = buffer_h->range_odd.x; i < buffer_h->range_odd.y; ++i) {
        buffer_data_ptr[i << 1] = tile_data_ptr[i];
    }
}

/* <summary>                             */
/* Inverse lazy transform (vertical).    */
/* </summary>                            */
static void opj_dwt_region_interleave53_v(opj_dwt53_t* buffer_v,
        int32_t *tile_data,
        int32_t stride)
{
    int32_t *tile_data_ptr = tile_data;
    int32_t *buffer_data_ptr = buffer_v->data - buffer_v->interleaved_offset + buffer_v->odd_top_left_bit;
    int32_t  i;

    for (i = buffer_v->range_even.x; i < buffer_v->range_even.y; ++i) {
        buffer_data_ptr[i << 1] = tile_data_ptr[i*stride];
    }

    tile_data_ptr	= tile_data + (buffer_v->s_n * stride);
    buffer_data_ptr = buffer_v->data - buffer_v->interleaved_offset + (buffer_v->odd_top_left_bit^1);

    for (i = buffer_v->range_odd.x; i < buffer_v->range_odd.y; ++i) {
        buffer_data_ptr[i << 1] = tile_data_ptr[i*stride];
    }
}


/* <summary>                            */
/* Inverse 5-3 data transform in 1-D. */
/* </summary>
*/

#define OPJ_S(i) a[(i)<<1]
#define OPJ_D(i) a[(1+((i)<<1))]
#define OPJ_S_(i) ((i)<0?OPJ_S(0):((i)>=s_n?OPJ_S(s_n-1):OPJ_S(i)))
#define OPJ_D_(i) ((i)<0?OPJ_D(0):((i)>=d_n?OPJ_D(d_n-1):OPJ_D(i)))


#define OPJ_SS_(i) ((i)<0?OPJ_S(0):((i)>=d_n?OPJ_S(d_n-1):OPJ_S(i)))
#define OPJ_DD_(i) ((i)<0?OPJ_D(0):((i)>=s_n?OPJ_D(s_n-1):OPJ_D(i)))

static void opj_dwt_region_decode53_1d(opj_dwt53_t *buffer)
{
    int32_t *a = buffer->data - buffer->interleaved_offset;
    int32_t d_n = buffer->d_n;
    int32_t s_n = buffer->s_n;
    int32_t i;

    if (!buffer->odd_top_left_bit) {
        if ((d_n > 0) || (s_n > 1)) {
            /* inverse update */
            for (i = buffer->range_even.x; i < buffer->range_even.y; ++i)
                OPJ_S(i) -= (OPJ_D_(i - 1) + OPJ_D_(i) + 2) >> 2;
            /* inverse predict */
            for (i = buffer->range_odd.x; i < buffer->range_odd.y; ++i)
                OPJ_D(i) += (OPJ_S_(i) + OPJ_S_(i + 1)) >> 1;
        }
    } else {
        if (!s_n  && d_n == 1)
            OPJ_S(0) >>=1;
        else {
            /* inverse update */
            for (i = buffer->range_even.x; i < buffer->range_even.y; ++i)
                OPJ_D(i) -= (OPJ_SS_(i) + OPJ_SS_(i + 1) + 2) >> 2;

            /* inverse predict */
            for (i = buffer->range_odd.x; i < buffer->range_odd.y; ++i)
                OPJ_S(i) += (OPJ_DD_(i) + OPJ_DD_(i - 1)) >> 1;
        }
    }
}

/*
==========================================================
   DWT interface
==========================================================
*/


/* <summary>                            */
/* Inverse 5-3 data transform in 2-D. */
/* </summary>                           */
bool opj_dwt_region_decode53(opj_tcd_tilecomp_t* tilec, uint32_t numres)
{
    opj_dwt53_t buffer_h;
    opj_dwt53_t buffer_v;

    opj_tcd_resolution_t* tr = tilec->resolutions;

    uint32_t res_width = (uint32_t)(tr->x1 - tr->x0);	/* width of the resolution level computed */
    uint32_t res_height = (uint32_t)(tr->y1 - tr->y0);	/* height of the resolution level computed */

    uint32_t w = (uint32_t)(tilec->x1 - tilec->x0);

    int32_t resno = 1;

    if (numres == 1U) {
        return true;
    }
    buffer_h.data =
        (int32_t*)opj_aligned_malloc((opj_tile_buf_get_max_interleaved_range(tilec->buf)+2) * sizeof(int32_t));
    if (!buffer_h.data) {
        /* FIXME event manager error callback */
        return false;
    }

    buffer_v.data = buffer_h.data;

    while (--numres) {
        int32_t * restrict tiledp = opj_tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0);
        int32_t j;
        opj_pt_t interleaved_h, interleaved_v;

        /* start with the first resolution, and work upwards*/
        buffer_h.range_even = opj_tile_buf_get_uninterleaved_range(tilec->buf, resno, true, true);
        buffer_h.range_odd = opj_tile_buf_get_uninterleaved_range(tilec->buf, resno, false, true);
        buffer_v.range_even = opj_tile_buf_get_uninterleaved_range(tilec->buf, resno, true, false);
        buffer_v.range_odd = opj_tile_buf_get_uninterleaved_range(tilec->buf, resno, false, false);

        interleaved_h = opj_tile_buf_get_interleaved_range(tilec->buf, resno, true);
        interleaved_v = opj_tile_buf_get_interleaved_range(tilec->buf, resno, false);


        buffer_h.s_n = (int32_t)res_width;
        buffer_v.s_n = (int32_t)res_height;
        buffer_v.interleaved_offset = opj_int_max(0,interleaved_v.x-2);

        ++tr;
        res_width = (uint32_t)(tr->x1 - tr->x0);
        res_height = (uint32_t)(tr->y1 - tr->y0);

        buffer_h.d_n = (int32_t)(res_width - (uint32_t)buffer_h.s_n);
        buffer_h.odd_top_left_bit = tr->x0 &1;
        buffer_h.interleaved_offset = opj_int_max(0, interleaved_h.x - 2);

        /* first do horizontal interleave */
        tiledp += buffer_v.range_even.x * w;
        for (j = buffer_v.range_even.x; j < buffer_v.range_even.y; ++j) {
            opj_dwt_region_interleave53_h(&buffer_h, tiledp);
            opj_dwt_region_decode53_1d(&buffer_h);
            memcpy(tiledp + interleaved_h.x, buffer_h.data + interleaved_h.x - buffer_h.interleaved_offset, (interleaved_h.y - interleaved_h.x) * sizeof(int32_t));
            tiledp += w;
        }

        tiledp += (buffer_v.s_n - buffer_v.range_even.y + buffer_v.range_odd.x)*w;
        for (j = buffer_v.range_odd.x; j <  buffer_v.range_odd.y; ++j) {
            opj_dwt_region_interleave53_h(&buffer_h, tiledp);
            opj_dwt_region_decode53_1d(&buffer_h);
            memcpy(tiledp + interleaved_h.x, buffer_h.data + interleaved_h.x - buffer_h.interleaved_offset, (interleaved_h.y - interleaved_h.x) * sizeof(int32_t));
            tiledp += w;
        }

        buffer_v.d_n = (int32_t)(res_height - (uint32_t)buffer_v.s_n);
        buffer_v.odd_top_left_bit = tr->y0 &1;

        /* next do vertical interleave */
        tiledp = opj_tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0) + interleaved_h.x;
        for (j = interleaved_h.x; j < interleaved_h.y; ++j) {
            int32_t k;
            int32_t * restrict tiledp_v = tiledp + (interleaved_v.x)*w;
            opj_dwt_region_interleave53_v(&buffer_v, tiledp, (int32_t)w);
            opj_dwt_region_decode53_1d(&buffer_v);
            for (k = interleaved_v.x; k < interleaved_v.y; ++k) {
                *tiledp_v = buffer_v.data[k - buffer_v.interleaved_offset];
                tiledp_v += w;
            }
            tiledp++;
        }
        resno++;
    }
    opj_aligned_free(buffer_h.data);
    return true;
}

/***************************************************************************************

9/7 Synthesis Wavelet Transform

*****************************************************************************************/
static void opj_region_interleave97_h(opj_dwt97_t* restrict buffer,
                                      float* restrict tile_data,
                                      int32_t stride,
                                      int32_t size)
{

    float* restrict buffer_data_ptr = (float*) (buffer->data - buffer->interleaved_offset + buffer->odd_top_left_bit);
    int32_t count_low = buffer->range_even.x;
    int32_t count_high = buffer->range_even.y;
    int32_t i, k;

    for (k = 0; k < 2; ++k) {
        if (count_low + 3 * stride < size) {
            /* Fast code path */
            for (i = count_low; i < count_high; ++i) {
                int32_t j = i;
                buffer_data_ptr[i << 3] = tile_data[j];
                j += stride;

                buffer_data_ptr[(i << 3) + 1] = tile_data[j];
                j += stride;

                buffer_data_ptr[(i << 3) + 2] = tile_data[j];
                j += stride;

                buffer_data_ptr[(i << 3) + 3] = tile_data[j];
            }
        } else {
            /* Slow code path */
            for (i = count_low; i < count_high; ++i) {
                int32_t j = i;

                buffer_data_ptr[i << 3] = tile_data[j];
                j += stride;
                if (j >= size)
                    continue;

                buffer_data_ptr[(i << 3) + 1] = tile_data[j];
                j += stride;
                if (j >= size)
                    continue;

                buffer_data_ptr[(i << 3) + 2] = tile_data[j];
                j += stride;
                if (j >= size)
                    continue;

                buffer_data_ptr[(i << 3) + 3] = tile_data[j];
            }
        }

        buffer_data_ptr = (float*)(buffer->data - buffer->interleaved_offset + (buffer->odd_top_left_bit^1) );
        tile_data += buffer->s_n;
        size -= buffer->s_n;
        count_low = buffer->range_odd.x;
        count_high = buffer->range_odd.y;
    }
}

static void opj_region_interleave97_v(opj_dwt97_t* restrict buffer ,
                                      float* restrict tile_data ,
                                      int32_t stride,
                                      int32_t nb_elts_read)
{
    opj_coeff97_t* restrict buffer_data_ptr = buffer->data - buffer->interleaved_offset + buffer->odd_top_left_bit;
    int32_t i;

    int32_t count_low = buffer->range_even.x;
    int32_t count_high = buffer->range_even.y;

    for(i = count_low; i < count_high; ++i) {
        memcpy(buffer_data_ptr + (i<<1),
               tile_data + i*stride,
               (size_t)nb_elts_read * sizeof(float));
    }

    tile_data += buffer->s_n * stride;
    buffer_data_ptr = buffer->data - buffer->interleaved_offset + (buffer->odd_top_left_bit^1);

    count_low = buffer->range_odd.x;
    count_high = buffer->range_odd.y;

    for(i = count_low; i < count_high; ++i) {
        memcpy(buffer_data_ptr + (i<<1),
               tile_data + i*stride,
               (size_t)nb_elts_read * sizeof(float));
    }
}

static void opj_region_decode97_scale(opj_coeff97_t* buffer,
                                      opj_pt_t range,
                                      const float scale)
{
    float* restrict fw = ((float*) buffer);
    int32_t i;
    int32_t count_low = range.x;
    int32_t count_high = range.y;

    for(i = count_low; i < count_high; ++i) {
        fw[(i<<3)    ] *= scale;
        fw[(i<<3) + 1] *= scale;
        fw[(i<<3) + 2] *= scale;
        fw[(i<<3) + 3] *= scale;
    }
}

static void opj_region_decode97_lift(opj_coeff97_t* l,
                                     opj_coeff97_t* w,
                                     opj_pt_t range,
                                     int32_t count,
                                     int32_t maximum,
                                     float scale)
{
    float* fl = (float*) l;
    float* fw = (float*) w;
    int32_t i;

    int32_t count_low = range.x;
    int32_t count_high = range.y;
    int32_t count_max = opj_int_min(count_high, maximum);

    assert(count_low <= count_high);
    assert(maximum <= count);

    if (count_low > 0) {
        fw += count_low << 3;
        fl = fw - 8;
    }

    for(i = count_low; i < count_max; ++i) {
        fw[-4] += ((fl[0] + fw[0]) * scale);
        fw[-3] += ((fl[1] + fw[1]) * scale);
        fw[-2] += ((fl[2] + fw[2]) * scale);
        fw[-1] += ((fl[3] + fw[3]) * scale);
        fl = fw;
        fw += 8;
    }

    /* symmetric boundary extension */
    if(maximum < count_high) {
        scale += scale;
        for(; maximum < count_high; ++maximum) {
            fw[-4] += fl[0] * scale;
            fw[-3] += fl[1] * scale;
            fw[-2] += fl[2] * scale;
            fw[-1] += fl[3] * scale;
            fw += 8;
        }
    }
}


/* <summary>                             */
/* Inverse 9-7 data transform in 1-D. */
/* </summary>                            */
static void opj_region_decode97(opj_dwt97_t* restrict dwt)
{
    /* either 0 or 1 */
    int32_t odd_top_left_bit = dwt->odd_top_left_bit;
    int32_t even_top_left_bit = odd_top_left_bit ^ 1;


    if (!((dwt->d_n > odd_top_left_bit) || (dwt->s_n > even_top_left_bit))) {
        return;
    }

    /* inverse low-pass scale */
    opj_region_decode97_scale(dwt->data - dwt->interleaved_offset+ odd_top_left_bit,
                              dwt->range_even,
                              opj_K);

    /* inverse high-pass scale */
    opj_region_decode97_scale(dwt->data - dwt->interleaved_offset + even_top_left_bit,
                              dwt->range_odd,
                              opj_c13318);

    /* inverse update */
    opj_region_decode97_lift(dwt->data - dwt->interleaved_offset + even_top_left_bit,
                             dwt->data - dwt->interleaved_offset + odd_top_left_bit+1,
                             dwt->range_even,
                             dwt->s_n,
                             opj_int_min(dwt->s_n, dwt->d_n-odd_top_left_bit),
                             opj_dwt_delta);

    /* inverse predict */
    opj_region_decode97_lift(dwt->data - dwt->interleaved_offset + odd_top_left_bit,
                             dwt->data - dwt->interleaved_offset + even_top_left_bit+1,
                             dwt->range_odd,
                             dwt->d_n,
                             opj_int_min(dwt->d_n, dwt->s_n-even_top_left_bit),
                             opj_dwt_gamma);
    /* inverse update */
    opj_region_decode97_lift(dwt->data - dwt->interleaved_offset + even_top_left_bit,
                             dwt->data - dwt->interleaved_offset + odd_top_left_bit+1,
                             dwt->range_even,
                             dwt->s_n,
                             opj_int_min(dwt->s_n, dwt->d_n-odd_top_left_bit),
                             opj_dwt_beta);

    /* inverse predict */
    opj_region_decode97_lift(dwt->data - dwt->interleaved_offset + odd_top_left_bit,
                             dwt->data - dwt->interleaved_offset + even_top_left_bit+1,
                             dwt->range_odd,
                             dwt->d_n,
                             opj_int_min(dwt->d_n, dwt->s_n-even_top_left_bit),
                             opj_dwt_alpha);

}


/* <summary>                             */
/* Inverse 9-7 data transform in 2-D. */
/* </summary>                            */
bool opj_dwt_region_decode97(opj_tcd_tilecomp_t* restrict tilec, uint32_t numres)
{
    opj_dwt97_t buffer_h;
    opj_dwt97_t buffer_v;

    opj_tcd_resolution_t* res = tilec->resolutions;

    int32_t resno = 1;

    /* start with lowest resolution */
    uint32_t res_width = (uint32_t)(res->x1 - res->x0);	/* width of the resolution level computed */
    uint32_t res_height = (uint32_t)(res->y1 - res->y0);	/* height of the resolution level computed */

    uint32_t tile_width = (uint32_t)(tilec->x1 - tilec->x0);

    buffer_h.data =
        (opj_coeff97_t*)opj_aligned_malloc((opj_tile_buf_get_max_interleaved_range(tilec->buf) + 4) * sizeof(opj_coeff97_t));

    if (!buffer_h.data) {
        /* FIXME event manager error callback */
        return false;
    }
    /* share data buffer between vertical and horizontal lifting steps*/
    buffer_v.data = buffer_h.data;

    while( --numres) {
        float * restrict tile_data = (float*)opj_tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0);
        uint32_t bufsize = (uint32_t)(tile_width * (tilec->y1 - tilec->y0));
        int32_t j;
        opj_pt_t interleaved_h, interleaved_v;

        /* start with the first resolution, and work upwards*/

        buffer_h.s_n = (int32_t)res_width;
        buffer_v.s_n = (int32_t)res_height;

        buffer_h.range_even = opj_tile_buf_get_uninterleaved_range(tilec->buf, resno, true, true);
        buffer_h.range_odd = opj_tile_buf_get_uninterleaved_range(tilec->buf, resno, false, true);
        buffer_v.range_even = opj_tile_buf_get_uninterleaved_range(tilec->buf, resno, true, false);
        buffer_v.range_odd = opj_tile_buf_get_uninterleaved_range(tilec->buf, resno, false, false);

        interleaved_h = opj_tile_buf_get_interleaved_range(tilec->buf, resno, true);
        interleaved_v = opj_tile_buf_get_interleaved_range(tilec->buf, resno, false);

        ++res;

        /* dimensions of next higher resolution */
        res_width = (uint32_t)(res->x1 - res->x0);	/* width of the resolution level computed */
        res_height = (uint32_t)(res->y1 - res->y0);	/* height of the resolution level computed */

        buffer_h.d_n = (int32_t)(res_width - (uint32_t)buffer_h.s_n);
        buffer_h.odd_top_left_bit = res->x0 &1;
        buffer_h.interleaved_offset = opj_int_max(0, interleaved_h.x - 4);

        /*  Step 1.  interleave and lift in horizontal direction */
        tile_data += tile_width * buffer_v.range_even.x;
        bufsize -= tile_width * buffer_v.range_even.x;

        for(j = buffer_v.range_even.y - buffer_v.range_even.x; j > 3; j -= 4) {
            int32_t k;
            opj_region_interleave97_h(&buffer_h,
                                      tile_data,
                                      (int32_t)tile_width,
                                      (int32_t)bufsize);

            opj_region_decode97(&buffer_h);

            for (k = interleaved_h.x; k < interleaved_h.y; ++k) {
                int32_t buffer_index = k - buffer_h.interleaved_offset;
                tile_data[k               ]					= buffer_h.data[buffer_index].f[0];
                tile_data[k+(int32_t)tile_width]			= buffer_h.data[buffer_index].f[1];
                tile_data[k+((int32_t)tile_width <<1)]	= buffer_h.data[buffer_index].f[2];
                tile_data[k+(int32_t)tile_width *3]		= buffer_h.data[buffer_index].f[3];
            }

            tile_data	+= tile_width <<2;
            bufsize		-= tile_width <<2;
        }

        if (j > 0) {
            int32_t k;
            opj_region_interleave97_h(&buffer_h,
                                      tile_data,
                                      (int32_t)tile_width,
                                      (int32_t)bufsize);

            opj_region_decode97(&buffer_h);
            for (k = interleaved_h.x; k < interleaved_h.y; ++k) {
                int32_t buffer_index = k - buffer_h.interleaved_offset;
                switch(j) {
                case 3:
                    tile_data[k+((int32_t)tile_width <<1)] = buffer_h.data[buffer_index].f[2];
                case 2:
                    tile_data[k+(int32_t)tile_width]		= buffer_h.data[buffer_index].f[1];
                case 1:
                    tile_data[k]							= buffer_h.data[buffer_index].f[0];
                }
            }

            tile_data	+= tile_width * j;
            bufsize		-= 	tile_width *j;
        }

        tile_data += tile_width *(buffer_v.s_n - buffer_v.range_even.y + buffer_v.range_odd.x);
        bufsize -= tile_width *(buffer_v.s_n - buffer_v.range_even.y + buffer_v.range_odd.x);

        for (j = buffer_v.range_odd.y - buffer_v.range_odd.x; j > 3; j -= 4) {
            int32_t k;
            opj_region_interleave97_h(&buffer_h,
                                      tile_data,
                                      (int32_t)tile_width,
                                      (int32_t)bufsize);

            opj_region_decode97(&buffer_h);

            for (k = interleaved_h.x; k < interleaved_h.y; ++k) {
                int32_t buffer_index = k - buffer_h.interleaved_offset;
                tile_data[k] = buffer_h.data[buffer_index].f[0];
                tile_data[k + (int32_t)tile_width] = buffer_h.data[buffer_index].f[1];
                tile_data[k + ((int32_t)tile_width << 1)] = buffer_h.data[buffer_index].f[2];
                tile_data[k + (int32_t)tile_width * 3] = buffer_h.data[buffer_index].f[3];
            }

            tile_data += tile_width << 2;
            bufsize -= tile_width << 2;
        }

        if (j > 0) {
            int32_t k;
            opj_region_interleave97_h(&buffer_h,
                                      tile_data,
                                      (int32_t)tile_width,
                                      (int32_t)bufsize);

            opj_region_decode97(&buffer_h);
            for (k = interleaved_h.x; k < interleaved_h.y; ++k) {
                int32_t buffer_index = k - buffer_h.interleaved_offset;
                switch (j) {
                case 3:
                    tile_data[k + ((int32_t)tile_width << 1)] = buffer_h.data[buffer_index].f[2];
                case 2:
                    tile_data[k + (int32_t)tile_width] = buffer_h.data[buffer_index].f[1];
                case 1:
                    tile_data[k] = buffer_h.data[buffer_index].f[0];
                }
            }
        }
        /*****************************************************************************/

        /* interleave and lift in vertical direction */

        buffer_v.d_n = (int32_t)(res_height - (uint32_t)buffer_v.s_n);
        buffer_v.odd_top_left_bit = res->y0 &1;
        buffer_v.interleaved_offset = opj_int_max(0, interleaved_v.x - 4);

        tile_data = ((float*)opj_tile_buf_get_ptr(tilec->buf, 0, 0, 0, 0)) + interleaved_h.x;
        for(j = interleaved_h.y - interleaved_h.x; j > 3; j -= 4) {
            int32_t k;

            opj_region_interleave97_v(&buffer_v,
                                      tile_data,
                                      (int32_t)tile_width,
                                      4);

            opj_region_decode97(&buffer_v);

            for (k = interleaved_v.x; k < interleaved_v.y; ++k) {
                memcpy(tile_data +k*tile_width,
                       buffer_v.data+k - buffer_v.interleaved_offset,
                       4 * sizeof(float));
            }
            tile_data += 4;
        }


        if (j > 0) {
            int32_t k;
            opj_region_interleave97_v(&buffer_v,
                                      tile_data,
                                      (int32_t)tile_width,
                                      j);

            opj_region_decode97(&buffer_v);

            for (k = interleaved_v.x; k < interleaved_v.y; ++k) {
                memcpy(tile_data + k*tile_width,
                       buffer_v.data+k - buffer_v.interleaved_offset,
                       (size_t)j * sizeof(float));
            }
        }
        /*****************************************************************************/

        resno++;
    }

    opj_aligned_free(buffer_h.data);
    return true;
}
