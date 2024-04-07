/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
 */

#include "grk_includes.h"

namespace grk
{

PacketParser::PacketParser(TileProcessor* tileProcessor, uint16_t packetSequenceNumber,
						   uint16_t compno, uint8_t resno, uint64_t precinctIndex, uint16_t layno,
						   uint8_t* data, uint32_t lengthFromMarker, size_t tileBytes,
						   size_t remainingTilePartBytes)
	: tileProcessor_(tileProcessor), packetSequenceNumber_(packetSequenceNumber), compno_(compno),
	  resno_(resno), precinctIndex_(precinctIndex), layno_(layno), data_(data),
	  tileBytes_(tileBytes), remainingTilePartBytes_(remainingTilePartBytes),
	  tagBitsPresent_(false), packetHeaderBytes_(0), signalledDataBytes_(0), readDataBytes_(0),
	  lengthFromMarker_(lengthFromMarker), parsedHeader_(false), headerError_(false)
{}
void PacketParser::print(void)
{
   std::cout << "/////////////////////////////////" << compno_ << std::endl;
   std::cout << "compno: " << compno_ << std::endl;
   std::cout << "resno: " << resno_ << std::endl;
   std::cout << "precinctIndex: " << precinctIndex_ << std::endl;
   std::cout << "layno: " << layno_ << std::endl;
   std::cout << "tileBytes: " << tileBytes_ << std::endl;
   std::cout << "remainingTilePartBytes: " << remainingTilePartBytes_ << std::endl;
   std::cout << "tagBitsPresent: " << tagBitsPresent_ << std::endl;
   std::cout << "packetHeaderBytes: " << packetHeaderBytes_ << std::endl;
   std::cout << "signalledDataBytes: " << signalledDataBytes_ << std::endl;
   std::cout << "readDataBytes: " << readDataBytes_ << std::endl;
   std::cout << "lengthFromMarker: " << lengthFromMarker_ << std::endl;
}

uint32_t PacketParser::numHeaderBytes(void)
{
   return packetHeaderBytes_;
}
uint32_t PacketParser::numSignalledDataBytes(void)
{
   return signalledDataBytes_;
}
uint32_t PacketParser::numReadDataBytes(void)
{
   return readDataBytes_;
}
uint32_t PacketParser::numSignalledBytes(void)
{
   return packetHeaderBytes_ + signalledDataBytes_;
}

void PacketParser::readHeader(void)
{
   if(parsedHeader_)
	  return;

   auto currentData = data_;
   auto tilePtr = tileProcessor_->getTile();
   auto res = tilePtr->comps[compno_].resolutions_ + resno_;
   auto tcp = tileProcessor_->getTileCodingParams();
   bool mayHaveSOP = tcp->csty & J2K_CP_CSTY_SOP;
   bool hasEPH = tcp->csty & J2K_CP_CSTY_EPH;
   parsedHeader_ = true;
   // check for optional SOP marker
   // (present in packet even with packed packet headers)
   if(mayHaveSOP && remainingTilePartBytes_ >= 2)
   {
	  uint16_t marker =
		  (uint16_t)(((uint16_t)(*currentData) << 8) | (uint16_t)(*(currentData + 1)));
	  if(marker == J2K_MS_SOP)
	  {
		 if(remainingTilePartBytes_ < 6)
		 {
			headerError_ = true;
			throw TruncatedPacketHeaderException();
		 }
		 uint16_t signalledPacketSequenceNumber =
			 (uint16_t)(((uint16_t)currentData[4] << 8) | currentData[5]);
		 if(signalledPacketSequenceNumber != (packetSequenceNumber_))
		 {
			Logger::logger_.warn("SOP marker packet counter %u does not match expected counter %u",
								 signalledPacketSequenceNumber, packetSequenceNumber_);
			headerError_ = true;
			throw CorruptPacketHeaderException();
		 }
		 currentData += 6;
		 remainingTilePartBytes_ -= 6;
	  }
   }
   auto headerStart = &currentData;
   auto remainingBytes = &remainingTilePartBytes_;
   auto cp = tileProcessor_->cp_;
   if(cp->ppm_marker)
   {
	  if(tileProcessor_->getIndex() >= cp->ppm_marker->packetHeaders.size())
	  {
		 Logger::logger_.error("PPM marker has no packed packet header data for tile %u",
							   tileProcessor_->getIndex() + 1);
		 headerError_ = true;
		 throw CorruptPacketHeaderException();
	  }
	  auto header = &cp->ppm_marker->packetHeaders[tileProcessor_->getIndex()];
	  headerStart = &header->buf;
	  remainingBytes = &header->len;
   }
   else if(tcp->ppt)
   {
	  headerStart = &tcp->ppt_data;
	  remainingBytes = &tcp->ppt_len;
   }
   if(*remainingBytes == 0)
	  throw TruncatedPacketHeaderException();
   auto currentHeaderPtr = *headerStart;
   std::unique_ptr<BitIO> bio(new BitIO(currentHeaderPtr, *remainingBytes, false));
   auto tccp = tcp->tccps + compno_;
   try
   {
	  tagBitsPresent_ = bio->read();
	  // Logger::logger_.info("present=%u ", present);
	  if(tagBitsPresent_)
	  {
		 for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
		 {
			auto band = res->tileBand + bandIndex;
			if(band->empty())
			   continue;
			auto prc = band->getPrecinct(precinctIndex_);
			if(!prc)
			   continue;
			auto numPrecCodeBlocks = prc->getNumCblks();
			// assuming 1 bit minimum encoded per code block,
			// let's check if we have enough bytes
			if((numPrecCodeBlocks >> 3) > tileBytes_)
			{
			   headerError_ = true;
			   throw TruncatedPacketHeaderException();
			}
			for(uint64_t cblkno = 0; cblkno < numPrecCodeBlocks; cblkno++)
			{
			   auto cblk = prc->tryGetDecompressedBlockPtr(cblkno);
			   uint8_t included;
			   if(!cblk || !cblk->numlenbits)
			   {
				  uint16_t value;
				  auto incl = prc->getInclTree();
				  incl->decodeValue(bio.get(), cblkno, layno_ + 1, &value);
				  if(value != incl->getUninitializedValue() && value != layno_)
				  {
					 Logger::logger_.warn("Tile number: %u", tileProcessor_->getIndex() + 1);
					 std::string msg =
						 "Corrupt inclusion tag tree found when decoding packet header.";
					 Logger::logger_.warn("%s", msg.c_str());
					 headerError_ = true;
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
				  cblk = prc->getDecompressedBlockPtr(cblkno);
			   if(!cblk->numlenbits)
			   {
				  uint8_t K_msbs = 0;
				  uint8_t value;
				  auto imsb = prc->getImsbTree();

				  // see Taubman + Marcellin page 388
				  // loop below stops at (# of missing bit planes  + 1)
				  imsb->decodeValue(bio.get(), cblkno, K_msbs, &value);
				  while(value >= K_msbs)
				  {
					 ++K_msbs;
					 if(K_msbs > maxBitPlanesGRK)
					 {
						Logger::logger_.warn(
							"More missing code block bit planes (%u)"
							" than supported number of bit planes (%u) in library.",
							K_msbs, maxBitPlanesGRK);
						headerError_ = true;
						throw CorruptPacketHeaderException();
					 }
					 imsb->decodeValue(bio.get(), cblkno, K_msbs, &value);
				  }
				  assert(K_msbs >= 1);
				  K_msbs--;
				  if(K_msbs > band->numbps)
				  {
					 Logger::logger_.warn(
						 "More missing code block bit planes (%u) than band bit planes "
						 "(%u).",
						 K_msbs, band->numbps);
					 headerError_ = true;
					 throw CorruptPacketHeaderException();
				  }
				  else
				  {
					 cblk->numbps = band->numbps - K_msbs;
				  }
				  if(cblk->numbps > maxBitPlanesGRK)
				  {
					 Logger::logger_.warn("Number of bit planes %u is larger than maximum %u",
										  cblk->numbps, maxBitPlanesGRK);
					 headerError_ = true;
					 throw CorruptPacketHeaderException();
				  }
				  cblk->numlenbits = 3;
			   }
			   uint32_t numPassesInPacket = 0;
			   bio->getnumpasses(&numPassesInPacket);
			   cblk->setNumPassesInPacket(layno_, (uint8_t)numPassesInPacket);
			   uint8_t increment = bio->getcommacode();
			   cblk->numlenbits += increment;
			   uint32_t segno = 0;
			   if(!cblk->getNumSegments())
			   {
				  initSegment(cblk, 0, tccp->cblk_sty, true);
			   }
			   else
			   {
				  segno = cblk->getNumSegments() - 1;
				  if(cblk->getSegment(segno)->numpasses == cblk->getSegment(segno)->maxpasses)
					 initSegment(cblk, ++segno, tccp->cblk_sty, false);
			   }
			   auto blockPassesInPacket = (int32_t)cblk->getNumPassesInPacket(layno_);
			   do
			   {
				  auto seg = cblk->getSegment(segno);
				  /* sanity check when there is no mode switch */
				  if(seg->maxpasses == maxPassesPerSegmentJ2K)
				  {
					 if(blockPassesInPacket > (int32_t)maxPassesPerSegmentJ2K)
					 {
						Logger::logger_.warn("Number of code block passes (%u) in packet is "
											 "suspiciously large.",
											 blockPassesInPacket);
						headerError_ = true;
						throw CorruptPacketHeaderException();
					 }
					 else
					 {
						seg->numPassesInPacket = (uint32_t)blockPassesInPacket;
					 }
				  }
				  else
				  {
					 assert(seg->maxpasses >= seg->numpasses);
					 seg->numPassesInPacket = (uint32_t)std::min<int32_t>(
						 (int32_t)(seg->maxpasses - seg->numpasses), blockPassesInPacket);
				  }
				  uint8_t bits_to_read = cblk->numlenbits + floorlog2(seg->numPassesInPacket);
				  if(bits_to_read > 32)
				  {
					 Logger::logger_.warn("readHeader: too many bits in segment length ");
					 headerError_ = true;
					 throw CorruptPacketHeaderException();
				  }
				  bio->read(&seg->numBytesInPacket, bits_to_read);
				  signalledDataBytes_ += seg->numBytesInPacket;
#ifdef DEBUG_LOSSLESS_T2
				  cblk->packet_length_info.push_back(PacketLengthInfo(
					  seg->numBytesInPacket, cblk->numlenbits + floorlog2(seg->numPassesInPacket)));
#endif
				  blockPassesInPacket -= (int32_t)seg->numPassesInPacket;
				  if(blockPassesInPacket > 0)
					 initSegment(cblk, ++segno, tccp->cblk_sty, false);
			   } while(blockPassesInPacket > 0);
			}
		 }
	  }
	  bio->inalign();
	  currentHeaderPtr += bio->numBytes();
   }
   catch([[maybe_unused]] const InvalidMarkerException& ex)
   {
	  headerError_ = true;
	  throw CorruptPacketHeaderException();
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
	  if(marker != J2K_MS_EPH)
	  {
		 Logger::logger_.warn("Expected EPH marker, but found 0x%x", marker);
		 headerError_ = true;
		 throw CorruptPacketHeaderException();
	  }
	  else
	  {
		 currentHeaderPtr += 2;
	  }
   }
   auto headerMinusSopBytes = (size_t)(currentHeaderPtr - *headerStart);
   *remainingBytes -= headerMinusSopBytes;
   *headerStart += headerMinusSopBytes;
   packetHeaderBytes_ = (uint32_t)(currentData - data_);
   // validate PL marker against parsed packet
   if(lengthFromMarker_ && lengthFromMarker_ != numSignalledBytes())
   {
	  Logger::logger_.error("Corrupt PL marker reports %u bytes for packet;"
							" parsed bytes are in fact %u",
							lengthFromMarker_, numSignalledBytes());
	  headerError_ = true;
	  throw CorruptPacketHeaderException();
   }
   data_ += packetHeaderBytes_;
}
void PacketParser::initSegment(DecompressCodeblock* cblk, uint32_t index, uint8_t cblk_sty,
							   bool first)
{
   auto seg = cblk->getSegment(index);

   seg->clear();
   if(cblk_sty & GRK_CBLKSTY_TERMALL)
   {
	  seg->maxpasses = 1;
   }
   else if(cblk_sty & GRK_CBLKSTY_LAZY)
   {
	  if(first)
	  {
		 seg->maxpasses = 10;
	  }
	  else
	  {
		 auto last_seg = seg - 1;
		 seg->maxpasses = ((last_seg->maxpasses == 1) || (last_seg->maxpasses == 10)) ? 2 : 1;
	  }
   }
   else
   {
	  seg->maxpasses = maxPassesPerSegmentJ2K;
   }
}
void PacketParser::readData(void)
{
   if(!tagBitsPresent_)
   {
	  readDataFinalize();
	  return;
   }
   uint32_t offset = 0;
   auto tile = tileProcessor_->getTile();
   auto res = tile->comps[compno_].resolutions_ + resno_;
   for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
   {
	  auto band = res->tileBand + bandIndex;
	  if(band->empty())
		 continue;
	  auto prc = band->getPrecinct(precinctIndex_);
	  if(!prc)
		 continue;
	  for(uint64_t cblkno = 0; cblkno < prc->getNumCblks(); ++cblkno)
	  {
		 auto cblk = prc->getDecompressedBlockPtr(cblkno);
		 if(!cblk->getNumPassesInPacket(layno_))
			continue;

		 auto seg = cblk->getCurrentSegment();
		 if(!seg || (seg->numpasses == seg->maxpasses))
			seg = cblk->nextSegment();
		 uint32_t numPassesInPacket = cblk->getNumPassesInPacket(layno_);
		 do
		 {
			if(remainingTilePartBytes_ == 0)
			{
			   Logger::logger_.warn("Packet data is truncated or packet header is corrupt :");
			   Logger::logger_.warn("at component=%02d resolution=%02d precinct=%03d "
									"layer=%02d",
									compno_, resno_, precinctIndex_, layno_);
			   goto finish;
			}
			if(((seg->numBytesInPacket) > remainingTilePartBytes_))
			{
			   // HT doesn't tolerate truncated code blocks since decoding runs both forward
			   // and reverse. So, in this case, we ignore the entire code block
			   if(tileProcessor_->cp_->tcps[0].isHT())
				  cblk->cleanUpSegBuffers();
			   seg->numBytesInPacket = 0;
			   seg->numpasses = 0;
			   break;
			}
			if(seg->numBytesInPacket)
			{
			   // sanity check on seg->numBytesInPacket
			   if(UINT_MAX - seg->numBytesInPacket < seg->len)
			   {
				  Logger::logger_.error(
					  "Segment packet length %u plus total segment length %u must be "
					  "less than 2^32",
					  seg->numBytesInPacket, seg->len);
				  throw CorruptPacketDataException();
			   }
			   // correct for truncated packet
			   if(seg->numBytesInPacket > remainingTilePartBytes_)
				  seg->numBytesInPacket = (uint32_t)remainingTilePartBytes_;
			   cblk->seg_buffers.push_back(
				   new grk_buf8(data_ + offset, seg->numBytesInPacket, false));
			   offset += seg->numBytesInPacket;
			   cblk->compressedStream.len += seg->numBytesInPacket;
			   seg->len += seg->numBytesInPacket;
			   remainingTilePartBytes_ -= seg->numBytesInPacket;
			}
			seg->numpasses += seg->numPassesInPacket;
			numPassesInPacket -= seg->numPassesInPacket;
			if(numPassesInPacket > 0)
			   seg = cblk->nextSegment();
		 } while(numPassesInPacket > 0);
	  } /* next code_block */
   }

finish:
   readDataBytes_ = offset;
   readDataFinalize();
}

template<typename T>
void update_maximum(std::atomic<T>& maximum_value, T const& value) noexcept
{
   T prev_value = maximum_value;
   while(prev_value < value && !maximum_value.compare_exchange_weak(prev_value, value))
   {}
}

void PacketParser::readDataFinalize(void)
{
   auto tile = tileProcessor_->getTile();
   update_maximum<uint8_t>((tile->comps + compno_)->highestResolutionDecompressed, resno_);
   tileProcessor_->incNumDecompressedPackets();
}

PrecinctPacketParsers::PrecinctPacketParsers(TileProcessor* tileProcessor)
	: tileProcessor_(tileProcessor), parsers_(nullptr), numParsers_(0), allocatedParsers_(0)
{
   auto tcp = tileProcessor_->getTileCodingParams();
   allocatedParsers_ = tcp->max_layers_;
   if(allocatedParsers_)
   {
	  parsers_ = new PacketParser*[allocatedParsers_];
	  for(uint16_t i = 0; i < allocatedParsers_; ++i)
		 parsers_[i] = nullptr;
   }
}

PrecinctPacketParsers::~PrecinctPacketParsers(void)
{
   for(uint16_t i = 0; i < numParsers_; ++i)
	  delete parsers_[i];
   delete[] parsers_;
}

void PrecinctPacketParsers::pushParser(PacketParser* parser)
{
   if(!parser)
	  return;
   if(numParsers_ >= allocatedParsers_)
   {
	  Logger::logger_.warn("Attempt to add parser for layer larger than max number of layers.");
	  return;
   }
   parsers_[numParsers_++] = parser;
}

ParserMap::ParserMap(TileProcessor* tileProcessor) : tileProcessor_(tileProcessor) {}

ParserMap::~ParserMap()
{
   for(const auto& p : precinctParsers_)
	  delete p.second;
}

void ParserMap::pushParser(uint64_t precinctIndex, PacketParser* parser)
{
   if(!parser)
	  return;
   PrecinctPacketParsers* ppp = nullptr;
   auto it = precinctParsers_.find(precinctIndex);
   if(it == precinctParsers_.end())
   {
	  ppp = new PrecinctPacketParsers(tileProcessor_);
	  precinctParsers_[precinctIndex] = ppp;
   }
   else
   {
	  ppp = it->second;
   }
   ppp->pushParser(parser);
}

} // namespace grk
