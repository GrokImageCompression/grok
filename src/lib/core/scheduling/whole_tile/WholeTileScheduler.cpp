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
#include "ImageComponentFlow.h"
namespace grk
{
struct ITileProcessor;
}
#include "ICoder.h"
#include "CoderPool.h"
#include "CodecScheduler.h"
#include "WholeTileScheduler.h"

namespace grk
{

WholeTileScheduler::WholeTileScheduler(uint16_t numComps) : CodecScheduler(numComps)
{
  for(uint16_t compno = 0; compno < numcomps_; ++compno)
    imageComponentFlow_.push_back(nullptr);
}
WholeTileScheduler::~WholeTileScheduler()
{
  release();
}
void WholeTileScheduler::release(void)
{
  for(const auto& flow : imageComponentFlow_)
    delete flow;
  imageComponentFlow_.clear();
  for(uint16_t compno = 0; compno < numcomps_; ++compno)
    imageComponentFlow_.push_back(nullptr);
  clear();
}
void WholeTileScheduler::graph(uint16_t compno)
{
  assert(compno < numcomps_);
  imageComponentFlow_[compno]->graph();
}
ImageComponentFlow* WholeTileScheduler::getImageComponentFlow(uint16_t compno)
{
  return (compno < numcomps_) ? imageComponentFlow_[compno] : nullptr;
}

} // namespace grk
