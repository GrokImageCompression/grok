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
#include "T1Encoder.h"
#include "Barrier.h"


T1Encoder::T1Encoder() : tile(NULL), 
						maxCblkW(0),
						maxCblkH(0)
{

}

void T1Encoder::encode(void) {
	auto state = opj_plugin_get_debug_state();
	auto t1 = opj_t1_create(true, 0,0);
	if (!t1) {
		return_code = false;
	}
	encodeBlockInfo* block = NULL;
	while (return_code && encodeQueue.tryPop(block)) {
		uint32_t tileIndex = 0, tileLineAdvance;
		if (!opj_t1_allocate_buffers(
			t1,
			block->cblk->x1 - block->cblk->x0,
			block->cblk->y1 - block->cblk->y0)) {
			delete block;
			return_code = false;
			break;
		}
		auto tilec = tile->comps + block->compno;
		uint32_t tile_width = tilec->x1 - tilec->x0;
		tileLineAdvance = tile_width - t1->w;
		block->tiledp =
			opj_tile_buf_get_ptr(tilec->buf, block->resno, block->bandno, (uint32_t)block->x, (uint32_t)block->y);
		t1->data = block->tiledp;
		t1->data_stride = tile_width;
		if (block->qmfbid == 1) {
			for (auto j = 0U; j < t1->h; ++j) {
				for (auto i = 0U; i < t1->w; ++i) {
					// pass through otherwise
					if ( !(state & OPJ_PLUGIN_STATE_DEBUG_ENCODE) || (state & OPJ_PLUGIN_STATE_PRE_TR1)) {
						block->tiledp[tileIndex] <<= T1_NMSEDEC_FRACBITS;
					}
					tileIndex++;
				}
				tileIndex += tileLineAdvance;
			}
		}
		else {
			for (auto j = 0U; j < t1->h; ++j) {
				for (auto i = 0U; i < t1->w; ++i) {
					// In lossy mode, we do a direct pass through of the image data in two cases while in debug encode mode:
					// 1. plugin is being used for full T1 encoding, so no need to quantize in OPJ
					// 2. plugin is only being used for pre T1 encoding, and we are applying quantization
					//    in the plugin DWT step
					if (!(state & OPJ_PLUGIN_STATE_DEBUG_ENCODE) ||
						((state & OPJ_PLUGIN_STATE_PRE_TR1) && !(state & OPJ_PLUGIN_STATE_DWT_QUANTIZATION))) {
						block->tiledp[tileIndex] = opj_int_fix_mul_t1(block->tiledp[tileIndex], block->bandconst);
					}
					tileIndex++;
				}
				tileIndex += tileLineAdvance;
			}
		}

		auto dist =  opj_t1_encode_cblk(t1,
										block->cblk,
										block->bandno,
										block->compno,
										tilec->numresolutions - 1 - block->resno,
										block->qmfbid,
										block->stepsize,
										block->cblksty,
										tile->numcomps,
										block->mct_norms,
										block->mct_numcomps);

		delete block;

		std::unique_lock<std::mutex> lk(distortion_mutex);
		tile->distotile += dist;
	}
	opj_t1_destroy(t1);
}
void T1Encoder::encodeOpt(size_t threadId) {
	auto state = opj_plugin_get_debug_state();
	auto t1 = t1OptVec[threadId];
	encodeBlockInfo* block = NULL;
	while (return_code && encodeQueue.tryPop(block)) {

		auto tilec = tile->comps + block->compno;
		opj_t1_opt_init_buffers(t1,
								(block->cblk->x1 - block->cblk->x0),
								(block->cblk->y1 - block->cblk->y0));

		uint32_t tile_width = (tilec->x1 - tilec->x0);
		auto tileLineAdvance = tile_width - t1->w;

		auto tiledp = block->tiledp;
			
		uint32_t tileIndex = 0;
		uint32_t max = 0;
		uint32_t cblk_index = 0;
		if (block->qmfbid == 1) {
			for (auto j = 0U; j < t1->h; ++j) {
				for (auto i = 0U; i < t1->w; ++i) {
					int32_t tmp=0;
					// pass through otherwise
					if (!(state & OPJ_PLUGIN_STATE_DEBUG_ENCODE) || (state & OPJ_PLUGIN_STATE_PRE_TR1)) {
						tmp = block->tiledp[tileIndex] << T1_NMSEDEC_FRACBITS;
					}
					else
					{
						tmp = block->tiledp[tileIndex];
					}
					uint32_t mag = (uint32_t)opj_int_abs(tmp);
					max = opj_uint_max(max, mag);
					t1->data[cblk_index] = mag | ((uint32_t)(tmp < 0) << T1_DATA_SIGN_BIT_INDEX);
					tileIndex++;
					cblk_index++;
				}
				tileIndex += tileLineAdvance;
			}
		}
		else {
			for (auto j = 0U; j < t1->h; ++j) {
				for (auto i = 0U; i < t1->w; ++i) {
					// In lossy mode, we do a direct pass through of the image data in two cases while in debug encode mode:
					// 1. plugin is being used for full T1 encoding, so no need to quantize in OPJ
					// 2. plugin is only being used for pre T1 encoding, and we are applying quantization
					//    in the plugin DWT step
					int32_t tmp = 0;
					if (!(state & OPJ_PLUGIN_STATE_DEBUG_ENCODE) ||
						((state & OPJ_PLUGIN_STATE_PRE_TR1) && !(state & OPJ_PLUGIN_STATE_DWT_QUANTIZATION))) {
						tmp = opj_int_fix_mul_t1(tiledp[tileIndex], block->bandconst);
					}
					else{
						tmp = tiledp[tileIndex];
					}

					uint32_t mag = (uint32_t)opj_int_abs(tmp);
					uint32_t sign_mag = mag | ((uint32_t)(tmp < 0) << T1_DATA_SIGN_BIT_INDEX);
					max = opj_uint_max(max, mag);
					t1->data[cblk_index] = sign_mag;
					
					tileIndex++;
					cblk_index++;
				}
				tileIndex += tileLineAdvance;
			}
		}

		auto dist = opj_t1_opt_encode_cblk(	t1,
											block->cblk,
											block->bandno,
											block->compno,
											tilec->numresolutions - 1 - block->resno,
											block->qmfbid,
											block->stepsize,
											tile->numcomps,
											block->mct_norms,
											block->mct_numcomps,
											max);

		delete block;
		std::unique_lock<std::mutex> lk(distortion_mutex);
		tile->distotile += dist;
	}
}

