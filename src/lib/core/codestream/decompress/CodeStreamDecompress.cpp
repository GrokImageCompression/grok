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

#include "geometry.h"
#include "grk_includes.h"
#include <functional>

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

  tileMarkerParsers_.resize(ExecSingleton::num_threads());
  std::generate(tileMarkerParsers_.begin(), tileMarkerParsers_.end(),
                []() { return std::make_unique<MarkerParser>(); });
}
void CodeStreamDecompress::init(grk_decompress_parameters* parameters)
{
  assert(parameters);
  cp_.init(parameters, tileCache_);
  auto core = &parameters->core;
  tileCache_->setStrategy(core->tile_cache_strategy);
  ioBufferCallback_ = core->io_buffer_callback;
  ioUserData_ = core->io_user_data;
  grkRegisterReclaimCallback_ = core->io_register_client_callback;

  postReadHeader();
}

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

  // no need to allocate composite data if there is only one tile
  // i.e. no compositing
  if(singleTile || !headerImage_->has_multiple_tiles)
    return true;

  return cp_.codingParams_.dec_.skipAllocateComposite_ || scratch->allocCompositeData();
}

TileProcessor* CodeStreamDecompress::getTileProcessor(uint16_t tileIndex)
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
    tileCache_->put(tileIndex, tileProcessor);
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

void CodeStreamDecompress::onRowCompleted(uint16_t tileIndexBegin, uint16_t tileIndexEnd)
{
  if(!doTileBatching())
    return;
  grklog.debug("CodeStreamDecompress: %u to %u completed", tileIndexBegin, tileIndexEnd);
  if(cp_.hasTLM())
  {
    {
      std::unique_lock<std::mutex> lock(batchTileQueueMutex_);
      size_t tilesToSchedule =
          batchTileHeadroomIncrement(batchTileNextRows_, (uint16_t)batchTileQueueTLM_.size());
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
    }
  }
  else
  {
    std::unique_lock<std::mutex> lock(batchTileQueueMutex_);
    batchTileScheduleHeadroomSequential_ +=
        batchTileHeadroomIncrement(batchTileNextRows_, (uint16_t)batchTileUnscheduledSequential_);
    batchDequeueSequential();
  }
  batchTileQueueCondition_.notify_one();
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
          [this](uint16_t tileIndexBegin, uint16_t tileIndexEnd) {
            onRowCompleted(tileIndexBegin, tileIndexEnd);
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

void CodeStreamDecompress::wait(uint16_t tile_index)
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

  decompressTileFutureManager_.wait(tile_index);
}
void CodeStreamDecompress::wait(grk_wait_swath* swath)
{
  // 1. wait for swath
  if(swath && tileCompletion_)
  {
    bool rc = tileCompletion_->wait(swath);
    if(!rc)
      return;
  }

  // 2. wait for sequential parse
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

  // 4. wait for all tile decompression operations
  decompressTileFutureManager_.waitAndClear();

  // 5. clear postMulti lambda
  if(postMulti_)
  {
    postMulti_();
    postMulti_ = nullptr;
  }
}

bool CodeStreamDecompress::decompress(grk_plugin_tile* tile)
{
  current_plugin_tile = tile;
  multiTileComposite_->postReadHeader(&cp_);
  tileCache_->init(cp_.t_grid_width_ * cp_.t_grid_height_);
  if(!decompressImpl(tilesToDecompress_.getSlatedTiles()))
    return false;
  postMulti_ = postMultiTile();

  if(cp_.asynchronous_)
    return true;

  wait(nullptr);

  return success_;
}

bool CodeStreamDecompress::decompressImpl(std::set<uint16_t> slated)
{
  // Filter out fully cached tiles from slated
  std::erase_if(slated, [this](uint16_t index) {
    auto cacheEntry = tileCache_->get(index);
    return cacheEntry && cacheEntry->processor->getImage() && !cacheEntry->dirty_;
  });
  if(slated.empty())
    return true;

  bool doDifferential = true;
  for(auto& tileIndex : slated)
  {
    auto cacheEntry = tileCache_->get(tileIndex);
    if(!cacheEntry || !cacheEntry->processor->getImage())
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

  // synchronous batch init
  if(doTileBatching() && !cp_.hasTLM())
  {
    batchTileUnscheduledSequential_ = (uint16_t)slated.size();
    batchTileScheduleHeadroomSequential_ =
        batchTileHeadroomIncrement(batchTileInitialRows_, batchTileUnscheduledSequential_);
  }

  // prepare for different types of decompression
  if(doDifferential)
  {
    differentialUpdate(scratchImage_.get());
  }
  else if(cp_.hasTLM())
  {
    // a) begin network fetch
    auto generator = [this](TileProcessor* tp) {
      return postMultiTile(tp); // Return the result directly
    };

    if(fetchByTile(slated, scratchImage_->getBounds(), generator))
      return true;

    // b) prepare for TLM decompress
    tilePartFetchFlat_ = std::make_shared<TPFetchSeq>();
    tilePartFetchByTile_ =
        std::make_shared<std::unordered_map<uint16_t, std::shared_ptr<TPFetchSeq>>>();
    TPFetchSeq::genCollections(&cp_.tlmMarkers_->getTileParts(), slated, tilePartFetchFlat_,
                               tilePartFetchByTile_);
  }
  else
  {
    // a) begin network fetch
    auto fetcher = stream_->getFetcher();
    if(stream_->getFetcher())
    {
      auto chunkSize = cp_.t_width_ * cp_.t_height_;
      chunkBuffer_ = std::make_shared<ChunkBuffer<>>(chunkSize, markerCache_->getTileStreamStart(),
                                                     fetcher->size());
      fetcher->fetchChunks(chunkBuffer_);
      stream_->setChunkBuffer(chunkBuffer_);
    }

    // b) prepare for sequential decompress
    decompressSequentialPrepare();
  }

  // schedule decompression

  // 1. differential decompression
  if(doDifferential)
  {
    for(auto& tileIndex : slated)
    {
      if(doDifferential)
      {
        auto cacheEntry = tileCache_->get(tileIndex);
        auto tileProcessor = cacheEntry->processor;
        if(!tileProcessor->differentialUpdate(headerImage_->getBounds()))
        {
          return false;
        }

        if(!schedule(tileProcessor, true))
          return false;
      }
    }
    return true;
  }

  if(cp_.asynchronous_ && ExecSingleton::num_threads() > 1)
  {
    if(cp_.hasTLM())
      decompressWorker_ = std::thread(&CodeStreamDecompress::decompressTLM, this, slated);
    else
      decompressWorker_ = std::thread(&CodeStreamDecompress::decompressSequential, this);
  }
  else
  {
    if(cp_.hasTLM())
      decompressTLM(slated);
    else
      decompressSequential();
  }

  return true;
}

bool CodeStreamDecompress::sequentialSchedule(TileProcessor* tileProcessor, bool multiTile)
{
  tileProcessor->prepareForDecompression();
  bool doSchedule = true;
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
      doSchedule = false;
    }
  }
  if(doSchedule && !schedule(tileProcessor, multiTile))
    return false;

  return true;
}

