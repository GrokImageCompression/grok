/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
CompressScheduler::CompressScheduler(Tile* tile, bool needsRateControl, TileCodingParams* tcp,
									 const double* mct_norms, uint16_t mct_numcomps)
	: Scheduler(tile), tile(tile), needsRateControl(needsRateControl), encodeBlocks(nullptr),
	  blockCount(-1), tcp_(tcp), mct_norms_(mct_norms), mct_numcomps_(mct_numcomps)
{
   for(uint16_t compno = 0; compno < numcomps_; ++compno)
   {
	  uint8_t num_resolutions = (tile->comps + compno)->highestResolutionDecompressed + 1;
	  imageComponentFlows_[compno] = new ImageComponentFlow(num_resolutions);
   }
}
bool CompressScheduler::schedule(uint16_t compno)
{
   return scheduleBlocks(compno);
}
bool CompressScheduler::scheduleBlocks(uint16_t compno)
{
   uint8_t resno, bandIndex;
   tile->distortion = 0;
   std::vector<CompressBlockExec*> blocks;
   uint32_t maxCblkW = 0;
   uint32_t maxCblkH = 0;

   for(compno = 0; compno < tile->numcomps_; ++compno)
   {
	  auto tilec = tile->comps + compno;
	  auto tccp = tcp_->tccps + compno;
	  for(resno = 0; resno < tilec->numresolutions; ++resno)
	  {
		 auto res = &tilec->resolutions_[resno];
		 for(bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
		 {
			auto band = &res->tileBand[bandIndex];
			for(auto prc : band->precincts)
			{
			   auto nominalBlockSize = prc->getNominalBlockSize();
			   for(uint64_t cblkno = 0; cblkno < prc->getNumCblks(); ++cblkno)
			   {
				  auto cblk = prc->getCompressedBlockPtr(cblkno);
				  if(cblk->empty())
					 continue;
				  if(!cblk->allocData(nominalBlockSize))
					 continue;
				  auto block = new CompressBlockExec();
				  block->tile = tile;
				  block->doRateControl = needsRateControl;
				  block->x = cblk->x0;
				  block->y = cblk->y0;
				  tilec->getWindow()->toRelativeCoordinates(resno, band->orientation, block->x,
															block->y);
				  auto highest = tilec->getWindow()->getResWindowBufferHighestSimple();
				  block->tiledp =
					  highest.buf_ + (uint64_t)block->x + block->y * (uint64_t)highest.stride_;
				  maxCblkW = std::max<uint32_t>(maxCblkW, (uint32_t)(1 << tccp->cblkw));
				  maxCblkH = std::max<uint32_t>(maxCblkH, (uint32_t)(1 << tccp->cblkh));
				  block->compno = compno;
				  block->bandOrientation = band->orientation;
				  block->cblk = cblk;
				  block->cblk_sty = tccp->cblk_sty;
				  block->qmfbid = tccp->qmfbid;
				  block->resno = resno;
				  block->inv_step_ht = 1.0f / band->stepsize;
				  block->stepsize = band->stepsize;
				  block->mct_norms = mct_norms_;
				  block->mct_numcomps = mct_numcomps_;
				  block->k_msbs = (uint8_t)(band->numbps - cblk->numbps);
				  blocks.push_back(block);
			   }
			}
		 }
	  }
   }
   for(auto i = 0U; i < ExecSingleton::get().num_workers(); ++i)
	  t1Implementations.push_back(T1Factory::makeT1(true, tcp_, maxCblkW, maxCblkH));
   compress(&blocks);

   return true;
}

void CompressScheduler::compress(std::vector<CompressBlockExec*>* blocks)
{
   if(!blocks || blocks->size() == 0)
	  return;

   size_t num_threads = ExecSingleton::get().num_workers();
   if(num_threads == 1)
   {
	  auto impl = t1Implementations[0];
	  for(auto iter = blocks->begin(); iter != blocks->end(); ++iter)
	  {
		 compress(impl, *iter);
		 delete *iter;
	  }
	  return;
   }
   const size_t maxBlocks = blocks->size();
   encodeBlocks = new CompressBlockExec*[maxBlocks];
   for(uint64_t i = 0; i < maxBlocks; ++i)
	  encodeBlocks[i] = blocks->operator[](i);
   blocks->clear();

   tf::Taskflow taskflow;
   num_threads = ExecSingleton::get().num_workers();
   auto node = new tf::Task[num_threads];
   for(uint64_t i = 0; i < num_threads; i++)
	  node[i] = taskflow.placeholder();
   for(uint64_t i = 0; i < num_threads; i++)
   {
	  node[i].work([this, maxBlocks] {
		 auto threadnum = ExecSingleton::get().this_worker_id();
		 while(compress((size_t)threadnum, maxBlocks))
		 {
		 }
	  });
   }
   ExecSingleton::get().run(taskflow).wait();

   delete[] node;
   delete[] encodeBlocks;
}
bool CompressScheduler::compress(size_t threadId, uint64_t maxBlocks)
{
   auto impl = t1Implementations[threadId];
   uint64_t index = (uint64_t)++blockCount;
   if(index >= maxBlocks)
	  return false;
   auto block = encodeBlocks[index];
   compress(impl, block);
   delete block;

   return true;
}
void CompressScheduler::compress(T1Interface* impl, CompressBlockExec* block)
{
   block->open(impl);
   if(needsRateControl)
   {
	  std::unique_lock<std::mutex> lk(distortion_mutex);
	  tile->distortion += block->distortion;
   }
}

} // namespace grk
