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
#include "grk_includes.h"
#include "T1Factory.h"
#include "T1Decoder.h"
#include <atomic>

namespace grk {

T1Decoder::T1Decoder(TileCodingParams *tcp,
					uint16_t blockw,
					uint16_t blockh) :
		codeblock_width((uint16_t) (blockw ? (uint32_t) 1 << blockw : 0)),
		codeblock_height((uint16_t) (blockh ? (uint32_t) 1 << blockh : 0)),
		success(true),
		decodeBlocks(nullptr){
	for (auto i = 0U; i < ThreadPool::get()->num_threads(); ++i) {
		threadStructs.push_back(
				T1Factory::get_t1(false, tcp, codeblock_width,
						codeblock_height));
	}
}

T1Decoder::~T1Decoder() {
	for (auto &t : threadStructs) {
		delete t;
	}
}
bool T1Decoder::decompressBlock(T1Interface *impl, decodeBlockInfo *block){
	try {
		if (!impl->decompress(block)) {
			delete block;
			return false;
		}
	} catch (std::runtime_error &rerr){
		delete block;
		GRK_ERROR(rerr.what());
		return false;
	}
	bool rc =  impl->postDecode(block);
	delete block;
	return rc;
}

bool T1Decoder::decompress(std::vector<decodeBlockInfo*> *blocks) {
	if (!blocks || !blocks->size())
		return true;
	size_t num_threads = ThreadPool::get()->num_threads();
	success = true;
	if (num_threads == 1){
		for (size_t i = 0; i < blocks->size(); ++i){
			auto block = blocks->operator[](i);
			auto impl = threadStructs[(size_t)0];
			if (!decompressBlock(impl,block))
				success = false;
		}
		return success;
	}
	auto maxBlocks = blocks->size();
	decodeBlocks = new decodeBlockInfo*[maxBlocks];
	for (uint64_t i = 0; i < maxBlocks; ++i)
		decodeBlocks[i] = blocks->operator[](i);
	std::atomic<int> blockCount(-1);
    std::vector< std::future<int> > results;
    for(size_t i = 0; i < num_threads; ++i) {
        results.emplace_back(
            ThreadPool::get()->enqueue([this, maxBlocks, &blockCount] {
                auto threadnum =  ThreadPool::get()->thread_number(std::this_thread::get_id());
                assert(threadnum >= 0);
                while (true) {
                	uint64_t index = (uint64_t)++blockCount;
                	//note: even after failure, we continue to read and delete
                	//blocks unil index is out of bounds. Otherwise, we leak blocks.
                	if (index >= maxBlocks)
                		return 0;
					auto block = decodeBlocks[index];
					if (!success){
						delete block;
						continue;
					}
					auto impl = threadStructs[(size_t)threadnum];
					if (!decompressBlock(impl,block))
						success = false;
                }
                return 0;
            })
        );
    }
    for(auto &result: results){
        result.get();
    }
	delete[] decodeBlocks;

	return success;
}

}
