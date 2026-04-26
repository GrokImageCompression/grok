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

#include "FRBSingleton.h"
#include "TFSingleton.h"
#include "TileFutureManager.h"

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
#include "ITileProcessor.h"
#include "CoderFactory.h"
#include "TileBlocks.h"
#include "WaveletReverse.h"
#include "StripPartitioner.h"
#include "SchedulerFreebyrd.h"

#include "StripDecompressor.h"

namespace grk
{

SchedulerFreebyrd::SchedulerFreebyrd(uint16_t numcomps, uint8_t prec)
    : numcomps_(numcomps), prec_(prec), success_(true)
{}

SchedulerFreebyrd::~SchedulerFreebyrd()
{
  release();
}

void SchedulerFreebyrd::release()
{
  blocksByComp_.clear();
}

bool SchedulerFreebyrd::decompressTile(ITileProcessor* tileProcessor)
{
  success_ = true;
  auto& pool = FRBSingleton::get();

  grklog.info("SchedulerFreebyrd: active with %zu threads", FRBSingleton::num_threads());

  if(isStripMode())
  {
    grklog.info("SchedulerFreebyrd: strip-based decompression (GRK_STRIP=1)");

    bool hasStripCallback = !!stripOutputCallback_;

    // strip-based: T1 decode + DWT interleaved per component per strip
    for(uint16_t compno = 0; compno < numcomps_; ++compno)
    {
      if(!tileProcessor->shouldDecodeComponent(compno))
        continue;

      StripDecompressor strip(tileProcessor, compno, prec_, success_);

      if(hasStripCallback)
      {
        // streaming mode: strip-local buffers, output via callback
        auto cb = stripOutputCallback_;
        if(!strip.decompressStream(
               [compno, cb](uint32_t row0, uint32_t numRows, const void* rowData,
                            uint32_t rowStride) { cb(compno, row0, numRows, rowData, rowStride); }))
          return false;
      }
      else
      {
        // legacy mode: write into tile buffer for comparison
        if(!strip.decompress())
          return false;
      }
    }
    grklog.info("SchedulerFreebyrd: strip T1+DWT complete");

    // skip post-processing for streaming mode (no tile buffer to process)
    if(!hasStripCallback)
    {
      if(!postProcess(tileProcessor))
        return false;
    }
  }
  else
  {
    // original full-buffer approach
    if(!decodeBlocks(tileProcessor))
      return false;
    grklog.info("SchedulerFreebyrd: T1 decode complete");

    if(!runDWT(tileProcessor))
      return false;
    grklog.info("SchedulerFreebyrd: DWT complete");

    if(!postProcess(tileProcessor))
      return false;
  }

  (void)pool;
  return success_;
}

/* Build block list and T1-decode all code blocks using freebyrd pool.
 * Mirrors DecompressScheduler::scheduleT1() block iteration. */
bool SchedulerFreebyrd::decodeBlocks(ITileProcessor* tileProcessor)
{
  auto tcp = tileProcessor->getTCP();
  auto& pool = FRBSingleton::get();
  uint32_t num_threads = (uint32_t)FRBSingleton::num_threads();

  blocksByComp_.resize(numcomps_);

  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    if(!tileProcessor->shouldDecodeComponent(compno))
      continue;

    auto tccp = tcp->tccps_ + compno;
    uint16_t cbw = tccp->cblkw_expn_ ? (uint16_t)1 << tccp->cblkw_expn_ : 0;
    uint16_t cbh = tccp->cblkh_expn_ ? (uint16_t)1 << tccp->cblkh_expn_ : 0;

    coderPool_.makeCoders(num_threads, tccp->cblkw_expn_, tccp->cblkh_expn_,
                          [tcp, cbw, cbh, tileProcessor]() -> std::shared_ptr<t1::ICoder> {
                            return std::shared_ptr<t1::ICoder>(
                                t1::CoderFactory::makeCoder(tcp->isHT(), false, cbw, cbh,
                                                            tileProcessor->getTileCacheStrategy()));
                          });

    auto tilec = tileProcessor->getTile()->comps_ + compno;
    auto wholeTileDecoding = tilec->isWholeTileDecoding();

    auto& componentBlocks = blocksByComp_[compno];
    componentBlocks.clear();

    uint8_t numRes = tilec->nextPacketProgressionState_.numResolutionsRead();
    for(uint8_t resno = 0; resno < numRes; ++resno)
    {
      BlockList resBlocks;
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
            if(wholeTileDecoding || paddedBandWindow->nonEmptyIntersection(&cblkBounds))
            {
              auto cblk = precinct->getDecompressBlock(cblkno);
              auto block = std::make_shared<t1::DecompressBlockExec>(false);
              block->x = cblk->x0();
              block->y = cblk->y0();
              block->postProcessor_ =
                  tcp->isHT() ? t1::DecompressBlockPostProcessor<int32_t>(
                                    [tilec](int32_t* srcData, t1::DecompressBlockExec* block,
                                            uint16_t stride) {
                                      tilec->postProcessBlockHT(srcData, block, stride);
                                    })
                              : t1::DecompressBlockPostProcessor<int32_t>(
                                    [tilec](int32_t* srcData, t1::DecompressBlockExec* block,
                                            [[maybe_unused]] uint16_t stride) {
                                      tilec->postProcessBlock(srcData, block);
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
              block->finalLayer_ = (tcp->layersToDecompress_ == tcp->numLayers_);
              resBlocks.push_back(block);
            }
          }
        }
      }
      componentBlocks.push_back(std::move(resBlocks));
    }