bool CodeStreamDecompress::schedule(TileProcessor* tileProcessor, bool multiTile)
{
  if(cp_.hasTLM())
  {
    auto generator = [this](TileProcessor* tp) { return postMultiTile(tp); };
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
    if(!tileProcessor->scheduleT2T1(
           &coderPool_, multiTile ? scratchImage_->getBounds() : headerImage_->getBounds(),
           multiTile ? postMultiTile(tileProcessor) : postSingleTile(tileProcessor),
           decompressTileFutureManager_))
    {
      grklog.error("Failed to decompress tile %u/%u", tileProcessor->getIndex(),
                   tilesToDecompress_.getTotalNumTiles());
      success_ = false;
      return false;
    }
    return true;
  }
}

bool CodeStreamDecompress::doTileBatching(void)
{
  return tilesToDecompress_.getSlatedTiles().size() > 1 && cp_.asynchronous_ &&
         !stream_->getFetcher();
}

uint16_t CodeStreamDecompress::batchTileHeadroomIncrement(uint16_t numRows, uint16_t tilesLeft)
{
  return std::min(uint16_t(tilesToDecompress_.getSlatedTileRect().width() * numRows), tilesLeft);
}

void CodeStreamDecompress::decompressTLM(const std::set<uint16_t>& slated)
{
  // 1 schedule all slated tiles
  if(!doTileBatching())
  {
    for(auto& tileIndex : slated)
    {
      if(!schedule(getTileProcessor(tileIndex), true))
        break;
    }
    return;
  }

  // 2. push all slated tiles into the queue
  for(const auto& value : slated)
    batchTileQueueTLM_.push(value);

  // 3. schedule first  N rows
  uint16_t initialBatchCount =
      batchTileHeadroomIncrement(batchTileInitialRows_, (uint16_t)slated.size());
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

bool CodeStreamDecompress::fetchByTile(
    std::set<uint16_t>& slated, Rect32 unreducedImageBounds,
    std::function<std::function<void()>(TileProcessor*)> postGenerator)
{
  auto fetcher = stream_->getFetcher();
  if(!fetcher)
    return false;
  fetchByTileFutures_.push_back(fetcher->fetchTiles(
      cp_.tlmMarkers_->getTileParts(), slated, nullptr,
      [this, unreducedImageBounds, postGenerator](size_t requestIndex, TileFetchContext* context) {
        auto& tilePart = (*context->requests_)[requestIndex];
        tilePart->stream_ = std::unique_ptr<IStream>(memStreamCreate(
            tilePart->data_.get(), tilePart->length_, false, nullptr, stream_->getFormat(), true));
        auto& tilePartSeq = (*context->tilePartFetchByTile_)[tilePart->tileIndex_];
        if(tilePartSeq->incrementFetchCount() == tilePartSeq->size())
        {
          grklog.debug("Decompressing tile %d", tilePart->tileIndex_);
          const auto tileProcessor = getTileProcessor(tilePart->tileIndex_);
          auto decompressTask = genDecompressTileTLMTask(tileProcessor, tilePartSeq,
                                                         unreducedImageBounds, postGenerator);
          decompressTask();
        }
      }));

  return true;
}

std::function<bool()> CodeStreamDecompress::genDecompressTileTLMTask(
    TileProcessor* tileProcessor, const std::shared_ptr<TPFetchSeq>& tilePartFetchSeq,
    Rect32 unreducedImageBounds, std::function<std::function<void()>(TileProcessor*)> postGenerator)
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

bool CodeStreamDecompress::postProcess(GrkImage* img)
{
  if(!img->postProcess())
    return false;
  return postPostProcess_ ? postPostProcess_(img) : true;
}

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
      scratchImage_->transferDataTo(multiTileComposite_.get());
      success_ = postProcess(multiTileComposite_.get());
    }
  };
}

