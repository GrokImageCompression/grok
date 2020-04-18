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


T1Decoder::T1Decoder(uint16_t blockw, 
					uint16_t blockh) :cblk_w(blockw), 
					  				  cblk_h(blockh)
{

}

void T1Decoder::decode(std::vector<decodeBlock*>* blocks, int32_t numThreads) {
	decodeQueue.push_no_lock(blocks);
	for (auto threadNum = 0; threadNum < numThreads; threadNum++) {
		decodeWorkers.push_back(std::thread([this, threadNum]()
		{
			auto t1 = opj_t1_create(false, (uint16_t)cblk_w, (uint16_t)cblk_h);
			if (!t1)
				return;
			while (!decodeQueue.empty()) {
				decodeBlock* block = NULL;
				decodeQueue.tryPop(block);
				if (!block || !block->cblk)
					break;

				uint32_t tile_w = (uint32_t)(block->tilec->x1 - block->tilec->x0);

				if (!opj_t1_decode_cblk(t1,
										block->cblk,
										block->bandno,
										(uint32_t)block->roishift,
										block->cblksty)) {
						delete block;
						break;
				}


				auto datap = t1->data;
				auto cblk_w = t1->w;
				auto cblk_h = t1->h;
				if (block->roishift) {
					int32_t thresh = 1 << block->roishift;
					for (auto j = 0; j < cblk_h; ++j) {
						for (auto i = 0; i < cblk_w; ++i) {
							int32_t val = datap[(j * cblk_w) + i];
							int32_t mag = abs(val);
							if (mag >= thresh) {
								mag >>= block->roishift;
								datap[(j * cblk_w) + i] = val < 0 ? -mag : mag;
							}
						}
					}
				}
				if (block->qmfbid == 1) {
					int32_t* restrict tiledp = block->tiledp;
					for (auto j = 0; j < cblk_h; ++j) {
						for (auto i = 0; i < cblk_w; ++i) {
							int32_t tmp = datap[(j * cblk_w) + i];
							((int32_t*)tiledp)[(j * tile_w) + i] = tmp / 2;
						}
					}
				}
				else {		/* if (tccp->qmfbid == 0) */
					float* restrict tiledp = (float*)block->tiledp;
					for (auto j = 0; j < cblk_h; ++j) {
						float* restrict tiledp2 = tiledp;
						for (auto i = 0; i < cblk_w; ++i) {
							float tmp = (float)*datap * block->stepsize;
							*tiledp2 = tmp;
							datap++;
							tiledp2++;
						}
						tiledp += tile_w;
					}
				}
				delete block;
			}
			opj_t1_destroy(t1);
			_condition.notify_one();
		}));
	}

	std::unique_lock<std::mutex> lk(_mutex);
	_condition.wait(lk, [this] { return decodeQueue.empty(); });

	for (auto& t : decodeWorkers) {
		t.join();
	}

}
