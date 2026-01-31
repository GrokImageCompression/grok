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

#include "grk_includes.h"

namespace grk
{

DecompressScheduler::DecompressScheduler(uint16_t numcomps, uint8_t prec, CoderPool* streamPool)
    : WholeTileScheduler(numcomps), prec_(prec), blocksByTile_(TileBlocks(numcomps)),
      differentialInfo_(new DifferentialInfo[numcomps]), prePostProc_(nullptr),
      streamPool_(streamPool)
{
  for(uint16_t compno = 0; compno < numcomps; ++compno)
    waveletReverse_.push_back(nullptr);
}
DecompressScheduler::~DecompressScheduler()
{
  for(const auto& wav : waveletReverse_)
    delete wav;

  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    for(auto& block : blocksByTile_[compno])
      block.release();
    blocksByTile_[compno].clear();
  }

  delete[] differentialInfo_;
  release();
}

void DecompressScheduler::release(void)
{
  WholeTileScheduler::release();
  delete prePostProc_;
  prePostProc_ = nullptr;
}

bool DecompressScheduler::schedule(TileProcessor* tileProcessor)
{
  auto tcp = tileProcessor->getTCP();
  auto mct = tileProcessor->getMCT();
  auto doPostT1 = tileProcessor->doPostT1();
  FlowComponent* mctPostProc = nullptr;
  // schedule MCT post processing
  if(doPostT1 && tileProcessor->needsMctDecompress())
    mctPostProc = genPrePostProc();
  uint32_t num_threads = (uint32_t)ExecSingleton::num_threads();
  bool singleThread = num_threads == 1;
  bool cacheAll =
      (tileProcessor->getTileCacheStrategy() & GRK_TILE_CACHE_ALL) == GRK_TILE_CACHE_ALL;

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
    auto& componentBlocks = blocksByTile_[compno];
    diffInfo->layersDecompressed_ = tcp->layersToDecompress_;
    bool finalLayer = tcp->layersToDecompress_ == tcp->numLayers_;

    auto resBounds = rChecker.getResBounds(compno);
    uint8_t resno = resBounds.first;
    uint8_t resUpperBound = resBounds.second;
    ResBlocks resBlocks;
    for(; resno < resUpperBound; ++resno)
    {
      auto res = tilec->resolutions_ + resno;
      for(uint8_t bandIndex = 0; bandIndex < res->numBands_; ++bandIndex)
      {
        auto band = res->band + bandIndex;
        auto paddedBandWindow = tilec->getWindow()->getBandWindowPadded(resno, band->orientation_);
        for(auto precinct : band->precincts_)
        {
          if(!wholeTileDecoding && !paddedBandWindow->nonEmptyIntersection(precinct))
            continue;
          for(uint32_t cblkno = 0; cblkno < precinct->getNumCblks(); ++cblkno)
          {
            auto cblkBounds = precinct->getCodeBlockBounds(cblkno);
            if(wholeTileDecoding || paddedBandWindow->nonEmptyIntersection(&cblkBounds))
            {
              auto cblk = precinct->getDecompressedBlock(cblkno);
              auto block = std::make_shared<t1::DecompressBlockExec>(cacheAll);
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
              resBlocks.blocks_.push_back(block);
            }
          }
        }
      }
      // combine first two resolutions together into single resBlock
      if(componentBlocks.size() == 1 && resno == 1 && !resBlocks.blocks_.empty())
      {
        resBlocks.combine(componentBlocks[0]);
        componentBlocks[0] = resBlocks;
        resBlocks.clear();
        continue;
      }
      // combine first two resolutions together into single resBlock
      if(resno == 0 && resUpperBound > 1)
      {
        continue;
      }
      else if(!resBlocks.blocks_.empty())
      {
        componentBlocks.push_back(resBlocks);
        resBlocks.clear();
      }
    }
    if(componentBlocks.empty())
    {
      grklog.warn("No code blocks for component %d", compno);
    }
    else
    {
      // 2. prepare for decompression
      if(imageComponentFlow_[compno])
        delete imageComponentFlow_[compno];
      imageComponentFlow_[compno] =
          new ImageComponentFlow(tilec->nextPacketProgressionState_.numResolutionsRead());
      if(!tileProcessor->getTile()->comps_->isWholeTileDecoding())
        imageComponentFlow_[compno]->setRegionDecompression();

      // 3. decompress
      success = true;
      resno = 0;
    }
    for(auto& rblocks : componentBlocks)
    {
      Resflow* resFlow = nullptr;
      if(!singleThread)
        resFlow = imageComponentFlow_[compno]->resFlows_ + resno;
      for(auto& block : rblocks.blocks_)
      {
        auto blockFunc = [this, activePool, singleThread, tileProcessor, &block, tccp, cbw, cbh,
                          cacheAll, finalLayer] {
          if(!success)
          {
            block.reset();
          }
          else
          {
            block->finalLayer_ = finalLayer;
            t1::ICoder* coder = nullptr;
            if(block->needsCachedCoder())
            {
              // make a new coder for this block
              coder = t1::CoderFactory::makeCoder(tileProcessor->getTCP()->isHT(), false, cbw, cbh,
                                                  tileProcessor->getTileCacheStrategy());
            }
            else if(!cacheAll)
            {
              // get coder from pool
              auto threadnum = singleThread ? 0 : ExecSingleton::get().this_worker_id();
              coder = activePool->getCoder((size_t)threadnum, tccp->cblkw_expn_, tccp->cblkh_expn_)
                          .get();
            }
            try
            {
              if(!block->open(coder))
                success = false;
            }
            catch(const std::runtime_error& rerr)
            {
              block.reset();
              grklog.error(rerr.what());
              success = false;
            }
          }
        };
        if(singleThread)
          blockFunc();
        else
          resFlow->blocks_->nextTask().work(blockFunc);
      }
      if(!singleThread)
        resno++;
    }
    tilec->currentPacketProgressionState_ = tilec->nextPacketProgressionState_;
    if(!success)
      return false;

    auto imageFlow = getImageComponentFlow(compno);
    if(imageFlow)
    {
      // compose blocks and wavelet
      imageFlow->addTo(*this);
      // generate dependency graph
      graph(compno);
    }
    uint8_t numRes = tilec->nextPacketProgressionState_.numResolutionsRead();
    if(numRes > 0)
    {
      if(waveletReverse_[compno])
        delete waveletReverse_[compno];
      waveletReverse_[compno] =
          new WaveletReverse(tileProcessor, tilec, compno, tilec->getWindow()->unreducedBounds(),
                             numRes, (tcp->tccps_ + compno)->qmfbid_);

      if(!waveletReverse_[compno]->decompress())
        return false;
    }

    // post processing
    auto imageComponentFlow = getImageComponentFlow(compno);
    if(imageComponentFlow)
    {
      if(mctPostProc && compno < 3)
      {
        // link to MCT
        imageComponentFlow->getFinalFlowT1()->precede(mctPostProc);
      }
      else if(doPostT1)
      {
        // use with either custom MCT, or no MCT
        if(!tileProcessor->needsMctDecompress(compno) || tcp->mct_ == 2)
        {
          auto dcPostProc = imageComponentFlow->getPrePostProc(*this);
          imageComponentFlow->getFinalFlowT1()->precede(dcPostProc);
          if((tcp->tccps_ + compno)->qmfbid_ == 1)
            mct->schedule_decompress_dc_shift_rev(dcPostProc, compno);
          else
            mct->schedule_decompress_dc_shift_irrev(dcPostProc, compno);
        }
      }
    }
  }

  // sanity check on MCT scheduling
  if(doPostT1 && numcomps_ >= 3 && mctPostProc)
  {
    // custom MCT
    if(tcp->mct_ == 2)
    {
      /*
      auto data = new uint8_t*[tile->numcomps_];
      for(uint16_t i = 0; i < tile->numcomps_; ++i)
      {
        auto tile_comp = tile->comps + i;
        data[i] = (uint8_t*)tile_comp->getWindow()->getResWindowBufferHighestSimple().buf_;
      }
      uint64_t samples = tile->comps->getWindow()->stridedArea();
      bool rc = Mct::decompress_custom((uint8_t*)tcp_->mct_decoding_matrix_, samples, data,
                                      tile->numcomps_, headerImage->comps->sgnd);
      return rc;
      */
      return false;
    }
    else
    {
      if(tcp->tccps_->qmfbid_ == 1)
        mct->schedule_decompress_rev(mctPostProc);
      else
        mct->schedule_decompress_irrev(mctPostProc);
    }
  }

  return true;
}

FlowComponent* DecompressScheduler::genPrePostProc(void)
{
  delete prePostProc_;
  prePostProc_ = new FlowComponent();
  prePostProc_->addTo(*this);

  return prePostProc_;
}

} // namespace grk
