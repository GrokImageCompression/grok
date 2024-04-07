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
const uint8_t gain_b[4] = {0, 1, 1, 2};

void ResDecompressBlocks::clear(void)
{
   blocks_.clear();
}
bool ResDecompressBlocks::empty(void) const
{
   return blocks_.empty();
}
void ResDecompressBlocks::release(void)
{
   for(const auto& b : blocks_)
	  delete b;
   blocks_.clear();
}

DecompressScheduler::DecompressScheduler(TileProcessor* tileProcessor, Tile* tile,
										 TileCodingParams* tcp, uint8_t prec)
	: Scheduler(tile), tileProcessor_(tileProcessor), tcp_(tcp), prec_(prec),
	  numcomps_(tile->numcomps_), tileBlocks_(TileDecompressBlocks(numcomps_)),
	  waveletReverse_(nullptr)
{
   waveletReverse_ = new WaveletReverse*[numcomps_];
   for(uint16_t compno = 0; compno < numcomps_; ++compno)
	  waveletReverse_[compno] = nullptr;
}
DecompressScheduler::~DecompressScheduler()
{
   if(waveletReverse_)
   {
	  for(uint16_t compno = 0; compno < numcomps_; ++compno)
		 delete waveletReverse_[compno];
	  delete[] waveletReverse_;
   }
}
bool DecompressScheduler::schedule(uint16_t compno)
{
   auto tilec = tile_->comps + compno;

   if(!scheduleBlocks(compno))
	  return false;
   auto imageFlow = getImageComponentFlow(compno);
   if(imageFlow)
   {
	  // composite blocks and wavelet
	  imageFlow->addTo(codecFlow_);
	  // generate dependency graph
	  graph(compno);
   }
   uint8_t numRes = tilec->highestResolutionDecompressed + 1U;
   if(numRes > 0 && !scheduleWavelet(compno))
   {
	  for(uint16_t i = 0; i < numcomps_; ++i)
		 releaseBlocks(i);
	  return false;
   }

   return true;
}

void DecompressScheduler::releaseBlocks(uint16_t compno)
{
   auto& componentBlocks = tileBlocks_[compno];
   for(auto& rb : componentBlocks)
	  rb.release();
   componentBlocks.clear();
}

bool DecompressScheduler::scheduleBlocks(uint16_t compno)
{
   ComponentDecompressBlocks blocks;
   ResDecompressBlocks resBlocks;
   auto tccp = tcp_->tccps + compno;
   auto tilec = tile_->comps + compno;
   bool wholeTileDecoding = tilec->isWholeTileDecoding();
   uint8_t resno = 0;
   for(; resno <= tilec->highestResolutionDecompressed; ++resno)
   {
	  auto res = tilec->resolutions_ + resno;
	  for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
	  {
		 auto band = res->tileBand + bandIndex;
		 auto paddedBandWindow = tilec->getWindow()->getBandWindowPadded(resno, band->orientation);
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
				  block->R_b = prec_ + gain_b[band->orientation];
				  resBlocks.blocks_.push_back(block);
			   }
			}
		 }
	  }
	  // combine first two resolutions together into single resBlock
	  if(!resBlocks.blocks_.empty() && resno > 0)
	  {
		 blocks.push_back(resBlocks);
		 resBlocks.clear();
	  }
   }
   // handle case where only one resolution is decompressed
   // (in this case, there will be no wavelet transform)
   if(!resBlocks.empty())
   {
	  assert(tilec->highestResolutionDecompressed == 0);
	  blocks.push_back(resBlocks);
	  resBlocks.clear();
   }
   if(blocks.empty())
	  return true;

   uint8_t numResolutions = (tile_->comps + compno)->highestResolutionDecompressed + 1;
   imageComponentFlows_[compno] = new ImageComponentFlow(numResolutions);
   if(!tile_->comps->isWholeTileDecoding())
	  imageComponentFlows_[compno]->setRegionDecompression();

   // nominal code block dimensions
   uint16_t codeblock_width = (uint16_t)(tccp->cblkw ? (uint32_t)1 << tccp->cblkw : 0);
   uint16_t codeblock_height = (uint16_t)(tccp->cblkh ? (uint32_t)1 << tccp->cblkh : 0);
   for(auto i = 0U; i < ExecSingleton::get().num_workers(); ++i)
	  t1Implementations.push_back(
		  T1Factory::makeT1(false, tcp_, codeblock_width, codeblock_height));

   size_t num_threads = ExecSingleton::get().num_workers();
   success = true;
   if(num_threads == 1)
   {
	  for(auto& rb : blocks)
	  {
		 for(auto& block : rb.blocks_)
		 {
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
   uint8_t resFlowNum = 0;
   for(auto& rb : blocks)
   {
	  auto resFlow = imageComponentFlows_[compno]->resFlows_ + resFlowNum;
	  for(auto& block : rb.blocks_)
	  {
		 resFlow->blocks_->nextTask().work([this, block] {
			if(!success)
			{
			   delete block;
			}
			else
			{
			   auto threadnum = ExecSingleton::get().this_worker_id();
			   auto impl = t1Implementations[(size_t)threadnum];
			   if(!decompressBlock(impl, block))
				  success = false;
			}
		 });
	  }
	  resFlowNum++;
   }
   auto& componentBlocks = tileBlocks_[compno];
   for(auto rb : blocks)
	  componentBlocks.push_back(rb);

   return true;
}
bool DecompressScheduler::decompressBlock(T1Interface* impl, DecompressBlockExec* block)
{
   try
   {
	  bool rc = block->open(impl);
	  delete block;
	  return rc;
   }
   catch(const std::runtime_error& rerr)
   {
	  delete block;
	  Logger::logger_.error(rerr.what());
	  return false;
   }

   return true;
}

bool DecompressScheduler::scheduleWavelet(uint16_t compno)
{
   auto tilec = tile_->comps + compno;
   uint8_t numRes = tilec->highestResolutionDecompressed + 1U;
   waveletReverse_[compno] =
	   new WaveletReverse(tileProcessor_, tilec, compno, tilec->getWindow()->unreducedBounds(),
						  numRes, (tcp_->tccps + compno)->qmfbid);

   return waveletReverse_[compno]->decompress();
}

} // namespace grk
