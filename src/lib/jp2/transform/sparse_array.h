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

#ifndef OPJ_SPARSE_ARRAY_H
#define OPJ_SPARSE_ARRAY_H
/**
@file sparse_array.h
@brief Sparse array management

The functions in this file manage sparse arrays. Sparse arrays are arrays with
potential big dimensions, but with very few samples actually set. Such sparse
arrays require allocating a low amount of memory, by just allocating memory
for blocks of the array that are set. The minimum memory allocation unit is a
a block. There is a trade-off to pick up an appropriate dimension for blocks.
If it is too big, and pixels set are far from each other, too much memory will
be used. If blocks are too small, the book-keeping costs of blocks will raise.
*/

/** @defgroup SPARSE_ARRAY SPARSE ARRAYS - Sparse arrays */
/*@{*/

/** Opaque type for sparse arrays that contain int32 values */
typedef struct sparse_array sparse_array_t;

/** Creates a new sparse array.
 * @param width total width of the array.
 * @param height total height of the array
 * @param block_width width of a block.
 * @param block_height height of a block.
 * @return a new sparse array instance, or NULL in case of failure.
 */
sparse_array_t* sparse_array_create(uint32_t width,
        uint32_t height,
        uint32_t block_width,
        uint32_t block_height);

/** Frees a sparse array.
 * @param sa sparse array instance.
 */
void sparse_array_free(sparse_array_t* sa);

/** Returns whether region bounds are valid (non empty and within array bounds)
 * @param sa sparse array instance.
 * @param x0 left x coordinate of the region.
 * @param y0 top x coordinate of the region.
 * @param x1 right x coordinate (not included) of the region. Must be greater than x0.
 * @param y1 bottom y coordinate (not included) of the region. Must be greater than y0.
 * @return true or false.
 */
bool sparse_array_is_region_valid(const sparse_array_t* sa,
        uint32_t x0,
        uint32_t y0,
        uint32_t x1,
        uint32_t y1);

/** Read the content of a rectangular region of the sparse array into a
 * user buffer.
 *
 * Regions not written with sparse_array_write() are read as 0.
 *
 * @param sa sparse array instance.
 * @param x0 left x coordinate of the region to read in the sparse array.
 * @param y0 top x coordinate of the region to read in the sparse array.
 * @param x1 right x coordinate (not included) of the region to read in the sparse array. Must be greater than x0.
 * @param y1 bottom y coordinate (not included) of the region to read in the sparse array. Must be greater than y0.
 * @param dest user buffer to fill. Must be at least sizeof(int32) * ( (y1 - y0 - 1) * dest_line_stride + (x1 - x0 - 1) * dest_col_stride + 1) bytes large.
 * @param dest_col_stride spacing (in elements, not in bytes) in x dimension between consecutive elements of the user buffer.
 * @param dest_line_stride spacing (in elements, not in bytes) in y dimension between consecutive elements of the user buffer.
 * @param forgiving if set to TRUE and the region is invalid, true will still be returned.
 * @return true in case of success.
 */
bool sparse_array_read(const sparse_array_t* sa,
                                     uint32_t x0,
                                     uint32_t y0,
                                     uint32_t x1,
                                     uint32_t y1,
                                     int32_t* dest,
                                     uint32_t dest_col_stride,
                                     uint32_t dest_line_stride,
                                     bool forgiving);


/** Write the content of a rectangular region into the sparse array from a
 * user buffer.
 *
 * Blocks intersecting the region are allocated, if not already done.
 *
 * @param sa sparse array instance.
 * @param x0 left x coordinate of the region to write into the sparse array.
 * @param y0 top x coordinate of the region to write into the sparse array.
 * @param x1 right x coordinate (not included) of the region to write into the sparse array. Must be greater than x0.
 * @param y1 bottom y coordinate (not included) of the region to write into the sparse array. Must be greater than y0.
 * @param src user buffer to fill. Must be at least sizeof(int32) * ( (y1 - y0 - 1) * src_line_stride + (x1 - x0 - 1) * src_col_stride + 1) bytes large.
 * @param src_col_stride spacing (in elements, not in bytes) in x dimension between consecutive elements of the user buffer.
 * @param src_line_stride spacing (in elements, not in bytes) in y dimension between consecutive elements of the user buffer.
 * @param forgiving if set to TRUE and the region is invalid, true will still be returned.
 * @return true in case of success.
 */
bool sparse_array_write(sparse_array_t* sa,
                                      uint32_t x0,
                                      uint32_t y0,
                                      uint32_t x1,
                                      uint32_t y1,
                                      const int32_t* src,
                                      uint32_t src_col_stride,
                                      uint32_t src_line_stride,
                                      bool forgiving);

/*@}*/

#endif /* OPJ_SPARSE_ARRAY_H */
