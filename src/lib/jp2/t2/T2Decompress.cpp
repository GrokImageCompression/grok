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
T2Decompress::T2Decompress(TileProcessor* tileProc) : tileProcessor(tileProc)
{}

bool T2Decompress::processPacket(uint16_t compno, uint8_t resno,
								 uint64_t precinctIndex, uint16_t layno, SparseBuffer* src)
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
#endif
	auto tilec = tileProcessor->getTile()->comps + compno;
	auto res = tilec->tileCompResolution + resno;
	auto tcp = tileProcessor->getTileCodingParams();
	auto skip = layno >= tcp->numLayersToDecompress || resno >= tilec->numResolutionsToDecompress;
	if(!skip && !tilec->isWholeTileDecoding())
	{
		skip = true;
		auto tilecBuffer = tilec->getBuffer();
		for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
		{
			auto band = res->tileBand + bandIndex;
			if(band->empty())
				continue;
			auto paddedBandWindow = tilecBuffer->getBandWindowPadded(resno, band->orientation);
			auto prec = band->generatePrecinctBounds(precinctIndex, res->precinctPartitionTopLeft,
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
			if(!band->createPrecinct(false, precinctIndex, res->precinctPartitionTopLeft,
									 res->precinctExpn, res->precinctGridWidth, res->cblkExpn))
				return false;
		}
	}
	if(skip)
	{
		if(packetInfo->packetLength)
			src->incrementCurrentChunkOffset(packetInfo->packetLength);
		else if(!decompressPacket(compno, resno, precinctIndex, layno, src, packetInfo, true))
			return false;
	}
	else
	{
		if(!decompressPacket(compno, resno, precinctIndex, layno, src, packetInfo, false))
			return false;
		update_maximum<uint8_t>(tilec->highestResolutionDecompressed, resno);
		tileProcessor->incNumDecompressedPackets();
	}
	tileProcessor->incNumProcessedPackets();
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
	auto tcp = tileProcessor->getTileCodingParams();
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
				if(!processPacket(currPi->getCompno(), currPi->getResno(),
								  currPi->getPrecinctIndex(), currPi->getLayno(), src))
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
bool T2Decompress::decompressPacket(uint16_t compno,
									uint8_t resno,
									uint64_t precinctIndex,
									uint16_t layno,
									SparseBuffer* srcBuf,
									PacketInfo* packetInfo,
									bool skipData)
{
	auto container = PrecinctParsers(tileProcessor);
	auto parser =
			PacketParser(&container,
					tileProcessor->getNumProcessedPackets() & 0xFFFF,
					compno,resno,precinctIndex,layno,
					srcBuf->getCurrentChunkPtr(), srcBuf->totalLength(),
					srcBuf->getCurrentChunkLength());

	bool tagBitsPresent;
	uint32_t packetDataBytes;
	uint32_t packetHeaderBytes;
	if(!parser.readPacketHeader(&tagBitsPresent,
						 &packetHeaderBytes, &packetDataBytes))
		return false;
	srcBuf->incrementCurrentChunkOffset(packetHeaderBytes);
	auto packetBytes = packetHeaderBytes + packetDataBytes;
	// validate PL marker against parsed packet
	if(packetInfo->packetLength && packetInfo->packetLength != packetBytes)
	{
		GRK_ERROR("Corrupt PL marker reports %u bytes for packet;"
				  " parsed bytes are in fact %u",
				  packetInfo->packetLength, packetBytes);
		return false;
	}
	packetInfo->packetLength = packetBytes;
	if(tagBitsPresent)
	{
		if(skipData)
		{
			srcBuf->incrementCurrentChunkOffset(packetDataBytes);
		}
		else
		{
			uint32_t packetDataRead = 0;
			if(!parser.readPacketData(&packetDataRead))
				return false;
			srcBuf->incrementCurrentChunkOffset(packetDataRead);
		}
	}

	return true;
}
} // namespace grk
