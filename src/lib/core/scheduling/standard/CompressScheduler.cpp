/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "ImageComponentFlow.h"
#include "IStream.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "CodingParams.h"

#include "ICoder.h"
#include "CoderPool.h"
#include "BitIO.h"

#include "TagTree.h"

#include "CodeblockCompress.h"

#include "CodeblockDecompress.h"
#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"

#include "CodecScheduler.h"
#include "SchedulerStandard.h"
#include "TileComponentWindow.h"
#include "canvas/tile/Tile.h"
#include "CoderFactory.h"
#include "CompressScheduler.h"

namespace grk
{
CompressScheduler::CompressScheduler(Tile* tile, bool needsRateControl, TileCodingParams* tcp,
                                     const double* mct_norms, uint16_t mct_numcomps)
    : SchedulerStandard(tile->numcomps_), tile_(tile), needsRateControl_(needsRateControl),
      blockCount_(-1), tcp_(tcp), mct_norms_(mct_norms), mct_numcomps_(mct_numcomps)
{}

bool CompressScheduler::scheduleT1(ITileProcessor* proc)
{
  (void)proc;
  tile_->distortion_ = 0;
  std::vector<t1::CompressBlockExec*> blocks;
  uint16_t maxCblkW = 0;
  uint16_t maxCblkH = 0;

  for(uint16_t compno = 0; compno < tile_->numcomps_; ++compno)
  {
    auto tilec = tile_->comps_ + compno;
    auto tccp = tcp_->tccps_ + compno;
    for(uint8_t resno = 0; resno < tilec->num_resolutions_; ++resno)
    {
      auto res = &tilec->resolutions_[resno];
      for(uint8_t bandIndex = 0; bandIndex < res->numBands_; ++bandIndex)
      {
        auto band = &res->band[bandIndex];
        for(auto prc : band->precincts_)
        {
          auto nominalBlockSize = prc->getNominalBlockSize();
          for(uint32_t cblkno = 0; cblkno < prc->getNumCblks(); ++cblkno)
          {
            auto cblk = prc->getCompressedBlock(cblkno);
            if(cblk->empty())
              continue;
            if(!cblk->allocData(nominalBlockSize))
              continue;
            auto block = new t1::CompressBlockExec();
            block->tile_width =
                (tile_->comps_ + compno)->getWindow()->getResWindowBufferHighestStride();
            block->doRateControl = needsRateControl_;
            block->x = cblk->x0();
            block->y = cblk->y0();
            tilec->getWindow()->toRelativeCoordinates(resno, band->orientation_, block->x,
                                                      block->y);
            auto highest = tilec->getWindow()->getResWindowBufferHighestSimple();
            block->tiledp =
                highest.buf_ + (uint64_t)block->x + block->y * (uint64_t)highest.stride_;
            maxCblkW = std::max<uint16_t>(maxCblkW, (uint16_t)(1 << tccp->cblkw_expn_));
            maxCblkH = std::max<uint16_t>(maxCblkH, (uint16_t)(1 << tccp->cblkh_expn_));
            block->compno = compno;
            block->bandOrientation = band->orientation_;
            block->cblk = cblk;
            block->cblk_sty = tccp->cblkStyle_;
            block->qmfbid = tccp->qmfbid_;
            block->resno = resno;
            block->level = (uint8_t)((tile_->comps_ + compno)->num_resolutions_ - 1 - resno);
            block->inv_step_ht = 1.0f / band->stepsize_;
            block->stepsize = band->stepsize_;
            block->mct_norms = mct_norms_;
            block->mct_numcomps = mct_numcomps_;
            block->k_msbs = (uint8_t)(band->maxBitPlanes_ - cblk->numbps());
            blocks.push_back(block);
          }
        }
      }
    }
  }
  if(blocks.size() == 0)
    return true;

  for(auto i = 0U; i < TFSingleton::num_threads(); ++i)
    coders_.push_back(t1::CoderFactory::makeCoder(tcp_->isHT(), true, maxCblkW, maxCblkH, 0));

  size_t num_threads = TFSingleton::num_threads();
  encodeBlocks_ = blocks;
  const size_t maxBlocks = blocks.size();

  tf::Taskflow taskflow;
  num_threads = TFSingleton::num_threads();
  auto node = new tf::Task[num_threads];
  for(auto i = 0U; i < num_threads; i++)
    node[i] = taskflow.placeholder();
  for(auto i = 0U; i < num_threads; i++)
  {
    node[i].work([this, maxBlocks] {
      auto threadnum = TFSingleton::get().this_worker_id();
      while(compress((size_t)threadnum, maxBlocks))
      {
      }
    });
  }
  TFSingleton::get().run(taskflow).wait();
  delete[] node;

  return true;
}

bool CompressScheduler::compress(size_t workerId, uint64_t maxBlocks)
{
  auto coder = coders_[workerId];
  uint64_t index = (uint64_t)++blockCount_;
  if(index >= maxBlocks)
    return false;
  auto block = encodeBlocks_[index];
  compress(coder, block);
  delete block;

  return true;
}
void CompressScheduler::compress(t1::ICoder* coder, t1::CompressBlockExec* block)
{
  block->open(coder);
  if(needsRateControl_)
  {
    std::unique_lock<std::mutex> lk(distortion_mutex_);
    tile_->distortion_ += block->distortion;
  }
}

} // namespace grk
