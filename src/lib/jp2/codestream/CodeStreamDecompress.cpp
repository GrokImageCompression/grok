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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"

namespace grk
{

CodeStreamDecompress::CodeStreamDecompress(IBufferedStream* stream)
	: CodeStream(stream), wholeTileDecompress(true), m_curr_marker(0), m_headerError(false),
	  m_tile_ind_to_dec(-1), m_marker_scratch(nullptr), m_marker_scratch_size(0),
	  m_outputImage(nullptr), m_tileCache(new TileCache())
{
	m_decompressorState.m_default_tcp = new TileCodingParams();
	m_decompressorState.lastSotReadPosition = 0;

	/* code stream index creation */
	codeStreamInfo = new CodeStreamInfo(stream);
	if(!codeStreamInfo)
	{
		delete m_decompressorState.m_default_tcp;
		throw std::runtime_error("Out of memory");
	}
	marker_map = {
		{J2K_MS_SOT,
		 new marker_handler(J2K_MS_SOT, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH_SOT,
							[this](uint8_t* data, uint16_t len) { return read_sot(data, len); })},
		{J2K_MS_COD,
		 new marker_handler(J2K_MS_COD, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
							[this](uint8_t* data, uint16_t len) { return read_cod(data, len); })},
		{J2K_MS_COC,
		 new marker_handler(J2K_MS_COC, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
							[this](uint8_t* data, uint16_t len) { return read_coc(data, len); })},
		{J2K_MS_RGN,
		 new marker_handler(J2K_MS_RGN, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
							[this](uint8_t* data, uint16_t len) { return read_rgn(data, len); })},
		{J2K_MS_QCD,
		 new marker_handler(J2K_MS_QCD, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
							[this](uint8_t* data, uint16_t len) { return read_qcd(data, len); })},
		{J2K_MS_QCC,
		 new marker_handler(J2K_MS_QCC, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
							[this](uint8_t* data, uint16_t len) { return read_qcc(data, len); })},
		{J2K_MS_POC,
		 new marker_handler(J2K_MS_POC, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
							[this](uint8_t* data, uint16_t len) { return read_poc(data, len); })},
		{J2K_MS_SIZ,
		 new marker_handler(J2K_MS_SIZ, DECOMPRESS_STATE_MH_SIZ,
							[this](uint8_t* data, uint16_t len) { return read_siz(data, len); })},
		{J2K_MS_CAP,
		 new marker_handler(J2K_MS_CAP, DECOMPRESS_STATE_MH,
							[this](uint8_t* data, uint16_t len) { return read_cap(data, len); })},
		{J2K_MS_TLM,
		 new marker_handler(J2K_MS_TLM, DECOMPRESS_STATE_MH,
							[this](uint8_t* data, uint16_t len) { return read_tlm(data, len); })},
		{J2K_MS_PLM,
		 new marker_handler(J2K_MS_PLM, DECOMPRESS_STATE_MH,
							[this](uint8_t* data, uint16_t len) { return read_plm(data, len); })},
		{J2K_MS_PLT,
		 new marker_handler(J2K_MS_PLT, DECOMPRESS_STATE_TPH,
							[this](uint8_t* data, uint16_t len) { return read_plt(data, len); })},
		{J2K_MS_PPM,
		 new marker_handler(J2K_MS_PPM, DECOMPRESS_STATE_MH,
							[this](uint8_t* data, uint16_t len) { return read_ppm(data, len); })},
		{J2K_MS_PPT,
		 new marker_handler(J2K_MS_PPT, DECOMPRESS_STATE_TPH,
							[this](uint8_t* data, uint16_t len) { return read_ppt(data, len); })},
		{J2K_MS_CRG,
		 new marker_handler(J2K_MS_CRG, DECOMPRESS_STATE_MH,
							[this](uint8_t* data, uint16_t len) { return read_crg(data, len); })},
		{J2K_MS_COM,
		 new marker_handler(J2K_MS_COM, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
							[this](uint8_t* data, uint16_t len) { return read_com(data, len); })},
		{J2K_MS_MCT,
		 new marker_handler(J2K_MS_MCT, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
							[this](uint8_t* data, uint16_t len) { return read_mct(data, len); })},
		{J2K_MS_CBD,
		 new marker_handler(J2K_MS_CBD, DECOMPRESS_STATE_MH,
							[this](uint8_t* data, uint16_t len) { return read_cbd(data, len); })},
		{J2K_MS_MCC,
		 new marker_handler(J2K_MS_MCC, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
							[this](uint8_t* data, uint16_t len) { return read_mcc(data, len); })},
		{J2K_MS_MCO,
		 new marker_handler(J2K_MS_MCO, DECOMPRESS_STATE_MH | DECOMPRESS_STATE_TPH,
							[this](uint8_t* data, uint16_t len) { return read_mco(data, len); })}};
}
CodeStreamDecompress::~CodeStreamDecompress()
{
	for(auto& val : marker_map)
		delete val.second;
	delete m_decompressorState.m_default_tcp;
	delete[] m_marker_scratch;
	if(m_outputImage)
		grk_object_unref(&m_outputImage->obj);
	delete m_tileCache;
}
GrkImage* CodeStreamDecompress::getCompositeImage()
{
	return m_tileCache->getComposite();
}
TileProcessor* CodeStreamDecompress::allocateProcessor(uint16_t tileIndex)
{
	auto tileCache = m_tileCache->get(tileIndex);
	auto tileProcessor = tileCache ? tileCache->processor : nullptr;
	if(!tileProcessor)
	{
		tileProcessor = new TileProcessor(this, m_stream, false, wholeTileDecompress);
		tileProcessor->m_tileIndex = tileIndex;
		m_tileCache->put(tileIndex, tileProcessor);
	}
	m_currentTileProcessor = tileProcessor;

	return m_currentTileProcessor;
}
TileCodingParams* CodeStreamDecompress::get_current_decode_tcp()
{
	auto tileProcessor = m_currentTileProcessor;

	return (isDecodingTilePartHeader()) ? m_cp.tcps + tileProcessor->m_tileIndex
										: m_decompressorState.m_default_tcp;
}
CodeStreamInfo* CodeStreamDecompress::getCodeStreamInfo(void)
{
	return codeStreamInfo;
}
bool CodeStreamDecompress::isDecodingTilePartHeader()
{
	return (m_decompressorState.getState() & DECOMPRESS_STATE_TPH);
}
DecompressorState* CodeStreamDecompress::getDecompressorState(void)
{
	return &m_decompressorState;
}
GrkImage* CodeStreamDecompress::getImage(uint16_t tileIndex)
{
	auto entry = m_tileCache->get(tileIndex);
	return entry ? entry->processor->getImage() : nullptr;
}
std::vector<GrkImage*> CodeStreamDecompress::getAllImages(void)
{
	return m_tileCache->getAllImages();
}
GrkImage* CodeStreamDecompress::getImage()
{
	return getCompositeImage();
}
bool CodeStreamDecompress::readHeader(grk_header_info* header_info)
{
	if(m_headerError)
		return false;

	if(!m_headerImage)
	{
		m_headerImage = new GrkImage();

		/* customization of the validation */
		m_validation_list.push_back(std::bind(&CodeStreamDecompress::decompressValidation, this));

		/* validation of the parameters codec */
		if(!exec(m_validation_list))
		{
			m_headerError = true;
			return false;
		}

		m_procedure_list.push_back(std::bind(&CodeStreamDecompress::readHeaderProcedure, this));
		// custom procedures here
		m_procedure_list.push_back(std::bind(&CodeStreamDecompress::copy_default_tcp, this));

		/* read header */
		if(!exec(m_procedure_list))
		{
			m_headerError = true;
			return false;
		}
		auto composite = getCompositeImage();
		m_headerImage->copyHeader(composite);
		if (header_info) {
			composite->decompressFormat = header_info->decompressFormat;
			composite->forceRGB = header_info->forceRGB;
			composite->upsample = header_info->upsample;
			composite->precision = header_info->precision;
			composite->numPrecision = header_info->numPrecision;
		}
	}

	if(header_info)
	{
		CodingParams* cp = nullptr;
		TileCodingParams* tcp = nullptr;
		TileComponentCodingParams* tccp = nullptr;

		cp = &(m_cp);
		tcp = m_decompressorState.m_default_tcp;
		tccp = tcp->tccps;

		header_info->cblockw_init = 1U << tccp->cblkw;
		header_info->cblockh_init = 1U << tccp->cblkh;
		header_info->irreversible = tccp->qmfbid == 0;
		header_info->mct = tcp->mct;
		header_info->rsiz = cp->rsiz;
		header_info->numresolutions = tccp->numresolutions;
		// !!! assume that coding style is constant across all tile components
		header_info->csty = tccp->csty;
		// !!! assume that mode switch is constant across all tiles
		header_info->cblk_sty = tccp->cblk_sty;
		for(uint32_t i = 0; i < header_info->numresolutions; ++i)
		{
			header_info->prcw_init[i] = 1U << tccp->precinctWidthExp[i];
			header_info->prch_init[i] = 1U << tccp->precinctHeightExp[i];
		}
		header_info->tx0 = m_cp.tx0;
		header_info->ty0 = m_cp.ty0;

		header_info->t_width = m_cp.t_width;
		header_info->t_height = m_cp.t_height;

		header_info->t_grid_width = m_cp.t_grid_width;
		header_info->t_grid_height = m_cp.t_grid_height;

		header_info->numlayers = tcp->numlayers;

		header_info->num_comments = m_cp.num_comments;
		for(size_t i = 0; i < header_info->num_comments; ++i)
		{
			header_info->comment[i] = m_cp.comment[i];
			header_info->comment_len[i] = m_cp.comment_len[i];
			header_info->isBinaryComment[i] = m_cp.isBinaryComment[i];
		}
	}
	return true;
}
bool CodeStreamDecompress::setDecompressWindow(grkRectU32 window)
{
	auto cp = &(m_cp);
	auto image = m_headerImage;
	auto compositeImage = getCompositeImage();
	auto decompressor = &m_decompressorState;

	/* Check if we have read the main header */
	if(decompressor->getState() != DECOMPRESS_STATE_TPH_SOT)
	{
		GRK_ERROR("Need to read the main header before setting decompress window");
		return false;
	}

	if(window == grkRectU32(0, 0, 0, 0))
	{
		decompressor->m_start_tile_x_index = 0;
		decompressor->m_start_tile_y_index = 0;
		decompressor->m_end_tile_x_index = cp->t_grid_width;
		decompressor->m_end_tile_y_index = cp->t_grid_height;
	} else {
		/* Check if the window provided by the user are correct */
		uint32_t start_x = window.x0 + image->x0;
		uint32_t start_y = window.y0 + image->y0;
		uint32_t end_x = window.x1 + image->x0;
		uint32_t end_y = window.y1 + image->y0;
		/* Left */
		if(start_x > image->x1)
		{
			GRK_ERROR("Left position of the decompress window (%u)"
					  " is outside of the image area (Xsiz=%u).",
					  start_x, image->x1);
			return false;
		}
		else
		{
			decompressor->m_start_tile_x_index = (start_x - cp->tx0) / cp->t_width;
			compositeImage->x0 = start_x;
		}

		/* Up */
		if(start_y > image->y1)
		{
			GRK_ERROR("Top position of the decompress window (%u)"
					  " is outside of the image area (Ysiz=%u).",
					  start_y, image->y1);
			return false;
		}
		else
		{
			decompressor->m_start_tile_y_index = (start_y - cp->ty0) / cp->t_height;
			compositeImage->y0 = start_y;
		}

		/* Right */
		assert(end_x > 0);
		assert(end_y > 0);
		if(end_x > image->x1)
		{
			GRK_WARN("Right position of the decompress window (%u)"
					 " is outside the image area (Xsiz=%u).",
					 end_x, image->x1);
			decompressor->m_end_tile_x_index = cp->t_grid_width;
			compositeImage->x1 = image->x1;
		}
		else
		{
			// avoid divide by zero
			if(cp->t_width == 0)
				return false;
			decompressor->m_end_tile_x_index = ceildiv<uint32_t>(end_x - cp->tx0, cp->t_width);
			compositeImage->x1 = end_x;
		}

		/* Bottom */
		if(end_y > image->y1)
		{
			GRK_WARN("Bottom position of the decompress window (%u)"
					 " is outside of the image area (Ysiz=%u).",
					 end_y, image->y1);
			decompressor->m_end_tile_y_index = cp->t_grid_height;
			compositeImage->y1 = image->y1;
		}
		else
		{
			// avoid divide by zero
			if(cp->t_height == 0)
				return false;
			decompressor->m_end_tile_y_index = ceildiv<uint32_t>(end_y - cp->ty0, cp->t_height);
			compositeImage->y1 = end_y;
		}
		wholeTileDecompress = false;
		if(!compositeImage->subsampleAndReduce(cp->m_coding_params.m_dec.m_reduce))
			return false;

		GRK_INFO("Decompress window set to (%d,%d,%d,%d)", compositeImage->x0 - image->x0,
				 compositeImage->y0 - image->y0, compositeImage->x1 - image->x0,
				 compositeImage->y1 - image->y0);

	}
	compositeImage->postReadHeader(&m_cp);

	return true;
}
void CodeStreamDecompress::initDecompress(grk_decompress_core_params* parameters)
{
	assert(parameters);

	m_cp.m_coding_params.m_dec.m_layer = parameters->max_layers;
	m_cp.m_coding_params.m_dec.m_reduce = parameters->reduce;
	m_tileCache->setStrategy(parameters->tileCacheStrategy);
	serialize_data = parameters->serialize_data;
	serializeBufferCallback = parameters->serializeBufferCallback;
	reclaimCallback = parameters->reclaimCallback;
}
bool CodeStreamDecompress::decompress(grk_plugin_tile* tile)
{
	m_procedure_list.push_back(std::bind(&CodeStreamDecompress::decompressTiles, this));
	current_plugin_tile = tile;

	return decompressExec();
}
bool CodeStreamDecompress::decompressTile(uint16_t tileIndex)
{
	auto entry = m_tileCache->get(tileIndex);
	if(entry && entry->processor && entry->processor->getImage())
		return true;

	// another tile has already been decoded
	if(m_outputImage)
	{
		/* Copy code stream image information to composite image */
		m_headerImage->copyHeader(getCompositeImage());
	}

	uint16_t numTilesToDecompress = (uint16_t)(m_cp.t_grid_width * m_cp.t_grid_height);
	if(codeStreamInfo)
	{
		if(!codeStreamInfo->allocTileInfo(numTilesToDecompress))
		{
			m_headerError = true;
			return false;
		}
	}

	auto compositeImage = getCompositeImage();
	if(tileIndex >= numTilesToDecompress)
	{
		GRK_ERROR("Tile index %u is greater than maximum tile index %u", tileIndex,
				  numTilesToDecompress - 1);
		return false;
	}

	/* Compute the dimension of the desired tile*/
	uint32_t tile_x = tileIndex % m_cp.t_grid_width;
	uint32_t tile_y = tileIndex / m_cp.t_grid_width;

	auto imageBounds =
		grkRectU32(compositeImage->x0, compositeImage->y0, compositeImage->x1, compositeImage->y1);
	auto tileBounds = m_cp.getTileBounds(compositeImage, tile_x, tile_y);
	// crop tile bounds with image bounds
	auto croppedImageBounds = imageBounds.intersection(tileBounds);
	if(imageBounds.non_empty() && tileBounds.non_empty() && croppedImageBounds.non_empty())
	{
		compositeImage->x0 = (uint32_t)croppedImageBounds.x0;
		compositeImage->y0 = (uint32_t)croppedImageBounds.y0;
		compositeImage->x1 = (uint32_t)croppedImageBounds.x1;
		compositeImage->y1 = (uint32_t)croppedImageBounds.y1;
	}
	else
	{
		GRK_WARN("Decompress bounds <%u,%u,%u,%u> do not overlap with requested tile %u. "
				 "Decompressing full image",
				 imageBounds.x0, imageBounds.y0, imageBounds.x1, imageBounds.y1, tileIndex);
		croppedImageBounds = imageBounds;
	}

	auto reduce = m_cp.m_coding_params.m_dec.m_reduce;
	for(uint32_t compno = 0; compno < compositeImage->numcomps; ++compno)
	{
		auto comp = compositeImage->comps + compno;
		auto compBounds = croppedImageBounds.rectceildiv(comp->dx, comp->dy);
		auto reducedCompBounds = compBounds.rectceildivpow2(reduce);
		comp->x0 = reducedCompBounds.x0;
		comp->y0 = reducedCompBounds.y0;
		comp->w = reducedCompBounds.width();
		comp->h = reducedCompBounds.height();
	}
	compositeImage->postReadHeader(&m_cp);
	m_tile_ind_to_dec = (int32_t)tileIndex;

	// reset tile part numbers, in case we are re-using the same codec object
	// from previous decompress
	for(uint32_t i = 0; i < numTilesToDecompress; ++i)
		m_cp.tcps[i].m_tilePartIndexCounter = -1;

	/* customization of the decoding */
	m_procedure_list.push_back([this] { return decompressTile(); });

	return decompressExec();
}
bool CodeStreamDecompress::endOfCodeStream(void)
{
	return m_decompressorState.getState() == DECOMPRESS_STATE_EOC ||
		   m_decompressorState.getState() == DECOMPRESS_STATE_NO_EOC ||
		   m_stream->numBytesLeft() == 0;
}
bool CodeStreamDecompress::decompressTiles(void)
{
	uint16_t numTilesToDecompress = (uint16_t)(m_cp.t_grid_height * m_cp.t_grid_width);
	if(codeStreamInfo)
	{
		if(!codeStreamInfo->allocTileInfo(numTilesToDecompress))
		{
			m_headerError = true;
			return false;
		}
	}
	if (!createOutputImage())
		return false;

	m_stripCache.init(m_cp.t_height,
					  m_cp.t_grid_height,
					  m_outputImage,
					  serialize_data,
					  serializeBufferCallback,
					  reclaimCallback);

	std::vector<std::future<int>> results;
	std::atomic<bool> success(true);
	std::atomic<uint32_t> numTilesDecompressed(0);
	ThreadPool pool(
		std::min<uint32_t>((uint32_t)ThreadPool::get()->num_threads(), numTilesToDecompress));
	bool breakAfterT1 = false;
	bool canDecompress = true;
	while(!endOfCodeStream() && !breakAfterT1)
	{
		// 1. read header
		try
		{
			if(!parseTileHeaderMarkers(&canDecompress))
			{
				success = false;
				goto cleanup;
			}
		}
		catch(InvalidMarkerException& ime)
		{
			GRK_ERROR("Found invalid marker : 0x%x", ime.m_marker);
			success = false;
			goto cleanup;
		}
		if(!canDecompress)
			continue;
		if(!m_currentTileProcessor)
		{
			GRK_ERROR("Missing SOT marker");
			success = false;
			goto cleanup;
		}
		// 2. find next tile
		auto processor = m_currentTileProcessor;
		m_currentTileProcessor = nullptr;
		try
		{
			if(!findNextTile(processor))
			{
				GRK_ERROR("Failed to decompress tile %u/%u", processor->m_tileIndex,
						  numTilesToDecompress);
				success = false;
				goto cleanup;
			}
		}
		catch(DecodeUnknownMarkerAtEndOfTileException& e)
		{
			GRK_UNUSED(e);
			breakAfterT1 = true;
		}
		// 3. T2 + T1 decompress
		// once we schedule a processor for T1 compression, we will destroy it
		// regardless of success or not
		auto exec = [this, processor, numTilesToDecompress, &numTilesDecompressed, &success] {
			if(success)
			{
				if(!decompressT2T1(processor))
				{
					GRK_ERROR("Failed to decompress tile %u/%u", processor->m_tileIndex,
							  numTilesToDecompress);
					success = false;
				}
				else
				{
					numTilesDecompressed++;
					if(m_multiTile && processor->getImage()) {
						if (!m_outputImage->composite(processor->getImage()))
							success = false;
						/*
						if (m_outputImage->canAllocInterleaved(&m_cp)) {
							if (!m_stripCache.composite(processor->getImage()))
								success = false;
						}
						*/
					}
					//if cache strategy set to none, then delete image
					if (!success || m_tileCache->getStrategy() == GRK_TILE_CACHE_NONE){
						processor->release();
					}
				}

			}
			return 0;
		};
		if(pool.num_threads() > 1)
			results.emplace_back(pool.enqueue(exec));
		else
		{
			exec();
			if(!success)
				goto cleanup;
		}
	}
	for(auto& result : results)
	{
		result.get();
	}
	results.clear();
	if(!success)
		return false;

	// check if there is another tile that has not been processed
	// we will reject if it has the TPSot problem
	// (https://github.com/uclouvain/openjpeg/issues/254)
	if(m_curr_marker == J2K_MS_SOT && m_stream->numBytesLeft())
	{
		uint16_t marker_size;
		if(!read_short(&marker_size))
		{
			success = false;
			goto cleanup;
		}
		marker_size =
			(uint16_t)(marker_size - 2); /* Subtract the size of the marker ID already read */
		auto marker_handler = get_marker_handler(m_curr_marker);
		if(!(m_decompressorState.getState() & marker_handler->states))
		{
			GRK_ERROR("Marker %d is not compliant with its position", m_curr_marker);
			success = false;
			goto cleanup;
		}
		if(!process_marker(marker_handler, marker_size))
		{
			success = false;
			goto cleanup;
		}
	}
	if(numTilesDecompressed == 0)
	{
		GRK_ERROR("No tiles were decompressed.");
		success = false;
		goto cleanup;
	}
	else if(numTilesDecompressed < numTilesToDecompress && wholeTileDecompress)
	{
		uint32_t decompressed = numTilesDecompressed;
		GRK_WARN("Only %u out of %u tiles were decompressed", decompressed, numTilesToDecompress);
	}
cleanup:
	for(auto& result : results)
	{
		result.get();
	}
	return success;
}
bool CodeStreamDecompress::copy_default_tcp(void)
{
	for(uint32_t i = 0; i < m_cp.t_grid_height * m_cp.t_grid_width; ++i)
	{
		auto tcp = m_cp.tcps + i;
		if(!tcp->copy(m_decompressorState.m_default_tcp, m_headerImage))
			return false;
	}

	return true;
}
void CodeStreamDecompress::addMarker(uint16_t id, uint64_t pos, uint32_t len)
{
	if(codeStreamInfo)
		codeStreamInfo->pushMarker(id, pos, len);
}
uint16_t CodeStreamDecompress::getCurrentMarker()
{
	return m_curr_marker;
}
bool CodeStreamDecompress::isWholeTileDecompress()
{
	return wholeTileDecompress;
}
GrkImage* CodeStreamDecompress::getHeaderImage(void)
{
	return m_headerImage;
}
int32_t CodeStreamDecompress::tileIndexToDecode()
{
	return m_tile_ind_to_dec;
}
bool CodeStreamDecompress::readHeaderProcedure(void)
{
	bool rc = false;
	try
	{
		rc = readHeaderProcedureImpl();
	}
	catch(InvalidMarkerException& ime)
	{
		GRK_ERROR("Found invalid marker : 0x%x", ime.m_marker);
		rc = false;
	}
	return rc;
}
bool CodeStreamDecompress::readHeaderProcedureImpl(void)
{
	bool has_siz = false;
	bool has_cod = false;
	bool has_qcd = false;

	m_decompressorState.setState(DECOMPRESS_STATE_MH_SOC);

	/* Try to read the SOC marker, the code stream must begin with SOC marker */
	if(!read_soc())
	{
		GRK_ERROR("Code stream must begin with SOC marker ");
		return false;
	}
	if(!readMarker())
		return false;

	if(m_curr_marker != J2K_MS_SIZ)
	{
		GRK_ERROR("Code-stream must contain a valid SIZ marker segment, immediately after the SOC "
				  "marker ");
		return false;
	}

	/* Try to read until the SOT is detected */
	while(m_curr_marker != J2K_MS_SOT)
	{
		auto marker_handler = get_marker_handler(m_curr_marker);
		if(!marker_handler)
		{
			if(!read_unk())
				return false;
			if(m_curr_marker == J2K_MS_SOT)
				break;
			marker_handler = get_marker_handler(m_curr_marker);
		}
		if(marker_handler->id == J2K_MS_SIZ)
			has_siz = true;
		else if(marker_handler->id == J2K_MS_COD)
			has_cod = true;
		else if(marker_handler->id == J2K_MS_QCD)
			has_qcd = true;

		/* Check if the marker is known and if it is in the correct location
		 * (main, tile, end of code stream)*/
		if(!(m_decompressorState.getState() & marker_handler->states))
		{
			GRK_ERROR("Marker %d is not compliant with its position", m_curr_marker);
			return false;
		}

		uint16_t marker_size;
		if(!read_short(&marker_size))
			return false;
		else if(marker_size == 2)
		{
			GRK_ERROR("Zero-size marker in header.");
			return false;
		}
		marker_size =
			(uint16_t)(marker_size - 2); /* Subtract the size of the marker ID already read */

		if(!process_marker(marker_handler, marker_size))
			return false;

		/* Add the marker to the code stream index*/
		addMarker(marker_handler->id, m_stream->tell() - marker_size - 4U, marker_size + 4U);
		// read next marker
		if(!readMarker())
			return false;
	}
	if(!has_siz)
	{
		GRK_ERROR("required SIZ marker not found in main header");
		return false;
	}
	else if(!has_cod)
	{
		GRK_ERROR("required COD marker not found in main header");
		return false;
	}
	else if(!has_qcd)
	{
		GRK_ERROR("required QCD marker not found in main header");
		return false;
	}
	if(!merge_ppm(&(m_cp)))
	{
		GRK_ERROR("Failed to merge PPM data");
		return false;
	}
	// we don't include the SOC marker, therefore subtract 2
	if(codeStreamInfo)
		codeStreamInfo->setMainHeaderEnd((uint32_t)m_stream->tell() - 2U);

	/* Next step: read a tile-part header */
	m_decompressorState.setState(DECOMPRESS_STATE_TPH_SOT);

	return true;
}
bool CodeStreamDecompress::decompressExec(void)
{
	if(!exec(m_procedure_list))
		return false;

	// transfer output image to composite image
	m_outputImage->transferDataTo(getCompositeImage());

	return true;
}

bool CodeStreamDecompress::createOutputImage(void){
	if(!m_multiTile)
	{
		if(m_outputImage)
			grk_object_unref(&m_outputImage->obj);
		m_outputImage = nullptr;
	}
	if(!m_outputImage)
	{
		m_outputImage = new GrkImage();
		getCompositeImage()->copyHeader(m_outputImage);
	}
	// only allocate data if there are multiple tiles. Otherwise, the single tile data
	// will simply be transferred to the output image
	if(m_multiTile && !m_outputImage->allocCompositeData(wholeTileDecompress, &m_cp))
		return false;

	return true;
}

/*
 * Read and decompress one tile.
 */
bool CodeStreamDecompress::decompressTile()
{
	m_multiTile = false;
	if (!createOutputImage())
		return false;

	if(tileIndexToDecode() == -1)
	{
		GRK_ERROR("j2k_decompress_tile: Unable to decompress tile "
				  "since first tile SOT has not been detected");
		return false;
	}
	auto tileCache = m_tileCache->get((uint16_t)tileIndexToDecode());
	auto tileProcessor = tileCache ? tileCache->processor : nullptr;
	bool rc = false;
	if(!tileCache || !tileCache->processor->getImage())
	{
		// if we have a TLM marker, then we can skip tiles until
		// we get to desired tile
		if(m_cp.tlm_markers)
		{
			// for first SOT position, we add two to skip SOC marker
			if(!m_cp.tlm_markers->skipTo((uint16_t)m_tile_ind_to_dec, m_stream,
										 codeStreamInfo->getMainHeaderEnd() + 2))
				return false;
		}
		else
		{
			if(!codeStreamInfo->allocTileInfo((uint16_t)(m_cp.t_grid_width * m_cp.t_grid_height)))
				return false;
			if(!codeStreamInfo->skipToTile((uint16_t)m_tile_ind_to_dec,
										   m_decompressorState.lastSotReadPosition))
				return false;
		}
		/* Special case if we have previously read the EOC marker
		 * (if the previous tile decompressed is the last ) */
		if(m_decompressorState.getState() == DECOMPRESS_STATE_EOC)
			m_decompressorState.setState(DECOMPRESS_STATE_TPH_SOT);
		bool canDecompress = true;
		try
		{
			if(!parseTileHeaderMarkers(&canDecompress))
				goto cleanup;
		}
		catch(InvalidMarkerException& ime)
		{
			GRK_ERROR("Found invalid marker : 0x%x", ime.m_marker);
			goto cleanup;
		}
		tileProcessor = m_currentTileProcessor;
		try
		{
			if(!findNextTile(tileProcessor))
			{
				GRK_ERROR("Failed to decompress tile %u", tileProcessor->m_tileIndex);
				goto cleanup;
			}
		}
		catch(DecodeUnknownMarkerAtEndOfTileException& e)
		{
			GRK_UNUSED(e);
		}
		if(!decompressT2T1(tileProcessor))
			goto cleanup;
	}
	rc = true;
cleanup:

	return rc;
}
bool CodeStreamDecompress::decompressT2T1(TileProcessor* tileProcessor)
{
	auto tcp = m_cp.tcps + tileProcessor->m_tileIndex;
	if(!tcp->m_compressedTileData)
	{
		GRK_ERROR("Decompress: Tile %d has no compressed data", tileProcessor->m_tileIndex);
		return false;
	}
	bool doPost =
		!current_plugin_tile || (current_plugin_tile->decompress_flags & GRK_DECODE_POST_T1);
	if(!tileProcessor->decompressT2T1(tcp, m_outputImage, m_multiTile, doPost))
		return false;

	return true;
}
bool CodeStreamDecompress::findNextTile(TileProcessor* tileProcessor)
{
	auto decompressor = &m_decompressorState;

	if(!(decompressor->getState() & DECOMPRESS_STATE_DATA))
	{
		GRK_ERROR("no tile data.");
		return false;
	}
	auto tcp = m_cp.tcps + tileProcessor->m_tileIndex;
	if(!tcp->m_compressedTileData)
	{
		GRK_ERROR("Missing SOD marker");
		return false;
	}
	// find next tile
	bool rc = true;
	bool doPost = !tileProcessor->current_plugin_tile ||
				  (tileProcessor->current_plugin_tile->decompress_flags & GRK_DECODE_POST_T1);
	if(doPost)
	{
		rc = decompressor->findNextTile(this);
	}
	return rc;
}
bool CodeStreamDecompress::decompressValidation(void)
{
	bool is_valid = true;
	is_valid &= (m_decompressorState.getState() == DECOMPRESS_STATE_NONE);

	return is_valid;
}
bool CodeStreamDecompress::process_marker(const marker_handler* marker_handler,
										  uint16_t marker_size)
{
	if(!m_marker_scratch)
	{
		m_marker_scratch = new uint8_t[default_header_size];
		m_marker_scratch_size = default_header_size;
	}
	if(marker_size > m_marker_scratch_size)
	{
		if(marker_size > m_stream->numBytesLeft())
		{
			GRK_ERROR("Marker size inconsistent with stream length");
			return false;
		}
		delete[] m_marker_scratch;
		m_marker_scratch = new uint8_t[2 * marker_size];
		m_marker_scratch_size = (uint16_t)(2U * marker_size);
	}
	if(m_stream->read(m_marker_scratch, marker_size) != marker_size)
	{
		GRK_ERROR("Stream too short");
		return false;
	}

	return marker_handler->func(m_marker_scratch, marker_size);
}
bool CodeStreamDecompress::read_short(uint16_t* val)
{
	uint8_t temp[2];
	if(m_stream->read(temp, 2) != 2)
		return false;

	grk_read<uint16_t>(temp, val);
	return true;
}
const marker_handler* CodeStreamDecompress::get_marker_handler(uint16_t id)
{
	auto iter = marker_map.find(id);
	if(iter != marker_map.end())
		return iter->second;
	else
	{
		GRK_WARN("Unknown marker 0x%02x detected.", id);
		return nullptr;
	}
}
bool CodeStreamDecompress::readMarker(void)
{
	return readMarker(false);
}
bool CodeStreamDecompress::readMarker(bool suppressWarning)
{
	if(!read_short(&m_curr_marker))
		return false;

	/* Check if the current marker ID is valid */
	if(m_curr_marker < 0xff00)
	{
		if(!suppressWarning)
			GRK_WARN("marker ID 0x%.4x does not match JPEG 2000 marker format 0xffxx",
					 m_curr_marker);
		throw InvalidMarkerException(m_curr_marker);
	}

	return true;
}

/** Reading function used after code stream if necessary */
bool CodeStreamDecompress::endDecompress(void)
{
	return true;
}
bool CodeStreamDecompress::postProcess(void){
	auto img = getCompositeImage();
	bool rc =  img->convertToRGB(wholeTileDecompress);
	if (rc){
		img->applyColourManagement();
		img->greyToRGB();
		img->convertPrecision();
		if (!img->execUpsample())
			return false;
	}

	return rc;
}

void CodeStreamDecompress::dump_tile_info(TileCodingParams* default_tile, uint32_t numcomps,
										  FILE* outputFileStream)
{
	if(default_tile)
	{
		uint32_t compno;

		fprintf(outputFileStream, "\t default tile {\n");
		fprintf(outputFileStream, "\t\t csty=%#x\n", default_tile->csty);
		fprintf(outputFileStream, "\t\t prg=%#x\n", default_tile->prg);
		fprintf(outputFileStream, "\t\t numlayers=%d\n", default_tile->numlayers);
		fprintf(outputFileStream, "\t\t mct=%x\n", default_tile->mct);

		for(compno = 0; compno < numcomps; compno++)
		{
			auto tccp = &(default_tile->tccps[compno]);
			uint32_t resno;
			uint32_t bandIndex, numBandWindows;

			assert(tccp->numresolutions > 0);

			/* coding style*/
			fprintf(outputFileStream, "\t\t comp %u {\n", compno);
			fprintf(outputFileStream, "\t\t\t csty=%#x\n", tccp->csty);
			fprintf(outputFileStream, "\t\t\t numresolutions=%d\n", tccp->numresolutions);
			fprintf(outputFileStream, "\t\t\t cblkw=2^%d\n", tccp->cblkw);
			fprintf(outputFileStream, "\t\t\t cblkh=2^%d\n", tccp->cblkh);
			fprintf(outputFileStream, "\t\t\t cblksty=%#x\n", tccp->cblk_sty);
			fprintf(outputFileStream, "\t\t\t qmfbid=%d\n", tccp->qmfbid);

			fprintf(outputFileStream, "\t\t\t preccintsize (w,h)=");
			for(resno = 0; resno < tccp->numresolutions; resno++)
			{
				fprintf(outputFileStream, "(%d,%d) ", tccp->precinctWidthExp[resno],
						tccp->precinctHeightExp[resno]);
			}
			fprintf(outputFileStream, "\n");

			/* quantization style*/
			fprintf(outputFileStream, "\t\t\t qntsty=%d\n", tccp->qntsty);
			fprintf(outputFileStream, "\t\t\t numgbits=%d\n", tccp->numgbits);
			fprintf(outputFileStream, "\t\t\t stepsizes (m,e)=");
			numBandWindows = (tccp->qntsty == J2K_CCP_QNTSTY_SIQNT)
								 ? 1
								 : (uint32_t)(tccp->numresolutions * 3 - 2);
			for(bandIndex = 0; bandIndex < numBandWindows; bandIndex++)
			{
				fprintf(outputFileStream, "(%d,%d) ", tccp->stepsizes[bandIndex].mant,
						tccp->stepsizes[bandIndex].expn);
			}
			fprintf(outputFileStream, "\n");

			/* RGN value*/
			fprintf(outputFileStream, "\t\t\t roishift=%d\n", tccp->roishift);

			fprintf(outputFileStream, "\t\t }\n");
		} /*end of component of default tile*/
		fprintf(outputFileStream, "\t }\n"); /*end of default tile*/
	}
}

void CodeStreamDecompress::dump(uint32_t flag, FILE* outputFileStream)
{
	/* Check if the flag is compatible with j2k file*/
	if((flag & GRK_JP2_INFO) || (flag & GRK_JP2_IND))
	{
		fprintf(outputFileStream, "Wrong flag\n");
		return;
	}

	/* Dump the image_header */
	if(flag & GRK_IMG_INFO)
	{
		if(getHeaderImage())
			dump_image_header(getHeaderImage(), 0, outputFileStream);
	}

	/* Dump the code stream info from main header */
	if(flag & GRK_J2K_MH_INFO)
	{
		if(getHeaderImage())
			dump_MH_info(outputFileStream);
	}
	/* Dump all tile/code stream info */
	auto cp = getCodingParams();
	if(flag & GRK_J2K_TCH_INFO)
	{
		uint32_t numTiles = cp->t_grid_height * cp->t_grid_width;
		if(getHeaderImage())
		{
			for(uint32_t i = 0; i < numTiles; ++i)
			{
				auto tcp = cp->tcps + i;
				dump_tile_info(tcp, getHeaderImage()->numcomps, outputFileStream);
			}
		}
	}

	/* Dump the code stream info of the current tile */
	if(flag & GRK_J2K_TH_INFO)
	{
	}

	/* Dump the code stream index from main header */
	if((flag & GRK_J2K_MH_IND) && codeStreamInfo)
		codeStreamInfo->dump(outputFileStream);

	/* Dump the code stream index of the current tile */
	if(flag & GRK_J2K_TH_IND)
	{
	}
}
void CodeStreamDecompress::dump_MH_info(FILE* outputFileStream)
{
	fprintf(outputFileStream, "Codestream info from main header: {\n");
	fprintf(outputFileStream, "\t tx0=%d, ty0=%d\n", m_cp.tx0, m_cp.ty0);
	fprintf(outputFileStream, "\t tdx=%d, tdy=%d\n", m_cp.t_width, m_cp.t_height);
	fprintf(outputFileStream, "\t tw=%d, th=%d\n", m_cp.t_grid_width, m_cp.t_grid_height);
	CodeStreamDecompress::dump_tile_info(getDecompressorState()->m_default_tcp,
										 getHeaderImage()->numcomps, outputFileStream);
	fprintf(outputFileStream, "}\n");
}
void CodeStreamDecompress::dump_image_header(GrkImage* img_header, bool dev_dump_flag,
											 FILE* outputFileStream)
{
	char tab[2];

	if(dev_dump_flag)
	{
		fprintf(stdout, "[DEV] Dump an image_header struct {\n");
		tab[0] = '\0';
	}
	else
	{
		fprintf(outputFileStream, "Image info {\n");
		tab[0] = '\t';
		tab[1] = '\0';
	}
	fprintf(outputFileStream, "%s x0=%d, y0=%d\n", tab, img_header->x0, img_header->y0);
	fprintf(outputFileStream, "%s x1=%d, y1=%d\n", tab, img_header->x1, img_header->y1);
	fprintf(outputFileStream, "%s numcomps=%d\n", tab, img_header->numcomps);
	if(img_header->comps)
	{
		uint32_t compno;
		for(compno = 0; compno < img_header->numcomps; compno++)
		{
			fprintf(outputFileStream, "%s\t component %u {\n", tab, compno);
			CodeStreamDecompress::dump_image_comp_header(&(img_header->comps[compno]),
														 dev_dump_flag, outputFileStream);
			fprintf(outputFileStream, "%s}\n", tab);
		}
	}
	fprintf(outputFileStream, "}\n");
}
void CodeStreamDecompress::dump_image_comp_header(grk_image_comp* comp_header, bool dev_dump_flag,
												  FILE* outputFileStream)
{
	char tab[3];

	if(dev_dump_flag)
	{
		fprintf(stdout, "[DEV] Dump an image_comp_header struct {\n");
		tab[0] = '\0';
	}
	else
	{
		tab[0] = '\t';
		tab[1] = '\t';
		tab[2] = '\0';
	}

	fprintf(outputFileStream, "%s dx=%d, dy=%d\n", tab, comp_header->dx, comp_header->dy);
	fprintf(outputFileStream, "%s prec=%d\n", tab, comp_header->prec);
	fprintf(outputFileStream, "%s sgnd=%d\n", tab, comp_header->sgnd ? 1 : 0);

	if(dev_dump_flag)
		fprintf(outputFileStream, "}\n");
}

} // namespace grk
