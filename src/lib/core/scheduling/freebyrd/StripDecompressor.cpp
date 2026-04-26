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
#include "StripDecompressor.h"

namespace grk
{

// ─── construction / teardown ─────────────────────────────────────────────────

StripDecompressor::StripDecompressor(ITileProcessor* tp, uint16_t compno,
                                     uint8_t prec, std::atomic_bool& success,
                                     StripConfig config)
    : tp_(tp), compno_(compno), prec_(prec), success_(success), config_(config)
{
  auto tcp = tp_->getTCP();
  auto tccp = tcp->tccps_ + compno;
  auto tilec = tp_->getTile()->comps_ + compno;

  numRes_ = tilec->nextPacketProgressionState_.numResolutionsRead();
  qmfbid_ = tccp->qmfbid_;
  is16Bit_ = tilec->is16BitDwt();
  elemSize_ = is16Bit_ ? sizeof(int16_t) : (qmfbid_ == 0 ? sizeof(float) : sizeof(int32_t));

  // set up T1 coder pool
  uint32_t num_threads = (uint32_t)FRBSingleton::num_threads();
  uint16_t cbw = tccp->cblkw_expn_ ? (uint16_t)1 << tccp->cblkw_expn_ : 0;
  uint16_t cbh = tccp->cblkh_expn_ ? (uint16_t)1 << tccp->cblkh_expn_ : 0;
  coderPool_.makeCoders(
      num_threads, tccp->cblkw_expn_, tccp->cblkh_expn_,
      [tcp, cbw, cbh, tp]() -> std::shared_ptr<t1::ICoder> {
        return std::shared_ptr<t1::ICoder>(t1::CoderFactory::makeCoder(
            tcp->isHT(), false, cbw, cbh, tp->getTileCacheStrategy()));
      });

  initLevelInfo();
  buildBlockIndex();
  computeRefCounts();
  preAllocateBuffers();
}

StripDecompressor::~StripDecompressor() = default;

// ─── init helpers ────────────────────────────────────────────────────────────

void StripDecompressor::initLevelInfo()
{
  auto tilec = tp_->getTile()->comps_ + compno_;
  auto tr = tilec->resolutions_;

  levels_.resize(numRes_);

  uint32_t prevW = 0, prevH = 0;
  for(uint8_t r = 0; r < numRes_; ++r)
  {
    auto& li = levels_[r];
    auto res = tr + r;
    li.width = res->width();
    li.height = res->height();
    li.prevWidth = prevW;
    li.prevHeight = prevH;

    if(r > 0)
    {
      li.h_sn = prevW;
      li.h_dn = li.width - prevW;
      li.h_parity = res->x0 & 1;
      li.v_sn = prevH;
      li.v_dn = li.height - prevH;
      li.v_parity = res->y0 & 1;
    }

    prevW = li.width;
    prevH = li.height;
  }
}

void StripDecompressor::buildBlockIndex()
{
  auto tcp = tp_->getTCP();
  auto tilec = tp_->getTile()->comps_ + compno_;
  auto tccp = tcp->tccps_ + compno_;

  blockIndex_.resize(numRes_);

  for(uint8_t resno = 0; resno < numRes_; ++resno)
  {
    auto res = tilec->resolutions_ + resno;
    for(uint8_t bandIndex = 0; bandIndex < res->numBands_; ++bandIndex)
    {
      auto band = res->band + bandIndex;
      uint8_t orient = band->orientation_;

      for(auto precinct : band->precincts_)
      {
        if(precinct->empty())
          continue;
        for(uint32_t cblkno = 0; cblkno < precinct->getNumCblks(); ++cblkno)
        {
          auto cblk = precinct->getDecompressBlock(cblkno);
          auto cblkBounds = precinct->getCodeBlockBounds(cblkno);

          // band-relative row range
          uint32_t relY0 = cblkBounds.y0() - band->y0;
          uint32_t relY1 = relY0 + cblkBounds.height();
          // band-relative column range
          uint32_t relX0 = cblkBounds.x0() - band->x0;

          auto block = std::make_shared<t1::DecompressBlockExec>(false);
          block->x = cblk->x0();
          block->y = cblk->y0();
          block->bandIndex = bandIndex;
          block->bandNumbps = band->maxBitPlanes_;
          block->bandOrientation = (t1::eBandOrientation)orient;
          block->cblk = cblk;
          block->cblk_sty = tccp->cblkStyle_;
          block->qmfbid = qmfbid_;
          block->resno = resno;
          block->roishift = tccp->roishift_;
          block->stepsize = band->stepsize_;
          block->k_msbs = (uint8_t)(band->maxBitPlanes_ - cblk->numbps());
          block->R_b = prec_ + gain_b[orient];
          block->finalLayer_ = (tcp->layersToDecompress_ == tcp->numLayers_);

          auto& br = blockIndex_[resno][orient].emplace_back();
          br.block = std::move(block);
          br.bandRelX0 = relX0;
          br.bandRelY0 = relY0;
          br.bandRelY1 = relY1;
          br.blockWidth = cblkBounds.width();
          br.blockHeight = cblkBounds.height();
        }
      }
    }

    // sort each band's blocks by relY0 for efficient range queries
    for(auto& vec : blockIndex_[resno])
      std::sort(vec.begin(), vec.end(),
                [](const BlockRef& a, const BlockRef& b) { return a.bandRelY0 < b.bandRelY0; });
  }
}

// ─── compute reference counts ────────────────────────────────────────────────

void StripDecompressor::computeRefCounts()
{
  if(numRes_ == 0)
    return;

  auto& highestLevel = levels_[numRes_ - 1];
  uint32_t outH = highestLevel.height;
  if(outH == 0)
    return;

  uint32_t stripHeight = config_.outputStripHeight;
  uint32_t halo = (qmfbid_ == 0) ? 4 : 2;
  uint32_t haloInterleaved = 2 * halo;

  // For each output strip, compute which sub-band row ranges are needed
  // at each resolution (same logic as produceRows), and increment refCount
  // for every block that overlaps.
  //
  // We walk the strip list recursively to get the sub-band ranges at every
  // resolution. Rather than full recursion, we use the same halo math as
  // produceRows to compute the sub-band ranges that each highest-res strip
  // requires at each lower resolution, then iterate over blocks.

  // helper: given an output strip [y0, y1) at a resolution, compute
  // the sub-band row ranges needed (including halo)
  struct BandRanges
  {
    SubbandRange rangeL, rangeH;
  };

  // For each output strip at the highest resolution, walk down the
  // resolution pyramid to determine sub-band row ranges at each level,
  // and increment refCount for overlapping blocks.
  for(uint32_t stripY0 = 0; stripY0 < outH; stripY0 += stripHeight)
  {
    uint32_t stripY1 = std::min(stripY0 + stripHeight, outH);

    // Start at the highest resolution and compute needed ranges at each level
    // by simulating the recursive produceRows halo expansion
    std::vector<BandRanges> neededRanges(numRes_);

    // At the highest resolution, the output range is [stripY0, stripY1)
    uint32_t curY0 = stripY0;
    uint32_t curY1 = stripY1;

    for(int8_t r = numRes_ - 1; r >= 0; --r)
    {
      auto& li = levels_[r];
      curY0 = std::min(curY0, li.height);
      curY1 = std::min(curY1, li.height);
      if(curY0 >= curY1)
        break;

      if(r == 0)
      {
        // resolution 0: only LL band (orient=0)
        neededRanges[r].rangeL = {curY0, curY1};
        neededRanges[r].rangeH = {0, 0};
      }
      else
      {
        // compute extended range with halo
        uint32_t extFirst = (curY0 >= haloInterleaved) ? curY0 - haloInterleaved : 0;
        uint32_t extLast = std::min(curY1 - 1 + haloInterleaved, li.height - 1);

        SubbandRange rangeL, rangeH;
        if(li.v_parity == 0)
        {
          rangeL.lo = (extFirst + 1) / 2;
          rangeL.hi = extLast / 2 + 1;
          rangeH.lo = extFirst / 2;
          rangeH.hi = (extLast + 1) / 2;
        }
        else
        {
          rangeH.lo = (extFirst + 1) / 2;
          rangeH.hi = extLast / 2 + 1;
          rangeL.lo = extFirst / 2;
          rangeL.hi = (extLast + 1) / 2;
        }
        rangeL.hi = std::min(rangeL.hi, li.v_sn);
        rangeH.hi = std::min(rangeH.hi, li.v_dn);

        neededRanges[r].rangeL = rangeL;
        neededRanges[r].rangeH = rangeH;

        // LL at this resolution comes from the previous resolution's output
        // range [rangeL.lo, rangeL.hi)
        curY0 = rangeL.lo;
        curY1 = rangeL.hi;
      }
    }

    // Now increment refCount for all blocks overlapping these ranges
    for(uint8_t r = 0; r < numRes_; ++r)
    {
      auto& ranges = neededRanges[r];
      if(r == 0)
      {
        // LL band at res 0
        if(ranges.rangeL.count() > 0)
        {
          for(auto& br : blockIndex_[0][t1::BAND_ORIENT_LL])
          {
            if(br.bandRelY1 > ranges.rangeL.lo && br.bandRelY0 < ranges.rangeL.hi)
              br.refCount.fetch_add(1, std::memory_order_relaxed);
          }
        }
      }
      else
      {
        // HL band uses rangeL, LH and HH use rangeH
        if(ranges.rangeL.count() > 0)
        {
          for(auto& br : blockIndex_[r][t1::BAND_ORIENT_HL])
          {
            if(br.bandRelY1 > ranges.rangeL.lo && br.bandRelY0 < ranges.rangeL.hi)
              br.refCount.fetch_add(1, std::memory_order_relaxed);
          }
        }
        if(ranges.rangeH.count() > 0)
        {
          for(auto& br : blockIndex_[r][t1::BAND_ORIENT_LH])
          {
            if(br.bandRelY1 > ranges.rangeH.lo && br.bandRelY0 < ranges.rangeH.hi)
              br.refCount.fetch_add(1, std::memory_order_relaxed);
          }
          for(auto& br : blockIndex_[r][t1::BAND_ORIENT_HH])
          {
            if(br.bandRelY1 > ranges.rangeH.lo && br.bandRelY0 < ranges.rangeH.hi)
              br.refCount.fetch_add(1, std::memory_order_relaxed);
          }
        }
      }
    }
  }
}

// ─── pre-allocate reusable strip buffers ─────────────────────────────────────

void StripDecompressor::preAllocateBuffers()
{
  if(numRes_ == 0)
    return;

  levelBufs_.resize(numRes_);

  uint32_t stripHeight = config_.outputStripHeight;
  uint32_t halo = (qmfbid_ == 0) ? 4 : 2;
  uint32_t haloInterleaved = 2 * halo;

  // walk the resolution pyramid to compute worst-case subband row counts
  uint32_t maxRange = stripHeight;
  for(int8_t r = numRes_ - 1; r >= 0; --r)
  {
    auto& li = levels_[r];
    maxRange = std::min(maxRange, li.height);

    if(r == 0)
      continue;

    // worst-case extended interleaved range at this level
    uint32_t maxExtended = maxRange + 2 * haloInterleaved;
    uint32_t maxRL = std::min((maxExtended + 1) / 2 + 1, li.v_sn);
    uint32_t maxRH = std::min((maxExtended + 1) / 2 + 1, li.v_dn);

    auto& bufs = levelBufs_[r];
    uint32_t llW = li.prevWidth;
    uint32_t hlW = li.h_dn;
    uint32_t lhW = li.prevWidth;
    uint32_t hhW = li.h_dn;
    uint32_t resWidth = li.width;

    bufs.ll.resize((size_t)llW * maxRL * elemSize_);
    bufs.hl.resize((size_t)hlW * maxRL * elemSize_);
    bufs.lh.resize((size_t)lhW * maxRH * elemSize_);
    bufs.hh.resize((size_t)hhW * maxRH * elemSize_);

    bufs.splitL.resize((size_t)resWidth * maxRL * elemSize_);
    bufs.splitH.resize((size_t)resWidth * maxRH * elemSize_);
    bufs.vOut.resize((size_t)resWidth * (maxRL + maxRH) * elemSize_);

    maxRange = maxRL;
  }
}

// ─── binary search for overlapping blocks ────────────────────────────────────

std::pair<size_t, size_t> StripDecompressor::findBlockRange(
    const std::vector<BlockRef>& blocks, uint32_t rowStart, uint32_t rowEnd)
{
  // blocks sorted by bandRelY0
  auto lower = std::lower_bound(blocks.begin(), blocks.end(), rowStart,
      [](const BlockRef& br, uint32_t val) { return br.bandRelY1 <= val; });
  auto upper = std::lower_bound(lower, blocks.end(), rowEnd,
      [](const BlockRef& br, uint32_t val) { return br.bandRelY0 < val; });
  return {(size_t)(lower - blocks.begin()), (size_t)(upper - blocks.begin())};
}

// ─── main entry point ────────────────────────────────────────────────────────

bool StripDecompressor::decompress()
{
  if(numRes_ == 0)
    return true;

  auto tilec = tp_->getTile()->comps_ + compno_;
  auto& highestLevel = levels_[numRes_ - 1];
  uint32_t outW = highestLevel.width;
  uint32_t outH = highestLevel.height;

  if(outW == 0 || outH == 0)
    return true;

  // determine output buffer and stride from the existing tile buffer
  // (Phase 3a: write into the tile's allocated buffer for bit-exact comparison)
  uint32_t outStride = 0;
  void* outData = nullptr;
  if(is16Bit_)
  {
    auto buf = tilec->getWindow16();
    auto highestBuf = buf->getResWindowBufferHighestSimple();
    outData = highestBuf.buf_;
    outStride = highestBuf.stride_;
  }
  else
  {
    auto buf = tilec->getWindow();
    auto highestBuf = buf->getResWindowBufferHighestSimple();
    outData = highestBuf.buf_;
    outStride = highestBuf.stride_;
  }

  if(!outData)
  {
    grklog.error("StripDecompressor: no output buffer allocated");
    return false;
  }

  auto& pool = FRBSingleton::get();

  // process output strips from top to bottom
  uint32_t stripHeight = config_.outputStripHeight;
  size_t elemSize = is16Bit_ ? sizeof(int16_t) : (qmfbid_ == 0 ? sizeof(float) : sizeof(int32_t));
  for(uint32_t y0 = 0; y0 < outH; y0 += stripHeight)
  {
    uint32_t y1 = std::min(y0 + stripHeight, outH);

    // lazy decode: submit T1 tasks for blocks needed by this strip
    decodeBlocksForStrip(numRes_ - 1, y0, y1);
    pool.wait_idle();
    if(!success_)
      return false;

    // pre-offset destBuf so row y0 maps to offset 0
    void* stripDest = (uint8_t*)outData + (size_t)y0 * outStride * elemSize;
    if(!produceRows(numRes_ - 1, y0, y1, stripDest, outStride))
      return false;

    if(!success_)
      return false;

    // release blocks no longer needed
    releaseBlocksForStrip(numRes_ - 1, y0, y1);
  }

  return true;
}

bool StripDecompressor::decompressStream(StripOutputCallback outputCallback)
{
  if(numRes_ == 0)
    return true;

  auto& highestLevel = levels_[numRes_ - 1];
  uint32_t outW = highestLevel.width;
  uint32_t outH = highestLevel.height;

  if(outW == 0 || outH == 0)
    return true;

  auto& pool = FRBSingleton::get();

  uint32_t stripHeight = config_.outputStripHeight;
  size_t elemSize = is16Bit_ ? sizeof(int16_t) : (qmfbid_ == 0 ? sizeof(float) : sizeof(int32_t));
  size_t stripBytes = (size_t)outW * stripHeight * elemSize;
  std::vector<uint8_t> stripBuf(stripBytes, 0);

  for(uint32_t y0 = 0; y0 < outH; y0 += stripHeight)
  {
    uint32_t y1 = std::min(y0 + stripHeight, outH);
    uint32_t rows = y1 - y0;

    // lazy decode: submit T1 tasks for blocks needed by this strip
    decodeBlocksForStrip(numRes_ - 1, y0, y1);
    pool.wait_idle();
    if(!success_)
      return false;

    // produce DWT output into strip buffer
    memset(stripBuf.data(), 0, (size_t)outW * rows * elemSize);
    if(!produceRows(numRes_ - 1, y0, y1, stripBuf.data(), outW))
      return false;
    if(!success_)
      return false;

    // deliver strip to callback
    if(outputCallback)
      outputCallback(y0, rows, stripBuf.data(), outW);

    // release blocks no longer needed
    releaseBlocksForStrip(numRes_ - 1, y0, y1);
  }

  return true;
}

// ─── recursive strip production ──────────────────────────────────────────────

bool StripDecompressor::produceRows(uint8_t resno, uint32_t y0, uint32_t y1,
                                    void* destBuf, uint32_t destStride)
{
  if(!success_)
    return false;

  auto& li = levels_[resno];

  // clamp to resolution bounds
  y0 = std::min(y0, li.height);
  y1 = std::min(y1, li.height);
  if(y0 >= y1)
    return true;

  if(resno == 0)
  {
    // resolution 0: only LL band, copy decoded T1 data directly into output
    copyBlocksToBand(resno, t1::BAND_ORIENT_LL, y0, y1, destBuf, destStride, y0);
    return success_.load();
  }

  // For resno > 0: need LL from previous level + detail bands from T1
  // then run DWT to produce output

  // compute V-DWT halo (interleaved rows)
  uint32_t halo = (qmfbid_ == 0) ? 4 : 2; // 9/7 needs 4, 5/3 needs 2
  uint32_t haloInterleaved = 2 * halo;

  // extended interleaved range with halo
  uint32_t extFirst = (y0 >= haloInterleaved) ? y0 - haloInterleaved : 0;
  uint32_t extLast = std::min(y1 - 1 + haloInterleaved, li.height - 1);

  // map extended range to sub-band row ranges
  SubbandRange rangeL, rangeH;
  if(li.v_parity == 0)
  {
    rangeL.lo = (extFirst + 1) / 2;
    rangeL.hi = extLast / 2 + 1;
    rangeH.lo = extFirst / 2;
    rangeH.hi = (extLast + 1) / 2;
  }
  else
  {
    rangeH.lo = (extFirst + 1) / 2;
    rangeH.hi = extLast / 2 + 1;
    rangeL.lo = extFirst / 2;
    rangeL.hi = (extLast + 1) / 2;
  }
  rangeL.hi = std::min(rangeL.hi, li.v_sn);
  rangeH.hi = std::min(rangeH.hi, li.v_dn);

  // allocate strip band buffers
  uint32_t llHeight = rangeL.count();
  uint32_t lhHeight = rangeH.count();
  uint32_t hlHeight = rangeL.count();
  uint32_t hhHeight = rangeH.count();

  // LL half-width = prevWidth, detail half-widths
  uint32_t llW = li.prevWidth;
  uint32_t hlW = li.h_dn;
  uint32_t lhW = li.prevWidth;
  uint32_t hhW = li.h_dn;

  // use pre-allocated sub-band strip buffers (memset instead of vector alloc)
  auto& bufs = levelBufs_[resno];
  size_t llSize = (size_t)llW * llHeight * elemSize_;
  size_t hlSize = (size_t)hlW * hlHeight * elemSize_;
  size_t lhSize = (size_t)lhW * lhHeight * elemSize_;
  size_t hhSize = (size_t)hhW * hhHeight * elemSize_;
  memset(bufs.ll.data(), 0, llSize);
  memset(bufs.hl.data(), 0, hlSize);
  memset(bufs.lh.data(), 0, lhSize);
  memset(bufs.hh.data(), 0, hhSize);

  // recursively produce LL data from previous resolution
  // LL rows [rangeL.lo, rangeL.hi) at resolution resno correspond to
  // output rows [rangeL.lo, rangeL.hi) at resolution resno-1
  if(!produceRows(resno - 1, rangeL.lo, rangeL.hi, bufs.ll.data(), llW))
    return false;

  // copy decoded T1 data for detail bands (strides are in elements)
  copyBlocksToBand(resno, t1::BAND_ORIENT_HL, rangeL.lo, rangeL.hi,
                   bufs.hl.data(), hlW, rangeL.lo);
  copyBlocksToBand(resno, t1::BAND_ORIENT_LH, rangeH.lo, rangeH.hi,
                   bufs.lh.data(), lhW, rangeH.lo);
  copyBlocksToBand(resno, t1::BAND_ORIENT_HH, rangeH.lo, rangeH.hi,
                   bufs.hh.data(), hhW, rangeH.lo);

  // run DWT synthesis
  if(qmfbid_ == 0 && !is16Bit_)
  {
    return synthesizeStrip97(resno, y0, y1,
                             Buffer2dSimple<float>((float*)bufs.ll.data(), llW, llHeight),
                             Buffer2dSimple<float>((float*)bufs.hl.data(), hlW, hlHeight),
                             Buffer2dSimple<float>((float*)bufs.lh.data(), lhW, lhHeight),
                             Buffer2dSimple<float>((float*)bufs.hh.data(), hhW, hhHeight),
                             rangeL, rangeH, extFirst,
                             destBuf, destStride);
  }
  else if(is16Bit_)
  {
    return synthesizeStrip16(resno, y0, y1,
                             Buffer2dSimple<int16_t>((int16_t*)bufs.ll.data(), llW, llHeight),
                             Buffer2dSimple<int16_t>((int16_t*)bufs.hl.data(), hlW, hlHeight),
                             Buffer2dSimple<int16_t>((int16_t*)bufs.lh.data(), lhW, lhHeight),
                             Buffer2dSimple<int16_t>((int16_t*)bufs.hh.data(), hhW, hhHeight),
                             rangeL, rangeH, extFirst,
                             destBuf, destStride);
  }
  else
  {
    return synthesizeStrip53(resno, y0, y1,
                             Buffer2dSimple<int32_t>((int32_t*)bufs.ll.data(), llW, llHeight),
                             Buffer2dSimple<int32_t>((int32_t*)bufs.hl.data(), hlW, hlHeight),
                             Buffer2dSimple<int32_t>((int32_t*)bufs.lh.data(), lhW, lhHeight),
                             Buffer2dSimple<int32_t>((int32_t*)bufs.hh.data(), hhW, hhHeight),
                             rangeL, rangeH, extFirst,
                             destBuf, destStride);
  }
}

// ─── lazy per-strip T1 decode ────────────────────────────────────────────────

// Decode only the blocks needed for a strip at the highest resolution.
// Walks the resolution pyramid (same as produceRows) to find needed sub-band
// ranges, then decodes any blocks that haven't been decoded yet.
void StripDecompressor::decodeBlocksForStrip(uint8_t resno, uint32_t y0, uint32_t y1)
{
  if(!success_)
    return;

  auto& li = levels_[resno];
  y0 = std::min(y0, li.height);
  y1 = std::min(y1, li.height);
  if(y0 >= y1)
    return;

  auto& pool = FRBSingleton::get();
  auto tcp = tp_->getTCP();
  auto tccp = tcp->tccps_ + compno_;
  auto tccp_w = tccp->cblkw_expn_;
  auto tccp_h = tccp->cblkh_expn_;

  // helper: submit undecoded blocks in [rowStart, rowEnd) for the given orient
  auto submitBlocks = [&](uint8_t res, uint8_t orient, uint32_t rowStart, uint32_t rowEnd) {
    auto& blocks = blockIndex_[res][orient];
    auto [lo, hi] = findBlockRange(blocks, rowStart, rowEnd);
    for(size_t i = lo; i < hi; ++i)
    {
      auto& br = blocks[i];
      // only decode if not already decoded
      if(br.decoded.load(std::memory_order_acquire))
        continue;

      auto block = br.block;
      auto blockW = br.blockWidth;
      auto blockH = br.blockHeight;
      auto* brPtr = &br;

      // set up postProcessor to capture decoded data
      block->postProcessor_ =
          t1::DecompressBlockPostProcessor<int32_t>(
              [brPtr, blockW, blockH](int32_t* srcData, t1::DecompressBlockExec* blk,
                                    uint16_t stride) {
                if(blk->cblk->dataChunksEmpty())
                {
                  brPtr->decoded.store(true, std::memory_order_release);
                  return;
                }
                uint16_t srcStride = stride ? stride : (uint16_t)blockW;
                brPtr->decodedStride = srcStride;
                brPtr->decodedData.resize((size_t)srcStride * blockH);
                memcpy(brPtr->decodedData.data(), srcData,
                       (size_t)srcStride * blockH * sizeof(int32_t));
                brPtr->decoded.store(true, std::memory_order_release);
                // release compressed cblk data — no longer needed after decode
                blk->cblk->release();
              });

      pool.submit(frb::task([this, block, tccp_w, tccp_h] {
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
  };

  if(resno == 0)
  {
    // LL band at resolution 0
    submitBlocks(0, t1::BAND_ORIENT_LL, y0, y1);
  }
  else
  {
    uint32_t halo = (qmfbid_ == 0) ? 4 : 2;
    uint32_t haloInterleaved = 2 * halo;

    uint32_t extFirst = (y0 >= haloInterleaved) ? y0 - haloInterleaved : 0;
    uint32_t extLast = std::min(y1 - 1 + haloInterleaved, li.height - 1);

    SubbandRange rangeL, rangeH;
    if(li.v_parity == 0)
    {
      rangeL.lo = (extFirst + 1) / 2;
      rangeL.hi = extLast / 2 + 1;
      rangeH.lo = extFirst / 2;
      rangeH.hi = (extLast + 1) / 2;
    }
    else
    {
      rangeH.lo = (extFirst + 1) / 2;
      rangeH.hi = extLast / 2 + 1;
      rangeL.lo = extFirst / 2;
      rangeL.hi = (extLast + 1) / 2;
    }
    rangeL.hi = std::min(rangeL.hi, li.v_sn);
    rangeH.hi = std::min(rangeH.hi, li.v_dn);

    // submit detail band blocks for this resolution
    submitBlocks(resno, t1::BAND_ORIENT_HL, rangeL.lo, rangeL.hi);
    submitBlocks(resno, t1::BAND_ORIENT_LH, rangeH.lo, rangeH.hi);
    submitBlocks(resno, t1::BAND_ORIENT_HH, rangeH.lo, rangeH.hi);

    // recurse: decode blocks needed for LL at the previous resolution
    decodeBlocksForStrip((uint8_t)(resno - 1), rangeL.lo, rangeL.hi);
  }
}

// ─── release blocks after strip consumption ──────────────────────────────────

void StripDecompressor::releaseBlocksForStrip(uint8_t resno, uint32_t y0, uint32_t y1)
{
  auto& li = levels_[resno];
  y0 = std::min(y0, li.height);
  y1 = std::min(y1, li.height);
  if(y0 >= y1)
    return;

  // helper: decrement refCount and free if zero (binary search)
  auto releaseBlocks = [](std::vector<BlockRef>& blocks, uint32_t rowStart, uint32_t rowEnd) {
    auto [lo, hi] = findBlockRange(blocks, rowStart, rowEnd);
    for(size_t i = lo; i < hi; ++i)
    {
      auto& br = blocks[i];
      auto prev = br.refCount.fetch_sub(1, std::memory_order_acq_rel);
      if(prev <= 1)
      {
        // last consumer — free the decoded data
        br.decodedData.clear();
        br.decodedData.shrink_to_fit();
      }
    }
  };

  if(resno == 0)
  {
    releaseBlocks(blockIndex_[0][t1::BAND_ORIENT_LL], y0, y1);
  }
  else
  {
    uint32_t halo = (qmfbid_ == 0) ? 4 : 2;
    uint32_t haloInterleaved = 2 * halo;

    uint32_t extFirst = (y0 >= haloInterleaved) ? y0 - haloInterleaved : 0;
    uint32_t extLast = std::min(y1 - 1 + haloInterleaved, li.height - 1);

    SubbandRange rangeL, rangeH;
    if(li.v_parity == 0)
    {
      rangeL.lo = (extFirst + 1) / 2;
      rangeL.hi = extLast / 2 + 1;
      rangeH.lo = extFirst / 2;
      rangeH.hi = (extLast + 1) / 2;
    }
    else
    {
      rangeH.lo = (extFirst + 1) / 2;
      rangeH.hi = extLast / 2 + 1;
      rangeL.lo = extFirst / 2;
      rangeL.hi = (extLast + 1) / 2;
    }
    rangeL.hi = std::min(rangeL.hi, li.v_sn);
    rangeH.hi = std::min(rangeH.hi, li.v_dn);

    releaseBlocks(blockIndex_[resno][t1::BAND_ORIENT_HL], rangeL.lo, rangeL.hi);
    releaseBlocks(blockIndex_[resno][t1::BAND_ORIENT_LH], rangeH.lo, rangeH.hi);
    releaseBlocks(blockIndex_[resno][t1::BAND_ORIENT_HH], rangeH.lo, rangeH.hi);

    // recurse: release blocks at the previous resolution for LL
    releaseBlocksForStrip((uint8_t)(resno - 1), rangeL.lo, rangeL.hi);
  }
}

// ─── copy decoded block data to strip band buffer ────────────────────────────

void StripDecompressor::copyBlocksToBand(uint8_t resno, uint8_t orient,
                                         uint32_t rowStart, uint32_t rowEnd,
                                         void* bandBuf, uint32_t bandBufStride,
                                         uint32_t bandBufRowOffset)
{
  auto& blocks = blockIndex_[resno][orient];
  auto [lo, hi] = findBlockRange(blocks, rowStart, rowEnd);

  for(size_t idx = lo; idx < hi; ++idx)
  {
    auto& br = blocks[idx];

    // skip blocks with no decoded data (empty codeblocks)
    if(br.decodedData.empty())
      continue;

    uint32_t blockLocalY = (br.bandRelY0 >= bandBufRowOffset)
                               ? br.bandRelY0 - bandBufRowOffset
                               : 0;
    uint32_t srcRowSkip = (br.bandRelY0 < bandBufRowOffset)
                              ? bandBufRowOffset - br.bandRelY0
                              : 0;
    uint32_t copyHeight = std::min(br.blockHeight - srcRowSkip,
                                   rowEnd - std::max(br.bandRelY0, rowStart));

    uint32_t blockX = br.bandRelX0;
    uint32_t blockW = br.blockWidth;
    uint16_t srcStride = br.decodedStride;
    auto srcData = br.decodedData.data();
    float stepsize = br.block->stepsize;

    if(is16Bit_)
    {
      auto destBase = (int16_t*)bandBuf;
      for(uint32_t row = 0; row < copyHeight; ++row)
      {
        auto src = srcData + (row + srcRowSkip) * srcStride;
        auto dst = destBase + (blockLocalY + row) * bandBufStride + blockX;
        if(qmfbid_ == 0)
        {
          float scale = stepsize / 2.0f;
          for(uint32_t i = 0; i < blockW; ++i)
          {
            float val = (float)src[i] * scale;
            int32_t rounded = (int32_t)(val >= 0 ? val + 0.5f : val - 0.5f);
            if(rounded > 32767) rounded = 32767;
            else if(rounded < -32768) rounded = -32768;
            dst[i] = (int16_t)rounded;
          }
        }
        else
        {
          for(uint32_t i = 0; i < blockW; ++i)
            dst[i] = (int16_t)(src[i] / 2);
        }
      }
    }
    else if(qmfbid_ == 0)
    {
      auto destBase = (float*)bandBuf;
      float scale = stepsize / 2.0f;
      for(uint32_t row = 0; row < copyHeight; ++row)
      {
        auto src = srcData + (row + srcRowSkip) * srcStride;
        auto dst = destBase + (blockLocalY + row) * bandBufStride + blockX;
        for(uint32_t i = 0; i < blockW; ++i)
          dst[i] = (float)src[i] * scale;
      }
    }
    else
    {
      auto destBase = (int32_t*)bandBuf;
      for(uint32_t row = 0; row < copyHeight; ++row)
      {
        auto src = srcData + (row + srcRowSkip) * srcStride;
        auto dst = destBase + (blockLocalY + row) * bandBufStride + blockX;
        for(uint32_t i = 0; i < blockW; ++i)
          dst[i] = src[i] / 2;
      }
    }
  }
}

// ─── DWT synthesis implementations ───────────────────────────────────────────

bool StripDecompressor::synthesizeStrip53(uint8_t resno, uint32_t outY0, uint32_t outY1,
                                          Buffer2dSimple<int32_t> winLL,
                                          Buffer2dSimple<int32_t> winHL,
                                          Buffer2dSimple<int32_t> winLH,
                                          Buffer2dSimple<int32_t> winHH,
                                          SubbandRange rangeL, SubbandRange rangeH,
                                          uint32_t extFirst,
                                          void* destBuf, uint32_t destStride)
{
  auto& pool = FRBSingleton::get();
  auto& li = levels_[resno];
  auto tilec = tp_->getTile()->comps_ + compno_;
  auto tcp = tp_->getTCP();
  auto tccp = tcp->tccps_ + compno_;

  // use pre-allocated SPLIT_L and SPLIT_H intermediate buffers
  uint32_t splitLHeight = rangeL.count();
  uint32_t splitHHeight = rangeH.count();
  uint32_t resWidth = li.width;

  auto& bufs = levelBufs_[resno];
  size_t splitLSize = (size_t)resWidth * splitLHeight * sizeof(int32_t);
  size_t splitHSize = (size_t)resWidth * splitHHeight * sizeof(int32_t);
  memset(bufs.splitL.data(), 0, splitLSize);
  memset(bufs.splitH.data(), 0, splitHSize);

  Buffer2dSimple<int32_t> splitL((int32_t*)bufs.splitL.data(), resWidth, splitLHeight);
  Buffer2dSimple<int32_t> splitH((int32_t*)bufs.splitH.data(), resWidth, splitHHeight);

  // create WaveletReverse to access h_strip_53 / v_strip_53
  bool wholeDecompress = tcp->wholeTileDecompress_;
  WaveletReverse wavelet(nullptr, tilec, compno_, tilec->windowUnreducedBounds(),
                         numRes_, qmfbid_,
                         std::max(tp_->getCodingParams()->t_width_,
                                  tp_->getCodingParams()->t_height_),
                         wholeDecompress, nullptr, {}, true);

  uint32_t maxDim = max_resolution(tilec->resolutions_, numRes_);

  // ---- H-DWT phase: LL+HL → SPLIT_L, LH+HH → SPLIT_H ----
  for(uint32_t orient = 0; orient < 2; ++orient)
  {
    uint32_t bandHeight = (orient == 0) ? splitLHeight : splitHHeight;
    if(bandHeight == 0)
      continue;

    Buffer2dSimple<int32_t> srcL, srcH, dst;
    if(orient == 0)
    {
      srcL = winLL;
      srcH = winHL;
      dst = splitL;
    }
    else
    {
      srcL = winLH;
      srcH = winHH;
      dst = splitH;
    }

    uint32_t numTasks = std::min(bandHeight, (uint32_t)FRBSingleton::num_threads());
    uint32_t heightIncr = bandHeight / numTasks;
    for(uint32_t j = 0; j < numTasks; ++j)
    {
      auto hMax = (j < numTasks - 1U) ? heightIncr : bandHeight - j * heightIncr;
      pool.submit(frb::task([this, &wavelet, hMax, srcL, srcH, dst, &li, maxDim] {
        if(!success_)
          return;

        dwt_scratch<int32_t> hScratch;
        hScratch.sn = li.h_sn;
        hScratch.dn = li.h_dn;
        hScratch.parity = li.h_parity;
        if(!hScratch.alloc(maxDim))
        {
          grklog.error("StripDecompressor: 5/3 hScratch alloc failed");
          success_ = false;
          return;
        }

        wavelet.h_strip_53(&hScratch, 0, hMax, srcL, srcH, dst);
      }));
      srcL.incY_IN_PLACE(heightIncr);
      srcH.incY_IN_PLACE(heightIncr);
      dst.incY_IN_PLACE(heightIncr);
    }
  }

  pool.wait_idle();
  if(!success_)
    return false;

  // ---- V-DWT phase: SPLIT_L + SPLIT_H → temp output ----
  uint32_t vOutHeight = splitLHeight + splitHHeight;
  size_t vOutSize = (size_t)resWidth * vOutHeight * sizeof(int32_t);
  memset(bufs.vOut.data(), 0, vOutSize);
  Buffer2dSimple<int32_t> vOut((int32_t*)bufs.vOut.data(), resWidth, vOutHeight);

  // compute DC shift for fusion into last-level V-DWT
  DcShiftParam dcShift;
  if(resno == numRes_ - 1 && wholeDecompress)
  {
    bool isMctComp = tp_->needsMctDecompress(compno_) && tcp->mct_ == 1;
    if(!isMctComp)
    {
      auto img_comp = tp_->getHeaderImage()->comps + compno_;
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

  uint32_t pllCols53 = get_PLL_COLS_53();
  size_t scratchElems = (size_t)maxDim * pllCols53;

  auto vSplitL = splitL;
  auto vSplitH = splitH;
  auto vDest = vOut;

  uint32_t numTasks = std::min(resWidth, (uint32_t)FRBSingleton::num_threads());
  uint32_t widthIncr = resWidth / numTasks;
  for(uint32_t j = 0; j < numTasks; ++j)
  {
    auto wMin = j * widthIncr;
    auto wMax = (j < numTasks - 1U) ? (j + 1U) * widthIncr : resWidth;
    pool.submit(frb::task([this, &wavelet, splitLHeight, splitHHeight,
                           wMin, wMax, vSplitL, vSplitH, vDest,
                           dcShift, scratchElems, extFirst, &li] {
      if(!success_)
        return;

      dwt_scratch<int32_t> vScratch;
      vScratch.sn = splitLHeight;
      vScratch.dn = splitHHeight;
      vScratch.parity = (extFirst & 1) ^ li.v_parity;
      if(!vScratch.alloc(scratchElems))
      {
        grklog.error("StripDecompressor: 5/3 vScratch alloc failed");
        success_ = false;
        return;
      }

      wavelet.v_strip_53(&vScratch, wMin, wMax, vSplitL, vSplitH, vDest, dcShift);
    }));
    vSplitL.incX_IN_PLACE(widthIncr);
    vSplitH.incX_IN_PLACE(widthIncr);
    vDest.incX_IN_PLACE(widthIncr);
  }

  pool.wait_idle();
  if(!success_)
    return false;

  // copy output rows [outY0, outY1) from vOut to destination buffer
  // destBuf already points to where row y0 should go (offset 0)
  uint32_t rowOffset = outY0 - extFirst;
  uint32_t outRows = outY1 - outY0;
  auto srcPtr = (int32_t*)bufs.vOut.data() + rowOffset * resWidth;
  auto dstPtr = (int32_t*)destBuf;
  for(uint32_t row = 0; row < outRows; ++row)
  {
    memcpy(dstPtr, srcPtr, resWidth * sizeof(int32_t));
    srcPtr += resWidth;
    dstPtr += destStride;
  }

  return true;
}

bool StripDecompressor::synthesizeStrip97(uint8_t resno, uint32_t outY0, uint32_t outY1,
                                          Buffer2dSimple<float> winLL,
                                          Buffer2dSimple<float> winHL,
                                          Buffer2dSimple<float> winLH,
                                          Buffer2dSimple<float> winHH,
                                          SubbandRange rangeL, SubbandRange rangeH,
                                          uint32_t extFirst,
                                          void* destBuf, uint32_t destStride)
{
  auto& pool = FRBSingleton::get();
  auto& li = levels_[resno];
  auto tilec = tp_->getTile()->comps_ + compno_;
  auto tcp = tp_->getTCP();
  auto tccp = tcp->tccps_ + compno_;

  uint32_t resWidth = li.width;
  uint32_t resHeight = rangeL.count() + rangeH.count();
  bool wholeDecompress = tcp->wholeTileDecompress_;

  // create WaveletReverse for cascade_strip_97
  WaveletReverse wavelet(nullptr, tilec, compno_, tilec->windowUnreducedBounds(),
                         numRes_, qmfbid_,
                         std::max(tp_->getCodingParams()->t_width_,
                                  tp_->getCodingParams()->t_height_),
                         wholeDecompress, nullptr, {}, true);

  size_t dataLength = max_resolution(tilec->resolutions_, numRes_);

  // DC shift for last resolution level
  DcShiftParam dcShift;
  if(resno == numRes_ - 1 && wholeDecompress)
  {
    bool isMctComp = tp_->needsMctDecompress(compno_) && tcp->mct_ == 1;
    if(!isMctComp)
    {
      auto img_comp = tp_->getHeaderImage()->comps + compno_;
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
      dcShift.enabled = (dcShift.shift != 0) || (qmfbid_ == 0);
    }
  }

  // use pre-allocated temp output buffer
  auto& bufs = levelBufs_[resno];
  size_t tempSize = (size_t)resWidth * resHeight * sizeof(float);
  memset(bufs.vOut.data(), 0, tempSize);
  Buffer2dSimple<float> tempDest((float*)bufs.vOut.data(), resWidth, resHeight);

  // strip's local parity
  uint32_t stripParity = (extFirst & 1) ^ li.v_parity;

  // submit a single strip DWT task
  pool.submit(frb::task([this, &wavelet, &li, resWidth, dataLength,
                         winLL, winHL, winLH, winHH, tempDest, dcShift,
                         rangeL, rangeH, stripParity, outY0, outY1, extFirst] {
    if(!success_)
      return;

    // allocate per-task DWT scratch buffers
    dwt_scratch<vec4f> hScratch;
    hScratch.sn = li.h_sn;
    hScratch.dn = li.h_dn;
    hScratch.parity = li.h_parity;
    hScratch.win_l = Line32(0, li.h_sn);
    hScratch.win_h = Line32(0, li.h_dn);
    if(!WaveletReverse::allocCascadeScratch97(hScratch, dataLength))
    {
      success_ = false;
      return;
    }

    dwt_scratch<vec4f> vScratch;
    uint32_t ext_sn = rangeL.count();
    uint32_t ext_dn = rangeH.count();
    vScratch.sn = ext_sn;
    vScratch.dn = ext_dn;
    vScratch.parity = stripParity;
    vScratch.win_l = Line32(0, ext_sn);
    vScratch.win_h = Line32(0, ext_dn);
    vScratch.outputStart = outY0 - extFirst;
    vScratch.outputCount = outY1 - outY0;
    if(!WaveletReverse::allocCascadeScratch97(vScratch, dataLength))
    {
      success_ = false;
      return;
    }

    wavelet.cascade_strip_97(&hScratch, &vScratch, resWidth,
                             winLL, winHL, winLH, winHH,
                             tempDest, dcShift);
  }));

  pool.wait_idle();
  if(!success_)
    return false;

  // copy output rows from tempDest to final destination
  uint32_t outRows = outY1 - outY0;
  auto srcPtr = (float*)bufs.vOut.data();
  auto dstPtr = (float*)destBuf;
  for(uint32_t row = 0; row < outRows; ++row)
  {
    memcpy(dstPtr, srcPtr, resWidth * sizeof(float));
    srcPtr += resWidth;
    dstPtr += destStride;
  }

  return true;
}

bool StripDecompressor::synthesizeStrip16(uint8_t resno, uint32_t outY0, uint32_t outY1,
                                          Buffer2dSimple<int16_t> winLL,
                                          Buffer2dSimple<int16_t> winHL,
                                          Buffer2dSimple<int16_t> winLH,
                                          Buffer2dSimple<int16_t> winHH,
                                          SubbandRange rangeL, SubbandRange rangeH,
                                          uint32_t extFirst,
                                          void* destBuf, uint32_t destStride)
{
  auto& pool = FRBSingleton::get();
  auto& li = levels_[resno];
  auto tilec = tp_->getTile()->comps_ + compno_;
  auto tcp = tp_->getTCP();
  auto tccp = tcp->tccps_ + compno_;

  // use pre-allocated SPLIT_L and SPLIT_H intermediate buffers
  uint32_t splitLHeight = rangeL.count();
  uint32_t splitHHeight = rangeH.count();
  uint32_t resWidth = li.width;

  auto& bufs = levelBufs_[resno];
  size_t splitLSize = (size_t)resWidth * splitLHeight * sizeof(int16_t);
  size_t splitHSize = (size_t)resWidth * splitHHeight * sizeof(int16_t);
  memset(bufs.splitL.data(), 0, splitLSize);
  memset(bufs.splitH.data(), 0, splitHSize);

  Buffer2dSimple<int16_t> splitL((int16_t*)bufs.splitL.data(), resWidth, splitLHeight);
  Buffer2dSimple<int16_t> splitH((int16_t*)bufs.splitH.data(), resWidth, splitHHeight);

  bool wholeDecompress = tcp->wholeTileDecompress_;
  WaveletReverse wavelet(nullptr, tilec, compno_, tilec->windowUnreducedBounds(),
                         numRes_, qmfbid_,
                         std::max(tp_->getCodingParams()->t_width_,
                                  tp_->getCodingParams()->t_height_),
                         wholeDecompress, nullptr, {}, true);

  uint32_t maxDim = max_resolution(tilec->resolutions_, numRes_);
  uint32_t pllCols = (qmfbid_ == 1) ? get_PLL_COLS_16_53() : get_PLL_COLS_16_97();
  size_t scratchElems = (size_t)maxDim * pllCols;

  // ---- H-DWT phase ----
  for(uint32_t orient = 0; orient < 2; ++orient)
  {
    uint32_t bandHeight = (orient == 0) ? splitLHeight : splitHHeight;
    if(bandHeight == 0)
      continue;

    Buffer2dSimple<int16_t> srcL, srcH, dst;
    if(orient == 0)
    {
      srcL = winLL;
      srcH = winHL;
      dst = splitL;
    }
    else
    {
      srcL = winLH;
      srcH = winHH;
      dst = splitH;
    }

    uint32_t numTasks = std::min(bandHeight, (uint32_t)FRBSingleton::num_threads());
    uint32_t heightIncr = bandHeight / numTasks;
    for(uint32_t j = 0; j < numTasks; ++j)
    {
      auto hMax = (j < numTasks - 1U) ? heightIncr : bandHeight - j * heightIncr;
      pool.submit(frb::task([this, &wavelet, hMax, srcL, srcH, dst, &li, maxDim] {
        if(!success_)
          return;

        dwt_scratch<int16_t> hScratch;
        hScratch.sn = li.h_sn;
        hScratch.dn = li.h_dn;
        hScratch.parity = li.h_parity;
        if(!hScratch.alloc(maxDim))
        {
          grklog.error("StripDecompressor: 16-bit hScratch alloc failed");
          success_ = false;
          return;
        }

        if(qmfbid_ == 1)
          wavelet.h_strip_16_53(&hScratch, 0, hMax, srcL, srcH, dst);
        else
          wavelet.h_strip_16_97(&hScratch, 0, hMax, srcL, srcH, dst);
      }));
      srcL.incY_IN_PLACE(heightIncr);
      srcH.incY_IN_PLACE(heightIncr);
      dst.incY_IN_PLACE(heightIncr);
    }
  }

  pool.wait_idle();
  if(!success_)
    return false;

  // ---- V-DWT phase ----
  uint32_t vOutHeight = splitLHeight + splitHHeight;
  size_t vOutSize16 = (size_t)resWidth * vOutHeight * sizeof(int16_t);
  memset(bufs.vOut.data(), 0, vOutSize16);
  Buffer2dSimple<int16_t> vOut((int16_t*)bufs.vOut.data(), resWidth, vOutHeight);

  DcShiftParam dcShift;
  if(resno == numRes_ - 1 && wholeDecompress)
  {
    bool isMctComp = tp_->needsMctDecompress(compno_) && tcp->mct_ == 1;
    if(!isMctComp)
    {
      auto img_comp = tp_->getHeaderImage()->comps + compno_;
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
      dcShift.enabled = (dcShift.shift != 0) || (qmfbid_ == 0);
    }
  }

  auto vSplitL = splitL;
  auto vSplitH = splitH;
  auto vDest = vOut;

  uint32_t numTasks = std::min(resWidth, (uint32_t)FRBSingleton::num_threads());
  uint32_t widthIncr = resWidth / numTasks;
  for(uint32_t j = 0; j < numTasks; ++j)
  {
    auto wMin = j * widthIncr;
    auto wMax = (j < numTasks - 1U) ? (j + 1U) * widthIncr : resWidth;
    pool.submit(frb::task([this, &wavelet, splitLHeight, splitHHeight,
                           wMin, wMax, vSplitL, vSplitH, vDest,
                           dcShift, scratchElems, extFirst, &li] {
      if(!success_)
        return;

      dwt_scratch<int16_t> vScratch;
      vScratch.sn = splitLHeight;
      vScratch.dn = splitHHeight;
      vScratch.parity = (extFirst & 1) ^ li.v_parity;
      if(!vScratch.alloc(scratchElems))
      {
        grklog.error("StripDecompressor: 16-bit vScratch alloc failed");
        success_ = false;
        return;
      }

      if(qmfbid_ == 1)
        wavelet.v_strip_16_53(&vScratch, wMin, wMax, vSplitL, vSplitH, vDest, dcShift);
      else
        wavelet.v_strip_16_97(&vScratch, wMin, wMax, vSplitL, vSplitH, vDest, dcShift);
    }));
    vSplitL.incX_IN_PLACE(widthIncr);
    vSplitH.incX_IN_PLACE(widthIncr);
    vDest.incX_IN_PLACE(widthIncr);
  }

  pool.wait_idle();
  if(!success_)
    return false;

  // copy output rows to destination
  // destBuf already points to where row y0 should go (offset 0)
  uint32_t rowOffset = outY0 - extFirst;
  uint32_t outRows = outY1 - outY0;
  auto srcPtr = (int16_t*)bufs.vOut.data() + rowOffset * resWidth;
  auto dstPtr = (int16_t*)destBuf;
  for(uint32_t row = 0; row < outRows; ++row)
  {
    memcpy(dstPtr, srcPtr, resWidth * sizeof(int16_t));
    srcPtr += resWidth;
    dstPtr += destStride;
  }

  return true;
}

} // namespace grk
