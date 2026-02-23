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
#include "grk_includes.h"

std::unique_ptr<tf::Executor> ExecSingleton::instance_ = nullptr;
std::mutex ExecSingleton::mutex_;
size_t ExecSingleton::numThreads_;

namespace grk
{

ResolutionChecker::ResolutionChecker(uint16_t numComponents, TileProcessor* tileProcessor,
                                     bool cacheAll)
{
  for(uint16_t compno = 0; compno < numComponents; ++compno)
  {
    auto tilec = tileProcessor->getTile()->comps_ + compno;
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
  runFuture_ = ExecSingleton::get().run(*this);
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
