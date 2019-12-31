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

#pragma once

#include <thread>

namespace grk {

class T1Encoder {
public:
	T1Encoder(grk_tcp *tcp, grk_tcd_tile *tile, uint16_t encodeMaxCblkW,
			uint16_t encodeMaxCblkH, bool needsRateControl);
	~T1Encoder();
	bool encode(std::vector<encodeBlockInfo*> *blocks);

private:
	void encode(size_t threadId, uint64_t maxBlocks);

	grk_tcd_tile *tile;
	std::vector<t1_interface*> threadStructs;
	mutable std::mutex distortion_mutex;
	bool needsRateControl;
	mutable std::mutex block_mutex;
	encodeBlockInfo** encodeBlocks;
	std::atomic<int64_t> blockCount;

};

}
