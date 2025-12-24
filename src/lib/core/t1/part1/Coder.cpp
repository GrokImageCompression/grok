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
#include "Coder.h"
#include "t1_common.h"
#include "BlockCoder.h"
#include "SchedulerFactory.h"

namespace grk
{
namespace t1_part1
{
  Coder::Coder(bool isCompressor, uint8_t maxCblkW, uint8_t maxCblkH, uint32_t cacheStrategy)
      : blockCoder_(nullptr), cacheStrategy_(cacheStrategy)
  {
    blockCoder_ = new BlockCoder(isCompressor, maxCblkW, maxCblkH, cacheStrategy);
  }
  Coder::~Coder()
  {
    delete blockCoder_;
  }
  bool Coder::preCompress(CompressBlockExec* block, uint32_t& maximum)
  {
    auto tile = block->tile;
    auto cblk = block->cblk;
    auto w = (uint8_t)cblk->width();
    auto h = (uint8_t)cblk->height();
    if(w == 0 || h == 0)
    {
      grklog.error("Unable to compress degenerate code block of dimensions %ux%u", w, h);
      return false;
    }
    if(!blockCoder_->alloc(w, h))
      return false;
    auto tileLineAdvance =
        (tile->comps_ + block->compno)->getWindow()->getResWindowBufferHighestStride() - w;
    uint32_t tile_index = 0;
    uint32_t cblk_index = 0;
    maximum = 0;
    auto uncompressedData = blockCoder_->getUncompressedData();
    if(block->qmfbid == 1)
    {
      for(auto j = 0U; j < h; ++j)
      {
        for(auto i = 0U; i < w; ++i)
        {
          int32_t temp = (block->tiledp[tile_index++] *= (1 << T1_NMSEDEC_FRACBITS));
          int32_t mag = temp * ((temp > 0) - (temp < 0));
          if((uint32_t)mag > maximum)
            maximum = (uint32_t)mag;
          int32_t sgn = int32_t((uint32_t)(mag != temp) * 0x80000000);
          uncompressedData[cblk_index++] = sgn | mag;
        }
        tile_index += tileLineAdvance;
      }
    }
    else
    {
      const auto* const tiledp = (float*)block->tiledp;
      double quant = 1.0 / block->stepsize;
      for(auto j = 0U; j < h; ++j)
      {
        for(auto i = 0U; i < w; ++i)
        {
          int32_t temp = (int32_t)grk_lrintf((float)(((double)tiledp[tile_index++] * quant)) *
                                             (1 << T1_NMSEDEC_FRACBITS));
          int32_t mag = temp * ((temp > 0) - (temp < 0));
          if((uint32_t)mag > maximum)
            maximum = (uint32_t)mag;
          int32_t sgn = int32_t((uint32_t)(mag != temp) * 0x80000000);
          uncompressedData[cblk_index++] = sgn | mag;
        }
        tile_index += tileLineAdvance;
      }
    }

    return true;
  }
  bool Coder::compress(CompressBlockExec* block)
  {
    uint32_t max = 0;
    if(!preCompress(block, max))
      return false;

    auto cblk = block->cblk;
    cblk_enc cblkexp = {};
    cblkexp.x0 = block->x;
    cblkexp.y0 = block->y;
    cblkexp.x1 = block->x + cblk->width();
    cblkexp.y1 = block->y + cblk->height();
    assert(cblk->width() > 0);
    assert(cblk->height() > 0);

    cblkexp.data = cblk->getPaddedCompressedStream();
#ifdef PLUGIN_DEBUG_ENCODE
    cblkexp.context_stream = cblk->context_stream;
#endif

    auto distortion = blockCoder_->compress_cblk(
        &cblkexp, max, block->bandOrientation, block->compno,
        (uint8_t)((block->tile->comps_ + block->compno)->num_resolutions_ - 1 - block->resno),
        block->qmfbid, block->stepsize, block->cblk_sty, block->mct_norms, block->mct_numcomps,
        block->doRateControl);

    cblk->setNumPasses(cblkexp.numPassesTotal);
    cblk->setNumBps(cblkexp.numbps);
    for(uint8_t i = 0; i < cblk->getNumPasses(); ++i)
    {
      auto passexp = cblkexp.passes + i;
      auto passgrk = cblk->getPass(i);
      passgrk->distortiondec_ = passexp->distortiondec;
      passgrk->len_ = passexp->len;
      passgrk->rate_ = passexp->rate;
      passgrk->term_ = passexp->term;
    }

    blockCoder_->code_block_enc_deallocate(&cblkexp);
    cblkexp.data = nullptr;

    block->distortion = distortion;

    return true;
  }

  bool Coder::decompress(DecompressBlockExec* block)
  {
    // 1. allocate
    blockCoder_->setFinalLayer(block->finalLayer_);
    auto cblk = block->cblk;
    // 2. decompress
    if(!blockCoder_->decompress_cblk(cblk, block->bandOrientation, block->cblk_sty))
      return false;
    // 3. post process
    if(!Scheduling::isWindowedScheduling())
      block->tilec->postProcess<int32_t>(blockCoder_->getUncompressedData(), block);

    return true;
  }

} // namespace t1_part1
} // namespace grk
