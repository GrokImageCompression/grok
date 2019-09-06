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
#include "T1Encoder.h"

namespace grk {

T1Encoder::T1Encoder(tcp_t *tcp, tcd_tile_t *tile, uint16_t encodeMaxCblkW,
		uint16_t encodeMaxCblkH, bool needsRateControl) :
		tile(tile),
		needsRateControl(needsRateControl),
		encodeBlocks(nullptr),
		blockCount(0)
{
#ifdef DEBUG_LOSSLESS_T1
	numThreads = 1;
#endif
	for (auto i = 0U; i < Scheduler::g_TS.GetNumTaskThreads(); ++i) {
		threadStructs.push_back(
				t1_factory::get_t1(true, tcp, encodeMaxCblkW, encodeMaxCblkH));
	}
}
T1Encoder::~T1Encoder() {
	for (auto &t : threadStructs) {
		delete t;
	}
}
void T1Encoder::encode(size_t threadId, uint64_t maxBlocks) {
	auto impl = threadStructs[threadId];
	uint64_t index=0;
	std::unique_lock<std::mutex> lk(block_mutex);
	index = blockCount++;
	lk.unlock();

	if (index >= maxBlocks)
		return;
	encodeBlockInfo *block = encodeBlocks[index];
	uint32_t max = 0;
	impl->preEncode(block, tile, max);
	auto dist = impl->encode(block, tile, max, needsRateControl);
	if (needsRateControl) {
		std::unique_lock<std::mutex> lk(distortion_mutex);
		tile->distotile += dist;
	}
	delete block;

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
	enki::TaskSet task((uint32_t) maxBlocks,
			[this,maxBlocks](enki::TaskSetPartition range, uint32_t threadnum) {
				for (auto i = range.start; i < range.end; ++i)
					encode(threadnum, maxBlocks);
			});

	Scheduler::g_TS.AddTaskSetToPipe(&task);
	Scheduler::g_TS.WaitforTask(&task);
	delete[] encodeBlocks;

	return true;
}

}
