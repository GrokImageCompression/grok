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
namespace grk
{
TileProcessor::TileProcessor(CodeStream* codeStream, IBufferedStream* stream, bool isCompressor,
							 bool isWholeTileDecompress)
	: m_tileIndex(0), m_first_poc_tile_part(true), m_tilePartIndex(0), tilePartDataLength(0),
	  numTilePartsTotal(0), pino(0), tile(nullptr), headerImage(codeStream->getHeaderImage()),
	  current_plugin_tile(codeStream->getCurrentPluginTile()),
	  wholeTileDecompress(isWholeTileDecompress), m_cp(codeStream->getCodingParams()),
	  packetLengthCache(PacketLengthCache(m_cp)), m_stream(stream), m_corrupt_packet(false),
	  newTilePartProgressionPosition(0), m_tcp(nullptr), truncated(false), m_image(nullptr),
	  m_isCompressor(isCompressor), preCalculatedTileLen(0)
{
	tile = new Tile();
	tile->comps = new TileComponent[headerImage->numcomps];
	tile->numcomps = headerImage->numcomps;

	newTilePartProgressionPosition = m_cp->m_coding_params.m_enc.newTilePartProgressionPosition;
}
TileProcessor::~TileProcessor()
{
	delete tile;
	if(m_image)
		grk_object_unref(&m_image->obj);
}
IBufferedStream* TileProcessor::getStream(void)
{
	return m_stream;
}
uint32_t TileProcessor::getPreCalculatedTileLen(void)
{
	return preCalculatedTileLen;
}
bool TileProcessor::canPreCalculateTileLen(void)
{
	return !m_cp->m_coding_params.m_enc.m_enableTilePartGeneration && (m_cp->tcps + m_tileIndex)->getNumProgressions() == 1;
}
void TileProcessor::generateImage(GrkImage* src_image, Tile* src_tile)
{
	if(m_image)
		grk_object_unref(&m_image->obj);
	m_image = src_image->duplicate(src_tile);
}
GrkImage* TileProcessor::getImage(void)
{
	return m_image;
}
void TileProcessor::setCorruptPacket(void)
{
	m_corrupt_packet = true;
}
PacketTracker* TileProcessor::getPacketTracker(void)
{
	return &m_packetTracker;
}
TileCodingParams* TileProcessor::getTileCodingParams(void)
{
	return m_cp->tcps + m_tileIndex;
}
uint8_t TileProcessor::getMaxNumDecompressResolutions(void)
{
	uint8_t rc = 0;
	auto tcp = m_cp->tcps + m_tileIndex;
	for(uint16_t compno = 0; compno < tile->numcomps; ++compno)
	{
		auto tccp = tcp->tccps + compno;
		auto numresolutions = tccp->numresolutions;
		uint8_t resToDecomp;
		if(numresolutions < m_cp->m_coding_params.m_dec.m_reduce)
			resToDecomp = 1;
		else
			resToDecomp = (uint8_t)(numresolutions - m_cp->m_coding_params.m_dec.m_reduce);
		rc = std::max<uint8_t>(rc, resToDecomp);
	}
	return rc;
}
bool TileProcessor::init(void)
{
	uint32_t state = grk_plugin_get_debug_state();
	auto tcp = &(m_cp->tcps[m_tileIndex]);

	if(tcp->m_compressedTileData)
		tcp->m_compressedTileData->rewind();

	// generate tile bounds from tile grid coordinates
	uint32_t tile_x = m_tileIndex % m_cp->t_grid_width;
	uint32_t tile_y = m_tileIndex / m_cp->t_grid_width;
	*((grkRectU32*)tile) = m_cp->getTileBounds(headerImage, tile_x, tile_y);

	if(tcp->tccps->numresolutions == 0)
	{
		GRK_ERROR("tiles require at least one resolution");
		return false;
	}

	for(uint16_t compno = 0; compno < tile->numcomps; ++compno)
	{
		auto imageComp = headerImage->comps + compno;
		/*fprintf(stderr, "compno = %u/%u\n", compno, tile->numcomps);*/
		if(imageComp->dx == 0 || imageComp->dy == 0)
			return false;
		auto tilec = tile->comps + compno;
		grkRectU32 unreducedTileComp = grkRectU32(
			ceildiv<uint32_t>(tile->x0, imageComp->dx), ceildiv<uint32_t>(tile->y0, imageComp->dy),
			ceildiv<uint32_t>(tile->x1, imageComp->dx), ceildiv<uint32_t>(tile->y1, imageComp->dy));
		if(!tilec->init(m_isCompressor, wholeTileDecompress, unreducedTileComp, imageComp->prec,
						m_cp, tcp, tcp->tccps + compno, current_plugin_tile))
		{
			return false;
		}
	}

	// decompressor plugin debug sanity check on tile struct
	if(!m_isCompressor)
	{
		if(state & GRK_PLUGIN_STATE_DEBUG)
		{
			if(!tile_equals(current_plugin_tile, tile))
				GRK_WARN("plugin tile differs from grok tile", nullptr);
		}
	}
	tile->numProcessedPackets = 0;

	if(m_isCompressor)
	{
		uint64_t max_precincts = 0;
		for(uint16_t compno = 0; compno < headerImage->numcomps; ++compno)
		{
			auto tilec = tile->comps + compno;
			for(uint32_t resno = 0; resno < tilec->numresolutions; ++resno)
			{
				auto res = tilec->tileCompResolution + resno;
				max_precincts = max<uint64_t>(max_precincts, (uint64_t)res->precinctGridWidth *
																 res->precinctGridHeight);
			}
		}
		m_packetTracker.init(tile->numcomps, tile->comps->numresolutions, max_precincts,
							 tcp->numlayers);
	}

	return true;
}
bool TileProcessor::allocWindowBuffers(const GrkImage* outputImage)
{
	for(uint16_t compno = 0; compno < tile->numcomps; ++compno)
	{
		auto imageComp = headerImage->comps + compno;
		if(imageComp->dx == 0 || imageComp->dy == 0)
			return false;
		grkRectU32 rct;
		if(m_isCompressor)
		{
			rct = *((grkRectU32*)tile);
		}
		else
		{
			rct = grkRectU32(outputImage->x0, outputImage->y0, outputImage->x1, outputImage->y1);
			unreducedTileWindow = rct.intersection(tile);
		}
		grkRectU32 unreducedTileCompOrImageCompWindow =
			rct.rectceildiv(imageComp->dx, imageComp->dy);
		if(!(tile->comps + compno)->allocWindowBuffer(unreducedTileCompOrImageCompWindow))
			return false;
	}

	return true;
}

grkRectU32 TileProcessor::getUnreducedTileWindow(void)
{
	return unreducedTileWindow;
}

void TileProcessor::deallocBuffers()
{
	for(uint16_t compno = 0; compno < (int64_t)tile->numcomps; ++compno)
	{
		auto tile_comp = tile->comps + compno;
		tile_comp->deallocBuffers();
	}
}
bool TileProcessor::doCompress(void)
{
	uint32_t state = grk_plugin_get_debug_state();
	if(state & GRK_PLUGIN_STATE_DEBUG)
		set_context_stream(this);

	m_tcp = &m_cp->tcps[m_tileIndex];

	// When debugging the compressor, we do all of T1 up to and including DWT
	// in the plugin, and pass this in as image data.
	// This way, both Grok and plugin start with same inputs for
	// context formation and MQ coding.
	bool debugEncode = state & GRK_PLUGIN_STATE_DEBUG;
	bool debugMCT = (state & GRK_PLUGIN_STATE_MCT_ONLY) ? true : false;

	if(!current_plugin_tile || debugEncode)
	{
		if(!debugEncode)
		{
			if(!dcLevelShiftCompress())
				return false;
			if(!mct_encode())
				return false;
		}
		if(!debugEncode || debugMCT)
		{
			if(!dwt_encode())
				return false;
		}
		t1_encode();
	}
	// 1. create PLT marker if required
	packetLengthCache.deleteMarkers();
	if(m_cp->m_coding_params.m_enc.writePLT)
		packetLengthCache.createMarkers(m_stream);
	// 2. rate control
	uint32_t allPacketBytes = 0;
	if(!rateAllocate(&allPacketBytes))
		return false;
	m_packetTracker.clear();

	if(canPreCalculateTileLen())
	{
		// SOT marker
		preCalculatedTileLen = sot_marker_segment_len;
		// POC marker
		if(canWritePocMarker())
			preCalculatedTileLen +=
				CodeStreamCompress::getPocSize(tile->numcomps, m_tcp->getNumProgressions());
		// calculate PLT marker length
		if(packetLengthCache.getMarkers())
			preCalculatedTileLen += packetLengthCache.getMarkers()->write(true);
		// calculate SOD marker length
		preCalculatedTileLen += 2;
		// calculate packets length
		preCalculatedTileLen += allPacketBytes;
	}
	return true;
}
bool TileProcessor::canWritePocMarker(void)
{
	bool firstTilePart = (m_tilePartIndex == 0);

	// note: DCP standard does not allow POC marker
	return (m_cp->tcps[m_tileIndex].numpocs > 0) && firstTilePart && !GRK_IS_CINEMA(m_cp->rsiz);
}
bool TileProcessor::writeTilePartT2(uint32_t* tileBytesWritten)
{
	// write entire PLT marker in first tile part header
	if(m_tilePartIndex == 0 && packetLengthCache.getMarkers())
		*tileBytesWritten += packetLengthCache.getMarkers()->write(false);

	// write SOD
	if(!m_stream->writeShort(J2K_MS_SOD))
		return false;
	*tileBytesWritten += 2;

	// write tile packets
	return encodeT2(tileBytesWritten);
}
/** Returns whether a tile component should be fully decompressed,
 * taking into account win_* members.
 *
 * @param compno Component number
 * @return true if the tile component should be fully decompressed
 */
bool TileProcessor::isWholeTileDecompress(uint32_t compno)
{
	auto tilec = tile->comps + compno;
	/* Compute the intersection of the area of interest, expressed in tile component coordinates, */
	/* with the tile bounds */
	auto dims = tilec->getBuffer()->bounds().intersection(tilec);

	uint32_t shift = (uint32_t)(tilec->numresolutions - tilec->resolutions_to_decompress);
	/* Tolerate small margin within the reduced resolution factor to consider if */
	/* the whole tile path must be taken */
	return (dims.is_valid() &&
			(shift >= 32 ||
			 (((dims.x0 - tilec->x0) >> shift) == 0 && ((dims.y0 - tilec->y0) >> shift) == 0 &&
			  ((tilec->x1 - dims.x1) >> shift) == 0 && ((tilec->y1 - dims.y1) >> shift) == 0)));
}

bool TileProcessor::decompressT2(SparseBuffer* srcBuf)
{
	m_tcp = m_cp->tcps + m_tileIndex;

	// optimization for regions that are close to largest decompressed resolution
	// (currently breaks tests, so disabled)
	for(uint16_t compno = 0; compno < headerImage->numcomps; compno++)
	{
		if(!isWholeTileDecompress(compno))
		{
			wholeTileDecompress = false;
			break;
		}
	}

	bool doT2 = !current_plugin_tile || (current_plugin_tile->decompress_flags & GRK_DECODE_T2);

	if(doT2)
	{
		auto t2 = new T2Decompress(this);
		bool rc = t2->decompressPackets(m_tileIndex, srcBuf, &truncated);
		delete t2;
		if(!rc)
			return false;
		// synch plugin with T2 data
		decompress_synch_plugin_with_host(this);
	}

	return true;
}
bool TileProcessor::decompressT1(void)
{
	bool doT1 = !current_plugin_tile || (current_plugin_tile->decompress_flags & GRK_DECODE_T1);
	bool doPostT1 =
		!current_plugin_tile || (current_plugin_tile->decompress_flags & GRK_DECODE_POST_T1);
	if(doT1)
	{
		for(uint16_t compno = 0; compno < tile->numcomps; ++compno)
		{
			auto tilec = tile->comps + compno;
			auto tccp = m_tcp->tccps + compno;

			if(!wholeTileDecompress)
			{
				try
				{
					tilec->allocSparseCanvas(tilec->resolutions_decompressed + 1U, truncated);
				}
				catch(runtime_error& ex)
				{
					GRK_UNUSED(ex);
					continue;
				}
			}
			std::vector<DecompressBlockExec*> blocks;
			auto scheduler = std::unique_ptr<T1DecompressScheduler>(new T1DecompressScheduler());
			if(!scheduler->prepareScheduleDecompress(tilec, tccp, &blocks))
				return false;
			if(!scheduler->scheduleDecompress(m_tcp, (uint16_t)tccp->cblkw, (uint16_t)tccp->cblkh,
											  &blocks))
				return false;

			if(doPostT1)
			{
				WaveletReverse w;
				if(!w.decompress(this, tilec, compno, tilec->getBuffer()->unreducedBounds(),
								 tilec->resolutions_decompressed + 1U, tccp->qmfbid))
					return false;
			}
		}
	}
	if(doPostT1)
	{
		if(!mctDecompress())
			return false;
		if(!dcLevelShiftDecompress())
			return false;
	}
	return true;
}
bool TileProcessor::decompressT2T1(TileCodingParams* tcp, GrkImage* outputImage, bool multiTile,
								   bool doPost)
{
	if(!allocWindowBuffers(outputImage))
		return false;
	if(!decompressT2(tcp->m_compressedTileData) || m_corrupt_packet)
	{
		GRK_WARN("Tile %d was not decompressed", m_tileIndex + 1);
		return true;
	}
	if(!decompressT1())
	{
		return false;
	}
	if(doPost)
	{
		if(multiTile)
			generateImage(outputImage, tile);
		else
			outputImage->transferDataFrom(tile);
		deallocBuffers();
	}

	return true;
}

void TileProcessor::ingestImage()
{
	for(uint16_t i = 0; i < headerImage->numcomps; ++i)
	{
		auto tilec = tile->comps + i;
		auto img_comp = headerImage->comps + i;

		uint32_t offset_x = ceildiv<uint32_t>(headerImage->x0, img_comp->dx);
		uint32_t offset_y = ceildiv<uint32_t>(headerImage->y0, img_comp->dy);
		uint64_t image_offset =
			(tilec->x0 - offset_x) + (uint64_t)(tilec->y0 - offset_y) * img_comp->stride;
		auto src = img_comp->data + image_offset;
		auto dest = tilec->getBuffer()->getHighestBufferResWindowREL()->getBuffer();

		for(uint32_t j = 0; j < tilec->height(); ++j)
		{
			memcpy(dest, src, tilec->width() * sizeof(int32_t));
			src += img_comp->stride;
			dest += tilec->getBuffer()->getHighestBufferResWindowREL()->stride;
		}
	}
}
bool TileProcessor::needsMctDecompress(uint32_t compno)
{
	if(!m_tcp->mct)
		return false;
	if(tile->numcomps < 3)
	{
		GRK_WARN("Number of components (%u) is inconsistent with a MCT. Skip the MCT step.",
				 tile->numcomps);
		return false;
	}
	/* testcase 1336.pdf.asan.47.376 */
	uint64_t samples = tile->comps->getBuffer()->stridedArea();
	if(tile->comps[1].getBuffer()->stridedArea() != samples ||
	   tile->comps[2].getBuffer()->stridedArea() != samples)
	{
		GRK_WARN("Not all tiles components have the same dimension: skipping MCT.");
		return false;
	}
	if(compno > 2)
		return false;
	if(m_tcp->mct == 2 && !m_tcp->m_mct_decoding_matrix)
		return false;

	return true;
}
bool TileProcessor::mctDecompress()
{
	if(!needsMctDecompress(0))
		return true;
	if(m_tcp->mct == 2)
	{
		auto data = new uint8_t*[tile->numcomps];
		for(uint16_t i = 0; i < tile->numcomps; ++i)
		{
			auto tile_comp = tile->comps + i;
			data[i] = (uint8_t*)tile_comp->getBuffer()->getHighestBufferResWindowREL()->getBuffer();
		}
		uint64_t samples = tile->comps->getBuffer()->stridedArea();
		bool rc = mct::decompress_custom((uint8_t*)m_tcp->m_mct_decoding_matrix, samples, data,
										 tile->numcomps, headerImage->comps->sgnd);
		delete[] data;
		return rc;
	}
	else
	{
		if(m_tcp->tccps->qmfbid == 1)
		{
			mct::decompress_rev(tile, headerImage, m_tcp->tccps);
		}
		else
		{
			mct::decompress_irrev(tile, headerImage, m_tcp->tccps);
		}
	}

	return true;
}

bool TileProcessor::dcLevelShiftDecompress()
{
	for(uint16_t compno = 0; compno < tile->numcomps; compno++)
	{
		if(!needsMctDecompress(compno) || m_tcp->mct == 2)
		{
			auto tccp = m_tcp->tccps + compno;
			if(tccp->qmfbid == 1)
				mct::decompress_dc_shift_rev(tile, headerImage, m_tcp->tccps, compno);
			else
				mct::decompress_dc_shift_irrev(tile, headerImage, m_tcp->tccps, compno);
		}
	}
	return true;
}

bool TileProcessor::dcLevelShiftCompress()
{
	for(uint16_t compno = 0; compno < tile->numcomps; compno++)
	{
		auto tile_comp = tile->comps + compno;
		auto tccp = m_tcp->tccps + compno;
		auto current_ptr = tile_comp->getBuffer()->getHighestBufferResWindowREL()->getBuffer();
		uint64_t samples = tile_comp->getBuffer()->stridedArea();
		if(!m_tcp->mct && tccp->qmfbid == 0)
		{
			for(uint64_t i = 0; i < samples; ++i)
			{
				*current_ptr = (*current_ptr - tccp->m_dc_level_shift) * 2048;
				++current_ptr;
			}
		}
		else
		{
			if(tccp->m_dc_level_shift == 0)
				continue;
			for(uint64_t i = 0; i < samples; ++i)
			{
				*current_ptr -= tccp->m_dc_level_shift;
				++current_ptr;
			}
		}
	}

	return true;
}
bool TileProcessor::mct_encode()
{
	uint64_t samples = tile->comps->getBuffer()->stridedArea();

	if(!m_tcp->mct)
		return true;
	if(m_tcp->mct == 2)
	{
		if(!m_tcp->m_mct_coding_matrix)
			return true;
		auto data = new uint8_t*[tile->numcomps];
		for(uint32_t i = 0; i < tile->numcomps; ++i)
		{
			auto tile_comp = tile->comps + i;
			data[i] = (uint8_t*)tile_comp->getBuffer()->getHighestBufferResWindowREL()->getBuffer();
		}
		bool rc = mct::compress_custom((uint8_t*)m_tcp->m_mct_coding_matrix, samples, data,
									   tile->numcomps, headerImage->comps->sgnd);
		delete[] data;
		return rc;
	}
	else if(m_tcp->tccps->qmfbid == 0)
	{
		mct::compress_irrev(tile->comps[0].getBuffer()->getHighestBufferResWindowREL()->getBuffer(),
							tile->comps[1].getBuffer()->getHighestBufferResWindowREL()->getBuffer(),
							tile->comps[2].getBuffer()->getHighestBufferResWindowREL()->getBuffer(),
							samples);
	}
	else
	{
		mct::compress_rev(tile->comps[0].getBuffer()->getHighestBufferResWindowREL()->getBuffer(),
						  tile->comps[1].getBuffer()->getHighestBufferResWindowREL()->getBuffer(),
						  tile->comps[2].getBuffer()->getHighestBufferResWindowREL()->getBuffer(),
						  samples);
	}

	return true;
}
bool TileProcessor::dwt_encode()
{
	bool rc = true;
	for(uint16_t compno = 0; compno < tile->numcomps; ++compno)
	{
		auto tile_comp = tile->comps + compno;
		auto tccp = m_tcp->tccps + compno;
		WaveletFwdImpl w;
		if(!w.compress(tile_comp, tccp->qmfbid))
		{
			rc = false;
			continue;
		}
	}
	return rc;
}
void TileProcessor::t1_encode()
{
	const double* mct_norms;
	uint16_t mct_numcomps = 0U;
	auto tcp = m_tcp;

	if(tcp->mct == 1)
	{
		mct_numcomps = 3U;
		/* irreversible compressing */
		if(tcp->tccps->qmfbid == 0)
			mct_norms = mct::get_norms_irrev();
		else
			mct_norms = mct::get_norms_rev();
	}
	else
	{
		mct_numcomps = headerImage->numcomps;
		mct_norms = (const double*)(tcp->mct_norms);
	}

	auto scheduler =
		std::unique_ptr<T1CompressScheduler>(new T1CompressScheduler(tile, needsRateControl()));
	scheduler->scheduleCompress(tcp, mct_norms, mct_numcomps);
}
bool TileProcessor::encodeT2(uint32_t* tileBytesWritten)
{
	auto l_t2 = new T2Compress(this);
#ifdef DEBUG_LOSSLESS_T2
	for(uint32_t compno = 0; compno < p_image->m_numcomps; ++compno)
	{
		TileComponent* tilec = &tilePtr->comps[compno];
		tilec->round_trip_resolutions = new Resolution[tilec->numresolutions];
		for(uint32_t resno = 0; resno < tilec->numresolutions; ++resno)
		{
			auto res = tilec->tileCompResolution + resno;
			auto roundRes = tilec->round_trip_resolutions + resno;
			roundRes->x0 = res->x0;
			roundRes->y0 = res->y0;
			roundRes->x1 = res->x1;
			roundRes->y1 = res->y1;
			roundRes->numTileBandWindows = res->numTileBandWindows;
			for(uint32_t bandIndex = 0; bandIndex < roundRes->numTileBandWindows; ++bandIndex)
			{
				roundRes->tileBand[bandIndex] = res->tileBand[bandIndex];
				roundRes->tileBand[bandIndex].x0 = res->tileBand[bandIndex].x0;
				roundRes->tileBand[bandIndex].y0 = res->tileBand[bandIndex].y0;
				roundRes->tileBand[bandIndex].x1 = res->tileBand[bandIndex].x1;
				roundRes->tileBand[bandIndex].y1 = res->tileBand[bandIndex].y1;
			}

			// allocate
			for(uint32_t bandIndex = 0; bandIndex < roundRes->numTileBandWindows; ++bandIndex)
			{
				auto tileBand = res->tileBand + bandIndex;
				auto decodeBand = roundRes->tileBand + bandIndex;
				if(!tileBand->numPrecincts)
					continue;
				decodeBand->precincts = new Precinct[tileBand->numPrecincts];
				decodeBand->precincts_data_size =
					(uint32_t)(tileBand->numPrecincts * sizeof(Precinct));
				for(uint64_t precinctIndex = 0; precinctIndex < tileBand->numPrecincts;
					++precinctIndex)
				{
					auto prec = tileBand->precincts + precinctIndex;
					auto decodePrec = decodeBand->precincts + precinctIndex;
					decodePrec->cblk_grid_width = prec->cblk_grid_width;
					decodePrec->cblk_grid_height = prec->cblk_grid_height;
					if(prec->getCompressedBlockPtr() && prec->cblk_grid_width &&
					   prec->cblk_grid_height)
					{
						decodePrec->initTagTrees();
						decodePrec->getDecompressedBlockPtr() =
							new DecompressCodeblock[(uint64_t)decodePrec->cblk_grid_width *
													decodePrec->cblk_grid_height];
						for(uint64_t cblkno = 0;
							cblkno < decodePrec->cblk_grid_width * decodePrec->cblk_grid_height;
							++cblkno)
						{
							auto cblk = prec->getCompressedBlockPtr() + cblkno;
							auto decodeCblk = decodePrec->getDecompressedBlockPtr() + cblkno;
							decodeCblk->x0 = cblk->x0;
							decodeCblk->y0 = cblk->y0;
							decodeCblk->x1 = cblk->x1;
							decodeCblk->y1 = cblk->y1;
							decodeCblk->alloc();
						}
					}
				}
			}
		}
	}
#endif
	if(!l_t2->compressPackets(m_tileIndex, m_tcp->numlayers, m_stream, tileBytesWritten,
							  m_first_poc_tile_part, newTilePartProgressionPosition, pino))
	{
		delete l_t2;
		return false;
	}

	delete l_t2;

#ifdef DEBUG_LOSSLESS_T2
	for(uint32_t compno = 0; compno < p_image->m_numcomps; ++compno)
	{
		TileComponent* tilec = &tilePtr->comps[compno];
		for(uint32_t resno = 0; resno < tilec->numresolutions; ++resno)
		{
			auto roundRes = tilec->round_trip_resolutions + resno;
			for(uint32_t bandIndex = 0; bandIndex < roundRes->numTileBandWindows; ++bandIndex)
			{
				auto decodeBand = roundRes->tileBand + bandIndex;
				if(decodeBand->precincts)
				{
					for(uint64_t precinctIndex = 0; precinctIndex < decodeBand->numPrecincts;
						++precinctIndex)
					{
						auto decodePrec = decodeBand->precincts + precinctIndex;
						decodePrec->cleanupDecodeBlocks();
					}
					delete[] decodeBand->precincts;
					decodeBand->precincts = nullptr;
				}
			}
		}
		delete[] tilec->round_trip_resolutions;
	}
#endif

	return true;
}
bool TileProcessor::preCompressTile()
{
	m_tilePartIndex = 0;
	numTilePartsTotal = m_cp->tcps[m_tileIndex].numTileParts;
	m_first_poc_tile_part = true;

	/* initialization before tile compressing  */
	bool rc = init();
	if(!rc)
		return false;
	rc = allocWindowBuffers(nullptr);
	if(!rc)
		return false;
	uint32_t numTiles = (uint32_t)m_cp->t_grid_height * m_cp->t_grid_width;
	bool transfer_image_to_tile = (numTiles == 1);

	/* if we only have one tile, then simply set tile component data equal to
	 * image component data. Otherwise, allocate tile data and copy */
	for(uint32_t j = 0; j < headerImage->numcomps; ++j)
	{
		auto tilec = tile->comps + j;
		auto imagec = headerImage->comps + j;
		if(transfer_image_to_tile && imagec->data)
		{
			tilec->getBuffer()->attach(imagec->data, imagec->stride);
		}
		else
		{
			if(!tilec->getBuffer()->alloc())
			{
				GRK_ERROR("Error allocating tile component data.");
				return false;
			}
		}
	}
	if(!transfer_image_to_tile)
		ingestImage();

	return true;
}
/**
 * Assume that source stride  == source width == destination width
 */
template<typename T>
void grk_copy_strided(uint32_t w, uint32_t stride, uint32_t h, T* src, int32_t* dest)
{
	assert(stride >= w);
	uint32_t stride_diff = stride - w;
	size_t src_ind = 0, dest_ind = 0;
	for(uint32_t j = 0; j < h; ++j)
	{
		for(uint32_t i = 0; i < w; ++i)
		{
			dest[dest_ind++] = src[src_ind++];
		}
		dest_ind += stride_diff;
	}
}
bool TileProcessor::ingestUncompressedData(uint8_t* p_src, uint64_t src_length)
{
	uint64_t tile_size = 0;
	for(uint32_t i = 0; i < headerImage->numcomps; ++i)
	{
		auto tilec = tile->comps + i;
		auto img_comp = headerImage->comps + i;
		uint32_t size_comp = (uint32_t)((img_comp->prec + 7) >> 3);
		tile_size += size_comp * tilec->area();
	}
	if(!p_src || (tile_size != src_length))
		return false;
	size_t length_per_component = src_length / headerImage->numcomps;
	for(uint32_t i = 0; i < headerImage->numcomps; ++i)
	{
		auto tilec = tile->comps + i;
		auto img_comp = headerImage->comps + i;

		uint32_t size_comp = (uint32_t)((img_comp->prec + 7) >> 3);
		auto dest_ptr = tilec->getBuffer()->getHighestBufferResWindowREL()->getBuffer();
		uint32_t w = (uint32_t)tilec->getBuffer()->bounds().width();
		uint32_t h = (uint32_t)tilec->getBuffer()->bounds().height();
		uint32_t stride = tilec->getBuffer()->getHighestBufferResWindowREL()->stride;
		switch(size_comp)
		{
			case 1:
				if(img_comp->sgnd)
				{
					auto src = (int8_t*)p_src;
					grk_copy_strided<int8_t>(w, stride, h, src, dest_ptr);
					p_src = (uint8_t*)(src + length_per_component);
				}
				else
				{
					auto src = (uint8_t*)p_src;
					grk_copy_strided<uint8_t>(w, stride, h, src, dest_ptr);
					p_src = (uint8_t*)(src + length_per_component);
				}
				break;
			case 2:
				if(img_comp->sgnd)
				{
					auto src = (int16_t*)p_src;
					grk_copy_strided<int16_t>(w, stride, h, (int16_t*)p_src, dest_ptr);
					p_src = (uint8_t*)(src + length_per_component);
				}
				else
				{
					auto src = (uint16_t*)p_src;
					grk_copy_strided<uint16_t>(w, stride, h, (uint16_t*)p_src, dest_ptr);
					p_src = (uint8_t*)(src + length_per_component);
				}
				break;
		}
	}
	return true;
}
bool TileProcessor::prepareSodDecompress(CodeStreamDecompress* codeStream)
{
	assert(codeStream);

	// note: we subtract 2 to account for SOD marker
	auto tcp = codeStream->get_current_decode_tcp();
	if(codeStream->getDecompressorState()->lastTilePartInCodeStream)
	{
		tilePartDataLength = (uint32_t)(m_stream->numBytesLeft() - 2);
	}
	else
	{
		if(tilePartDataLength >= 2)
			tilePartDataLength -= 2;
	}
	if(tilePartDataLength)
	{
		auto bytesLeftInStream = m_stream->numBytesLeft();
		if(bytesLeftInStream == 0)
		{
			GRK_ERROR("Tile %d, tile part %d: stream has been truncated and "
					  "there is no tile data available",
					  m_tileIndex, tcp->m_tilePartIndex);
			return false;
		}
		// check that there are enough bytes in stream to fill tile data
		if(tilePartDataLength > bytesLeftInStream)
		{
			GRK_WARN("Tile part length %lld greater than "
					 "stream length %lld\n"
					 "(tile: %u, tile part: %d). Tile has been truncated.",
					 tilePartDataLength, m_stream->numBytesLeft(), m_tileIndex,
					 tcp->m_tilePartIndex);

			// sanitize tilePartDataLength
			tilePartDataLength = (uint32_t)bytesLeftInStream;
		}
	}
	/* Index */
	auto codeStreamInfo = codeStream->getCodeStreamInfo();
	if(codeStreamInfo)
	{
		uint64_t current_pos = m_stream->tell();
		if(current_pos < 2)
		{
			GRK_ERROR("Stream too short");
			return false;
		}
		current_pos = (uint64_t)(current_pos - 2);

		auto tileInfo = codeStreamInfo->getTileInfo(m_tileIndex);
		uint8_t current_tile_part = tileInfo->currentTilePart;
		auto tilePartInfo = tileInfo->getTilePartInfo(current_tile_part);
		tilePartInfo->endHeaderPosition = current_pos;
		tilePartInfo->endPosition = current_pos + tilePartDataLength + 2;

		if(!TileLengthMarkers::addTileMarkerInfo(m_tileIndex, codeStreamInfo, J2K_MS_SOD,
												 current_pos, 0))
		{
			GRK_ERROR("Not enough memory to add tl marker");
			return false;
		}
	}
	size_t current_read_size = 0;
	if(tilePartDataLength)
	{
		if(!tcp->m_compressedTileData)
			tcp->m_compressedTileData = new SparseBuffer();

		auto len = tilePartDataLength;
		uint8_t* buff = nullptr;
		auto zeroCopy = m_stream->supportsZeroCopy();
		if(zeroCopy)
		{
			buff = m_stream->getZeroCopyPtr();
		}
		else
		{
			try
			{
				buff = new uint8_t[len];
			}
			catch(std::bad_alloc& ex)
			{
				GRK_UNUSED(ex);
				GRK_ERROR("Not enough memory to allocate segment");
				return false;
			}
		}
		current_read_size = m_stream->read(zeroCopy ? nullptr : buff, len);
		tcp->m_compressedTileData->pushBack(buff, len, !zeroCopy);
	}
	if(current_read_size != tilePartDataLength)
		codeStream->getDecompressorState()->setState(J2K_DEC_STATE_NO_EOC);
	else
		codeStream->getDecompressorState()->setState(J2K_DEC_STATE_TPH_SOT);

	return true;
}
// RATE CONTROL ////////////////////////////////////////////
bool TileProcessor::rateAllocate(uint32_t* allPacketBytes)
{
	// rate control by rate/distortion or fixed quality
	switch(m_cp->m_coding_params.m_enc.rateControlAlgorithm)
	{
		case 0:
			return pcrdBisectSimple(allPacketBytes);
			break;
		case 1:
			return pcrdBisectFeasible(allPacketBytes);
			break;
		default:
			return pcrdBisectFeasible(allPacketBytes);
			break;
	}
}
bool TileProcessor::layerNeedsRateControl(uint32_t layno)
{
	auto enc_params = &m_cp->m_coding_params.m_enc;
	return (enc_params->m_allocationByRateDistortion && (m_tcp->rates[layno] > 0.0)) ||
		   (enc_params->m_allocationByFixedQuality && (m_tcp->distortion[layno] > 0.0f));
}
bool TileProcessor::needsRateControl()
{
	for(uint16_t i = 0; i < m_tcp->numlayers; ++i)
	{
		if(layerNeedsRateControl(i))
			return true;
	}
	return false;
}
// lossless in the sense that no code passes are removed; it mays still be a lossless layer
// due to irreversible DWT and quantization
bool TileProcessor::makeSingleLosslessLayer()
{
	if(m_tcp->numlayers == 1 && !layerNeedsRateControl(0))
	{
		makeLayerFinal(0);
		return true;
	}
	return false;
}
void TileProcessor::makeLayerFeasible(uint32_t layno, uint16_t thresh, bool finalAttempt)
{
	uint32_t passno;
	tile->layerDistoration[layno] = 0;
	for(uint16_t compno = 0; compno < tile->numcomps; compno++)
	{
		auto tilec = tile->comps + compno;
		for(uint8_t resno = 0; resno < tilec->numresolutions; resno++)
		{
			auto res = tilec->tileCompResolution + resno;
			for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
			{
				auto band = res->tileBand + bandIndex;
				for(auto prc : band->precincts)
				{
					for(uint64_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
					{
						auto cblk = prc->getCompressedBlockPtr(cblkno);
						auto layer = cblk->layers + layno;
						uint32_t cumulative_included_passes_in_block;

						if(layno == 0)
							cblk->numPassesInPreviousPackets = 0;

						cumulative_included_passes_in_block = cblk->numPassesInPreviousPackets;

						for(passno = cblk->numPassesInPreviousPackets;
							passno < cblk->numPassesTotal; passno++)
						{
							auto pass = &cblk->passes[passno];

							// truncate or include feasible, otherwise ignore
							if(pass->slope)
							{
								if(pass->slope <= thresh)
									break;
								cumulative_included_passes_in_block = passno + 1;
							}
						}

						layer->numpasses =
							cumulative_included_passes_in_block - cblk->numPassesInPreviousPackets;

						if(!layer->numpasses)
						{
							layer->distortion = 0;
							continue;
						}

						// update layer
						if(cblk->numPassesInPreviousPackets == 0)
						{
							layer->len = cblk->passes[cumulative_included_passes_in_block - 1].rate;
							layer->data = cblk->paddedCompressedStream;
							layer->distortion =
								cblk->passes[cumulative_included_passes_in_block - 1].distortiondec;
						}
						else
						{
							layer->len =
								cblk->passes[cumulative_included_passes_in_block - 1].rate -
								cblk->passes[cblk->numPassesInPreviousPackets - 1].rate;
							layer->data = cblk->paddedCompressedStream +
										  cblk->passes[cblk->numPassesInPreviousPackets - 1].rate;
							layer->distortion =
								cblk->passes[cumulative_included_passes_in_block - 1]
									.distortiondec -
								cblk->passes[cblk->numPassesInPreviousPackets - 1].distortiondec;
						}

						tile->layerDistoration[layno] += layer->distortion;
						if(finalAttempt)
							cblk->numPassesInPreviousPackets = cumulative_included_passes_in_block;
					}
				}
			}
		}
	}
}
/*
 Hybrid rate control using bisect algorithm with optimal truncation points
 */
bool TileProcessor::pcrdBisectFeasible(uint32_t* allPacketBytes)
{
	bool single_lossless = makeSingleLosslessLayer();
	const double K = 1;
	double maxSE = 0;
	auto tcp = m_tcp;
	uint32_t state = grk_plugin_get_debug_state();
	RateInfo rateInfo;
	for(uint16_t compno = 0; compno < tile->numcomps; compno++)
	{
		auto tilec = &tile->comps[compno];
		uint64_t numpix = 0;
		for(uint8_t resno = 0; resno < tilec->numresolutions; resno++)
		{
			auto res = &tilec->tileCompResolution[resno];
			for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
			{
				auto band = &res->tileBand[bandIndex];
				for(auto prc : band->precincts)
				{
					for(uint64_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
					{
						auto cblk = prc->getCompressedBlockPtr(cblkno);
						uint32_t numPix = (uint32_t)cblk->area();
						if(!(state & GRK_PLUGIN_STATE_PRE_TR1))
						{
							compress_synch_with_plugin(this, compno, resno, bandIndex,
													   prc->precinctIndex, cblkno, band, cblk,
													   &numPix);
						}

						if(!single_lossless)
						{
							RateControl::convexHull(cblk->passes, cblk->numPassesTotal);
							rateInfo.synch(cblk);
							numpix += numPix;
						}
					} /* cbklno */
				} /* precinctIndex */
			} /* bandIndex */
		} /* resno */

		if(!single_lossless)
		{
			maxSE += (double)(((uint64_t)1 << headerImage->comps[compno].prec) - 1) *
					 (double)(((uint64_t)1 << headerImage->comps[compno].prec) - 1) *
					 (double)numpix;
		}
	} /* compno */

	auto t2 = T2Compress(this);
	if(single_lossless)
	{
		// simulation will generate correct PLT lengths
		// and correct tile length
		return t2.compressPacketsSimulate(m_tileIndex, 0 + 1U, allPacketBytes, UINT_MAX,
										  newTilePartProgressionPosition,
										  packetLengthCache.getMarkers());
	}

	uint32_t min_slope = rateInfo.getMinimumThresh();
	uint32_t max_slope = USHRT_MAX;
	double cumulativeDistortion[maxCompressLayersGRK];
	uint32_t upperBound = max_slope;
	uint32_t maxLayerLength = UINT_MAX;
	for(uint16_t layno = 0; layno < tcp->numlayers; layno++)
	{
		uint32_t lowerBound = min_slope;
		maxLayerLength = tcp->rates[layno] > 0.0f ? ((uint32_t)ceil(tcp->rates[layno])) : UINT_MAX;

		if(layerNeedsRateControl(layno))
		{
			// thresh from previous iteration - starts off uninitialized
			// used to bail out if difference with current thresh is small enough
			uint32_t prevthresh = 0;
			double distortionTarget =
				tile->distortion - ((K * maxSE) / pow(10.0, tcp->distortion[layno] / 10.0));

			for(uint32_t i = 0; i < 128; ++i)
			{
				uint32_t thresh = (lowerBound + upperBound) >> 1;
				if(prevthresh != 0 && prevthresh == thresh)
					break;
				makeLayerFeasible(layno, (uint16_t)thresh, false);
				prevthresh = thresh;
				if(m_cp->m_coding_params.m_enc.m_allocationByFixedQuality)
				{
					double distoachieved = layno == 0 ? tile->layerDistoration[0]
													  : cumulativeDistortion[layno - 1] +
															tile->layerDistoration[layno];

					if(distoachieved < distortionTarget)
					{
						upperBound = thresh;
						continue;
					}
					lowerBound = thresh;
				}
				else
				{
					if(!t2.compressPacketsSimulate(m_tileIndex, (uint16_t)(layno + 1U),
												   allPacketBytes, maxLayerLength,
												   newTilePartProgressionPosition, nullptr))
					{
						lowerBound = thresh;
						continue;
					}
					upperBound = thresh;
				}
			}
			// choose conservative value for goodthresh
			/* Threshold for Marcela Index */
			// start by including everything in this layer
			uint32_t goodthresh = upperBound;
			makeLayerFeasible(layno, (uint16_t)goodthresh, true);
			cumulativeDistortion[layno] =
				(layno == 0) ? tile->layerDistoration[0]
							 : (cumulativeDistortion[layno - 1] + tile->layerDistoration[layno]);
			// upper bound for next layer is initialized to lowerBound for current layer, minus one
			upperBound = lowerBound - 1;
		}
		else
		{
			makeLayerFinal(layno);
		}
	}

	// final simulation will generate correct PLT lengths
	// and correct tile length
	return t2.compressPacketsSimulate(m_tileIndex, tcp->numlayers, allPacketBytes, maxLayerLength,
									  newTilePartProgressionPosition,
									  packetLengthCache.getMarkers());
}
/*
 Simple bisect algorithm to calculate optimal layer truncation points
 */
bool TileProcessor::pcrdBisectSimple(uint32_t* allPacketBytes)
{
	uint32_t passno;
	const double K = 1;
	double maxSE = 0;
	double min_slope = DBL_MAX;
	double max_slope = -1;
	uint32_t state = grk_plugin_get_debug_state();
	bool single_lossless = makeSingleLosslessLayer();
	for(uint16_t compno = 0; compno < tile->numcomps; compno++)
	{
		auto tilec = &tile->comps[compno];
		uint64_t numpix = 0;
		for(uint8_t resno = 0; resno < tilec->numresolutions; resno++)
		{
			auto res = &tilec->tileCompResolution[resno];
			for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
			{
				auto band = &res->tileBand[bandIndex];
				for(auto prc : band->precincts)
				{
					for(uint64_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
					{
						auto cblk = prc->getCompressedBlockPtr(cblkno);
						uint32_t numPix = (uint32_t)cblk->area();
						if(!(state & GRK_PLUGIN_STATE_PRE_TR1))
						{
							compress_synch_with_plugin(this, compno, resno, bandIndex,
													   prc->precinctIndex, cblkno, band, cblk,
													   &numPix);
						}
						if(!single_lossless)
						{
							for(passno = 0; passno < cblk->numPassesTotal; passno++)
							{
								auto pass = cblk->passes + passno;
								int32_t dr;
								double dd, rdslope;

								if(passno == 0)
								{
									dr = (int32_t)pass->rate;
									dd = pass->distortiondec;
								}
								else
								{
									dr = (int32_t)(pass->rate - cblk->passes[passno - 1].rate);
									dd = pass->distortiondec -
										 cblk->passes[passno - 1].distortiondec;
								}

								if(dr == 0)
									continue;

								rdslope = dd / dr;
								if(rdslope < min_slope)
									min_slope = rdslope;
								if(rdslope > max_slope)
									max_slope = rdslope;
							} /* passno */
							numpix += numPix;
						}
					} /* cbklno */
				} /* precinctIndex */
			} /* bandIndex */
		} /* resno */

		if(!single_lossless)
			maxSE += (double)(((uint64_t)1 << headerImage->comps[compno].prec) - 1) *
					 (double)(((uint64_t)1 << headerImage->comps[compno].prec) - 1) *
					 (double)numpix;

	} /* compno */

	auto t2 = T2Compress(this);
	if(single_lossless)
	{
		// simulation will generate correct PLT lengths
		// and correct tile length
		return t2.compressPacketsSimulate(m_tileIndex, 0 + 1U, allPacketBytes, UINT_MAX,
										  newTilePartProgressionPosition,
										  packetLengthCache.getMarkers());
	}
	double cumulativeDistortion[maxCompressLayersGRK];
	double upperBound = max_slope;
	if(packetLengthCache.getMarkers())
		packetLengthCache.getMarkers()->pushInit();
	uint32_t maxLayerLength = UINT_MAX;
	for(uint16_t layno = 0; layno < m_tcp->numlayers; layno++)
	{
		maxLayerLength =
			(m_tcp->rates[layno] > 0.0f) ? (uint32_t)ceil(m_tcp->rates[layno]) : UINT_MAX;
		if(layerNeedsRateControl(layno))
		{
			double lowerBound = min_slope;

			/* Threshold for Marcela Index */
			// start by including everything in this layer
			double goodthresh = 0;

			// thresh from previous iteration - starts off uninitialized
			// used to bail out if difference with current thresh is small enough
			double prevthresh = -1;
			double distortionTarget =
				tile->distortion - ((K * maxSE) / pow(10.0, m_tcp->distortion[layno] / 10.0));

			double thresh;
			for(uint32_t i = 0; i < 128; ++i)
			{
				// thresh is half-way between lower and upper bound
				thresh = (upperBound == -1) ? lowerBound : (lowerBound + upperBound) / 2;
				makeLayerSimple(layno, thresh, false);
				if(prevthresh != -1 && (fabs(prevthresh - thresh)) < 0.001)
					break;
				prevthresh = thresh;
				if(m_cp->m_coding_params.m_enc.m_allocationByFixedQuality)
				{
					double distoachieved = layno == 0 ? tile->layerDistoration[0]
													  : cumulativeDistortion[layno - 1] +
															tile->layerDistoration[layno];
					if(distoachieved < distortionTarget)
					{
						upperBound = thresh;
						continue;
					}
					lowerBound = thresh;
				}
				else
				{
					if(!t2.compressPacketsSimulate(m_tileIndex, layno + 1U, allPacketBytes,
												   maxLayerLength, newTilePartProgressionPosition,
												   nullptr))
					{
						lowerBound = thresh;
						continue;
					}
					upperBound = thresh;
				}
			}
			// choose conservative value for goodthresh
			goodthresh = (upperBound == -1) ? thresh : upperBound;
			makeLayerSimple(layno, goodthresh, true);
			cumulativeDistortion[layno] =
				(layno == 0) ? tile->layerDistoration[0]
							 : (cumulativeDistortion[layno - 1] + tile->layerDistoration[layno]);

			// upper bound for next layer will equal lowerBound for previous layer, minus one
			upperBound = lowerBound - 1;
		}
		else
		{
			makeLayerFinal(layno);
			assert(layno == m_tcp->numlayers - 1);
		}
	}
	// final simulation will generate correct PLT lengths
	// and correct tile length
	// GRK_INFO("Rate control final simulation");
	return t2.compressPacketsSimulate(m_tileIndex, m_tcp->numlayers, allPacketBytes, maxLayerLength,
									  newTilePartProgressionPosition,
									  packetLengthCache.getMarkers());
}
static void prepareBlockForFirstLayer(CompressCodeblock* cblk)
{
	cblk->numPassesInPreviousPackets = 0;
	cblk->numPassesInPacket = 0;
	cblk->numlenbits = 0;
}
/*
 Form layer for bisect rate control algorithm
 */
void TileProcessor::makeLayerSimple(uint32_t layno, double thresh, bool finalAttempt)
{
	tile->layerDistoration[layno] = 0;
	for(uint16_t compno = 0; compno < tile->numcomps; compno++)
	{
		auto tilec = tile->comps + compno;
		for(uint8_t resno = 0; resno < tilec->numresolutions; resno++)
		{
			auto res = tilec->tileCompResolution + resno;
			for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
			{
				auto band = res->tileBand + bandIndex;
				for(auto prc : band->precincts)
				{
					for(uint64_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
					{
						auto cblk = prc->getCompressedBlockPtr(cblkno);
						auto layer = cblk->layers + layno;
						uint32_t included_blk_passes;

						if(layno == 0)
							prepareBlockForFirstLayer(cblk);
						if(thresh == 0)
						{
							included_blk_passes = cblk->numPassesTotal;
						}
						else
						{
							included_blk_passes = cblk->numPassesInPreviousPackets;
							for(uint32_t passno = cblk->numPassesInPreviousPackets;
								passno < cblk->numPassesTotal; passno++)
							{
								uint32_t dr;
								double dd;
								CodePass* pass = &cblk->passes[passno];
								if(included_blk_passes == 0)
								{
									dr = pass->rate;
									dd = pass->distortiondec;
								}
								else
								{
									dr = pass->rate - cblk->passes[included_blk_passes - 1].rate;
									dd = pass->distortiondec -
										 cblk->passes[included_blk_passes - 1].distortiondec;
								}

								if(!dr)
								{
									if(dd != 0)
										included_blk_passes = passno + 1;
									continue;
								}
								auto slope = dd / dr;
								/* do not rely on float equality, check with DBL_EPSILON margin */
								if(thresh - slope < DBL_EPSILON)
									included_blk_passes = passno + 1;
							}
						}
						layer->numpasses = included_blk_passes - cblk->numPassesInPreviousPackets;
						if(!layer->numpasses)
						{
							layer->distortion = 0;
							continue;
						}

						// update layer
						if(cblk->numPassesInPreviousPackets == 0)
						{
							layer->len = cblk->passes[included_blk_passes - 1].rate;
							layer->data = cblk->paddedCompressedStream;
							layer->distortion = cblk->passes[included_blk_passes - 1].distortiondec;
						}
						else
						{
							layer->len = cblk->passes[included_blk_passes - 1].rate -
										 cblk->passes[cblk->numPassesInPreviousPackets - 1].rate;
							layer->data = cblk->paddedCompressedStream +
										  cblk->passes[cblk->numPassesInPreviousPackets - 1].rate;
							layer->distortion =
								cblk->passes[included_blk_passes - 1].distortiondec -
								cblk->passes[cblk->numPassesInPreviousPackets - 1].distortiondec;
						}
						tile->layerDistoration[layno] += layer->distortion;
						if(finalAttempt)
							cblk->numPassesInPreviousPackets = included_blk_passes;
					}
				}
			}
		}
	}
}
// Add all remaining passes to this layer
void TileProcessor::makeLayerFinal(uint32_t layno)
{
	tile->layerDistoration[layno] = 0;
	for(uint16_t compno = 0; compno < tile->numcomps; compno++)
	{
		auto tilec = tile->comps + compno;
		for(uint8_t resno = 0; resno < tilec->numresolutions; resno++)
		{
			auto res = tilec->tileCompResolution + resno;
			for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; bandIndex++)
			{
				auto band = res->tileBand + bandIndex;
				for(auto prc : band->precincts)
				{
					for(uint64_t cblkno = 0; cblkno < prc->getNumCblks(); cblkno++)
					{
						auto cblk = prc->getCompressedBlockPtr(cblkno);
						auto layer = cblk->layers + layno;
						if(layno == 0)
							prepareBlockForFirstLayer(cblk);
						uint32_t included_blk_passes = cblk->numPassesInPreviousPackets;
						if(cblk->numPassesTotal > cblk->numPassesInPreviousPackets)
							included_blk_passes = cblk->numPassesTotal;

						layer->numpasses = included_blk_passes - cblk->numPassesInPreviousPackets;
						if(!layer->numpasses)
						{
							layer->distortion = 0;
							continue;
						}
						// update layer
						if(cblk->numPassesInPreviousPackets == 0)
						{
							layer->len = cblk->passes[included_blk_passes - 1].rate;
							layer->data = cblk->paddedCompressedStream;
							layer->distortion = cblk->passes[included_blk_passes - 1].distortiondec;
						}
						else
						{
							layer->len = cblk->passes[included_blk_passes - 1].rate -
										 cblk->passes[cblk->numPassesInPreviousPackets - 1].rate;
							layer->data = cblk->paddedCompressedStream +
										  cblk->passes[cblk->numPassesInPreviousPackets - 1].rate;
							layer->distortion =
								cblk->passes[included_blk_passes - 1].distortiondec -
								cblk->passes[cblk->numPassesInPreviousPackets - 1].distortiondec;
						}
						tile->layerDistoration[layno] += layer->distortion;
						cblk->numPassesInPreviousPackets = included_blk_passes;
						assert(cblk->numPassesInPreviousPackets == cblk->numPassesTotal);
					}
				}
			}
		}
	}
}
Tile::Tile()
	: numcomps(0), comps(nullptr), distortion(0), numProcessedPackets(0), numDecompressedPackets(0)
{
	for(uint32_t i = 0; i < maxCompressLayersGRK; ++i)
		layerDistoration[i] = 0;
}
Tile::~Tile()
{
	delete[] comps;
}
PacketTracker::PacketTracker()
	: bits(nullptr), m_numcomps(0), m_numres(0), m_numprec(0), m_numlayers(0)
{}
PacketTracker::~PacketTracker()
{
	delete[] bits;
}
void PacketTracker::init(uint32_t numcomps, uint32_t numres, uint64_t numprec, uint32_t numlayers)
{
	uint64_t len = get_buffer_len(numcomps, numres, numprec, numlayers);
	if(!bits)
		bits = new uint8_t[len];
	else
	{
		uint64_t currentLen = get_buffer_len(m_numcomps, m_numres, m_numprec, m_numlayers);
		if(len > currentLen)
		{
			delete[] bits;
			bits = new uint8_t[len];
		}
	}
	clear();
	m_numcomps = numcomps;
	m_numres = numres;
	m_numprec = numprec;
	m_numlayers = numlayers;
}
void PacketTracker::clear(void)
{
	uint64_t currentLen = get_buffer_len(m_numcomps, m_numres, m_numprec, m_numlayers);
	memset(bits, 0, currentLen);
}
uint64_t PacketTracker::get_buffer_len(uint32_t numcomps, uint32_t numres, uint64_t numprec,
									   uint32_t numlayers)
{
	uint64_t len = numcomps * numres * numprec * numlayers;

	return ((len + 7) >> 3) << 3;
}
void PacketTracker::packet_encoded(uint32_t comps, uint32_t res, uint64_t prec, uint32_t layer)
{
	if(comps >= m_numcomps || prec >= m_numprec || res >= m_numres || layer >= m_numlayers)
	{
		return;
	}

	uint64_t ind = index(comps, res, prec, layer);
	uint64_t ind_maj = ind >> 3;
	uint64_t ind_min = ind & 7;

	bits[ind_maj] = (uint8_t)(bits[ind_maj] | (1 << ind_min));
}
bool PacketTracker::is_packet_encoded(uint32_t comps, uint32_t res, uint64_t prec, uint32_t layer)
{
	if(comps >= m_numcomps || prec >= m_numprec || res >= m_numres || layer >= m_numlayers)
	{
		return true;
	}
	uint64_t ind = index(comps, res, prec, layer);
	uint64_t ind_maj = ind >> 3;
	uint64_t ind_min = ind & 7;

	return ((bits[ind_maj] >> ind_min) & 1);
}
uint64_t PacketTracker::index(uint32_t comps, uint32_t res, uint64_t prec, uint32_t layer)
{
	return prec + (uint64_t)res * m_numprec + (uint64_t)comps * m_numres * m_numprec +
		   (uint64_t)layer * m_numcomps * m_numres * m_numprec;
}

} // namespace grk
