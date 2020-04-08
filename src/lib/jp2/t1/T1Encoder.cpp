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
 */

#include "grok_includes.h"
#include "T1Factory.h"
#include "T1Encoder.h"

namespace grk {

T1Encoder::T1Encoder(grk_tcp *tcp, grk_tcd_tile *tile, uint32_t encodeMaxCblkW,
		uint32_t encodeMaxCblkH, bool needsRateControl) :
		tile(tile),
		needsRateControl(needsRateControl),
		encodeBlocks(nullptr),
		blockCount(-1)
{
	for (auto i = 0U; i < ThreadPool::get()->num_threads(); ++i) {
		threadStructs.push_back(
				T1Factory::get_t1(true, tcp, encodeMaxCblkW, encodeMaxCblkH));
	}
}
T1Encoder::~T1Encoder() {
	for (auto &t : threadStructs) {
		delete t;
	}
}
bool T1Encoder::encode(size_t threadId, uint64_t maxBlocks) {
	auto impl = threadStructs[threadId];
	uint64_t index = ++blockCount;
	if (index >= maxBlocks)
		return false;
	encodeBlockInfo *block = encodeBlocks[index];
	uint32_t max = 0;
	impl->preEncode(block, tile, max);
	auto dist = impl->encode(block, tile, max, needsRateControl);
	if (needsRateControl) {
		std::unique_lock<std::mutex> lk(distortion_mutex);
		tile->distotile += dist;
	}
	delete block;
	return true;

}
bool T1Encoder::encode(std::vector<encodeBlockInfo*> *blocks) {
	if (!blocks || blocks->size() == 0)
		return true;

	auto maxBlocks = blocks->size();
	encodeBlocks = new encodeBlockInfo*[maxBlocks];
	for (uint64_t i = 0; i < maxBlocks; ++i) {
		encodeBlocks[i] = blocks->operator[](i);
	}
	blocks->clear();
    std::vector< std::future<int> > results;
    for(size_t i = 0; i < ThreadPool::get()->num_threads(); ++i) {
          results.emplace_back(
            ThreadPool::get()->enqueue([this, maxBlocks] {
                auto threadnum =  ThreadPool::get()->thread_number(std::this_thread::get_id());
                while(encode(threadnum, maxBlocks)){

                }
                return 0;
            })
        );
    }
    for(auto && result: results){
        result.get();
    }
	delete[] encodeBlocks;
	return true;
}

}
