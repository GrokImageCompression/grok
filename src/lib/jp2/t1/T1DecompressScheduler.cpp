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

namespace grk {

T1DecompressScheduler::T1DecompressScheduler() :success(true),
												decodeBlocks(nullptr){

}

T1DecompressScheduler::~T1DecompressScheduler() {
	for (auto &t : t1Implementations) {
		delete t;
	}
}

bool T1DecompressScheduler::prepareScheduleDecompress(TileComponent *tilec, TileComponentCodingParams *tccp,
		std::vector<DecompressBlockExec*> *blocks) {
	if (!tilec->getBuffer()->alloc()) {
		GRK_ERROR( "Not enough memory for tile data");
		return false;
	}
	for (uint32_t resno = 0; resno < tilec->resolutions_to_decompress; ++resno) {
		auto res = &tilec->resolutions[resno];
		for (uint8_t bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
			Subband *GRK_RESTRICT band = res->bandWindow + bandIndex;
			for (auto precinct : band->precincts) {
				if (!tilec->subbandIntersectsAOI(resno,
												bandIndex,
												precinct)){

					continue;
				}
				for (uint64_t cblkno = 0; cblkno < precinct->getNumCblks();	++cblkno) {
					auto cblk = precinct->getDecompressedBlockPtr() + cblkno;
					if (tilec->subbandIntersectsAOI(resno,
													bandIndex,
													cblk)){

						auto block = new DecompressBlockExec();
						block->x = cblk->x0;
						block->y = cblk->y0;
						block->tilec = tilec;
						block->bandIndex = bandIndex;
						block->band_orientation = band->orientation;
						block->cblk = cblk;
						block->cblk_sty = tccp->cblk_sty;
						block->qmfbid = tccp->qmfbid;
						block->resno = resno;
						block->roishift = tccp->roishift;
						block->stepsize = band->stepsize;

						block->k_msbs = (uint8_t)(band->numbps - cblk->numbps);
						blocks->push_back(block);
					}

				}
			}
		}
	}
	return true;
}


bool T1DecompressScheduler::scheduleDecompress(TileCodingParams *tcp,
		                    uint16_t blockw, uint16_t blockh,
		                    std::vector<DecompressBlockExec*> *blocks) {
	// nominal code block dimensions
	uint16_t codeblock_width = (uint16_t) (blockw ? (uint32_t) 1 << blockw : 0);
	uint16_t codeblock_height = (uint16_t) (blockh ? (uint32_t) 1 << blockh : 0);
	for (auto i = 0U; i < ThreadPool::get()->num_threads(); ++i) {
		t1Implementations.push_back(T1Factory::get_t1(false, tcp, codeblock_width,codeblock_height));
	}
	return decompress(blocks);
}
bool T1DecompressScheduler::decompressBlock(T1Interface *impl, DecompressBlockExec *block){
	try {
		bool rc = block->open(impl);
		delete block;
		return rc;
	} catch (std::runtime_error &rerr){
		delete block;
		GRK_ERROR(rerr.what());
		return false;
	}

	return true;
}

bool T1DecompressScheduler::decompress(std::vector<DecompressBlockExec*> *blocks) {
	if (!blocks || !blocks->size())
		return true;
	size_t num_threads = ThreadPool::get()->num_threads();
	success = true;
	if (num_threads == 1){
		for (size_t i = 0; i < blocks->size(); ++i){
			auto block = blocks->operator[](i);
			if (!success){
				delete block;
			} else {
				auto impl = t1Implementations[(size_t)0];
				if (!decompressBlock(impl,block))
					success = false;
			}
		}
		return success;
	}
	auto maxBlocks = blocks->size();
	decodeBlocks = new DecompressBlockExec*[maxBlocks];
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
					auto impl = t1Implementations[(size_t)threadnum];
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
