/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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

namespace grk
{
const uint8_t gain_b[4] = {0, 1, 1, 2};

DecompressScheduler::DecompressScheduler() : success(true), decodeBlocks(nullptr)
{}

void DecompressScheduler::prepareScheduleDecompress(TileComponent* tilec,
													  TileComponentCodingParams* tccp,
													  DecompressBlocks &blocks,
													  uint8_t prec)
{
	bool wholeTileDecoding = tilec->isWholeTileDecoding();
	for(uint8_t resno = 0; resno <= tilec->highestResolutionDecompressed; ++resno)
	{
		ResDecompressBlocks resBlocks;
		auto res = tilec->tileCompResolution + resno;
		for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
		{
			auto band = res->tileBand + bandIndex;
			auto paddedBandWindow =
				tilec->getBuffer()->getBandWindowPadded(resno, band->orientation);
			for(auto precinct : band->precincts)
			{
				if(!wholeTileDecoding && !paddedBandWindow->nonEmptyIntersection(precinct))
					continue;
				for(uint64_t cblkno = 0; cblkno < precinct->getNumCblks(); ++cblkno)
				{
					auto cblkBounds = precinct->getCodeBlockBounds(cblkno);
					if(wholeTileDecoding || paddedBandWindow->nonEmptyIntersection(&cblkBounds))
					{
						auto cblk = precinct->getDecompressedBlockPtr(cblkno);
						auto block = new DecompressBlockExec();
						block->x = cblk->x0;
						block->y = cblk->y0;
						block->tilec = tilec;
						block->bandIndex = bandIndex;
						block->bandNumbps = band->numbps;
						block->bandOrientation = band->orientation;
						block->cblk = cblk;
						block->cblk_sty = tccp->cblk_sty;
						block->qmfbid = tccp->qmfbid;
						block->resno = resno;
						block->roishift = tccp->roishift;
						block->stepsize = band->stepsize;
						block->k_msbs = (uint8_t)(band->numbps - cblk->numbps);
						block->R_b = prec + gain_b[band->orientation];
						resBlocks.push_back(block);
					}
				}
			}
		}
		if (!resBlocks.empty())
			blocks.push_back(resBlocks);
	}
}
bool DecompressScheduler::scheduleDecompress(TileComponent* tilec,TileCodingParams* tcp,
												TileComponentCodingParams* tccp,
											   DecompressBlocks &blocks,
											   uint8_t prec)
{
	prepareScheduleDecompress(tilec, tccp, blocks,prec);
	// nominal code block dimensions
	uint16_t codeblock_width = (uint16_t)(tccp->cblkw ? (uint32_t)1 << tccp->cblkw : 0);
	uint16_t codeblock_height = (uint16_t)(tccp->cblkh ? (uint32_t)1 << tccp->cblkh : 0);
	for(auto i = 0U; i < ExecSingleton::get()->num_workers(); ++i)
		t1Implementations.push_back(
			T1Factory::makeT1(false, tcp, codeblock_width, codeblock_height));

	return decompress(blocks);
}
bool DecompressScheduler::decompress(DecompressBlocks &blocks)
{
	if(!blocks.size())
		return true;
	size_t num_threads = ExecSingleton::get()->num_workers();
	success = true;
	if(num_threads == 1)
	{
		for(auto& rb : blocks){
			for (auto& block : rb) {
				if(!success)
				{
					delete block;
				}
				else
				{
					auto impl = t1Implementations[(size_t)0];
					if(!decompressBlock(impl, block))
						success = false;
				}
			}
		}

		return success;
	}
	size_t maxBlocks = 0;
	for(auto& rb : blocks)
		maxBlocks += rb.size();
	decodeBlocks = new DecompressBlockExec*[maxBlocks];
	size_t ct = 0;
	for(auto& rb : blocks){
		for (auto& block : rb) {
			decodeBlocks[ct++] = block;
		}
	}
	tf::Taskflow taskflow;
	auto tasks = new tf::Task[maxBlocks];
	for(uint64_t i = 0; i < maxBlocks; i++)
		tasks[i] = taskflow.placeholder();
	for(uint64_t i = 0; i < maxBlocks; i++)
	{
		auto block = decodeBlocks[i];
		tasks[i].work([this, block] {
			if(!success)
			{
				delete block;
			} else {
				auto threadnum = ExecSingleton::get()->this_worker_id();
				auto impl = t1Implementations[(size_t)threadnum];
				if(!decompressBlock(impl, block))
					success = false;
			}
		});
	}
	ExecSingleton::get()->run(taskflow).wait();

	delete[] decodeBlocks;
	delete[] tasks;

	return success;
}
bool DecompressScheduler::decompressBlock(T1Interface* impl, DecompressBlockExec* block)
{
	try
	{
		bool rc = block->open(impl);
		delete block;
		return rc;
	}
	catch(std::runtime_error& rerr)
	{
		delete block;
		GRK_ERROR(rerr.what());
		return false;
	}

	return true;
}

} // namespace grk
