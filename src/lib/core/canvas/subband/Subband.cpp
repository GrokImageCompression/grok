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

#include <vector>

#include "geometry.h"
#include "BitIO.h"
#include "TagTree.h"
#include "CodeblockDecompress.h"
#include "CodeblockCompress.h"
#include "Precinct.h"
#include "Subband.h"

namespace grk
{

// note: don't copy precinct array
Subband::Subband(const Subband& rhs)
    : Rect32(rhs), orientation_(rhs.orientation_), maxBitPlanes_(rhs.maxBitPlanes_),
      stepsize_(rhs.stepsize_)
{}
Subband& Subband::operator=(const Subband& rhs)
{
  if(this != &rhs)
  { // self-assignment check expected
    *this = Subband(rhs);
  }
  return *this;
}
void Subband::print() const
{
  Rect32::print();
}
bool Subband::empty()
{
  return ((x1 - x0 == 0) || (y1 - y0 == 0));
}
Precinct* Subband::tryGetPrecinct(uint64_t precinctIndex)
{
  if(!precinctMap_.contains(precinctIndex))
    return nullptr;
  uint64_t index = precinctMap_[precinctIndex];

  return precincts_[index];
}
static Rect32_16 intersect(const Rect32* lhs, const Rect32_16& rhs)
{
  uint32_t x = std::max<uint32_t>(lhs->x0, rhs.x0());
  uint32_t y = std::max<uint32_t>(lhs->y0, rhs.y0());
  uint16_t w = uint16_t(std::min<uint32_t>(lhs->x1, rhs.x1()) - x);
  uint16_t h = uint16_t(std::min<uint32_t>(lhs->y1, rhs.y1()) - y);
  return Rect32_16(x, y, w, h);
}
Rect32_16 Subband::generateBandPrecinctBounds(uint64_t precinctIndex, Rect32 bandPrecinctPartition,
                                              Point8 bandPrecinctExpn, uint32_t precinctGridWidth)
{
  auto bandPrecinctTopLeft =
      Point32(bandPrecinctPartition.x0 +
                  (uint32_t)((precinctIndex % precinctGridWidth) << bandPrecinctExpn.x),
              bandPrecinctPartition.y0 +
                  (uint32_t)((precinctIndex / precinctGridWidth) << bandPrecinctExpn.y));
  Rect32_16 bds =
      Rect32_16(bandPrecinctTopLeft.x, bandPrecinctTopLeft.y, (uint16_t)(1U << bandPrecinctExpn.x),
                (uint16_t)(1U << bandPrecinctExpn.y));
  return intersect(this, bds);
}
Precinct* Subband::createPrecinct(bool isCompressor, uint16_t numLayers, uint64_t precinctIndex,
                                  Rect32 bandPrecinctPartition, Point8 bandPrecinctExpn,
                                  uint32_t precinctGridWidth, Point8 cblk_expn)
{
  auto temp = precinctMap_.find(precinctIndex);
  if(temp != precinctMap_.end())
    return precincts_[temp->second];

  auto bounds = generateBandPrecinctBounds(precinctIndex, bandPrecinctPartition, bandPrecinctExpn,
                                           precinctGridWidth);
  if(!bounds.valid())
  {
    grklog.error("createPrecinct: invalid precinct bounds.");
    return nullptr;
  }
  Precinct* currPrec;
  if(isCompressor)
    currPrec = new PrecinctCompress(numLayers, bounds, cblk_expn);
  else
    currPrec = new PrecinctDecompress(numLayers, bounds, cblk_expn);
  precincts_.push_back(currPrec);
  precinctMap_[precinctIndex] = precincts_.size() - 1;

  return currPrec;
}

} // namespace grk
