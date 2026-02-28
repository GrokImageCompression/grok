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
#include "TileComponentWindow.h"
#include "PacketManager.h"
#include "canvas/tile/Tile.h"
#include "ITileProcessor.h"
#include "T2Decompress.h"

namespace grk
{
T2Decompress::T2Decompress(ITileProcessor* tileProc) : tileProcessor(tileProc) {}

bool T2Decompress::parsePackets(uint16_t tile_no, PacketCache* compressedPackets)
{
  auto cp = tileProcessor->getCodingParams();
  auto tcp = tileProcessor->getTCP();
  PacketManager packetManager(false, tileProcessor->getHeaderImage(), cp, tile_no, FINAL_PASS,
                              tileProcessor);
  auto pltMarkers = tileProcessor->getPacketLengthCache()->getMarkers();
  if(pltMarkers && !pltMarkers->isEnabled())
    pltMarkers = nullptr;
  for(auto prog_iter_num = 0U; prog_iter_num < tcp->getNumProgressions(); ++prog_iter_num)
  {
    auto currPi = packetManager.getPacketIter(prog_iter_num);
    while(currPi->next(pltMarkers ? compressedPackets : nullptr))
    {
      // code below is written this way as chunkLength() can throw, also indicating truncated tile
      try
      {
        if(compressedPackets->chunkLength() == 0)
        {
          throw SparseBufferIncompleteException();
        }
      }
      catch(SparseBufferIncompleteException& sbie)
      {
        grklog.warn("Tile %u is truncated.", tile_no);
        return true;
      }

      try
      {
        if(!parsePacket(currPi->getCompno(), currPi->getResno(), currPi->getPrecinctIndex(),
                        currPi->getLayno(), compressedPackets))
        {
          return true;
        }
      }
      catch([[maybe_unused]] const t1_t2::TruncatedPacketHeaderException& tex)
      {
        grklog.warn("Truncated packet: tile=%u component=%02d resolution=%02d precinct=%03d "
                    "layer=%02d",
                    tile_no, currPi->getCompno(), currPi->getResno(), currPi->getPrecinctIndex(),
                    currPi->getLayno());
        return true;
      }
      catch([[maybe_unused]] const t1::CorruptPacketException& cex)
      {
        // we can skip corrupt packet if PLT markers are present
        if(!tileProcessor->getPacketLengthCache()->getMarkers())
        {
          grklog.warn("Corrupt packet: tile=%u component=%02d resolution=%02d precinct=%03d "
                      "layer=%02d",
                      tile_no, currPi->getCompno(), currPi->getResno(), currPi->getPrecinctIndex(),
                      currPi->getLayno());
          return true;
        }
        else
        {
          grklog.warn("Corrupt packet: tile=%u component=%02d resolution=%02d precinct=%03d "
                      "layer=%02d",
                      tile_no, currPi->getCompno(), currPi->getResno(), currPi->getPrecinctIndex(),
                      currPi->getLayno());
        }
        // ToDo: skip corrupt packet if SOP marker is present
      }
    }
  }

  return false;
}

bool T2Decompress::parsePacket(uint16_t compno, uint8_t resno, uint64_t precinctIndex,
                               uint16_t layno, PacketCache* packetCache)
{
  // 1. skip packet if outside specified layer or resolution ranges
  auto tilec = tileProcessor->getTile()->comps_ + compno;
  auto& nextMaxLayer = tilec->nextPacketProgressionState_.resLayers_[resno];
  auto& maxLayer = tilec->currentPacketProgressionState_.resLayers_[resno];
  auto tcp = tileProcessor->getTCP();
  auto skip = layno < maxLayer || layno >= tcp->layersToDecompress_ ||
              resno >= tilec->resolutions_to_decompress_;

  //  if (!skip)
  //	  printf("tile %d layno %d read layers %d layers to decomp %d\n",
  //			  tileProcessor->getIndex(), layno, tcp->maxPacketDataLayersRead_,
  //			  tcp->layers_to_decompress_);

  // 2. also skip packet if outside of padded decompression window
  auto res = tilec->resolutions_ + resno;
  if(!skip && !tilec->isWholeTileDecoding())
  {
    skip = true;
    auto tilecBuffer = tilec->getWindow();
    for(auto bandIndex = 0U; bandIndex < res->numBands_; ++bandIndex)
    {
      auto band = res->band + bandIndex;
      if(band->empty())
        continue;
      auto paddedBandWindow = tilecBuffer->getBandWindowPadded(resno, band->orientation_);
      auto prec =
          band->generateBandPrecinctBounds(precinctIndex, res->bandPrecinctPartition_,
                                           res->bandPrecinctExpn_, res->precinctGrid_.width());
      if(paddedBandWindow->nonEmptyIntersection(&prec))
      {
        skip = false;
        break;
      }
    }
  }

  // read from PL cache or PLM or PLT marker, if available
  auto packetLength = tileProcessor->getPacketLengthCache()->next();

  // 3. packetLength is non-zero only if there is a PLT marker or previously cached
  // parser. Otherwise, we need to create the precinct and at least read the packet header
  if(!skip || !packetLength)
  {
    for(auto bandIndex = 0U; bandIndex < res->numBands_; ++bandIndex)
    {
      auto band = res->band + bandIndex;
      if(band->empty())
        continue;
      if(!band->createPrecinct(tileProcessor->isCompressor(), tileProcessor->getTCP()->numLayers_,
                               precinctIndex, res->bandPrecinctPartition_, res->bandPrecinctExpn_,
                               res->precinctGrid_.width(), res->cblkExpn_))
        return false;
    }
  }
  // 4. cache length read from PL marker
  auto plMarkerLength = packetLength;

  // 5. we must create a parser if no PL marker or not skipping
  PacketParser* parser = nullptr;
  if(!plMarkerLength || !skip)
  {
    parser = packetCache->gen(tileProcessor, tileProcessor->getNumProcessedPackets() & 0xFFFF,
                              compno, resno, precinctIndex, layno, plMarkerLength);
  }

  // 6. we must read the header if no PL marker
  if(!plMarkerLength)
  {
    try
    {
      packetLength = parser->readHeader();
    }
    catch([[maybe_unused]] const std::exception& ex)
    {
      throw;
    }
  }

  // 7. compressedPackets can now increment to next packet
  try
  {
    packetCache->next(packetLength);
  }
  catch([[maybe_unused]] const SparseBufferOverrunException& sboe)
  {
    return false;
  }

  // 8. parse packet data if not skipping.
  // Non-zero plMarkerLength will allow us to queue the packet
  // for concurrent parsing (currently disabled)
  if(!skip)
  {
    parsePacketData(res, parser, precinctIndex, false /*plMarkerLength > 0*/);
    nextMaxLayer = std::max(nextMaxLayer, (uint16_t)(layno + 1));
  }

  // 9. increment processed packets counter
  tileProcessor->incNumProcessedPackets();

  return true;
}

void T2Decompress::parsePacketData(Resolution* res, PacketParser* parser, uint64_t precinctIndex,
                                   bool enqueue)
{
  if(enqueue)
  {
    res->packetParser_->enqueue(precinctIndex, parser);
  }
  else
  {
    parser->parsePacketData();
  }
}
} // namespace grk
