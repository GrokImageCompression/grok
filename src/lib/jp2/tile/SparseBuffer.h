/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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

	/** Read the content of a rectangular window of the sparse array into a
	 * user buffer.
	 *
	 * Windows not written with write() are read as 0.
	 *
	 * @param x0 left x coordinate of the window to read in the sparse array.
	 * @param y0 top x coordinate of the window to read in the sparse array.
	 * @param x1 right x coordinate (not included) of the window to read in the sparse array. Must be greater than x0.
	 * @param y1 bottom y coordinate (not included) of the window to read in the sparse array. Must be greater than y0.
	 * @param dest user buffer to fill. Must be at least sizeof(int32) * ( (y1 - y0 - 1) * dest_line_stride + (x1 - x0 - 1) * dest_col_stride + 1) bytes large.
	 * @param dest_col_stride spacing (in elements, not in bytes) in x dimension between consecutive elements of the user buffer.
	 * @param dest_line_stride spacing (in elements, not in bytes) in y dimension between consecutive elements of the user buffer.
	 * @param forgiving if set to TRUE and the window is invalid, true will still be returned.
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

	/** Read the content of a rectangular window of the sparse array into a
	 * user buffer.
	 *
	 * Windows not written with write() are read as 0.
	 *
	 * @param window window to read in the sparse array.
	 * @param dest user buffer to fill. Must be at least sizeof(int32) * ( (y1 - y0 - 1) * dest_line_stride + (x1 - x0 - 1) * dest_col_stride + 1) bytes large.
	 * @param dest_col_stride spacing (in elements, not in bytes) in x dimension between consecutive elements of the user buffer.
	 * @param dest_line_stride spacing (in elements, not in bytes) in y dimension between consecutive elements of the user buffer.
	 * @param forgiving if set to TRUE and the window is invalid, true will still be returned.
	 * @return true in case of success.
	 */
	virtual bool read(grk_rect_u32 window,
						 int32_t* dest,
						 const uint32_t dest_col_stride,
						 const uint32_t dest_line_stride,
						 bool forgiving) = 0;


	/** Write the content of a rectangular window into the sparse array from a
	 * user buffer.
	 *
	 * Blocks intersecting the window are allocated, if not already done.
	 *
	 * @param x0 left x coordinate of the window to write into the sparse array.
	 * @param y0 top x coordinate of the window to write into the sparse array.
	 * @param x1 right x coordinate (not included) of the window to write into the sparse array. Must be greater than x0.
	 * @param y1 bottom y coordinate (not included) of the window to write into the sparse array. Must be greater than y0.
	 * @param src user buffer to fill. Must be at least sizeof(int32) * ( (y1 - y0 - 1) * src_line_stride + (x1 - x0 - 1) * src_col_stride + 1) bytes large.
	 * @param src_col_stride spacing (in elements, not in bytes) in x dimension between consecutive elements of the user buffer.
	 * @param src_line_stride spacing (in elements, not in bytes) in y dimension between consecutive elements of the user buffer.
	 * @param forgiving if set to TRUE and the window is invalid, true will still be returned.
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


	/** Allocate all blocks for a rectangular window into the sparse array from a
	 * user buffer.
	 *
	 * Blocks intersecting the window are allocated
	 *
	 * @param window window to write into the sparse array.
	 *
	 * @return true in case of success.
	 */
	virtual bool alloc( grk_rect_u32 window) = 0;
};

struct SparseBlock{
	SparseBlock(void) : data(nullptr)
	{}
	~SparseBlock() {
		delete[] data;
	}
	bool alloc(uint32_t block_area){
		data = new int32_t[block_area];
		// note: we need to zero out each source block, in case
		// some code blocks are missing from the compressed stream.
		// In this case, zero is the best default value for the block.
		memset(data, 0, block_area * sizeof(int32_t));
		return true;
	}
	int32_t *data;
};

