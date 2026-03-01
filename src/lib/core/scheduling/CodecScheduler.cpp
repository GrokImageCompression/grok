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
#include "FlowComponent.h"
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

std::unique_ptr<tf::Executor> TFSingleton::instance_ = nullptr;
std::mutex TFSingleton::mutex_;
size_t TFSingleton::numThreads_;

namespace grk
{

ResolutionChecker::ResolutionChecker(uint16_t numComponents, TileComponent* comps, bool cacheAll)
{
  for(uint16_t compno = 0; compno < numComponents; ++compno)
  {
    auto tilec = comps + compno;
    uint8_t resBegin =
        cacheAll ? (uint8_t)tilec->currentPacketProgressionState_.numResolutionsRead() : 0;
    uint8_t resUpperBound = tilec->nextPacketProgressionState_.numResolutionsRead();
    componentResolutions_.emplace_back(resBegin, resUpperBound);
  }
}

// Check if a specific component contains a given resolution
bool ResolutionChecker::contains(uint16_t compno, uint8_t resolution) const
{
  if(compno >= componentResolutions_.size())
  {
    return false; // Out of bounds
  }
  auto [resBegin, resUpperBound] = componentResolutions_[compno];
  return resolution >= resBegin && resolution < resUpperBound;
}

std::pair<uint8_t, uint8_t> ResolutionChecker::getResBounds(uint16_t compno)
{
  if(compno >= componentResolutions_.size())
    return {0, 0};
  return componentResolutions_[compno];
}

CodecScheduler::CodecScheduler(uint16_t numComps) : success(true), numcomps_(numComps) {}

CodecScheduler::~CodecScheduler()
{
  releaseCoders();
}

void CodecScheduler::releaseCoders(void)
{
  for(const auto& t : coders_)
    delete t;
  coders_.clear();
}

void CodecScheduler::run(void)
{
  runFuture_ = TFSingleton::get().run(*this);
}

bool CodecScheduler::wait(void)
{
  if(!runFuture_.valid())
  {
    return false; // Future not set or invalid
  }

  runFuture_.wait();

  // Reset the future to indicate it is no longer in use
  runFuture_ = tf::Future<void>();

  return success; // Assume success is set elsewhere in your code
}

} // namespace grk
