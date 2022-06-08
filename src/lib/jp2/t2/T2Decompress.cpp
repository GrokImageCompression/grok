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

namespace grk
{
T2Decompress::T2Decompress(TileProcessor* tileProc) : tileProcessor(tileProc) {}

void T2Decompress::decompressPackets(uint16_t tile_no, SparseBuffer* src,
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
				{
					*stopProcessionPackets = true;
					break;
				}
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
			catch(CorruptPacketException& cex)
			{
				GRK_UNUSED(cex);
				// we can skip corrupt packet if PLT markers are present
				if(!tileProcessor->packetLengthCache.getMarkers())
				{
					GRK_WARN("Corrupt packet: tile=%u component=%02d resolution=%02d precinct=%03d "
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
}

bool T2Decompress::processPacket(uint16_t compno, uint8_t resno, uint64_t precinctIndex,
								 uint16_t layno, SparseBuffer* src)
{
	// read from PL marker, if available
	PacketInfo p;
	auto packetInfo = &p;
	if(!tileProcessor->packetLengthCache.next(&packetInfo))
		return false;
	auto tilec = tileProcessor->getTile()->comps + compno;
	auto res = tilec->resolutions_ + resno;
	auto tcp = tileProcessor->getTileCodingParams();
	auto skip = layno >= tcp->numLayersToDecompress || resno >= tilec->numResolutionsToDecompress;
	if(!skip && !tilec->isWholeTileDecoding())
	{
		skip = true;
		auto tilecBuffer = tilec->getWindow();
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
			if(!band->createPrecinct(tileProcessor, precinctIndex, res->precinctPartitionTopLeft,
									 res->precinctExpn, res->precinctGridWidth, res->cblkExpn))
				return false;
		}
	}
	auto parser = new PacketParser(tileProcessor, tileProcessor->getNumProcessedPackets() & 0xFFFF,
								   compno, resno, precinctIndex, layno, src->getCurrentChunkPtr(),
								   packetInfo->packetLength, src->totalLength(),
								   src->getCurrentChunkLength());
	uint32_t packetLen = packetInfo->packetLength;
	if(!packetInfo->packetLength)
	{
		try
		{
			parser->readHeader();
		}
		catch(std::exception& ex)
		{
			GRK_UNUSED(ex);
			delete parser;
			throw;
		}
		packetLen = parser->numHeaderBytes() + parser->numSignalledDataBytes();
	}
	try
	{
		src->incrementCurrentChunkOffset(packetLen);
	}
	catch(SparseBufferOverrunException& sboe)
	{
		GRK_UNUSED(sboe);
		delete parser;
		return false;
	}
	if(!skip)
		readPacketData(res, parser, precinctIndex, packetInfo->packetLength);
	tileProcessor->incNumProcessedPackets();

	return true;
}
void T2Decompress::readPacketData(Resolution* res, PacketParser* parser, uint64_t precinctIndex,
								  bool defer)
{
	if(defer)
	{
		res->parserMap_->pushParser(precinctIndex, parser);
	}
	else
	{
		try
		{
			parser->readHeader();
			parser->readData();
			delete parser;
		}
		catch(std::exception& ex)
		{
			GRK_UNUSED(ex);
			delete parser;
			throw;
		}
	}
}
void T2Decompress::decompressPacket(PacketParser* parser, bool skipData)
{
	parser->readHeader();
	if(!skipData)
		parser->readData();
}
} // namespace grk
