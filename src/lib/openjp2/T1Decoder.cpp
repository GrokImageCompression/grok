/*
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
#include "T1Decoder.h"
#include "Barrier.h"

Barrier decode_t1_barrier(numDecodeThreads);

T1Decoder::T1Decoder(uint16_t blockw, 
					uint16_t blockh) :codeblock_width(blockw), 
					  				  codeblock_height(blockh)
{

}


void T1Decoder::decode(std::vector<decodeBlockInfo*>* blocks, int32_t numThreads) {
	decodeQueue.push_no_lock(blocks);
	for (auto threadNum = 0; threadNum < numThreads; threadNum++) {
		decodeWorkers.push_back(std::thread([this, threadNum]()
		{
			auto t1 = opj_t1_create(false, (uint16_t)codeblock_width, (uint16_t)codeblock_height);
			if (!t1)
				return;
			decodeBlockInfo* block = NULL;
			while (decodeQueue.tryPop(block)) {
				if (!opj_t1_decode_cblk(t1,
										block->cblk,
										block->bandno,
										(uint32_t)block->roishift,
										block->cblksty)) {
						delete block;
						break;
				}


				auto t1_data = t1->data;
				if (block->roishift) {
					int32_t threshold = 1 << block->roishift;
					for (auto j = 0U; j < t1->h; ++j) {
						for (auto i = 0U; i < t1->w; ++i) {
							auto value = *t1_data;
							auto magnitude = abs(value);
							if (magnitude >= threshold) {
								magnitude >>= block->roishift;
								*t1_data = value < 0 ? -magnitude : magnitude;
							}
							t1_data++;
						}
					}
					//reset t1_data to start of buffer
					t1_data = t1->data;
				}

				uint32_t tile_width = (uint32_t)(block->tilec->x1 - block->tilec->x0);
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
				delete block;
			}
			opj_t1_destroy(t1);
			decode_t1_barrier.arrive_and_wait();
		}));
	}

	for (auto& t : decodeWorkers) {
		t.join();
	}

}
