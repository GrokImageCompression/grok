/*
*    Copyright (C) 2016-2017 Grok Image Compression Inc.
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

namespace grk {

struct t1_opt_t;

class T1Encoder
{
public:
	T1Encoder(bool opt, uint32_t encodeMaxCblkW, uint32_t encodeMaxCblkH, size_t numThreads);
	~T1Encoder();
	bool encode(tcd_tile_t *tile, std::vector<encodeBlockInfo*>* blocks);

private:
	void encode(size_t threadId);
	std::atomic_bool return_code;

	tcd_tile_t *tile;
	bool do_opt;
	std::vector<t1_interface*> threadStructs;

	BlockingQueue<encodeBlockInfo*> encodeQueue;
	mutable std::mutex distortion_mutex;

};


}