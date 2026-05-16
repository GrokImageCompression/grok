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

#include "geometry.h"
#include "ISparseCanvas.h"
#include "grk_restrict.h"
#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
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
struct ITileProcessor;
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
#include "canvas/tile/Tile.h"
#include "mct.h"
#include "ITileProcessor.h"
#include "CoderFactory.h"
#include "WaveletReverse.h"
#include "WaveletPoolData.h"
#include "TileBlocks.h"
#include "SchedulerStandard.h"
#include "ImageComponentFlow.h"
#ifdef GRK_USE_SCX_SCHEDULING
#include "scx_scheduling_ffi.h"
#include "ScxEngineSingleton.h"
#include "StripPartitioner.h"
#include "StripBuffer.h"
#include <unordered_map>
#endif
#include "SchedulerFreebyrd.h"

namespace grk
{

// Minimal concrete SchedulerStandard subclass for providing ImageComponentFlow to WaveletReverse
class DwtFlowHelper : public SchedulerStandard
{
public:
  explicit DwtFlowHelper(uint16_t numComps) : SchedulerStandard(numComps) {}
  bool scheduleT1([[maybe_unused]] ITileProcessor* proc) override
  {
    return true;
  }
  void release(void) override
  {
    SchedulerStandard::release();
  }

  void setupComponentFlow(uint16_t compno, uint8_t numRes, bool regionDecompress)
  {
    if(compno >= numcomps_)
      return;
    if(imageComponentFlow_[compno])
      delete imageComponentFlow_[compno];
    imageComponentFlow_[compno] = new ImageComponentFlow(numRes);
    if(regionDecompress)
      imageComponentFlow_[compno]->setRegionDecompression();
    imageComponentFlow_[compno]->addTo(*this);
    SchedulerStandard::graph(compno);
  }
};

SchedulerFreebyrd::SchedulerFreebyrd(uint16_t numcomps, uint8_t prec, CoderPool* streamPool)
    : numcomps_(numcomps), prec_(prec), success_(true), streamPool_(streamPool),
      waveletPoolData_(new WaveletPoolData()), dwtHelper_(new DwtFlowHelper(numcomps))
{}

SchedulerFreebyrd::~SchedulerFreebyrd()
{
  release();
  delete waveletPoolData_;
  delete dwtHelper_;
}

void SchedulerFreebyrd::release()
{
  blocksByComp_.clear();
}

bool SchedulerFreebyrd::decompressTile(ITileProcessor* tileProcessor)
{
  success_ = true;

#ifdef GRK_USE_SCX_SCHEDULING
  // Unified T1+DWT via ScxEngine: all work submitted as domain sequences
  if(!decodeAndTransformScx(tileProcessor))
    return false;
#else
  // Interleaved T1+DWT: per-component DAG with T1 and DWT tasks (Phase 4)
  if(!decodeAndTransform(tileProcessor))
    return false;
#endif

  if(!postProcess(tileProcessor))
    return false;

  return success_;
}

bool SchedulerFreebyrd::decodeBlocks(ITileProcessor* tileProcessor)
{
  auto tcp = tileProcessor->getTCP();
  bool cacheAll =
      (tileProcessor->getTileCacheStrategy() & GRK_TILE_CACHE_ALL) == GRK_TILE_CACHE_ALL;
  uint32_t num_threads = (uint32_t)TFSingleton::num_threads();
  bool finalLayer = tcp->layersToDecompress_ == tcp->numLayers_;

  // Collect all blocks across all components
  std::vector<std::shared_ptr<t1::DecompressBlockExec>> allBlocks;

  struct BlockDecodeContext
  {
    CoderPool* pool;
    uint8_t cblkwExpn;
    uint8_t cblkhExpn;
    bool cacheAll;
    bool isHT;
    uint32_t tileCacheStrategy;
    uint16_t cbw;
    uint16_t cbh;
  };
  std::vector<BlockDecodeContext> blockContexts;

  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    if(!tileProcessor->shouldDecodeComponent(compno))
      continue;

    auto tccp = tcp->tccps_ + compno;
    uint16_t cbw = tccp->cblkw_expn_ ? (uint16_t)(1 << tccp->cblkw_expn_) : 0U;
    uint16_t cbh = tccp->cblkh_expn_ ? (uint16_t)(1 << tccp->cblkh_expn_) : 0U;
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
    uint8_t resBegin =
        cacheAll ? (uint8_t)tilec->currentPacketProgressionState_.numResolutionsRead() : 0;
    uint8_t resUpperBound = tilec->nextPacketProgressionState_.numResolutionsRead();

    for(uint8_t resno = resBegin; resno < resUpperBound; ++resno)
    {
      auto res = tilec->resolutions_ + resno;
      for(uint8_t bandIndex = 0; bandIndex < res->numBands_; ++bandIndex)
      {
        auto band = res->band + bandIndex;
        auto paddedBandWindow = tilec->getBandWindowPadded(resno, band->orientation_);
        for(auto precinct : band->precincts_)
        {
          if(!wholeTileDecoding && !paddedBandWindow->nonEmptyIntersection(precinct))
            continue;
          for(uint32_t cblkno = 0; cblkno < precinct->getNumCblks(); ++cblkno)
          {
            auto cblkBounds = precinct->getCodeBlockBounds(cblkno);
            if(!wholeTileDecoding && !paddedBandWindow->nonEmptyIntersection(&cblkBounds))
              continue;

            auto cblk = precinct->getDecompressBlock(cblkno);
            auto block = std::make_shared<t1::DecompressBlockExec>(cacheAll);
            block->x = cblk->x0();
            block->y = cblk->y0();
            block->postProcessor_ =
                tcp->isHT()
                    ? t1::DecompressBlockPostProcessor<int32_t>(
                          [tilec](int32_t* srcData, t1::DecompressBlockExec* blk, uint16_t stride) {
                            tilec->postProcessBlockHT(srcData, blk, stride);
                          })
                    : t1::DecompressBlockPostProcessor<int32_t>(
                          [tilec](int32_t* srcData, t1::DecompressBlockExec* blk,
                                  [[maybe_unused]] uint16_t stride) {
                            tilec->postProcessBlock(srcData, blk);
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

            allBlocks.push_back(block);
            blockContexts.push_back({activePool, tccp->cblkw_expn_, tccp->cblkh_expn_, cacheAll,
                                     tcp->isHT(), tileProcessor->getTileCacheStrategy(), cbw, cbh});
          }
        }
      }
    }
    tilec->currentPacketProgressionState_ = tilec->nextPacketProgressionState_;
  }

  if(allBlocks.empty())
    return true;

#ifdef GRK_USE_SCX_SCHEDULING
  // Decode all blocks in parallel using ScxEngine persistent thread pool.
  // Per-thread coder maps avoid races with the shared CoderPool.
  struct ParallelDecodeCtx
  {
    SchedulerFreebyrd* self;
    std::vector<std::shared_ptr<t1::DecompressBlockExec>>* blocks;
    std::vector<BlockDecodeContext>* contexts;
    std::vector<std::unordered_map<uint32_t, std::shared_ptr<t1::ICoder>>> threadCoders;

    static uint32_t coderKey(uint8_t cblkwExpn, uint8_t cblkhExpn)
    {
      return ((uint32_t)cblkwExpn << 16) | (uint32_t)cblkhExpn;
    }

    static void decode(size_t i, size_t thread_id, void* ud)
    {
      auto* c = static_cast<ParallelDecodeCtx*>(ud);
      if(!c->self->success_)
        return;
      try
      {
        auto& block = (*c->blocks)[i];
        auto& bctx = (*c->contexts)[i];
        t1::ICoder* coder = nullptr;
        if(block->needsCachedCoder())
        {
          coder = t1::CoderFactory::makeCoder(bctx.isHT, false, bctx.cbw, bctx.cbh,
                                              bctx.tileCacheStrategy);
        }
        else if(!bctx.cacheAll)
        {
          auto key = coderKey(bctx.cblkwExpn, bctx.cblkhExpn);
          auto& coderMap = c->threadCoders[thread_id];
          auto it = coderMap.find(key);
          if(it == coderMap.end())
          {
            auto newCoder = std::shared_ptr<t1::ICoder>(t1::CoderFactory::makeCoder(
                bctx.isHT, false, bctx.cbw, bctx.cbh, bctx.tileCacheStrategy));
            it = coderMap.emplace(key, std::move(newCoder)).first;
          }
          coder = it->second.get();
        }
        if(!block->open(coder))
          c->self->success_ = false;
      }
      catch(...)
      {
        c->self->success_ = false;
      }
    }
  };

  auto* engine = ScxEngineSingleton::get();
  // Use a thread-local domain ID — create once, reuse across tiles
  static uint32_t t1DomainId = scx_engine_create_domain(engine, "T1");

  ParallelDecodeCtx ctx{this, &allBlocks, &blockContexts, {}};
  ctx.threadCoders.resize(num_threads);

  scx_engine_reset_domain(engine, t1DomainId);
  scx_engine_submit_batch(engine, t1DomainId, 0, allBlocks.size(), ParallelDecodeCtx::decode, &ctx);
  scx_engine_run(engine);
#else
  // Decode all blocks in parallel using TaskFlow
  tf::Taskflow taskflow;
  for(size_t i = 0; i < allBlocks.size(); ++i)
  {
    taskflow.emplace([this, i, &allBlocks, &blockContexts, tileProcessor]() {
      if(!success_)
        return;
      auto& block = allBlocks[i];
      auto& ctx = blockContexts[i];
      t1::ICoder* coder = nullptr;
      if(block->needsCachedCoder())
      {
        coder =
            t1::CoderFactory::makeCoder(ctx.isHT, false, ctx.cbw, ctx.cbh, ctx.tileCacheStrategy);
      }
      else if(!ctx.cacheAll)
      {
        auto threadnum = TFSingleton::get().this_worker_id();
        coder = ctx.pool->getCoder((size_t)threadnum, ctx.cblkwExpn, ctx.cblkhExpn).get();
      }
      try
      {
        if(!block->open(coder))
          success_ = false;
      }
      catch(const std::runtime_error& rerr)
      {
        grklog.error(rerr.what());
        success_ = false;
      }
    });
  }
  TFSingleton::get().run(taskflow).wait();
#endif

  return success_;
}

bool SchedulerFreebyrd::runDWT(ITileProcessor* tileProcessor)
{
  auto tcp = tileProcessor->getTCP();

  // Release any previous ImageComponentFlow state and clear the taskflow
  dwtHelper_->release();
  dwtHelper_->clear();

  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    if(!tileProcessor->shouldDecodeComponent(compno))
      continue;

    auto tilec = tileProcessor->getTile()->comps_ + compno;
    uint8_t numRes = tilec->nextPacketProgressionState_.numResolutionsRead();
    if(numRes <= 1)
      continue;

    // Create ImageComponentFlow for this component (required by WaveletReverse)
    dwtHelper_->setupComponentFlow(compno, numRes, !tilec->isWholeTileDecoding());

    auto tccp = tcp->tccps_ + compno;
    auto maxDim = std::max(tileProcessor->getCodingParams()->t_width_,
                           tileProcessor->getCodingParams()->t_height_);

    WaveletReverse wavelet(dwtHelper_, tilec, compno, tilec->windowUnreducedBounds(), numRes,
                           tccp->qmfbid_, maxDim, tcp->wholeTileDecompress_, waveletPoolData_);
    if(!wavelet.decompress())
      return false;

    // WaveletReverse::decompress() only schedules tasks into the flow —
    // we must actually run them.
    TFSingleton::get().run(*dwtHelper_).wait();

    // Clear the taskflow for the next component
    dwtHelper_->release();
    dwtHelper_->clear();
  }

  return true;
}

#ifdef GRK_USE_SCX_SCHEDULING
bool SchedulerFreebyrd::decodeAndTransformScx(ITileProcessor* tileProcessor)
{
  auto tcp = tileProcessor->getTCP();
  bool cacheAll =
      (tileProcessor->getTileCacheStrategy() & GRK_TILE_CACHE_ALL) == GRK_TILE_CACHE_ALL;
  uint32_t num_threads = (uint32_t)TFSingleton::num_threads();
  bool finalLayer = tcp->layersToDecompress_ == tcp->numLayers_;

  auto* engine = ScxEngineSingleton::get();

  // === Phase 1: Collect T1 block work and DWT stripe work per component ===

  struct BlockDecodeContext
  {
    CoderPool* pool;
    uint8_t cblkwExpn;
    uint8_t cblkhExpn;
    bool cacheAll;
    bool isHT;
    uint32_t tileCacheStrategy;
    uint16_t cbw;
    uint16_t cbh;
  };

  // Per-component work collection
  struct ComponentWork
  {
    uint32_t t1DomainId;
    uint32_t dwtDomainId;
    // T1 blocks
    std::vector<std::shared_ptr<t1::DecompressBlockExec>> t1Blocks;
    std::vector<BlockDecodeContext> t1Contexts;
    std::vector<std::unordered_map<uint32_t, std::shared_ptr<t1::ICoder>>> threadCoders;
    // Per T1 block: gates to signal on completion (multiple for strip overlap)
    std::vector<std::vector<uint32_t>> t1BlockGateIds;
    // Per T1 block: sub-band relative Y position and height (for strip assignment)
    struct BlockBandInfo
    {
      uint8_t orientation; // BAND_ORIENT_LL/HL/LH/HH
      uint32_t relY0; // sub-band relative Y
      uint32_t height; // block height in rows
    };
    std::vector<BlockBandInfo> t1BlockBandInfo;
    // DWT state
    uint8_t numRes;
    uint8_t qmfbid;
    bool needsDwt;
  };

  std::vector<ComponentWork> compWork(numcomps_);

  // Create/reuse domains: 2 per component (T1 + DWT)
  static std::vector<uint32_t> t1DomainIds;
  static std::vector<uint32_t> dwtDomainIds;
  if(t1DomainIds.size() < numcomps_)
  {
    for(size_t i = t1DomainIds.size(); i < numcomps_; ++i)
    {
      char name[32];
      snprintf(name, sizeof(name), "t1_comp%zu", i);
      t1DomainIds.push_back(scx_engine_create_domain(engine, name));
      snprintf(name, sizeof(name), "dwt_comp%zu", i);
      dwtDomainIds.push_back(scx_engine_create_domain(engine, name));
    }
  }

  // Reset domains for this tile
  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    scx_engine_reset_domain(engine, t1DomainIds[compno]);
    scx_engine_reset_domain(engine, dwtDomainIds[compno]);
  }