    // submit all blocks for this component to freebyrd pool
    auto tccp_w = tccp->cblkw_expn_;
    auto tccp_h = tccp->cblkh_expn_;
    for(auto& resBlocks : componentBlocks)
    {
      for(auto& block : resBlocks)
      {
        pool.submit(frb::task([this, &block, &pool, tccp_w, tccp_h] {
          if(!success_)
            return;
          auto threadId = frb::thread_pool::current_worker_id();
          auto coder = coderPool_.getCoder((size_t)threadId, tccp_w, tccp_h).get();
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
        }));
      }
    }
  }

  // wait for all T1 decode tasks to complete
  pool.wait_idle();

  if(!success_)
  {
    grklog.error("SchedulerFreebyrd: T1 decode failed");
    return false;
  }

  // update progression state
  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    if(!tileProcessor->shouldDecodeComponent(compno))
      continue;
    auto tilec = tileProcessor->getTile()->comps_ + compno;
    tilec->currentPacketProgressionState_ = tilec->nextPacketProgressionState_;
  }

  return true;
}

/* Cascade DWT per strip per resolution, using freebyrd pool.
/* Dispatch DWT based on wavelet type (qmfbid). */
bool SchedulerFreebyrd::runDWT(ITileProcessor* tileProcessor)
{
  auto tcp = tileProcessor->getTCP();

  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    if(!tileProcessor->shouldDecodeComponent(compno))
      continue;

    auto tccp = tcp->tccps_ + compno;
    auto tilec = tileProcessor->getTile()->comps_ + compno;
    uint8_t numRes = tilec->nextPacketProgressionState_.numResolutionsRead();

    if(numRes <= 1)
      continue;

    if(tilec->is16BitDwt())
    {
      if(!runSeparateDWT16(tileProcessor, compno))
        return false;
    }
    else if(tccp->qmfbid_ == 0)
    {
      if(!runCascadeDWT97(tileProcessor, compno))
        return false;
    }
    else
    {
      if(!runSeparateDWT53(tileProcessor, compno))
        return false;
    }
  }

  return true;
}

/* Cascade DWT for 9/7 irreversible: combined H+V per strip per resolution.
 * Mirrors WaveletReverse::tile_97_cascade() but without Taskflow. */
