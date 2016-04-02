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

#pragma once

#include "BlockingQueue.h"
#include <atomic>
#include <thread>


class T1Encoder
{
public:
	T1Encoder();
	bool encode(bool do_opt, opj_tcd_tile_t *tile,
				std::vector<encodeBlockInfo*>* blocks,
				int32_t maxCblkW, 
				int32_t maxCblkH,
				uint32_t numThreads);

	void encode(int32_t threadId );
	void encodeOpt(int32_t threadId);

	std::atomic_bool return_code;

private:
	opj_tcd_tile_t *tile;
	int32_t maxCblkW;
	int32_t maxCblkH;

	std::vector<opj_t1_opt*> t1OptVec;
	std::vector<opj_t1*> t1Vec;

	std::vector<std::thread> encodeWorkers;
	BlockingQueue<encodeBlockInfo*> encodeQueue;
	mutable std::mutex distortion_mutex;

};
