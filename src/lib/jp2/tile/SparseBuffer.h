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

#pragma once

/**
@file SparseBuffer.h
@brief Sparse array management

The functions in this file manage sparse arrays. Sparse arrays are arrays with
potentially large dimensions, but with very few samples actually set. Such sparse
arrays require allocating a small amount of memory, by just allocating memory
for blocks of the array that are set. The minimum memory allocation unit is a
a block. There is a trade-off to pick up an appropriate dimension for blocks.
If it is too big, and pixels set are far from each other, too much memory will
be used. If blocks are too small, the book-keeping costs of blocks will rise.
*/

/** @defgroup SparseBuffer SPARSE BUFFER - Sparse buffers */
/*@{*/

#include <cstdint>

namespace grk {

class ISparseBuffer {
public:

	virtual ~ISparseBuffer(){};

	/** Read the content of a rectangular region of the sparse array into a
	 * user buffer.
	 *
	 * Regions not written with write() are read as 0.
	 *
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
	virtual bool read(uint32_t x0,
					 uint32_t y0,
					 uint32_t x1,
					 uint32_t y1,
					 int32_t* dest,
					 const uint32_t dest_col_stride,
					 const uint32_t dest_line_stride,
					 bool forgiving) = 0;

	/** Read the content of a rectangular region of the sparse array into a
	 * user buffer.
	 *
	 * Regions not written with write() are read as 0.
	 *
	 * @param region region to read in the sparse array.
	 * @param dest user buffer to fill. Must be at least sizeof(int32) * ( (y1 - y0 - 1) * dest_line_stride + (x1 - x0 - 1) * dest_col_stride + 1) bytes large.
	 * @param dest_col_stride spacing (in elements, not in bytes) in x dimension between consecutive elements of the user buffer.
	 * @param dest_line_stride spacing (in elements, not in bytes) in y dimension between consecutive elements of the user buffer.
	 * @param forgiving if set to TRUE and the region is invalid, true will still be returned.
	 * @return true in case of success.
	 */
	virtual bool read(grk_rect_u32 region,
						 int32_t* dest,
						 const uint32_t dest_col_stride,
						 const uint32_t dest_line_stride,
						 bool forgiving) = 0;


