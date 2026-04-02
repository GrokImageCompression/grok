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
  // DC shift is normally fused into the wavelet transform.
  // This function handles:
  // - fallback DC shift for components with no wavelet (num_resolutions == 1)
  // - int32 → float conversion for irreversible non-MCT components
  for(uint16_t compno = 0; compno < tile_->numcomps_; compno++)
  {
    auto tile_comp = tile_->comps_ + compno;
    auto tccp = tcp_->tccps_ + compno;
    auto current_ptr = tile_comp->getWindow()->getResWindowBufferHighestSimple().buf_;
    uint64_t samples = tile_comp->getWindow()->stridedArea();
    bool hasWavelet = (tile_comp->num_resolutions_ > 1);

#ifndef GRK_FORCE_SIGNED_COMPRESS
    // MCT components: DC shift is handled by MCT (no-wavelet) or wavelet (with wavelet)
    if(needsMctDecompress(compno))
      continue;
#else
    tccp->dc_level_shift_ = 1 << ((this->headerImage->comps + compno)->prec - 1);
#endif

    if(tccp->qmfbid_ == 1)
    {
      // reversible: DC shift is applied in wavelet first level when wavelet is used
      if(hasWavelet || tccp->dcLevelShift_ == 0)
        continue;
      // no wavelet: apply DC shift here as fallback
      for(uint64_t i = 0; i < samples; ++i)
      {
        *current_ptr = (int32_t)((int64_t)*current_ptr - tccp->dcLevelShift_);
        ++current_ptr;
      }
    }
    else
    {
      // irreversible: int32 → float conversion
      // DC shift is applied in wavelet when wavelet is used, otherwise apply here
      float* floatPtr = (float*)current_ptr;
      if(hasWavelet)
      {
        for(uint64_t i = 0; i < samples; ++i)
          *floatPtr++ = (float)(*current_ptr++);
      }
      else
      {
        for(uint64_t i = 0; i < samples; ++i)
          *floatPtr++ = (float)((int64_t)*current_ptr++ - tccp->dcLevelShift_);
      }
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

void TileProcessorCompress::buildCompressDAG(void)
{
  compressFlow_ = std::make_unique<tf::Taskflow>();
  dagSuccess_ = true;

  // 1. DC level shift as a single task, then MCT in its own FlowComponent
  // dcShift is fast and runs as a single task
  auto dcShiftFlow = std::make_unique<FlowComponent>();
  dcShiftFlow->nextTask().work([this] { dcLevelShiftCompress(); });
  dcShiftFlow->addTo(*compressFlow_);

  // MCT gets its own FlowComponent so its parallel tasks can run on the executor
  mctFlow_ = std::make_unique<FlowComponent>();
  if(tcp_->mct_)
  {
    if(tcp_->mct_ == 2)
    {
      dagSuccess_ = false;
      return;
    }
    if(tcp_->tccps_->qmfbid_ == 0)
      mct_->compress_irrev(mctFlow_.get(), true);
    else
      mct_->compress_rev(mctFlow_.get(), true);
  }
  mctFlow_->addTo(*compressFlow_);
  dcShiftFlow->precede(*mctFlow_);
  dwtFlows_.push_back(std::move(dcShiftFlow)); // store to keep alive

  // 2. Per-component DWT levels as FlowComponent pairs (vert, horiz)
  //    Fan-out from MCT: all components run in parallel.
  //    Fan-in to T1: T1 waits for all components to finish.
  std::vector<FlowComponent*> lastDwtPerComponent;
  for(uint16_t compno = 0; compno < tile_->numcomps_; ++compno)
  {
    auto tile_comp = tile_->comps_ + compno;
    auto tccp = tcp_->tccps_ + compno;
    auto maxDim = std::max(cp_->t_width_, cp_->t_height_);

    if(tile_comp->num_resolutions_ <= 1)
      continue;

    uint8_t numLevels = (uint8_t)(tile_comp->num_resolutions_ - 1);

    // Compute DC shift for fusion into wavelet first level
    DcShiftParam dcShift;
    bool isMctComp = needsMctDecompress(compno) && tcp_->mct_ == 1;
    if(!isMctComp)
    {
      auto img_comp = headerImage_->comps + compno;
      dcShift.shift = (int32_t)tccp->dcLevelShift_;
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
      dcShift.enabled = (tccp->dcLevelShift_ != 0);
    }

    // Create FlowComponent pairs for each level
    std::vector<std::pair<FlowComponent*, FlowComponent*>> levelFlows;
    for(uint8_t lvl = 0; lvl < numLevels; ++lvl)
    {
      auto vertFlow = std::make_unique<FlowComponent>();
      auto horizFlow = std::make_unique<FlowComponent>();
      levelFlows.push_back({vertFlow.get(), horizFlow.get()});
      dwtFlows_.push_back(std::move(vertFlow));
      dwtFlows_.push_back(std::move(horizFlow));
    }

    // Schedule DWT tasks into the FlowComponents
    WaveletFwdImpl w;
    auto scratch = w.scheduleCompress(tile_comp, tccp->qmfbid_, maxDim, dcShift, levelFlows);
    if(scratch)
      dwtScratch_.push_back(std::move(scratch));

    // Compose into parent flow and wire dependencies within this component
    FlowComponent* prevInComp = nullptr;
    for(uint8_t lvl = 0; lvl < numLevels; ++lvl)
    {
      auto vertFlow = static_cast<FlowComponent*>(levelFlows[lvl].first);
      auto horizFlow = static_cast<FlowComponent*>(levelFlows[lvl].second);

      vertFlow->addTo(*compressFlow_);
      horizFlow->addTo(*compressFlow_);

      if(lvl == 0)
      {
        // MCT → first vert of this component (fan-out)
        mctFlow_->precede(*vertFlow);
      }
      else
      {
        prevInComp->precede(*vertFlow);
      }
      vertFlow->precede(*horizFlow);
      prevInComp = horizFlow;
    }
    // Track the last FlowComponent for this component (for fan-in to T1)
    if(prevInComp)
      lastDwtPerComponent.push_back(prevInComp);
  }

  // 3. T1 block encoding
  t1Flow_ = std::make_unique<FlowComponent>();
  {
    const double* mct_norms;
    uint16_t mct_numcomps = 0U;
    auto tcp = tcp_;
    if(tcp->mct_ == 1)
    {
      mct_numcomps = 3U;
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
    static_cast<CompressScheduler*>(scheduler_)->populateT1Flow(t1Flow_.get());
  }
  t1Flow_->addTo(*compressFlow_);
  // Fan-in: all component DWT chains must complete before T1 starts
  if(lastDwtPerComponent.empty())
  {
    // No DWT (all components have num_resolutions == 1): MCT → T1
    mctFlow_->precede(*t1Flow_);
  }
  else
  {
    for(auto* lastDwt : lastDwtPerComponent)
      lastDwt->precede(*t1Flow_);
  }

  // 4. Rate allocation (single task at the end)
  auto rateAllocTask =
      compressFlow_
          ->emplace([this] {
            packetLengthCache_->deleteMarkers();
            if(cp_->codingParams_.enc_.writePlt_)
              packetLengthCache_->createMarkers(stream_);
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
                dagSuccess_ = false;
                return;
              }
            }
            packetTracker_->clear();
            if(canPreCalculateTileLen())
            {
              preCalculatedTileLen_ = sotMarkerSegmentLen;
              if(canWritePocMarker())
              {
                uint32_t pocSize =
                    CodeStreamCompress::getPocSize(tile_->numcomps_, tcp_->getNumProgressions());
                preCalculatedTileLen_ += pocSize;
              }
              if(packetLengthCache_->getMarkers())
                preCalculatedTileLen_ += packetLengthCache_->getMarkers()->getTotalBytesWritten();
              preCalculatedTileLen_ += 2;
              preCalculatedTileLen_ += allPacketBytes;
            }
          })
          .name("rateAlloc");
  t1Flow_->precede(rateAllocTask);
}

tf::Future<void> TileProcessorCompress::submitCompressDAG(void)
{
  return TFSingleton::get().run(*compressFlow_);
}

bool TileProcessorCompress::compressDAGSuccess(void) const
{
  return dagSuccess_;
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
        {
          // MCT always handles DC shift (wavelet doesn't fuse it for MCT components)
          mct_->compress_irrev(nullptr, true);
        }
        else
        {
          mct_->compress_rev(nullptr, true);
        }
      }
    }
    if(!debugEncode || debugMCT)
    {
      for(uint16_t compno = 0; compno < tile_->numcomps_; ++compno)
      {
        auto tile_comp = tile_->comps_ + compno;
        auto tccp = tcp_->tccps_ + compno;
        auto maxDim = std::max(cp_->t_width_, cp_->t_height_);

        // compute DC shift for fusion into wavelet first level
        DcShiftParam dcShift;
        if(tile_comp->num_resolutions_ > 1)
        {
          bool isMctComp = needsMctDecompress(compno) && tcp_->mct_ == 1;
          // Don't fuse DC shift for MCT components:
          // MCT handles DC shift before color transform
          if(!isMctComp)
          {
            auto img_comp = headerImage_->comps + compno;
            // positive shift: forward wavelet subtracts it from data
            dcShift.shift = (int32_t)tccp->dcLevelShift_;
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
            dcShift.enabled = (tccp->dcLevelShift_ != 0);
          }
        }

        WaveletFwdImpl w;
        if(!w.compress(tile_comp, tccp->qmfbid_, maxDim, dcShift))
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