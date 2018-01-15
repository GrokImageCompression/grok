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

#include "t1_factory.h"
#include "T1Decoder.h"


namespace grk {
	

T1Decoder::T1Decoder(tcp_t *tcp, 
					uint16_t blockw,
					uint16_t blockh, 
					size_t numThreads) :codeblock_width(  (uint16_t)(blockw ? (uint32_t)1<<blockw : 0)),
					  				  codeblock_height( (uint16_t)(blockh ? (uint32_t)1<<blockh : 0)) {
	for (auto i = 0U; i < numThreads; ++i) {
		threadStructs.push_back(t1_factory::get_t1(false,tcp, codeblock_width, codeblock_height));
	}

}


T1Decoder::~T1Decoder() {
	for (auto& t : threadStructs) {
		delete t;
	}
}



bool T1Decoder::decode(std::vector<decodeBlockInfo*>* blocks, size_t numThreads) {
	if (!blocks)
		return false;
	decodeQueue.push_no_lock(blocks);
	blocks->clear();
#ifdef DEBUG_LOSSLESS_T1
	numThreads = 1;
#endif
	Barrier decode_t1_barrier(numThreads);
	Barrier decode_t1_calling_barrier(numThreads + 1);

	auto pool = new ThreadPool(numThreads);
	std::atomic_bool success;
	success = true;
	for (auto threadId = 0U; threadId < numThreads; threadId++) {
		pool->enqueue([this,
						&decode_t1_barrier,
						&decode_t1_calling_barrier,
						threadId,
						&success]()		{

			decodeBlockInfo* block = nullptr;
			auto impl = threadStructs[threadId];
			while (decodeQueue.tryPop(block)) {
				if (!impl->decode(block)) {
						success = false;
						delete block;
						break;
				}
				impl->postDecode(block);
				delete block;
			}
			decode_t1_barrier.arrive_and_wait();
			decode_t1_calling_barrier.arrive_and_wait();
		});
	}

	decode_t1_calling_barrier.arrive_and_wait();

	// cleanup
	delete pool;
	decodeBlockInfo* block = nullptr;
	while (decodeQueue.tryPop(block)) {
		delete block;
	}
	return success;
}

}
