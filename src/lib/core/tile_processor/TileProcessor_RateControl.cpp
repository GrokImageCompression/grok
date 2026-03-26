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

#include <cfloat>

#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "TileFutureManager.h"
#include "FlowComponent.h"
#include "IStream.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImage.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
struct ITileProcessorCompress;
} // namespace grk
#include "CodeStream.h"
#include "PacketIter.h"
#include "PacketLengthCache.h"
#include "CoderPool.h"
#include "BitIO.h"
#include "TagTree.h"
#include "CodeblockCompress.h"
#include "CodeblockDecompress.h"
#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"
#include "CodecScheduler.h"
#include "Tile.h"
#include "T2Compress.h"
#include "plugin_bridge.h"
#include "RateControl.h"
#include "RateInfo.h"
#include "CompressScheduler.h"
#include "TFSingleton.h"
#include "TileProcessorCompress.h"

namespace grk
{

bool TileProcessorCompress::rateAllocate(uint32_t* allPacketBytes, bool disableRateControl)
{
  // rate control by rate/distortion or fixed quality
  switch(cp_->codingParams_.enc_.rateControlAlgorithm_)
  {
    case 0:
      return pcrdBisectSimple(allPacketBytes, disableRateControl);
    default:
      return pcrdBisectFeasible(allPacketBytes, disableRateControl);
  }
}
bool TileProcessorCompress::layerNeedsRateControl(uint16_t layno)
{
  auto enc_params = &cp_->codingParams_.enc_;
  return (enc_params->allocationByRateDistortion_ && (tcp_->rates_[layno] > 0.0)) ||
         (enc_params->allocationByFixedQuality_ && (tcp_->distortion_[layno] > 0.0f));
}
bool TileProcessorCompress::needsRateControl()
{
  for(uint16_t i = 0; i < tcp_->numLayers_; ++i)
  {
    if(layerNeedsRateControl(i))
      return true;
  }
  return false;
}
// lossless in the sense that no code passes are removed; it mays still be a lossless layer
// due to irreversible DWT and quantization
bool TileProcessorCompress::makeSingleLosslessLayer()
{
  if(tcp_->numLayers_ != 1 || layerNeedsRateControl(0))
    return false;

  makeLayerFinal(0);

  return true;
}
bool TileProcessorCompress::makeLayerFeasible(uint16_t layno, uint16_t thresh, bool finalAttempt)
{
  tile_->setLayerDistortion(layno, 0);
  bool allocationChanged = false;
  for(uint16_t compno = 0; compno < tile_->numcomps_; compno++)
  {
    auto tilec = tile_->comps_ + compno;
    for(uint8_t resno = 0; resno < tilec->num_resolutions_; resno++)
    {
      auto res = tilec->resolutions_ + resno;
      for(uint8_t bandIndex = 0; bandIndex < res->numBands_; bandIndex++)
      {
        auto band = res->band + bandIndex;
        for(auto prc : band->precincts_)
        {
          for(uint32_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
          {
            auto cblk = prc->getCompressBlock(cblkno);
            auto layer = cblk->getLayer(layno);
            uint8_t cumulative_included_passes_in_block;

            if(layno == 0)
              cblk->setNumPassesInPreviousLayers(0);

            cumulative_included_passes_in_block = cblk->getNumPassesInPreviousLayers();

            for(auto passno = cblk->getNumPassesInPreviousLayers(); passno < cblk->getNumPasses();
                passno++)
            {
              auto pass = cblk->getPass(passno);
              // truncate or include feasible, otherwise ignore
              if(pass->slope_)
              {
                if(pass->slope_ <= thresh)
                  break;
                cumulative_included_passes_in_block = passno + 1;
              }
            }
            layer->totalPasses_ =
                cumulative_included_passes_in_block - cblk->getNumPassesInPreviousLayers();
            if(!layer->totalPasses_)
            {
              layer->distortion = 0;
              continue;
            }
            // update layer
            allocationChanged = true;
            if(cblk->getNumPassesInPreviousLayers() == 0)
            {
              layer->len = cblk->getPass(cumulative_included_passes_in_block - 1)->rate_;
              layer->data = cblk->getPaddedCompressedStream();
              layer->distortion =
                  cblk->getPass(cumulative_included_passes_in_block - 1)->distortiondec_;
            }
            else
            {
              layer->len = cblk->getPass(cumulative_included_passes_in_block - 1)->rate_ -
                           cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->rate_;
              layer->data = cblk->getPaddedCompressedStream() +
                            cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->rate_;
              layer->distortion =
                  cblk->getPass(cumulative_included_passes_in_block - 1)->distortiondec_ -
                  cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->distortiondec_;
            }

            tile_->incLayerDistortion(layno, layer->distortion);
            if(finalAttempt)
              cblk->setNumPassesInPreviousLayers(cumulative_included_passes_in_block);
          }
        }
      }
    }
  }

  return allocationChanged;
}
/*
 Hybrid rate control using bisect algorithm with optimal truncation points
 */
bool TileProcessorCompress::pcrdBisectFeasible(uint32_t* allPacketBytes, bool disableRateControl)
{
  bool single_lossless = tcp_->numLayers_ == 1 && !layerNeedsRateControl(0);
  const double K = 1;
  double maxSE = 0;
  auto tcp = tcp_;
  uint32_t state = grk_plugin_get_debug_state();
  bool debug = false;

  auto t2 = T2Compress(this);
  if(single_lossless)
  {
    makeSingleLosslessLayer();

    // simulation will generate correct PLT lengths
    // and correct tile length
    return t2.compressPacketsSimulate(tileIndex_, 0 + 1U, allPacketBytes, UINT_MAX,
                                      newTilePartProgressionPosition_,
                                      packetLengthCache_->getMarkers(), true, false);
  }

  // Use pre-computed stats from T1 encoding when available (no plugin override)
  auto compressScheduler = dynamic_cast<CompressScheduler*>(scheduler_);
  uint32_t min_slope;
  if(compressScheduler && !(state & GRK_PLUGIN_STATE_PRE_TR1))
  {
    auto& stats = compressScheduler->getRateControlStats();
    min_slope = stats.minimumSlope_.load(std::memory_order_relaxed);
    for(uint16_t compno = 0; compno < tile_->numcomps_; compno++)
    {
      uint64_t numpix = stats.numpixByComponent_[compno].load(std::memory_order_relaxed);
      maxSE += (double)(((uint64_t)1 << headerImage_->comps[compno].prec) - 1) *
               (double)(((uint64_t)1 << headerImage_->comps[compno].prec) - 1) * (double)numpix;
    }
  }
  else
  {
    // Fallback: serial traversal (plugin path or no scheduler)
    RateInfo rateInfo;
    for(uint16_t compno = 0; compno < tile_->numcomps_; compno++)
    {
      auto tilec = &tile_->comps_[compno];
      uint64_t numpix = 0;
      for(uint8_t resno = 0; resno < tilec->num_resolutions_; resno++)
      {
        auto res = &tilec->resolutions_[resno];
        for(uint8_t bandIndex = 0; bandIndex < res->numBands_; bandIndex++)
        {
          auto band = &res->band[bandIndex];
          for(auto [precinctIndex, vectorIndex] : band->precinctMap_)
          {
            auto prc = band->precincts_[vectorIndex];
            for(uint32_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
            {
              auto cblk = prc->getCompressBlock(cblkno);
              uint32_t num_pix = (uint32_t)cblk->area();
              if(!(state & GRK_PLUGIN_STATE_PRE_TR1))
              {
                compress_synch_with_plugin(this, compno, resno, bandIndex, precinctIndex, cblkno,
                                           band, cblk, &num_pix);
              }

              RateControl::convexHull(cblk->getPass(0), cblk->getNumPasses());
              rateInfo.synch(cblk);
              numpix += num_pix;
            } /* cbklno */
          } /* precinctIndex */
        } /* bandIndex */
      } /* resno */

      maxSE += (double)(((uint64_t)1 << headerImage_->comps[compno].prec) - 1) *
               (double)(((uint64_t)1 << headerImage_->comps[compno].prec) - 1) * (double)numpix;
    } /* compno */
    min_slope = rateInfo.getMinimumThresh();
  }

  // Build flat codeblock vector for parallel makeLayer
  std::vector<t1::CodeblockCompress*> flatCodeblocks;
  for(uint16_t compno = 0; compno < tile_->numcomps_; compno++)
  {
    auto tilec = tile_->comps_ + compno;
    for(uint8_t resno = 0; resno < tilec->num_resolutions_; resno++)
    {
      auto res = tilec->resolutions_ + resno;
      for(uint8_t bandIndex = 0; bandIndex < res->numBands_; bandIndex++)
      {
        auto band = res->band + bandIndex;
        for(auto prc : band->precincts_)
        {
          for(uint32_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
            flatCodeblocks.push_back(prc->getCompressBlock(cblkno));
        }
      }
    }
  }

  const size_t numBlocks = flatCodeblocks.size();
  const size_t numWorkers = TFSingleton::num_threads();
  const bool useParallel = numBlocks >= 256 && numWorkers > 1;

  auto runMakeLayerFeasible = [&](uint16_t layno, uint16_t thresh, bool finalAttempt,
                                  uint64_t* bodyBytesOut) -> bool {
    if(bodyBytesOut)
      *bodyBytesOut = 0;
    if(!useParallel)
      return makeLayerFeasible(layno, thresh, finalAttempt);

    std::atomic<double> totalDistortion{0.0};
    std::atomic<bool> allocationChanged{false};
    std::atomic<uint64_t> totalBodyBytes{0};
    std::atomic<size_t> blockIdx{0};

    tf::Taskflow taskflow;
    for(size_t w = 0; w < numWorkers; w++)
    {
      taskflow.emplace([&]() {
        double localDistortion = 0.0;
        uint64_t localBodyBytes = 0;
        bool localChanged = false;
        while(true)
        {
          auto i = blockIdx.fetch_add(1, std::memory_order_relaxed);
          if(i >= numBlocks)
            break;
          auto cblk = flatCodeblocks[i];
          auto layer = cblk->getLayer(layno);
          uint8_t cumulative_included_passes_in_block;

          if(layno == 0)
            cblk->setNumPassesInPreviousLayers(0);

          cumulative_included_passes_in_block = cblk->getNumPassesInPreviousLayers();

          for(auto passno = cblk->getNumPassesInPreviousLayers();
              passno < cblk->getNumPasses(); passno++)
          {
            auto pass = cblk->getPass(passno);
            if(pass->slope_)
            {
              if(pass->slope_ <= thresh)
                break;
              cumulative_included_passes_in_block = passno + 1;
            }
          }

          layer->totalPasses_ =
              cumulative_included_passes_in_block - cblk->getNumPassesInPreviousLayers();
          if(!layer->totalPasses_)
          {
            layer->distortion = 0;
            continue;
          }

          localChanged = true;
          if(cblk->getNumPassesInPreviousLayers() == 0)
          {
            layer->len = cblk->getPass(cumulative_included_passes_in_block - 1)->rate_;
            layer->data = cblk->getPaddedCompressedStream();
            layer->distortion =
                cblk->getPass(cumulative_included_passes_in_block - 1)->distortiondec_;
          }
          else
          {
            layer->len = cblk->getPass(cumulative_included_passes_in_block - 1)->rate_ -
                         cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->rate_;
            layer->data =
                cblk->getPaddedCompressedStream() +
                cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->rate_;
            layer->distortion =
                cblk->getPass(cumulative_included_passes_in_block - 1)->distortiondec_ -
                cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->distortiondec_;
          }
          localDistortion += layer->distortion;
          localBodyBytes += layer->len;
          if(finalAttempt)
            cblk->setNumPassesInPreviousLayers(cumulative_included_passes_in_block);
        }
        if(localDistortion != 0.0)
          totalDistortion.fetch_add(localDistortion, std::memory_order_relaxed);
        if(localBodyBytes != 0)
          totalBodyBytes.fetch_add(localBodyBytes, std::memory_order_relaxed);
        if(localChanged)
          allocationChanged.store(true, std::memory_order_relaxed);
      });
    }

    auto& executor = TFSingleton::get();
    if(executor.this_worker_id() >= 0)
      executor.corun(taskflow);
    else
      executor.run(taskflow).wait();

    tile_->setLayerDistortion(layno, totalDistortion.load(std::memory_order_relaxed));
    if(bodyBytesOut)
      *bodyBytesOut = totalBodyBytes.load(std::memory_order_relaxed);
    return allocationChanged.load(std::memory_order_relaxed);
  };

  uint32_t max_slope = USHRT_MAX;
  double cumulativeDistortion[maxCompressLayersGRK];
  uint32_t upperBound = max_slope;
  uint32_t maxLayerLength = UINT_MAX;
  for(uint16_t layno = 0; layno < tcp->numLayers_; layno++)
  {
    maxLayerLength = (!disableRateControl && tcp->rates_[layno] > 0.0f)
                         ? ((uint32_t)ceil(tcp->rates_[layno]))
                         : UINT_MAX;
    if(layerNeedsRateControl(layno))
    {
      // thresh from previous iteration - starts off uninitialized
      // used to bail out if difference with current thresh is small enough
      uint32_t prevthresh = 0;
      double distortionTarget =
          tile_->distortion_ - ((K * maxSE) / pow(10.0, tcp->distortion_[layno] / 10.0));
      uint32_t lowerBound = min_slope;
      for(auto i = 0U; i < 128; ++i)
      {
        uint32_t thresh = (lowerBound + upperBound) >> 1;
        if(prevthresh != 0 && prevthresh == thresh)
          break;
        uint64_t bodyBytes = 0;
        bool allocationChanged =
            runMakeLayerFeasible(layno, (uint16_t)thresh, false, &bodyBytes);
        prevthresh = thresh;
        if(cp_->codingParams_.enc_.allocationByFixedQuality_)
        {
          double distoachieved =
              layno == 0 ? tile_->getLayerDistortion(0)
                         : cumulativeDistortion[layno - 1] + tile_->getLayerDistortion(layno);
          if(distoachieved < distortionTarget)
          {
            upperBound = thresh;
            continue;
          }
          lowerBound = thresh;
        }
        else
        {
          if(allocationChanged &&
             (bodyBytes > maxLayerLength ||
              !t2.compressPacketsSimulate(tileIndex_, (uint16_t)(layno + 1U), allPacketBytes,
                                          maxLayerLength, newTilePartProgressionPosition_,
                                          packetLengthCache_->getMarkers(), false, false)))
          {
            lowerBound = thresh;
            continue;
          }
          upperBound = thresh;
        }
      }
      // choose conservative value for goodthresh
      /* Threshold for Marcela Index */
      // start by including everything in this layer
      uint32_t goodthresh = upperBound;
      runMakeLayerFeasible(layno, (uint16_t)goodthresh, true, nullptr);
      if(cp_->codingParams_.enc_.allocationByFixedQuality_)
      {
        cumulativeDistortion[layno] =
            (layno == 0) ? tile_->getLayerDistortion(0)
                         : (cumulativeDistortion[layno - 1] + tile_->getLayerDistortion(layno));
      }
      // upper bound for next layer is initialized to lowerBound for current layer, minus one
      upperBound = lowerBound - 1;
    }
    else
    {
      makeLayerFinal(layno);
    }
  }

  // final simulation will generate correct PLT lengths
  // and correct tile length
  bool rc = t2.compressPacketsSimulate(tileIndex_, tcp->numLayers_, allPacketBytes, maxLayerLength,
                                       newTilePartProgressionPosition_,
                                       packetLengthCache_->getMarkers(), true, debug);

  // assert(!disableRateControl || rc);
  return rc;
}
/*
 Simple bisect algorithm to calculate optimal layer truncation points
 */
bool TileProcessorCompress::pcrdBisectSimple(uint32_t* allPacketBytes, bool disableRateControl)
{
  const double K = 1;
  double maxSE = 0;
  double min_slope = DBL_MAX;
  double max_slope = -1;
  uint32_t state = grk_plugin_get_debug_state();
  bool single_lossless = makeSingleLosslessLayer();
  auto tcp = tcp_;

  auto t2 = T2Compress(this);
  if(single_lossless)
  {
    // simulation will generate correct PLT lengths
    // and correct tile length
    return t2.compressPacketsSimulate(tileIndex_, 0 + 1U, allPacketBytes, UINT_MAX,
                                      newTilePartProgressionPosition_,
                                      packetLengthCache_->getMarkers(), true, false);
  }

  // Use pre-computed stats from T1 encoding when available (no plugin override)
  auto compressScheduler = dynamic_cast<CompressScheduler*>(scheduler_);
  if(compressScheduler && !(state & GRK_PLUGIN_STATE_PRE_TR1))
  {
    auto& stats = compressScheduler->getRateControlStats();
    min_slope = stats.minRDSlope_.load(std::memory_order_relaxed);
    max_slope = stats.maxRDSlope_.load(std::memory_order_relaxed);
    for(uint16_t compno = 0; compno < tile_->numcomps_; compno++)
    {
      uint64_t numpix = stats.numpixByComponent_[compno].load(std::memory_order_relaxed);
      maxSE += (double)(((uint64_t)1 << headerImage_->comps[compno].prec) - 1) *
               (double)(((uint64_t)1 << headerImage_->comps[compno].prec) - 1) * (double)numpix;
    }
  }
  else
  {
    // Fallback: serial traversal (plugin path or no scheduler)
    for(uint16_t compno = 0; compno < tile_->numcomps_; compno++)
    {
      auto tilec = &tile_->comps_[compno];
      uint64_t numpix = 0;
      for(uint8_t resno = 0; resno < tilec->num_resolutions_; resno++)
      {
        auto res = &tilec->resolutions_[resno];
        for(uint8_t bandIndex = 0; bandIndex < res->numBands_; bandIndex++)
        {
          auto band = &res->band[bandIndex];
          for(auto [precinctIndex, vectorIndex] : band->precinctMap_)
          {
            auto prc = band->precincts_[vectorIndex];
            for(uint32_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
            {
              auto cblk = prc->getCompressBlock(cblkno);
              uint32_t num_pix = (uint32_t)cblk->area();
              if(!(state & GRK_PLUGIN_STATE_PRE_TR1))
              {
                compress_synch_with_plugin(this, compno, resno, bandIndex, precinctIndex, cblkno,
                                           band, cblk, &num_pix);
              }
              for(uint8_t passno = 0U; passno < cblk->getNumPasses(); passno++)
              {
                auto pass = cblk->getPass(passno);
                int32_t dr;
                double dd, rdslope;
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
                if(dr == 0)
                  continue;
                rdslope = dd / dr;
                if(rdslope < min_slope)
                  min_slope = rdslope;
                if(rdslope > max_slope)
                  max_slope = rdslope;
              } /* passno */
              numpix += num_pix;
            } /* cbklno */
          } /* precinctIndex */
        } /* bandIndex */
      } /* resno */
      maxSE += (double)(((uint64_t)1 << headerImage_->comps[compno].prec) - 1) *
               (double)(((uint64_t)1 << headerImage_->comps[compno].prec) - 1) * (double)numpix;
    } /* compno */
  }

  // Build flat codeblock vector for parallel makeLayer
  std::vector<t1::CodeblockCompress*> flatCodeblocks;
  for(uint16_t compno = 0; compno < tile_->numcomps_; compno++)
  {
    auto tilec = tile_->comps_ + compno;
    for(uint8_t resno = 0; resno < tilec->num_resolutions_; resno++)
    {
      auto res = tilec->resolutions_ + resno;
      for(uint8_t bandIndex = 0; bandIndex < res->numBands_; bandIndex++)
      {
        auto band = res->band + bandIndex;
        for(auto prc : band->precincts_)
        {
          for(uint32_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
            flatCodeblocks.push_back(prc->getCompressBlock(cblkno));
        }
      }
    }
  }

  const size_t numBlocksSimple = flatCodeblocks.size();
  const size_t numWorkersSimple = TFSingleton::num_threads();
  const bool useParallelSimple = numBlocksSimple >= 256 && numWorkersSimple > 1;

  auto runMakeLayerSimple = [&](uint16_t layno, double thresh, bool finalAttempt,
                                uint64_t* bodyBytesOut) {
    if(bodyBytesOut)
      *bodyBytesOut = 0;
    if(!useParallelSimple)
    {
      makeLayerSimple(layno, thresh, finalAttempt);
      return;
    }

    std::atomic<double> totalDistortion{0.0};
    std::atomic<uint64_t> totalBodyBytes{0};
    std::atomic<size_t> blockIdx{0};

    tf::Taskflow taskflow;
    for(size_t w = 0; w < numWorkersSimple; w++)
    {
      taskflow.emplace([&]() {
        double localDistortion = 0.0;
        uint64_t localBodyBytes = 0;
        while(true)
        {
          auto i = blockIdx.fetch_add(1, std::memory_order_relaxed);
          if(i >= numBlocksSimple)
            break;
          auto cblk = flatCodeblocks[i];
          auto layer = cblk->getLayer(layno);
          uint8_t included_blk_passes;
          if(thresh == 0)
          {
            included_blk_passes = cblk->getNumPasses();
          }
          else
          {
            included_blk_passes = cblk->getNumPassesInPreviousLayers();
            for(auto passno = cblk->getNumPassesInPreviousLayers();
                passno < cblk->getNumPasses(); passno++)
            {
              uint32_t dr;
              double dd;
              const auto pass = cblk->getPass(passno);
              if(included_blk_passes == 0)
              {
                dr = pass->rate_;
                dd = pass->distortiondec_;
              }
              else
              {
                dr = pass->rate_ - cblk->getPass(included_blk_passes - 1)->rate_;
                dd = pass->distortiondec_ -
                     cblk->getPass(included_blk_passes - 1)->distortiondec_;
              }

              if(!dr)
              {
                if(dd != 0)
                  included_blk_passes = passno + 1;
                continue;
              }
              auto slope = dd / dr;
              if(thresh - slope < DBL_EPSILON)
                included_blk_passes = passno + 1;
            }
          }
          layer->totalPasses_ = included_blk_passes - cblk->getNumPassesInPreviousLayers();
          if(!layer->totalPasses_)
          {
            layer->distortion = 0;
            continue;
          }

          if(cblk->getNumPassesInPreviousLayers() == 0)
          {
            layer->len = cblk->getPass(included_blk_passes - 1)->rate_;
            layer->data = cblk->getPaddedCompressedStream();
            layer->distortion = cblk->getPass(included_blk_passes - 1)->distortiondec_;
          }
          else
          {
            layer->len = cblk->getPass(included_blk_passes - 1)->rate_ -
                         cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->rate_;
            layer->data =
                cblk->getPaddedCompressedStream() +
                cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->rate_;
            layer->distortion =
                cblk->getPass(included_blk_passes - 1)->distortiondec_ -
                cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->distortiondec_;
          }
          localDistortion += layer->distortion;
          localBodyBytes += layer->len;
          if(finalAttempt)
            cblk->setNumPassesInPreviousLayers(included_blk_passes);
        }
        if(localDistortion != 0.0)
          totalDistortion.fetch_add(localDistortion, std::memory_order_relaxed);
        if(localBodyBytes != 0)
          totalBodyBytes.fetch_add(localBodyBytes, std::memory_order_relaxed);
      });
    }

    auto& executor = TFSingleton::get();
    if(executor.this_worker_id() >= 0)
      executor.corun(taskflow);
    else
      executor.run(taskflow).wait();

    tile_->setLayerDistortion(layno, totalDistortion.load(std::memory_order_relaxed));
    if(bodyBytesOut)
      *bodyBytesOut = totalBodyBytes.load(std::memory_order_relaxed);
  };

  double cumulativeDistortion[maxCompressLayersGRK];
  double upperBound = max_slope;
  uint32_t maxLayerLength = UINT_MAX;
  for(uint16_t layno = 0; layno < tcp_->numLayers_; layno++)
  {
    maxLayerLength = (!disableRateControl && tcp->rates_[layno] > 0.0f)
                         ? ((uint32_t)ceil(tcp->rates_[layno]))
                         : UINT_MAX;
    if(layerNeedsRateControl(layno))
    {
      double lowerBound = min_slope;
      /* Threshold for Marcela Index */
      // start by including everything in this layer
      double goodthresh = 0;
      // thresh from previous iteration - starts off uninitialized
      // used to bail out if difference with current thresh is small enough
      double prevthresh = -1;
      double distortionTarget =
          tile_->distortion_ - ((K * maxSE) / pow(10.0, tcp_->distortion_[layno] / 10.0));
      double thresh;
      for(auto i = 0U; i < 128; ++i)
      {
        // thresh is half-way between lower and upper bound
        thresh = (upperBound == -1) ? lowerBound : (lowerBound + upperBound) / 2;
        uint64_t bodyBytes = 0;
        runMakeLayerSimple(layno, thresh, false, &bodyBytes);
        if(prevthresh != -1 && (fabs(prevthresh - thresh)) < 0.001)
          break;
        prevthresh = thresh;
        if(cp_->codingParams_.enc_.allocationByFixedQuality_)
        {
          double distoachieved =
              layno == 0 ? tile_->getLayerDistortion(0)
                         : cumulativeDistortion[layno - 1] + tile_->getLayerDistortion(layno);
          if(distoachieved < distortionTarget)
          {
            upperBound = thresh;
            continue;
          }
          lowerBound = thresh;
        }
        else
        {
          if(bodyBytes > maxLayerLength ||
             !t2.compressPacketsSimulate(tileIndex_, layno + 1U, allPacketBytes, maxLayerLength,
                                         newTilePartProgressionPosition_,
                                         packetLengthCache_->getMarkers(), false, false))
          {
            lowerBound = thresh;
            continue;
          }
          upperBound = thresh;
        }
      }
      // choose conservative value for goodthresh
      goodthresh = (upperBound == -1) ? thresh : upperBound;
      runMakeLayerSimple(layno, goodthresh, true, nullptr);
      cumulativeDistortion[layno] =
          (layno == 0) ? tile_->getLayerDistortion(0)
                       : (cumulativeDistortion[layno - 1] + tile_->getLayerDistortion(layno));

      // upper bound for next layer will equal lowerBound for previous layer, minus one
      upperBound = lowerBound - 1;
    }
    else
    {
      makeLayerFinal(layno);
      assert(layno == tcp_->numLayers_ - 1);
    }
  }

  // final simulation will generate correct PLT lengths
  // and correct tile length
  // grklog.info("Rate control final simulation");
  return t2.compressPacketsSimulate(tileIndex_, tcp_->numLayers_, allPacketBytes, maxLayerLength,
                                    newTilePartProgressionPosition_,
                                    packetLengthCache_->getMarkers(), true, false);
}
/*
 Form layer for bisect rate control algorithm
 */
void TileProcessorCompress::makeLayerSimple(uint16_t layno, double thresh, bool finalAttempt)
{
  tile_->setLayerDistortion(layno, 0);
  for(uint16_t compno = 0; compno < tile_->numcomps_; compno++)
  {
    auto tilec = tile_->comps_ + compno;
    for(uint8_t resno = 0; resno < tilec->num_resolutions_; resno++)
    {
      auto res = tilec->resolutions_ + resno;
      for(uint8_t bandIndex = 0; bandIndex < res->numBands_; bandIndex++)
      {
        auto band = res->band + bandIndex;
        for(auto prc : band->precincts_)
        {
          for(uint32_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
          {
            auto cblk = prc->getCompressBlock(cblkno);
            auto layer = cblk->getLayer(layno);
            uint8_t included_blk_passes;
            if(thresh == 0)
            {
              included_blk_passes = cblk->getNumPasses();
            }
            else
            {
              included_blk_passes = cblk->getNumPassesInPreviousLayers();
              for(auto passno = cblk->getNumPassesInPreviousLayers(); passno < cblk->getNumPasses();
                  passno++)
              {
                uint32_t dr;
                double dd;
                const auto pass = cblk->getPass(passno);
                if(included_blk_passes == 0)
                {
                  dr = pass->rate_;
                  dd = pass->distortiondec_;
                }
                else
                {
                  dr = pass->rate_ - cblk->getPass(included_blk_passes - 1)->rate_;
                  dd =
                      pass->distortiondec_ - cblk->getPass(included_blk_passes - 1)->distortiondec_;
                }

                if(!dr)
                {
                  if(dd != 0)
                    included_blk_passes = passno + 1;
                  continue;
                }
                auto slope = dd / dr;
                /* do not rely on float equality, check with DBL_EPSILON margin */
                if(thresh - slope < DBL_EPSILON)
                  included_blk_passes = passno + 1;
              }
            }
            layer->totalPasses_ = included_blk_passes - cblk->getNumPassesInPreviousLayers();
            if(!layer->totalPasses_)
            {
              layer->distortion = 0;
              continue;
            }

            // update layer
            if(cblk->getNumPassesInPreviousLayers() == 0)
            {
              layer->len = cblk->getPass(included_blk_passes - 1)->rate_;
              layer->data = cblk->getPaddedCompressedStream();
              layer->distortion = cblk->getPass(included_blk_passes - 1)->distortiondec_;
            }
            else
            {
              layer->len = cblk->getPass(included_blk_passes - 1)->rate_ -
                           cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->rate_;
              layer->data = cblk->getPaddedCompressedStream() +
                            cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->rate_;
              layer->distortion =
                  cblk->getPass(included_blk_passes - 1)->distortiondec_ -
                  cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->distortiondec_;
            }
            tile_->incLayerDistortion(layno, layer->distortion);
            if(finalAttempt)
              cblk->setNumPassesInPreviousLayers(included_blk_passes);
          }
        }
      }
    }
  }
}
// Add all remaining passes to this layer
void TileProcessorCompress::makeLayerFinal(uint16_t layno)
{
  tile_->setLayerDistortion(layno, 0);
  for(uint16_t compno = 0; compno < tile_->numcomps_; compno++)
  {
    auto tilec = tile_->comps_ + compno;
    for(uint8_t resno = 0; resno < tilec->num_resolutions_; resno++)
    {
      auto res = tilec->resolutions_ + resno;
      for(uint8_t bandIndex = 0; bandIndex < res->numBands_; bandIndex++)
      {
        auto band = res->band + bandIndex;
        for(auto prc : band->precincts_)
        {
          for(uint32_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
          {
            auto cblk = prc->getCompressBlock(cblkno);
            auto layer = cblk->getLayer(layno);
            uint8_t included_blk_passes = cblk->getNumPassesInPreviousLayers();
            if(cblk->getNumPasses() > cblk->getNumPassesInPreviousLayers())
              included_blk_passes = cblk->getNumPasses();

            layer->totalPasses_ = included_blk_passes - cblk->getNumPassesInPreviousLayers();
            if(!layer->totalPasses_)
            {
              layer->distortion = 0;
              continue;
            }
            // update layer
            if(cblk->getNumPassesInPreviousLayers() == 0)
            {
              layer->len = cblk->getPass(included_blk_passes - 1)->rate_;
              layer->data = cblk->getPaddedCompressedStream();
              layer->distortion = cblk->getPass(included_blk_passes - 1)->distortiondec_;
            }
            else
            {
              layer->len = cblk->getPass(included_blk_passes - 1)->rate_ -
                           cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->rate_;
              layer->data = cblk->getPaddedCompressedStream() +
                            cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->rate_;
              layer->distortion =
                  cblk->getPass(included_blk_passes - 1)->distortiondec_ -
                  cblk->getPass(cblk->getNumPassesInPreviousLayers() - 1)->distortiondec_;
            }
            tile_->incLayerDistortion(layno, layer->distortion);
            cblk->setNumPassesInPreviousLayers(included_blk_passes);
            assert(cblk->getNumPassesInPreviousLayers() == cblk->getNumPasses());
          }
        }
      }
    }
  }
}

} // namespace grk
