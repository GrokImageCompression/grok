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
#include <set>
#include <tuple>
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

static void setPacketPosition(ITileProcessor* tileProcessor, CodingParams::RecordedPacketInfo& info)
{
  auto tile = tileProcessor->getTile();
  auto tccp = tileProcessor->getTCP()->tccps_ + info.compno;
  auto imageComp = tileProcessor->getHeaderImage()->comps + info.compno;
  auto res = tile->comps_[info.compno].resolutions_ + info.resno;
  const uint32_t levelsDone = tccp->numresolutions_ - 1U - info.resno;
  const uint32_t levelsX = tccp->horizontalDepth_[levelsDone];
  const uint32_t levelsY = tccp->verticalDepth_[levelsDone];
  const uint32_t widthExp = tccp->precWidthExp_[info.resno];
  const uint32_t heightExp = tccp->precHeightExp_[info.resno];
  const uint64_t gridWidth = ceildivpow2<uint32_t>(res->x1, widthExp) - (res->x0 >> widthExp);
  const uint64_t column = (uint64_t)(res->x0 >> widthExp) + info.precinctIndex % gridWidth;
  const uint64_t row = (uint64_t)(res->y0 >> heightExp) + info.precinctIndex / gridWidth;
  info.numResolutions = tccp->numresolutions_;
  info.positionX = std::max<uint64_t>(tile->x0, (column << (widthExp + levelsX)) * imageComp->dx);
  info.positionY = std::max<uint64_t>(tile->y0, (row << (heightExp + levelsY)) * imageComp->dy);
}

void T2Decompress::recordMissingPackets(uint16_t tile_no)
{
  auto cp = tileProcessor->getCodingParams();
  auto tcp = tileProcessor->getTCP();
  auto tile = tileProcessor->getTile();
  auto& packets = cp->recordedPacketInfo_[tile_no];
  std::set<std::tuple<uint16_t, uint8_t, uint64_t, uint16_t>> recorded;
  for(const auto& packet : packets)
    recorded.emplace(packet.compno, packet.resno, packet.precinctIndex, packet.layno);
  for(uint16_t compno = 0; compno < tile->numcomps_; ++compno)
  {
    auto tccp = tcp->tccps_ + compno;
    for(uint8_t resno = 0; resno < tccp->numresolutions_; ++resno)
    {
      auto res = tile->comps_[compno].resolutions_ + resno;
      if(res->x0 == res->x1 || res->y0 == res->y1)
        continue;
      const uint32_t widthExp = tccp->precWidthExp_[resno];
      const uint32_t heightExp = tccp->precHeightExp_[resno];
      const uint64_t numPrecincts =
          (uint64_t)(ceildivpow2<uint32_t>(res->x1, widthExp) - (res->x0 >> widthExp)) *
          (ceildivpow2<uint32_t>(res->y1, heightExp) - (res->y0 >> heightExp));
      for(uint64_t precinctIndex = 0; precinctIndex < numPrecincts; ++precinctIndex)
        for(uint16_t layno = 0; layno < tcp->numLayers_; ++layno)
        {
          if(recorded.count({compno, resno, precinctIndex, layno}))
            continue;
          CodingParams::RecordedPacketInfo info{};
          info.compno = compno;
          info.resno = resno;
          info.layno = layno;
          info.precinctIndex = precinctIndex;
          info.missingFromStream = true;
          info.packedHeader.push_back(0);
          setPacketPosition(tileProcessor, info);
          packets.push_back(std::move(info));
        }
    }
  }
}

bool T2Decompress::parsePackets(uint16_t tile_no, PacketCache* compressedPackets)
{
  const bool truncated = parseTilePackets(tile_no, compressedPackets);
  if(tileProcessor->getCodingParams()->recordMissingPackets_)
    recordMissingPackets(tile_no);
  return truncated;
}