  // Collect work per component
  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    auto& cw = compWork[compno];
    cw.t1DomainId = t1DomainIds[compno];
    cw.dwtDomainId = dwtDomainIds[compno];
    cw.needsDwt = false;

    if(!tileProcessor->shouldDecodeComponent(compno))
      continue;

    auto tccp = tcp->tccps_ + compno;
    uint16_t cbw = tccp->cblkw_expn_ ? (uint16_t)(1 << tccp->cblkw_expn_) : 0U;
    uint16_t cbh = tccp->cblkh_expn_ ? (uint16_t)(1 << tccp->cblkh_expn_) : 0U;
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
    uint8_t resBegin =
        cacheAll ? (uint8_t)tilec->currentPacketProgressionState_.numResolutionsRead() : 0;
    uint8_t resUpperBound = tilec->nextPacketProgressionState_.numResolutionsRead();
    cw.numRes = tilec->nextPacketProgressionState_.numResolutionsRead();
    cw.qmfbid = tccp->qmfbid_;
    cw.needsDwt = (cw.numRes > 1) && wholeTileDecoding;

    // Collect T1 blocks (gate assignment happens later after gates are created)
    for(uint8_t resno = resBegin; resno < resUpperBound; ++resno)
    {
      auto res = tilec->resolutions_ + resno;
      for(uint8_t bandIndex = 0; bandIndex < res->numBands_; ++bandIndex)
      {
        auto band = res->band + bandIndex;
        auto paddedBandWindow = tilec->getBandWindowPadded(resno, band->orientation_);
        for(auto precinct : band->precincts_)
        {
          if(!wholeTileDecoding && !paddedBandWindow->nonEmptyIntersection(precinct))
            continue;
          for(uint32_t cblkno = 0; cblkno < precinct->getNumCblks(); ++cblkno)
          {
            auto cblkBounds = precinct->getCodeBlockBounds(cblkno);
            if(!wholeTileDecoding && !paddedBandWindow->nonEmptyIntersection(&cblkBounds))
              continue;

            auto cblk = precinct->getDecompressBlock(cblkno);
            auto block = std::make_shared<t1::DecompressBlockExec>(cacheAll);
            block->x = cblk->x0();
            block->y = cblk->y0();
            block->postProcessor_ =
                tcp->isHT()
                    ? t1::DecompressBlockPostProcessor<int32_t>(
                          [tilec](int32_t* srcData, t1::DecompressBlockExec* blk, uint16_t stride) {
                            tilec->postProcessBlockHT(srcData, blk, stride);
                          })
                    : t1::DecompressBlockPostProcessor<int32_t>(
                          [tilec](int32_t* srcData, t1::DecompressBlockExec* blk,
                                  [[maybe_unused]] uint16_t stride) {
                            tilec->postProcessBlock(srcData, blk);
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

            cw.t1Blocks.push_back(block);
            cw.t1Contexts.push_back({activePool, tccp->cblkw_expn_, tccp->cblkh_expn_, cacheAll,
                                     tcp->isHT(), tileProcessor->getTileCacheStrategy(), cbw, cbh});
            // Gate assignment placeholder — filled in after gates are created
            cw.t1BlockGateIds.push_back({});
            // Store band-relative Y range for strip-aware gate assignment
            ComponentWork::BlockBandInfo binfo;
            binfo.orientation = (uint8_t)band->orientation_;
            binfo.relY0 = (uint32_t)(cblkBounds.y0() - band->y0);
            binfo.height = (uint32_t)(cblkBounds.y1() - cblkBounds.y0());
            cw.t1BlockBandInfo.push_back(binfo);
          }
        }
      }
    }
    tilec->currentPacketProgressionState_ = tilec->nextPacketProgressionState_;

    // Prepare per-thread coder maps
    cw.threadCoders.resize(num_threads);
  }

  // === Phase 2: Create per-strip gates and assign T1 blocks ===
  // Strip-aware gating with cross-resolution pipelining:
  //   - Each strip at each resolution has its own gate
  //   - T1 blocks signal only the strip gates they overlap
  //   - Strips at res R-1 directly signal dependent strip gates at res R
  //     (via in-job signaling at the end of FusedStripJob::execute)
  //
  // Gate count for strip S at resolution R:
  //   Res 1: (overlapping T1 blocks from res 0 + res 1)
  //   Res R>1: (overlapping T1 blocks from res R) + (upstream strips at R-1 count)

  struct CompGates
  {
    // Per resolution: vector of per-strip gate IDs
    // stripGateIds[r-1][s] = gate for strip s at resolution r
    std::vector<std::vector<uint32_t>> stripGateIds;
    // Per resolution: strip geometries (needed by Phase 4)
    std::vector<std::vector<StripGeometry>> stripGeoms;
    // Per resolution: per-strip dependent gates at next resolution (for cross-res pipelining)
    // crossResDepGates[r-1][s] = list of strip gates at res r+1 that strip s at res r must signal
    std::vector<std::vector<std::vector<uint32_t>>> crossResDepGates;
  };
  std::vector<CompGates> compGates(numcomps_);

  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    auto& cw = compWork[compno];
    if(!cw.needsDwt)
      continue;

    auto tilec = tileProcessor->getTile()->comps_ + compno;
    // Only use strip-aware gates for whole-tile decode
    if(!tilec->isWholeTileDecoding())
      continue;

    auto& gates = compGates[compno];
    gates.stripGateIds.resize(cw.numRes - 1);
    gates.stripGeoms.resize(cw.numRes - 1);
    gates.crossResDepGates.resize(cw.numRes - 1);

    // Compute strip geometries for each resolution
    auto bandLL = tilec->resolutions_;
    for(uint8_t r = 1; r < cw.numRes; ++r)
    {
      uint32_t sn = bandLL->height();
      ++bandLL;
      uint32_t resHeight = bandLL->height();
      uint32_t resWidth = bandLL->width();
      if(resHeight == 0 || resWidth == 0)
      {
        gates.stripGeoms[r - 1] = {};
        continue;
      }
      uint32_t dn = resHeight - sn;
      uint32_t parity = bandLL->y0 & 1;
      uint32_t halo = (cw.qmfbid == 1) ? 1 : 4; // 5/3 needs 1, 9/7 needs 4
      gates.stripGeoms[r - 1] = StripPartitioner::partition(
          resHeight, sn, dn, parity, computeIdealStripeHeight(resWidth, halo, resHeight), halo);
    }

    // Count cross-resolution dependencies: for each strip at R, count how many
    // strips at R-1 produce output rows that overlap this strip's rangeL
    // (since output of res R-1 = LL input of res R)
    std::vector<std::vector<size_t>> crossResUpstreamCounts(cw.numRes - 1);
    for(uint8_t r = 2; r < cw.numRes; ++r)
    {
      auto& geoms = gates.stripGeoms[r - 1];
      auto& prevGeoms = gates.stripGeoms[r - 2];
      size_t numStrips = geoms.size();
      crossResUpstreamCounts[r - 1].resize(numStrips, 0);

      if(prevGeoms.empty() || numStrips == 0)
        continue;

      for(size_t s = 0; s < numStrips; ++s)
      {
        // Strip S at res R needs LL rows [rangeL.lo, rangeL.hi)
        // These correspond to output rows at res R-1
        uint32_t needLo = geoms[s].rangeL.lo;
        uint32_t needHi = geoms[s].rangeL.hi;

        for(size_t t = 0; t < prevGeoms.size(); ++t)
        {
          uint32_t outLo = prevGeoms[t].outStart;
          uint32_t outHi = prevGeoms[t].outStart + prevGeoms[t].outCount;
          // Check overlap
          if(outHi > needLo && outLo < needHi)
            crossResUpstreamCounts[r - 1][s]++;
        }
      }
    }

    // For each resolution and strip, count overlapping T1 blocks
    for(uint8_t r = 1; r < cw.numRes; ++r)
    {
      auto& geoms = gates.stripGeoms[r - 1];
      size_t numStrips = geoms.size();
      if(numStrips == 0)
        continue;

      // Count overlapping blocks per strip
      std::vector<size_t> stripBlockCounts(numStrips, 0);

      for(size_t bi = 0; bi < cw.t1Blocks.size(); ++bi)
      {
        auto& block = cw.t1Blocks[bi];
        auto& info = cw.t1BlockBandInfo[bi];
        uint8_t blockRes = block->resno;

        // For resolution 1: count blocks from res 0 (LL) and res 1
        // For resolution R>1: count blocks from res R only
        bool contributes = false;
        if(r == 1)
          contributes = (blockRes == 0 || blockRes == 1);
        else
          contributes = (blockRes == r);

        if(!contributes)
          continue;

        bool isLBand =
            (info.orientation == t1::BAND_ORIENT_LL || info.orientation == t1::BAND_ORIENT_HL);

        uint32_t blockRelY0 = info.relY0;
        uint32_t blockRelY1 = info.relY0 + info.height;

        for(size_t s = 0; s < numStrips; ++s)
        {
          auto& range = isLBand ? geoms[s].rangeL : geoms[s].rangeH;
          if(blockRelY1 > range.lo && blockRelY0 < range.hi)
            stripBlockCounts[s]++;
        }
      }

      // Create per-strip gates
      gates.stripGateIds[r - 1].resize(numStrips);
      for(size_t s = 0; s < numStrips; ++s)
      {
        // Gate count: overlapping T1 blocks + upstream strip count (cross-res, for R > 1)
        size_t upstreamCount = (r > 1) ? crossResUpstreamCounts[r - 1][s] : 0;
        size_t gateCount = stripBlockCounts[s] + upstreamCount;
        if(gateCount == 0)
          gateCount = 1; // safety: prevent immediate open for empty strips
        gates.stripGateIds[r - 1][s] = scx_engine_create_gate(engine, gateCount);
      }
    }

    // Build cross-resolution dependency map: for each strip at R-1, list the
    // strip gates at R it must signal upon completion
    for(uint8_t r = 1; r < cw.numRes; ++r)
    {
      auto& geoms = gates.stripGeoms[r - 1];
      gates.crossResDepGates[r - 1].resize(geoms.size());

      if(r >= cw.numRes - 1)
        continue; // last resolution has no downstream

      auto& nextGeoms = gates.stripGeoms[r];
      if(nextGeoms.empty())
        continue;

      for(size_t t = 0; t < geoms.size(); ++t)
      {
        uint32_t outLo = geoms[t].outStart;
        uint32_t outHi = geoms[t].outStart + geoms[t].outCount;

        for(size_t s = 0; s < nextGeoms.size(); ++s)
        {
          // Strip S at res R+1 needs LL rows [rangeL.lo, rangeL.hi)
          uint32_t needLo = nextGeoms[s].rangeL.lo;
          uint32_t needHi = nextGeoms[s].rangeL.hi;
          if(outHi > needLo && outLo < needHi)
            gates.crossResDepGates[r - 1][t].push_back(gates.stripGateIds[r][s]);
        }
      }
    }

    // Assign gate IDs to T1 blocks
    for(size_t bi = 0; bi < cw.t1Blocks.size(); ++bi)
      cw.t1BlockGateIds[bi].clear();

    for(size_t bi = 0; bi < cw.t1Blocks.size(); ++bi)
    {
      auto& block = cw.t1Blocks[bi];
      auto& info = cw.t1BlockBandInfo[bi];
      uint8_t blockRes = block->resno;

      for(uint8_t r = 1; r < cw.numRes; ++r)
      {
        auto& geoms = gates.stripGeoms[r - 1];
        if(geoms.empty())
          continue;

        bool contributes = false;
        if(r == 1)
          contributes = (blockRes == 0 || blockRes == 1);
        else
          contributes = (blockRes == r);

        if(!contributes)
          continue;

        bool isLBand =
            (info.orientation == t1::BAND_ORIENT_LL || info.orientation == t1::BAND_ORIENT_HL);

        uint32_t blockRelY0 = info.relY0;
        uint32_t blockRelY1 = info.relY0 + info.height;

        for(size_t s = 0; s < geoms.size(); ++s)
        {
          auto& range = isLBand ? geoms[s].rangeL : geoms[s].rangeH;
          if(blockRelY1 > range.lo && blockRelY0 < range.hi)
            cw.t1BlockGateIds[bi].push_back(gates.stripGateIds[r - 1][s]);
        }
      }
    }
  }

