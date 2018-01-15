/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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
#include "t1_factory.h"
#include "T1Encoder.h"


namespace grk {

T1Encoder::T1Encoder(tcp_t *tcp, tcd_tile_t *tile,
					uint16_t encodeMaxCblkW,
					uint16_t encodeMaxCblkH,
					size_t numThreads, 
					bool needsRateControl) : tile(tile), 
											needsRateControl(needsRateControl) {
	for (auto i = 0U; i < numThreads; ++i) {
		threadStructs.push_back(t1_factory::get_t1(true,tcp, encodeMaxCblkW, encodeMaxCblkH));
	}
}

T1Encoder::~T1Encoder() {
	for (auto& t : threadStructs) {
		delete t;
   }
}

void T1Encoder::encode(size_t threadId) {
	encodeBlockInfo* block = nullptr;
	auto impl = threadStructs[threadId];
	while (return_code && encodeQueue.tryPop(block)) {
		uint32_t max = 0;
		impl->preEncode(block, tile, max);
		auto dist = impl->encode(block, tile,max, needsRateControl);
		{
			std::unique_lock<std::mutex> lk(distortion_mutex);
			tile->distotile += dist;
		}
		delete block;
	}
}
bool T1Encoder::encode(	std::vector<encodeBlockInfo*>* blocks) {
	if (!blocks || blocks->size() == 0)
		return true;
	auto numThreads = threadStructs.size();
#ifdef DEBUG_LOSSLESS_T1
	numThreads = 1;
#endif
	encodeQueue.push_no_lock(blocks);
	return_code = true;

	Barrier encode_t1_barrier(numThreads);
	Barrier encode_t1_calling_barrier(numThreads + 1);

	auto pool = new ThreadPool(numThreads);
	for (auto threadId = 0U; threadId < numThreads; threadId++) {
		pool->enqueue([this,
						&encode_t1_barrier,
						&encode_t1_calling_barrier,
						threadId] {

			encode(threadId);
			encode_t1_barrier.arrive_and_wait();
			encode_t1_calling_barrier.arrive_and_wait();
		});
	}

	encode_t1_calling_barrier.arrive_and_wait();
	delete pool;
	
	// clean up remaining blocks
	encodeBlockInfo* block = nullptr;
	while (encodeQueue.tryPop(block)) {
		delete block;
	}
	return return_code;
}


}
