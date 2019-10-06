/*
 *    Copyright (C) 2016-2019 Grok Image Compression Inc.
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
#include "t1_factory.h"
#include "T1Decoder.h"

namespace grk {

T1Decoder::T1Decoder(grk_tcp *tcp, uint16_t blockw, uint16_t blockh) :
		codeblock_width((uint16_t) (blockw ? (uint32_t) 1 << blockw : 0)),
		codeblock_height((uint16_t) (blockh ? (uint32_t) 1 << blockh : 0)),
		decodeBlocks(nullptr),
		blockCount(-1){
	for (auto i = 0U; i < Scheduler::g_TS.GetNumTaskThreads(); ++i) {
		threadStructs.push_back(
				t1_factory::get_t1(false, tcp, codeblock_width,
						codeblock_height));
	}
}

T1Decoder::~T1Decoder() {
	for (auto &t : threadStructs) {
		delete t;
	}
}

bool T1Decoder::decode(std::vector<decodeBlockInfo*> *blocks) {
	if (!blocks || !blocks->size())
		return true;;
	auto maxBlocks = blocks->size();
	decodeBlocks = new decodeBlockInfo*[maxBlocks];
	for (uint64_t i = 0; i < maxBlocks; ++i) {
		decodeBlocks[i] = blocks->operator[](i);
	}
	success = true;
	enki::TaskSet task((uint32_t) maxBlocks,
			[this, maxBlocks](enki::TaskSetPartition range, uint32_t threadnum) {
				for (auto i = range.start; i < range.end; ++i) {
					uint64_t index = ++blockCount;
					if (index >= maxBlocks)
						return;
					decodeBlockInfo *block = decodeBlocks[index];
					if (!success){
						delete block;
						return;
					}
					auto impl = threadStructs[threadnum];
					if (!impl->decode(block)) {
						success = false;
						delete block;
						return;
					}
					impl->postDecode(block);
					delete block;
				}
			});
	Scheduler::g_TS.AddTaskSetToPipe(&task);
	Scheduler::g_TS.WaitforTask(&task);
	delete[] decodeBlocks;
	return success;
}

}
