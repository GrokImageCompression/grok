/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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
								 uint32_t* p_data_written, bool first_poc_tile_part,
								 uint32_t tp_pos, uint32_t pino)
{
	auto cp = tileProcessor->m_cp;
	auto image = tileProcessor->headerImage;
	auto tilePtr = tileProcessor->tile;
	auto tcp = &cp->tcps[tile_no];
	PacketManager packetManager(true, image, cp, tile_no, FINAL_PASS, tileProcessor);
	packetManager.enableTilePartGeneration(pino, first_poc_tile_part, tp_pos);
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
			uint32_t nb_bytes = 0;
			if(!compressPacket(tcp, current_pi, stream, &nb_bytes))
			{
				return false;
			}
			*p_data_written += nb_bytes;
			tilePtr->numProcessedPackets++;
		}
	}

	return true;
}
bool T2Compress::compressPacketsSimulate(uint16_t tile_no, uint16_t max_layers,
										 uint32_t* lengthOfAllPackets, uint32_t max_len,
										 uint32_t tp_pos, PacketLengthMarkers* markers)
{
	assert(lengthOfAllPackets);
	auto cp = tileProcessor->m_cp;
	auto image = tileProcessor->headerImage;
	auto tcp = cp->tcps + tile_no;
	uint32_t pocno = (cp->rsiz == GRK_PROFILE_CINEMA_4K) ? 2 : 1;
	uint32_t max_comp = cp->m_coding_params.m_enc.m_max_comp_size > 0 ? image->numcomps : 1;
	PacketManager packetManager(true, image, cp, tile_no, THRESH_CALC, tileProcessor);
	*lengthOfAllPackets = 0;

	tileProcessor->getPacketTracker()->clear();
#ifdef DEBUG_ENCODE_PACKETS
	GRK_INFO("simulate compress packets for layers below layno %u", max_layers);
#endif
	// todo: assume CPRL progression, why ???
	for(uint32_t compno = 0; compno < max_comp; ++compno)
	{
		uint64_t comp_len = 0;
		for(uint32_t poc = 0; poc < pocno; ++poc)
		{
			auto current_pi = packetManager.getPacketIter(poc);
			// todo: 1. why is tile part number set to component number ?
			// todo: 2. why is tile part generation initialized for each progression order change ?
			packetManager.enableTilePartGeneration(poc, (compno == 0), tp_pos);

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

					if(!compressPacketSimulate(tcp, current_pi, &bytesInPacket, max_len, markers))
					{
						return false;
					}

					comp_len += bytesInPacket;
					max_len -= bytesInPacket;
					*lengthOfAllPackets += bytesInPacket;
				}
			}
			if(cp->m_coding_params.m_enc.m_max_comp_size)
			{
				if(comp_len > cp->m_coding_params.m_enc.m_max_comp_size)
				{
					return false;
				}
			}
		}
	}
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
	auto tilec = &tile->comps[compno];
	auto res = &tilec->tileCompResolution[resno];
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
		/* numProcessedPackets is uint32_t modulo 65536, in big endian format */
		uint16_t numProcessedPackets = (uint16_t)(tile->numProcessedPackets % 0x10000);
		if(!stream->writeByte((uint8_t)(numProcessedPackets >> 8)))
			return false;
		if(!stream->writeByte((uint8_t)(numProcessedPackets & 0xff)))
			return false;
	}
	// initialize precinct and code blocks if this is the first layer
	if(!layno)
	{
		for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
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
			{
				band++;
				continue;
			}
			if(prc->getInclTree())
				prc->getInclTree()->reset();
			if(prc->getImsbTree())
				prc->getImsbTree()->reset();
			for(uint64_t cblkno = 0; cblkno < nb_blocks; ++cblkno)
			{
				auto cblk = prc->getCompressedBlockPtr(cblkno);
				cblk->numPassesInPacket = 0;
				assert(band->numbps >= cblk->numbps);
				if(band->numbps < cblk->numbps)
				{
					GRK_WARN("Code block %u bps greater than band bps. Skipping.", cblkno);
				}
				else
				{
					prc->getImsbTree()->setvalue(cblkno, band->numbps - cblk->numbps);
				}
			}
		}
	}
	std::unique_ptr<BitIO> bio(new BitIO(stream, true));
	// Empty header bit. Grok always sets this to 1,
	// even though there is also an option to set it to zero.
	if(!bio->write(1, 1))
		return false;
	/* Writing Packet header */
	for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
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
			{
				prc->getInclTree()->setvalue(cblkno, layno);
			}
		}
		for(uint64_t cblkno = 0; cblkno < nb_blocks; cblkno++)
		{
			auto cblk = prc->getCompressedBlockPtr(cblkno);
			auto layer = cblk->layers + layno;
			uint32_t increment = 0;
			uint32_t nump = 0;
			uint32_t len = 0;

			/* cblk inclusion bits */
			if(!cblk->numPassesInPacket)
			{
				bool rc = prc->getInclTree()->compress(bio.get(), cblkno, layno + 1);
				assert(rc);
				if(!rc)
					return false;
#ifdef DEBUG_LOSSLESS_T2
				cblk->included = layno;
#endif
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
				bool rc = prc->getImsbTree()->compress(bio.get(), cblkno,
						prc->getImsbTree()->getUninitializedValue());
				assert(rc);
				if(!rc)
					return false;
			}
			/* number of coding passes included */
			bio->putnumpasses(layer->numpasses);
			uint32_t nb_passes = cblk->numPassesInPacket + layer->numpasses;
			auto pass = cblk->passes + cblk->numPassesInPacket;

			/* computation of the increase of the length indicator and insertion in the header */
			for(uint32_t passno = cblk->numPassesInPacket; passno < nb_passes; ++passno)
			{
				++nump;
				len += pass->len;

				if(pass->term || passno == nb_passes - 1)
				{
					increment = (uint32_t)std::max<int32_t>(
						(int32_t)increment,
						floorlog2<int32_t>(len) + 1 -
							((int32_t)cblk->numlenbits + floorlog2<int32_t>(nump)));
					len = 0;
					nump = 0;
				}
				++pass;
			}
			bio->putcommacode((int32_t)increment);
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
						len, cblk->numlenbits + (uint32_t)floorlog2<int32_t>((int32_t)nump)));
