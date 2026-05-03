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
#include "TFSingleton.h"

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
#include "RateControl.h"

namespace grk
{
CompressScheduler::CompressScheduler(Tile* tile, bool needsRateControl, TileCodingParams* tcp,
                                     const double* mct_norms, uint16_t mct_numcomps,
                                     bool progressiveRateControl)
    : SchedulerStandard(tile->numcomps_), tile_(tile), needsRateControl_(needsRateControl),
      progressiveRateControl_(progressiveRateControl),
      blockCount_(-1), tcp_(tcp), mct_norms_(mct_norms), mct_numcomps_(mct_numcomps)
{
  rateControlStats_.init(tile->numcomps_);
}

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
            auto cblk = prc->getCompressBlock(cblkno);
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
            block->use16BitDwt = tilec->is16BitDwt();
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

  encodeBlocks_ = blocks;
  const size_t maxBlocks = blocks.size();

  // Initialize progressive slope estimator when rate control is active.
  // The estimator needs: total samples (sum of all block areas) and target rate (bytes/sample).
  // This enables early termination of coding passes predicted to be discarded by PCRD.
  initSlopeEstimator(blocks);

  tf::Taskflow taskflow;
  size_t num_threads = TFSingleton::num_threads();
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

bool CompressScheduler::populateT1Flow(FlowComponent* flow)
{
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
            auto cblk = prc->getCompressBlock(cblkno);
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
            block->use16BitDwt = tilec->is16BitDwt();
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

  encodeBlocks_ = blocks;
  const size_t maxBlocks = blocks.size();

  // Initialize progressive slope estimator (same as scheduleT1 path)
  initSlopeEstimator(blocks);

  size_t num_threads = TFSingleton::num_threads();
  for(auto i = 0U; i < num_threads; i++)
  {
    flow->nextTask().work([this, maxBlocks] {
      auto threadnum = TFSingleton::get().this_worker_id();
      while(compress((size_t)threadnum, maxBlocks))
      {
      }
    });
  }

  return true;
}

bool CompressScheduler::compress(size_t workerId, uint64_t maxBlocks)
{
  auto coder = coders_[workerId];
  uint64_t index = (uint64_t)++blockCount_;
  if(index >= maxBlocks)
    return false;
  auto block = encodeBlocks_[index];

  // Set the early-stop threshold from the progressive estimator.
  // This is a lock-free atomic read — the estimator updates it as blocks complete.
  if(slopeEstimator_)
    block->earlyStopSlope = slopeEstimator_->getEarlyStopSlope();

  compress(coder, block);
  delete block;

  return true;
}
void CompressScheduler::compress(t1::ICoder* coder, t1::CompressBlockExec* block)
{
  block->open(coder);
  if(needsRateControl_)
  {
    {
      std::unique_lock<std::mutex> lk(distortion_mutex_);
      tile_->distortion_ += block->distortion;
    }

    // Collect rate control stats in-thread to avoid serial traversal later
    auto cblk = block->cblk;
    auto compno = block->compno;
    uint32_t num_pix = (uint32_t)cblk->area();
    rateControlStats_.numpixByComponent_[compno].fetch_add(num_pix, std::memory_order_relaxed);
    rateControlStats_.numCodeBlocks_.fetch_add(1, std::memory_order_relaxed);

    // Compute convex hull for feasible truncation points
    if(cblk->getNumPasses() > 0)
      RateControl::convexHull(cblk->getPass(0), cblk->getNumPasses());

    // Feed completed block data to the progressive slope estimator.
    // This builds the slope-rate histogram used to predict the PCRD threshold.
    if(slopeEstimator_ && cblk->getNumPasses() > 0)
    {
      // Extract slopes and rates into stack arrays for the estimator.
      // Maximum coding passes per block: 3 * maxBitPlanes ≈ 3*16 = 48.
      uint8_t numPasses = cblk->getNumPasses();
      uint16_t slopes[48];
      uint16_t rates[48];
      for(uint8_t p = 0; p < numPasses; p++)
      {
        auto pass = cblk->getPass(p);
        slopes[p] = pass->slope_;
        rates[p] = pass->rate_;
      }
      slopeEstimator_->updateStats(slopes, rates, numPasses, num_pix);
    }

    // Collect slope stats for both bisect algorithms
    for(uint8_t passno = 0; passno < cblk->getNumPasses(); passno++)
    {
      auto pass = cblk->getPass(passno);

      // Feasible bisect: track min/max log-domain slopes from convex hull
      if(pass->slope_ != 0)
      {
        rateControlStats_.updateMinSlope(pass->slope_);
        rateControlStats_.updateMaxSlope(pass->slope_);
      }

      // Simple bisect: track min/max raw RD slopes
      int32_t dr;
      double dd;
      if(passno == 0)
      {
        dr = (int32_t)pass->rate_;
        dd = pass->distortiondec_;
      }
      else
      {
        dr = (int32_t)(pass->rate_ - cblk->getPass(passno - 1)->rate_);
        dd = pass->distortiondec_ - cblk->getPass(passno - 1)->distortiondec_;
      }
      if(dr != 0)
      {
        double rdslope = dd / dr;
        rateControlStats_.updateMinRDSlope(rdslope);
        rateControlStats_.updateMaxRDSlope(rdslope);
      }
    }
  }
}

void CompressScheduler::initSlopeEstimator(
    const std::vector<t1::CompressBlockExec*>& blocks)
{
  // The progressive slope estimator requires:
  //   1. A target rate is specified (rate-distortion mode)
  //   2. Rate control is needed (not lossless, not zero-rate)
  //
  // For multi-layer encoding, we must use the HIGHEST quality layer's target
  // (the largest byte budget). This is the most permissive threshold — it
  // determines the minimum slope below which passes are DEFINITELY discarded
  // by ALL layers. Using a smaller layer's target would over-estimate the
  // threshold and incorrectly terminate passes needed by higher-quality layers.
  //
  // tcp_->rates_[k] contains target byte counts after conversion from
  // compression ratios. Higher indices = higher quality = more bytes.
  if(!needsRateControl_)
    return;
  if(!progressiveRateControl_)
    return;

  // Find the maximum target rate across all layers
  double maxTargetBytes = 0.0;
  for(uint16_t k = 0; k < tcp_->numLayers_; ++k)
  {
    if(tcp_->rates_[k] > maxTargetBytes)
      maxTargetBytes = tcp_->rates_[k];
  }
  if(maxTargetBytes <= 0.0)
    return;

  uint64_t totalSamples = 0;
  for(const auto* blk : blocks)
    totalSamples += static_cast<uint64_t>(blk->cblk->width()) * blk->cblk->height();

  if(totalSamples == 0)
    return;

  double targetRate = maxTargetBytes / static_cast<double>(totalSamples);
  slopeEstimator_ = std::make_unique<ProgressiveSlopeEstimator>(totalSamples, targetRate);
}

} // namespace grk
