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

#include <grk_includes.h>
#include <algorithm>
#include <cstdint>
#include <cassert>
#include <exception>

using namespace std;

namespace grk {


SparseBuffer::SparseBuffer(uint32_t width,
							uint32_t height,
							uint32_t log2_bw,
							uint32_t log2_bh) :
									width(width),
									height(height),
									log2_block_width(log2_bw),
									log2_block_height(log2_bh),
									block_width(1<<log2_bw),
									block_height(1<<log2_bh),
									block_area(block_width*block_height)
{
    if (!width  || !height  || !log2_block_width || !log2_block_height)
    	throw std::runtime_error("invalid region for sparse array");
    block_count_hor = ceildiv<uint32_t>(width, block_width);
    block_count_ver = ceildiv<uint32_t>(height, block_height);
    auto block_count = (uint64_t)block_count_hor * block_count_ver;
    data_blocks = new int32_t*[block_count];
    for (uint64_t i = 0; i < block_count; ++i)
    	data_blocks[i] = nullptr;

}

SparseBuffer::~SparseBuffer()
{
	for (uint64_t i = 0; i < (uint64_t)block_count_hor * block_count_ver; i++)
		grk_free(data_blocks[i]);
	grk_free(data_blocks);
}

bool SparseBuffer::is_region_valid(
        uint32_t x0,
        uint32_t y0,
        uint32_t x1,
        uint32_t y1)
{
    return !(x0 >= width || x1 <= x0 || x1 > width ||
             y0 >= height || y1 <= y0 || y1 > height);
}
bool SparseBuffer::alloc( uint32_t x0,
						  uint32_t y0,
						  uint32_t x1,
						  uint32_t y1){
    if (!SparseBuffer::is_region_valid(x0, y0, x1, y1))
        return true;

    uint32_t y_incr = 0;
    uint32_t block_y = y0 >> log2_block_height;
    for (uint32_t y = y0; y < y1; block_y ++, y += y_incr) {
        y_incr = (y == y0) ? block_height - (y0 & (block_height-1)) : block_height;
        y_incr = min<uint32_t>(y_incr, y1 - y);
        uint32_t block_x = x0 >>  log2_block_width;
        uint32_t x_incr = 0;
        for (uint32_t x = x0; x < x1; block_x ++, x += x_incr) {
            x_incr = (x == x0) ? block_width - (x0 & (block_width-1)) : block_width;
            x_incr = min<uint32_t>(x_incr, x1 - x);
            auto src_block = data_blocks[(uint64_t)block_y * block_count_hor + block_x];
			if (!src_block) {
				src_block = (int32_t*) grk_calloc(block_area, sizeof(int32_t));
				if (!src_block) {
					GRK_ERROR("SparseBuffer: Out of memory");
					return false;
				}
				data_blocks[(uint64_t)block_y * block_count_hor + block_x] = src_block;
			}
        }
    }

    return true;
}

bool SparseBuffer::read_or_write(uint32_t x0,
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
    uint32_t block_y 			= y0 >>  log2_block_height;
    uint32_t y_incr = 0;
    for (uint32_t y = y0; y < y1; block_y ++, y += y_incr) {
        y_incr = (y == y0) ? block_height - (y0 & (block_height-1)) : block_height;
        uint32_t block_y_offset = block_height - y_incr;
        y_incr = min<uint32_t>(y_incr, y1 - y);
        uint32_t block_x = x0 >> log2_block_width;
        uint32_t x_incr = 0;
        for (uint32_t x = x0; x < x1; block_x ++, x += x_incr) {
            x_incr = (x == x0) ? block_width - (x0 & (block_width-1) ) : block_width;
            uint32_t block_x_offset = block_width - x_incr;
            x_incr = min<uint32_t>(x_incr, x1 - x);
            auto src_block = data_blocks[(uint64_t)block_y * block_count_hor + block_x];
            if (is_read_op) {
            	assert(src_block);
				const int32_t* GRK_RESTRICT src_ptr =
						src_block + ((uint64_t)block_y_offset << log2_block_width) + block_x_offset;
				if (col_stride == 1) {
					int32_t* GRK_RESTRICT dest_ptr = buf + (y - y0) * line_stride +
													   (x - x0) * col_stride;
					if (x_incr == 4) {
						/* Same code as general branch, but the compiler */
						/* can have an efficient memcpy() */
						(void)(x_incr); /* trick to silent cppcheck duplicateBranch warning */
						for (uint32_t j = 0; j < y_incr; j++) {
							memcpy(dest_ptr, src_ptr, x_incr << 2); // << 2 == * sizeof(int32_t)
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
					int32_t* GRK_RESTRICT dest_ptr = buf +	(y - y0) * (size_t)line_stride +
													   (x - x0) * col_stride;
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
            	//all blocks should be allocated first before read/write is called
                assert(src_block);
                if (col_stride == 1) {
                    int32_t* GRK_RESTRICT dest_ptr = src_block + ((uint64_t)block_y_offset << log2_block_width) + block_x_offset;
                    const int32_t* GRK_RESTRICT src_ptr = buf + (y - y0) * line_stride + (x - x0) * col_stride;
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
                    int32_t* GRK_RESTRICT dest_ptr = src_block + ((uint64_t)block_y_offset << log2_block_width) + block_x_offset;
                    const int32_t* GRK_RESTRICT src_ptr = buf + (y - y0) * line_stride + (x - x0) * col_stride;
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

bool SparseBuffer::read(grk_rect_u32 region,
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


bool SparseBuffer::read(uint32_t x0,
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

bool SparseBuffer::write(uint32_t x0,
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


}
