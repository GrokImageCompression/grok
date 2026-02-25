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
#include "MemManager.h"
#include "buffer.h"
#include "GrkObjectWrapper.h"
#include "Quantizer.h"
#include "SparseBuffer.h"
#include "MarkerCache.h"
#include "IStream.h"

#include "GrkImageMeta.h"
#include "GrkImage.h"
#include "ICompressor.h"
#include "IDecompressor.h"
#include "MarkerParser.h"
#include "Codec.h"

#include "PLMarker.h"
#include "SIZMarker.h"
#include "PPMMarker.h"
namespace grk
{
struct ITileProcessor;
}
#include "PacketParser.h"
#include "CodingParams.h"
#include "CodeStream.h"
#include "BitIO.h"
#include "TagTree.h"

#include "CodeblockCompress.h"
#include "CodeblockDecompress.h"

#include "Precinct.h"
#include "Subband.h"
#include "Resolution.h"

namespace grk
{

Resolution::~Resolution(void)
{
  delete packetParser_;
}
void Resolution::print(void) const
{
  Rect32::print();
  for(uint32_t i = 0; i < numBands_; ++i)
  {
    std::cout << "band " << i << " : ";
    band[i].print();
  }
}
bool Resolution::init(grk_plugin_tile* currentPluginTile, bool isCompressor, uint16_t numLayers,
                      ITileProcessor* tileProcessor, TileComponentCodingParams* tccp, uint8_t resno)
{
  if(initialized_)
    return true;

  current_plugin_tile_ = currentPluginTile;

  /* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
  bandPrecinctExpn_ = Point8(tccp->precWidthExp_[resno], tccp->precHeightExp_[resno]);

  /* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
  bandPrecinctPartition_ = precinctPartition_;
  if(resno != 0)
  {
    bandPrecinctPartition_ = bandPrecinctPartition_.scaleDownPow2(1, 1);
    bandPrecinctExpn_.x--;
    bandPrecinctExpn_.y--;
  }
  cblkExpn_ = Point8(std::min(tccp->cblkw_expn_, bandPrecinctExpn_.x),
                     std::min(tccp->cblkh_expn_, bandPrecinctExpn_.y));

  // create all precincts up front if we are compressing
  if(isCompressor)
  {
    uint64_t num_precincts = precinctGrid_.area();
    for(uint8_t bandIndex = 0; bandIndex < numBands_; ++bandIndex)
    {
      auto curr_band = band + bandIndex;
      for(uint64_t precinctIndex = 0; precinctIndex < num_precincts; ++precinctIndex)
      {
        if(!curr_band->createPrecinct(isCompressor, numLayers, precinctIndex,
                                      bandPrecinctPartition_, bandPrecinctExpn_,
                                      precinctGrid_.width(), cblkExpn_))
          return false;
      }
    }
  }

  if(!isCompressor)
    packetParser_ = new ResolutionPacketParser(tileProcessor);
  initialized_ = true;

  return true;
}

Rect32 Resolution::genPrecinctPartition(const Rect32& window, uint8_t precWidthExp,
                                        uint8_t precHeightExp)
{
  Rect32 partition;
  // Compute lower bounds (aligned to precinct grid)
  partition.x0 = floordivpow2(window.x0, precWidthExp) << precWidthExp;
  partition.y0 = floordivpow2(window.y0, precHeightExp) << precHeightExp;

  // Compute upper bounds (ceiling-aligned to cover window)
  uint64_t temp = (uint64_t)ceildivpow2<uint32_t>(window.x1, precWidthExp) << precWidthExp;
  if(temp > UINT_MAX)
  {
    grklog.warn("Resolution x1 value %u exceeds 2^32; clamping to %u", (uint32_t)temp, UINT_MAX);
    partition.x1 = UINT_MAX;
  }
  else
  {
    partition.x1 = (uint32_t)temp;
  }

  temp = (uint64_t)ceildivpow2<uint32_t>(window.y1, precHeightExp) << precHeightExp;
  if(temp > UINT_MAX)
  {
    grklog.warn("Resolution y1 value %u exceeds 2^32; clamping to %u", (uint32_t)temp, UINT_MAX);
    partition.y1 = UINT_MAX;
  }
  else
  {
    partition.y1 = (uint32_t)temp;
  }

  // Ensure partition covers the window (in case of numerical edge cases)
  if(partition.x1 < window.x1 || partition.y1 < window.y1)
  {
    grklog.warn("Partition (x1=%u, y1=%u) does not fully cover window (x1=%u, y1=%u); adjusting",
                partition.x1, partition.y1, window.x1, window.y1);
    partition.x1 = std::max(partition.x1, window.x1);
    partition.y1 = std::max(partition.y1, window.y1);
  }

  return partition;
}

} // namespace grk
