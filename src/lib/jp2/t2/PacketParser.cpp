/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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

namespace grk {

PacketParser::PacketParser(TileProcessor* tileProcessor,
							uint16_t packetSequenceNumber,
							uint16_t compno,
							uint8_t resno,
							uint64_t precinctIndex,
							uint16_t layno,
							uint8_t *data,
							uint32_t lengthFromMarker,
							size_t tileBytes,
							size_t remainingTilePartBytes) :
		                                    tileProcessor_(tileProcessor),
											packetSequenceNumber_(packetSequenceNumber),
											compno_(compno),
											resno_(resno),
											precinctIndex_(precinctIndex),
											layno_(layno),
											data_(data),
											tileBytes_(tileBytes),
											remainingTilePartBytes_(remainingTilePartBytes),
											tagBitsPresent_(false),
											headerBytes_(0),
											signalledDataBytes_(0),
											readDataBytes_(0),
											lengthFromMarker_(lengthFromMarker)
{}

uint32_t PacketParser::numHeaderBytes(void){
	return headerBytes_;
}
uint32_t PacketParser::numSignalledDataBytes(void){
	return signalledDataBytes_;
}
uint32_t PacketParser::numReadDataBytes(void){
	return readDataBytes_;
}
uint32_t PacketParser::numSignalledBytes(void){
	return headerBytes_ + signalledDataBytes_;
}

bool PacketParser::readPacketHeader(void)
{
	auto active_src = data_;
	auto tilePtr = tileProcessor_->getTile();
	auto res = tilePtr->comps[compno_].tileCompResolution + resno_;
	auto tcp = tileProcessor_->getTileCodingParams();
	if(tcp->csty & J2K_CP_CSTY_SOP)
	{
		if(remainingTilePartBytes_ < 6)
			throw TruncatedPacketHeaderException();
		uint16_t marker =
			(uint16_t)(((uint16_t)(*active_src) << 8) | (uint16_t)(*(active_src + 1)));
		if(marker != J2K_MS_SOP)
		{
			GRK_WARN("Expected SOP marker, but found 0x%x", marker);
			throw CorruptPacketHeaderException();
		}
		else
		{
			uint16_t signalledPacketSequenceNumber =
				(uint16_t)(((uint16_t)active_src[4] << 8) | active_src[5]);
			if(signalledPacketSequenceNumber != (packetSequenceNumber_))
			{
				GRK_WARN("SOP marker packet counter %u does not match expected counter %u",
						signalledPacketSequenceNumber, packetSequenceNumber_);
				throw CorruptPacketHeaderException();
			}
			active_src += 6;
			remainingTilePartBytes_ -= 6;
		}
	}
	auto header_data_start = &active_src;
	auto remaining_length = &remainingTilePartBytes_;
	auto cp = tileProcessor_->cp_;
	if(cp->ppm_marker)
	{
		if(tileProcessor_->getIndex() >= cp->ppm_marker->tile_packet_headers_.size())
		{
			GRK_ERROR("PPM marker has no packed packet header data for tile %u",
					  tileProcessor_->getIndex() + 1);
			return false;
		}
		auto tile_packet_header = &cp->ppm_marker->tile_packet_headers_[tileProcessor_->getIndex()];
		header_data_start = &tile_packet_header->buf;
		remaining_length = &tile_packet_header->len;
	}
	else if(tcp->ppt)
	{
		header_data_start = &tcp->ppt_data;
		remaining_length = &tcp->ppt_len;
	}
	if(*remaining_length == 0)
		throw TruncatedPacketHeaderException();
	auto header_data = *header_data_start;
	std::unique_ptr<BitIO> bio(new BitIO(header_data, *remaining_length, false));
	auto tccp = tcp->tccps + compno_;
	try
	{
		tagBitsPresent_= bio->read();
		// GRK_INFO("present=%u ", present);
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
					throw TruncatedPacketHeaderException();
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
							GRK_WARN("Tile number: %u", tileProcessor_->getIndex() + 1);
							std::string msg =
								"Illegal inclusion tag tree found when decoding packet header.";
							GRK_WARN("%s", msg.c_str());
							tileProcessor_->setCorruptPacket();
						}
						included = (value <= layno_) ? 1 : 0;
					}
					else
					{
						included = bio->read();
					}
					if(!included)
					{
						if(cblk)
							cblk->numPassesInPacket = 0;
						// GRK_INFO("included=%u ", included);
						continue;
					}
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
								GRK_WARN("More missing code block bit planes (%u)"
										 " than supported number of bit planes (%u) in library.",
										 K_msbs, maxBitPlanesGRK);
								break;
							}
							imsb->decodeValue(bio.get(), cblkno, K_msbs, &value);
						}
						assert(K_msbs >= 1);
						K_msbs--;
						if(K_msbs > band->numbps)
						{
							GRK_WARN("More missing code block bit planes (%u) than band bit planes "
									 "(%u).",
									 K_msbs, band->numbps);
							// since we don't know how many bit planes are in this block, we
							// set numbps to max - the t1 decoder will sort it out
							cblk->numbps = maxBitPlanesGRK;
						}
						else
						{
							cblk->numbps = band->numbps - K_msbs;
						}
						if(cblk->numbps > maxBitPlanesGRK)
						{
							GRK_WARN("Number of bit planes %u is larger than maximum %u",
									 cblk->numbps, maxBitPlanesGRK);
							cblk->numbps = maxBitPlanesGRK;
						}
						cblk->numlenbits = 3;
					}
					bio->getnumpasses(&cblk->numPassesInPacket);
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
					auto blockPassesInPacket = (int32_t)cblk->numPassesInPacket;
					do
					{
						auto seg = cblk->getSegment(segno);
						/* sanity check when there is no mode switch */
						if(seg->maxpasses == maxPassesPerSegmentJ2K)
						{
							if(blockPassesInPacket > (int32_t)maxPassesPerSegmentJ2K)
							{
								GRK_WARN("Number of code block passes (%u) in packet is "
										 "suspiciously large.",
										 blockPassesInPacket);
								// TODO - we are truncating the number of passes at an arbitrary
								// value of maxPassesPerSegmentJ2K. We should probably either skip
								// the rest of this block, if possible, or do further sanity check
								// on packet
								seg->numPassesInPacket = maxPassesPerSegmentJ2K;
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
							GRK_ERROR("readPacketHeader: too many bits in segment length ");
							return false;
						}
						bio->read(&seg->numBytesInPacket, bits_to_read);
						signalledDataBytes_ += seg->numBytesInPacket;
#ifdef DEBUG_LOSSLESS_T2
						cblk->packet_length_info.push_back(
							PacketLengthInfo(seg->numBytesInPacket,
											 cblk->numlenbits + floorlog2(seg->numPassesInPacket)));
#endif
						/*
						 GRK_INFO(
						 "included=%u numPassesInPacket=%u increment=%u len=%u ",
						 included, seg->numPassesInPacket, increment,
						 seg->newlen);
						 */
						blockPassesInPacket -= (int32_t)seg->numPassesInPacket;
						if(blockPassesInPacket > 0)
							initSegment(cblk, ++segno, tccp->cblk_sty, false);
					} while(blockPassesInPacket > 0);
				}
			}
		}
		bio->inalign();
		header_data += bio->numBytes();
	}
	catch(InvalidMarkerException& ex)
	{
		GRK_UNUSED(ex);
		return false;
	}
	/* EPH markers */
	if(tcp->csty & J2K_CP_CSTY_EPH)
	{
		if((*remaining_length - (uint32_t)(header_data - *header_data_start)) < 2U)
			// GRK_WARN("Not enough space for expected EPH marker");
			throw TruncatedPacketHeaderException();
		uint16_t marker =
			(uint16_t)(((uint16_t)(*header_data) << 8) | (uint16_t)(*(header_data + 1)));
		if(marker != J2K_MS_EPH)
		{
			GRK_WARN("Expected EPH marker, but found 0x%x", marker);
			throw CorruptPacketHeaderException();
		}
		else
		{
			header_data += 2;
		}
	}
	auto header_length = (size_t)(header_data - *header_data_start);
	// GRK_INFO("hdrlen=%u ", header_length);
	// GRK_INFO("packet body\n");
	*remaining_length -= header_length;
	*header_data_start += header_length;
	headerBytes_ = (uint32_t)(active_src - data_);
	data_ += headerBytes_;
	if(!tagBitsPresent_ && !headerBytes_)
		throw TruncatedPacketHeaderException();
	// validate PL marker against parsed packet
	if(lengthFromMarker_ && lengthFromMarker_ != numSignalledBytes())
	{
		GRK_ERROR("Corrupt PL marker reports %u bytes for packet;"
				  " parsed bytes are in fact %u",
				  lengthFromMarker_, numSignalledBytes());
		return false;
	}

	return true;
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
bool PacketParser::readPacketData()
{
	if (!tagBitsPresent_){
		readPacketDataFinalize();
		return true;
	}
	uint32_t offset = 0;
	auto tile = tileProcessor_->getTile();
	auto res = tile->comps[compno_].tileCompResolution + resno_;
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
			if(!cblk->numPassesInPacket)
				continue;

			auto seg = cblk->getCurrentSegment();
			if(!seg || (seg->numpasses == seg->maxpasses))
				seg = cblk->nextSegment();
			uint32_t numPassesInPacket = cblk->numPassesInPacket;
			do
			{
				if(remainingTilePartBytes_ == 0)
					goto finish;
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
						GRK_ERROR("Segment packet length %u plus total segment length %u must be "
								  "less than 2^32",
								  seg->numBytesInPacket, seg->len);
						return false;
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
	readPacketDataFinalize();

	return true;
}

void PacketParser::readPacketDataFinalize(void){
	auto tile = tileProcessor_->getTile();
	update_maximum<uint8_t>((tile->comps + compno_)->highestResolutionDecompressed, resno_);
	tileProcessor_->incNumDecompressedPackets();
}

PrecinctParsers::PrecinctParsers(TileProcessor* tileProcessor) :
		tileProcessor_(tileProcessor),
		parsers_(new PacketParser*[tileProcessor_->getTileCodingParams()->numlayers]),
		numParsers_(0)
{
	for (uint16_t i = 0; i < tileProcessor_->getTileCodingParams()->numlayers; ++i)
		parsers_[i] = nullptr;
}

PrecinctParsers::~PrecinctParsers(void)
{
	for (uint16_t i = 0; i < tileProcessor_->getTileCodingParams()->numlayers; ++i)
		delete parsers_[i];
	delete[] parsers_;
}

}
