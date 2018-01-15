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

#pragma once

#include "BlockingQueue.h"
#include <atomic>
#include <thread>

namespace grk {

class T1Encoder
{
public:
	T1Encoder(tcp_t *tcp,
				tcd_tile_t *tile,
				uint16_t encodeMaxCblkW,
				uint16_t encodeMaxCblkH,
				size_t numThreads,
				bool needsRateControl);
	~T1Encoder();
	bool encode(std::vector<encodeBlockInfo*>* blocks);

private:
	void encode(size_t threadId);
	std::atomic_bool return_code;

	tcd_tile_t *tile;
	std::vector<t1_interface*> threadStructs;

	BlockingQueue<encodeBlockInfo*> encodeQueue;
	mutable std::mutex distortion_mutex;
	bool needsRateControl;

};


}
