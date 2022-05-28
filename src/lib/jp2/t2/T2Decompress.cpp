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
 *
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */
#include "grk_includes.h"
//#define DEBUG_PACKET_ITERATOR

namespace grk
{
T2Decompress::T2Decompress(TileProcessor* tileProc) : tileProcessor(tileProc) {}
void T2Decompress::initSegment(DecompressCodeblock* cblk, uint32_t index, uint8_t cblk_sty,
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
bool T2Decompress::processPacket(TileCodingParams* tcp, PacketIter* pi, SparseBuffer* src)
{
#ifdef DEBUG_PLT
	static int ct = -1;
	ct++;
	bool hasPLT = tileProcessor->packetLengthCache.getMarkers() != nullptr;
#endif
	// read from PL marker, if available
	PacketInfo p;
	auto packetInfo = &p;
	if(!tileProcessor->packetLengthCache.next(&packetInfo))
		return false;
#ifdef DEBUG_PLT
	auto packetCache = *packetInfo;
	packetInfo->headerLength = 0;
	packetInfo->packetLength = 0;
	packetInfo->parsedData = false;
#endif
	auto tilec = tileProcessor->getTile()->comps + pi->getCompno();
	auto res = tilec->tileCompResolution + pi->getResno();
	auto skip = pi->getLayno() >= tcp->numLayersToDecompress ||
				pi->getResno() >= tilec->numResolutionsToDecompress;
	if(!skip && !tilec->isWholeTileDecoding())
	{
		skip = true;
		auto tilecBuffer = tilec->getBuffer();
		for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
		{
			auto band = res->tileBand + bandIndex;
			if(band->empty())
				continue;
			auto paddedBandWindow =
				tilecBuffer->getBandWindowPadded(pi->getResno(), band->orientation);
			auto prec =
				band->generatePrecinctBounds(pi->getPrecinctIndex(), res->precinctPartitionTopLeft,
											 res->precinctExpn, res->precinctGridWidth);
			if(paddedBandWindow->nonEmptyIntersection(&prec))
			{
				skip = false;
#ifdef DEBUG_PACKET_ITERATOR
				GRK_INFO("");
				GRK_INFO("Overlap detected with band %u =>", bandIndex);
				paddedBandWindow->print();
				GRK_INFO("Precinct bounds =>");
				prec.print();
				GRK_INFO("");
				pi->printDynamicState();
#endif
				break;
			}
		}
	}
	if(!skip || !packetInfo->packetLength)
	{
		for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
		{
			auto band = res->tileBand + bandIndex;
			if(band->empty())
				continue;
			if(!band->createPrecinct(false, pi->getPrecinctIndex(), res->precinctPartitionTopLeft,
									 res->precinctExpn, res->precinctGridWidth, res->cblkExpn))
				return false;
		}
	}
	if(skip)
	{
		if(packetInfo->packetLength)
			src->incrementCurrentChunkOffset(packetInfo->packetLength);
		else if(!decompressPacket(tcp, pi, src, packetInfo, true))
			return false;
	}
	else
	{
		if(!decompressPacket(tcp, pi, src, packetInfo, false))
			return false;
		tilec->highestResolutionDecompressed =
			std::max<uint8_t>(pi->getResno(), tilec->highestResolutionDecompressed);
		tileProcessor->incNumDecompressedPackets(1);
	}
	tileProcessor->incNumProcessedPackets(1);
#ifdef DEBUG_PLT
	if(hasPLT && packetCache.packetLength != packetInfo->packetLength)
	{
		printf("%u: parsed %u, PLT %u\n", ct, packetInfo->packetLength, packetCache.packetLength);
		assert(0);
	}
#endif

	return true;
}
bool T2Decompress::decompressPackets(uint16_t tile_no, SparseBuffer* src,
									 bool* stopProcessionPackets)
{
	auto cp = tileProcessor->cp_;
	auto tcp = cp->tcps + tile_no;
	*stopProcessionPackets = false;
	PacketManager packetManager(false, tileProcessor->headerImage, cp, tile_no, FINAL_PASS,
								tileProcessor);
	tileProcessor->packetLengthCache.rewind();
	auto markers = tileProcessor->packetLengthCache.getMarkers();
	if(markers && !markers->isEnabled())
		markers = nullptr;
	for(uint32_t pino = 0; pino < tcp->getNumProgressions(); ++pino)
	{
		auto currPi = packetManager.getPacketIter(pino);
		if(currPi->getProgression() == GRK_PROG_UNKNOWN)
		{
			GRK_ERROR("decompressPackets: Unknown progression order");
			return false;
		}
#ifdef DEBUG_PACKET_ITERATOR
		currPi->printStaticState();
#endif
		while(currPi->next(markers ? src : nullptr))
		{
			if(src->getCurrentChunkLength() == 0)
			{
				GRK_WARN("Tile %u is truncated.", tile_no);
				*stopProcessionPackets = true;
				break;
			}
			try
			{
				if(!processPacket(tcp, currPi, src))
					return false;
			}
			catch(TruncatedPacketHeaderException& tex)
			{
				GRK_UNUSED(tex);
				GRK_WARN("Truncated packet: tile=%u component=%02d resolution=%02d precinct=%03d "
						 "layer=%02d",
						 tile_no, currPi->getCompno(), currPi->getResno(),
						 currPi->getPrecinctIndex(), currPi->getLayno());
				*stopProcessionPackets = true;
				break;
			}
			catch(CorruptPacketHeaderException& cex)
			{
				GRK_UNUSED(cex);
				// we can skip corrupt packet if PLT markers are present
				if(!tileProcessor->packetLengthCache.getMarkers())
				{
					GRK_ERROR(
						"Corrupt packet: tile=%u component=%02d resolution=%02d precinct=%03d "
						"layer=%02d",
						tile_no, currPi->getCompno(), currPi->getResno(),
						currPi->getPrecinctIndex(), currPi->getLayno());
					*stopProcessionPackets = true;
					break;
				}
				else
				{
					GRK_WARN("Corrupt packet: tile=%u component=%02d resolution=%02d precinct=%03d "
							 "layer=%02d",
							 tile_no, currPi->getCompno(), currPi->getResno(),
							 currPi->getPrecinctIndex(), currPi->getLayno());
				}
				// ToDo: skip corrupt packet if SOP marker is present
			}
		}
		if(*stopProcessionPackets)
			break;
	}
	if(tileProcessor->getNumDecompressedPackets() == 0)
		GRK_WARN("T2Decompress: no packets for tile %u were successfully read", tile_no);

	return tileProcessor->getNumDecompressedPackets() > 0;
}
bool T2Decompress::decompressPacket(TileCodingParams* tcp, const PacketIter* pi,
									SparseBuffer* srcBuf, PacketInfo* packetInfo, bool skipData)
{
	auto tile = tileProcessor->getTile();
	auto res = tile->comps[pi->getCompno()].tileCompResolution + pi->getResno();
	bool tagBitsPresent;
	uint32_t packetDataBytes;
	uint32_t packetBytes;
	if(!readPacketHeader(tcp, pi, &tagBitsPresent, srcBuf, &packetBytes, &packetDataBytes))
		return false;
	packetBytes += packetDataBytes;
	// validate PL marker against parsed packet
	if(packetInfo->packetLength &&  packetInfo->packetLength != packetBytes)
	{
		GRK_ERROR("Corrupt PL marker reports %u bytes for packet;"
				  " parsed bytes are in fact %u",
				  packetInfo->packetLength, packetBytes);
		return false;
	}
	packetInfo->packetLength = packetBytes;
	if(tagBitsPresent)
	{
		if(skipData || packetInfo->parsedData)
		{
			srcBuf->incrementCurrentChunkOffset(packetDataBytes);
		}
		else
		{
			if(!readPacketData(res, pi, srcBuf))
				return false;
			packetInfo->parsedData = true;
		}
	}

	return true;
}
bool T2Decompress::readPacketHeader(TileCodingParams* tcp,
									const PacketIter* pi,
									bool* tagBitsPresent,
									SparseBuffer* src,
									uint32_t* packetHeaderBytes,
									uint32_t* packetDataBytes)
{
	assert(packetDataBytes);
	assert(packetHeaderBytes);
	assert(src);
	assert(pi);
	assert(tcp);
	assert(tagBitsPresent);
	auto tilePtr = tileProcessor->getTile();
	auto res = tilePtr->comps[pi->getCompno()].tileCompResolution + pi->getResno();
	auto srcPtr = src->getCurrentChunkPtr();
	size_t available_bytes = src->getCurrentChunkLength();
	auto active_src = srcPtr;
	*packetHeaderBytes = 0;
	*packetDataBytes = 0;

	if(tcp->csty & J2K_CP_CSTY_SOP)
	{
		if(available_bytes < 6)
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
			uint16_t numIteratedPackets =
				(uint16_t)(((uint16_t)active_src[4] << 8) | active_src[5]);
			if(numIteratedPackets != (tileProcessor->getNumProcessedPackets() % 0x10000))
			{
				GRK_WARN("SOP marker packet counter %u does not match expected counter %u",
						 numIteratedPackets, tileProcessor->getNumProcessedPackets());
				throw CorruptPacketHeaderException();
			}
			active_src += 6;
			available_bytes -= 6;
		}
	}
	auto header_data_start = &active_src;
	auto remaining_length = &available_bytes;
	auto cp = tileProcessor->cp_;
	if(cp->ppm_marker)
	{
		if(tileProcessor->getIndex() >= cp->ppm_marker->tile_packet_headers_.size())
		{
			GRK_ERROR("PPM marker has no packed packet header data for tile %u",
					  tileProcessor->getIndex() + 1);
			return false;
		}
		auto tile_packet_header = &cp->ppm_marker->tile_packet_headers_[tileProcessor->getIndex()];
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
	uint32_t present;
	std::unique_ptr<BitIO> bio(new BitIO(header_data, *remaining_length, false));
	auto tccp = tcp->tccps + pi->getCompno();
	try
	{
		present = bio->read();
		// GRK_INFO("present=%u ", present);
		if(present)
		{
			for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
			{
				auto band = res->tileBand + bandIndex;
				if(band->empty())
					continue;
				auto prc = band->getPrecinct(pi->getPrecinctIndex());
				if(!prc)
					continue;
				auto numPrecCodeBlocks = prc->getNumCblks();
				// assuming 1 bit minimum encoded per code block,
				// let's check if we have enough bytes
				if ( (numPrecCodeBlocks >> 3) > src->totalLength())
					throw TruncatedPacketHeaderException();
				for(uint64_t cblkno = 0; cblkno < numPrecCodeBlocks; cblkno++)
				{
					auto cblk = prc->tryGetDecompressedBlockPtr(cblkno);
					uint8_t included;
					if(!cblk || !cblk->numlenbits)
					{
						uint16_t value;
						auto incl = prc->getInclTree();
						incl->decodeValue(bio.get(), cblkno, pi->getLayno() + 1,&value);
						if(value != incl->getUninitializedValue() && value != pi->getLayno())
						{
							GRK_WARN("Tile number: %u", tileProcessor->getIndex() + 1);
							std::string msg =
								"Illegal inclusion tag tree found when decoding packet header.";
							GRK_WARN("%s", msg.c_str());
							tileProcessor->setCorruptPacket();
						}
						included = (value <= pi->getLayno()) ? 1 : 0;
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
						{
							++segno;
							initSegment(cblk, segno, tccp->cblk_sty, false);
						}
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
						uint8_t bits_to_read =
							cblk->numlenbits + floorlog2(seg->numPassesInPacket);
						if(bits_to_read > 32)
						{
							GRK_ERROR("readPacketHeader: too many bits in segment length ");
							return false;
						}
						bio->read(&seg->numBytesInPacket, bits_to_read);
						*packetDataBytes += seg->numBytesInPacket;
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
						{
							++segno;
							initSegment(cblk, segno, tccp->cblk_sty, false);
						}
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
	*tagBitsPresent = present;
	*packetHeaderBytes = (uint32_t)(active_src - srcPtr);
	src->incrementCurrentChunkOffset(*packetHeaderBytes);
	if(!present && !*packetHeaderBytes)
		throw TruncatedPacketHeaderException();

	return true;
}
bool T2Decompress::readPacketData(Resolution* res, const PacketIter* p_pi, SparseBuffer* srcBuf)
{
	for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
	{
		auto band = res->tileBand + bandIndex;
		if(band->empty())
			continue;
		auto prc = band->getPrecinct(p_pi->getPrecinctIndex());
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
				size_t maxLen = srcBuf->getCurrentChunkLength();
				if(maxLen == 0)
					return true;
				if(((seg->numBytesInPacket) > maxLen))
				{
					// HT doesn't tolerate truncated code blocks since decoding runs both forward
					// and reverse. So, in this case, we ignore the entire code block
					if(tileProcessor->cp_->tcps[0].isHT())
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
					size_t maxSegmentLength = srcBuf->getCurrentChunkLength();
					// correct for truncated packet
					if(seg->numBytesInPacket > maxSegmentLength)
						seg->numBytesInPacket = (uint32_t)maxSegmentLength;
					cblk->seg_buffers.push_back(
						new grk_buf8(srcBuf->getCurrentChunkPtr(), seg->numBytesInPacket, false));
					srcBuf->incrementCurrentChunkOffset(seg->numBytesInPacket);
					cblk->compressedStream.len += seg->numBytesInPacket;
					seg->len += seg->numBytesInPacket;
				}
				seg->numpasses += seg->numPassesInPacket;
				numPassesInPacket -= seg->numPassesInPacket;
				if(numPassesInPacket > 0)
					seg = cblk->nextSegment();
			} while(numPassesInPacket > 0);
		} /* next code_block */
	}

	return true;
}

} // namespace grk