	/** Write the content of a rectangular region into the sparse array from a
	 * user buffer.
	 *
	 * Blocks intersecting the region are allocated, if not already done.
	 *
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
	virtual bool write(uint32_t x0,
						  uint32_t y0,
						  uint32_t x1,
						  uint32_t y1,
						  const int32_t* src,
						  const uint32_t src_col_stride,
						  const uint32_t src_line_stride,
						  bool forgiving) = 0;


	/** Allocate all blocks for a rectangular region into the sparse array from a
	 * user buffer.
	 *
	 * Blocks intersecting the region are allocated
	 *
	 * @param x0 left x coordinate of the region to write into the sparse array.
	 * @param y0 top x coordinate of the region to write into the sparse array.
	 * @param x1 right x coordinate (not included) of the region to write into the sparse array. Must be greater than x0.
	 * @param y1 bottom y coordinate (not included) of the region to write into the sparse array. Must be greater than y0.
	 * @return true in case of success.
	 */
	virtual bool alloc( uint32_t x0,
					  uint32_t y0,
					  uint32_t x1,
					  uint32_t y1) = 0;


};

template<uint32_t LBW, uint32_t LBH> class SparseBuffer : public ISparseBuffer {

public:

	/** Creates a new sparse array.
	 *
	 * @param width total width of the array.
	 * @param height total height of the array
	 *
	 * @return a new sparse array instance, or NULL in case of failure.
	 */
	SparseBuffer(uint32_t w,uint32_t h) :
										buffer_width(w),
										buffer_height(h),
										block_width(1<<LBW),
										block_height(1<<LBH)
	{
	    if (!buffer_width  || !buffer_height  || !LBW || !LBH) {
	    	throw std::runtime_error("invalid region for sparse array");
		}
	    grid_width = ceildiv<uint32_t>(buffer_width, block_width);
	    grid_height = ceildiv<uint32_t>(buffer_height, block_height);
	    auto block_count = (uint64_t)grid_width * grid_height;
	    data_blocks = new int32_t*[block_count];
	    for (uint64_t i = 0; i < block_count; ++i)
	    	data_blocks[i] = nullptr;

	}

	/** Frees a sparse array.
	 *
	 */
	~SparseBuffer()
	{
		for (uint64_t i = 0; i < (uint64_t)grid_width * grid_height; i++)
			grk_free(data_blocks[i]);
		grk_free(data_blocks);
	}
	bool read(uint32_t x0,
			 uint32_t y0,
			 uint32_t x1,
			 uint32_t y1,
			 int32_t* dest,
			 const uint32_t dest_col_stride,
			 const uint32_t dest_line_stride,
			 bool forgiving)
	{
	    return read_or_write( x0, y0, x1, y1,
							   dest,
							   dest_col_stride,
							   dest_line_stride,
							   forgiving,
							   true);
	}

	bool read(grk_rect_u32 region,
			 int32_t* dest,
			 const uint32_t dest_col_stride,
			 const uint32_t dest_line_stride,
			 bool forgiving)
	{
		return read(region.x0,
				region.y0,
				region.x1,
				region.y1,
				dest,
				dest_col_stride,
				dest_line_stride,
				forgiving);
	}

	bool write(uint32_t x0,
			  uint32_t y0,
			  uint32_t x1,
			  uint32_t y1,
			  const int32_t* src,
			  const uint32_t src_col_stride,
			  const uint32_t src_line_stride,
			  bool forgiving)
	{
	    return read_or_write(x0, y0, x1, y1,
	            (int32_t*)src,
	            src_col_stride,
	            src_line_stride,
	            forgiving,
	            false);
	}

	bool alloc( uint32_t x0,
				  uint32_t y0,
				  uint32_t x1,
				  uint32_t y1){
	    if (!SparseBuffer::is_region_valid(x0, y0, x1, y1))
	        return true;

	    uint32_t y_incr = 0;
	    uint32_t block_y = y0 >> LBH;
	    for (uint32_t y = y0; y < y1; block_y ++, y += y_incr) {
	        y_incr = (y == y0) ? block_height - (y0 & (block_height-1)) : block_height;
	        y_incr = min<uint32_t>(y_incr, y1 - y);
	        uint32_t block_x = x0 >>  LBW;
	        uint32_t x_incr = 0;
	        for (uint32_t x = x0; x < x1; block_x ++, x += x_incr) {
	            x_incr = (x == x0) ? block_width - (x0 & (block_width-1)) : block_width;
	            x_incr = min<uint32_t>(x_incr, x1 - x);
	            auto src_block = data_blocks[(uint64_t)block_y * grid_width + block_x];
				if (!src_block) {
					const uint32_t block_area = block_width*block_height;
					// note: we need to zero out each source block, in case
					// some code blocks are missing from the compressed stream.
					// In this case, zero is the best default value for the block.
					src_block = (int32_t*) grk_calloc(block_area, sizeof(int32_t));
					if (!src_block) {
						GRK_ERROR("SparseBuffer: Out of memory");
						return false;
					}
					data_blocks[(uint64_t)block_y * grid_width + block_x] = src_block;
				}
	        }
	    }

	    return true;
	}

private:

	/** Returns whether region bounds are valid (non empty and within array bounds)
	 * @param x0 left x coordinate of the region.
	 * @param y0 top x coordinate of the region.
	 * @param x1 right x coordinate (not included) of the region. Must be greater than x0.
	 * @param y1 bottom y coordinate (not included) of the region. Must be greater than y0.
	 * @return true or false.
	 */
	bool is_region_valid(uint32_t x0,
						uint32_t y0,
						uint32_t x1,
						uint32_t y1){
	    return !(x0 >= buffer_width || x1 <= x0 || x1 > buffer_width ||
	             y0 >= buffer_height || y1 <= y0 || y1 > buffer_height);
	}

	bool read_or_write(uint32_t x0,
						uint32_t y0,
						uint32_t x1,
						uint32_t y1,
						int32_t* buf,
						const uint32_t buf_col_stride,
						const uint32_t buf_line_stride,
						bool forgiving,
						bool is_read_op){
	    if (!is_region_valid(x0, y0, x1, y1))
	        return forgiving;

	    const uint64_t line_stride 	= buf_line_stride;
	    const uint64_t col_stride 	= buf_col_stride;
	    uint32_t block_y 			= y0 >>  LBH;
	    uint32_t y_incr = 0;
	    for (uint32_t y = y0; y < y1; block_y ++, y += y_incr) {
	        y_incr = (y == y0) ? block_height - (y0 & (block_height-1)) : block_height;
	        uint32_t block_y_offset = block_height - y_incr;
	        y_incr = min<uint32_t>(y_incr, y1 - y);
	        uint32_t block_x = x0 >> LBW;
	        uint32_t x_incr = 0;
	        for (uint32_t x = x0; x < x1; block_x ++, x += x_incr) {
	            x_incr = (x == x0) ? block_width - (x0 & (block_width-1) ) : block_width;
	            uint32_t block_x_offset = block_width - x_incr;
	            x_incr = min<uint32_t>(x_incr, x1 - x);
	            auto src_block = data_blocks[(uint64_t)block_y * grid_width + block_x];
            	//all blocks should be allocated first before read/write is called
                assert(src_block);
	            if (is_read_op) {
					const int32_t* GRK_RESTRICT src_ptr =
							src_block + ((uint64_t)block_y_offset << LBW) + block_x_offset;
					int32_t* GRK_RESTRICT dest_ptr = buf + (y - y0) * line_stride +
													   (x - x0) * col_stride;
					if (col_stride == 1) {
						if (x_incr == 4) {
							/* Same code as general branch, but the compiler */
							/* can have an efficient memcpy() */
							(void)(x_incr); /* trick to silent cppcheck duplicateBranch warning */
							for (uint32_t j = 0; j < y_incr; j++) {
								memcpy(dest_ptr, src_ptr, 4 << 2); // << 2 == * sizeof(int32_t)
								dest_ptr += line_stride;
								src_ptr  += block_width;
							}
						} else {
							for (uint32_t j = 0; j < y_incr; j++) {
								memcpy(dest_ptr, src_ptr, x_incr << 2);
								dest_ptr += line_stride;
								src_ptr  += block_width;
							}
						}
					} else {
						if (x_incr == 1) {
							for (uint32_t j = 0; j < y_incr; j++) {
								*dest_ptr = *src_ptr;
								dest_ptr += line_stride;
								src_ptr  += block_width;
							}
						} else if (y_incr == 1 && col_stride == 2) {
							uint32_t k;
							for (k = 0; k < (x_incr & ~3U); k += 4) {
								dest_ptr[k << 1] = src_ptr[k];
								dest_ptr[(k + 1) << 1] = src_ptr[k + 1];
								dest_ptr[(k + 2) << 1] = src_ptr[k + 2];
								dest_ptr[(k + 3) << 1] = src_ptr[k + 3];
							}
							for (; k < x_incr; k++)
								dest_ptr[k << 1] = src_ptr[k];
						} else if (x_incr >= 8 && col_stride == 8) {
							for (uint32_t j = 0; j < y_incr; j++) {
								uint32_t k;
								for (k = 0; k < (x_incr & ~3U); k += 4) {
									dest_ptr[k << 3] = src_ptr[k];
									dest_ptr[(k + 1) << 3] = src_ptr[k + 1];
									dest_ptr[(k + 2) << 3] = src_ptr[k + 2];
									dest_ptr[(k + 3) << 3] = src_ptr[k + 3];
								}
								uint64_t ind = k << 3;
								for (; k < x_incr; k++) {
									dest_ptr[ind] = src_ptr[k];
									ind += col_stride;
								}
								dest_ptr += line_stride;
								src_ptr  += block_width;
							}
						} else {
							/* General case */
							for (uint32_t j = 0; j < y_incr; j++) {
								uint64_t ind = 0;
								for (uint32_t k = 0; k < x_incr; k++){
									dest_ptr[ind] = src_ptr[k];
									ind += col_stride;
								}
								dest_ptr += line_stride;
								src_ptr  += block_width;
							}
						}
					}

	            } else {
                    const int32_t* GRK_RESTRICT src_ptr = buf + (y - y0) * line_stride + (x - x0) * col_stride;
                    int32_t* GRK_RESTRICT dest_ptr = src_block + ((uint64_t)block_y_offset << LBW) + block_x_offset;
	                if (col_stride == 1) {
	                    if (x_incr == 4) {
	                        /* Same code as general branch, but the compiler */
	                        /* can have an efficient memcpy() */
	                        (void)(x_incr); /* trick to silent cppcheck duplicateBranch warning */
	                        for (uint32_t j = 0; j < y_incr; j++) {
	                            memcpy(dest_ptr, src_ptr, x_incr << 2);
	                            dest_ptr += block_width;
	                            src_ptr  += line_stride;
	                        }
	                    } else {
	                        for (uint32_t j = 0; j < y_incr; j++) {
	                            memcpy(dest_ptr, src_ptr, x_incr << 2);
	                            dest_ptr += block_width;
	                            src_ptr  += line_stride;
	                        }
	                    }
	                } else {
	                    if (x_incr == 1) {
	                        for (uint32_t j = 0; j < y_incr; j++) {
	                            *dest_ptr = *src_ptr;
	                            src_ptr  += line_stride;
	                            dest_ptr += block_width;
	                        }
	                    } else if (x_incr >= 8 && col_stride == 8) {
	                        for (uint32_t j = 0; j < y_incr; j++) {
	                            uint32_t k;
	                            for (k = 0; k < (x_incr & ~3U); k += 4) {
	                                dest_ptr[k] = src_ptr[k << 3];
	                                dest_ptr[k + 1] = src_ptr[(k + 1) << 3];
	                                dest_ptr[k + 2] = src_ptr[(k + 2) << 3];
	                                dest_ptr[k + 3] = src_ptr[(k + 3) << 3];
	                            }
	                            for (; k < x_incr; k++)
	                                dest_ptr[k] = src_ptr[k << 3];
	                            src_ptr  += line_stride;
	                            dest_ptr += block_width;
	                        }
	                    } else {
	                        /* General case */
	                        for (uint32_t j = 0; j < y_incr; j++) {
								uint64_t ind = 0;
	                            for (uint32_t k = 0; k < x_incr; k++) {
	                            	dest_ptr[k] = src_ptr[ind];
	                            	ind += col_stride;
	                            }
	                            src_ptr  += line_stride;
	                            dest_ptr += block_width;
	                        }
	                    }
	                }
	            }
	        }
	    }

	    return true;
	}
private:

	uint32_t buffer_width;
    uint32_t buffer_height;
    const uint32_t block_width;
    const uint32_t block_height;
    uint32_t grid_width;
    uint32_t grid_height;
    int32_t** data_blocks;
};

}

/*@}*/

