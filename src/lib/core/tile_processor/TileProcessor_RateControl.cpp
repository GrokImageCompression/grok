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
#include "grk_includes.h"
#include "TileFutureManager.h"
#include "FlowComponent.h"
#include "IStream.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
struct TileProcessorCompress;
} // namespace grk
#include "CodeStream.h"
#include "PacketIter.h"
#include "PacketLengthCache.h"
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
#include "TileComponentWindow.h"
#include "WaveletFwd.h"
#include "canvas/tile/Tile.h"
#include "T2Compress.h"
#include "plugin_bridge.h"
#include "RateControl.h"
#include "RateInfo.h"
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
            auto cblk = prc->getCompressedBlock(cblkno);
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
  RateInfo rateInfo;
  uint64_t numPacketsPerLayer = 0;
  uint64_t numCodeBlocks = 0;
  bool debug = false;
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
          numPacketsPerLayer++;
          for(uint32_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
          {
            numCodeBlocks++;
            auto cblk = prc->getCompressedBlock(cblkno);
            uint32_t num_pix = (uint32_t)cblk->area();
            if(!(state & GRK_PLUGIN_STATE_PRE_TR1))
            {
              compress_synch_with_plugin(this, compno, resno, bandIndex, precinctIndex, cblkno,
                                         band, cblk, &num_pix);
            }

            if(!single_lossless)
            {
              RateControl::convexHull(cblk->getPass(0), cblk->getNumPasses());
              rateInfo.synch(cblk);
              numpix += num_pix;
            }
          } /* cbklno */
        } /* precinctIndex */
      } /* bandIndex */
    } /* resno */

    if(!single_lossless)
    {
      maxSE += (double)(((uint64_t)1 << headerImage_->comps[compno].prec) - 1) *
               (double)(((uint64_t)1 << headerImage_->comps[compno].prec) - 1) * (double)numpix;
    }
  } /* compno */
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
  uint32_t min_slope = rateInfo.getMinimumThresh();
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
        bool allocationChanged = makeLayerFeasible(layno, (uint16_t)thresh, false);
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
             !t2.compressPacketsSimulate(tileIndex_, (uint16_t)(layno + 1U), allPacketBytes,
                                         maxLayerLength, newTilePartProgressionPosition_,
                                         packetLengthCache_->getMarkers(), false, false))
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
      makeLayerFeasible(layno, (uint16_t)goodthresh, true);
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
  uint64_t numPacketsPerLayer = 0;
  uint64_t numCodeBlocks = 0;
  auto tcp = tcp_;
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
          numPacketsPerLayer++;
          for(uint32_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
          {
            auto cblk = prc->getCompressedBlock(cblkno);
            uint32_t num_pix = (uint32_t)cblk->area();
            numCodeBlocks++;
            if(!(state & GRK_PLUGIN_STATE_PRE_TR1))
            {
              compress_synch_with_plugin(this, compno, resno, bandIndex, precinctIndex, cblkno,
                                         band, cblk, &num_pix);
            }
            if(!single_lossless)
            {
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
            }
          } /* cbklno */
        } /* precinctIndex */
      } /* bandIndex */
    } /* resno */
    if(!single_lossless)
      maxSE += (double)(((uint64_t)1 << headerImage_->comps[compno].prec) - 1) *
               (double)(((uint64_t)1 << headerImage_->comps[compno].prec) - 1) * (double)numpix;

  } /* compno */

  auto t2 = T2Compress(this);
  if(single_lossless)
  {
    // simulation will generate correct PLT lengths
    // and correct tile length
    return t2.compressPacketsSimulate(tileIndex_, 0 + 1U, allPacketBytes, UINT_MAX,
                                      newTilePartProgressionPosition_,
                                      packetLengthCache_->getMarkers(), true, false);
  }
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
        makeLayerSimple(layno, thresh, false);
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
          if(!t2.compressPacketsSimulate(tileIndex_, layno + 1U, allPacketBytes, maxLayerLength,
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
      makeLayerSimple(layno, goodthresh, true);
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
            auto cblk = prc->getCompressedBlock(cblkno);
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
            auto cblk = prc->getCompressedBlock(cblkno);
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
