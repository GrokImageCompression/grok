/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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

T1CompressScheduler::T1CompressScheduler(grk_tile *tile,
										bool needsRateControl) :  tile(tile),
																	needsRateControl(needsRateControl),
																	encodeBlocks(nullptr),
																	blockCount(-1) {
}
T1CompressScheduler::~T1CompressScheduler() {
	for (auto &t : t1Implementations)
		delete t;
}

void T1CompressScheduler::scheduleCompress(TileCodingParams *tcp,
							const double *mct_norms,
							uint16_t mct_numcomps) {

	uint16_t compno;
	uint8_t resno, bandIndex;
	tile->distotile = 0;
	std::vector<CompressBlockExec*> blocks;
	uint32_t maxCblkW = 0;
	uint32_t maxCblkH = 0;

	for (compno = 0; compno < tile->numcomps; ++compno) {
		auto tilec = tile->comps + compno;
		auto tccp = tcp->tccps + compno;
		for (resno = 0; resno < tilec->numresolutions; ++resno) {
			auto res = &tilec->resolutions[resno];
			for (bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
				auto band = &res->band[bandIndex];
				for (auto prc : band->precincts){
					auto nominalBlockSize = prc->getNominalBlockSize();
					for (uint64_t cblkno = 0; cblkno < prc->getNumCblks();	++cblkno) {
						auto cblk = prc->getCompressedBlockPtr(cblkno);
						if (!cblk->alloc_data(nominalBlockSize))
							continue;
						auto block = new CompressBlockExec();
						block->tile = tile;
						block->doRateControl = needsRateControl;
						block->x = cblk->x0;
						block->y = cblk->y0;
						tilec->getBuffer()->transformToCanvasCoordinates(resno,band->orientation,block->x,block->y);
						block->tiledp = tilec->getBuffer()->getWindow()->data + (uint64_t) block->x +
											block->y * (uint64_t) tilec->getBuffer()->getWindow()->stride;
						maxCblkW = std::max<uint32_t>(maxCblkW,
								(uint32_t) (1 << tccp->cblkw));
						maxCblkH = std::max<uint32_t>(maxCblkH,
								(uint32_t) (1 << tccp->cblkh));
						block->compno = compno;
						block->band_orientation = band->orientation;
						block->cblk = cblk;
						block->cblk_sty = tccp->cblk_sty;
						block->qmfbid = tccp->qmfbid;
						block->resno = resno;
						block->inv_step_ht = 1.0f/band->stepsize;
						block->stepsize = band->stepsize;
						block->mct_norms = mct_norms;
						block->mct_numcomps = mct_numcomps;
						block->k_msbs = (uint8_t)(band->numbps - cblk->numbps);
						blocks.push_back(block);

					}
				}
			}
		}
	}
	for (auto i = 0U; i < ThreadPool::get()->num_threads(); ++i)
		t1Implementations.push_back(T1Factory::get_t1(true, tcp, maxCblkW, maxCblkH));
	compress(&blocks);
}

void T1CompressScheduler::compress(std::vector<CompressBlockExec*> *blocks) {
	if (!blocks || blocks->size() == 0)
		return;

	size_t num_threads = ThreadPool::get()->num_threads();
	if (num_threads == 1){
		auto impl = t1Implementations[0];
		for (auto iter = blocks->begin(); iter != blocks->end(); ++iter){
			compress(impl, *iter);
			delete *iter;
		}
		return;
	}
	auto maxBlocks = blocks->size();
	encodeBlocks = new CompressBlockExec*[maxBlocks];
	for (uint64_t i = 0; i < maxBlocks; ++i)
		encodeBlocks[i] = blocks->operator[](i);
	blocks->clear();
    std::vector< std::future<int> > results;
    for(size_t i = 0; i < num_threads; ++i) {
          results.emplace_back(
            ThreadPool::get()->enqueue([this, maxBlocks] {
                auto threadnum =  ThreadPool::get()->thread_number(std::this_thread::get_id());
                while(compress((size_t)threadnum, maxBlocks)){

                }
                return 0;
            })
        );
    }
    for(auto &result: results){
        result.get();
    }
	delete[] encodeBlocks;
}
bool T1CompressScheduler::compress(size_t threadId, uint64_t maxBlocks) {
	auto impl = t1Implementations[threadId];
	uint64_t index = (uint64_t)++blockCount;
	if (index >= maxBlocks)
		return false;
	auto block = encodeBlocks[index];
	compress(impl,block);
	delete block;

	return true;
}
void T1CompressScheduler::compress(T1Interface *impl, CompressBlockExec *block){
	block->open(impl);
	if (needsRateControl) {
		std::unique_lock<std::mutex> lk(distortion_mutex);
		tile->distotile += block->distortion;
	}
}

}