  // === Phase 3: Submit T1 work (T1 domain, seq 0) ===

  struct T1DecodeCtx
  {
    SchedulerFreebyrd* self;
    ComponentWork* cw;
    ScxEngine* engine;

    static uint32_t coderKey(uint8_t cblkwExpn, uint8_t cblkhExpn)
    {
      return ((uint32_t)cblkwExpn << 16) | (uint32_t)cblkhExpn;
    }

    static void decode(size_t i, size_t thread_id, void* ud)
    {
      auto* ctx = static_cast<T1DecodeCtx*>(ud);
      if(!ctx->self->success_)
        return;
      try
      {
        auto& block = ctx->cw->t1Blocks[i];
        auto& bctx = ctx->cw->t1Contexts[i];
        t1::ICoder* coder = nullptr;
        if(block->needsCachedCoder())
        {
          coder = t1::CoderFactory::makeCoder(bctx.isHT, false, bctx.cbw, bctx.cbh,
                                              bctx.tileCacheStrategy);
        }
        else if(!bctx.cacheAll)
        {
          auto key = coderKey(bctx.cblkwExpn, bctx.cblkhExpn);
          auto& coderMap = ctx->cw->threadCoders[thread_id];
          auto it = coderMap.find(key);
          if(it == coderMap.end())
          {
            auto newCoder = std::shared_ptr<t1::ICoder>(t1::CoderFactory::makeCoder(
                bctx.isHT, false, bctx.cbw, bctx.cbh, bctx.tileCacheStrategy));
            it = coderMap.emplace(key, std::move(newCoder)).first;
          }
          coder = it->second.get();
        }
        if(!block->open(coder))
          ctx->self->success_ = false;
      }
      catch(...)
      {
        ctx->self->success_ = false;
      }
      // Signal all strip gates this block overlaps
      for(auto gateId : ctx->cw->t1BlockGateIds[i])
        scx_engine_signal_gate(ctx->engine, gateId);
    }
  };