bool SchedulerFreebyrd::runCascadeDWT97(ITileProcessor* tileProcessor, uint16_t compno)
{
  auto tcp = tileProcessor->getTCP();
  auto& pool = FRBSingleton::get();

  auto tccp = tcp->tccps_ + compno;
  auto tilec = tileProcessor->getTile()->comps_ + compno;
  uint8_t numRes = tilec->nextPacketProgressionState_.numResolutionsRead();

  auto tr = tilec->resolutions_;
  auto buf = tilec->getWindow();
  uint32_t resWidth = tr->width();
  uint32_t resHeight = tr->height();

  size_t dataLength = max_resolution(tr, numRes);

  // compute DC shift for fusion into last-level DWT
  bool wholeDecompress = tcp->wholeTileDecompress_;
  DcShiftParam dcShift;
  if(numRes > 1 && wholeDecompress)
  {
    bool isMctComp = tileProcessor->needsMctDecompress(compno) && tcp->mct_ == 1;
    if(!isMctComp)
    {
      auto img_comp = tileProcessor->getHeaderImage()->comps + compno;
      dcShift.shift = tccp->dcLevelShift_;
      if(img_comp->sgnd)
      {
        dcShift.min = -(1 << (img_comp->prec - 1));
        dcShift.max = (1 << (img_comp->prec - 1)) - 1;
      }
      else
      {
        dcShift.min = 0;
        dcShift.max = (1 << img_comp->prec) - 1;
      }
      dcShift.enabled = (dcShift.shift != 0) || (tccp->qmfbid_ == 0);
    }
  }

  // create a WaveletReverse instance to access cascade_strip_97
  // (scheduler_=nullptr is safe since cascade_strip_97 doesn't use it)
  WaveletReverse wavelet(nullptr, tilec, compno, tilec->windowUnreducedBounds(), numRes,
                         tccp->qmfbid_,
                         std::max(tileProcessor->getCodingParams()->t_width_,
                                  tileProcessor->getCodingParams()->t_height_),
                         wholeDecompress, nullptr, {}, true);

  for(uint8_t res = 1; res < numRes; ++res)
  {
    auto prevResWidth = resWidth;
    auto prevResHeight = resHeight;
    ++tr;
    resWidth = tr->width();
    resHeight = tr->height();
    if(resWidth == 0 || resHeight == 0)
      continue;

    // H-DWT parameters for this resolution
    uint32_t h_sn = prevResWidth;
    uint32_t h_dn = resWidth - prevResWidth;
    uint32_t h_parity = tr->x0 & 1;

    // V-DWT parameters
    uint32_t v_sn = prevResHeight;
    uint32_t v_dn = resHeight - prevResHeight;
    uint32_t v_parity = tr->y0 & 1;

    // sub-band inputs
    auto winLL = buf->getResWindowBufferSimpleF((uint8_t)(res - 1U));
    auto winHL = buf->getBandWindowBufferPaddedSimpleF(res, t1::BAND_ORIENT_HL);
    auto winLH = buf->getBandWindowBufferPaddedSimpleF(res, t1::BAND_ORIENT_LH);
    auto winHH = buf->getBandWindowBufferPaddedSimpleF(res, t1::BAND_ORIENT_HH);
    auto winDest = buf->getResWindowBufferSimpleF(res);

    // allocate temp buffer to avoid aliasing (sub-band inputs share memory with dest)
    auto tempBufMem = std::make_shared<std::vector<float>>((size_t)resWidth * resHeight);
    Buffer2dSimple<float> tempDest(tempBufMem->data(), resWidth, resHeight);

    // DC shift only on the last resolution level
    DcShiftParam resDcShift = (res == numRes - 1) ? dcShift : DcShiftParam{};

    // partition into strips
    auto strips = StripPartitioner::partition(resHeight, v_sn, v_dn, v_parity);

    // submit strip DWT tasks to freebyrd pool
    for(auto& sg : strips)
    {
      pool.submit(frb::task([this, &wavelet, sg, h_sn, h_dn, h_parity, resWidth, dataLength, winLL,
                             winHL, winLH, winHH, tempDest, resDcShift] {
        if(!success_)
          return;

        // allocate per-task DWT scratch buffers
        dwt_scratch<vec4f> hScratch;
        hScratch.sn = h_sn;
        hScratch.dn = h_dn;
        hScratch.parity = h_parity;
        hScratch.win_l = Line32(0, h_sn);
        hScratch.win_h = Line32(0, h_dn);
        if(!WaveletReverse::allocCascadeScratch97(hScratch, dataLength))
        {
          grklog.error("SchedulerFreebyrd: cascade hScratch alloc failed");
          success_ = false;
          return;
        }

        dwt_scratch<vec4f> vScratch;
        uint32_t ext_sn = sg.rangeL.count();
        uint32_t ext_dn = sg.rangeH.count();
        vScratch.sn = ext_sn;
        vScratch.dn = ext_dn;
        vScratch.parity = sg.localParity;
        vScratch.win_l = Line32(0, ext_sn);
        vScratch.win_h = Line32(0, ext_dn);
        vScratch.outputStart = sg.outputStartInStripe;
        vScratch.outputCount = sg.outCount;
        if(!WaveletReverse::allocCascadeScratch97(vScratch, dataLength))
        {
          grklog.error("SchedulerFreebyrd: cascade vScratch alloc failed");
          success_ = false;
          return;
        }

        // set up sub-band windows offset to this strip's start row
        auto stripLL = winLL;
        stripLL.incY_IN_PLACE(sg.rangeL.lo);
        auto stripHL = winHL;
        stripHL.incY_IN_PLACE(sg.rangeL.lo);
        auto stripLH = winLH;
        stripLH.incY_IN_PLACE(sg.rangeH.lo);
        auto stripHH = winHH;
        stripHH.incY_IN_PLACE(sg.rangeH.lo);
        auto stripDest = tempDest;
        stripDest.incY_IN_PLACE(sg.outStart);

        wavelet.cascade_strip_97(&hScratch, &vScratch, resWidth, stripLL, stripHL, stripLH, stripHH,
                                 stripDest, resDcShift);
      }));
    }

    // wait for all strip tasks at this resolution to complete
    pool.wait_idle();

    if(!success_)
      return false;

    // copy-back from temp buffer to real destination
    for(uint32_t row = 0; row < resHeight; ++row)
    {
      memcpy(winDest.buf_ + row * winDest.stride_, tempDest.buf_ + row * tempDest.stride_,
             resWidth * sizeof(float));
    }
  }

  return true;
}

/* Separate H-DWT + V-DWT for 5/3 reversible: row-parallel H, then column-parallel V.
 * Mirrors WaveletReverse::tile_53() but uses freebyrd pool instead of Taskflow. */
