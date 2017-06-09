/*
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
#include "grok_includes.h"
#include "Barrier.h"
#include "ThreadPool.h"

 // tier 1 interface
#include "t1_interface.h"
#include "t1.h"
#include "T1Decoder.h"


namespace grk {
	

T1Decoder::T1Decoder(uint16_t blockw, 
					uint16_t blockh) :codeblock_width(  (uint16_t)(blockw ? (uint32_t)1<<blockw : 0)),
					  				  codeblock_height( (uint16_t)(blockh ? (uint32_t)1<<blockh : 0))
{}


bool T1Decoder::decode(std::vector<decodeBlockInfo*>* blocks, size_t numThreads) {
	if (!blocks)
		return false;
	decodeQueue.push_no_lock(blocks);
	blocks->clear();
#ifdef DEBUG_LOSSLESS_T1
	numThreads = 1;
#endif

	for (auto i = 0U; i < numThreads; ++i) {
		auto t1 = new t1_t(false, (uint16_t)codeblock_width, (uint16_t)codeblock_height);
		if (!t1_allocate_buffers(
			t1,
			codeblock_width,
			codeblock_height)) {
			for (auto t : threadStorage) {
				delete((t1_t*)t);
			}
			return false;
		}
		threadStorage.push_back(t1);
	}

	Barrier decode_t1_barrier(numThreads);
	Barrier decode_t1_calling_barrier(numThreads + 1);

	auto pool = new ThreadPool(numThreads);
	volatile bool success = true;
	for (auto threadId = 0; threadId < numThreads; threadId++) {
		pool->enqueue([this,
						&decode_t1_barrier,
						&decode_t1_calling_barrier,
						threadId,
						&success]()		{

			decodeBlockInfo* block = NULL;
			//1. get t1 struct for this thread
			auto t1 = (t1_t*)threadStorage[threadId];
			while (decodeQueue.tryPop(block)) {

				//2. decode
				if (!t1_decode_cblk(t1,
										block->cblk,
										block->bandno,
										(uint32_t)block->roishift,
										block->cblksty)) {
						delete block;
						break;
				}

				//3. post-decode
				auto t1_data = t1->data;
				// ROI shift
				if (block->roishift) {
					int32_t threshold = 1 << block->roishift;
					for (auto j = 0U; j < t1->h; ++j) {
						for (auto i = 0U; i < t1->w; ++i) {
							auto value = *t1_data;
							auto magnitude = abs(value);
							if (magnitude >= threshold) {
								magnitude >>= block->roishift;
								// ((value > 0) - (value < 0)) == signum(value)
								*t1_data = ((value > 0) - (value < 0))* magnitude;
							}
							t1_data++;
						}
					}
					//reset t1_data to start of buffer
					t1_data = t1->data;
				}

				//dequantization
				uint32_t tile_width = block->tilec->x1 - block->tilec->x0;
				if (block->qmfbid == 1) {
					int32_t* restrict tile_data = block->tiledp;
					for (auto j = 0U; j < t1->h; ++j) {
						int32_t* restrict tile_row_data = tile_data;
						for (auto i = 0U; i < t1->w; ++i) {
							tile_row_data[i] = *t1_data / 2;
							t1_data++;
						}
						tile_data += tile_width;
					}
				}
				else {		
					float* restrict tile_data = (float*)block->tiledp;
					for (auto j = 0U; j < t1->h; ++j) {
						float* restrict tile_row_data = tile_data;
						for (auto i = 0U; i < t1->w; ++i) {
							tile_row_data[i] = (float)*t1_data * block->stepsize;
							t1_data++;
						}
						tile_data += tile_width;
					}
				}

				//3. cleanup
				delete block;
			}
			decode_t1_barrier.arrive_and_wait();
			decode_t1_calling_barrier.arrive_and_wait();
		});
	}

	decode_t1_calling_barrier.arrive_and_wait();

	// cleanup
	delete pool;
	decodeBlockInfo* block = NULL;
	while (decodeQueue.tryPop(block)) {
		delete block;
	}

	// clean up t1 structs
	for (auto t : threadStorage) {
		delete((t1_t*)t);
	}
	return success;

}

}
