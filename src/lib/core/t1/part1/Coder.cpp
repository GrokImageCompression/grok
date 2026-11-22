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

#include "simd.h"
#include "Coder.h"
#include "t1_common.h"
#include "BlockCoder.h"
#include "PreCompressFilters.h"

namespace grk::t1::part1
{

Coder::Coder(bool isCompressor, uint16_t maxCblkW, uint16_t maxCblkH, uint32_t cacheStrategy)
    : blockCoder_(new BlockCoder(isCompressor, maxCblkW, maxCblkH, cacheStrategy))
{
  if(isCompressor)
    streamScratch_.resize(compressedStreamScratchBytes((uint32_t)maxCblkW * maxCblkH));
}
Coder::~Coder()
{
  delete blockCoder_;
}
bool Coder::preCompress(CompressBlockExec* block, uint32_t& maximum)
{
  auto cblk = block->cblk;
  auto w = cblk->width();
  auto h = cblk->height();
  if(w == 0 || h == 0)
  {
    grklog.error("Unable to compress degenerate code block of dimensions %ux%u", w, h);
    return false;
  }
  if(block->tile_width < w)
  {
    grklog.error("Tile width %u less than code block width %u", block->tile_width, w);
    return false;
  }
  if(!blockCoder_->alloc(w, h))
    return false;
  auto tileLineAdvance = block->tile_width - w;
  uint32_t tile_index = 0;
  uint32_t cblk_index = 0;
  maximum = 0;
  auto uncompressedData = blockCoder_->getUncompressedData();
  if(block->qmfbid == 1)
  {
    if(block->uses16BitBuffer)
    {
      maximum = toSignMagnitude16(uncompressedData, static_cast<const int16_t*>(block->tileData),
                                  block->tile_width, w, h);
    }
    else
    {
      auto tileData = static_cast<int32_t*>(block->tileData);
      for(auto row = 0U; row < h; ++row)
      {
        for(auto column = 0U; column < w; ++column)
        {
          int32_t temp = (tileData[tile_index++] *= (1 << T1_NMSEDEC_FRACBITS));
          int32_t magnitude = temp * ((temp > 0) - (temp < 0));
          if((uint32_t)magnitude > maximum)
            maximum = (uint32_t)magnitude;
          int32_t sign = int32_t((uint32_t)(magnitude != temp) * 0x80000000);
          uncompressedData[cblk_index++] = sign | magnitude;
        }
        tile_index += tileLineAdvance;
      }
    }
  }
  else if(block->uses16BitBuffer)
  {
    maximum =
        quantize16ToSignMagnitude(uncompressedData, static_cast<const int16_t*>(block->tileData),
                                  block->tile_width, w, h, block->fixedPointInverseStep);
  }
  else
  {
    maximum = quantizeToSignMagnitude(uncompressedData, static_cast<const float*>(block->tileData),
                                      block->tile_width, w, h, 1.0 / block->stepsize);
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

  streamScratch_[0] = 0;
  streamScratch_[1] = 0;
  cblkexp.data = streamScratch_.data() + grk_cblk_enc_compressed_data_pad_left;
#ifdef PLUGIN_DEBUG_ENCODE
  cblkexp.context_stream = cblk->context_stream;
#endif

  auto distortion =
      blockCoder_->compress_cblk(&cblkexp, max, block->bandOrientation, block->compno, block->level,
                                 block->qmfbid, block->stepsize, block->cblk_sty, block->mct_norms,
                                 block->mct_numcomps, block->doRateControl, block->earlyStopSlope);

  auto passes = cblk->allocatePasses(cblkexp.numPassesTotal);
  cblk->setNumBps(cblkexp.numbps);
  cblk->storeCompressedBytes(
      cblkexp.data, cblkexp.numPassesTotal ? cblkexp.passes[cblkexp.numPassesTotal - 1].rate : 0);
  for(uint8_t i = 0; i < cblkexp.numPassesTotal; ++i)
  {
    auto passexp = cblkexp.passes + i;
    auto passgrk = passes + i;
    passgrk->distortiondec_ = passexp->distortiondec;
    passgrk->len_ = passexp->len;
    passgrk->rate_ = passexp->rate;
    passgrk->term_ = passexp->term;
  }

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
  if(block->postProcessor_)
    block->postProcessor_(blockCoder_->getUncompressedData(), block, 0);

  return true;
}

} // namespace  grk::t1::part1