bool SchedulerFreebyrd::runSeparateDWT53(ITileProcessor* tileProcessor, uint16_t compno)
{
  auto tcp = tileProcessor->getTCP();
  auto& pool = FRBSingleton::get();

  auto tccp = tcp->tccps_ + compno;
  auto tilec = tileProcessor->getTile()->comps_ + compno;
  uint8_t numRes = tilec->nextPacketProgressionState_.numResolutionsRead();

  auto tr = tilec->resolutions_;
  auto buf = tilec->getWindow();
  uint32_t resWidth = tr->width();
  uint32_t resHeight = tr->height();

  uint32_t maxDim = max_resolution(tr, numRes);

  // compute DC shift for fusion into last-level V-DWT
  bool wholeDecompress = tcp->wholeTileDecompress_;
  DcShiftParam dcShift;
  if(numRes > 1 && wholeDecompress)
  {
    bool isMctComp = tileProcessor->needsMctDecompress(compno) && tcp->mct_ == 1;
    if(!isMctComp)
    {
      auto img_comp = tileProcessor->getHeaderImage()->comps + compno;
      dcShift.shift = tccp->dcLevelShift_;
      if(img_comp->sgnd)
      {
        dcShift.min = -(1 << (img_comp->prec - 1));
        dcShift.max = (1 << (img_comp->prec - 1)) - 1;
      }
      else
      {
        dcShift.min = 0;
        dcShift.max = (1 << img_comp->prec) - 1;
      }
      dcShift.enabled = (dcShift.shift != 0);
    }
  }

  // create a WaveletReverse instance to access h_strip_53 and v_strip_53
  WaveletReverse wavelet(nullptr, tilec, compno, tilec->windowUnreducedBounds(), numRes,
                         tccp->qmfbid_,
                         std::max(tileProcessor->getCodingParams()->t_width_,
                                  tileProcessor->getCodingParams()->t_height_),
                         wholeDecompress, nullptr, {}, true);

  uint32_t pllCols53 = get_PLL_COLS_53();
  // scratch size: enough for V-DWT SIMD path (height * PLL_COLS_53 int32_t elements)
  size_t scratchElems = (size_t)maxDim * pllCols53;

  for(uint8_t res = 1; res < numRes; ++res)
  {
    auto prevResWidth = resWidth;
    auto prevResHeight = resHeight;
    ++tr;
    resWidth = tr->width();
    resHeight = tr->height();
    if(resWidth == 0 || resHeight == 0)
      continue;

    // H-DWT parameters for this resolution
    uint32_t h_sn = prevResWidth;
    uint32_t h_dn = resWidth - prevResWidth;
    uint32_t h_parity = tr->x0 & 1;

    // V-DWT parameters
    uint32_t v_sn = prevResHeight;
    uint32_t v_dn = resHeight - prevResHeight;
    uint32_t v_parity = tr->y0 & 1;

    // ---- H-DWT phase: LL+HL → SPLIT_L, LH+HH → SPLIT_H ----
    for(uint32_t orient = 0; orient < 2; ++orient)
    {
      uint32_t bandHeight = (orient == 0) ? v_sn : v_dn;
      if(bandHeight == 0)
        continue;

      Buffer2dSimple<int32_t> winL, winH, winDest;
      if(orient == 0)
      {
        winL = buf->getResWindowBufferSimple((uint8_t)(res - 1U));
        winH = buf->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_HL);
        winDest = buf->getResWindowBufferSplitSimple(res, SPLIT_L);
      }
      else
      {
        winL = buf->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_LH);
        winH = buf->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_HH);
        winDest = buf->getResWindowBufferSplitSimple(res, SPLIT_H);
      }

      uint32_t numTasks = std::min(bandHeight, (uint32_t)FRBSingleton::num_threads());
      uint32_t heightIncr = bandHeight / numTasks;
      for(uint32_t j = 0; j < numTasks; ++j)
      {
        auto hMax = (j < numTasks - 1U) ? heightIncr : bandHeight - j * heightIncr;
        pool.submit(
            frb::task([this, &wavelet, h_sn, h_dn, h_parity, winL, winH, winDest, hMax, maxDim] {
              if(!success_)
                return;

              dwt_scratch<int32_t> hScratch;
              hScratch.sn = h_sn;
              hScratch.dn = h_dn;
              hScratch.parity = h_parity;
              if(!hScratch.alloc(maxDim))
              {
                grklog.error("SchedulerFreebyrd: 5/3 hScratch alloc failed");
                success_ = false;
                return;
              }

              wavelet.h_strip_53(&hScratch, 0, hMax, winL, winH, winDest);
            }));
        winL.incY_IN_PLACE(heightIncr);
        winH.incY_IN_PLACE(heightIncr);
        winDest.incY_IN_PLACE(heightIncr);
      }
    }

    pool.wait_idle();
    if(!success_)
      return false;

    // ---- V-DWT phase: SPLIT_L + SPLIT_H → dest ----
    auto winL = buf->getResWindowBufferSplitSimple(res, SPLIT_L);
    auto winH = buf->getResWindowBufferSplitSimple(res, SPLIT_H);
    auto winDest = buf->getResWindowBufferSimple(res);

    // DC shift only on the last resolution level
    DcShiftParam resDcShift = (res == numRes - 1) ? dcShift : DcShiftParam{};

    uint32_t numTasks = std::min(resWidth, (uint32_t)FRBSingleton::num_threads());
    uint32_t widthIncr = resWidth / numTasks;
    for(uint32_t j = 0; j < numTasks; ++j)
    {
      auto wMin = j * widthIncr;
      auto wMax = (j < numTasks - 1U) ? (j + 1U) * widthIncr : resWidth;
      pool.submit(frb::task([this, &wavelet, v_sn, v_dn, v_parity, wMin, wMax, winL, winH, winDest,
                             resDcShift, scratchElems] {
        if(!success_)
          return;

        dwt_scratch<int32_t> vScratch;
        vScratch.sn = v_sn;
        vScratch.dn = v_dn;
        vScratch.parity = v_parity;
        if(!vScratch.alloc(scratchElems))
        {
          grklog.error("SchedulerFreebyrd: 5/3 vScratch alloc failed");
          success_ = false;
          return;
        }

        wavelet.v_strip_53(&vScratch, wMin, wMax, winL, winH, winDest, resDcShift);
      }));
      winL.incX_IN_PLACE(widthIncr);
      winH.incX_IN_PLACE(widthIncr);
      winDest.incX_IN_PLACE(widthIncr);
    }

    pool.wait_idle();
    if(!success_)
      return false;
  }

  return true;
}