  // Submit T1 batches to T1 domain
  std::vector<T1DecodeCtx> t1Contexts(numcomps_);
  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    auto& cw = compWork[compno];
    if(cw.t1Blocks.empty())
      continue;
    t1Contexts[compno] = {this, &cw, engine};
    scx_engine_submit_batch(engine, cw.t1DomainId, 0, cw.t1Blocks.size(), T1DecodeCtx::decode,
                            &t1Contexts[compno]);
  }

  // === Phase 4: Submit fused strip DWT work (DWT domain, seq 0, per-strip gates) ===
  //
  // Multi-component parallelism is inherent: each component has its own DWT domain
  // with independent gate chains. The ScxEngine's work-stealing scheduler naturally
  // interleaves DWT strips from all components — no explicit coordination needed.
  // Per-thread scratch memory (waveletPoolData_ for 5/3 H-DWT, per-component threadBufL/H
  // for intermediates) is safe because each thread executes only one job at a time.
  //
  // Each strip independently computes H-DWT for its rows (including halo),
  // then cascade V-DWT producing only the strip's output rows.
  // Per-strip gating allows each strip to start as soon as its T1 blocks decode.
  // Cross-resolution pipelining: each strip signals downstream strip gates directly.

  // Shared state per resolution (per-thread intermediate buffers)
  struct FusedStripShared
  {
    WaveletReverse* wavelet = nullptr;
    uint32_t intermediateStride = 0;
    uint32_t maxLRows = 0;
    uint32_t maxHRows = 0;
    std::vector<std::unique_ptr<int32_t[]>> threadBufL;
    std::vector<std::unique_ptr<int32_t[]>> threadBufH;
  };

  // Per-strip job context
  struct FusedStripJob
  {
    FusedStripShared* shared;
    ScxEngine* engine;
    StripGeometry geom;
    Buffer2dSimple<int32_t> llBand, hlBand, lhBand, hhBand;
    Buffer2dSimple<int32_t> winDest;
    uint32_t hSn, hDn, hParity;
    uint32_t resWidth;
    DcShiftParam dcShift;
    // Cross-resolution: gates to signal at next resolution when this strip completes
    std::vector<uint32_t> dependentGates;

    static void execute([[maybe_unused]] size_t i, size_t thread_id, void* ud)
    {
      auto* job = static_cast<FusedStripJob*>(ud);
      auto* wav = job->shared->wavelet;

      const uint32_t stride = job->shared->intermediateStride;
      int32_t* tempL = job->shared->threadBufL[thread_id].get();
      int32_t* tempH = job->shared->threadBufH[thread_id].get();

      // === Step 1: H-DWT for L rows (LL + HL → tempL) ===
      {
        wav->horizPool_[thread_id].sn = job->hSn;
        wav->horizPool_[thread_id].dn = job->hDn;
        wav->horizPool_[thread_id].parity = job->hParity;

        auto winL = job->llBand;
        auto winH = job->hlBand;
        winL.incY_IN_PLACE(job->geom.rangeL.lo);
        winH.incY_IN_PLACE(job->geom.rangeL.lo);
        Buffer2dSimple<int32_t> dest(tempL, stride, job->geom.rangeL.count());

        wav->h_strip_53(&wav->horizPool_[thread_id], 0, job->geom.rangeL.count(), winL, winH, dest);
      }

      // === Step 2: H-DWT for H rows (LH + HH → tempH) ===
      {
        auto winL = job->lhBand;
        auto winH = job->hhBand;
        winL.incY_IN_PLACE(job->geom.rangeH.lo);
        winH.incY_IN_PLACE(job->geom.rangeH.lo);
        Buffer2dSimple<int32_t> dest(tempH, stride, job->geom.rangeH.count());

        wav->h_strip_53(&wav->horizPool_[thread_id], 0, job->geom.rangeH.count(), winL, winH, dest);
      }

      // === Step 3: Cascade V-DWT using tempL, tempH → output ===
      {
        uint32_t localSn = job->geom.rangeL.count();
        uint32_t localDn = job->geom.rangeH.count();

        wav->vertPool_[thread_id].sn = localSn;
        wav->vertPool_[thread_id].dn = localDn;
        wav->vertPool_[thread_id].parity = job->geom.localParity;

        Buffer2dSimple<int32_t> winL(tempL, stride, localSn);
        Buffer2dSimple<int32_t> winH(tempH, stride, localDn);

        wav->v_cascade_strip_53(&wav->vertPool_[thread_id], 0, job->resWidth, winL, winH,
                                job->winDest, job->dcShift, job->geom.outputStartInStripe,
                                job->geom.outCount);
      }

      // === Step 4: Signal downstream strip gates (cross-resolution pipelining) ===
      for(auto gateId : job->dependentGates)
        scx_engine_signal_gate(job->engine, gateId);
    }
  };

  // Shared state per resolution for 9/7 (float-based cascade strip DWT)
  struct FusedStripShared97
  {
    WaveletReverse* wavelet = nullptr;
    uint32_t resWidth = 0;
    size_t hScratchLen = 0; // dataLength for allocCascadeScratch97
  };

  // Per-strip job context for 9/7
  struct FusedStripJob97
  {
    FusedStripShared97* shared;
    ScxEngine* engine;
    StripGeometry geom;
    Buffer2dSimple<float> llBand, hlBand, lhBand, hhBand;
    Buffer2dSimple<float> winDest;
    uint32_t hSn, hDn, hParity;
    uint32_t resWidth;
    DcShiftParam dcShift;
    std::vector<uint32_t> dependentGates;

    static void execute([[maybe_unused]] size_t i, [[maybe_unused]] size_t thread_id, void* ud)
    {
      auto* job = static_cast<FusedStripJob97*>(ud);
      auto* wav = job->shared->wavelet;

      uint32_t localSn = job->geom.rangeL.count();
      uint32_t localDn = job->geom.rangeH.count();

      // Allocate H-DWT scratch
      dwt_scratch<vec4f> hScratch;
      hScratch.sn = job->hSn;
      hScratch.dn = job->hDn;
      hScratch.parity = job->hParity;
      hScratch.win_l = Line32(0, hScratch.sn);
      hScratch.win_h = Line32(0, hScratch.dn);
      if(!WaveletReverse::allocCascadeScratch97(hScratch, job->shared->hScratchLen))
        return;

      // Set up V-DWT scratch with strip geometry
      dwt_scratch<vec4f> vScratch;
      vScratch.sn = localSn;
      vScratch.dn = localDn;
      vScratch.parity = job->geom.localParity;
      vScratch.win_l = Line32(0, localSn);
      vScratch.win_h = Line32(0, localDn);
      vScratch.outputStart = job->geom.outputStartInStripe;
      vScratch.outputCount = job->geom.outCount;
      if(!WaveletReverse::allocCascadeScratch97(vScratch, (size_t)(localSn + localDn)))
        return;

      // Offset band views to strip's sub-band range
      auto winLL = job->llBand;
      auto winHL = job->hlBand;
      auto winLH = job->lhBand;
      auto winHH = job->hhBand;
      winLL.incY_IN_PLACE(job->geom.rangeL.lo);
      winHL.incY_IN_PLACE(job->geom.rangeL.lo);
      winLH.incY_IN_PLACE(job->geom.rangeH.lo);
      winHH.incY_IN_PLACE(job->geom.rangeH.lo);

      // cascade_strip_97 handles H-DWT for L rows, H-DWT for H rows,
      // then V-DWT with partial output [outputStart, outputStart+outputCount)
      wav->cascade_strip_97(&hScratch, &vScratch, job->resWidth, winLL, winHL, winLH, winHH,
                            job->winDest, job->dcShift);

      // Signal downstream strip gates (cross-resolution pipelining)
      for(auto gateId : job->dependentGates)
        scx_engine_signal_gate(job->engine, gateId);
    }
  };

  // Shared state per resolution for 16-bit (per-thread intermediate buffers)
  struct FusedStripShared16
  {
    WaveletReverse* wavelet = nullptr;
    uint32_t intermediateStride = 0;
    uint32_t maxLRows = 0;
    uint32_t maxHRows = 0;
    std::vector<std::unique_ptr<int16_t[]>> threadBufL;
    std::vector<std::unique_ptr<int16_t[]>> threadBufH;
  };

  // Per-strip job context for 16-bit (both 5/3 and 9/7)
  struct FusedStripJob16
  {
    FusedStripShared16* shared;
    ScxEngine* engine;
    StripGeometry geom;
    Buffer2dSimple<int16_t> llBand, hlBand, lhBand, hhBand;
    Buffer2dSimple<int16_t> winDest;
    uint32_t hSn, hDn, hParity;
    uint32_t resWidth;
    uint8_t qmfbid;
    DcShiftParam dcShift;
    std::vector<uint32_t> dependentGates;

    static void execute([[maybe_unused]] size_t i, size_t thread_id, void* ud)
    {
      auto* job = static_cast<FusedStripJob16*>(ud);
      auto* wav = job->shared->wavelet;

      const uint32_t stride = job->shared->intermediateStride;
      int16_t* tempL = job->shared->threadBufL[thread_id].get();
      int16_t* tempH = job->shared->threadBufH[thread_id].get();

      // === Step 1: H-DWT for L rows ===
      {
        wav->horizPool16_[thread_id].sn = job->hSn;
        wav->horizPool16_[thread_id].dn = job->hDn;
        wav->horizPool16_[thread_id].parity = job->hParity;

        auto winL = job->llBand;
        auto winH = job->hlBand;
        winL.incY_IN_PLACE(job->geom.rangeL.lo);
        winH.incY_IN_PLACE(job->geom.rangeL.lo);
        Buffer2dSimple<int16_t> dest(tempL, stride, job->geom.rangeL.count());

        if(job->qmfbid == 1)
          wav->h_strip_16_53(&wav->horizPool16_[thread_id], 0, job->geom.rangeL.count(), winL, winH,
                             dest);
        else
          wav->h_strip_16_97(&wav->horizPool16_[thread_id], 0, job->geom.rangeL.count(), winL, winH,
                             dest);
      }

      // === Step 2: H-DWT for H rows ===
      {
        auto winL = job->lhBand;
        auto winH = job->hhBand;
        winL.incY_IN_PLACE(job->geom.rangeH.lo);
        winH.incY_IN_PLACE(job->geom.rangeH.lo);
        Buffer2dSimple<int16_t> dest(tempH, stride, job->geom.rangeH.count());

        if(job->qmfbid == 1)
          wav->h_strip_16_53(&wav->horizPool16_[thread_id], 0, job->geom.rangeH.count(), winL, winH,
                             dest);
        else
          wav->h_strip_16_97(&wav->horizPool16_[thread_id], 0, job->geom.rangeH.count(), winL, winH,
                             dest);
      }

      // === Step 3: Cascade V-DWT ===
      {
        uint32_t localSn = job->geom.rangeL.count();
        uint32_t localDn = job->geom.rangeH.count();

        wav->vertPool16_[thread_id].sn = localSn;
        wav->vertPool16_[thread_id].dn = localDn;
        wav->vertPool16_[thread_id].parity = job->geom.localParity;

        Buffer2dSimple<int16_t> winL(tempL, stride, localSn);
        Buffer2dSimple<int16_t> winH(tempH, stride, localDn);

        if(job->qmfbid == 1)
          wav->v_cascade_strip_16_53(&wav->vertPool16_[thread_id], 0, job->resWidth, winL, winH,
                                     job->winDest, job->dcShift, job->geom.outputStartInStripe,
                                     job->geom.outCount);
        else
          wav->v_cascade_strip_16_97(&wav->vertPool16_[thread_id], 0, job->resWidth, winL, winH,
                                     job->winDest, job->dcShift, job->geom.outputStartInStripe,
                                     job->geom.outCount);
      }

      // === Step 4: Signal downstream strip gates ===
      for(auto gateId : job->dependentGates)
        scx_engine_signal_gate(job->engine, gateId);
    }
  };

  struct CompDwtState
  {
    std::unique_ptr<WaveletReverse> wavelet;
    std::vector<FusedStripShared> shared; // one per resolution (5/3 32-bit)
    std::vector<FusedStripJob> jobs; // all strip jobs (5/3 32-bit)
    std::vector<FusedStripShared97> shared97; // one per resolution (9/7 float)
    std::vector<FusedStripJob97> jobs97; // all strip jobs (9/7 float)
    std::vector<FusedStripShared16> shared16; // one per resolution (16-bit)
    std::vector<FusedStripJob16> jobs16; // all strip jobs (16-bit)
    bool usedScxDwt = false;
  };
  std::vector<CompDwtState> dwtStates(numcomps_);

  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    auto& cw = compWork[compno];
    if(!cw.needsDwt)
      continue;

    auto tilec = tileProcessor->getTile()->comps_ + compno;
    auto maxDim = std::max(tileProcessor->getCodingParams()->t_width_,
                           tileProcessor->getCodingParams()->t_height_);

    // Don't use ScxEngine DWT for partial decompress (handled in fallback)
    if(!tilec->isWholeTileDecoding())
      continue;

    auto& ds = dwtStates[compno];
    ds.usedScxDwt = true;

    ds.wavelet =
        std::make_unique<WaveletReverse>(nullptr, tilec, compno, tilec->windowUnreducedBounds(),
                                         cw.numRes, cw.qmfbid, maxDim, true, waveletPoolData_);

    auto* wav = ds.wavelet.get();
    if(waveletPoolData_)
      waveletPoolData_->alloc(maxDim);

    auto& gates = compGates[compno];

    auto bandLL = tilec->resolutions_;

    if(tilec->is16BitDwt())
    {
      // === 16-bit DWT path (both 5/3 and 9/7) ===
      auto tileBuffer16 = tilec->getWindow16();
      wav->horizPool16_ = std::make_unique<dwt_scratch<int16_t>[]>(num_threads);
      wav->vertPool16_ = std::make_unique<dwt_scratch<int16_t>[]>(num_threads);

      ds.shared16.resize(cw.numRes - 1);

      for(uint8_t res = 1; res < cw.numRes; ++res)
      {
        wav->horiz_.sn = bandLL->width();
        wav->vert_.sn = bandLL->height();
        for(uint32_t i = 0; i < num_threads; ++i)
        {
          wav->horizPool16_[i].sn = bandLL->width();
          wav->vertPool16_[i].sn = bandLL->height();
        }
        ++bandLL;
        auto resWidth = bandLL->width();
        auto resHeight = bandLL->height();
        if(resWidth == 0 || resHeight == 0)
          continue;
        wav->horiz_.dn = resWidth - wav->horiz_.sn;
        wav->horiz_.parity = bandLL->x0 & 1;
        wav->vert_.dn = resHeight - wav->vert_.sn;
        wav->vert_.parity = bandLL->y0 & 1;
        for(uint32_t i = 0; i < num_threads; ++i)
        {
          wav->horizPool16_[i].dn = resWidth - wav->horizPool16_[i].sn;
          wav->horizPool16_[i].parity = bandLL->x0 & 1;
          wav->horizPool16_[i].allocatedMem = (int16_t*)waveletPoolData_->getHoriz(i);
          wav->horizPool16_[i].mem = (int16_t*)waveletPoolData_->getHoriz(i);

          wav->vertPool16_[i].dn = resHeight - wav->vertPool16_[i].sn;
          wav->vertPool16_[i].parity = bandLL->y0 & 1;
          wav->vertPool16_[i].allocatedMem = (int16_t*)waveletPoolData_->getVert(i);
          wav->vertPool16_[i].mem = (int16_t*)waveletPoolData_->getVert(i);
        }

        auto& stripGeoms = gates.stripGeoms[res - 1];
        if(stripGeoms.empty())
          continue;

        uint32_t intermediateStride = (resWidth + 15U) & ~15U;

        uint32_t maxLRows = 0, maxHRows = 0;
        for(auto& sg : stripGeoms)
        {
          maxLRows = std::max(maxLRows, sg.rangeL.count());
          maxHRows = std::max(maxHRows, sg.rangeH.count());
        }

        auto& sh = ds.shared16[res - 1];
        sh.wavelet = wav;
        sh.intermediateStride = intermediateStride;
        sh.maxLRows = maxLRows;
        sh.maxHRows = maxHRows;
        sh.threadBufL.resize(num_threads);
        sh.threadBufH.resize(num_threads);
        for(uint32_t t = 0; t < num_threads; ++t)
        {
          sh.threadBufL[t] = std::make_unique<int16_t[]>((size_t)intermediateStride * maxLRows);
          sh.threadBufH[t] = std::make_unique<int16_t[]>((size_t)intermediateStride * maxHRows);
        }

        DcShiftParam dcShift = (res == cw.numRes - 1) ? wav->dcShift_ : DcShiftParam{};

        auto llBand = tileBuffer16->getResWindowBufferSimple((uint8_t)(res - 1U));
        auto hlBand = tileBuffer16->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_HL);
        auto lhBand = tileBuffer16->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_LH);
        auto hhBand = tileBuffer16->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_HH);
        auto winDest = tileBuffer16->getResWindowBufferSimple(res);

        size_t jobBase = ds.jobs16.size();

        for(size_t s = 0; s < stripGeoms.size(); ++s)
        {
          FusedStripJob16 job;
          job.shared = &sh;
          job.engine = engine;
          job.geom = stripGeoms[s];
          job.llBand = llBand;
          job.hlBand = hlBand;
          job.lhBand = lhBand;
          job.hhBand = hhBand;
          job.winDest = winDest;
          job.winDest.incY_IN_PLACE(stripGeoms[s].outStart);
          job.hSn = wav->horiz_.sn;
          job.hDn = wav->horiz_.dn;
          job.hParity = wav->horiz_.parity;
          job.resWidth = resWidth;
          job.qmfbid = cw.qmfbid;
          job.dcShift = dcShift;
          job.dependentGates = gates.crossResDepGates[res - 1][s];
          ds.jobs16.push_back(std::move(job));
        }

        for(size_t s = 0; s < stripGeoms.size(); ++s)
        {
          uint32_t stripGate = gates.stripGateIds[res - 1][s];
          scx_engine_submit_full_batch(engine, cw.dwtDomainId, 0, 1, FusedStripJob16::execute,
                                       &ds.jobs16[jobBase + s], stripGate, SCX_NO_GATE);
        }
      }
    }
    else if(cw.qmfbid == 1)
    {
      // === 5/3 reversible DWT path (32-bit) ===
      auto tileBuffer = tilec->getWindow();
      wav->horizPool_ = std::make_unique<dwt_scratch<int32_t>[]>(num_threads);
      wav->vertPool_ = std::make_unique<dwt_scratch<int32_t>[]>(num_threads);

      ds.shared.resize(cw.numRes - 1);

      for(uint8_t res = 1; res < cw.numRes; ++res)
      {
        wav->horiz_.sn = bandLL->width();
        wav->vert_.sn = bandLL->height();
        for(uint32_t i = 0; i < num_threads; ++i)
        {
          wav->horizPool_[i].sn = bandLL->width();
          wav->vertPool_[i].sn = bandLL->height();
        }
        ++bandLL;
        auto resWidth = bandLL->width();
        auto resHeight = bandLL->height();
        if(resWidth == 0 || resHeight == 0)
          continue;
        wav->horiz_.dn = resWidth - wav->horiz_.sn;
        wav->horiz_.parity = bandLL->x0 & 1;
        wav->vert_.dn = resHeight - wav->vert_.sn;
        wav->vert_.parity = bandLL->y0 & 1;
        for(uint32_t i = 0; i < num_threads; ++i)
        {
          wav->horizPool_[i].dn = resWidth - wav->horizPool_[i].sn;
          wav->horizPool_[i].parity = bandLL->x0 & 1;
          wav->horizPool_[i].allocatedMem = (int32_t*)waveletPoolData_->getHoriz(i);
          wav->horizPool_[i].mem = (int32_t*)waveletPoolData_->getHoriz(i);

          wav->vertPool_[i].dn = resHeight - wav->vertPool_[i].sn;
          wav->vertPool_[i].parity = bandLL->y0 & 1;
          wav->vertPool_[i].allocatedMem = (int32_t*)waveletPoolData_->getVert(i);
          wav->vertPool_[i].mem = (int32_t*)waveletPoolData_->getVert(i);
        }

        auto& stripGeoms = gates.stripGeoms[res - 1];
        if(stripGeoms.empty())
          continue;

        // Align intermediate stride for SIMD
        uint32_t intermediateStride = (resWidth + 15U) & ~15U;

        // Find max L/H rows across strips for buffer allocation
        uint32_t maxLRows = 0, maxHRows = 0;
        for(auto& sg : stripGeoms)
        {
          maxLRows = std::max(maxLRows, sg.rangeL.count());
          maxHRows = std::max(maxHRows, sg.rangeH.count());
        }

        // Initialize shared state for this resolution
        auto& sh = ds.shared[res - 1];
        sh.wavelet = wav;
        sh.intermediateStride = intermediateStride;
        sh.maxLRows = maxLRows;
        sh.maxHRows = maxHRows;
        sh.threadBufL.resize(num_threads);
        sh.threadBufH.resize(num_threads);
        for(uint32_t t = 0; t < num_threads; ++t)
        {
          sh.threadBufL[t] = std::make_unique<int32_t[]>((size_t)intermediateStride * maxLRows);
          sh.threadBufH[t] = std::make_unique<int32_t[]>((size_t)intermediateStride * maxHRows);
        }

        DcShiftParam dcShift = (res == cw.numRes - 1) ? wav->dcShift_ : DcShiftParam{};

        // Get sub-band buffer views (int32_t)
        auto llBand = tileBuffer->getResWindowBufferSimple((uint8_t)(res - 1U));
        auto hlBand = tileBuffer->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_HL);
        auto lhBand = tileBuffer->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_LH);
        auto hhBand = tileBuffer->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_HH);
        auto winDest = tileBuffer->getResWindowBufferSimple(res);

        // Record starting index for this resolution's jobs
        size_t jobBase = ds.jobs.size();

        // Create per-strip jobs with cross-resolution dependency gates
        for(size_t s = 0; s < stripGeoms.size(); ++s)
        {
          FusedStripJob job;
          job.shared = &sh;
          job.engine = engine;
          job.geom = stripGeoms[s];
          job.llBand = llBand;
          job.hlBand = hlBand;
          job.lhBand = lhBand;
          job.hhBand = hhBand;
          job.winDest = winDest;
          job.winDest.incY_IN_PLACE(stripGeoms[s].outStart);
          job.hSn = wav->horiz_.sn;
          job.hDn = wav->horiz_.dn;
          job.hParity = wav->horiz_.parity;
          job.resWidth = resWidth;
          job.dcShift = dcShift;
          job.dependentGates = gates.crossResDepGates[res - 1][s];
          ds.jobs.push_back(std::move(job));
        }

        // Submit per-strip batches
        for(size_t s = 0; s < stripGeoms.size(); ++s)
        {
          uint32_t stripGate = gates.stripGateIds[res - 1][s];
          scx_engine_submit_full_batch(engine, cw.dwtDomainId, 0, 1, FusedStripJob::execute,
                                       &ds.jobs[jobBase + s], stripGate, SCX_NO_GATE);
        }
      }
    }
    else
    {
      // === 9/7 irreversible DWT path ===
      auto tileBuffer = tilec->getWindow();
      ds.shared97.resize(cw.numRes - 1);

      for(uint8_t res = 1; res < cw.numRes; ++res)
      {
        wav->horiz_.sn = bandLL->width();
        wav->vert_.sn = bandLL->height();
        ++bandLL;
        auto resWidth = bandLL->width();
        auto resHeight = bandLL->height();
        if(resWidth == 0 || resHeight == 0)
          continue;
        wav->horiz_.dn = resWidth - wav->horiz_.sn;
        wav->horiz_.parity = bandLL->x0 & 1;
        wav->vert_.dn = resHeight - wav->vert_.sn;
        wav->vert_.parity = bandLL->y0 & 1;

        auto& stripGeoms = gates.stripGeoms[res - 1];
        if(stripGeoms.empty())
          continue;

        // Initialize shared state for this resolution
        auto& sh = ds.shared97[res - 1];
        sh.wavelet = wav;
        sh.resWidth = resWidth;
        sh.hScratchLen = (size_t)resWidth;

        DcShiftParam dcShift = (res == cw.numRes - 1) ? wav->dcShift_ : DcShiftParam{};

        // Get sub-band buffer views (float)
        auto llBand = tileBuffer->getResWindowBufferSimpleF((uint8_t)(res - 1U));
        auto hlBand = tileBuffer->getBandWindowBufferPaddedSimpleF(res, t1::BAND_ORIENT_HL);
        auto lhBand = tileBuffer->getBandWindowBufferPaddedSimpleF(res, t1::BAND_ORIENT_LH);
        auto hhBand = tileBuffer->getBandWindowBufferPaddedSimpleF(res, t1::BAND_ORIENT_HH);
        auto winDest = tileBuffer->getResWindowBufferSimpleF(res);

        // Record starting index for this resolution's jobs
        size_t jobBase = ds.jobs97.size();

        // Create per-strip jobs with cross-resolution dependency gates
        for(size_t s = 0; s < stripGeoms.size(); ++s)
        {
          FusedStripJob97 job;
          job.shared = &sh;
          job.engine = engine;
          job.geom = stripGeoms[s];
          job.llBand = llBand;
          job.hlBand = hlBand;
          job.lhBand = lhBand;
          job.hhBand = hhBand;
          job.winDest = winDest;
          job.winDest.incY_IN_PLACE(stripGeoms[s].outStart);
          job.hSn = wav->horiz_.sn;
          job.hDn = wav->horiz_.dn;
          job.hParity = wav->horiz_.parity;
          job.resWidth = resWidth;
          job.dcShift = dcShift;
          job.dependentGates = gates.crossResDepGates[res - 1][s];
          ds.jobs97.push_back(std::move(job));
        }

        // Submit per-strip batches
        for(size_t s = 0; s < stripGeoms.size(); ++s)
        {
          uint32_t stripGate = gates.stripGateIds[res - 1][s];
          scx_engine_submit_full_batch(engine, cw.dwtDomainId, 0, 1, FusedStripJob97::execute,
                                       &ds.jobs97[jobBase + s], stripGate, SCX_NO_GATE);
        }
      }
    }
  }

  // === Phase 5: Run engine (T1 + gated DWT interleave naturally) ===
  // Working-wait: workers process T1 jobs while DWT gates are closed,
  // then immediately start DWT work as gates open — no sequence barriers.
  scx_engine_run(engine);

  if(!success_)
    return false;

  // === Phase 6: Handle components that couldn't use ScxEngine DWT ===
  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    auto& cw = compWork[compno];
    if(!cw.needsDwt || dwtStates[compno].usedScxDwt)
      continue;

    auto tilec = tileProcessor->getTile()->comps_ + compno;
    auto maxDim = std::max(tileProcessor->getCodingParams()->t_width_,
                           tileProcessor->getCodingParams()->t_height_);

    dwtHelper_->release();
    dwtHelper_->clear();
    dwtHelper_->setupComponentFlow(compno, cw.numRes, !tilec->isWholeTileDecoding());

    WaveletReverse wavelet(dwtHelper_, tilec, compno, tilec->windowUnreducedBounds(), cw.numRes,
                           cw.qmfbid, maxDim, tcp->wholeTileDecompress_, waveletPoolData_);
    if(!wavelet.decompress())
      return false;

    TFSingleton::get().run(*dwtHelper_).wait();
    dwtHelper_->release();
    dwtHelper_->clear();
  }

  return success_;
}
#else
bool SchedulerFreebyrd::decodeAndTransform(ITileProcessor* tileProcessor)
{
  auto tcp = tileProcessor->getTCP();
  bool cacheAll =
      (tileProcessor->getTileCacheStrategy() & GRK_TILE_CACHE_ALL) == GRK_TILE_CACHE_ALL;
  uint32_t num_threads = (uint32_t)TFSingleton::num_threads();
  bool finalLayer = tcp->layersToDecompress_ == tcp->numLayers_;

  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    if(!tileProcessor->shouldDecodeComponent(compno))
      continue;

    auto tccp = tcp->tccps_ + compno;
    uint16_t cbw = tccp->cblkw_expn_ ? (uint16_t)(1 << tccp->cblkw_expn_) : 0U;
    uint16_t cbh = tccp->cblkh_expn_ ? (uint16_t)(1 << tccp->cblkh_expn_) : 0U;
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
    uint8_t resBegin =
        cacheAll ? (uint8_t)tilec->currentPacketProgressionState_.numResolutionsRead() : 0;
    uint8_t resUpperBound = tilec->nextPacketProgressionState_.numResolutionsRead();
    uint8_t numRes = tilec->nextPacketProgressionState_.numResolutionsRead();

    if(numRes == 0)
      continue;

    // 1. Set up ImageComponentFlow for this component
    dwtHelper_->release();
    dwtHelper_->clear();
    dwtHelper_->setupComponentFlow(compno, numRes, !wholeTileDecoding);

    // 2. Collect blocks per resolution and schedule T1 decode into flow
    // Combine first two resolution levels (0+1) into a single flow slot (like standard scheduler)
    uint8_t flowResIdx = 0;
    for(uint8_t resno = resBegin; resno < resUpperBound; ++resno)
    {
      auto res = tilec->resolutions_ + resno;
      for(uint8_t bandIndex = 0; bandIndex < res->numBands_; ++bandIndex)
      {
        auto band = res->band + bandIndex;
        auto paddedBandWindow = tilec->getBandWindowPadded(resno, band->orientation_);
        for(auto precinct : band->precincts_)
        {
          if(!wholeTileDecoding && !paddedBandWindow->nonEmptyIntersection(precinct))
            continue;
          for(uint32_t cblkno = 0; cblkno < precinct->getNumCblks(); ++cblkno)
          {
            auto cblkBounds = precinct->getCodeBlockBounds(cblkno);
            if(!wholeTileDecoding && !paddedBandWindow->nonEmptyIntersection(&cblkBounds))
              continue;

            auto cblk = precinct->getDecompressBlock(cblkno);
            auto block = std::make_shared<t1::DecompressBlockExec>(cacheAll);
            block->x = cblk->x0();
            block->y = cblk->y0();
            block->postProcessor_ =
                tcp->isHT()
                    ? t1::DecompressBlockPostProcessor<int32_t>(
                          [tilec](int32_t* srcData, t1::DecompressBlockExec* blk, uint16_t stride) {
                            tilec->postProcessBlockHT(srcData, blk, stride);
                          })
                    : t1::DecompressBlockPostProcessor<int32_t>(
                          [tilec](int32_t* srcData, t1::DecompressBlockExec* blk,
                                  [[maybe_unused]] uint16_t stride) {
                            tilec->postProcessBlock(srcData, blk);
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

            // Schedule T1 decode into the flow
            auto imageComponentFlow = dwtHelper_->getImageComponentFlow(compno);
            auto resFlow = imageComponentFlow->getResflow(flowResIdx);
            resFlow->blocks_->nextTask().work([this, activePool, block, tccp, cbw, cbh, cacheAll,
                                               tileProcessor]() {
              if(!success_)
                return;
              t1::ICoder* coder = nullptr;
              if(block->needsCachedCoder())
              {
                coder = t1::CoderFactory::makeCoder(tileProcessor->getTCP()->isHT(), false, cbw,
                                                    cbh, tileProcessor->getTileCacheStrategy());
              }
              else if(!cacheAll)
              {
                auto threadnum = TFSingleton::get().this_worker_id();
                coder =
                    activePool->getCoder((size_t)threadnum, tccp->cblkw_expn_, tccp->cblkh_expn_)
                        .get();
              }
              try
              {
                if(!block->open(coder))
                  success_ = false;
              }
              catch(const std::runtime_error& rerr)
              {
                grklog.error(rerr.what());
                success_ = false;
              }
            });
          }
        }
      }
      // Combine res 0 with res 1 into the same flow slot (same as standard scheduler)
      if(resno == 0 && resUpperBound > 1)
        continue;
      flowResIdx++;
    }
    tilec->currentPacketProgressionState_ = tilec->nextPacketProgressionState_;

    if(!success_)
      return false;

    // 3. Schedule DWT (if more than 1 resolution)
    if(numRes > 1)
    {
      auto maxDim = std::max(tileProcessor->getCodingParams()->t_width_,
                             tileProcessor->getCodingParams()->t_height_);

      WaveletReverse wavelet(dwtHelper_, tilec, compno, tilec->windowUnreducedBounds(), numRes,
                             tccp->qmfbid_, maxDim, tcp->wholeTileDecompress_, waveletPoolData_);
      if(!wavelet.decompress())
        return false;
    }

    // 4. Run the complete T1+DWT flow
    TFSingleton::get().run(*dwtHelper_).wait();
    dwtHelper_->release();
    dwtHelper_->clear();
  }

  return success_;
}
#endif

bool SchedulerFreebyrd::runCascadeDWT97([[maybe_unused]] ITileProcessor* tileProcessor,
                                        [[maybe_unused]] uint16_t compno)
{
  return false;
}

bool SchedulerFreebyrd::runSeparateDWT53([[maybe_unused]] ITileProcessor* tileProcessor,
                                         [[maybe_unused]] uint16_t compno)
{
  return false;
}

bool SchedulerFreebyrd::runSeparateDWT16([[maybe_unused]] ITileProcessor* tileProcessor,
                                         [[maybe_unused]] uint16_t compno)
{
  return false;
}

bool SchedulerFreebyrd::postProcess(ITileProcessor* tileProcessor)
{
  if(!tileProcessor->doPostT1())
    return true;

  auto tcp = tileProcessor->getTCP();
  auto mct = tileProcessor->getMCT();

  if(tileProcessor->needsMctDecompress())
  {
    // MCT with DC shift
    FlowComponent mctComp;
    if(tcp->tccps_->qmfbid_ == 1)
      mct->schedule_decompress_rev(&mctComp, true);
    else
      mct->schedule_decompress_irrev(&mctComp, true);

    TFSingleton::get().run(mctComp).wait();
  }
  else
  {
    // DC shift only, per component
    for(uint16_t compno = 0; compno < numcomps_; ++compno)
    {
      if(!tileProcessor->shouldDecodeComponent(compno))
        continue;

      auto tccp = tcp->tccps_ + compno;

      // Freebyrd doesn't fuse DC shift into wavelet, so always apply it
      FlowComponent dcComp;
      if(tccp->qmfbid_ == 1)
        mct->schedule_decompress_dc_shift_rev(&dcComp, compno);
      else
        mct->schedule_decompress_dc_shift_irrev(&dcComp, compno);

      TFSingleton::get().run(dcComp).wait();
    }
  }

  return true;
}

} // namespace grk