bool T2Decompress::parseTilePackets(uint16_t tile_no, PacketCache* compressedPackets)
{
  auto cp = tileProcessor->getCodingParams();
  auto tcp = tileProcessor->getTCP();
  PacketManager packetManager(false, tileProcessor->getHeaderImage(), cp, tile_no, FINAL_PASS,
                              tileProcessor);
  auto packetLengthCache = tileProcessor->getPacketLengthCache();
  auto usePacketLengths = packetLengthCache->usesPacketLengths();
  // Bound the PLT skip-corrupt-packet path: the smallest legal packet with no
  // SOP/EPH is a 1-byte header, so a tile cannot hold more packets than it has
  // compressed bytes. A malformed precinct grid can declare far more packets
  // than that, and skipping each corrupt one would iterate the whole bogus
  // grid. Once skips exceed the byte budget the grid is provably lying.
  const size_t maxPackets = compressedPackets->length();
  size_t corruptSkips = 0;
  for(auto prog_iter_num = 0U; prog_iter_num < tcp->getNumProgressions(); ++prog_iter_num)
  {
    auto currPi = packetManager.getPacketIter(prog_iter_num);
    while(currPi->next(usePacketLengths ? compressedPackets : nullptr))
    {
      if(tileProcessor->getNumProcessedPackets() >= maxPackets)
      {
        grklog.warn("Tile %u is truncated.", tile_no);
        return true;
      }
      // code below is written this way as chunkLength() can throw, also indicating truncated tile
      // With selective fetch, the buffer may be exhausted for skipped (unfetched) packets,
      // so we only check for truncation when not in selective fetch mode.
      if(!compressedPackets->isSelectiveFetch())
      {
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
          if(++corruptSkips > maxPackets)
          {
            grklog.warn("Tile %u is truncated.", tile_no);
            return true;
          }
        }
        // ToDo: skip corrupt packet if SOP marker is present
      }
    }
    // skipPackets failure also ends next()
    if(packetLengthCache->hasPacketLengthError())
      return true;
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
    for(auto bandIndex = 0U; bandIndex < res->numBands_; ++bandIndex)
    {
      auto band = res->band + bandIndex;
      if(band->empty())
        continue;
      auto paddedBandWindow = tilec->getBandWindowPadded(resno, band->orientation_);
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
  uint32_t packetLength;
  if(!tileProcessor->getPacketLengthCache()->readNextPacketLength(packetLength))
    return false;
  auto cpRec = tileProcessor->getCodingParams();
  // PL markers give the packet length but not the header length that transcode needs
  if(cpRec->recordPacketLengths_)
    packetLength = 0;

  // 3. packetLength is non-zero only if there is a PLT marker or previously cached
  // parser. Otherwise, we need to create the precinct and at least read the packet header
  if(!skip || !packetLength)
  {
    for(auto bandIndex = 0U; bandIndex < res->numBands_; ++bandIndex)
    {
      auto band = res->band + bandIndex;
      if(band->empty())
        continue;
      if(tilec->isWholeTileDecoding())
        band->precincts_.reserve(res->precinctGrid_.area());
      if(!band->createPrecinct(tileProcessor->isCompressor(), tileProcessor->getTCP()->numLayers_,
                               precinctIndex, res->bandPrecinctPartition_, res->bandPrecinctExpn_,
                               res->precinctGrid_.width(), res->cblkExpn_,
                               &tileProcessor->getTile()->arena_))
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

  auto packedHeaderCursor = [&]() -> const uint8_t* {
    auto tileIndex = tileProcessor->getIndex();
    if(cpRec->ppmMarkers_ && tileIndex < cpRec->ppmMarkers_->packetHeaders.size())
      return *cpRec->ppmMarkers_->packetHeaders[tileIndex].ptr_to_buf();
    return tcp->ppt_ ? tcp->pptData_ : nullptr;
  };
  const uint8_t* packedHeaderStart = cpRec->recordPacketLengths_ ? packedHeaderCursor() : nullptr;

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

  // 6.5. Record packet info for transcode (PLT, SOP/EPH, filtering, reorder)
  if(cpRec->recordPacketLengths_)
  {
    auto tileIdx = tileProcessor->getIndex();
    CodingParams::RecordedPacketInfo info{};
    info.compno = compno;
    info.resno = resno;
    info.layno = layno;
    info.precinctIndex = precinctIndex;
    info.totalLength = packetLength;
    info.headerLength = parser ? parser->getHeaderLength() : 0;
    if(packedHeaderStart)
      info.packedHeader.assign(packedHeaderStart, packedHeaderCursor());
    setPacketPosition(tileProcessor, info);
    cpRec->recordedPacketInfo_[tileIdx].push_back(info);
  }

  // 7. compressedPackets can now increment to next packet
  try
  {
    bool dataPresent = !skip || !packetCache->isSelectiveFetch();
    packetCache->next(packetLength, dataPresent);
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