/* 16-bit separate H-DWT + V-DWT for both 5/3 and 9/7.
 * Uses int16_t buffers from getWindow16() and the _16_ DWT variants. */
bool SchedulerFreebyrd::runSeparateDWT16(ITileProcessor* tileProcessor, uint16_t compno)
{
  auto tcp = tileProcessor->getTCP();
  auto& pool = FRBSingleton::get();

  auto tccp = tcp->tccps_ + compno;
  auto tilec = tileProcessor->getTile()->comps_ + compno;
  uint8_t numRes = tilec->nextPacketProgressionState_.numResolutionsRead();
  uint8_t qmfbid = tccp->qmfbid_;

  auto tr = tilec->resolutions_;
  auto buf = tilec->getWindow16();
  uint32_t resWidth = tr->width();
  uint32_t resHeight = tr->height();

  uint32_t maxDim = max_resolution(tr, numRes);

  // compute DC shift for fusion into last-level V-DWT
  bool wholeDecompress = tcp->wholeTileDecompress_;
  DcShiftParam dcShift;
  if(numRes > 1 && wholeDecompress)
  {
    bool isMctComp = tileProcessor->needsMctDecompress(compno) && tcp->mct_ == 1;
    if(!isMctComp)
    {
      auto img_comp = tileProcessor->getHeaderImage()->comps + compno;
      dcShift.shift = tccp->dcLevelShift_;
      if(img_comp->sgnd)
      {
        dcShift.min = -(1 << (img_comp->prec - 1));
        dcShift.max = (1 << (img_comp->prec - 1)) - 1;
      }
      else
      {
        dcShift.min = 0;
        dcShift.max = (1 << img_comp->prec) - 1;
      }
      dcShift.enabled = (dcShift.shift != 0) || (qmfbid == 0);
    }
  }

  // create a WaveletReverse instance to access 16-bit h_strip/v_strip methods
  WaveletReverse wavelet(nullptr, tilec, compno, tilec->windowUnreducedBounds(), numRes, qmfbid,
                         std::max(tileProcessor->getCodingParams()->t_width_,
                                  tileProcessor->getCodingParams()->t_height_),
                         wholeDecompress, nullptr, {}, true);

  uint32_t pllCols = (qmfbid == 1) ? get_PLL_COLS_16_53() : get_PLL_COLS_16_97();
  // scratch size: enough for V-DWT SIMD path (height * pllCols int16_t elements)
  size_t scratchElems = (size_t)maxDim * pllCols;

  for(uint8_t res = 1; res < numRes; ++res)
  {
    auto prevResWidth = resWidth;
    auto prevResHeight = resHeight;
    ++tr;
    resWidth = tr->width();
    resHeight = tr->height();
    if(resWidth == 0 || resHeight == 0)
      continue;

    // H-DWT parameters for this resolution
    uint32_t h_sn = prevResWidth;
    uint32_t h_dn = resWidth - prevResWidth;
    uint32_t h_parity = tr->x0 & 1;

    // V-DWT parameters
    uint32_t v_sn = prevResHeight;
    uint32_t v_dn = resHeight - prevResHeight;
    uint32_t v_parity = tr->y0 & 1;

    // ---- H-DWT phase: LL+HL → SPLIT_L, LH+HH → SPLIT_H ----
    for(uint32_t orient = 0; orient < 2; ++orient)
    {
      uint32_t bandHeight = (orient == 0) ? v_sn : v_dn;
      if(bandHeight == 0)
        continue;

      Buffer2dSimple<int16_t> winL, winH, winDest;
      if(orient == 0)
      {
        winL = buf->getResWindowBufferSimple((uint8_t)(res - 1U));
        winH = buf->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_HL);
        winDest = buf->getResWindowBufferSplitSimple(res, SPLIT_L);
      }
      else
      {
        winL = buf->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_LH);
        winH = buf->getBandWindowBufferPaddedSimple(res, t1::BAND_ORIENT_HH);
        winDest = buf->getResWindowBufferSplitSimple(res, SPLIT_H);
      }

      uint32_t numTasks = std::min(bandHeight, (uint32_t)FRBSingleton::num_threads());
      uint32_t heightIncr = bandHeight / numTasks;
      for(uint32_t j = 0; j < numTasks; ++j)
      {
        auto hMax = (j < numTasks - 1U) ? heightIncr : bandHeight - j * heightIncr;
        pool.submit(frb::task(
            [this, &wavelet, qmfbid, h_sn, h_dn, h_parity, winL, winH, winDest, hMax, maxDim] {
              if(!success_)
                return;

              dwt_scratch<int16_t> hScratch;
              hScratch.sn = h_sn;
              hScratch.dn = h_dn;
              hScratch.parity = h_parity;
              if(!hScratch.alloc(maxDim))
              {
                grklog.error("SchedulerFreebyrd: 16-bit hScratch alloc failed");
                success_ = false;
                return;
              }

              if(qmfbid == 1)
                wavelet.h_strip_16_53(&hScratch, 0, hMax, winL, winH, winDest);
              else
                wavelet.h_strip_16_97(&hScratch, 0, hMax, winL, winH, winDest);
            }));
        winL.incY_IN_PLACE(heightIncr);
        winH.incY_IN_PLACE(heightIncr);
        winDest.incY_IN_PLACE(heightIncr);
      }
    }

    pool.wait_idle();
    if(!success_)
      return false;

    // ---- V-DWT phase: SPLIT_L + SPLIT_H → dest ----
    auto winL = buf->getResWindowBufferSplitSimple(res, SPLIT_L);
    auto winH = buf->getResWindowBufferSplitSimple(res, SPLIT_H);
    auto winDest = buf->getResWindowBufferSimple(res);

    // DC shift only on the last resolution level
    DcShiftParam resDcShift = (res == numRes - 1) ? dcShift : DcShiftParam{};

    uint32_t numTasks = std::min(resWidth, (uint32_t)FRBSingleton::num_threads());
    uint32_t widthIncr = resWidth / numTasks;
    for(uint32_t j = 0; j < numTasks; ++j)
    {
      auto wMin = j * widthIncr;
      auto wMax = (j < numTasks - 1U) ? (j + 1U) * widthIncr : resWidth;
      pool.submit(frb::task([this, &wavelet, qmfbid, v_sn, v_dn, v_parity, wMin, wMax, winL, winH,
                             winDest, resDcShift, scratchElems] {
        if(!success_)
          return;

        dwt_scratch<int16_t> vScratch;
        vScratch.sn = v_sn;
        vScratch.dn = v_dn;
        vScratch.parity = v_parity;
        if(!vScratch.alloc(scratchElems))
        {
          grklog.error("SchedulerFreebyrd: 16-bit vScratch alloc failed");
          success_ = false;
          return;
        }

        if(qmfbid == 1)
          wavelet.v_strip_16_53(&vScratch, wMin, wMax, winL, winH, winDest, resDcShift);
        else
          wavelet.v_strip_16_97(&vScratch, wMin, wMax, winL, winH, winDest, resDcShift);
      }));
      winL.incX_IN_PLACE(widthIncr);
      winH.incX_IN_PLACE(widthIncr);
      winDest.incX_IN_PLACE(widthIncr);
    }

    pool.wait_idle();
    if(!success_)
      return false;
  }

  return true;
}

