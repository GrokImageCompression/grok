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

//#define DEBUG_ENCODE_PACKETS

namespace grk
{
T2Compress::T2Compress(TileProcessor* tileProc) : tileProcessor(tileProc) {}
bool T2Compress::compressPackets(uint16_t tile_no, uint16_t max_layers, IBufferedStream* stream,
								 uint32_t* tileBytesWritten, bool first_poc_tile_part,
								 uint32_t newTilePartProgressionPosition, uint32_t pino)
{
	auto cp = tileProcessor->m_cp;
	auto image = tileProcessor->headerImage;
	auto tilePtr = tileProcessor->tile;
	auto tcp = &cp->tcps[tile_no];
	PacketManager packetManager(true, image, cp, tile_no, FINAL_PASS, tileProcessor);
	packetManager.enableTilePartGeneration(pino, first_poc_tile_part,
										   newTilePartProgressionPosition);
	auto current_pi = packetManager.getPacketIter(pino);
	if(current_pi->prog.progression == GRK_PROG_UNKNOWN)
	{
		GRK_ERROR("compressPackets: Unknown progression order");
		return false;
	}
	while(current_pi->next())
	{
		if(current_pi->layno < max_layers)
		{
			uint32_t numBytes = 0;
			if(!compressPacket(tcp, current_pi, stream, &numBytes))
				return false;
			*tileBytesWritten += numBytes;
			tilePtr->numProcessedPackets++;
		}
	}
	// if (tile_no == 3)
	//	GRK_INFO("Tile %d : compressed packet bytes %d", tile_no, *tileBytesWritten);

	return true;
}
bool T2Compress::compressPacketsSimulate(uint16_t tile_no, uint16_t max_layers,
										 uint32_t* allPacketBytes, uint32_t maxBytes,
										 uint32_t newTilePartProgressionPosition,
										 PacketLengthMarkers* markers, bool finalSimulation)
{
	assert(allPacketBytes);
	auto cp = tileProcessor->m_cp;
	auto image = tileProcessor->headerImage;
	auto tcp = cp->tcps + tile_no;
	uint32_t pocno = (cp->rsiz == GRK_PROFILE_CINEMA_4K) ? 2 : 1;

	// Cinema profile has CPRL progression and maximum component size specification,
	// so in this case, we set max_comp to the number of components, so we can ensure that
	// each component length meets spec. Otherwise, set to 1.
	uint32_t max_comp = cp->m_coding_params.m_enc.m_max_comp_size > 0 ? image->numcomps : 1;

	PacketManager packetManager(true, image, cp, tile_no, THRESH_CALC, tileProcessor);
	*allPacketBytes = 0;
	tileProcessor->getPacketTracker()->clear();
	for(uint32_t compno = 0; compno < max_comp; ++compno)
	{
		uint64_t componentBytes = 0;
		for(uint32_t poc = 0; poc < pocno; ++poc)
		{
			auto current_pi = packetManager.getPacketIter(poc);
			packetManager.enableTilePartGeneration(poc, (compno == 0),
												   newTilePartProgressionPosition);

			if(current_pi->prog.progression == GRK_PROG_UNKNOWN)
			{
				GRK_ERROR("decompress_packets_simulate: Unknown progression order");
				return false;
			}
			while(current_pi->next())
			{
				if(current_pi->layno < max_layers)
				{
					uint32_t bytesInPacket = 0;
					if(!compressPacketSimulate(tcp, current_pi, &bytesInPacket, maxBytes, markers,
											   finalSimulation))
						return false;

					componentBytes += bytesInPacket;
					if(maxBytes != UINT_MAX)
						maxBytes -= bytesInPacket;
					*allPacketBytes += bytesInPacket;
					if(cp->m_coding_params.m_enc.m_max_comp_size &&
					   componentBytes > cp->m_coding_params.m_enc.m_max_comp_size)
						return false;
				}
			}
		}
	}
	// if (finalSimulation && tile_no == 3)
	//	GRK_INFO("Tile %d : simulated compressed packet bytes %d", tile_no, *allPacketBytes);

	return true;
}

bool T2Compress::compressHeader(BitIO* bio, Resolution* res, uint16_t layno, uint64_t precinctIndex)
{
	if(layno == 0)
	{
		for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
		{
			auto band = res->tileBand + bandIndex;
			if(precinctIndex >= band->precincts.size())
			{
				GRK_ERROR("compress packet simulate: precinct index %d must be less than total "
						  "number of precincts %d",
						  precinctIndex, band->precincts.size());
				return false;
			}
			auto prc = band->precincts[precinctIndex];
			uint64_t nb_blocks = prc->getNumCblks();

			if(band->isEmpty() || !nb_blocks)
				continue;

			if(prc->getInclTree())
				prc->getInclTree()->reset();
			if(prc->getImsbTree())
				prc->getImsbTree()->reset();
			for(uint64_t cblkno = 0; cblkno < nb_blocks; ++cblkno)
			{
				auto cblk = prc->getCompressedBlockPtr(cblkno);
				cblk->numPassesInPacket = 0;
				assert(band->numbps >= cblk->numbps);
				if(cblk->numbps > band->numbps)
					GRK_WARN("Code block %u bps %d greater than band bps %d. Skipping.", cblkno,
							 cblk->numbps, band->numbps);
				else
					prc->getImsbTree()->setvalue(cblkno, band->numbps - cblk->numbps);
			}
		}
	}

	// Empty header bit. Grok always sets this to 1,
	// even though there is also an option to set it to zero.
	if(!bio->write(1, 1))
		return false;

	/* Writing Packet header */
	for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
	{
		auto band = res->tileBand + bandIndex;
		auto prc = band->precincts[precinctIndex];
		uint64_t nb_blocks = prc->getNumCblks();

		if(band->isEmpty() || !nb_blocks)
			continue;
		for(uint64_t cblkno = 0; cblkno < nb_blocks; ++cblkno)
		{
			auto cblk = prc->getCompressedBlockPtr(cblkno);
			auto layer = cblk->layers + layno;
			if(!cblk->numPassesInPacket && layer->numpasses)
				prc->getInclTree()->setvalue(cblkno, layno);
		}
		for(uint64_t cblkno = 0; cblkno < nb_blocks; cblkno++)
		{
			auto cblk = prc->getCompressedBlockPtr(cblkno);
			auto layer = cblk->layers + layno;
			uint8_t increment = 0;
			uint32_t nump = 0;
			uint32_t len = 0;

			/* cblk inclusion bits */
			if(!cblk->numPassesInPacket)
			{
				if(!prc->getInclTree()->compress(bio, cblkno, layno + 1))
					return false;
			}
			else
			{
#ifdef DEBUG_LOSSLESS_T2
				cblk->included = layer->numpasses != 0 ? 1 : 0;
#endif
				if(!bio->write(layer->numpasses != 0, 1))
					return false;
			}
			/* if cblk not included, go to next cblk  */
			if(!layer->numpasses)
				continue;
			/* if first instance of cblk --> zero bit-planes information */
			if(!cblk->numPassesInPacket)
			{
				cblk->numlenbits = 3;
				if(!prc->getImsbTree()->compress(bio, cblkno,
												 prc->getImsbTree()->getUninitializedValue()))
					return false;
			}
			/* number of coding passes included */
			if(!bio->putnumpasses(layer->numpasses))
				return false;
			uint32_t nb_passes = cblk->numPassesInPacket + layer->numpasses;
			auto pass = cblk->passes + cblk->numPassesInPacket;

			/* computation of the increase of the length indicator and insertion in the header */
			for(uint32_t passno = cblk->numPassesInPacket; passno < nb_passes; ++passno)
			{
				++nump;
				len += pass->len;

				if(pass->term || passno == nb_passes - 1)
				{
					increment = (uint8_t)std::max<int8_t>(
						(int8_t)increment,
						int8_t(floorlog2(len) + 1 - (cblk->numlenbits + floorlog2(nump))));
					len = 0;
					nump = 0;
				}
				++pass;
			}
			if(!bio->putcommacode(increment))
				return false;
			/* computation of the new Length indicator */
			cblk->numlenbits += increment;

			pass = cblk->passes + cblk->numPassesInPacket;
			/* insertion of the codeword segment length */
			for(uint32_t passno = cblk->numPassesInPacket; passno < nb_passes; ++passno)
			{
				nump++;
				len += pass->len;
				if(pass->term || passno == nb_passes - 1)
				{
#ifdef DEBUG_LOSSLESS_T2
					cblk->packet_length_info.push_back(PacketLengthInfo(
						len, cblk->numlenbits + (uint32_t)floorlog2((int32_t)nump)));
#endif
					if(!bio->write(len, cblk->numlenbits + floorlog2(nump)))
						return false;
					len = 0;
					nump = 0;
				}
				++pass;
			}
		}
	}
	if(!bio->flush())
		return false;

	return true;
}
bool T2Compress::compressPacket(TileCodingParams* tcp, PacketIter* pi, IBufferedStream* stream,
								uint32_t* packet_bytes_written)
{
	assert(stream);

	uint32_t compno = pi->compno;
	uint32_t resno = pi->resno;
	uint64_t precinctIndex = pi->precinctIndex;
	uint16_t layno = pi->layno;
	auto tile = tileProcessor->tile;
	auto tilec = tile->comps + compno;
	size_t stream_start = stream->tell();

	if(compno >= tile->numcomps)
	{
		GRK_ERROR("compress packet simulate: component number %d must be less than total number "
				  "of components %d",
				  compno, tile->numcomps);
		return false;
	}
	if(tileProcessor->getPacketTracker()->is_packet_encoded(compno, resno, precinctIndex, layno))
		return true;
	tileProcessor->getPacketTracker()->packet_encoded(compno, resno, precinctIndex, layno);

#ifdef DEBUG_ENCODE_PACKETS
	GRK_INFO("compress packet compono=%u, resno=%u, precinctIndex=%u, layno=%u", compno, resno,
			 precinctIndex, layno);
#endif
	// SOP marker
	if(tcp->csty & J2K_CP_CSTY_SOP)
	{
		if(!stream->writeByte(J2K_MS_SOP >> 8))
			return false;
		if(!stream->writeByte(J2K_MS_SOP & 0xff))
			return false;
		if(!stream->writeByte(0))
			return false;
		if(!stream->writeByte(4))
			return false;
		/* numProcessedPackets is uint64_t modulo 65536, in big endian format */
		// note - when compressing, numProcessedPackets in fact equals packet index,
		// i.e. one less than number of processed packets
		uint16_t numProcessedPackets = (uint16_t)(tile->numProcessedPackets & 0xFFFF);
		if(!stream->writeByte((uint8_t)(numProcessedPackets >> 8)))
			return false;
		if(!stream->writeByte((uint8_t)(numProcessedPackets & 0xff)))
			return false;
	}
	std::unique_ptr<BitIO> bio;
	bio = std::unique_ptr<BitIO>(new BitIO(stream, true));

	// initialize precinct and code blocks if this is the first layer
	auto res = tilec->tileCompResolution + resno;
	if(!compressHeader(bio.get(), res, layno, precinctIndex))
		return false;

	// EPH marker
	if(tcp->csty & J2K_CP_CSTY_EPH)
	{
		if(!stream->writeByte(J2K_MS_EPH >> 8))
			return false;
		if(!stream->writeByte(J2K_MS_EPH & 0xff))
			return false;
	}

	// if (tileProcessor->m_tileIndex == 3)
	//	GRK_INFO("Tile %d:  packet header length: %d for layer %d",
	//			tileProcessor->m_tileIndex, stream->tell() - stream_start,layno);

	// GRK_INFO("Written packet header bytes %d for layer %d", (uint32_t)(stream->tell() -
	// stream_start),layno);
	/* Writing the packet body */
	for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
	{
		auto band = res->tileBand + bandIndex;
		auto prc = band->precincts[precinctIndex];
		uint64_t nb_blocks = prc->getNumCblks();

		if(band->isEmpty() || !nb_blocks)
			continue;
		for(uint64_t cblkno = 0; cblkno < nb_blocks; ++cblkno)
		{
			auto cblk = prc->getCompressedBlockPtr(cblkno);
			auto cblk_layer = cblk->layers + layno;
			if(!cblk_layer->numpasses)
				continue;
			if(cblk_layer->len && !stream->writeBytes(cblk_layer->data, cblk_layer->len))
				return false;
			cblk->numPassesInPacket += cblk_layer->numpasses;
		}
	}
	*packet_bytes_written += (uint32_t)(stream->tell() - stream_start);

	// if (tileProcessor->m_tileIndex == 3)
	//	GRK_INFO("Tile %d : written packet length: %d for layer %d",
	//			tileProcessor->m_tileIndex, *packet_bytes_written,layno);
	return true;
}

bool T2Compress::compressPacketSimulate(TileCodingParams* tcp, PacketIter* pi,
										uint32_t* packet_bytes_written,
										uint32_t max_bytes_available, PacketLengthMarkers* markers,
										bool finalSimulation)
{
	GRK_UNUSED(finalSimulation);
	uint32_t compno = pi->compno;
	uint32_t resno = pi->resno;
	uint64_t precinctIndex = pi->precinctIndex;
	uint16_t layno = pi->layno;
	uint64_t nb_blocks;
	auto tile = tileProcessor->tile;
	auto tilec = tile->comps + compno;
	auto res = tilec->tileCompResolution + resno;
	uint64_t byteCount = 0;
	*packet_bytes_written = 0;

	if(compno >= tile->numcomps)
	{
		GRK_ERROR("compress packet simulate: component number %d must be less than total number "
				  "of components %d",
				  compno, tile->numcomps);
		return false;
	}
	if(tileProcessor->getPacketTracker()->is_packet_encoded(compno, resno, precinctIndex, layno))
		return true;
	tileProcessor->getPacketTracker()->packet_encoded(compno, resno, precinctIndex, layno);
#ifdef DEBUG_ENCODE_PACKETS
	GRK_INFO("simulate compress packet compono=%u, resno=%u, precinctIndex=%u, layno=%u", compno,
			 resno, precinctIndex, layno);
#endif
	if(tcp->csty & J2K_CP_CSTY_SOP)
	{
		if(max_bytes_available < 6)
			return false;
		if(max_bytes_available != UINT_MAX)
			max_bytes_available -= 6;
		byteCount += 6;
	}
	std::unique_ptr<BitIO> bio(new BitIO(nullptr, max_bytes_available, true));
	if(!compressHeader(bio.get(), res, layno, precinctIndex))
		return false;

	byteCount += (uint32_t)bio->numBytes();
	// if (max_bytes_available == UINT_MAX)
	//	GRK_INFO("Simulated packet header bytes %d for layer %d", *packet_bytes_written,layno);
	if(max_bytes_available != UINT_MAX)
		max_bytes_available -= (uint32_t)bio->numBytes();
	if(tcp->csty & J2K_CP_CSTY_EPH)
	{
		if(max_bytes_available < 2)
			return false;
		if(max_bytes_available != UINT_MAX)
			max_bytes_available -= 2;
		byteCount += 2;
	}
	// if (finalSimulation && tileProcessor->m_tileIndex == 3 && byteCount == 553)
	//	GRK_INFO("Tile %d, simulated packet header length: %d for layer %d",
	//			tileProcessor->m_tileIndex, byteCount,layno);
	/* Writing the packet body */
	for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
	{
		auto band = res->tileBand + bandIndex;
		auto prc = band->precincts[precinctIndex];

		nb_blocks = prc->getNumCblks();
		for(uint64_t cblkno = 0; cblkno < nb_blocks; ++cblkno)
		{
			auto cblk = prc->getCompressedBlockPtr(cblkno);
			auto layer = cblk->layers + layno;

			if(!layer->numpasses)
				continue;
			if(layer->len > max_bytes_available)
				return false;
			cblk->numPassesInPacket += layer->numpasses;
			byteCount += layer->len;
			if(max_bytes_available != UINT_MAX)
				max_bytes_available -= layer->len;
		}
	}
	if(byteCount > UINT_MAX)
	{
		GRK_ERROR("Tile part size exceeds standard maximum value of %d."
				  "Please enable tile part generation to keep tile part size below max",
				  UINT_MAX);
		return false;
	}
	*packet_bytes_written = (uint32_t)byteCount;
	if(markers)
		markers->pushNextPacketLength(*packet_bytes_written);

	// if (max_bytes_available == UINT_MAX)
	//	GRK_INFO("Simulated packet bytes %d for layer %d", *packet_bytes_written,layno);
	// if (finalSimulation && tileProcessor->m_tileIndex == 3)
	//	GRK_INFO("Tile %d : simulated packet length: %d for layer %d",
	//			tileProcessor->m_tileIndex, *packet_bytes_written,layno);

	return true;
}

#ifdef DEBUG_LOSSLESS_T2
auto originalDataBytes = *packet_bytes_written - numHeaderBytes;
auto roundRes = &tilec -> round_trip_resolutions[resno];
size_t bytesRead = 0;
auto srcBuf = std::unique_ptr<SparseBuffer>(new SparseBuffer());
seg_buf_push_back(srcBuf.get(), dest, *packet_bytes_written);

bool ret = true;
bool read_data;
if(!T2Compress::readPacketHeader(p_t2, roundRes, tcp, pi, &read_data, srcBuf.get(), &bytesRead))
{
	ret = false;
}
if(rc)
{
	// compare size of header
	if(numHeaderBytes != bytesRead)
	{
		printf("compressPacket: round trip header bytes %u differs from original %u\n",
			   (uint32_t)l_nb_bytes_read, (uint32_t)numHeaderBytes);
	}
	for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
	{
		auto tileBand = res->tileBand + bandIndex;
		auto roundTripBand = roundRes->tileBand + bandIndex;
		if(!tileBand->precincts)
			continue;
		for(uint64_t pno = 0; pno < tileBand->numPrecincts; ++pno)
		{
			auto prec = tileBand->precincts + pno;
			auto roundTripPrec = roundTripBand->precincts + pno;
			for(uint64_t cblkno = 0;
				cblkno < (uint64_t)prec->cblk_grid_width * prec->cblk_grid_height; ++cblkno)
			{
				auto originalCblk = prec->getCompressedBlockPtr() + cblkno;
				Layer* layer = originalCblk->layers + layno;
				if(!layer->numpasses)
					continue;

				// compare number of passes
				auto roundTripCblk = roundTripPrec->getDecompressedBlockPtr() + cblkno;
				if(roundTripCblk->numPassesInPacket != layer->numpasses)
				{
					printf("compressPacket: round trip layer numpasses %u differs from "
						   "original num passes %u at layer %u, component %u, band %u, "
						   "precinct %u, resolution %u\n",
						   roundTripCblk->numPassesInPacket, layer->numpasses, layno, compno,
						   bandIndex, (uint32_t)precinctIndex, pi->resno);
				}
				// compare number of bit planes
				if(roundTripCblk->numbps != originalCblk->numbps)
				{
					printf("compressPacket: round trip numbps %u differs from original %u\n",
						   roundTripCblk->numbps, originalCblk->numbps);
				}

				// compare number of length bits
				if(roundTripCblk->numlenbits != originalCblk->numlenbits)
				{
					printf("compressPacket: round trip numlenbits %u differs from original %u\n",
						   roundTripCblk->numlenbits, originalCblk->numlenbits);
				}

				// compare inclusion
				if(roundTripCblk->included != originalCblk->included)
				{
					printf("compressPacket: round trip inclusion %u differs from original "
						   "inclusion %u at layer %u, component %u, band %u, precinct %u, "
						   "resolution %u\n",
						   roundTripCblk->included, originalCblk->included, layno, compno,
						   bandIndex, (uint32_t)precinctIndex, pi->resno);
				}

				// compare lengths
				if(roundTripCblk->packet_length_info.size() !=
				   originalCblk->packet_length_info.size())
				{
					printf("compressPacket: round trip length size %u differs from original %u "
						   "at layer %u, component %u, band %u, precinct %u, resolution %u\n",
						   (uint32_t)roundTripCblk->packet_length_info.size(),
						   (uint32_t)originalCblk->packet_length_info.size(), layno, compno,
						   bandIndex, (uint32_t)precinctIndex, pi->resno);
				}
				else
				{
					for(uint32_t i = 0; i < roundTripCblk->packet_length_info.size(); ++i)
					{
						auto roundTrip = roundTripCblk->packet_length_info.operator[](i);
						auto original = originalCblk->packet_length_info.operator[](i);
						if(!(roundTrip == original))
						{
							printf("compressPacket: round trip length size %u differs from "
								   "original %u at layer %u, component %u, band %u, precinct "
								   "%u, resolution %u\n",
								   roundTrip.len, original.len, layno, compno, bandIndex,
								   (uint32_t)precinctIndex, pi->resno);
						}
					}
				}
			}
		}
	}
	/* we should read data for the packet */
	if(read_data)
	{
		bytesRead = 0;
		if(!T2Compress::readPacketData(roundRes, pi, srcBuf.get(), &bytesRead))
		{
			rc = false;
		}
		else
		{
			if(originalDataBytes != bytesRead)
			{
				printf("compressPacket: round trip data bytes %u differs from original %u\n",
					   (uint32_t)l_nb_bytes_read, (uint32_t)originalDataBytes);
			}

			for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
			{
				auto tileBand = res->tileBand + bandIndex;
				auto roundTripBand = roundRes->tileBand + bandIndex;
				if(!tileBand->precincts)
					continue;
				for(uint64_t pno = 0; pno < tileBand->numPrecincts; ++pno)
				{
					auto prec = tileBand->precincts + pno;
					auto roundTripPrec = roundTripBand->precincts + pno;
					for(uint32_t cblkno = 0;
						cblkno < (uint64_t)prec->cblk_grid_width * prec->cblk_grid_height; ++cblkno)
					{
						auto originalCblk = prec->getCompressedBlockPtr() + cblkno;
						Layer* layer = originalCblk->layers + layno;
						if(!layer->numpasses)
							continue;

						// compare cumulative length
						uint32_t originalCumulativeLayerLength = 0;
						for(uint32_t i = 0; i <= layno; ++i)
						{
							auto lay = originalCblk->layers + i;
							if(lay->numpasses)
								originalCumulativeLayerLength += lay->len;
						}
						auto roundTripCblk = roundTripPrec->getDecompressedBlockPtr() + cblkno;
						uint16_t roundTripTotalSegLen =
							min_buf_vec_get_len(&roundTripCblk->seg_buffers);
						if(roundTripTotalSegLen != originalCumulativeLayerLength)
						{
							printf("compressPacket: layer %u: round trip segment length %u "
								   "differs from original %u\n",
								   layno, roundTripTotalSegLen, originalCumulativeLayerLength);
						}

						// compare individual data points
						if(roundTripCblk->numSegments && roundTripTotalSegLen)
						{
							uint8_t* roundTripData = nullptr;
							bool needs_delete = false;
							/* if there is only one segment, then it is already contiguous, so
							 * no need to make a copy*/
							if(roundTripTotalSegLen == 1 && roundTripCblk->seg_buffers.get(0))
							{
								roundTripData = ((grkBufferU8*)(roundTripCblk->seg_buffers.get(0)))
													->getBuffer();
							}
							else
							{
								needs_delete = true;
								roundTripData = new uint8_t[roundTripTotalSegLen];
								min_buf_vec_copyToContiguousBuffer(&roundTripCblk->seg_buffers,
																   roundTripData);
							}
							for(uint32_t i = 0; i < originalCumulativeLayerLength; ++i)
							{
								if(roundTripData[i] != originalCblk->data[i])
								{
									printf("compressPacket: layer %u: round trip data %x "
										   "differs from original %x\n",
										   layno, roundTripData[i], originalCblk->data[i]);
								}
							}
							if(needs_delete)
								delete[] roundTripData;
						}
					}
				}
			}
		}
	}
}
else
{
	GRK_ERROR("compressPacket: decompress packet failed");
}
return ret;
#endif

} // namespace grk
