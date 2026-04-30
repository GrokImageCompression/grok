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

#include <chrono>
#include <functional>

#include "TFSingleton.h"
#include "grk_fseek.h"
#include "geometry.h"
#include "grk_exceptions.h"
#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "TileFutureManager.h"
#include "FlowComponent.h"
#include "IStream.h"
#include "MarkerCache.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImage.h"
#include "IDecompressor.h"
#include "MemStream.h"
#include "StreamGenerator.h"
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
#include "TLMMarker.h"

#include "ICoder.h"
#include "CoderPool.h"
#include "BitIO.h"

#include "SchedulerExcalibur.h"
#include "TileProcessor.h"
#include "TileCache.h"
#include "TileCompletion.h"
#include "GrkImageSIMD.h"
#include "CodeStreamDecompress.h"

namespace grk
{

CodeStreamDecompress::CodeStreamDecompress(IStream* stream)
    : CodeStream(stream), markerCache_(std::make_unique<MarkerCache>()),
      defaultTcp_(std::make_unique<TileCodingParams>(&cp_)),
      multiTileComposite_(new GrkImage(), RefCountedDeleter<GrkImage>()),
      tileCache_(std::make_unique<TileCache>())
{
  headerImage_ = new GrkImage();
  headerImage_->meta = grk_image_meta_new();
  markerParser_.add({
      {SIZ, new MarkerProcessor(
                SIZ, [this](uint8_t* data, uint16_t len) { return readSIZ(data, len); })},
      {CAP, new MarkerProcessor(
                CAP, [this](uint8_t* data, uint16_t len) { return readCAP(data, len); })},
      {TLM, new MarkerProcessor(
                TLM, [this](uint8_t* data, uint16_t len) { return readTLM(data, len); })},
      {PLM, new MarkerProcessor(
                PLM, [this](uint8_t* data, uint16_t len) { return readPLM(data, len); })},
      {PPM, new MarkerProcessor(
                PPM, [this](uint8_t* data, uint16_t len) { return readPPM(data, len); })},
      {CRG, new MarkerProcessor(
                CRG, [this](uint8_t* data, uint16_t len) { return readCRG(data, len); })},
      {CBD, new MarkerProcessor(
                CBD, [this](uint8_t* data, uint16_t len) { return readCBD(data, len); })},
      {SOT, new MarkerProcessor(
                SOT, [this](uint8_t* data, uint16_t len) { return readSOT(data, len); })},
  });

  markerParser_.setStream(stream, false);
  markerParser_.add(
      {{COD,
        new MarkerProcessor(
            COD, [this](uint8_t* data, uint16_t len) { return defaultTcp_->readCod(data, len); })},
       {COC,
        new MarkerProcessor(
            COC, [this](uint8_t* data, uint16_t len) { return defaultTcp_->readCoc(data, len); })},
       {RGN,
        new MarkerProcessor(
            RGN, [this](uint8_t* data, uint16_t len) { return defaultTcp_->readRgn(data, len); })},
       {QCD, new MarkerProcessor(QCD,
                                 [this](uint8_t* data, uint16_t len) {
                                   return defaultTcp_->readQcd(false, data, len);
                                 })},
       {QCC, new MarkerProcessor(QCC,
                                 [this](uint8_t* data, uint16_t len) {
                                   return defaultTcp_->readQcc(false, data, len);
                                 })},
       {POC, new MarkerProcessor(POC,
                                 [this](uint8_t* data, uint16_t len) {
                                   return defaultTcp_->readPoc(data, len, -1);
                                 })},
       {COM, new MarkerProcessor(
                 COM, [this](uint8_t* data, uint16_t len) { return cp_.readCom(data, len); })},
       {MCT,
        new MarkerProcessor(
            MCT, [this](uint8_t* data, uint16_t len) { return defaultTcp_->readMct(data, len); })},
       {MCC,
        new MarkerProcessor(
            MCC, [this](uint8_t* data, uint16_t len) { return defaultTcp_->readMcc(data, len); })},

       {MCO, new MarkerProcessor(MCO, [this](uint8_t* data, uint16_t len) {
          return defaultTcp_->readMco(data, len);
        })}});

  tileMarkerParsers_.resize(TFSingleton::num_threads());
  std::generate(tileMarkerParsers_.begin(), tileMarkerParsers_.end(),
                []() { return std::make_unique<MarkerParser>(); });
}
void CodeStreamDecompress::init(grk_decompress_parameters* parameters)
{
  assert(parameters);
  cp_.init(parameters, tileCache_);
  auto core = &parameters->core;
  tileCache_->setStrategy(core->tile_cache_strategy);
  tileCache_->setMaxActiveTiles(core->max_active_tiles);
  ioBufferCallback_ = core->io_buffer_callback;
  ioUserData_ = core->io_user_data;
  grkRegisterReclaimCallback_ = core->io_register_client_callback;
  ioBandCallback_ = core->io_band_callback;
  ioBandUserData_ = core->io_band_user_data;

  // Initialize compressed chunk cache for LRU + network fetches
  if((core->tile_cache_strategy & GRK_TILE_CACHE_LRU) && stream_->getFetcher())
  {
    auto diskCache = std::make_shared<DiskCache>();
    compressedChunkCache_ =
        std::make_unique<CompressedChunkCache>(0, diskCache, stream_->getFormat());
  }

  postReadHeader();
}
void CodeStreamDecompress::setBandCallback(grk_io_band_callback callback, void* user_data)
{
  ioBandCallback_ = callback;
  ioBandUserData_ = user_data;
}

// Multi Tile //////////////////////////////////////////////////////////

bool CodeStreamDecompress::decompress(grk_plugin_tile* tile)
{
  current_plugin_tile = tile;

  // GPU plugin T2-only fast path: skip heavy infrastructure
  if(tile && !(tile->decompress_flags & GRK_DECODE_T1))
  {
    tileCache_->init(cp_.t_grid_width_ * cp_.t_grid_height_);
    decompressSequentialPrepare();
    success_ = true;
    // Parse tile parts (SOT markers + tile data)
    while(!markerParser_.endOfCodeStream() && stream_->numBytesLeft() != 0)
    {
      try
      {
        auto [processed, length] = markerParser_.processMarker();
        if(!processed)
          break;
      }
      catch([[maybe_unused]] const CorruptSOTMarkerException&)
      {
        break;
      }
      try
      {
        if(!markerParser_.readId(false))
          break;
      }
      catch([[maybe_unused]] const t1_t2::InvalidMarkerException&)
      {
        break;
      }
      if(!currTileProcessor_)
        continue;
      if(!currTileProcessor_->parseTilePart(nullptr, nullptr, markerParser_.currId(),
                                            currTilePartInfo_))
      {
        success_ = false;
        break;
      }
      if(!currTileProcessor_->allSOTMarkersParsed() && !markerParser_.readSOTorEOC())
        break;
      if(currTileProcessor_->allSOTMarkersParsed())
        break;
    }
    if(currTileProcessor_)
    {
      currTileProcessor_->prepareForDecompression();
      // Run T2 inline with no-op post (no image extraction needed)
      currTileProcessor_->scheduleAndRunDecompress(
          &coderPool_, headerImage_->getBounds(), []() {}, decompressTileFutureManager_);
      currTileProcessor_ = nullptr;
      currTileIndex_ = -1;
    }
    return success_;
  }

  multiTileComposite_->postReadHeader(&cp_);
  tileCache_->init(cp_.t_grid_width_ * cp_.t_grid_height_);
  // create TileCompletion for band callback if set after header read
  if(ioBandCallback_ && !tileCompletion_)
  {
    // Use unreduced image bounds so that TileCompletion computes the same tile grid
    // (t_grid_width_ x t_grid_height_) as the codec.  Tile indices from
    // tileProcessor->getIndex() are based on the unreduced grid.
    Rect32 unreducedBounds(cp_.tx0_, cp_.ty0_, cp_.tx0_ + cp_.t_grid_width_ * cp_.t_width_,
                           cp_.ty0_ + cp_.t_grid_height_ * cp_.t_height_);
    auto slatedRect = tilesToDecompress_.getSlatedTileRect();
    nextBandTileY_ = slatedRect.y0;
    pendingBands_.clear();
    uint8_t reduce = cp_.codingParams_.dec_.reduce_;
    // Compute reduced-resolution pixel extents for Y clamping.
    // multiTileComposite_ bounds are unreduced at this point; apply reduction.
    auto compositeBounds = multiTileComposite_->getBounds();
    uint32_t regionY0 = ceildivpow2<uint32_t>(compositeBounds.y0, reduce);
    uint32_t regionY1 = ceildivpow2<uint32_t>(compositeBounds.y1, reduce);
    uint32_t unreducedTy0 = cp_.ty0_;
    uint32_t unreducedTileHeight = cp_.t_height_;
    uint32_t unreducedRegionY1 = compositeBounds.y1;
    tileCompletion_ = std::make_unique<TileCompletion>(
        tileCache_.get(), unreducedBounds, cp_.t_width_, cp_.t_height_,
        [this, regionY0, regionY1, unreducedTy0, unreducedTileHeight, unreducedRegionY1,
         reduce](uint16_t tileIndexBegin, uint16_t) {
          uint16_t numTileCols = tileCompletion_->getNumTileCols();
          uint16_t tileY = tileIndexBegin / numTileCols;
          // Compute exact reduced-resolution tile Y extents using ceildivpow2
          // on unreduced coordinates, since DWT reduction is not uniformly divisible
          uint32_t unreducedTileYBegin = unreducedTy0 + (uint32_t)tileY * unreducedTileHeight;
          uint32_t unreducedTileYEnd = std::min(
              unreducedTy0 + ((uint32_t)tileY + 1) * unreducedTileHeight, unreducedRegionY1);
          uint32_t tileGlobalYBegin = ceildivpow2<uint32_t>(unreducedTileYBegin, reduce);
          uint32_t tileGlobalYEnd = ceildivpow2<uint32_t>(unreducedTileYEnd, reduce);
          // Strip-relative y coordinates: the strip buffer starts at offset 0
          // for the current tile row
          uint32_t yBegin = 0;
          uint32_t yEnd = std::min(tileGlobalYEnd, regionY1) - std::max(tileGlobalYBegin, regionY0);

          uint16_t tileX0 = tileIndexBegin % numTileCols;
          uint16_t numSlatedCols = (uint16_t)tilesToDecompress_.getSlatedTileRect().width();

          // All compositing, band writing, and strip advancing must be serialized
          // to prevent races on the shared strip buffer.
          // Empty rows (yEnd <= yBegin) are inserted as sentinels so the drain
          // loop processes them in order — an early-return would lose the
          // advancement when an empty row completes before a prior row.
          std::lock_guard<std::mutex> lock(bandOrderMutex_);
          pendingBands_[tileY] = {yBegin, yEnd, tileX0, numSlatedCols};
          while(pendingBands_.count(nextBandTileY_))
          {
            auto& band = pendingBands_[nextBandTileY_];

            // Skip compositing and callback for empty rows (zero pixels
            // after resolution reduction).
            if(band.yEnd > band.yBegin)
            {
              // Check if any tile in this row already wrote strip output directly
              bool stripOutputHandled = false;
              for(uint16_t col = 0; col < band.numCols; col++)
              {
                uint16_t tileIndex =
                    static_cast<uint16_t>(nextBandTileY_ * numTileCols + (band.tileX0 + col));
                auto cacheEntry = tileCache_->get(tileIndex);
                if(cacheEntry && cacheEntry->processor &&
                   cacheEntry->processor->isStripOutputWritten())
                {
                  stripOutputHandled = true;
                  break;
                }
              }

              if(!stripOutputHandled)
              {
                // Composite all tiles in this row into the strip buffer
                for(uint16_t col = 0; col < band.numCols; col++)
                {
                  uint16_t tileIndex =
                      static_cast<uint16_t>(nextBandTileY_ * numTileCols + (band.tileX0 + col));
                  auto cacheEntry = tileCache_->get(tileIndex);
                  if(!cacheEntry || !cacheEntry->processor)
                    continue;
                  auto tileImage = cacheEntry->processor->getImage();
                  if(tileImage)
                  {
                    if(!scratchImage_->composite(tileImage))
                      success_ = false;
                  }
                }

                if(!ioBandCallback_(band.yBegin, band.yEnd, scratchImage_.get(), ioBandUserData_))
                  success_ = false;
              }

              // Release tile processors for this completed row
              for(uint16_t col = 0; col < band.numCols; col++)
              {
                uint16_t tileIndex =
                    static_cast<uint16_t>(nextBandTileY_ * numTileCols + (band.tileX0 + col));
                tileCache_->releaseForSwath(tileIndex);
              }
              MemoryManager::releaseFreedPages();
            }

            pendingBands_.erase(nextBandTileY_);

            // Advance strip buffer for the next tile row
            uint16_t nextTileY = nextBandTileY_ + 1;
            uint32_t nextUnreducedY0 = unreducedTy0 + (uint32_t)nextTileY * unreducedTileHeight;
            if(nextUnreducedY0 < unreducedRegionY1)
            {
              uint32_t nextUnreducedY1 =
                  std::min(nextUnreducedY0 + unreducedTileHeight, unreducedRegionY1);
              for(uint16_t i = 0; i < scratchImage_->numcomps; i++)
              {
                auto comp = scratchImage_->comps + i;
                comp->y0 =
                    ceildivpow2<uint32_t>(ceildiv<uint32_t>(nextUnreducedY0, comp->dy), reduce);
                uint32_t compY1 =
                    ceildivpow2<uint32_t>(ceildiv<uint32_t>(nextUnreducedY1, comp->dy), reduce);
                comp->h = compY1 - comp->y0;
              }
            }

            nextBandTileY_ = nextTileY;
          }
          // Wake the parser thread so it can schedule more tiles
          bandDrainCV_.notify_one();
        },
        nullptr, tilesToDecompress_.getSlatedTileRect());
  }
  auto slatedTiles = tilesToDecompress_.getSlatedTiles();
  if(!decompressImpl(slatedTiles))
    return false;

  // If all slated tiles were already cached, decompressImpl returned
  // immediately without starting any workers, but the new TileCompletion
  // (created in postReadHeader) doesn't know about them.  Mark them
  // complete so that grk_decompress_wait() won't hang.
  if(tileCompletion_)
  {
    for(auto tileIndex : slatedTiles)
    {
      auto cacheEntry = tileCache_->get(tileIndex);
      if(!cacheEntry)
        continue;
      auto proc = cacheEntry->processor;
      if(proc->isBestEffortDecompressed() ||
         (proc->getImage() && (!cacheEntry->dirty_ || proc->allSOTMarkersParsed())))
        tileCompletion_->complete(tileIndex);
    }
  }

  postMulti_ = postMultiTile();

  if(cp_.asynchronous_)
    return true;

  wait(nullptr);

  return success_;
}

bool CodeStreamDecompress::decompressImpl(std::set<uint16_t> pendingTiles)
{
  // Filter out fully cached tiles
  std::erase_if(pendingTiles, [this](uint16_t index) {
    auto cacheEntry = tileCache_->get(index);
    if(!cacheEntry)
      return false;
    auto proc = cacheEntry->processor;
    return proc->isBestEffortDecompressed() || (proc->getImage() && !cacheEntry->dirty_);
  });
  if(pendingTiles.empty())
    return true;

  // Extract LRU-evicted tiles that can be re-decompressed
  std::set<uint16_t> reDecompressTLM; // from compressed chunk cache
  std::set<uint16_t> reDecompressSeek; // from cached SOT offsets
  for(auto it = pendingTiles.begin(); it != pendingTiles.end();)
  {
    auto cacheEntry = tileCache_->get(*it);
    if(cacheEntry && cacheEntry->processor->getImage() && !cacheEntry->processor->getTile() &&
       cacheEntry->dirty_)
    {
      if(compressedChunkCache_ && compressedChunkCache_->contains(*it))
      {
        reDecompressTLM.insert(*it);
        it = pendingTiles.erase(it);
      }
      else if(cacheEntry->processor->allSOTMarkersParsed())
      {
        reDecompressSeek.insert(*it);
        it = pendingTiles.erase(it);
      }
      else
      {
        ++it;
      }
    }
    else
    {
      ++it;
    }
  }

  // Require tile_ to exist for differential updates — an LRU-evicted tile
  // without cached compressed data must go through the full decompress path
  bool doDifferential = !pendingTiles.empty();
  for(auto& tileIndex : pendingTiles)
  {
    auto cacheEntry = tileCache_->get(tileIndex);
    if(!cacheEntry || !cacheEntry->processor->getImage() || !cacheEntry->processor->getTile())
    {
      doDifferential = false;
      break;
    }
  }

  scratchImage_ = std::unique_ptr<GrkImage, RefCountedDeleter<GrkImage>>(
      new GrkImage(), RefCountedDeleter<GrkImage>());
  if(!activateScratch(false, scratchImage_.get()))
    return false;
  success_ = true;
  numTilesDecompressed_ = 0;

  // Re-decompress LRU-evicted tiles from compressed chunk cache
  for(auto tileIndex : reDecompressTLM)
  {
    auto cacheEntry = tileCache_->get(tileIndex);
    auto proc = cacheEntry->processor;
    auto cachedSeq = compressedChunkCache_->get(tileIndex);
    if(!cachedSeq || !proc->reinitForReDecompress())
      continue;
    auto generator = [this](ITileProcessor* tp) { return postMultiTile(tp); };
    auto decompressTask =
        genDecompressTileTLMTask(proc, cachedSeq, scratchImage_->getBounds(), generator);
    decompressTask();
    cacheEntry->dirty_ = false;
  }

  // Re-decompress LRU-evicted tiles by seeking to cached SOT offsets
  for(auto tileIndex : reDecompressSeek)
  {
    auto cacheEntry = tileCache_->get(tileIndex);
    auto proc = cacheEntry->processor;
    if(!proc->reinitForReDecompress())
      continue;
    if(!proc->decompressFromCachedTileParts())
      continue;
    if(!schedule(proc, true))
      break;
    cacheEntry->dirty_ = false;
  }

  if(pendingTiles.empty())
    return true;

  // prepare for differential decompression
  if(doDifferential)
  {
    differentialUpdate(scratchImage_.get());
    for(auto& tileIndex : pendingTiles)
    {
      auto cacheEntry = tileCache_->get(tileIndex);
      auto tileProcessor = cacheEntry->processor;
      if(!tileProcessor->differentialUpdate(headerImage_->getBounds()))
        return false;
      if(!schedule(tileProcessor, true))
        return false;
    }
    return true;
  }

  // one-time dispatch initialization
  if(!decompressStart_)
  {
    if(cp_.hasTLM())
      decompressStart_ = [this](auto& pt) { return startTLMDecompress(pt); };
    else
      decompressStart_ = [this](auto& pt) { return startSequentialDecompress(pt); };
  }

  return decompressStart_(pendingTiles);
}

bool CodeStreamDecompress::startTLMDecompress(std::set<uint16_t>& pendingTiles)
{
  // begin network fetch
  auto generator = [this](ITileProcessor* tp) { return postMultiTile(tp); };

  if(fetchByTile(pendingTiles, scratchImage_->getBounds(), generator))
    return true;

  // prepare TLM decompress
  tilePartFetchFlat_ = std::make_shared<TPFetchSeq>();
  tilePartFetchByTile_ =
      std::make_shared<std::unordered_map<uint16_t, std::shared_ptr<TPFetchSeq>>>();
  TPFetchSeq::genCollections(&cp_.tlmMarkers_->getTileParts(), pendingTiles, tilePartFetchFlat_,
                             tilePartFetchByTile_);

  // start decompress worker
  decompressWorker_ = std::thread([this, pendingTiles]() { decompressTLM(pendingTiles); });
  return true;
}

bool CodeStreamDecompress::startSequentialDecompress(std::set<uint16_t>& pendingTiles)
{
  // batch init
  if(doTileBatching())
  {
    batchTileUnscheduledSequential_ = (uint16_t)pendingTiles.size();
    batchTileScheduleHeadroomSequential_ =
        batchTileHeadroomIncrement(batchTileInitialRows_, batchTileUnscheduledSequential_);
    batchTileScheduledRows_ = batchTileInitialRows_;
  }

  // begin network fetch
  auto fetcher = stream_->getFetcher();
  if(fetcher)
  {
    auto chunkSize = cp_.t_width_ * cp_.t_height_;
    chunkBuffer_ = std::make_shared<ChunkBuffer<>>(chunkSize, markerCache_->getTileStreamStart(),
                                                   fetcher->size());
    fetcher->fetchChunks(chunkBuffer_);
    stream_->setChunkBuffer(chunkBuffer_);
  }

  // prepare sequential decompress
  decompressSequentialPrepare();

  // start decompress worker
  decompressWorker_ = std::thread([this, pendingTiles]() { decompressSequential(pendingTiles); });
  return true;
}

bool CodeStreamDecompress::schedule(ITileProcessor* tileProcessor, bool multiTile)
{
  if(cp_.hasTLM())
  {
    auto generator = [this](ITileProcessor* tp) { return postMultiTile(tp); };
    auto decompressTileTask =
        genDecompressTileTLMTask(tileProcessor, (*tilePartFetchByTile_)[tileProcessor->getIndex()],
                                 scratchImage_->getBounds(), generator);
    return decompressTileTask();
  }
  else
  {
    // T2 + T1 decompression
    // Once we schedule a processor for T1 compression, we will destroy it
    // regardless of success or not
    // When band callback is active, always use the multi-tile post path so that
    // tile data stays in scratchImage_ for the band writer to consume.
    bool useMultiPost = multiTile || ioBandCallback_;
    tileProcessor->scheduleAndRunDecompress(
        &coderPool_, useMultiPost ? scratchImage_->getBounds() : headerImage_->getBounds(),
        useMultiPost ? postMultiTile(tileProcessor) : postSingleTile(tileProcessor),
        decompressTileFutureManager_);

    return true;
  }
}

// TLM ///////////////////////////////////////////////////////////////

void CodeStreamDecompress::decompressTLM(const std::set<uint16_t>& pendingTiles)
{
  // 1 schedule all pending tiles
  if(!doTileBatching())
  {
    for(const auto& tileIndex : pendingTiles)
    {
      // Backpressure for strip-based band callback: block if this tile's row
      // is too far ahead of the row currently being drained, matching the
      // sequential path's throttle in sequentialParseAndSchedule().
      if(ioBandCallback_ && tileCompletion_)
      {
        uint16_t numTileCols = tileCompletion_->getNumTileCols();
        uint16_t tileY = tileIndex / numTileCols;
        std::unique_lock<std::mutex> lock(bandOrderMutex_);
        while(!(tileY < nextBandTileY_ + 2 || !success_))
          bandDrainCV_.wait_for(lock, std::chrono::milliseconds(100));
      }
      if(!schedule(getTileProcessor(tileIndex), true))
        break;
    }
    return;
  }

  // 2. push all pending tiles into the queue
  for(const auto& value : pendingTiles)
    batchTileQueueTLM_.push(value);

  // 3. schedule first  N rows
  uint16_t initialBatchCount =
      batchTileHeadroomIncrement(batchTileInitialRows_, (uint16_t)pendingTiles.size());
  batchTileScheduledRows_ = batchTileInitialRows_;
  {
    std::lock_guard<std::mutex> lock(batchTileQueueMutex_);
    for(size_t i = 0; i < initialBatchCount; ++i)
    {
      auto tileIndex = batchTileQueueTLM_.front();
      batchTileQueueTLM_.pop();
      if(!schedule(getTileProcessor(tileIndex), true))
        return; // Stop on scheduling failure
    }
  }

  // Wait for all all tiles to complete
  {
    std::unique_lock<std::mutex> lock(batchTileQueueMutex_);
    batchTileQueueCondition_.wait(lock, [this] { return batchTileQueueTLM_.empty(); });
  }
}

std::function<bool()> CodeStreamDecompress::genDecompressTileTLMTask(
    ITileProcessor* tileProcessor, const std::shared_ptr<TPFetchSeq>& tilePartFetchSeq,
    Rect32 unreducedImageBounds,
    std::function<std::function<void()>(ITileProcessor*)> postGenerator)
{
  auto post = postGenerator(tileProcessor);
  return [this, tileProcessor, &tilePartFetchSeq, unreducedImageBounds, post]() {
    try
    {
      if(!tileProcessor->decompressWithTLM(tilePartFetchSeq, &coderPool_, unreducedImageBounds,
                                           post, decompressTileFutureManager_))
      {
        return false;
      }
    }
    catch(const CorruptTLMException& ex)
    {
      return false;
    }
    catch(const CorruptSOTMarkerException& ex)
    {
      return false;
    }
    return true;
  };
}

// Sequential /////////////////////////////////////////////////////////////////////

void CodeStreamDecompress::decompressSequentialPrepare(void)
{
  stream_->seek(markerCache_->getTileStreamStart() + MARKER_BYTES);
  markerParser_.setSOT();
  tileCache_->resetSOTParsing();
  if(cp_.plmMarkers_)
    cp_.plmMarkers_->rewind();
  stream_->memAdvise(stream_->tell(), 0, GrkAccessPattern::ACCESS_RANDOM);
}

void CodeStreamDecompress::decompressSequential(const std::set<uint16_t>& pendingTiles)
{
  bool foundUnknownMarker = false;
  while(!markerParser_.endOfCodeStream() && !foundUnknownMarker)
  {
    // 1. parse and schedule tile
    try
    {
      if(!sequentialParseAndSchedule(true))
      {
        // If we've exhausted all pending tiles' markers or the codestream
        // ran out of data (truncated image), stop gracefully.
        if(tileCache_->allSlatedSOTMarkersParsed(pendingTiles) || markerParser_.endOfCodeStream() ||
           stream_->numBytesLeft() == 0)
          break;
        success_ = false;
        break;
      }
    }
    catch(const t1_t2::InvalidMarkerException& ime)
    {
      grklog.warn("Found invalid marker : 0x%.4x", ime.marker_);
      success_ = false;
      break;
    }

    // free chunks that have already been parsed
    if(chunkBuffer_)
      chunkBuffer_->free_before(stream_->tell() - chunkBuffer_->initialOffset());

    // 2. find next tile (or EOC)
    try
    {
      if(!markerParser_.endOfCodeStream() && !markerParser_.readSOTafterSOD())
      {
        grklog.error("Failed to find next SOT marker or EOC");
        success_ = false;
        break;
      }
    }
    catch([[maybe_unused]] const DecodeUnknownMarkerAtEndOfTileException& e)
    {
      foundUnknownMarker = true;
    }

    // check for corrupt Adobe files where 5 tile parts per tile are signalled
    // but there are actually 6.
    if(markerParser_.currId() == SOT && tileCache_->allSlatedSOTMarkersParsed(pendingTiles) &&
       markerParser_.checkForIllegalTilePart())
    {
      success_ = false;
      break;
    }

    if(tileCache_->allSlatedSOTMarkersParsed(pendingTiles))
      break;
  }

  // Best-effort: schedule incomplete tiles (those with some tile parts parsed
  // but not all) for decompression with whatever data they have.
  for(auto tileIndex : pendingTiles)
  {
    auto cached = tileCache_->get(tileIndex);
    if(cached && cached->processor && !cached->processor->scheduledForDecompression() &&
       cached->processor->hasUnparsedTileParts())
    {
      cached->processor->setTruncated();
      cached->processor->prepareForDecompression();
      if(!cached->processor->hasError())
        schedule(cached->processor, true);
    }
  }

  // Mark tiles that were never parsed (missing from truncated codestream)
  // as complete so wait() doesn't hang.  Tiles that WERE parsed are still
  // being decompressed asynchronously and must not be completed here.
  if(tileCompletion_)
  {
    for(auto tileIndex : pendingTiles)
    {
      auto cached = tileCache_->get(tileIndex);
      if(!cached || !cached->processor || cached->processor->hasUnparsedTileParts())
        tileCompletion_->complete(tileIndex);
    }
  }
}

bool CodeStreamDecompress::sequentialParseAndSchedule(bool multiTile)
{
  if(markerParser_.currId() != SOT)
    return false;

  /* Parse tile parts until we satisfy one of the conditions below:
   * 1. read a complete slated tile
   * 2. read EOC
   * 3. run out of data
   */
  bool concurrentTileParsing = TFSingleton::num_threads() > 1 &&
                               tilesToDecompress_.getSlatedTiles().size() > 1 &&
                               stream_->isMemStream();
  while(((currTileIndex_ == -1) || !tilesToDecompress_.isSlated((uint16_t)currTileIndex_) ||
         (currTileProcessor_ && !currTileProcessor_->allSOTMarkersParsed())) &&
        markerParser_.currId() != EOC && stream_->numBytesLeft() != 0)
  {
    try
    {
      auto [processed, length] = markerParser_.processMarker();
      if(!processed)
        return false;
    }
    catch([[maybe_unused]] const CorruptSOTMarkerException& csme)
    {
      return false;
    }
    // Attempt to read next SOT marker. If we hit an invalid marker, we still try to
    // decompress the tile, as truncated.
    try
    {
      if(!markerParser_.readId(false))
        break;
    }
    catch(const t1_t2::InvalidMarkerException& ime)
    {
      break;
    }

    if(!tilesToDecompress_.isSlated((uint16_t)currTileIndex_))
      continue;

    bool completelyParsed = currTileProcessor_->allSOTMarkersParsed();
    IStream* bifurcatedStream = nullptr;
    if(concurrentTileParsing)
    {
      // skip to beginning of next SOT marker (or EOC)
      auto tpl = currTilePartInfo_.tilePartLength_;
      uint32_t adjust = sotMarkerSegmentLen + sizeof(uint16_t);
      if(tpl < adjust)
        break;
      bifurcatedStream = stream_->bifurcate();
      auto skip = (uint64_t)(tpl - adjust);
      skip = std::min(skip, stream_->numBytesLeft());
      if(!stream_->skip((int64_t)skip))
      {
        delete bifurcatedStream;
        break;
      }
    }
    // parse tile part
    if(!currTileProcessor_->parseTilePart(concurrentTileParsing ? &tileMarkerParsers_ : nullptr,
                                          bifurcatedStream, markerParser_.currId(),
                                          currTilePartInfo_))
      return false;

    // read next SOT if there are remaining unparsed tile parts for this tile
    if(!completelyParsed && !markerParser_.readSOTorEOC())
      break;
  }

  if(!currTileProcessor_)
  {
    grklog.error("sequentialParseAndSchedule: no slated SOT markers found");
    return false;
  }
  auto tileProcessor = currTileProcessor_;
  currTileProcessor_ = nullptr;
  currTileIndex_ = -1;

  // batch
  tileProcessor->prepareForDecompression();
  if(doTileBatching())
  {
    std::unique_lock<std::mutex> lock(batchTileQueueMutex_);
    if(batchTileScheduleHeadroomSequential_ > 0)
    {
      batchTileScheduleHeadroomSequential_--;
      batchTileUnscheduledSequential_--;
    }
    else
    {
      batchTileQueueSequential_.push(tileProcessor);
      // Block the parser until the queue drains, preventing all compressed
      // tile data from being loaded into memory at once.
      // The queue drains when scheduleTileBatch() calls batchDequeueSequential().
      if(tileCompletion_)
      {
        uint16_t numTileCols = tilesToDecompress_.getSlatedTileRect().width();
        uint16_t maxQueued = numTileCols * (maxRowsAhead_ + 1);
        batchTileQueueCondition_.wait(lock, [this, maxQueued] {
          return batchTileQueueSequential_.size() <= maxQueued || !success_;
        });
      }
      return true;
    }
  }

  // push onto consumer queue if active, otherwise schedule directly
  if(decompressQueue_)
  {
    decompressQueue_->push(
        [this, tileProcessor, multiTile]() { schedule(tileProcessor, multiTile); });
    return true;
  }

  // Backpressure for strip-based band callback: block the parser thread if
  // this tile's row is too far ahead of the row currently being drained.
  // Without this, all tiles decompress into memory before any row is written.
  // Use wait_for with a timeout so we periodically re-check success_ even if
  // the Taskflow task graph skips the post callback on error.
  if(ioBandCallback_ && tileCompletion_)
  {
    uint16_t numTileCols = tileCompletion_->getNumTileCols();
    uint16_t tileY = tileProcessor->getIndex() / numTileCols;
    std::unique_lock<std::mutex> lock(bandOrderMutex_);
    while(!(tileY < nextBandTileY_ + 2 || !success_))
      bandDrainCV_.wait_for(lock, std::chrono::milliseconds(100));
  }

  // schedule
  return schedule(tileProcessor, multiTile);
}

/// Single Tile ////////////////////////////////////////////////////

bool CodeStreamDecompress::decompressTile(uint16_t tileIndex)
{
  multiTileComposite_->postReadHeader(&cp_);

  // 1. sanity check on tile index
  uint16_t numTilesToDecompress = (uint16_t)(cp_.t_grid_width_ * cp_.t_grid_height_);
  if(tileIndex >= numTilesToDecompress)
  {
    grklog.error("Tile index %u is greater than maximum tile index %u", tileIndex,
                 numTilesToDecompress - 1);
    return false;
  }
  tileCache_->init(numTilesToDecompress);

  // 2. simply return tile image if it already exists and is not dirty
  // i.e. no differential decompression
  auto cacheEntry = tileCache_->get(tileIndex);
  if(cacheEntry && cacheEntry->processor && cacheEntry->processor->getImage() &&
     !cacheEntry->dirty_)
    return true;

  // 3. schedule / execute tile decompression
  if(!decompressTileImpl(tileIndex))
    return false;

  // 4. wait if synchronous
  if(!cp_.asynchronous_)
    wait(nullptr);

  return true;
}

bool CodeStreamDecompress::decompressTileImpl(uint16_t tileIndex)
{
  scratchImage_ = std::unique_ptr<GrkImage, RefCountedDeleter<GrkImage>>(
      new GrkImage(), RefCountedDeleter<GrkImage>());

  auto cacheEntry = tileCache_->get(tileIndex);
  if(cacheEntry && cacheEntry->processor && cacheEntry->processor->getImage())
  {
    // if there are already processed packets, then this is a differential decompression
    // so perform differential update and continue on to decompression
    auto proc = cacheEntry->processor;
    if(proc->getNumProcessedPackets())
    {
      differentialUpdate(scratchImage_.get());
      if(!proc->differentialUpdate(headerImage_->getBounds()))
        return false;
    }
    std::function<void(GrkImage*)> deleter = NoopDeleter();
    activeImage_ =
        std::unique_ptr<GrkImage, std::function<void(GrkImage*)>>(proc->getImage(), deleter);
    proc->getTCP()->tilePartCounter_ = 0;
  }
  else
  {
    // otherwise prepare for full decompression
    std::function<void(GrkImage*)> deleter = RefCountedDeleter<GrkImage>();
    activeImage_ =
        std::unique_ptr<GrkImage, std::function<void(GrkImage*)>>(new GrkImage(), deleter);
    headerImage_->copyHeaderTo(activeImage_.get());
    /* Compute the dimension of the desired tile*/
    uint16_t tile_x = tileIndex % cp_.t_grid_width_;
    uint16_t tile_y = tileIndex / cp_.t_grid_width_;
    auto imageBounds = headerImage_->getBounds();
    auto tileBounds = cp_.getTileBounds(imageBounds, tile_x, tile_y);
    // crop tile bounds with image bounds
    auto croppedImageBounds = imageBounds.intersection(tileBounds);
    if(!imageBounds.empty() && !tileBounds.empty() && !croppedImageBounds.empty())
    {
      activeImage_->x0 = croppedImageBounds.x0;
      activeImage_->y0 = croppedImageBounds.y0;
      activeImage_->x1 = croppedImageBounds.x1;
      activeImage_->y1 = croppedImageBounds.y1;
    }
    else
    {
      grklog.warn("Decompress bounds <%u,%u,%u,%u> do not overlap with requested tile %u. "
                  "Decompressing full image",
                  imageBounds.x0, imageBounds.y0, imageBounds.x1, imageBounds.y1, tileIndex);
      croppedImageBounds = imageBounds;
    }
    activeImage_->subsampleAndReduce(cp_.codingParams_.dec_.reduce_);
    activeImage_->postReadHeader(&cp_);
  }

  tilesToDecompress_.slate(tileIndex);
  if(!activateScratch(true, scratchImage_.get()))
    return false;
  scratchImage_->has_multiple_tiles = false;
  // decompress tile
  if(!cp_.hasTLM())
  {
    bool invalidMarker = false;
    auto tileProcessor = cacheEntry ? cacheEntry->processor : nullptr;

    if(tileProcessor && tileProcessor->allSOTMarkersParsed() &&
       tileProcessor->getNumProcessedPackets() > 0)
    {
      // (a) Fully parsed + packets ready → schedule T1/T2 directly
      if(!schedule(tileProcessor, false))
        return false;
    }
    else if(tileProcessor && tileProcessor->allSOTMarkersParsed())
    {
      // (b) SOTs known from previous decode, packets not parsed →
      //     direct-seek parse using cached tile-part offsets
      if(!tileProcessor->decompressFromCachedTileParts())
        return false;
      if(!schedule(tileProcessor, false))
        return false;
    }
    else
    {
      // (c) No SOT info → full sequential parse (first decode)
      decompressSequentialPrepare();
      try
      {
        if(!sequentialParseAndSchedule(false))
          return false;
      }
      catch(const t1_t2::InvalidMarkerException& ime)
      {
        grklog.warn("Found invalid marker 0x%.4x in tile %u header", ime.marker_, tileIndex);
        invalidMarker = true;
      }

      // Check for corrupt Adobe images where a final tile part is not parsed
      // due to incorrectly-signalled number of tile parts.
      if(!invalidMarker && !cacheEntry)
      {
        try
        {
          if(markerParser_.readSOTorEOC() && markerParser_.currId() == SOT)
          {
            if(markerParser_.checkForIllegalTilePart())
              return false;
          }
        }
        catch(const t1_t2::InvalidMarkerException& ime)
        {
          grklog.warn("Found invalid marker 0x%.4x in tile %u header", ime.marker_, tileIndex);
        }
      }
    }

    return true;
  }

  // TLM
  std::set<uint16_t> slated = {tileIndex};
  auto generator = [this](ITileProcessor* tp) {
    return postSingleTile(tp); // Return the result directly
  };
  if(fetchByTile(slated, headerImage_->getBounds(), generator))
    return true;

  const auto tileProcessor = cacheEntry ? cacheEntry->processor : getTileProcessor(tileIndex);
  auto post = postSingleTile(tileProcessor);
  tilePartFetchFlat_ = std::make_shared<TPFetchSeq>();
  tilePartFetchFlat_->push_back(tileIndex, cp_.tlmMarkers_->getTileParts()[tileIndex]);
  try
  {
    if(!tileProcessor->decompressWithTLM(tilePartFetchFlat_, &coderPool_, headerImage_->getBounds(),
                                         post, decompressTileFutureManager_))
    {
      return false;
    }
  }
  catch(const CorruptTLMException& ex)
  {
    return false;
  }
  catch(const CorruptSOTMarkerException& ex)
  {
    return false;
  }
  return true;
}

std::function<void()> CodeStreamDecompress::postSingleTile(ITileProcessor* tileProcessor)
{
  return [this, tileProcessor]() {
    auto rawActive = activeImage_.release();
    tileProcessor->post_decompressT2T1(scratchImage_.get());
    scratchImage_->transferDataTo(rawActive);
    postProcess(rawActive);
    tileProcessor->setImage(rawActive);
    tileCache_->setDirty(tileProcessor->getIndex(), false);
    if(cp_.decompressCallback_)
    {
      cp_.decompressCallback_(this, tileProcessor->getIndex(), rawActive,
                              cp_.codingParams_.dec_.reduce_, cp_.decompressCallbackUserData_);
    }
  };
}

// Post Processing/////////////////////////////////////////////////

bool CodeStreamDecompress::postProcess(GrkImage* img)
{
  if(!img->postProcess())
    return false;
  img->filterComponents(cp_.compsToDecompress_);
  return postPostProcess_ ? postPostProcess_(img) : true;
}

/**
 * @brief Final post-processing lambda, executed by wait(nullptr) after all tiles complete.
 *
 * For non-skipAllocateComposite mode: transfers pixel data from scratchImage_
 * to multiTileComposite_ and applies postProcess (colour space, ICC, precision).
 * After this runs, scratchImage_ is empty and multiTileComposite_ has the
 * final post-processed image.  getImage() returns multiTileComposite_.
 *
 * This lambda is stored in postMulti_ and only runs during a full wait
 * (wait(nullptr)), NOT during the swath-based tileCompletion early-return path.
 */
std::function<void()> CodeStreamDecompress::postMultiTile(void)
{
  return [this]() {
    if(!success_)
      return;
    uint16_t numTilesToDecompress = (uint16_t)tilesToDecompress_.getSlatedTiles().size();
    if(numTilesDecompressed_ == 0)
    {
      grklog.error("No tiles were decompressed.");
      success_ = false;
      return;
    }
    else if(numTilesDecompressed_ < numTilesToDecompress)
    {
      uint32_t decompressed = numTilesDecompressed_;
      grklog.warn("Only %u out of %u tiles were decompressed", decompressed, numTilesToDecompress);
    }
    if(!cp_.codingParams_.dec_.skipAllocateComposite_)
    {
      if(ioBandCallback_)
      {
        // incremental band writes already consumed the data from scratchImage_;
        // skip transfer and postProcess
      }
      else
      {
        scratchImage_->transferDataTo(multiTileComposite_.get());
        success_ = postProcess(multiTileComposite_.get());
      }
    }
  };
}

/**
 * @brief Per-tile post-processing lambda, executed when each tile's decompression future completes.
 *
 * Calls post_decompressT2T1(scratchImage_) which:
 * - Multi-tile (has_multiple_tiles=true): extracts tile data into a new image,
 *   sets image_ on the processor.  getImage() will return this image.
 * - Single-tile (has_multiple_tiles=false): transfers tile data INTO scratchImage_.
 *   image_ is NOT set, so getImage() returns null.  Data stays in scratchImage_
 *   until postMultiTile() (no-arg) transfers it to multiTileComposite_.
 */
std::function<void()> CodeStreamDecompress::postMultiTile(ITileProcessor* tileProcessor)
{
  return [this, tileProcessor]() {
    auto releaseThrottle = [this]() {
      if(maxDecompressInFlight_ > 0)
      {
        {
          std::lock_guard<std::mutex> lock(decompressThrottleMutex_);
          decompressInFlight_--;
        }
        decompressThrottleCV_.notify_one();
      }
      // Wake the fetcher so it can schedule more HTTP requests
      auto fetcher = stream_->getFetcher();
      if(fetcher)
        fetcher->notifyThrottleRelease();
    };

    if(!success_)
    {
      releaseThrottle();
      // Always mark tile as complete so row callbacks can fire and
      // backpressure unblocks, even when the decompress failed.
      if(tileCompletion_)
        tileCompletion_->complete(tileProcessor->getIndex());
      return;
    }
    tileProcessor->post_decompressT2T1(scratchImage_.get());
    tileProcessor->setBestEffortDecompressed();
    numTilesDecompressed_++;

    // Release throttle early: decompression is done, free the slot for other tiles.
    // This prevents deadlock when strip-based band callback blocks tiles from future rows.
    releaseThrottle();

    auto tileImage = tileProcessor->getImage();
    if(!cp_.codingParams_.dec_.skipAllocateComposite_ && scratchImage_->has_multiple_tiles &&
       tileImage)
    {
      // When using strip-based band callback, skip composite here;
      // it will be done in the row callback after all tiles in the row are complete.
      if(!ioBandCallback_)
        success_ = scratchImage_->composite(tileImage);
    }
    // complete tile
    auto tileIndex = tileProcessor->getIndex();
    if(cp_.decompressCallback_)
      cp_.decompressCallback_(this, tileIndex, tileImage, cp_.codingParams_.dec_.reduce_,
                              cp_.decompressCallbackUserData_);

    if(tileCompletion_)
      tileCompletion_->complete(tileIndex);
    else
      tileProcessor->release();
  };
}

//////////////////////////////////////////////////////////////////

bool CodeStreamDecompress::setDecompressRegion(RectD region)
{
  auto image = headerImage_;
  auto imageBounds = headerImage_->getBounds();

  if(region != RectD(0, 0, 0, 0))
  {
    const double val[4] = {region.x0, region.y0, region.x1, region.y1};
    bool allLessThanOne = true;
    for(uint8_t i = 0; i < 4; ++i)
    {
      if(val[i] > 1.0)
        allLessThanOne = false;
    }
    if(allLessThanOne)
    {
      auto w = double(image->x1 - image->x0);
      auto h = double(image->y1 - image->y0);
      region.x0 = (double)floor(val[0] * w);
      region.y0 = (double)floor(val[1] * h);
      region.x1 = (double)ceil(val[2] * w);
      region.y1 = (double)ceil(val[3] * h);
    }
    Rect16 tilesToDecompress;
    auto canvasRegion = Rect32((uint32_t)region.x0 + image->x0, (uint32_t)region.y0 + image->y0,
                               (uint32_t)region.x1 + image->x0, (uint32_t)region.y1 + image->y0);
    /* Left */
    if(canvasRegion.x0 > image->x1)
    {
      grklog.error("Left position of the decompress region (%u)"
                   " is outside of the image area (Xsiz=%u).",
                   canvasRegion.x0, image->x1);
      return false;
    }
    else
    {
      tilesToDecompress.x0 = uint16_t((canvasRegion.x0 - cp_.tx0_) / cp_.t_width_);
      multiTileComposite_->x0 = canvasRegion.x0;
    }

    /* Up */
    if(canvasRegion.y0 > image->y1)
    {
      grklog.error("Top position of the decompress region (%u)"
                   " is outside of the image area (Ysiz=%u).",
                   canvasRegion.y0, image->y1);
      return false;
    }
    else
    {
      tilesToDecompress.y0 = uint16_t((canvasRegion.y0 - cp_.ty0_) / cp_.t_height_);
      multiTileComposite_->y0 = canvasRegion.y0;
    }

    /* Right */
    if(canvasRegion.x1 > image->x1)
    {
      grklog.warn("Right position of the decompress region (%u)"
                  " is outside the image area (Xsiz=%u).",
                  canvasRegion.x1, image->x1);
      tilesToDecompress.x1 = cp_.t_grid_width_;
      multiTileComposite_->x1 = image->x1;
      canvasRegion.x1 = image->x1;
    }
    else
    {
      // avoid divide by zero
      if(cp_.t_width_ == 0)
        return false;
      tilesToDecompress.x1 = uint16_t(ceildiv<uint32_t>(canvasRegion.x1 - cp_.tx0_, cp_.t_width_));
      multiTileComposite_->x1 = canvasRegion.x1;
    }

    /* Bottom */
    if(canvasRegion.y1 > image->y1)
    {
      grklog.warn("Bottom position of the decompress region (%u)"
                  " is outside of the image area (Ysiz=%u).",
                  canvasRegion.y1, image->y1);
      tilesToDecompress.y1 = cp_.t_grid_height_;
      multiTileComposite_->y1 = image->y1;
      canvasRegion.y1 = image->y1;
    }
    else
    {
      // avoid divide by zero
      if(cp_.t_height_ == 0)
        return false;
      tilesToDecompress.y1 =
          (uint16_t)(ceildiv<uint32_t>(canvasRegion.y1 - cp_.ty0_, cp_.t_height_));
      multiTileComposite_->y1 = canvasRegion.y1;
    }
    tilesToDecompress_.slate(tilesToDecompress);
    region_ = canvasRegion;

    if(cp_.asynchronous_ && cp_.simulate_synchronous_)
    {
      tileCompletion_ = std::make_unique<TileCompletion>(
          tileCache_.get(), imageBounds, cp_.t_width_, cp_.t_height_,
          [this](uint16_t tileIndexBegin, uint16_t) { onRowCompleted(tileIndexBegin); },
          [this]() {
            scheduleTileBatch();
            // Wake the fetcher so it can re-check the row-based throttle
            auto fetcher = stream_->getFetcher();
            if(fetcher)
              fetcher->notifyThrottleRelease();
          },
          tilesToDecompress);
    }
    if(!multiTileComposite_->subsampleAndReduce(cp_.codingParams_.dec_.reduce_))
      return false;

    grklog.info("Decompress region canvas coordinates:\n(%u,%u,%u,%u)", multiTileComposite_->x0,
                multiTileComposite_->y0, multiTileComposite_->x1, multiTileComposite_->y1);
    auto scaledX0 = double(multiTileComposite_->x0 - image->x0) / double(image->width());
    auto scaledY0 = double(multiTileComposite_->y0 - image->y0) / double(image->height());
    auto scaledX1 = double(multiTileComposite_->x1 - image->x0) / double(image->width());
    auto scaledY1 = double(multiTileComposite_->y1 - image->y0) / double(image->height());
    grklog.info("Decompress region scaled coordinates:\n(%1.17f,%1.17f,%1.17f,%1.17f)", scaledX0,
                scaledY0, scaledX1, scaledY1);
    grklog.info("Decompress region scaled coordinates in {<top>,<left>},{<height>,<width>} format"
                ":\n\"{%1.17f,%1.17f},{%1.17f,%1.17f}\"",
                scaledY0, scaledX0, scaledY1 - scaledY0, scaledX1 - scaledX0);
    grklog.info("Full image canvas coordinates:\n(%u,%u,%u,%u)", image->x0, image->y0, image->x1,
                image->y1);
  }

  return true;
}

// batching

bool CodeStreamDecompress::doTileBatching(void)
{
  return tilesToDecompress_.getSlatedTiles().size() > 1 && cp_.asynchronous_;
}

uint16_t CodeStreamDecompress::batchTileHeadroomIncrement(uint16_t numRows, uint16_t tilesLeft)
{
  return std::min(uint16_t(tilesToDecompress_.getSlatedTileRect().width() * numRows), tilesLeft);
}

bool CodeStreamDecompress::batchDequeueSequential(void)
{
  while((batchTileScheduleHeadroomSequential_ > 0) && !batchTileQueueSequential_.empty())
  {
    auto t = batchTileQueueSequential_.front();
    if(!schedule(t, true) && !success_)
      return false;
    batchTileQueueSequential_.pop();
    batchTileScheduleHeadroomSequential_--;
    batchTileUnscheduledSequential_--;
  }

  return true;
}

// Wait  //////////////////////////////////////////////

void CodeStreamDecompress::wait(uint16_t tile_index)
{
  // When swath-based waiting is active the fetch completes progressively;
  // don't block here waiting for the entire fetch operation.
  if(!tileCompletion_)
  {
    for(auto& ff : fetchByTileFutures_)
    {
      if(ff.valid())
      {
        ff.wait();
        bool success = ff.get();
        if(!success)
        {
          grklog.error("CodeStreamDecompress::wait : failed to get fetch future for tile %d",
                       tile_index);
          return;
        }
      }
    }
    fetchByTileFutures_.clear();

    // close consumer queue and join consumer thread
    if(decompressQueue_)
      decompressQueue_->close();
    if(decompressConsumer_.joinable())
      decompressConsumer_.join();
  }

  decompressTileFutureManager_.wait(tile_index);
}
/**
 * @brief Wait for tile decompression to complete.
 *
 * Two calling modes:
 *
 * **Swath-based (swath != nullptr, tileCompletion_ active):**
 * Returns as soon as the requested swath tiles are decompressed.
 * Tile data is available via entry->processor->getImage() for multi-tile
 * images (has_multiple_tiles=true), or in scratchImage_ for single-tile
 * images.  NOTE: postMulti_() does NOT run in this path, so
 * scratchImage_ data is raw (no postProcess).  Callers needing
 * post-processed data for single-tile images must subsequently call
 * wait(nullptr) or use getImage() which triggers a full wait.
 *
 * **Full wait (swath == nullptr, or no tileCompletion_):**
 * Joins all worker threads, waits for all tile futures, and runs
 * postMulti_() which transfers scratchImage_ data to
 * multiTileComposite_ with post-processing applied.
 */
void CodeStreamDecompress::wait(grk_wait_swath* swath)
{
  // 1. Swath-based early return: tile data ready but NOT post-processed
  if(swath && tileCompletion_)
  {
    bool rc = tileCompletion_->wait(swath);
    if(!rc)
      return;
    return;
  }

  // 2a. Flush batch queues when using non-swath full wait.
  // When tileCompletion_ is active but wait(nullptr) is called (not swath-based),
  // the back-pressure logic in scheduleTileBatch() prevents scheduling queued tiles
  // because lastCleared never advances (no swath consumer). Force-schedule all
  // remaining tiles so the worker thread can exit.
  if(tileCompletion_ && doTileBatching())
  {
    // Remove the scheduling limit so scheduleTileBatch can drain
    batchTileScheduledRows_ = 0;
    tileCompletion_->setLastClearedTileY(INT16_MAX);
    scheduleTileBatch();
  }

  // 2b. wait for sequential parse
  if(decompressWorker_.joinable())
    decompressWorker_.join();

  // 3. wait for all fetch operations
  for(auto& ff : fetchByTileFutures_)
  {
    if(ff.valid())
    {
      ff.wait();
      bool success = ff.get();
      if(!success)
      {
        grklog.error("CodeStreamDecompress::wait : failed to get fetch future");
        return;
      }
    }
  }
  fetchByTileFutures_.clear();

  // 3b. close consumer queue and join consumer thread
  if(decompressQueue_)
    decompressQueue_->close();
  if(decompressConsumer_.joinable())
    decompressConsumer_.join();

  // 4. wait for all tile decompression operations
  if(!success_)
    decompressTileFutureManager_.cancelAll();
  decompressTileFutureManager_.waitAndClear();

  // 5. Run postMulti_: transfers scratchImage_ data to multiTileComposite_
  // and applies postProcess (colour, ICC, precision, etc.).
  // Only runs in this full-wait path, never after tileCompletion early return.
  if(postMulti_)
  {
    postMulti_();
    postMulti_ = nullptr;
  }
}

/**
 * @brief Schedule SIMD-accelerated tile-to-swath copies for decoded tiles.
 *
 * For each tile in the swath range, retrieves the decoded tile image and
 * submits a Taskflow task that converts int32 component data to the
 * caller's output buffer (int8/int16/int32, signed or unsigned).
 *
 * ## Tile image lookup
 *
 * - **Multi-tile (has_multiple_tiles=true):** post_decompressT2T1 calls
 *   extractFrom() which sets image_ on the tile processor.  getImage()
 *   returns this per-tile image directly — no composite needed.
 *
 * - **Single-tile (has_multiple_tiles=false):** post_decompressT2T1 calls
 *   transferDataFrom() into scratchImage_ but does NOT set image_.
 *   getImage() returns null, so the fallback to scratchImage_ is used.
 *   IMPORTANT: scratchImage_ only contains valid data if the caller used
 *   the swath-based wait path (wait(swath) with tileCompletion_).
 *   If wait(nullptr) was called instead, postMulti_() will have already
 *   transferred data from scratchImage_ to multiTileComposite_, leaving
 *   scratchImage_ empty.  In that case, callers should use getImage()
 *   (which returns multiTileComposite_) rather than this function.
 *
 * @param swath  Tile range and grid info from grk_decompress_wait
 * @param buf    Output buffer descriptor (data pointer, layout, precision)
 */
void CodeStreamDecompress::scheduleSwathCopy(const grk_wait_swath* swath, grk_swath_buffer* buf)
{
  if(!swath || !buf || !buf->data || buf->prec == 0)
    return;

  for(uint16_t ty = swath->tile_y0; ty < swath->tile_y1; ++ty)
  {
    for(uint16_t tx = swath->tile_x0; tx < swath->tile_x1; ++tx)
    {
      const uint16_t tidx = static_cast<uint16_t>(ty * swath->num_tile_cols + tx);

      GrkImage* tileImg = nullptr;
      auto* entry = tileCache_->get(tidx);
      if(entry)
      {
        tileImg = entry->processor->getImage();
        // Single-tile path: image_ is not set; data is in scratchImage_
        // after the swath-based wait (tileCompletion early return).
        // If wait(nullptr) has run instead, scratchImage_ will be empty
        // and callers must use getImage() / multiTileComposite_.
        if(!tileImg)
          tileImg = scratchImage_.get();
      }
      else
        tileImg = multiTileComposite_.get();

      if(!tileImg)
        continue;

      // Submit the copy+convert task to the shared Taskflow executor.
      tf::Taskflow tf_copy;
      tf_copy.emplace([tileImg, buf]() { hwy_copy_tile_to_swath(tileImg, buf); });

      swathCopyFutureManager_.add(tidx, TFSingleton::get().run(std::move(tf_copy)));
    }
  }
}

void CodeStreamDecompress::waitSwathCopy()
{
  swathCopyFutureManager_.waitAndClear();
}

void CodeStreamDecompress::onRowCompleted(uint16_t tileIndexBegin)
{
  if(!doTileBatching())
    return;

  // Back-pressure: skip scheduling if producer is too far ahead of consumer.
  // The consumer (wait()) will call scheduleTileBatch() after releasing rows.
  if(tileCompletion_)
  {
    int32_t completedRow = tileIndexBegin / tileCompletion_->getNumTileCols();
    int32_t lastCleared = tileCompletion_->getLastClearedTileY();
    if(completedRow - lastCleared > maxRowsAhead_)
      return;
  }

  scheduleTileBatch();
}

void CodeStreamDecompress::scheduleTileBatch()
{
  // Compute how many rows we're allowed to schedule based on the consumer's position.
  // We allow up to maxRowsAhead_ rows beyond what the consumer has cleared.
  uint16_t rowsToSchedule = batchTileNextRows_;

  if(tileCompletion_)
  {
    int32_t lastCleared = tileCompletion_->getLastClearedTileY();
    int32_t maxAllowedRow = lastCleared + maxRowsAhead_ + 1;
    // If the consumer is waiting for rows beyond the back-pressure limit,
    // extend the limit to avoid deadlock.
    int32_t neededRow = tileCompletion_->getNeededTileY1();
    if(neededRow > maxAllowedRow)
      maxAllowedRow = neededRow;
    if(batchTileScheduledRows_ >= maxAllowedRow)
      return;
    // Only schedule enough rows to fill the window, not the full batchTileNextRows_
    int32_t rowsBudget = maxAllowedRow - batchTileScheduledRows_;
    if(rowsBudget < rowsToSchedule)
      rowsToSchedule = static_cast<uint16_t>(rowsBudget);
  }

  uint16_t numTileCols = tilesToDecompress_.getSlatedTileRect().width();

  if(cp_.hasTLM())
  {
    {
      std::unique_lock<std::mutex> lock(batchTileQueueMutex_);
      size_t tilesToSchedule =
          batchTileHeadroomIncrement(rowsToSchedule, (uint16_t)batchTileQueueTLM_.size());
      for(size_t i = 0; i < tilesToSchedule; ++i)
      {
        auto tileIndex = batchTileQueueTLM_.front();
        batchTileQueueTLM_.pop();
        if(!schedule(getTileProcessor(tileIndex), true))
        {
          lock.unlock();
          batchTileQueueCondition_.notify_one();
          return;
        }
      }
      if(numTileCols > 0)
        batchTileScheduledRows_ += rowsToSchedule;
    }
  }
  else
  {
    std::unique_lock<std::mutex> lock(batchTileQueueMutex_);
    batchTileScheduleHeadroomSequential_ +=
        batchTileHeadroomIncrement(rowsToSchedule, (uint16_t)batchTileUnscheduledSequential_);
    batchDequeueSequential();
    if(numTileCols > 0)
      batchTileScheduledRows_ += rowsToSchedule;
  }
  batchTileQueueCondition_.notify_one();
}

// Producer-consumer pipeline ///////////////////////////////////////

void CodeStreamDecompress::startDecompressConsumer(uint16_t maxInFlight)
{
  maxDecompressInFlight_ = maxInFlight;
  decompressQueue_ = std::make_unique<ConcurrentQueue<std::function<void()>>>();
  decompressConsumer_ = std::thread([this]() {
    std::function<void()> task;
    while(decompressQueue_->pop(task))
    {
      {
        std::unique_lock<std::mutex> lock(decompressThrottleMutex_);
        decompressThrottleCV_.wait(lock,
                                   [this] { return decompressInFlight_ < maxDecompressInFlight_; });
        decompressInFlight_++;
      }
      task();
    }
  });
}

// Fetching ////////////////////////////////////////////////////////

bool CodeStreamDecompress::fetchByTile(
    std::set<uint16_t>& slated, Rect32 unreducedImageBounds,
    std::function<std::function<void()>(ITileProcessor*)> postGenerator)
{
  auto fetcher = stream_->getFetcher();
  if(!fetcher)
    return false;

  maxFetchedTileRow_.store(-1, std::memory_order_release);

  startDecompressConsumer(std::min((uint16_t)TFSingleton::num_threads(),
                                   (uint16_t)((maxRowsAhead_ + 1) * cp_.t_grid_width_)));

  // Back pressure: prevent the fetcher from scheduling more HTTP requests
  // when either (a) too many tiles are in-flight for decompression, or
  // (b) the fetcher is too far ahead of the consumer (swath-based release).
  auto numTileCols = cp_.t_grid_width_;
  fetcher->setFetchThrottle([this]() {
    // Row-based: don't fetch tiles more than maxRowsAhead_ rows beyond
    // the last row released by the consumer.
    if(tileCompletion_)
    {
      int32_t lastCleared = tileCompletion_->getLastClearedTileY();
      int32_t maxAllowed = lastCleared + maxRowsAhead_ + 2;
      if(maxFetchedTileRow_.load(std::memory_order_acquire) >= maxAllowed)
        return false;
    }
    // In-flight: don't overwhelm the decompress pipeline
    uint16_t inFlight;
    {
      std::lock_guard<std::mutex> lock(decompressThrottleMutex_);
      inFlight = decompressInFlight_;
    }
    return decompressQueue_->size() + inFlight < maxDecompressInFlight_;
  });

  fetchByTileFutures_.push_back(fetcher->fetchTiles(
      cp_.tlmMarkers_->getTileParts(), slated, nullptr,
      [this, numTileCols, unreducedImageBounds, postGenerator](size_t requestIndex,
                                                               TileFetchContext* context) {
        auto& tilePart = (*context->requests_)[requestIndex];
        tilePart->stream_ = std::unique_ptr<IStream>(memStreamCreate(
            tilePart->data_.get(), tilePart->length_, false, nullptr, stream_->getFormat(), true));
        auto& tilePartSeq = (*context->tilePartFetchByTile_)[tilePart->tileIndex_];
        if(tilePartSeq->incrementFetchCount() == tilePartSeq->size())
        {
          auto tileIndex = tilePart->tileIndex_;

          // Register compressed data with cache for re-decompression
          if(compressedChunkCache_)
            compressedChunkCache_->put(tileIndex, tilePartSeq);

          // Track the highest tile row that has been fully fetched
          int32_t tileRow = tileIndex / numTileCols;
          int32_t prev = maxFetchedTileRow_.load(std::memory_order_acquire);
          while(prev < tileRow &&
                !maxFetchedTileRow_.compare_exchange_weak(prev, tileRow, std::memory_order_release,
                                                          std::memory_order_acquire))
          {
          }
          auto tilePartSeqCopy = tilePartSeq;
          decompressQueue_->push(
              [this, tileIndex, tilePartSeqCopy, unreducedImageBounds, postGenerator]() {
                const auto tileProcessor = getTileProcessor(tileIndex);
                auto decompressTask = genDecompressTileTLMTask(tileProcessor, tilePartSeqCopy,
                                                               unreducedImageBounds, postGenerator);
                decompressTask();
              });
        }
      }));

  return true;
}

/////////////////////////////////////////////////////////////////////

void CodeStreamDecompress::initTilesToDecompress(Rect16 region)
{
  tilesToDecompress_.init(region);
}
void CodeStreamDecompress::setNumComponents(uint16_t numComps)
{
  defaultTcp_->numComps_ = numComps;
}
bool CodeStreamDecompress::initDefaultTCP()
{
  return defaultTcp_->initDefault(headerImage_);
}

bool CodeStreamDecompress::setProgressionState(grk_progression_state state)
{
  return tileCache_->setProgressionState(state);
}
grk_progression_state CodeStreamDecompress::getProgressionState(uint16_t tileIndex)
{
  return tileCache_->getProgressionState(tileIndex);
}

void CodeStreamDecompress::setPostPostProcess(std::function<bool(GrkImage*)> func)
{
  postPostProcess_ = func;
}

void CodeStreamDecompress::differentialUpdate(GrkImage* scratch)
{
  headerImage_->subsampleAndReduce(cp_.codingParams_.dec_.reduce_);
  scratch->subsampleAndReduce(cp_.codingParams_.dec_.reduce_);
}

bool CodeStreamDecompress::activateScratch(bool singleTile, GrkImage* scratch)
{
  multiTileComposite_->copyHeaderTo(scratch);

  // Set int16 data_type on scratch components when all components are eligible
  // for 16-bit DWT. This ensures the composite buffer is allocated as int16,
  // avoiding int16→int32 widening during tile compositing.
  // Mirror the eligibility logic from TileProcessor::createDecompressTileComponentWindows.
  // Only do this when there's no region decode, because partial tiles have
  // wholeTileDecompress_=false and fall back to int32 DWT.
  if(scratch->has_multiple_tiles && region_.empty())
  {
    bool allEligible = true;
    bool hasMct =
        defaultTcp_->mct_ == 1 && scratch->numcomps >= 3 && scratch->componentsEqual(3, false);
    for(uint16_t i = 0; i < scratch->numcomps; i++)
    {
      auto tccp = defaultTcp_->tccps_ + i;
      auto comp = scratch->comps + i;
      if(tccp->qmfbid_ != 1)
      {
        allEligible = false;
        break;
      }
      bool isMctComp = hasMct && (i <= 2);
      uint32_t headroom = isMctComp ? 5 : 4;
      if(comp->prec + headroom > 16)
      {
        allEligible = false;
        break;
      }
    }
    if(allEligible)
    {
      for(uint16_t i = 0; i < scratch->numcomps; i++)
        scratch->comps[i].data_type = GRK_INT_16;
    }
  }

  // no need to allocate composite data if there is only one tile
  // i.e. no compositing — UNLESS band callback is active, which needs
  // a destination buffer for compositing before writing.
  if((singleTile || !headerImage_->has_multiple_tiles) && !ioBandCallback_)
    return true;

  // When band callback is active, allocate only a strip buffer (one tile-row height)
  // instead of the full composite image. This dramatically reduces peak memory for large images.
  if(ioBandCallback_)
  {
    uint8_t reduce = cp_.codingParams_.dec_.reduce_;
    auto slatedRect = tilesToDecompress_.getSlatedTileRect();
    uint32_t unreducedTileY0 = cp_.ty0_ + (uint32_t)slatedRect.y0 * cp_.t_height_;
    uint32_t unreducedTileY1 = std::min(unreducedTileY0 + cp_.t_height_, (uint32_t)scratch->y1);
    for(uint16_t i = 0; i < scratch->numcomps; i++)
    {
      auto comp = scratch->comps + i;
      comp->y0 = ceildivpow2<uint32_t>(ceildiv<uint32_t>(unreducedTileY0, comp->dy), reduce);
      uint32_t compY1 = ceildivpow2<uint32_t>(ceildiv<uint32_t>(unreducedTileY1, comp->dy), reduce);
      comp->h = compY1 - comp->y0;
    }
    return scratch->allocCompositeData();
  }

  return cp_.codingParams_.dec_.skipAllocateComposite_ || scratch->allocCompositeData();
}

ITileProcessor* CodeStreamDecompress::getTileProcessor(uint16_t tileIndex)
{
  auto cached = tileCache_->get(tileIndex);
  auto tileProcessor = cached ? cached->processor : nullptr;
  if(!tileProcessor)
  {
    auto tcp = new TileCodingParams(*defaultTcp_);
    // set number of tile parts if we have TLM markers
    tcp->signalledNumTileParts_ = cp_.getNumTilePartsFromTLM(tileIndex);

    uint16_t tile_x = tileIndex % cp_.t_grid_width_;
    uint16_t tile_y = tileIndex / cp_.t_grid_width_;
    auto tileBounds = cp_.getTileBounds(headerImage_->getBounds(), tile_x, tile_y);
    if(!region_.empty())
    {
      auto inter = tileBounds.intersection(region_);
      tcp->wholeTileDecompress_ = (inter.x0 == tileBounds.x0 && inter.y0 == tileBounds.y0 &&
                                   inter.x1 == tileBounds.x1 && inter.y1 == tileBounds.y1);
    }

    tileProcessor =
        new TileProcessor(tileIndex, tcp, this, stream_, false, tileCache_->getStrategy());
    if(!tileCache_->put(tileIndex, tileProcessor))
      return nullptr;
  }
  return tileProcessor;
}

GrkImage* CodeStreamDecompress::getImage(uint16_t tile_index, bool doWait)
{
  if(doWait)
    wait(tile_index);

  auto entry = tileCache_->get(tile_index);
  return entry ? entry->processor->getImage() : nullptr;
}
GrkImage* CodeStreamDecompress::getImage()
{
  wait(nullptr);

  return multiTileComposite_.get();
}
GrkImage* CodeStreamDecompress::getCompositeNoWait()
{
  return multiTileComposite_.get();
}

} // namespace grk
