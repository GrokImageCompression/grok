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
#include "grk_includes.h"
#include "TileFutureManager.h"
#include "ImageComponentFlow.h"
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
struct TileProcessor;
}
#include "CodeStream.h"
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
#include "TileProcessor.h"
#include "CoderFactory.h"
#include "DecompressWindowScheduler.h"

namespace grk
{

DecompressWindowScheduler::DecompressWindowScheduler(uint16_t numComps, uint8_t prec,
                                                     CoderPool* streamPool)
    : WindowScheduler(numComps), differentialInfo_(new DifferentialInfo[numComps]), prec_(prec),
      streamPool_(streamPool)
{}
DecompressWindowScheduler::~DecompressWindowScheduler()
{
  delete[] differentialInfo_;
}

void DecompressWindowScheduler::release(void) {}

bool DecompressWindowScheduler::schedule(TileProcessor* tileProcessor)
{
  auto tcp = tileProcessor->getTCP();
  bool cacheAll =
      (tileProcessor->getTileCacheStrategy() & GRK_TILE_CACHE_ALL) == GRK_TILE_CACHE_ALL;
  uint32_t num_threads = (uint32_t)ExecSingleton::num_threads();
  bool singleThread = num_threads == 1;
  bool finalLayer = tcp->layersToDecompress_ == tcp->numLayers_;

  uint8_t resMin = std::numeric_limits<uint8_t>::max();
  uint8_t resMax = 0;
  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    auto tilec = tileProcessor->getTile()->comps_ + compno;
    uint8_t resBegin =
        cacheAll ? (uint8_t)tilec->currentPacketProgressionState_.numResolutionsRead() : 0;
    uint8_t resUpperBound = tilec->nextPacketProgressionState_.numResolutionsRead();

    resMin = std::min(resMin, resBegin);
    resMax = std::max(resMax, resUpperBound);
  }
  ResolutionChecker rChecker(numcomps_, tileProcessor, cacheAll);
  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    // schedule blocks
    auto tccp = tcp->tccps_ + compno;
    // nominal code block dimensions
    uint16_t cbw = tccp->cblkw_expn_ ? (uint16_t)1 << tccp->cblkw_expn_ : 0;
    uint16_t cbh = tccp->cblkh_expn_ ? (uint16_t)1 << tccp->cblkh_expn_ : 0;
    auto activePool = &coderPool_;
    if(streamPool_ && streamPool_->contains(tccp->cblkw_expn_, tccp->cblkh_expn_))
      activePool = streamPool_;

    if(!cacheAll)
    {
      activePool->makeCoders(
          num_threads, tccp->cblkw_expn_, tccp->cblkh_expn_,
          [tcp, cbw, cbh, tileProcessor]() -> std::shared_ptr<t1::ICoder> {
            return std::shared_ptr<t1::ICoder>(t1::CoderFactory::makeCoder(
                tcp->isHT(), false, cbw, cbh, tileProcessor->getTileCacheStrategy()));
          });
    }

    auto tilec = tileProcessor->getTile()->comps_ + compno;
    auto wholeTileDecoding = tilec->isWholeTileDecoding();
    auto diffInfo = differentialInfo_ + compno;

    // 1. create blocks and store in blocksByRes
    diffInfo->layersDecompressed_ = tcp->layersToDecompress_;

    auto resBounds = rChecker.getResBounds(compno);
    uint8_t resno = resBounds.first;
    uint8_t resUpperBound = resBounds.second;
    for(; resno < resUpperBound; ++resno)
    {
      auto res = tilec->resolutions_ + resno;
      for(uint8_t bandIndex = 0; bandIndex < res->numBands_; ++bandIndex)
      {
        auto band = res->band + bandIndex;
        auto paddedBandWindow = tilec->getWindow()->getBandWindowPadded(resno, band->orientation_);
        for(auto precinct : band->precincts_)
        {
          // skip precincts that don't overlap with padded decompression window
          if(!wholeTileDecoding && !paddedBandWindow->nonEmptyIntersection(precinct))
            continue;
          for(uint32_t cblkno = 0; cblkno < precinct->getNumCblks(); ++cblkno)
          {
            auto cblkBounds = precinct->getCodeBlockBounds(cblkno);
            // skip code blocks that don't overlap with padded decompression window
            if(!wholeTileDecoding && !paddedBandWindow->nonEmptyIntersection(&cblkBounds))
              continue;

            auto cblk = precinct->getDecompressedBlock(cblkno);
            auto block = new t1::DecompressBlockExec(cacheAll);
            block->x = cblk->x0();
            block->y = cblk->y0();
            block->postProcessor_ =
                tcp->isHT() ? t1::DecompressBlockPostProcessor<int32_t>(
                                  [tilec](int32_t* srcData, t1::DecompressBlockExec* block,
                                          uint16_t stride) {
                                    tilec->postProcessHT(srcData, block, stride);
                                  })
                            : t1::DecompressBlockPostProcessor<int32_t>(
                                  [tilec](int32_t* srcData, t1::DecompressBlockExec* block,
                                          [[maybe_unused]] uint16_t stride) {
                                    tilec->postProcess(srcData, block);
                                  });
            block->bandIndex = bandIndex;
            block->bandNumbps = band->maxBitPlanes_;
            block->bandOrientation = band->orientation_;
            block->cblk = cblk;
            block->cblk_sty = tccp->cblkStyle_;
            block->qmfbid = tccp->qmfbid_;
            block->resno = resno;
            block->roishift = tccp->roishift_;
            block->stepsize = band->stepsize_;
            block->k_msbs = (uint8_t)(band->maxBitPlanes_ - cblk->numbps());
            block->R_b = prec_ + gain_b[band->orientation_];
            block->finalLayer_ = finalLayer;

            auto t = placeholder();
            tileProcessor->blockTasks_.emplace_back(t);
            auto blockFunc = [this, activePool, singleThread, tileProcessor, block, tccp, cbw, cbh,
                              cacheAll] {
              if(success)
              {
                t1::ICoder* coder = nullptr;
                if(block->needsCachedCoder())
                {
                  coder = t1::CoderFactory::makeCoder(tileProcessor->getTCP()->isHT(), false, cbw,
                                                      cbh, tileProcessor->getTileCacheStrategy());
                }
                else if(!cacheAll)
                {
                  auto threadnum = singleThread ? 0 : ExecSingleton::get().this_worker_id();
                  coder =
                      activePool->getCoder((size_t)threadnum, tccp->cblkw_expn_, tccp->cblkh_expn_)
                          .get();
                }
                try
                {
                  success = block->open(coder);
                }
                catch(const std::runtime_error& rerr)
                {
                  grklog.error(rerr.what());
                  success = false;
                }
                delete block;
              }
            };
            if(singleThread)
              blockFunc();
            else
              t.work(blockFunc);
          }
        }
      }
    }

    tilec->currentPacketProgressionState_ = tilec->nextPacketProgressionState_;
    if(!success)
      return false;
  }

  return success;
}

} // namespace grk