/* Post-processing: MCT and standalone DC shift for cases not fused into DWT. */
bool SchedulerFreebyrd::postProcess(ITileProcessor* tileProcessor)
{
  auto tcp = tileProcessor->getTCP();
  auto& pool = FRBSingleton::get();
  bool wholeDecompress = tcp->wholeTileDecompress_;

  // MCT inverse color transform (standard mct_==1 only; custom mct_==2 not supported)
  if(tileProcessor->needsMctDecompress() && tcp->mct_ == 1)
  {
    auto tile = tileProcessor->getTile();
    auto tccp0 = tcp->tccps_;
    auto headerImage = tileProcessor->getHeaderImage();

    // compute per-component DC shift + clamp info
    struct ShiftInfo
    {
      int32_t shift, min, max;
    };
    ShiftInfo si[3];
    for(uint16_t c = 0; c < 3; ++c)
    {
      auto img_comp = headerImage->comps + c;
      auto tccp = tcp->tccps_ + c;
      si[c].shift = tccp->dcLevelShift_;
      if(img_comp->sgnd)
      {
        si[c].min = -(1 << (img_comp->prec - 1));
        si[c].max = (1 << (img_comp->prec - 1)) - 1;
      }
      else
      {
        si[c].min = 0;
        si[c].max = (1 << img_comp->prec) - 1;
      }
    }

    if(tccp0->qmfbid_ == 1)
    {
      // reversible inverse MCT (RCT) with DC shift
      bool use16 = tile->comps_[0].is16BitDwt();
      if(use16)
      {
        // 16-bit path: int16_t buffers
        auto w0 = tile->comps_[0].getWindow16()->getResWindowBufferHighestSimple();
        auto w1 = tile->comps_[1].getWindow16()->getResWindowBufferHighestSimple();
        auto w2 = tile->comps_[2].getWindow16()->getResWindowBufferHighestSimple();

        uint32_t stride = tile->comps_[0].getWindow16()->getResWindowBufferHighestStride();
        uint32_t height = w0.height_;
        uint32_t numTasks = std::min(height, (uint32_t)FRBSingleton::num_threads());
        uint32_t rowsPerTask = height / numTasks;

        for(uint32_t t = 0; t < numTasks; ++t)
        {
          uint32_t yBegin = t * rowsPerTask;
          uint32_t yEnd = (t < numTasks - 1) ? (t + 1) * rowsPerTask : height;

          pool.submit(frb::task([this, w0, w1, w2, stride, yBegin, yEnd, si] {
            if(!success_)
              return;
            auto index = (uint64_t)yBegin * stride;
            auto count = (uint64_t)(yEnd - yBegin) * stride;
            auto c0 = w0.buf_ + index;
            auto c1 = w1.buf_ + index;
            auto c2 = w2.buf_ + index;
            for(uint64_t j = 0; j < count; ++j)
            {
              int16_t y = c0[j];
              int16_t u = c1[j];
              int16_t v = c2[j];
              int16_t g = (int16_t)(y - ((u + v) >> 2));
              int16_t r = (int16_t)(v + g);
              int16_t b = (int16_t)(u + g);
              c0[j] = (int16_t)std::clamp((int32_t)r + si[0].shift, si[0].min, si[0].max);
              c1[j] = (int16_t)std::clamp((int32_t)g + si[1].shift, si[1].min, si[1].max);
              c2[j] = (int16_t)std::clamp((int32_t)b + si[2].shift, si[2].min, si[2].max);
            }
          }));
        }
      }
      else
      {
        // 32-bit path: int32_t buffers
        auto w0 = tile->comps_[0].getWindow()->getResWindowBufferHighestSimple();
        auto w1 = tile->comps_[1].getWindow()->getResWindowBufferHighestSimple();
        auto w2 = tile->comps_[2].getWindow()->getResWindowBufferHighestSimple();

        uint32_t stride = tile->comps_[0].getWindow()->getResWindowBufferHighestStride();
        uint32_t height = w0.height_;
        uint32_t numTasks = std::min(height, (uint32_t)FRBSingleton::num_threads());
        uint32_t rowsPerTask = height / numTasks;

        for(uint32_t t = 0; t < numTasks; ++t)
        {
          uint32_t yBegin = t * rowsPerTask;
          uint32_t yEnd = (t < numTasks - 1) ? (t + 1) * rowsPerTask : height;

          pool.submit(frb::task([this, w0, w1, w2, stride, yBegin, yEnd, si] {
            if(!success_)
              return;
            auto index = (uint64_t)yBegin * stride;
            auto count = (uint64_t)(yEnd - yBegin) * stride;
            auto c0 = w0.buf_ + index;
            auto c1 = w1.buf_ + index;
            auto c2 = w2.buf_ + index;
            for(uint64_t j = 0; j < count; ++j)
            {
              int32_t y = c0[j];
              int32_t u = c1[j];
              int32_t v = c2[j];
              int32_t g = y - ((u + v) >> 2);
              int32_t r = v + g;
              int32_t b = u + g;
              c0[j] = std::clamp(r + si[0].shift, si[0].min, si[0].max);
              c1[j] = std::clamp(g + si[1].shift, si[1].min, si[1].max);
              c2[j] = std::clamp(b + si[2].shift, si[2].min, si[2].max);
            }
          }));
        }
      } // end 32-bit else
    }
    else
    {
      // irreversible inverse MCT (ICT) with DC shift on float buffers
      auto w0 = tile->comps_[0].getWindow()->getResWindowBufferHighestSimpleF();
      auto w1 = tile->comps_[1].getWindow()->getResWindowBufferHighestSimpleF();
      auto w2 = tile->comps_[2].getWindow()->getResWindowBufferHighestSimpleF();

      uint32_t stride = tile->comps_[0].getWindow()->getResWindowBufferHighestStride();
      uint32_t height = w0.height_;
      uint32_t numTasks = std::min(height, (uint32_t)FRBSingleton::num_threads());
      uint32_t rowsPerTask = height / numTasks;

      for(uint32_t t = 0; t < numTasks; ++t)
      {
        uint32_t yBegin = t * rowsPerTask;
        uint32_t yEnd = (t < numTasks - 1) ? (t + 1) * rowsPerTask : height;

        pool.submit(frb::task([this, w0, w1, w2, stride, yBegin, yEnd, si] {
          if(!success_)
            return;
          auto index = (uint64_t)yBegin * stride;
          auto count = (uint64_t)(yEnd - yBegin) * stride;
          auto c0 = w0.buf_ + index;
          auto c1 = w1.buf_ + index;
          auto c2 = w2.buf_ + index;
          auto i0 = reinterpret_cast<int32_t*>(c0);
          auto i1 = reinterpret_cast<int32_t*>(c1);
          auto i2 = reinterpret_cast<int32_t*>(c2);
          for(uint64_t j = 0; j < count; ++j)
          {
            float y = c0[j];
            float u = c1[j];
            float v = c2[j];
            float r = y + 1.402f * v;
            float g = y - 0.34413f * u - 0.71414f * v;
            float b = y + 1.772f * u;
            i0[j] = std::clamp((int32_t)lrintf(r) + si[0].shift, si[0].min, si[0].max);
            i1[j] = std::clamp((int32_t)lrintf(g) + si[1].shift, si[1].min, si[1].max);
            i2[j] = std::clamp((int32_t)lrintf(b) + si[2].shift, si[2].min, si[2].max);
          }
        }));
      }
    }

    pool.wait_idle();
    if(!success_)
      return false;
  }

  // handle standalone DC shift for non-MCT components
  // (MCT components had DC shift applied in MCT above)
  for(uint16_t compno = 0; compno < numcomps_; ++compno)
  {
    if(!tileProcessor->shouldDecodeComponent(compno))
      continue;

    // skip MCT components — already handled above
    if(tileProcessor->needsMctDecompress(compno) && tcp->mct_ == 1)
      continue;

    auto tilec = tileProcessor->getTile()->comps_ + compno;
    auto tccp = tcp->tccps_ + compno;
    uint8_t numRes = tilec->nextPacketProgressionState_.numResolutionsRead();

    bool waveletFusedDc = (numRes > 1 && wholeDecompress);
    if(waveletFusedDc)
      continue; // DC shift already applied in DWT

    // standalone DC shift for single-resolution or non-whole-tile cases
    if(tccp->dcLevelShift_ != 0)
    {
      auto img_comp = tileProcessor->getHeaderImage()->comps + compno;
      int32_t shift = tccp->dcLevelShift_;
      int32_t dcMin, dcMax;
      if(img_comp->sgnd)
      {
        dcMin = -(1 << (img_comp->prec - 1));
        dcMax = (1 << (img_comp->prec - 1)) - 1;
      }
      else
      {
        dcMin = 0;
        dcMax = (1 << img_comp->prec) - 1;
      }

      if(tccp->qmfbid_ == 1)
      {
        // reversible: int32_t buffer
        auto w = tilec->getWindow()->getResWindowBufferHighestSimple();
        uint32_t stride = tilec->getWindow()->getResWindowBufferHighestStride();
        uint64_t total = (uint64_t)w.height_ * stride;
        auto buf = w.buf_;
        pool.submit(frb::task([this, buf, total, shift, dcMin, dcMax] {
          if(!success_)
            return;
          for(uint64_t j = 0; j < total; ++j)
            buf[j] = std::clamp(buf[j] + shift, dcMin, dcMax);
        }));
      }
      else
      {
        // irreversible: float buffer → int32_t output
        auto w = tilec->getWindow()->getResWindowBufferHighestSimpleF();
        uint32_t stride = tilec->getWindow()->getResWindowBufferHighestStride();
        uint64_t total = (uint64_t)w.height_ * stride;
        auto fbuf = w.buf_;
        auto ibuf = reinterpret_cast<int32_t*>(fbuf);
        pool.submit(frb::task([this, fbuf, ibuf, total, shift, dcMin, dcMax] {
          if(!success_)
            return;
          for(uint64_t j = 0; j < total; ++j)
            ibuf[j] = std::clamp((int32_t)lrintf(fbuf[j]) + shift, dcMin, dcMax);
        }));
      }
    }
  }

  pool.wait_idle();
  return success_.load();
}

} // namespace grk
