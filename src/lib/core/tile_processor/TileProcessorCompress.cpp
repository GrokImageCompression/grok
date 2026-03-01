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
#include "ICompressor.h"
#include "IDecompressor.h"
#include "MarkerParser.h"
#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
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
#include "SchedulerStandard.h"
#include "TileComponentWindow.h"
#include "WaveletFwd.h"
#include "canvas/tile/Tile.h"
#include "mct.h"
#include "TileProcessor.h"
#include "TileProcessorCompress.h"
#include "CodeStreamCompress.h"
#include "T2Compress.h"
#include "CompressScheduler.h"
#include "TileProcessorCompress.h"

namespace grk
{
TileProcessorCompress::TileProcessorCompress(uint16_t tile_index, TileCodingParams* tcp,
                                             CodeStream* codeStream, IStream* stream)
    : TileProcessor(tile_index, tcp, codeStream, stream, true, GRK_TILE_CACHE_NONE),
      newTilePartProgressionPosition_(
          codeStream->getCodingParams()->codingParams_.enc_.newTilePartProgressionPosition_)
{}
TileProcessorCompress::~TileProcessorCompress()
{
  delete packetTracker_;
}

bool TileProcessorCompress::init(void)
{
  if(!TileProcessor::init())
    return false;

  uint64_t max_precincts = 0;
  for(uint16_t compno = 0; compno < headerImage_->numcomps; ++compno)
  {
    auto tilec = tile_->comps_ + compno;
    for(uint8_t resno = 0; resno < tilec->num_resolutions_; ++resno)
    {
      auto res = tilec->resolutions_ + resno;
      max_precincts = (std::max<uint64_t>)(max_precincts, res->precinctGrid_.area());
    }
  }
  packetTracker_ = new PacketTracker(tile_->numcomps_, tile_->comps_->num_resolutions_,
                                     max_precincts, getTCP()->numLayers_);

  return true;
}
bool TileProcessorCompress::preCompressTile([[maybe_unused]] size_t thread_id)
{
  tilePartCounter_ = 0;
  first_poc_tile_part_ = true;

  /* initialization before tile compressing  */
  bool rc = init();
  if(!rc)
    return false;
  // don't need to allocate any buffers if this is from the plugin.
  if(current_plugin_tile_)
    return true;
  for(uint16_t compno = 0; compno < tile_->numcomps_; ++compno)
  {
    auto imageComp = headerImage_->comps + compno;
    if(imageComp->dx == 0 || imageComp->dy == 0)
      return false;
    auto tileComp = tile_->comps_ + compno;
    if(!tileComp->canCreateWindow(Rect32(tileComp)))
      return false;
    auto unreducedTileComp = tileComp;
    tileComp->createWindow(Rect32(unreducedTileComp));
  }
  uint32_t numTiles = (uint32_t)cp_->t_grid_height_ * cp_->t_grid_width_;

  bool attachTileToImage = (numTiles == 1);
  /* if we only have one tile, then simply set tile component data equal to
   * image component data. Otherwise, allocate tile data and copy */
  for(uint32_t j = 0; j < headerImage_->numcomps; ++j)
  {
    auto tilec = tile_->comps_ + j;
    auto imagec = headerImage_->comps + j;
    if(attachTileToImage)
      tilec->getWindow()->attach((int32_t*)imagec->data, imagec->stride);
    else if(!tilec->getWindow()->alloc())
    {
      grklog.error("Error allocating tile component data.");
      return false;
    }
  }
  // otherwise copy image data to tile
  if(!attachTileToImage)
  {
    for(uint16_t i = 0; i < headerImage_->numcomps; ++i)
    {
      auto tilec = tile_->comps_ + i;
      auto img_comp = headerImage_->comps + i;
      if(!img_comp->data)
        continue;

      uint32_t offset_x = ceildiv<uint32_t>(headerImage_->x0, img_comp->dx);
      uint32_t offset_y = ceildiv<uint32_t>(headerImage_->y0, img_comp->dy);
      uint64_t image_offset =
          (tilec->x0 - offset_x) + (uint64_t)(tilec->y0 - offset_y) * img_comp->stride;
      auto src = (int32_t*)img_comp->data + image_offset;
      auto dest = tilec->getWindow()->getResWindowBufferHighestSimple();
      if(!dest.buf_)
        continue;

      for(uint32_t j = 0; j < tilec->height(); ++j)
      {
        memcpy(dest.buf_, src, (size_t)tilec->width() * sizeof(int32_t));
        src += img_comp->stride;
        dest.buf_ += dest.stride_;
      }
    }
  }

  return true;
}

uint32_t TileProcessorCompress::getPreCalculatedTileLen(void)
{
  return preCalculatedTileLen_;
}
bool TileProcessorCompress::canPreCalculateTileLen(void)
{
  return !cp_->codingParams_.enc_.enableTilePartGeneration_ && tcp_->getNumProgressions() == 1;
}
bool TileProcessorCompress::canWritePocMarker(void)
{
  bool firstTilePart = (tilePartCounter_ == 0);

  // note: DCP standard does not allow POC marker
  return tcp_->hasPoc() && firstTilePart && !GRK_IS_CINEMA(cp_->rsiz_);
}
bool TileProcessorCompress::writeTilePartT2(uint32_t* tileBytesWritten)
{
  // write entire PLT marker in first tile part header
  if(tilePartCounter_ == 0 && packetLengthCache_->getMarkers())
  {
    if(!packetLengthCache_->getMarkers()->write())
      return false;
    *tileBytesWritten += packetLengthCache_->getMarkers()->getTotalBytesWritten();
  }

  // write SOD
  if(!stream_->write(SOD))
    return false;
  *tileBytesWritten += 2;

  // write tile packets
  return compressT2(tileBytesWritten);
}

void TileProcessorCompress::dcLevelShiftCompress(void)
{
  for(uint16_t compno = 0; compno < tile_->numcomps_; compno++)
  {
    auto tile_comp = tile_->comps_ + compno;
    auto tccp = tcp_->tccps_ + compno;
    auto current_ptr = tile_comp->getWindow()->getResWindowBufferHighestSimple().buf_;
    uint64_t samples = tile_comp->getWindow()->stridedArea();
#ifndef GRK_FORCE_SIGNED_COMPRESS
    if(needsMctDecompress(compno))
      continue;
#else
    tccp->dc_level_shift_ = 1 << ((this->headerImage->comps + compno)->prec - 1);
#endif

    if(tccp->qmfbid_ == 1)
    {
      if(tccp->dcLevelShift_ == 0)
        continue;
      for(uint64_t i = 0; i < samples; ++i)
      {
        *current_ptr -= tccp->dcLevelShift_;
        ++current_ptr;
      }
    }
    else
    {
      // output float

      // Note: we need to convert to FP even if level shift is zero
      // todo: skip this inefficiency for zero level shift

      float* floatPtr = (float*)current_ptr;
      for(uint64_t i = 0; i < samples; ++i)
        *floatPtr++ = (float)(*current_ptr++ - tccp->dcLevelShift_);
    }
#ifdef GRK_FORCE_SIGNED_COMPRESS
    tccp->dc_level_shift_ = 0;
#endif
  }
}
void TileProcessorCompress::scheduleCompressT1()
{
  const double* mct_norms;
  uint16_t mct_numcomps = 0U;
  auto tcp = tcp_;

  if(tcp->mct_ == 1)
  {
    mct_numcomps = 3U;
    /* irreversible compressing */
    if(tcp->tccps_->qmfbid_ == 0)
      mct_norms = Mct::get_norms_irrev();
    else
      mct_norms = Mct::get_norms_rev();
  }
  else
  {
    mct_numcomps = headerImage_->numcomps;
    mct_norms = (const double*)(tcp->mct_norms_);
  }

  scheduler_ = new CompressScheduler(tile_, needsRateControl(), tcp, mct_norms, mct_numcomps);
  scheduler_->scheduleT1(nullptr);
}
bool TileProcessorCompress::compressT2(uint32_t* tileBytesWritten)
{
  auto l_t2 = new T2Compress(this);
  if(!l_t2->compressPackets(tileIndex_, tcp_->numLayers_, stream_, tileBytesWritten,
                            first_poc_tile_part_, newTilePartProgressionPosition_, prog_iter_num))
  {
    delete l_t2;
    return false;
  }
  delete l_t2;
  return true;
}

void TileProcessorCompress::setFirstPocTilePart(bool res)
{
  first_poc_tile_part_ = res;
}

bool TileProcessorCompress::doCompress(void)
{
  uint32_t state = grk_plugin_get_debug_state();
#ifdef PLUGIN_DEBUG_ENCODE
  if(state & GRK_PLUGIN_STATE_DEBUG)
    set_context_stream(this);
#endif
  // When debugging the compressor, we do all of T1 up to and including DWT
  // in the plugin, and pass this in as image data.
  // This way, both Grok and plugin start with same inputs for
  // context formation and MQ coding.
  bool debugEncode = state & GRK_PLUGIN_STATE_DEBUG;
  bool debugMCT = (state & GRK_PLUGIN_STATE_MCT_ONLY) ? true : false;

  if(!current_plugin_tile_ || debugEncode)
  {
    if(!debugEncode)
    {
      dcLevelShiftCompress();
      if(tcp_->mct_)
      {
        if(tcp_->mct_ == 2)
        {
          /*
          if(!tcp_->mct_coding_matrix_)
            return true;
          auto data = new uint8_t*[tile->numcomps_];
          for(uint32_t i = 0; i < tile->numcomps_; ++i)
          {
            auto tile_comp = tile->comps + i;
            data[i] = (uint8_t*)tile_comp->getWindow()->getResWindowBufferHighestSimple().buf_;
          }
          uint64_t samples = tile->comps->getWindow()->stridedArea();
          bool rc = Mct::compress_custom((uint8_t*)tcp_->mct_coding_matrix_, samples, data,
                                         tile->numcomps_, headerImage->comps->sgnd);
          delete[] data;
          return rc;
          */
          return false;
        }
        else if(tcp_->tccps_->qmfbid_ == 0)
          mct_->compress_irrev(nullptr);
        else
          mct_->compress_rev(nullptr);
      }
    }
    if(!debugEncode || debugMCT)
    {
      for(uint16_t compno = 0; compno < tile_->numcomps_; ++compno)
      {
        auto tile_comp = tile_->comps_ + compno;
        auto tccp = tcp_->tccps_ + compno;
        auto maxDim = std::max(cp_->t_width_, cp_->t_height_);
        WaveletFwdImpl w;
        if(!w.compress(tile_comp, tccp->qmfbid_, maxDim))
          return false;
      }
    }
    scheduleCompressT1();
  }
  // 1. create PLT marker if required
  packetLengthCache_->deleteMarkers();
  if(cp_->codingParams_.enc_.writePlt_)
    packetLengthCache_->createMarkers(stream_);
  // 2. rate control
  uint32_t allPacketBytes = 0;
  bool rc = rateAllocate(&allPacketBytes, false);
  if(!rc)
  {
    grklog.warn("Unable to perform rate control on tile %d", tileIndex_);
    grklog.warn("Rate control will be disabled for this tile");
    allPacketBytes = 0;
    rc = rateAllocate(&allPacketBytes, true);
    if(!rc)
    {
      grklog.error("Unable to perform rate control on tile %d", tileIndex_);
      return false;
    }
  }
  packetTracker_->clear();

  if(canPreCalculateTileLen())
  {
    // SOT marker
    preCalculatedTileLen_ = sotMarkerSegmentLen;
    // POC marker
    if(canWritePocMarker())
    {
      uint32_t pocSize =
          CodeStreamCompress::getPocSize(tile_->numcomps_, tcp_->getNumProgressions());
      preCalculatedTileLen_ += pocSize;
    }
    // calculate PLT marker length
    if(packetLengthCache_->getMarkers())
      preCalculatedTileLen_ += packetLengthCache_->getMarkers()->getTotalBytesWritten();

    // calculate SOD marker length
    preCalculatedTileLen_ += 2;
    // calculate packets length
    preCalculatedTileLen_ += allPacketBytes;
  }
  return true;
}
PacketTracker* TileProcessorCompress::getPacketTracker(void)
{
  return packetTracker_;
}
uint8_t TileProcessorCompress::getTilePartCounter(void) const
{
  return tilePartCounter_;
}
void TileProcessorCompress::incTilePartCounter(void)
{
  tilePartCounter_++;
}
void TileProcessorCompress::setProgIterNum(uint32_t num)
{
  prog_iter_num = num;
}

/**
 * Assume that source stride  == source width == destination width
 */
template<typename T>
void grk_copy_strided(uint32_t w, uint32_t stride, uint32_t h, const T* src, int32_t* dest)
{
  assert(stride >= w);
  uint32_t stride_diff = stride - w;
  size_t src_ind = 0, dest_ind = 0;
  for(uint32_t j = 0; j < h; ++j)
  {
    for(uint32_t i = 0; i < w; ++i)
      dest[dest_ind++] = src[src_ind++];
    dest_ind += stride_diff;
  }
}
bool TileProcessorCompress::ingestUncompressedData(uint8_t* p_src, uint64_t src_length)
{
  uint64_t tile_size = 0;
  for(uint32_t i = 0; i < headerImage_->numcomps; ++i)
  {
    auto tilec = tile_->comps_ + i;
    auto img_comp = headerImage_->comps + i;
    uint32_t size_comp = (uint32_t)((img_comp->prec + 7) >> 3);
    tile_size += size_comp * tilec->area();
  }
  if(!p_src || (tile_size != src_length))
    return false;
  size_t length_per_component = src_length / headerImage_->numcomps;
  for(uint32_t i = 0; i < headerImage_->numcomps; ++i)
  {
    auto tilec = tile_->comps_ + i;
    auto img_comp = headerImage_->comps + i;
    uint32_t size_comp = (uint32_t)((img_comp->prec + 7) >> 3);
    auto b = tilec->getWindow()->getResWindowBufferHighestSimple();
    auto dest_ptr = b.buf_;
    uint32_t w = (uint32_t)tilec->getWindow()->bounds().width();
    uint32_t h = (uint32_t)tilec->getWindow()->bounds().height();
    uint32_t stride = b.stride_;
    switch(size_comp)
    {
      case 1:
        if(img_comp->sgnd)
        {
          auto src = (int8_t*)p_src;
          grk_copy_strided<int8_t>(w, stride, h, src, dest_ptr);
          p_src = (uint8_t*)(src + length_per_component);
        }
        else
        {
          auto src = (uint8_t*)p_src;
          grk_copy_strided<uint8_t>(w, stride, h, src, dest_ptr);
          p_src = (uint8_t*)(src + length_per_component);
        }
        break;
      case 2:
        if(img_comp->sgnd)
        {
          auto src = (int16_t*)p_src;
          grk_copy_strided<int16_t>(w, stride, h, (int16_t*)p_src, dest_ptr);
          p_src = (uint8_t*)(src + length_per_component);
        }
        else
        {
          auto src = (uint16_t*)p_src;
          grk_copy_strided<uint16_t>(w, stride, h, (uint16_t*)p_src, dest_ptr);
          p_src = (uint8_t*)(src + length_per_component);
        }
        break;
    }
  }

  return true;
}

} // namespace grk