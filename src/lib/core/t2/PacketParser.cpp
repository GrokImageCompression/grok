/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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

#include "grk_includes.h"

namespace grk
{

PacketParser::PacketParser(TileProcessor* tileProcessor, uint16_t packetSequenceNumber,
                           uint16_t compno, uint8_t resno, uint64_t precinctIndex, uint16_t layno,
                           uint32_t plLength, SparseBuffer* compressedPackets)
    : tileProcessor_(tileProcessor), packetSequenceNumber_(packetSequenceNumber), compno_(compno),
      resno_(resno), precinctIndex_(precinctIndex), layno_(layno), packets_(compressedPackets),
      layerData_(compressedPackets->chunkPtr()),
      layerBytesAvailable_(compressedPackets->chunkLength()), tagBitsPresent_(false),
      headerLength_(0), signalledLayerDataBytes_(0), plLength_(plLength), parsedHeader_(false),
      headerError_(false)
{}
void PacketParser::print(void)
{
  std::cout << std::endl << "/////////////////////////////////" << std::endl;
  std::cout << "compno: " << compno_ << std::endl;
  std::cout << "resno: " << (uint8_t)resno_ << std::endl;
  std::cout << "precinctIndex: " << precinctIndex_ << std::endl;
  std::cout << "layno: " << layno_ << std::endl;
  std::cout << "tileBytes: " << packets_->length() << std::endl;
  std::cout << "layerBytesAvailable: " << layerBytesAvailable_ << std::endl;
  std::cout << "tagBitsPresent: " << tagBitsPresent_ << std::endl;
  std::cout << "packetHeaderBytes: " << headerLength_ << std::endl;
  std::cout << "signalledDataBytes: " << signalledLayerDataBytes_ << std::endl;
  std::cout << "plLength: " << plLength_ << std::endl;
  std::cout << "/////////////////////////////////" << std::endl << std::endl;
}

uint32_t PacketParser::getLength(void)
{
  return signalledLayerDataBytes_ + headerLength_;
}

uint32_t PacketParser::readHeader(void)
{
  if(parsedHeader_)
    return headerLength_ + signalledLayerDataBytes_;
  if(headerError_)
  {
    grklog.warn("Attempt to re-read errored header for packet");
    throw CorruptPacketHeaderException();
  }

  auto layerDataPtr = layerData_;
  auto tilePtr = tileProcessor_->getTile();
  auto res = tilePtr->comps_[compno_].resolutions_ + resno_;
  auto tcp = tileProcessor_->getTCP();
  bool mayHaveSOP = tcp->csty_ & CP_CSTY_SOP;
  bool hasEPH = tcp->csty_ & CP_CSTY_EPH;
  // check for optional SOP marker
  // (present in packet even with packed packet headers)
  if(mayHaveSOP && layerBytesAvailable_ >= 2)
  {
    uint16_t marker =
        (uint16_t)(((uint16_t)(*layerDataPtr) << 8) | (uint16_t)(*(layerDataPtr + 1)));
    if(marker == SOP)
    {
      if(layerBytesAvailable_ < 6)
      {
        headerError_ = true;
        throw TruncatedPacketHeaderException();
      }
      uint16_t signalledPacketSequenceNumber =
          (uint16_t)(((uint16_t)layerDataPtr[4] << 8) | layerDataPtr[5]);
      if(signalledPacketSequenceNumber != (packetSequenceNumber_))
      {
        grklog.warn("SOP marker packet counter %u does not match expected counter %u",
                    signalledPacketSequenceNumber, packetSequenceNumber_);
        headerError_ = true;
        throw CorruptPacketHeaderException();
      }
      layerDataPtr += 6;
      layerBytesAvailable_ -= 6;
    }
  }
  auto headerStart = &layerDataPtr;
  auto remainingBytes = &layerBytesAvailable_;
  auto cp = tileProcessor_->getCodingParams();
  if(cp->ppmMarkers_)
  {
    if(tileProcessor_->getIndex() >= cp->ppmMarkers_->packetHeaders.size())
    {
      grklog.error("PPM marker has no packed packet header data for tile %u",
                   tileProcessor_->getIndex() + 1);
      headerError_ = true;
      throw CorruptPacketHeaderException();
    }
    auto header = &cp->ppmMarkers_->packetHeaders[tileProcessor_->getIndex()];
    headerStart = header->ptr_to_buf();
    remainingBytes = header->num_elts_ptr();
  }
  else if(tcp->ppt_)
  {
    headerStart = &tcp->pptData_;
    remainingBytes = &tcp->pptLength_;
  }
  if(*remainingBytes == 0)
    throw TruncatedPacketHeaderException();
  auto currentHeaderPtr = *headerStart;
  std::shared_ptr<BitIO> bio(new BitIO(currentHeaderPtr, *remainingBytes, false));
  auto tccp = tcp->tccps_ + compno_;
  try
  {
    tagBitsPresent_ = bio->read();
    // grklog.info("present=%u ", present);
    if(tagBitsPresent_)
    {
      for(uint8_t bandIndex = 0; bandIndex < res->numBands_; ++bandIndex)
      {
        auto band = res->band + bandIndex;
        if(band->empty())
          continue;
        auto prc = band->tryGetPrecinct(precinctIndex_);
        if(!prc)
          continue;
        auto numPrecCodeBlocks = prc->getNumCblks();
        // assuming 1 bit minimum encoded per code block,
        // let's check if we have enough bytes
        if((numPrecCodeBlocks >> 3) > packets_->length())
        {
          headerError_ = true;
          throw TruncatedPacketHeaderException();
        }
        for(uint32_t cblkno = 0; cblkno < numPrecCodeBlocks; cblkno++)
        {
          auto cblk = prc->tryGetDecompressedBlock(cblkno);
          uint8_t included;
          if(!cblk || !cblk->numlenbits())
          {
            uint16_t value;
            auto incl = prc->getInclTree();
            incl->decode(bio.get(), cblkno, layno_ + 1, &value);
            if(value != incl->getUninitializedValue() && value != layno_)
            {
              grklog.warn("Tile number: %u", tileProcessor_->getIndex() + 1);
              std::string msg = "Corrupt inclusion tag tree found when decoding packet header.";
              grklog.warn("%s", msg.c_str());
              throw CorruptPacketHeaderException();
            }
            included = (value <= layno_) ? 1 : 0;
          }
          else
          {
            included = bio->read();
          }
          if(!included)
            continue;
          if(!cblk)
            cblk = prc->getDecompressedBlock(cblkno);
          if(!cblk->numlenbits())
          {
            uint8_t K_msbs = 0;
            uint8_t value;
            auto imsb = prc->getImsbTree();

            // see Taubman + Marcellin page 388
            // loop below stops at (# of missing bit planes  + 1)
            imsb->decode(bio.get(), cblkno, K_msbs, &value);
            while(value >= K_msbs)
            {
              ++K_msbs;
              if(K_msbs > maxBitPlanesJ2K)
              {
                grklog.warn("More missing code block bit planes (%u)"
                            " than supported number of bit planes (%u) in library.",
                            K_msbs, maxBitPlanesJ2K);
                throw CorruptPacketHeaderException();
              }
              imsb->decode(bio.get(), cblkno, K_msbs, &value);
            }
            if(K_msbs == 0)
            {
              grklog.warn("Missing code block bit planes cannot be zero.");
              throw CorruptPacketHeaderException();
            }
            K_msbs--;
            if(K_msbs > band->maxBitPlanes_)
            {
              grklog.warn("More missing code block bit planes (%u) than band bit planes "
                          "(%u).",
                          K_msbs, band->maxBitPlanes_);
              throw CorruptPacketHeaderException();
            }
            else
            {
              uint8_t numbps = band->maxBitPlanes_ - K_msbs;
              if(numbps > maxBitPlanesJ2K)
              {
                grklog.warn("Number of bit planes %u is larger than maximum %u", numbps,
                            maxBitPlanesJ2K);
                throw CorruptPacketHeaderException();
              }
              cblk->setNumBps(numbps);
            }
            cblk->setNumLenBits(3);
          }
          cblk->readPacketHeader(bio, signalledLayerDataBytes_, layno_, tccp->cblkStyle_);
        }
      }
    }
    bio->readFinalHeaderByte();
    currentHeaderPtr += bio->numBytes();
  }
  catch([[maybe_unused]] const InvalidMarkerException& ex)
  {
    headerError_ = true;
    throw CorruptPacketHeaderException();
  }
  catch([[maybe_unused]] const CorruptPacketHeaderException& ex)
  {
    headerError_ = true;
    throw;
  }
  // EPH marker (absent from packet in case of packed packet headers)
  if(hasEPH)
  {
    if((*remainingBytes - (uint32_t)(currentHeaderPtr - *headerStart)) < 2U)
    {
      headerError_ = true;
      throw TruncatedPacketHeaderException();
    }
    uint16_t marker =
        (uint16_t)(((uint16_t)(*currentHeaderPtr) << 8) | (uint16_t)(*(currentHeaderPtr + 1)));
    if(marker != EPH)
    {
      grklog.warn("Expected EPH marker, but found 0x%x", marker);
      headerError_ = true;
      throw CorruptPacketHeaderException();
    }
    else
    {
      currentHeaderPtr += 2;
    }
  }
  auto headerMinusSopBytes = (size_t)(currentHeaderPtr - *headerStart);
  if(*remainingBytes < headerMinusSopBytes)
  {
    grklog.error("readHeader: remaining bytes %d is less than header length minus sop bytes %d",
                 *remainingBytes, headerMinusSopBytes);
    headerError_ = true;
    throw CorruptPacketHeaderException();
  }
  *remainingBytes -= headerMinusSopBytes;
  *headerStart += headerMinusSopBytes;
  headerLength_ = (uint32_t)(layerDataPtr - layerData_);
  // validate PL marker against parsed packet
  auto packetBytesParsed = headerLength_ + signalledLayerDataBytes_;
  if(plLength_ && plLength_ != packetBytesParsed)
  {
    grklog.error("Corrupt PL marker reports %u bytes for packet;"
                 " parsed bytes are in fact %u",
                 plLength_, packetBytesParsed);
    headerError_ = true;
    throw CorruptPacketHeaderException();
  }
  layerData_ += headerLength_;
  parsedHeader_ = true;

  //  grklog.info("Parsed packet header: component=%02d resolution=%02d precinct=%04d "
  //                       "layer=%02d",
  //                       compno_, resno_, precinctIndex_, layno_);

  // print();

  return headerLength_ + signalledLayerDataBytes_;
}
void PacketParser::readData(void)
{
  if(!tagBitsPresent_)
  {
    readDataFinalize();
    return;
  }
  uint32_t layerDataOffset = 0;
  auto tile = tileProcessor_->getTile();
  auto res = tile->comps_[compno_].resolutions_ + resno_;
  for(uint8_t bandIndex = 0; bandIndex < res->numBands_; ++bandIndex)
  {
    auto band = res->band + bandIndex;
    if(band->empty())
      continue;
    auto prc = band->tryGetPrecinct(precinctIndex_);
    if(!prc)
      continue;
    bool isHT = tileProcessor_->getTCP()->isHT();
    for(uint32_t cblkno = 0; cblkno < prc->getNumCblks(); ++cblkno)
    {
      auto cblk = prc->getDecompressedBlock(cblkno);
      try
      {
        cblk->parsePacketData(layno_, layerBytesAvailable_, isHT, layerData_, layerDataOffset);
      }
      catch([[maybe_unused]] const CorruptPacketDataException& ex)
      {
        grklog.warn("Packet data is truncated or packet header is corrupt :");
        grklog.warn("at component=%02d resolution=%02d precinct=%03d "
                    "layer=%02d",
                    compno_, resno_, precinctIndex_, layno_);
        goto finish;
      }
    } /* next code_block */
  }

finish:
  if(layerDataOffset != signalledLayerDataBytes_)
  {
    grklog.warn("Packet data is truncated or packet header is corrupt :");
    grklog.warn("at component=%02d resolution=%02d precinct=%03d "
                "layer=%02d",
                compno_, resno_, precinctIndex_, layno_);
  }
  readDataFinalize();

  //  grklog.info("Parsed packet data: component=%02d resolution=%02d precinct=%04d "
  //                       "layer=%02d",
  //                       compno_, resno_, precinctIndex_, layno_);
}

void PacketParser::readDataFinalize(void)
{
  tileProcessor_->incNumReadDataPackets();
}

AllLayersPrecinctPacketParser::AllLayersPrecinctPacketParser(TileProcessor* tileProcessor)
    : tileProcessor_(tileProcessor), parserQueue_(tileProcessor_->getTCP()->numLayers_)
{}

void AllLayersPrecinctPacketParser::enqueue(PacketParser* parser)
{
  if(parser && !parserQueue_.push(parser))
    grklog.warn("Attempt to add parser for layer larger than max number of layers.");
}

ResolutionPacketParser::ResolutionPacketParser(TileProcessor* tileProcessor)
    : tileProcessor_(tileProcessor)
{}

ResolutionPacketParser::~ResolutionPacketParser()
{
  clearPrecinctParsers();
}

void ResolutionPacketParser::clearPrecinctParsers(void)
{
  allLayerPrecinctParsers_.clear();
}
void ResolutionPacketParser::enqueue(uint64_t precinctIndex, PacketParser* parser)
{
  if(!parser)
    return;

  // Use std::make_unique to create a unique_ptr for AllLayersPrecinctPacketParser
  auto [it, inserted] = allLayerPrecinctParsers_.try_emplace(
      precinctIndex, std::make_unique<AllLayersPrecinctPacketParser>(tileProcessor_));

  // Enqueue the parser
  it->second->enqueue(parser);
}

} // namespace grk
