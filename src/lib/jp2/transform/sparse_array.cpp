/*
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
 *
 *    This source code incorporates work covered by the following copyright and
 *    permission notice:
 *
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2017, IntoPix SA <contact@intopix.com>
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

#include <algorithm>
#include <cstdint>
#include <cassert>
#include "grok_includes.h"
#include "sparse_array.h"

using namespace std;

using namespace grk;

struct sparse_array {
    uint32_t width;
    uint32_t height;
    uint32_t block_width;
    uint32_t block_height;
    uint32_t block_count_hor;
    uint32_t block_count_ver;
    int32_t** data_blocks;
};

sparse_array* sparse_array_create(uint32_t width,
        uint32_t height,
        uint32_t block_width,
        uint32_t block_height)
{
    if (width == 0 || height == 0 || block_width == 0 || block_height == 0) {
        return NULL;
    }

    auto sa = (sparse_array*) grk_calloc(1,sizeof(sparse_array));
    sa->width = width;
    sa->height = height;
    sa->block_width = block_width;
    sa->block_height = block_height;
    sa->block_count_hor = ceildiv<uint32_t>(width, block_width);
    sa->block_count_ver = ceildiv<uint32_t>(height, block_height);
    sa->data_blocks = (int32_t**) grk_calloc((uint64_t)sa->block_count_hor * sa->block_count_ver,sizeof(int32_t*));
    if (sa->data_blocks == NULL) {
    	GROK_ERROR("Out of memory");
        grok_free(sa);
        return NULL;
    }

    return sa;
}

void sparse_array_free(sparse_array* sa)
{
    if (sa) {
        for (uint32_t i = 0; i < (uint64_t)sa->block_count_hor * sa->block_count_ver; i++) {
            if (sa->data_blocks[i]) {
                grok_free(sa->data_blocks[i]);
            }
        }
        grok_free(sa->data_blocks);
        grok_free(sa);
    }
}

bool sparse_array_is_region_valid(const sparse_array* sa,
        uint32_t x0,
        uint32_t y0,
        uint32_t x1,
        uint32_t y1)
{
    return !(x0 >= sa->width || x1 <= x0 || x1 > sa->width ||
             y0 >= sa->height || y1 <= y0 || y1 > sa->height);
}
bool sparse_array_alloc(sparse_array* sa,
                                      uint32_t x0,
                                      uint32_t y0,
                                      uint32_t x1,
                                      uint32_t y1){
    uint32_t y_incr = 0;
    const uint32_t block_width = sa->block_width;

    if (!sparse_array_is_region_valid(sa, x0, y0, x1, y1))
        return true;

    uint32_t block_y = y0 / sa->block_height;
    for (uint32_t y = y0; y < y1; block_y ++, y += y_incr) {
        uint32_t x, block_x;
        uint32_t x_incr = 0;
        y_incr = (y == y0) ? sa->block_height - (y0 % sa->block_height) :
                 sa->block_height;
        y_incr = min<uint32_t>(y_incr, y1 - y);
        block_x = x0 / block_width;
        for (x = x0; x < x1; block_x ++, x += x_incr) {
            x_incr = (x == x0) ? block_width - (x0 % block_width) : block_width;
            x_incr = min<uint32_t>(x_incr, x1 - x);
            auto src_block = sa->data_blocks[(uint64_t)block_y * sa->block_count_hor + block_x];
			if (src_block == NULL) {
				src_block = (int32_t*) grk_calloc((uint64_t)sa->block_width * sa->block_height,
													 sizeof(int32_t));
				if (src_block == NULL) {
					GROK_ERROR("Out of memory");
					return false;
				}
				sa->data_blocks[(uint64_t)block_y * sa->block_count_hor + block_x] = src_block;
			}
        }
    }

    return true;
}

static bool sparse_array_read_or_write(const sparse_array* sa,
										uint32_t x0,
										uint32_t y0,
										uint32_t x1,
										uint32_t y1,
										int32_t* buf,
										uint32_t buf_col_stride,
										uint32_t buf_line_stride,
										bool forgiving,
										bool is_read_op){
    uint32_t y_incr = 0;
    const uint32_t block_width = sa->block_width;

    if (!sparse_array_is_region_valid(sa, x0, y0, x1, y1))
        return forgiving;

    uint32_t block_y = y0 / sa->block_height;
    for (uint32_t y = y0; y < y1; block_y ++, y += y_incr) {
        uint32_t x, block_x;
        uint32_t x_incr = 0;
        y_incr = (y == y0) ? sa->block_height - (y0 % sa->block_height) :
                 sa->block_height;
        uint32_t block_y_offset = sa->block_height - y_incr;
        y_incr = min<uint32_t>(y_incr, y1 - y);
        block_x = x0 / block_width;
        for (x = x0; x < x1; block_x ++, x += x_incr) {
            x_incr = (x == x0) ? block_width - (x0 % block_width) : block_width;
            uint32_t block_x_offset = block_width - x_incr;
            x_incr = min<uint32_t>(x_incr, x1 - x);
            auto src_block = sa->data_blocks[(uint64_t)block_y * sa->block_count_hor + block_x];
            if (is_read_op) {
                if (src_block == NULL) { // if block is NULL, then zero out destination
                    if (buf_col_stride == 1) {
                        auto dest_ptr = buf + (y - y0) * (size_t)buf_line_stride +
                                              (x - x0) * buf_col_stride;
                        for (uint32_t j = 0; j < y_incr; j++) {
                            memset(dest_ptr, 0, sizeof(int32_t) * x_incr);
                            dest_ptr += buf_line_stride;
                        }
                    } else {
                        auto dest_ptr = buf + (y - y0) * (size_t)buf_line_stride +
                                              (x - x0) * buf_col_stride;
                        for (uint32_t j = 0; j < y_incr; j++) {
                            for (uint32_t k = 0; k < x_incr; k++)
                                dest_ptr[k * buf_col_stride] = 0;
                            dest_ptr += buf_line_stride;
                        }
                    }
                } else {
                    const int32_t* restrict src_ptr = src_block + (uint64_t)block_y_offset *
                                                            block_width + block_x_offset;
                    if (buf_col_stride == 1) {
                        int32_t* restrict dest_ptr = buf + (y - y0) * (size_t)buf_line_stride
                                                           +
                                                           (x - x0) * buf_col_stride;
                        if (x_incr == 4) {
                            /* Same code as general branch, but the compiler */
                            /* can have an efficient memcpy() */
                            (void)(x_incr); /* trick to silent cppcheck duplicateBranch warning */
                            for (uint32_t j = 0; j < y_incr; j++) {
                                memcpy(dest_ptr, src_ptr, sizeof(int32_t) * x_incr);
                                dest_ptr += buf_line_stride;
                                src_ptr  += block_width;
                            }
                        } else {
                            for (uint32_t j = 0; j < y_incr; j++) {
                                memcpy(dest_ptr, src_ptr, sizeof(int32_t) * x_incr);
                                dest_ptr += buf_line_stride;
                                src_ptr  += block_width;
                            }
                        }
                    } else {
                        int32_t* restrict dest_ptr = buf + (y - y0) * (size_t)buf_line_stride
                                                           +
                                                           (x - x0) * buf_col_stride;
                        if (x_incr == 1) {
                            for (uint32_t j = 0; j < y_incr; j++) {
                                *dest_ptr = *src_ptr;
                                dest_ptr += buf_line_stride;
                                src_ptr  += block_width;
                            }
                        } else if (y_incr == 1 && buf_col_stride == 2) {
                            uint32_t k;
                            for (k = 0; k < (x_incr & ~3U); k += 4) {
                                dest_ptr[k * buf_col_stride] = src_ptr[k];
                                dest_ptr[(k + 1) * buf_col_stride] = src_ptr[k + 1];
                                dest_ptr[(k + 2) * buf_col_stride] = src_ptr[k + 2];
                                dest_ptr[(k + 3) * buf_col_stride] = src_ptr[k + 3];
                            }
                            for (; k < x_incr; k++)
                                dest_ptr[k * buf_col_stride] = src_ptr[k];
                        } else if (x_incr >= 8 && buf_col_stride == 8) {
                            for (uint32_t j = 0; j < y_incr; j++) {
                                uint32_t k;
                                for (k = 0; k < (x_incr & ~3U); k += 4) {
                                    dest_ptr[k * buf_col_stride] = src_ptr[k];
                                    dest_ptr[(k + 1) * buf_col_stride] = src_ptr[k + 1];
                                    dest_ptr[(k + 2) * buf_col_stride] = src_ptr[k + 2];
                                    dest_ptr[(k + 3) * buf_col_stride] = src_ptr[k + 3];
                                }
                                for (; k < x_incr; k++)
                                    dest_ptr[k * buf_col_stride] = src_ptr[k];
                                dest_ptr += buf_line_stride;
                                src_ptr  += block_width;
                            }
                        } else {
                            /* General case */
                            for (uint32_t j = 0; j < y_incr; j++) {
                                for (uint32_t k = 0; k < x_incr; k++)
                                    dest_ptr[k * buf_col_stride] = src_ptr[k];
                                dest_ptr += buf_line_stride;
                                src_ptr  += block_width;
                            }
                        }
                    }
                }
            } else {
                if (src_block == NULL) {
                    src_block = (int32_t*) grk_calloc((uint64_t)sa->block_width * sa->block_height,
                                                         sizeof(int32_t));
                    if (src_block == NULL) {
                    	GROK_ERROR("Out of memory");
                        return false;
                    }
                    sa->data_blocks[(uint64_t)block_y * sa->block_count_hor + block_x] = src_block;
                }

                if (buf_col_stride == 1) {
                    int32_t* restrict dest_ptr = src_block + (uint64_t)block_y_offset *
                                                       	   	   	   block_width + block_x_offset;
                    const int32_t* restrict src_ptr = buf + (y - y0) *
                                                            (size_t)buf_line_stride + (x - x0) * buf_col_stride;
                    if (x_incr == 4) {
                        /* Same code as general branch, but the compiler */
                        /* can have an efficient memcpy() */
                        (void)(x_incr); /* trick to silent cppcheck duplicateBranch warning */
                        for (uint32_t j = 0; j < y_incr; j++) {
                            memcpy(dest_ptr, src_ptr, sizeof(int32_t) * x_incr);
                            dest_ptr += block_width;
                            src_ptr  += buf_line_stride;
                        }
                    } else {
                        for (uint32_t j = 0; j < y_incr; j++) {
                            memcpy(dest_ptr, src_ptr, sizeof(int32_t) * x_incr);
                            dest_ptr += block_width;
                            src_ptr  += buf_line_stride;
                        }
                    }
                } else {
                    int32_t* restrict dest_ptr = src_block + (uint64_t)block_y_offset *
                                                                  block_width + block_x_offset;
                    const int32_t* restrict src_ptr = buf + (y - y0) *
                                                            (size_t)buf_line_stride + (x - x0) * buf_col_stride;
                    if (x_incr == 1) {
                        for (uint32_t j = 0; j < y_incr; j++) {
                            *dest_ptr = *src_ptr;
                            src_ptr  += buf_line_stride;
                            dest_ptr += block_width;
                        }
                    } else if (x_incr >= 8 && buf_col_stride == 8) {
                        for (uint32_t j = 0; j < y_incr; j++) {
                            uint32_t k;
                            for (k = 0; k < (x_incr & ~3U); k += 4) {
                                dest_ptr[k] = src_ptr[k * buf_col_stride];
                                dest_ptr[k + 1] = src_ptr[(k + 1) * buf_col_stride];
                                dest_ptr[k + 2] = src_ptr[(k + 2) * buf_col_stride];
                                dest_ptr[k + 3] = src_ptr[(k + 3) * buf_col_stride];
                            }
                            for (; k < x_incr; k++)
                                dest_ptr[k] = src_ptr[k * buf_col_stride];
                            src_ptr  += buf_line_stride;
                            dest_ptr += block_width;
                        }
                    } else {
                        /* General case */
                        for (uint32_t j = 0; j < y_incr; j++) {
                            for (uint32_t k = 0; k < x_incr; k++)
                                dest_ptr[k] = src_ptr[k * buf_col_stride];
                            src_ptr  += buf_line_stride;
                            dest_ptr += block_width;
                        }
                    }
                }
            }
        }
    }

    return true;
}

bool sparse_array_read(const sparse_array* sa,
                                     uint32_t x0,
                                     uint32_t y0,
                                     uint32_t x1,
                                     uint32_t y1,
                                     int32_t* dest,
                                     uint32_t dest_col_stride,
                                     uint32_t dest_line_stride,
                                     bool forgiving)
{
    return sparse_array_read_or_write(
               (sparse_array*)sa, x0, y0, x1, y1,
               dest,
               dest_col_stride,
               dest_line_stride,
               forgiving,
               true);
}

bool sparse_array_write(sparse_array* sa,
                                      uint32_t x0,
                                      uint32_t y0,
                                      uint32_t x1,
                                      uint32_t y1,
                                      const int32_t* src,
                                      uint32_t src_col_stride,
                                      uint32_t src_line_stride,
                                      bool forgiving)
{
    return sparse_array_read_or_write(sa, x0, y0, x1, y1,
            (int32_t*)src,
            src_col_stride,
            src_line_stride,
            forgiving,
            false);
}