bool T1Encoder::encode(bool do_opt, 
						opj_tcd_tile_t *encodeTile,
						std::vector<encodeBlockInfo*>* blocks, 
						uint32_t encodeMaxCblkW,
						uint32_t encodeMaxCblkH,
						uint32_t numThreads) {
	if (!blocks || blocks->size() == 0)
		return true;
	tile = encodeTile;
	maxCblkW = encodeMaxCblkW;
	maxCblkH = encodeMaxCblkH;

	for (auto i = 0U; i < numThreads; ++i) {
		if (do_opt) {
			auto t1 = opj_t1_opt_create(true);
			if (!t1) {
				for (auto t : t1OptVec) {
					opj_t1_opt_destroy(t);
				}
				return false;
			}
			if (!opj_t1_opt_allocate_buffers(t1,
											maxCblkW,
											maxCblkH)) {
				for (auto t : t1OptVec) {
					opj_t1_opt_destroy(t);
				}
				return false;
			}
			t1OptVec.push_back(t1);
		}
	}
	encodeQueue.push_no_lock(blocks);
	return_code = true;
	Barrier encode_t1_barrier(numThreads);
	Barrier encode_t1_calling_barrier(numThreads + 1);

	for (auto threadId = 0U; threadId < numThreads; threadId++) {
		encodeWorkers.push_back(std::thread([this,
											do_opt,
											&encode_t1_barrier,
											&encode_t1_calling_barrier,
											threadId]()
		{
			if (do_opt)
				encodeOpt(threadId);
			else
				 encode();
			encode_t1_barrier.arrive_and_wait();
			encode_t1_calling_barrier.arrive_and_wait();
		}));
	}
	encode_t1_calling_barrier.arrive_and_wait();

	// join threads
	for (auto& t : encodeWorkers) {
		t.join();
	}

	// clean up blocks
	encodeBlockInfo* block = NULL;
	while (encodeQueue.tryPop(block)) {
		delete block;
	}

	// clean up t1 structs
	for (auto t : t1OptVec) {
		opj_t1_opt_destroy(t);
	}

	for (auto t : t1Vec) {
		opj_t1_destroy(t);
	}
	return return_code;

}
