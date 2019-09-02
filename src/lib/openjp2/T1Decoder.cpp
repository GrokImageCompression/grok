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
#include "ThreadPool.h"

#include "t1_factory.h"
#include "T1Decoder.h"


namespace grk {
	

T1Decoder::T1Decoder(tcp_t *tcp, 
					uint16_t blockw,
					uint16_t blockh, 
					size_t numThreads) :codeblock_width(  (uint16_t)(blockw ? (uint32_t)1<<blockw : 0)),
					  				  codeblock_height( (uint16_t)(blockh ? (uint32_t)1<<blockh : 0)) {
	Scheduler::g_TS.Initialize((uint32_t)numThreads);
	for (auto i = 0U; i < numThreads; ++i) {
		threadStructs.push_back(t1_factory::get_t1(false,tcp, codeblock_width, codeblock_height));
	}

}


T1Decoder::~T1Decoder() {
	for (auto& t : threadStructs) {
		delete t;
	}
}



bool T1Decoder::decode(std::vector<decodeBlockInfo*>* blocks) {
	if (!blocks)
		return false;
	decodeQueue.push_no_lock(blocks);
	blocks->clear();
#ifdef DEBUG_LOSSLESS_T1
	numThreads = 1;
#endif

	success = true;
    enki::TaskSet task( (uint32_t)decodeQueue.size(), [this]( enki::TaskSetPartition range, uint32_t threadnum  ) {
	   for (auto i=range.start; i < range.end; ++i) {
		   if (!success)
			   break;
		   decodeBlockInfo* block = nullptr;
			auto impl = threadStructs[threadnum];
			if (decodeQueue.tryPop(block)) {
				if (!impl->decode(block)) {
						success = false;
						delete block;
						break;
				}
				impl->postDecode(block);
				delete block;
			}
	   }
	  }  );

  Scheduler::g_TS.AddTaskSetToPipe( &task );
  Scheduler::g_TS.WaitforTask(&task);

	decodeBlockInfo* block = nullptr;
	while (decodeQueue.tryPop(block)) {
		delete block;
	}
	return success;
}

}
