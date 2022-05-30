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
T2Decompress::T2Decompress(TileProcessor* tileProc) :
		tileProcessor(tileProc),
		parserMap_(new ParserMap(tileProc))
{}

T2Decompress::~T2Decompress(void){
	delete parserMap_;
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
				if(!processPacket(	currPi->getCompno(),
									currPi->getResno(),
								  currPi->getPrecinctIndex(),
								  currPi->getLayno(), src))
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
	// run the parsers
	if (!parserMap_->precinctParsers_.empty()) {
		auto numThreads =
			std::min<size_t>(ExecSingleton::get()->num_workers(),
					parserMap_->precinctParsers_.size());
		if (numThreads == 1) {
			for (std::pair<const uint64_t,PrecinctPacketParsers*>& pp :
					parserMap_->precinctParsers_){
				std::pair<const uint64_t,PrecinctPacketParsers*>& ppair = pp;
				for (uint64_t i = 0; i < ppair.second->numParsers_; ++i){
					if (!decompressPacket(ppair.second->parsers_[i], false))
						return false;
				}
			}
		} else {
			tf::Taskflow taskflow;
			auto numTasks = parserMap_->precinctParsers_.size();
			auto tasks = new tf::Task[numTasks];
			for(uint64_t i = 0; i < numTasks; i++)
				tasks[i] = taskflow.placeholder();
			uint64_t i = 0;
			for (std::pair<const uint64_t,PrecinctPacketParsers*>& pp :
					parserMap_->precinctParsers_){
				std::pair<const uint64_t,PrecinctPacketParsers*>& ppair = pp;
				auto decompressor = [this,ppair]() {
					for (uint64_t j = 0; j < ppair.second->numParsers_; ++j){
						if (!decompressPacket(ppair.second->parsers_[j], false))
							return false;
					}
					return true;
				};
				tasks[i++].work(decompressor);
			}
			ExecSingleton::get()->run(taskflow).wait();
			delete[] tasks;
		}
	}

	if(tileProcessor->getNumDecompressedPackets() == 0)
		GRK_WARN("T2Decompress: no packets for tile %u were successfully read", tile_no);

	return tileProcessor->getNumDecompressedPackets() > 0;
}

bool T2Decompress::processPacket(uint16_t compno, uint8_t resno,
								 uint64_t precinctIndex,
								 uint16_t layno,
								 SparseBuffer* src)
{
	// read from PL marker, if available
	PacketInfo p;
	auto packetInfo = &p;
	if(!tileProcessor->packetLengthCache.next(&packetInfo))
		return false;
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
	auto parser =
			new PacketParser(tileProcessor,
					tileProcessor->getNumProcessedPackets() & 0xFFFF,
					compno,resno,precinctIndex,layno,
					src->getCurrentChunkPtr(),
					packetInfo->packetLength,
					src->totalLength(),
					src->getCurrentChunkLength());
	if(packetInfo->packetLength) {
		src->incrementCurrentChunkOffset(packetInfo->packetLength);
		if (!skip)
			parserMap_->pushParser(precinctIndex,parser);
	} else {
		bool rc = false;
		try {
		  rc = decompressPacket(parser, skip);
		} catch (std::exception& ex){
			delete parser;
			throw;
		}
		if (!rc) {
			delete parser;
			return false;
		}
		src->incrementCurrentChunkOffset(parser->numHeaderBytes() +
				(skip ? parser->numSignalledDataBytes() : parser->numReadDataBytes()));
		delete parser;
	}
	tileProcessor->incNumProcessedPackets();

	return true;
}
bool T2Decompress::decompressPacket(PacketParser *parser,
									bool skipData)
{
	if(!parser->readPacketHeader())
		return false;

	return skipData || parser->readPacketData();
}
} // namespace grk