#endif
					if(!bio->write(len, cblk->numlenbits + (uint32_t)floorlog2<int32_t>(nump)))
						return false;
					len = 0;
					nump = 0;
				}
				++pass;
			}
		}
	}
	if(!bio->flush())
	{
		GRK_ERROR("compressPacket: Bit IO flush failed while compressing packet");
		return false;
	}
	// EPH marker
	if(tcp->csty & J2K_CP_CSTY_EPH)
	{
		if(!stream->writeByte(J2K_MS_EPH >> 8))
			return false;
		if(!stream->writeByte(J2K_MS_EPH & 0xff))
			return false;
	}
	/* Writing the packet body */
	for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
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

			if(cblk_layer->len)
			{
				if(!stream->writeBytes(cblk_layer->data, cblk_layer->len))
					return false;
			}
			cblk->numPassesInPacket += cblk_layer->numpasses;
		}
	}
	*packet_bytes_written += (uint32_t)(stream->tell() - stream_start);

#ifdef DEBUG_LOSSLESS_T2
	auto originalDataBytes = *packet_bytes_written - numHeaderBytes;
	auto roundRes = &tilec->round_trip_resolutions[resno];
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
						printf(
							"compressPacket: round trip numlenbits %u differs from original %u\n",
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
							cblkno < (uint64_t)prec->cblk_grid_width * prec->cblk_grid_height;
							++cblkno)
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
									roundTripData =
										((grkBufferU8*)(roundTripCblk->seg_buffers.get(0)))
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
	return true;
}
bool T2Compress::compressPacketSimulate(TileCodingParams* tcp, PacketIter* pi,
										uint32_t* packet_bytes_written,
										uint32_t max_bytes_available, PacketLengthMarkers* markers)
{
	uint32_t compno = pi->compno;
	uint32_t resno = pi->resno;
	uint64_t precinctIndex = pi->precinctIndex;
	uint16_t layno = pi->layno;
	uint64_t nb_blocks;
	auto tile = tileProcessor->tile;
	auto tilec = tile->comps + compno;
	auto res = tilec->tileCompResolution + resno;

	if(compno >= tile->numcomps)
	{
		GRK_ERROR("compress packet simulate: component number %d must be less than total number "
				  "of components %d",
				  compno, tile->numcomps);
		return false;
	}
	*packet_bytes_written = 0;
	if(tileProcessor->getPacketTracker()->is_packet_encoded(compno, resno, precinctIndex, layno))
		return true;
	tileProcessor->getPacketTracker()->packet_encoded(compno, resno, precinctIndex, layno);

#ifdef DEBUG_ENCODE_PACKETS
	GRK_INFO("simulate compress packet compono=%u, resno=%u, precinctIndex=%u, layno=%u", compno,
			 resno, precinctIndex, layno);
#endif
	if(tcp->csty & J2K_CP_CSTY_SOP)
	{
		max_bytes_available -= 6;
		*packet_bytes_written += 6;
	}
	if(!layno)
	{
		for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
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
			if(prc->getInclTree())
				prc->getInclTree()->reset();
			if(prc->getImsbTree())
				prc->getImsbTree()->reset();
			nb_blocks = prc->getNumCblks();
			for(uint64_t cblkno = 0; cblkno < nb_blocks; ++cblkno)
			{
				auto cblk = prc->getCompressedBlockPtr(cblkno);
				cblk->numPassesInPacket = 0;
				if(band->numbps < cblk->numbps)
				{
					GRK_WARN("Code block %u bps greater than band bps. Skipping.", cblkno);
				}
				else
				{
					prc->getImsbTree()->setvalue(cblkno, band->numbps - cblk->numbps);
				}
			}
		}
	}
	std::unique_ptr<BitIO> bio(new BitIO(0, max_bytes_available, true));
	bio->simulateOutput(true);
	/* Empty header bit */
	if(!bio->write(1, 1))
		return false;
	/* Writing Packet header */
	for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
	{
		auto band = res->tileBand + bandIndex;
		auto prc = band->precincts[precinctIndex];

		nb_blocks = prc->getNumCblks();
		for(uint64_t cblkno = 0; cblkno < nb_blocks; ++cblkno)
		{
			auto cblk = prc->getCompressedBlockPtr(cblkno);
			auto layer = cblk->layers + layno;
			if(!cblk->numPassesInPacket && layer->numpasses)
			{
				prc->getInclTree()->setvalue(cblkno, layno);
			}
		}
		for(uint64_t cblkno = 0; cblkno < nb_blocks; cblkno++)
		{
			auto cblk = prc->getCompressedBlockPtr(cblkno);
			auto layer = cblk->layers + layno;
			uint32_t increment = 0;
			uint32_t nump = 0;
			uint32_t len = 0, passno;
			uint32_t nb_passes;

			/* cblk inclusion bits */
			if(!cblk->numPassesInPacket)
			{
				if(!prc->getInclTree()->compress(bio.get(), cblkno, layno + 1))
					return false;
			}
			else
			{
				if(!bio->write(layer->numpasses != 0, 1))
					return false;
			}

			/* if cblk not included, go to the next cblk  */
			if(!layer->numpasses)
				continue;

			/* if first instance of cblk --> zero bit-planes information */
			if(!cblk->numPassesInPacket)
			{
				cblk->numlenbits = 3;
				if(!prc->getImsbTree()->compress(bio.get(), cblkno,
						prc->getImsbTree()->getUninitializedValue()))
					return false;
			}

			/* number of coding passes included */
			bio->putnumpasses(layer->numpasses);
			nb_passes = cblk->numPassesInPacket + layer->numpasses;
			/* computation of the increase of the length indicator and insertion in the header */
			for(passno = cblk->numPassesInPacket; passno < nb_passes; ++passno)
			{
				auto pass = cblk->passes + cblk->numPassesInPacket + passno;
				++nump;
				len += pass->len;

				if(pass->term || passno == (cblk->numPassesInPacket + layer->numpasses) - 1)
				{
					increment = (uint32_t)std::max<int32_t>(
						(int32_t)increment,
						floorlog2<int32_t>(len) + 1 -
							((int32_t)cblk->numlenbits + floorlog2<int32_t>(nump)));
					len = 0;
					nump = 0;
				}
			}
			bio->putcommacode((int32_t)increment);
			/* computation of the new Length indicator */
			cblk->numlenbits += increment;
			/* insertion of the codeword segment length */
			for(passno = cblk->numPassesInPacket; passno < nb_passes; ++passno)
			{
				auto pass = cblk->passes + cblk->numPassesInPacket + passno;
				nump++;
				len += pass->len;
				if(pass->term || passno == (cblk->numPassesInPacket + layer->numpasses) - 1)
				{
					if(!bio->write(len, cblk->numlenbits + (uint32_t)floorlog2<int32_t>(nump)))
						return false;
					len = 0;
					nump = 0;
				}
			}
		}
	}
	if(!bio->flush())
		return false;
	*packet_bytes_written += (uint32_t)bio->numbytes();
	max_bytes_available -= (uint32_t)bio->numbytes();
	if(tcp->csty & J2K_CP_CSTY_EPH)
	{
		max_bytes_available -= 2;
		*packet_bytes_written += 2;
	}
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
			*packet_bytes_written += layer->len;
			max_bytes_available -= layer->len;
		}
	}
	if(markers)
		markers->writeNext(*packet_bytes_written);

	return true;
}

} // namespace grk