template<uint32_t LBW, uint32_t LBH> class SparseBuffer : public ISparseBuffer {

public:

	/** Creates a new sparse array.
	 *
	 * @param bds bounds
	 *
	 * @return a new sparse array instance, or NULL in case of failure.
	 */
	SparseBuffer(grk_rect_u32 bds) :	block_width(1<<LBW),
										block_height(1<<LBH),
										data_blocks(nullptr),
										bounds(bds)
	{
	    if (!bounds.width()  || !bounds.height()  || !LBW || !LBH) {
	    	throw std::runtime_error("invalid window for sparse array");
		}
	    uint32_t grid_off_x = uint_floordivpow2(bounds.x0, LBW);
	    uint32_t grid_off_y = uint_floordivpow2(bounds.y0, LBH);
	    uint32_t grid_width = ceildivpow2<uint32_t>(bounds.width(), LBW);
	    uint32_t grid_height = ceildivpow2<uint32_t>(bounds.height(), LBH);
	    grid_bounds = grk_rect_u32(grid_off_x,
	    							grid_off_y,
									grid_off_x+grid_width,
									grid_off_y + grid_height);
	    auto block_count = grid_bounds.area();
	    data_blocks = new SparseBlock*[block_count];
	    for (uint64_t i = 0; i < block_count; ++i){
	    	data_blocks[i] = nullptr;
	    }

#ifdef GRK_DEBUG_VALGRIND
	    validate = grk_rect_u32(278,50,288,51);
#endif
	}


	/** Creates a new sparse array.
	 *
	 * @param width total width of the array.
	 * @param height total height of the array
	 *
	 * @return a new sparse array instance, or NULL in case of failure.
	 */
	SparseBuffer(uint32_t width,uint32_t height) : SparseBuffer(grk_rect_u32(0,0,width,height))
	{}


	/** Frees a sparse array.
	 *
	 */
	~SparseBuffer()
	{
		if (data_blocks) {
			for (uint64_t i = 0; i < (uint64_t)grid_bounds.width() * grid_bounds.height(); i++){
				delete (data_blocks[i]);
				data_blocks[i] = nullptr;
			}
			delete[] data_blocks;
		}
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

	bool read(grk_rect_u32 window,
			 int32_t* dest,
			 const uint32_t dest_col_stride,
			 const uint32_t dest_line_stride,
			 bool forgiving)
	{
		return read(window.x0,
				window.y0,
				window.x1,
				window.y1,
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

	bool alloc( grk_rect_u32 window){
		return alloc(window.x0, window.y0, window.x1, window.y1);
	}
private:

	bool alloc( uint32_t x0,
				  uint32_t y0,
				  uint32_t x1,
				  uint32_t y1){
	    if (!SparseBuffer::is_window_valid(x0, y0, x1, y1))
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
	    		if (!grid_bounds.contains(grk_pt(block_x,block_y))){
	    			GRK_ERROR("Attempt to allocate a block (%d,%d) outside block grid bounds", block_x, block_y);
	    			return false;
	    		}
	            auto src_block = getBlock(block_x, block_y);
				if (!src_block) {
					auto b = new SparseBlock();
					if (!b->alloc(block_width*block_height))
						return false;
					assert(grid_bounds.contains(grk_pt(block_x,block_y)));
					assert(b->data);
					uint64_t index = (uint64_t)(block_y - grid_bounds.y0) * grid_bounds.width() + (block_x - grid_bounds.x0);
					data_blocks[index] = b;
				}
	        }
	    }

	    return true;
	}

	inline SparseBlock* getBlock(uint32_t block_x, uint32_t block_y){
		uint64_t index = (uint64_t)(block_y - grid_bounds.y0) * grid_bounds.width() + (block_x - grid_bounds.x0);
		auto b =  data_blocks[index];
#ifdef GRK_DEBUG_VALGRIND
/*
	if (b) {
		size_t val = VALGRIND_CHECK_MEM_IS_DEFINED(b->data,block_width*block_height * sizeof(int32_t));
		if (val) {
		   for (size_t i = 0; i < block_width*block_height; ++i){
		   	   size_t val = grk_memcheck<int32_t>(b->data + i,1);
			   if (val != grk_mem_ok) {
				   uint32_t x = block_x * block_width + i % block_width;
				   uint32_t y = block_y * block_height + i / block_width;
				   printf("Sparse array get block (%d.%d): uninitialized at location (%d,%d)\n",
						    block_x, block_y, x,y );
			   }
		   }

		}
	}
*/
#endif

		return b;
	}

	/** Returns whether window bounds are valid (non empty and within array bounds)
	 * @param x0 left x coordinate of the window.
	 * @param y0 top x coordinate of the window.
	 * @param x1 right x coordinate (not included) of the window. Must be greater than x0.
	 * @param y1 bottom y coordinate (not included) of the window. Must be greater than y0.
	 * @return true or false.
	 */
	bool is_window_valid(uint32_t x0,
						uint32_t y0,
						uint32_t x1,
						uint32_t y1){
	    return !(x0 >= bounds.width() || x1 <= x0 || x1 > bounds.width() ||
	             y0 >= bounds.height() || y1 <= y0 || y1 > bounds.height());
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
	    if (!is_window_valid(x0, y0, x1, y1)){
	    	// fill the client buffer with zeros in this case
	    	if (forgiving && is_read_op){
	    		//GRK_WARN("Sparse buffer: attempt to read invalid window (%d,%d,%d,%d). Filling with zeros.", x0,y0,x1,y1);
	    		for (uint32_t y = y0; y < y1; ++y){
	    	    	int32_t *bufPtr = buf + (y - y0)  * buf_line_stride;
		    		for (uint32_t x = x0; x < x1; ++x){
		    			*bufPtr = 0;
		    			bufPtr += buf_col_stride;
		    		}
	    		}
	    	}
	        return forgiving;
	    }

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
	    		if (!grid_bounds.contains(grk_pt(block_x,block_y))){
	    			GRK_ERROR("Attempt to access a block (%d,%d) outside block grid bounds", block_x, block_y);
	    			return false;
	    		}
	            auto src_block = getBlock(block_x, block_y);
            	//all blocks should be allocated first before read/write is called
	            if (!src_block){
	            	GRK_ERROR("Sparse array: missing block (%d,%d,%d,%d) for %s (%d,%d,%d,%d)",
	            			block_x*block_width,
						   block_y*block_height,
						   block_x*block_width + x_incr,
						   block_y*block_height + y_incr,
						   is_read_op ? "read" : "write",
						   x0,y0,x1,y1);
	            	throw MissingSparseBlockException();
	            }
	            if (is_read_op) {
					const int32_t* GRK_RESTRICT src_ptr =
							src_block->data + ((uint64_t)block_y_offset << LBW) + block_x_offset;
					int32_t* GRK_RESTRICT dest_ptr = buf + (y - y0) * line_stride +
													   (x - x0) * col_stride;
					uint32_t y_ = y;
					for (uint32_t j = 0; j < y_incr; j++) {
						uint64_t ind = 0;
						for (uint32_t k = 0; k < x_incr; k++){
#ifdef GRK_DEBUG_VALGRIND
							grk_pt pt((uint32_t)(x+k), y_);
							if (validate.contains(pt)) {
								size_t val = grk_memcheck<int32_t>(src_ptr+k,1);
								if (val != grk_mem_ok)
								   GRK_ERROR("Sparse array read block(%d,%d) : uninitialized at location (%d,%d)",
										   block_x, block_y, x+k,y_);
							}
#endif
							dest_ptr[ind] = src_ptr[k];
							ind += col_stride;
						}
						dest_ptr += line_stride;
						y_ ++;
						src_ptr  += block_width;
					}
	            } else {
                    const int32_t* GRK_RESTRICT src_ptr = buf + (y - y0) * line_stride + (x - x0) * col_stride;
                    int32_t* GRK_RESTRICT dest_ptr = src_block->data + ((uint64_t)block_y_offset << LBW) + block_x_offset;

					uint32_t y_ = y;
                    for (uint32_t j = 0; j < y_incr; j++) {
						uint64_t ind = 0;
						for (uint32_t k = 0; k < x_incr; k++) {
#ifdef GRK_DEBUG_VALGRIND
							grk_pt pt((uint32_t)(x+k), y_);
							if (validate.contains(pt)) {
								size_t val = grk_memcheck<int32_t>(src_ptr+ind,1);
								if (val != grk_mem_ok)
								   GRK_ERROR("Sparse array write block(%d,%d): uninitialized at location (%d,%d)",
										   block_x, block_y, x+k,y_);
							}
#endif
							dest_ptr[k] = src_ptr[ind];
							ind += col_stride;
						}
						src_ptr  += line_stride;
						y_ ++;
						dest_ptr += block_width;
					}
	            }
	        }
	    }

	    return true;
	}
private:
    const uint32_t block_width;
    const uint32_t block_height;
    SparseBlock** data_blocks;
    grk_rect_u32 bounds;
    grk_rect_u32 grid_bounds;
#ifdef GRK_DEBUG_VALGRIND
    grk_rect_u32 validate;
#endif
};

}

/*@}*/

