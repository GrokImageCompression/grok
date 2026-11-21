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

#include <optional>

#include "TFSingleton.h"
#include "plugin_accelerate.h"

#include "CodeStreamLimits.h"
#include "TileWindow.h"
#include "Quantizer.h"
#include "GrkMatrix.h"
#include "Logger.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "TileFutureManager.h"
#include "FlowComponent.h"
#include "IStream.h"
#include "StreamIO.h"
#include "FetchCommon.h"
#include "TPFetchSeq.h"
#include "GrkImage.h"
#include "ICompressor.h"
#include "IDecompressor.h"
#include "Profile.h"
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
#include "TLMMarker.h"

#include "ICoder.h"
#include "CoderPool.h"
#include "CodeblockCompress.h"
#include "MinHeap.h"
#include "PacketManager.h"
#include "mct.h"
#include "ITileProcessor.h"
#include "ITileProcessorCompress.h"
#include "SOTMarker.h"
#include "CodeStreamCompress.h"
#include "TileProcessorCompress.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace grk
{
// passes of every tile, bucketed by the threshold that first includes them
static constexpr uint32_t kSlopeBuckets = USHRT_MAX + 1;
static constexpr uint32_t kMaxLayerMeasurements = 16;

static bool forEachTile(std::vector<std::unique_ptr<TileProcessorCompress>>& tiles,
                        const std::function<bool(TileProcessorCompress*)>& work)
{
  std::atomic<bool> success{true};
  tf::Taskflow flow;
  for(auto& tile : tiles)
  {
    auto tileProcessor = tile.get();
    flow.emplace([&success, &work, tileProcessor] {
      if(!work(tileProcessor))
        success = false;
    });
  }
  auto& executor = TFSingleton::get();
  if(executor.this_worker_id() >= 0)
    executor.corun(flow);
  else
    executor.run(flow).wait();
  return success;
}

bool CodeStreamCompress::usesImageWideRateControl(uint32_t numTiles)
{
  auto& enc = cp_.codingParams_.enc_;
  if(numTiles <= 1 || !enc.allocationByRateDistortion_ || enc.allocationByFixedQuality_ ||
     enc.maxComponentRate_ || enc.rateControlAlgorithm_ == GRK_RATE_CONTROL_BISECT)
    return false;
  for(uint32_t tileIndex = 0; tileIndex < numTiles; ++tileIndex)
  {
    auto tcp = cp_.tcps_.get((uint16_t)tileIndex);
    for(uint16_t layno = 0; layno < tcp->numLayers_; ++layno)
    {
      if(tcp->writeEmptyLayer_[layno])
        return false;
    }
  }
  return true;
}

bool CodeStreamCompress::allocateImageWideRate(
    std::vector<std::unique_ptr<TileProcessorCompress>>& tiles)
{
  auto& enc = cp_.codingParams_.enc_;
  uint16_t numLayers = cp_.tcps_.get(0)->numLayers_;
  uint32_t minimumSlope = USHRT_MAX;
  for(auto& tile : tiles)
    minimumSlope = std::min<uint32_t>(minimumSlope, tile->minimumSlope());

  const size_t numWorkers = TFSingleton::num_threads();
  std::vector<std::vector<uint64_t>> workerBytes(numWorkers), workerPasses(numWorkers);
  std::vector<uint64_t> bytesAbove(kSlopeBuckets + 1), passesAbove(kSlopeBuckets + 1);
  std::vector<uint64_t> tileBytes(tiles.size());

  uint64_t previousTotal = 0;
  uint16_t lastThreshold = 0;
  for(uint16_t layno = 0; layno < numLayers; ++layno)
  {
    double budgetBytes = 0;
    for(auto& tile : tiles)
    {
      double rate = cp_.tcps_.get(tile->getIndex())->rates_[layno];
      if(rate > 0.0)
        budgetBytes += ceil(rate);
    }
    if(budgetBytes <= 0.0)
    {
      forEachTile(tiles, [layno](TileProcessorCompress* tile) {
        tile->commitWholeLayer(layno);
        return true;
      });
      continue;
    }
    auto budget = (uint64_t)budgetBytes;

    for(size_t w = 0; w < numWorkers; ++w)
    {
      workerBytes[w].assign(kSlopeBuckets, 0);
      workerPasses[w].assign(kSlopeBuckets, 0);
    }
    forEachTile(tiles, [&](TileProcessorCompress* tile) {
      auto worker = TFSingleton::workerId();
      tile->addLayerSlopes(layno, workerBytes[worker].data(), workerPasses[worker].data());
      return true;
    });
    // bytesAbove[t] counts what a threshold of t - 1 includes
    std::fill(bytesAbove.begin(), bytesAbove.end(), 0);
    std::fill(passesAbove.begin(), passesAbove.end(), 0);
    for(uint32_t slope = kSlopeBuckets; slope-- > 0;)
    {
      bytesAbove[slope] = bytesAbove[slope + 1];
      passesAbove[slope] = passesAbove[slope + 1];
      for(size_t w = 0; w < numWorkers; ++w)
      {
        bytesAbove[slope] += workerBytes[w][slope];
        passesAbove[slope] += workerPasses[w][slope];
      }
    }

    auto measure = [&](uint32_t thresh, uint64_t& total) -> bool {
      if(!forEachTile(tiles, [&](TileProcessorCompress* tile) {
           return tile->simulateLayersAt(layno, (uint16_t)thresh, &tileBytes[tile->getIndex()]);
         }))
        return false;
      total = 0;
      for(auto bytes : tileBytes)
        total += bytes;
      return true;
    };
    // packet headers grow with the number of included passes
    uint64_t measuredHeaders = 0;
    uint64_t measuredPasses = 0;
    auto estimate = [&](uint32_t thresh) -> uint64_t {
      uint64_t headers = measuredPasses ? measuredHeaders * passesAbove[thresh + 1] / measuredPasses
                                        : measuredHeaders;
      return previousTotal + bytesAbove[thresh + 1] + headers;
    };

    // every pass at or under the threshold is left out, so one below the minimum keeps every
    // hull pass
    const uint32_t everyHullPass = minimumSlope ? minimumSlope - 1 : 0;
    int64_t failing = (int64_t)everyHullPass - 1;
    uint32_t fitting = USHRT_MAX;
    uint64_t fittingTotal = 0;
    bool haveFit = false;
    for(uint32_t measurements = 0; measurements < kMaxLayerMeasurements; ++measurements)
    {
      uint32_t lower = (uint32_t)(failing + 1);
      uint32_t upper = haveFit ? fitting : USHRT_MAX;
      if(haveFit && lower >= upper)
        break;
      // smallest threshold the estimate says fits
      uint32_t low = lower, high = upper;
      while(low < high)
      {
        uint32_t mid = (low + high) / 2;
        if(estimate(mid) <= budget)
          high = mid;
        else
          low = mid + 1;
      }
      uint32_t thresh = low;
      if(haveFit && thresh == fitting)
        break;
      uint64_t total = 0;
      if(!measure(thresh, total))
        return false;
      measuredPasses = passesAbove[thresh + 1];
      measuredHeaders = total - previousTotal - bytesAbove[thresh + 1];
      if(total <= budget)
      {
        fitting = thresh;
        fittingTotal = total;
        haveFit = true;
        if(enc.rateControlTolerance_ > 0.0 &&
           (double)total >= (double)budget * (1.0 - enc.rateControlTolerance_))
          break;
      }
      else
      {
        failing = thresh;
        if(thresh == USHRT_MAX)
          break;
      }
    }
    if(!haveFit)
    {
      if(enc.failOnRateControlOvershoot_)
      {
        grklog.error("Unable to meet compression ratio for layer %u", layno);
        return false;
      }
      grklog.warn("Unable to meet compression ratio for layer %u, rate control will be disabled "
                  "for this layer",
                  layno);
      forEachTile(tiles, [layno](TileProcessorCompress* tile) {
        tile->commitWholeLayer(layno);
        return true;
      });
      continue;
    }
    // passes after a block's last hull pass need every pass taken to be included
    bool takesEveryPass = false;
    if(fitting == everyHullPass)
    {
      if(!forEachTile(tiles, [&](TileProcessorCompress* tile) {
           return tile->simulateWholeLayer(layno, &tileBytes[tile->getIndex()]);
         }))
        return false;
      uint64_t total = 0;
      for(auto bytes : tileBytes)
        total += bytes;
      if(total <= budget)
      {
        takesEveryPass = true;
        fittingTotal = total;
      }
    }
    forEachTile(tiles, [layno, fitting, takesEveryPass](TileProcessorCompress* tile) {
      if(takesEveryPass)
        tile->commitWholeLayer(layno);
      else
        tile->commitLayerAt(layno, (uint16_t)fitting);
      return true;
    });
    previousTotal = fittingTotal;
    lastThreshold = (uint16_t)fitting;
  }

  return forEachTile(tiles, [lastThreshold](TileProcessorCompress* tile) {
    return tile->finishImageRateControl(lastThreshold);
  });
}

uint64_t CodeStreamCompress::compressTilesWithImageWideRate(uint32_t numTiles)
{
  std::vector<std::unique_ptr<TileProcessorCompress>> tiles(numTiles);
  std::atomic<bool> tilesCoded{true};
  tf::Taskflow tileFlow;
  for(uint16_t tileIndex = 0; tileIndex < numTiles; ++tileIndex)
  {
    tileFlow.emplace([&, tileIndex] {
      tiles[tileIndex] = std::make_unique<TileProcessorCompress>(
          tileIndex, cp_.tcps_.get(tileIndex), this, stream_);
      auto tileProcessor = tiles[tileIndex].get();
      if(!tilesCoded)
        return;
      if(!tileProcessor->preCompressTile(TFSingleton::workerId()))
      {
        tilesCoded = false;
        return;
      }
      tileProcessor->buildCompressDAG();
      tileProcessor->runCompressionTasks();
      tileProcessor->releaseCompressDAG();
      if(!tileProcessor->compressDAGSuccess())
        tilesCoded = false;
    });
  }
  TFSingleton::get().run(tileFlow).wait();
  bool success = tilesCoded && allocateImageWideRate(tiles);
  for(auto& tile : tiles)
  {
    if(success && !writeTileParts(tile.get()))
      success = false;
    if(success)
      recordSlopeThreshold(tile->getSlopeThreshold());
    tile.reset();
  }
  if(success)
    success = end();

  return success ? stream_->tell() : 0;
}
} // namespace grk