std::function<void()> CodeStreamDecompress::postMultiTile(TileProcessor* tileProcessor)
{
  return [this, tileProcessor]() {
    if(!success_)
      return;
    tileProcessor->post_decompressT2T1(scratchImage_.get());
    numTilesDecompressed_++;
    auto tileImage = tileProcessor->getImage();
    if(!cp_.codingParams_.dec_.skipAllocateComposite_ && scratchImage_->has_multiple_tiles &&
       tileImage)
    {
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
void CodeStreamDecompress::decompressSequentialPrepare(void)
{
  stream_->seek(markerCache_->getTileStreamStart() + MARKER_BYTES);
  markerParser_.setSOT();
  if(cp_.plmMarkers_)
    cp_.plmMarkers_->rewind();
  stream_->memAdvise(stream_->tell(), 0, GrkAccessPattern::ACCESS_RANDOM);
}

void CodeStreamDecompress::decompressSequential(void)
{
  bool foundUnknownMarker = false;
  while(!markerParser_.endOfCodeStream() && !foundUnknownMarker)
  {
    // 1. parse and schedule tile
    try
    {
      if(!parseAndSchedule(true))
      {
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
      // schedule incomplete tiles
      if(!cp_.hasTLM())
      {
        tileCache_->forEachIncompleteTile(
            [this](TileProcessor* processor) { sequentialSchedule(processor, true); });
      }
    }

    // check for corrupt Adobe files where 5 tile parts per tile are signalled
    // but there are actually 6.
    if(markerParser_.currId() == SOT &&
       tileCache_->allSlatedSOTMarkersParsed(tilesToDecompress_.getSlatedTiles()) &&
       markerParser_.checkForIllegalTilePart())
    {
      success_ = false;
      break;
    }

    if(tileCache_->allSlatedSOTMarkersParsed(tilesToDecompress_.getSlatedTiles()))
      break;
  }
}

/// SINGLE TILE ////////////////////////////////////////////////////////////////////

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
    // a.parse remaining tile parts
    auto tileProcessor = cacheEntry ? cacheEntry->processor : nullptr;
    if(!tileProcessor || !tileProcessor->allSOTMarkersParsed())
    {
      decompressSequentialPrepare();
      try
      {
        if(!parseAndSchedule(false))
          return false;
      }
      catch(const t1_t2::InvalidMarkerException& ime)
      {
        grklog.warn("Found invalid marker 0x%.4x in tile %u header", ime.marker_, tileIndex);
        invalidMarker = true;
      }
    }
    else
    {
      // c. schedule decompression
      if(!schedule(tileProcessor, false))
        return false;
    }

    // b. If not cached, check for corrupt Adobe images where a final tile part is not parsed
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

    return true;
  }

  // TLM
  std::set<uint16_t> slated = {tileIndex};
  auto generator = [this](TileProcessor* tp) {
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

std::function<void()> CodeStreamDecompress::postSingleTile(TileProcessor* tileProcessor)
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

bool CodeStreamDecompress::parseAndSchedule(bool multiTile)
{
  if(markerParser_.currId() != SOT)
    return false;

  // A) Parse

  /* Parse tile parts until we satisfy one of the conditions below:
   * 1. read a complete slated tile
   * 2. read EOC
   * 3. run out of data
   */
  bool concurrentTileParsing = ExecSingleton::num_threads() > 1 &&
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
    catch(const CorruptSOTMarkerException& csme)
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
    grklog.error("parseAndSchedule: no slated SOT markers found");
    return false;
  }
  auto tileProcessor = currTileProcessor_;
  currTileProcessor_ = nullptr;
  currTileIndex_ = -1;

  // B) schedule

  return sequentialSchedule(tileProcessor, multiTile);
}

} // namespace grk
