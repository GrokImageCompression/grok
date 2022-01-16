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
static void j2k_write_float_to_int16(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
	j2k_write<float, int16_t>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_write_float_to_int32(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
	j2k_write<float, int32_t>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_write_float_to_float(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
	j2k_write<float, float>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_write_float_to_float64(const void* p_src_data, void* p_dest_data, uint64_t nb_elem)
{
	j2k_write<float, double>(p_src_data, p_dest_data, nb_elem);
}

static const j2k_mct_function j2k_mct_write_functions_from_float[] = {
	j2k_write_float_to_int16, j2k_write_float_to_int32, j2k_write_float_to_float,
	j2k_write_float_to_float64};

struct j2k_prog_order
{
	GRK_PROG_ORDER enum_prog;
	char str_prog[5];
};

static j2k_prog_order j2k_prog_order_list[] = {{GRK_CPRL, "CPRL"}, {GRK_LRCP, "LRCP"},
											   {GRK_PCRL, "PCRL"}, {GRK_RLCP, "RLCP"},
											   {GRK_RPCL, "RPCL"}, {(GRK_PROG_ORDER)-1, ""}};

CodeStreamCompress::CodeStreamCompress(IBufferedStream* stream) : CodeStream(stream) {}

CodeStreamCompress::~CodeStreamCompress() {}
char* CodeStreamCompress::convertProgressionOrder(GRK_PROG_ORDER prg_order)
{
	j2k_prog_order* po;
	for(po = j2k_prog_order_list; po->enum_prog != -1; po++)
	{
		if(po->enum_prog == prg_order)
			return (char*)po->str_prog;
	}
	return po->str_prog;
}
bool CodeStreamCompress::mct_validation(void)
{
	bool is_valid = true;
	if((m_cp.rsiz & 0x8200) == 0x8200)
	{
		uint32_t numTiles = m_cp.t_grid_height * m_cp.t_grid_width;
		for(uint32_t i = 0; i < numTiles; ++i)
		{
			auto tcp = m_cp.tcps + i;
			if(tcp->mct == 2)
			{
				is_valid &= (tcp->m_mct_coding_matrix != nullptr);
				for(uint32_t j = 0; j < m_headerImage->numcomps; ++j)
				{
					auto tccp = tcp->tccps + j;
					is_valid &= !(tccp->qmfbid & 1);
				}
			}
		}
	}

	return is_valid;
}

bool CodeStreamCompress::startCompress(void)
{
	/* customization of the validation */
	m_validation_list.push_back(std::bind(&CodeStreamCompress::compressValidation, this));
	// custom validation here
	m_validation_list.push_back(std::bind(&CodeStreamCompress::mct_validation, this));

	/* validation of the parameters codec */
	if(!exec(m_validation_list))
		return false;

	/* customization of the compressing */
	if(!init_header_writing())
		return false;

	/* write header */
	return exec(m_procedure_list);
}
bool CodeStreamCompress::initCompress(grk_cparameters* parameters, GrkImage* image)
{
	if(!parameters || !image)
		return false;

	// sanity check on image
	if(image->numcomps < 1 || image->numcomps > maxNumComponentsJ2K)
	{
		GRK_ERROR("Invalid number of components specified while setting up JP2 compressor");
		return false;
	}
	if((image->x1 < image->x0) || (image->y1 < image->y0))
	{
		GRK_ERROR("Invalid input image dimensions found while setting up JP2 compressor");
		return false;
	}
	for(uint32_t i = 0; i < image->numcomps; ++i)
	{
		auto comp = image->comps + i;
#ifdef GRK_FORCE_SIGNED_COMPRESS
		comp->sgnd = true;
#endif
		if(comp->w == 0 || comp->h == 0)
		{
			GRK_ERROR(
				"Invalid input image component dimensions found while setting up JP2 compressor");
			return false;
		}
		if(comp->prec == 0)
		{
			GRK_ERROR("Invalid component precision of 0 found while setting up JP2 compressor");
			return false;
		}
	}

	// create private sanitized copy of image
	m_headerImage = new GrkImage();
	image->copyHeader(m_headerImage);
	if(image->comps)
	{
		for(uint32_t compno = 0; compno < image->numcomps; compno++)
		{
			if(image->comps[compno].data)
			{
				m_headerImage->comps[compno].data = image->comps[compno].data;
				image->comps[compno].data = nullptr;
			}
		}
	}

	if((parameters->numresolution == 0) || (parameters->numresolution > GRK_J2K_MAXRLVLS))
	{
		GRK_ERROR("Invalid number of resolutions : %u not in range [1,%u]",
				  parameters->numresolution, GRK_J2K_MAXRLVLS);
		return false;
	}

	if(GRK_IS_IMF(parameters->rsiz) && parameters->max_cs_size > 0 && parameters->numlayers == 1 &&
	   parameters->layer_rate[0] == 0)
	{
		parameters->layer_rate[0] = (float)(image->numcomps * image->comps[0].w *
											image->comps[0].h * image->comps[0].prec) /
									(float)(((uint32_t)parameters->max_cs_size) * 8 *
											image->comps[0].dx * image->comps[0].dy);
	}

	/* if no rate entered, lossless by default */
	if(parameters->numlayers == 0)
	{
		parameters->layer_rate[0] = 0;
		parameters->numlayers = 1;
		parameters->allocationByRateDistoration = true;
	}

	/* see if max_codestream_size does limit input rate */
	double image_bytes =
		((double)image->numcomps * image->comps[0].w * image->comps[0].h * image->comps[0].prec) /
		(8 * image->comps[0].dx * image->comps[0].dy);
	if(parameters->max_cs_size == 0)
	{
		if(parameters->numlayers > 0 && parameters->layer_rate[parameters->numlayers - 1] > 0)
		{
			parameters->max_cs_size =
				(uint64_t)floor(image_bytes / parameters->layer_rate[parameters->numlayers - 1]);
		}
	}
	else
	{
		bool cap = false;
		auto min_rate = image_bytes / (double)parameters->max_cs_size;
		for(uint32_t i = 0; i < parameters->numlayers; i++)
		{
			if(parameters->layer_rate[i] < min_rate)
			{
				parameters->layer_rate[i] = min_rate;
				cap = true;
			}
		}
		if(cap)
		{
			GRK_WARN("The desired maximum code stream size has limited\n"
					 "at least one of the desired quality layers");
		}
	}

	/* Manage profiles and applications and set RSIZ */
	/* set cinema parameters if required */
	if(parameters->isHT)
	{
		parameters->rsiz |= GRK_JPH_RSIZ_FLAG;
	}
	if(GRK_IS_CINEMA(parameters->rsiz))
	{
		if((parameters->rsiz == GRK_PROFILE_CINEMA_S2K) ||
		   (parameters->rsiz == GRK_PROFILE_CINEMA_S4K))
		{
			GRK_WARN("JPEG 2000 Scalable Digital Cinema profiles not supported");
			parameters->rsiz = GRK_PROFILE_NONE;
		}
		else
		{
			if(Profile::is_cinema_compliant(image, parameters->rsiz))
				Profile::set_cinema_parameters(parameters, image);
			else
				parameters->rsiz = GRK_PROFILE_NONE;
		}
	}
	else if(GRK_IS_STORAGE(parameters->rsiz))
	{
		GRK_WARN("JPEG 2000 Long Term Storage profile not supported");
		parameters->rsiz = GRK_PROFILE_NONE;
	}
	else if(GRK_IS_BROADCAST(parameters->rsiz))
	{
		Profile::set_broadcast_parameters(parameters);
		if(!Profile::is_broadcast_compliant(parameters, image))
			parameters->rsiz = GRK_PROFILE_NONE;
	}
	else if(GRK_IS_IMF(parameters->rsiz))
	{
		Profile::set_imf_parameters(parameters, image);
		if(!Profile::is_imf_compliant(parameters, image))
			parameters->rsiz = GRK_PROFILE_NONE;
	}
	else if(GRK_IS_PART2(parameters->rsiz))
	{
		if(parameters->rsiz == ((GRK_PROFILE_PART2) | (GRK_EXTENSION_NONE)))
		{
			GRK_WARN("JPEG 2000 Part-2 profile defined\n"
					 "but no Part-2 extension enabled.\n"
					 "Profile set to NONE.");
			parameters->rsiz = GRK_PROFILE_NONE;
		}
		else if(parameters->rsiz != ((GRK_PROFILE_PART2) | (GRK_EXTENSION_MCT)))
		{
			GRK_WARN("Unsupported Part-2 extension enabled\n"
					 "Profile set to NONE.");
			parameters->rsiz = GRK_PROFILE_NONE;
		}
	}

	if(parameters->numpocs)
	{
		if(!validateProgressionOrders(parameters->progression, parameters->numpocs + 1,
									  parameters->numresolution, image->numcomps,
									  parameters->numlayers))
		{
			GRK_ERROR("Failed to initialize POC");
			return false;
		}
	}
	/* set default values for m_cp */
	m_cp.t_grid_width = 1;
	m_cp.t_grid_height = 1;

	m_cp.m_coding_params.m_enc.m_max_comp_size = parameters->max_comp_size;
	m_cp.rsiz = parameters->rsiz;
	m_cp.m_coding_params.m_enc.m_allocationByRateDistortion =
		parameters->allocationByRateDistoration;
	m_cp.m_coding_params.m_enc.m_allocationByFixedQuality = parameters->allocationByQuality;
	m_cp.m_coding_params.m_enc.writePLT = parameters->writePLT;
	m_cp.m_coding_params.m_enc.writeTLM = parameters->writeTLM;
	m_cp.m_coding_params.m_enc.rateControlAlgorithm = parameters->rateControlAlgorithm;

	/* tiles */
	m_cp.t_width = parameters->t_width;
	m_cp.t_height = parameters->t_height;

	/* tile offset */
	m_cp.tx0 = parameters->tx0;
	m_cp.ty0 = parameters->ty0;

	/* comment string */
	if(parameters->num_comments)
	{
		for(size_t i = 0; i < parameters->num_comments; ++i)
		{
			m_cp.comment_len[i] = parameters->comment_len[i];
			if(!m_cp.comment_len[i])
			{
				GRK_WARN("Empty comment. Ignoring");
				continue;
			}
			if(m_cp.comment_len[i] > GRK_MAX_COMMENT_LENGTH)
			{
				GRK_WARN("Comment length %s is greater than maximum comment length %u. Ignoring",
						 m_cp.comment_len[i], GRK_MAX_COMMENT_LENGTH);
				continue;
			}
			m_cp.comment[i] = (char*)new uint8_t[m_cp.comment_len[i]];
			if(!m_cp.comment[i])
			{
				GRK_ERROR("Not enough memory to allocate copy of comment string");
				return false;
			}
			memcpy(m_cp.comment[i], parameters->comment[i], m_cp.comment_len[i]);
			m_cp.isBinaryComment[i] = parameters->is_binary_comment[i];
			m_cp.num_comments++;
		}
	}
	else
	{
		/* Create default comment for code stream */
		const char comment[] = "Created by Grok     version ";
		const size_t clen = strlen(comment);
		const char* version = grk_version();

		m_cp.comment[0] = (char*)new uint8_t[clen + strlen(version) + 1];
		if(!m_cp.comment[0])
		{
			GRK_ERROR("Not enough memory to allocate comment string");
			return false;
		}
		sprintf(m_cp.comment[0], "%s%s", comment, version);
		m_cp.comment_len[0] = (uint16_t)strlen(m_cp.comment[0]);
		m_cp.num_comments = 1;
		m_cp.isBinaryComment[0] = false;
	}
	if(parameters->tile_size_on)
	{
		// avoid divide by zero
		if(m_cp.t_width == 0 || m_cp.t_height == 0)
		{
			GRK_ERROR("Invalid tile dimensions (%u,%u)", m_cp.t_width, m_cp.t_height);
			return false;
		}
		m_cp.t_grid_width  = (uint16_t)ceildiv<uint32_t>((image->x1 - m_cp.tx0), m_cp.t_width);
		m_cp.t_grid_height = (uint16_t)ceildiv<uint32_t>((image->y1 - m_cp.ty0), m_cp.t_height);
	}
	else
	{
		m_cp.t_width = image->x1 - m_cp.tx0;
		m_cp.t_height = image->y1 - m_cp.ty0;
	}
	if(parameters->enableTilePartGeneration)
	{
		m_cp.m_coding_params.m_enc.m_newTilePartProgressionDivider =
			parameters->newTilePartProgressionDivider;
		m_cp.m_coding_params.m_enc.m_enableTilePartGeneration = true;
	}
	uint8_t numgbits = parameters->numgbits;
	if(parameters->numgbits > 7)
	{
		GRK_ERROR("Number of guard bits %d is greater than 7", numgbits);
		return false;
	}
	m_cp.tcps = new TileCodingParams[m_cp.t_grid_width * m_cp.t_grid_height];
	for(uint32_t tileno = 0; tileno < m_cp.t_grid_width * m_cp.t_grid_height; tileno++)
	{
		auto tcp = m_cp.tcps + tileno;
		tcp->tccps = new TileComponentCodingParams[image->numcomps];

		tcp->setIsHT(parameters->isHT, !parameters->irreversible, numgbits);
		tcp->m_qcd->generate((uint32_t)(parameters->numresolution - 1), image->comps[0].prec,
							 parameters->mct > 0, image->comps[0].sgnd);
		for(uint32_t i = 0; i < image->numcomps; i++)
			tcp->m_qcd->pull((tcp->tccps + i)->stepsizes);

		tcp->numlayers = parameters->numlayers;
		for(uint16_t j = 0; j < tcp->numlayers; j++)
		{
			if(m_cp.m_coding_params.m_enc.m_allocationByFixedQuality)
				tcp->distortion[j] = parameters->layer_distortion[j];
			else
				tcp->rates[j] = parameters->layer_rate[j];
		}
		tcp->csty = parameters->csty;
		tcp->prg = parameters->prog_order;
		tcp->mct = parameters->mct;
		if(parameters->numpocs)
		{
			uint32_t numTileProgressions = 0;
			for(uint32_t i = 0; i < parameters->numpocs + 1; i++)
			{
				if(tileno == parameters->progression[i].tileno)
				{
					auto tcp_poc = tcp->progressionOrderChange + numTileProgressions;

					tcp_poc->resS = parameters->progression[numTileProgressions].resS;
					tcp_poc->compS = parameters->progression[numTileProgressions].compS;
					tcp_poc->layE = parameters->progression[numTileProgressions].layE;
					tcp_poc->resE = parameters->progression[numTileProgressions].resE;
					tcp_poc->compE = parameters->progression[numTileProgressions].compE;
					tcp_poc->specifiedCompressionPocProg =
						parameters->progression[numTileProgressions].specifiedCompressionPocProg;
					tcp_poc->tileno = parameters->progression[numTileProgressions].tileno;
					numTileProgressions++;
				}
			}
			if(numTileProgressions == 0)
			{
				GRK_ERROR("Problem with specified progression order changes");
				return false;
			}
			tcp->numpocs = numTileProgressions - 1;
		}
		else
		{
			tcp->numpocs = 0;
		}
		if(parameters->mct_data)
		{
			uint32_t lMctSize =
				(uint32_t)image->numcomps * image->numcomps * (uint32_t)sizeof(float);
			auto lTmpBuf = (float*)grkMalloc(lMctSize);
			auto dc_shift = (int32_t*)((uint8_t*)parameters->mct_data + lMctSize);
			if(!lTmpBuf)
			{
				GRK_ERROR("Not enough memory to allocate temp buffer");
				return false;
			}
			tcp->mct = 2;
			tcp->m_mct_coding_matrix = (float*)grkMalloc(lMctSize);
			if(!tcp->m_mct_coding_matrix)
			{
				grkFree(lTmpBuf);
				lTmpBuf = nullptr;
				GRK_ERROR("Not enough memory to allocate compressor MCT coding matrix ");
				return false;
			}
			memcpy(tcp->m_mct_coding_matrix, parameters->mct_data, lMctSize);
			memcpy(lTmpBuf, parameters->mct_data, lMctSize);

			tcp->m_mct_decoding_matrix = (float*)grkMalloc(lMctSize);
			if(!tcp->m_mct_decoding_matrix)
			{
				grkFree(lTmpBuf);
				lTmpBuf = nullptr;
				GRK_ERROR("Not enough memory to allocate compressor MCT decoding matrix ");
				return false;
			}
			if(GrkMatrix().matrix_inversion_f(lTmpBuf, (tcp->m_mct_decoding_matrix),
											  image->numcomps) == false)
			{
				grkFree(lTmpBuf);
				lTmpBuf = nullptr;
				GRK_ERROR("Failed to inverse compressor MCT decoding matrix ");
				return false;
			}

			tcp->mct_norms = (double*)grkMalloc(image->numcomps * sizeof(double));
			if(!tcp->mct_norms)
			{
				grkFree(lTmpBuf);
				lTmpBuf = nullptr;
				GRK_ERROR("Not enough memory to allocate compressor MCT norms ");
				return false;
			}
			mct::calculate_norms(tcp->mct_norms, image->numcomps, tcp->m_mct_decoding_matrix);
			grkFree(lTmpBuf);

			for(uint32_t i = 0; i < image->numcomps; i++)
			{
				auto tccp = &tcp->tccps[i];
				tccp->m_dc_level_shift = dc_shift[i];
			}

			if(init_mct_encoding(tcp, image) == false)
			{
				GRK_ERROR("Failed to set up j2k mct compressing");
				return false;
			}
		}
		else
		{
			if(tcp->mct == 1)
			{
				if(image->color_space == GRK_CLRSPC_EYCC || image->color_space == GRK_CLRSPC_SYCC)
				{
					GRK_WARN("Disabling MCT for sYCC/eYCC colour space");
					tcp->mct = 0;
				}
				else if(image->numcomps >= 3)
				{
					if((image->comps[0].dx != image->comps[1].dx) ||
					   (image->comps[0].dx != image->comps[2].dx) ||
					   (image->comps[0].dy != image->comps[1].dy) ||
					   (image->comps[0].dy != image->comps[2].dy))
					{
						GRK_WARN("Cannot perform MCT on components with different dimensions. "
								 "Disabling MCT.");
						tcp->mct = 0;
					}
				}
			}
			for(uint32_t i = 0; i < image->numcomps; i++)
			{
				auto tccp = tcp->tccps + i;
				auto comp = image->comps + i;
				if(!comp->sgnd)
					tccp->m_dc_level_shift = 1 << (comp->prec - 1);
			}
		}

		for(uint32_t i = 0; i < image->numcomps; i++)
		{
			auto tccp = &tcp->tccps[i];

			/* 0 => one precinct || 1 => custom precinct  */
			tccp->csty = parameters->csty & J2K_CP_CSTY_PRT;
			tccp->numresolutions = parameters->numresolution;
			tccp->cblkw = floorlog2(parameters->cblockw_init);
			tccp->cblkh = floorlog2(parameters->cblockh_init);
			tccp->cblk_sty = parameters->cblk_sty;
			tccp->qmfbid = parameters->irreversible ? 0 : 1;
			tccp->qntsty = parameters->irreversible ? J2K_CCP_QNTSTY_SEQNT : J2K_CCP_QNTSTY_NOQNT;
			tccp->numgbits = numgbits;
			if((int32_t)i == parameters->roi_compno)
				tccp->roishift = (uint8_t)parameters->roi_shift;
			else
				tccp->roishift = 0;
			if((parameters->csty & J2K_CCP_CSTY_PRT) && parameters->res_spec)
			{
				uint32_t p = 0;
				int32_t it_res;
				assert(tccp->numresolutions > 0);
				for(it_res = (int32_t)tccp->numresolutions - 1; it_res >= 0; it_res--)
				{
					if(p < parameters->res_spec)
					{
						if(parameters->prcw_init[p] < 1)
							tccp->precinctWidthExp[it_res] = 1;
						else
							tccp->precinctWidthExp[it_res] = floorlog2(parameters->prcw_init[p]);
						if(parameters->prch_init[p] < 1)
							tccp->precinctHeightExp[it_res] = 1;
						else
							tccp->precinctHeightExp[it_res] = floorlog2(parameters->prch_init[p]);
					}
					else
					{
						uint32_t res_spec = parameters->res_spec;
						uint32_t size_prcw = 0;
						uint32_t size_prch = 0;
						size_prcw = parameters->prcw_init[res_spec - 1] >> (p - (res_spec - 1));
						size_prch = parameters->prch_init[res_spec - 1] >> (p - (res_spec - 1));
						if(size_prcw < 1)
							tccp->precinctWidthExp[it_res] = 1;
						else
							tccp->precinctWidthExp[it_res] = floorlog2(size_prcw);
						if(size_prch < 1)
							tccp->precinctHeightExp[it_res] = 1;
						else
							tccp->precinctHeightExp[it_res] = floorlog2(size_prch);
					}
					p++;
					/*printf("\nsize precinct for level %u : %u,%u\n",
					 * it_res,tccp->precinctWidthExp[it_res], tccp->precinctHeightExp[it_res]); */
				} /*end for*/
			}
			else
			{
				for(uint32_t j = 0; j < tccp->numresolutions; j++)
				{
					tccp->precinctWidthExp[j] = 15;
					tccp->precinctHeightExp[j] = 15;
				}
			}
		}
	}
	grkFree(parameters->mct_data);
	parameters->mct_data = nullptr;

	return true;
}
bool CodeStreamCompress::compress(grk_plugin_tile* tile)
{
	MinHeap<TileProcessor, uint16_t> heap;
	uint32_t numTiles = (uint32_t)m_cp.t_grid_height * m_cp.t_grid_width;
	if(numTiles > maxNumTilesJ2K)
	{
		GRK_ERROR("Number of tiles %u is greater than max tiles %u"
				  "allowed by the standard.",
				  numTiles, maxNumTilesJ2K);
		return false;
	}
	auto pool_size = std::min<uint32_t>((uint32_t)ThreadPool::get()->num_threads(), numTiles);
	ThreadPool pool(pool_size);
	std::vector<std::future<int>> results;
	std::atomic<bool> success(true);
	if(pool_size > 1)
	{
		for(uint16_t i = 0; i < numTiles; ++i)
		{
			uint16_t tileIndex = i;
			results.emplace_back(pool.enqueue([this, tile, tileIndex, &heap, &success] {
				if(success)
				{
					auto tileProcessor = new TileProcessor(tileIndex,this, m_stream, true, false);
					tileProcessor->current_plugin_tile = tile;
					if(!tileProcessor->preCompressTile() || !tileProcessor->doCompress())
						success = false;
					heap.push(tileProcessor);
				}
				return 0;
			}));
		}
	}
	else
	{
		for(uint16_t i = 0; i < numTiles; ++i)
		{
			auto tileProcessor = new TileProcessor(i,this, m_stream, true, false);
			tileProcessor->current_plugin_tile = tile;
			if(!tileProcessor->preCompressTile() || !tileProcessor->doCompress())
			{
				delete tileProcessor;
				success = false;
				goto cleanup;
			}
			bool write_success = writeTileParts(tileProcessor);
			delete tileProcessor;
			if(!write_success)
			{
				success = false;
				goto cleanup;
			}
		}
	}
	if(pool_size > 1)
	{
		for(auto& result : results)
		{
			result.get();
		}
	}
cleanup:
	auto completeTileProcessor = heap.pop();
	while(completeTileProcessor)
	{
		if(success)
		{
			if(!writeTileParts(completeTileProcessor))
				success = false;
		}
		delete completeTileProcessor;
		completeTileProcessor = heap.pop();
	}

	return success;
}
bool CodeStreamCompress::compressTile(uint16_t tileIndex, uint8_t* p_data,
									  uint64_t uncompressed_data_size)
{
	if(!p_data)
		return false;
	bool rc = false;

	auto currentTileProcessor = new TileProcessor(tileIndex,this, m_stream, true, false);
	if(!currentTileProcessor->preCompressTile())
	{
		GRK_ERROR("Error while preCompressTile with tile index = %u", tileIndex);
		goto cleanup;
	}
	/* now copy data into the tile component */
	if(!currentTileProcessor->ingestUncompressedData(p_data, uncompressed_data_size))
	{
		GRK_ERROR("Size mismatch between tile data and sent data.");
		goto cleanup;
	}
	if(!currentTileProcessor->doCompress())
		goto cleanup;
	if(!writeTileParts(currentTileProcessor))
	{
		GRK_ERROR("Error while j2k_post_write_tile with tile index = %u", tileIndex);
		goto cleanup;
	}
	rc = true;
cleanup:
	delete currentTileProcessor;

	return rc;
}
bool CodeStreamCompress::endCompress(void)
{
	/* customization of the compressing */
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_eoc, this));
	if(m_cp.m_coding_params.m_enc.writeTLM)
		m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_tlm_end, this));

	return exec(m_procedure_list);
}
///////////////////////////////////////////////////////////////////////////////////////////
bool CodeStreamCompress::write_rgn(uint16_t tile_no, uint32_t comp_no, uint32_t nb_comps)
{
	uint32_t rgn_size;
	auto cp = &(m_cp);
	auto tcp = &cp->tcps[tile_no];
	auto tccp = &tcp->tccps[comp_no];
	uint32_t comp_room = (nb_comps <= 256) ? 1 : 2;
	rgn_size = 6 + comp_room;

	/* RGN  */
	if(!m_stream->writeShort(J2K_MS_RGN))
		return false;
	/* Lrgn */
	if(!m_stream->writeShort((uint16_t)(rgn_size - 2)))
		return false;
	/* Crgn */
	if(comp_room == 2)
	{
		if(!m_stream->writeShort((uint16_t)comp_no))
			return false;
	}
	else
	{
		if(!m_stream->writeByte((uint8_t)comp_no))
			return false;
	}
	/* Srgn */
	if(!m_stream->writeByte(0))
		return false;

	/* SPrgn */
	return m_stream->writeByte((uint8_t)tccp->roishift);
}

bool CodeStreamCompress::write_eoc()
{
	if(!m_stream->writeShort(J2K_MS_EOC))
		return false;

	return m_stream->flush();
}
bool CodeStreamCompress::write_mct_record(grk_mct_data* p_mct_record, IBufferedStream* stream)
{
	uint32_t mct_size;
	uint32_t tmp;

	mct_size = 10 + p_mct_record->m_data_size;

	/* MCT */
	if(!stream->writeShort(J2K_MS_MCT))
		return false;
	/* Lmct */
	if(!stream->writeShort((uint16_t)(mct_size - 2)))
		return false;
	/* Zmct */
	if(!stream->writeShort(0))
		return false;
	/* only one marker atm */
	tmp = (p_mct_record->m_index & 0xff) | (uint32_t)(p_mct_record->m_array_type << 8) |
		  (uint32_t)(p_mct_record->m_element_type << 10);

	if(!stream->writeShort((uint16_t)tmp))
		return false;

	/* Ymct */
	if(!stream->writeShort(0))
		return false;

	return stream->writeBytes(p_mct_record->m_data, p_mct_record->m_data_size);
}
bool CodeStreamCompress::get_end_header(void)
{
	codeStreamInfo->setMainHeaderEnd(m_stream->tell());

	return true;
}
bool CodeStreamCompress::init_header_writing(void)
{
	m_procedure_list.push_back([this] {
		return getNumTileParts(&m_compressorState.m_total_tile_parts, getHeaderImage());
	});

	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_soc, this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_siz, this));
	if(m_cp.tcps[0].isHT())
		m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_cap, this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_cod, this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_qcd, this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_all_coc, this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_all_qcc, this));

	if(m_cp.m_coding_params.m_enc.writeTLM)
		m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_tlm_begin, this));
	if(m_cp.tcps->hasPoc())
		m_procedure_list.push_back(std::bind(&CodeStreamCompress::writePoc, this));

	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_regions, this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_com, this));
	// begin custom procedures
	if((m_cp.rsiz & (GRK_PROFILE_PART2 | GRK_EXTENSION_MCT)) ==
	   (GRK_PROFILE_PART2 | GRK_EXTENSION_MCT))
		m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_mct_data_group, this));
	// end custom procedures

	if(codeStreamInfo)
		m_procedure_list.push_back(std::bind(&CodeStreamCompress::get_end_header, this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::updateRates, this));

	return true;
}
bool CodeStreamCompress::writeTilePart(TileProcessor* tileProcessor)
{
	uint64_t currentPos = 0;
	if(tileProcessor->canPreCalculateTileLen())
		currentPos = m_stream->tell();
	uint16_t currentTileIndex = tileProcessor->getIndex();
	auto calculatedBytesWritten = tileProcessor->getPreCalculatedTileLen();
	// 1. write SOT
	SOTMarker sot;
	if(!sot.write(tileProcessor, calculatedBytesWritten))
		return false;
	uint32_t tilePartBytesWritten = sot_marker_segment_len;
	// 2. write POC marker to first tile part
	if(tileProcessor->canWritePocMarker())
	{
		if(!writePoc())
			return false;
		auto tcp = m_cp.tcps + currentTileIndex;
		tilePartBytesWritten += getPocSize(m_headerImage->numcomps, tcp->getNumProgressions());
	}
	// 3. compress tile part and write to stream
	if(!tileProcessor->writeTilePartT2(&tilePartBytesWritten))
	{
		GRK_ERROR("Cannot compress tile");
		return false;
	}
	// 4. now that we know the tile part length, we can
	// write the Psot in the SOT marker
	if(!sot.write_psot(m_stream, tilePartBytesWritten))
		return false;
	// 5. update TLM
	if(tileProcessor->canPreCalculateTileLen())
	{
		auto actualBytes = m_stream->tell() - currentPos;
		// GRK_INFO("Tile %d: precalculated / actual : %d / %d",
		//		tileProcessor->getIndex(), calculatedBytesWritten, actualBytes);
		if(actualBytes != calculatedBytesWritten)
		{
			GRK_ERROR("Error in tile length calculation. Please share uncompressed image\n"
					  "and compression parameters on Github issue tracker");
			return false;
		}
		tilePartBytesWritten = calculatedBytesWritten;
	}
	if(m_cp.tlm_markers)
		m_cp.tlm_markers->push(currentTileIndex, tilePartBytesWritten);
	++tileProcessor->m_tilePartIndexCounter;

	return true;
}
bool CodeStreamCompress::writeTileParts(TileProcessor* tileProcessor)
{
	m_currentTileProcessor = tileProcessor;
	assert(tileProcessor->m_tilePartIndexCounter == 0);
	// 1. write first tile part
	tileProcessor->pino = 0;
	tileProcessor->m_first_poc_tile_part = true;
	if(!writeTilePart(tileProcessor))
		return false;
	// 2. write the other tile parts
	uint32_t pino;
	auto tcp = m_cp.tcps + tileProcessor->getIndex();
	// write tile parts for first progression order
	uint64_t numTileParts = getNumTilePartsForProgression(0, tileProcessor->getIndex());
	if(numTileParts > maxTilePartsPerTileJ2K)
	{
		GRK_ERROR("Number of tile parts %d for first POC exceeds maximum number of tile parts %d",
				  numTileParts, maxTilePartsPerTileJ2K);
		return false;
	}
	tileProcessor->m_first_poc_tile_part = false;
	for(uint8_t tilepartno = 1; tilepartno < (uint8_t)numTileParts; ++tilepartno)
	{
		if(!writeTilePart(tileProcessor))
			return false;
	}
	// write tile parts for remaining progression orders
	for(pino = 1; pino < tcp->getNumProgressions(); ++pino)
	{
		tileProcessor->pino = pino;
		numTileParts = getNumTilePartsForProgression(pino, tileProcessor->getIndex());
		if(numTileParts > maxTilePartsPerTileJ2K)
		{
			GRK_ERROR("Number of tile parts %d exceeds maximum number of "
					  "tile parts %d",
					  numTileParts, maxTilePartsPerTileJ2K);
			return false;
		}
		for(uint8_t tilepartno = 0; tilepartno < numTileParts; ++tilepartno)
		{
			tileProcessor->m_first_poc_tile_part = (tilepartno == 0);
			if(!writeTilePart(tileProcessor))
				return false;
		}
	}
	tileProcessor->incrementIndex();

	return true;
}
bool CodeStreamCompress::updateRates(void)
{
	auto cp = &(m_cp);
	auto image = m_headerImage;
	auto tcp = cp->tcps;
	auto width = image->x1 - image->x0;
	auto height = image->y1 - image->y0;
	if(width <= 0 || height <= 0)
		return false;

	uint32_t bits_empty = 8 * image->comps->dx * image->comps->dy;
	uint32_t size_pixel = (uint32_t)image->numcomps * image->comps->prec;
	auto header_size = (double)m_stream->tell();

	for(uint32_t tile_y = 0; tile_y < cp->t_grid_height; ++tile_y)
	{
		for(uint32_t tile_x = 0; tile_x < cp->t_grid_width; ++tile_x)
		{
			double stride = 0;
			if(cp->m_coding_params.m_enc.m_enableTilePartGeneration)
				stride = (tcp->m_numTileParts - 1) * 14;
			double offset = stride / tcp->numlayers;
			auto tileBounds = cp->getTileBounds(image, tile_x, tile_y);
			uint64_t numTilePixels = tileBounds.area();
			for(uint16_t k = 0; k < tcp->numlayers; ++k)
			{
				double* rates = tcp->rates + k;
				if(*rates > 0.0f)
					*rates = ((((double)size_pixel * (double)numTilePixels)) /
							  ((double)*rates * (double)bits_empty)) -
							 offset;
			}
			++tcp;
		}
	}
	tcp = cp->tcps;

	for(uint32_t tile_y = 0; tile_y < cp->t_grid_height; ++tile_y)
	{
		for(uint32_t tile_x = 0; tile_x < cp->t_grid_width; ++tile_x)
		{
			double* rates = tcp->rates;

			auto tileBounds = cp->getTileBounds(image, tile_x, tile_y);
			uint64_t numTilePixels = tileBounds.area();

			double sot_adjust =
				((double)numTilePixels * (double)header_size) / ((double)width * height);
			if(*rates > 0.0)
			{
				*rates -= sot_adjust;
				if(*rates < 30.0f)
					*rates = 30.0f;
			}
			++rates;
			for(uint16_t k = 1; k < (uint16_t)(tcp->numlayers - 1); ++k)
			{
				if(*rates > 0.0)
				{
					*rates -= sot_adjust;
					if(*rates < *(rates - 1) + 10.0)
						*rates = (*(rates - 1)) + 20.0;
				}
				++rates;
			}
			if(*rates > 0.0)
			{
				*rates -= (sot_adjust + 2.0);
				if(*rates < *(rates - 1) + 10.0)
					*rates = (*(rates - 1)) + 20.0;
			}
			++tcp;
		}
	}

	return true;
}
bool CodeStreamCompress::compressValidation()
{
	bool is_valid = true;

	/* ISO 15444-1:2004 states between 1 & 33
	 * ergo (number of decomposition levels between 0 -> 32) */
	if((m_cp.tcps->tccps->numresolutions == 0) ||
	   (m_cp.tcps->tccps->numresolutions > GRK_J2K_MAXRLVLS))
	{
		GRK_ERROR("Invalid number of resolutions : %u not in range [1,%u]",
				  m_cp.tcps->tccps->numresolutions, GRK_J2K_MAXRLVLS);
		return false;
	}
	if(m_cp.t_width == 0)
	{
		GRK_ERROR("Tile x dimension must be greater than zero ");
		return false;
	}
	if(m_cp.t_height == 0)
	{
		GRK_ERROR("Tile y dimension must be greater than zero ");
		return false;
	}

	return is_valid;
}
bool CodeStreamCompress::write_soc()
{
	return m_stream->writeShort(J2K_MS_SOC);
}
bool CodeStreamCompress::write_siz()
{
	SIZMarker siz;

	return siz.write(this, m_stream);
}
bool CodeStreamCompress::write_cap()
{
	return m_cp.tcps->m_qcd->write(m_stream);
}

bool CodeStreamCompress::write_com()
{
	for(uint32_t i = 0; i < m_cp.num_comments; ++i)
	{
		const char* comment = m_cp.comment[i];
		uint16_t comment_size = m_cp.comment_len[i];
		if(!comment_size)
		{
			GRK_WARN("Empty comment. Ignoring");
			continue;
		}
		if(comment_size > GRK_MAX_COMMENT_LENGTH)
		{
			GRK_WARN("Comment length %s is greater than maximum comment length %u. Ignoring",
					 comment_size, GRK_MAX_COMMENT_LENGTH);
			continue;
		}
		uint32_t totacom_size = (uint32_t)comment_size + 6;

		/* COM */
		if(!m_stream->writeShort(J2K_MS_COM))
			return false;
		/* L_COM */
		if(!m_stream->writeShort((uint16_t)(totacom_size - 2)))
			return false;
		if(!m_stream->writeShort(m_cp.isBinaryComment[i] ? 0 : 1))
			return false;
		if(!m_stream->writeBytes((uint8_t*)comment, comment_size))
			return false;
	}

	return true;
}
bool CodeStreamCompress::write_cod()
{
	uint32_t code_size;
	auto tcp = m_cp.tcps;
	code_size = 9 + get_SPCod_SPCoc_size(0);

	/* COD */
	if(!m_stream->writeShort(J2K_MS_COD))
		return false;
	/* L_COD */
	if(!m_stream->writeShort((uint16_t)(code_size - 2)))
		return false;
	/* Scod */
	if(!m_stream->writeByte((uint8_t)tcp->csty))
		return false;
	/* SGcod (A) */
	if(!m_stream->writeByte((uint8_t)tcp->prg))
		return false;
	/* SGcod (B) */
	if(!m_stream->writeShort((uint16_t)tcp->numlayers))
		return false;
	/* SGcod (C) */
	if(!m_stream->writeByte((uint8_t)tcp->mct))
		return false;
	if(!write_SPCod_SPCoc(0))
	{
		GRK_ERROR("Error writing COD marker");
		return false;
	}

	return true;
}
bool CodeStreamCompress::write_coc(uint32_t comp_no)
{
	uint32_t coc_size;
	uint32_t comp_room;
	auto tcp = m_cp.tcps;
	auto image = getHeaderImage();
	comp_room = (image->numcomps <= 256) ? 1 : 2;
	coc_size = cod_coc_len + comp_room + get_SPCod_SPCoc_size(comp_no);

	/* COC */
	if(!m_stream->writeShort(J2K_MS_COC))
		return false;
	/* L_COC */
	if(!m_stream->writeShort((uint16_t)(coc_size - 2)))
		return false;
	/* Ccoc */
	if(comp_room == 2)
	{
		if(!m_stream->writeShort((uint16_t)comp_no))
			return false;
	}
	else
	{
		if(!m_stream->writeByte((uint8_t)comp_no))
			return false;
	}

	/* Scoc */
	if(!m_stream->writeByte((uint8_t)tcp->tccps[comp_no].csty))
		return false;

	return write_SPCod_SPCoc(0);
}
bool CodeStreamCompress::compare_coc(uint32_t first_comp_no, uint32_t second_comp_no)
{
	auto tcp = m_cp.tcps;

	if(tcp->tccps[first_comp_no].csty != tcp->tccps[second_comp_no].csty)
		return false;

	return compare_SPCod_SPCoc(first_comp_no, second_comp_no);
}

bool CodeStreamCompress::write_qcd()
{
	uint32_t qcd_size;
	qcd_size = 4 + get_SQcd_SQcc_size(0);

	/* QCD */
	if(!m_stream->writeShort(J2K_MS_QCD))
		return false;
	/* L_QCD */
	if(!m_stream->writeShort((uint16_t)(qcd_size - 2)))
		return false;
	if(!write_SQcd_SQcc(0))
	{
		GRK_ERROR("Error writing QCD marker");
		return false;
	}

	return true;
}
bool CodeStreamCompress::write_qcc(uint32_t comp_no)
{
	uint32_t qcc_size = 6 + get_SQcd_SQcc_size(comp_no);

	/* QCC */
	if(!m_stream->writeShort(J2K_MS_QCC))
	{
		return false;
	}

	if(getHeaderImage()->numcomps <= 256)
	{
		--qcc_size;

		/* L_QCC */
		if(!m_stream->writeShort((uint16_t)(qcc_size - 2)))
			return false;
		/* Cqcc */
		if(!m_stream->writeByte((uint8_t)comp_no))
			return false;
	}
	else
	{
		/* L_QCC */
		if(!m_stream->writeShort((uint16_t)(qcc_size - 2)))
			return false;
		/* Cqcc */
		if(!m_stream->writeShort((uint16_t)comp_no))
			return false;
	}

	return write_SQcd_SQcc(comp_no);
}
bool CodeStreamCompress::compare_qcc(uint32_t first_comp_no, uint32_t second_comp_no)
{
	return compare_SQcd_SQcc(first_comp_no, second_comp_no);
}
bool CodeStreamCompress::writePoc()
{
	auto tcp = m_cp.tcps;
	auto tccp = tcp->tccps;
	auto image = getHeaderImage();
	uint16_t numComps = image->numcomps;
	uint32_t numPocs = tcp->getNumProgressions();
	uint32_t pocRoom = (numComps <= 256) ? 1 : 2;

	auto poc_size = getPocSize(numComps, numPocs);

	/* POC  */
	if(!m_stream->writeShort(J2K_MS_POC))
		return false;

	/* Lpoc */
	if(!m_stream->writeShort((uint16_t)(poc_size - 2)))
		return false;

	for(uint32_t i = 0; i < numPocs; ++i)
	{
		auto current_prog = tcp->progressionOrderChange + i;
		/* RSpoc_i */
		if(!m_stream->writeByte(current_prog->resS))
			return false;
		/* CSpoc_i */
		if(pocRoom == 2)
		{
			if(!m_stream->writeShort(current_prog->compS))
				return false;
		}
		else
		{
			if(!m_stream->writeByte((uint8_t)current_prog->compS))
				return false;
		}
		/* LYEpoc_i */
		if(!m_stream->writeShort(current_prog->layE))
			return false;
		/* REpoc_i */
		if(!m_stream->writeByte(current_prog->resE))
			return false;
		/* CEpoc_i */
		if(pocRoom == 2)
		{
			if(!m_stream->writeShort(current_prog->compE))
				return false;
		}
		else
		{
			if(!m_stream->writeByte((uint8_t)current_prog->compE))
				return false;
		}
		/* Ppoc_i */
		if(!m_stream->writeByte((uint8_t)current_prog->progression))
			return false;

		/* change the value of the max layer according to the actual number of layers in the file,
		 * components and resolutions*/
		current_prog->layE = std::min<uint16_t>(current_prog->layE, tcp->numlayers);
		current_prog->resE = std::min<uint8_t>(current_prog->resE, tccp->numresolutions);
		current_prog->compE = std::min<uint16_t>(current_prog->compE, numComps);
	}

	return true;
}

bool CodeStreamCompress::write_mct_data_group()
{
	if(!write_cbd())
		return false;

	auto tcp = m_cp.tcps;
	auto mct_record = tcp->m_mct_records;

	for(uint32_t i = 0; i < tcp->m_nb_mct_records; ++i)
	{
		if(!write_mct_record(mct_record, m_stream))
			return false;
		++mct_record;
	}

	auto mcc_record = tcp->m_mcc_records;
	for(uint32_t i = 0; i < tcp->m_nb_mcc_records; ++i)
	{
		if(!write_mcc_record(mcc_record, m_stream))
			return false;
		++mcc_record;
	}

	return write_mco();
}
bool CodeStreamCompress::write_all_coc()
{
	for(uint16_t compno = 1; compno < getHeaderImage()->numcomps; ++compno)
	{
		/* cod is first component of first tile */
		if(!compare_coc(0, compno))
		{
			if(!write_coc(compno))
				return false;
		}
	}

	return true;
}
bool CodeStreamCompress::write_all_qcc()
{
	for(uint16_t compno = 1; compno < getHeaderImage()->numcomps; ++compno)
	{
		/* qcd is first component of first tile */
		if(!compare_qcc(0, compno))
		{
			if(!write_qcc(compno))
				return false;
		}
	}
	return true;
}
bool CodeStreamCompress::write_regions()
{
	for(uint16_t compno = 0; compno < getHeaderImage()->numcomps; ++compno)
	{
		auto tccp = m_cp.tcps->tccps + compno;
		if(tccp->roishift)
		{
			if(!write_rgn(0, compno, getHeaderImage()->numcomps))
				return false;
		}
	}

	return true;
}
bool CodeStreamCompress::write_mcc_record(grk_simple_mcc_decorrelation_data* p_mcc_record,
										  IBufferedStream* stream)
{
	uint32_t i;
	uint32_t mcc_size;
	uint32_t nb_bytes_for_comp;
	uint32_t mask;
	uint32_t tmcc;

	assert(stream != nullptr);

	if(p_mcc_record->m_nb_comps > 255)
	{
		nb_bytes_for_comp = 2;
		mask = 0x8000;
	}
	else
	{
		nb_bytes_for_comp = 1;
		mask = 0;
	}

	mcc_size = p_mcc_record->m_nb_comps * 2 * nb_bytes_for_comp + 19;

	/* MCC */
	if(!stream->writeShort(J2K_MS_MCC))
		return false;
	/* Lmcc */
	if(!stream->writeShort((uint16_t)(mcc_size - 2)))
		return false;
	/* first marker */
	/* Zmcc */
	if(!stream->writeShort(0))
		return false;
	/* Imcc -> no need for other values, take the first */
	if(!stream->writeByte((uint8_t)p_mcc_record->m_index))
		return false;
	/* only one marker atm */
	/* Ymcc */
	if(!stream->writeShort(0))
		return false;
	/* Qmcc -> number of collections -> 1 */
	if(!stream->writeShort(1))
		return false;
	/* Xmcci type of component transformation -> array based decorrelation */
	if(!stream->writeByte(0x1))
		return false;
	/* Nmcci number of input components involved and size for each component offset = 8 bits */
	if(!stream->writeShort((uint16_t)(p_mcc_record->m_nb_comps | mask)))
		return false;

	for(i = 0; i < p_mcc_record->m_nb_comps; ++i)
	{
		/* Cmccij Component offset*/
		if(nb_bytes_for_comp == 2)
		{
			if(!stream->writeShort((uint16_t)i))
				return false;
		}
		else
		{
			if(!stream->writeByte((uint8_t)i))
				return false;
		}
	}

	/* Mmcci number of output components involved and size for each component offset = 8 bits */
	if(!stream->writeShort((uint16_t)(p_mcc_record->m_nb_comps | mask)))
		return false;

	for(i = 0; i < p_mcc_record->m_nb_comps; ++i)
	{
		/* Wmccij Component offset*/
		if(nb_bytes_for_comp == 2)
		{
			if(!stream->writeShort((uint16_t)i))
				return false;
		}
		else
		{
			if(!stream->writeByte((uint8_t)i))
				return false;
		}
	}

	tmcc = ((uint32_t)((!p_mcc_record->m_is_irreversible) & 1U)) << 16;

	if(p_mcc_record->m_decorrelation_array)
		tmcc |= p_mcc_record->m_decorrelation_array->m_index;

	if(p_mcc_record->m_offset_array)
		tmcc |= ((p_mcc_record->m_offset_array->m_index) << 8);

	/* Tmcci : use MCT defined as number 1 and irreversible array based. */
	return stream->write24(tmcc);
}
bool CodeStreamCompress::write_mco()
{
	uint32_t mco_size;
	uint32_t i;
	auto tcp = &(m_cp.tcps[0]);
	mco_size = 5 + tcp->m_nb_mcc_records;

	/* MCO */
	if(!m_stream->writeShort(J2K_MS_MCO))
		return false;

	/* Lmco */
	if(!m_stream->writeShort((uint16_t)(mco_size - 2)))
		return false;

	/* Nmco : only one transform stage*/
	if(!m_stream->writeByte((uint8_t)tcp->m_nb_mcc_records))
		return false;

	auto mcc_record = tcp->m_mcc_records;
	for(i = 0; i < tcp->m_nb_mcc_records; ++i)
	{
		/* Imco -> use the mcc indicated by 1*/
		if(!m_stream->writeByte((uint8_t)mcc_record->m_index))
			return false;
		++mcc_record;
	}
	return true;
}

bool CodeStreamCompress::write_cbd()
{
	uint32_t i;
	auto image = getHeaderImage();
	uint16_t cbd_size = (uint16_t)(6U + getHeaderImage()->numcomps);

	/* CBD */
	if(!m_stream->writeShort(J2K_MS_CBD))
		return false;

	/* L_CBD */
	if(!m_stream->writeShort((uint16_t)(cbd_size - 2U)))
		return false;

	/* Ncbd */
	if(!m_stream->writeShort(image->numcomps))
		return false;

	for(i = 0; i < image->numcomps; ++i)
	{
		auto comp = image->comps + i;
		/* Component bit depth */
		uint8_t bpc = (uint8_t)(comp->prec - 1);
		if(comp->sgnd)
			bpc = (uint8_t)(bpc + (1 << 7));
		if(!m_stream->writeByte(bpc))
			return false;
	}
	return true;
}

bool CodeStreamCompress::write_tlm_begin()
{
	if(!m_cp.tlm_markers)
		m_cp.tlm_markers = new TileLengthMarkers(m_stream);

	return m_cp.tlm_markers->writeBegin(m_compressorState.m_total_tile_parts);
}
bool CodeStreamCompress::write_tlm_end()
{
	return m_cp.tlm_markers->writeEnd();
}
uint32_t CodeStreamCompress::get_SPCod_SPCoc_size(uint32_t comp_no)
{
	auto tcp = m_cp.tcps;
	auto tccp = tcp->tccps + comp_no;
	assert(comp_no < getHeaderImage()->numcomps);

	uint32_t rc = SPCod_SPCoc_len;
	if(tccp->csty & J2K_CCP_CSTY_PRT)
		rc += tccp->numresolutions;

	return rc;
}
bool CodeStreamCompress::compare_SPCod_SPCoc(uint32_t first_comp_no, uint32_t second_comp_no)
{
	auto tcp = m_cp.tcps;
	auto tccp0 = tcp->tccps + first_comp_no;
	auto tccp1 = tcp->tccps + second_comp_no;

	if(tccp0->numresolutions != tccp1->numresolutions)
		return false;
	if(tccp0->cblkw != tccp1->cblkw)
		return false;
	if(tccp0->cblkh != tccp1->cblkh)
		return false;
	if(tccp0->cblk_sty != tccp1->cblk_sty)
		return false;
	if(tccp0->qmfbid != tccp1->qmfbid)
		return false;
	if((tccp0->csty & J2K_CCP_CSTY_PRT) != (tccp1->csty & J2K_CCP_CSTY_PRT))
		return false;
	for(uint32_t i = 0U; i < tccp0->numresolutions; ++i)
	{
		if(tccp0->precinctWidthExp[i] != tccp1->precinctWidthExp[i])
			return false;
		if(tccp0->precinctHeightExp[i] != tccp1->precinctHeightExp[i])
			return false;
	}

	return true;
}

bool CodeStreamCompress::write_SPCod_SPCoc(uint32_t comp_no)
{
	auto tcp = m_cp.tcps;
	auto tccp = tcp->tccps + comp_no;

	assert(comp_no < (getHeaderImage()->numcomps));

	/* SPcoc (D) */
	if(!m_stream->writeByte((uint8_t)(tccp->numresolutions - 1)))
		return false;
	/* SPcoc (E) */
	if(!m_stream->writeByte((uint8_t)(tccp->cblkw - 2)))
		return false;
	/* SPcoc (F) */
	if(!m_stream->writeByte((uint8_t)(tccp->cblkh - 2)))
		return false;
	/* SPcoc (G) */
	if(!m_stream->writeByte(tccp->cblk_sty))
		return false;
	/* SPcoc (H) */
	if(!m_stream->writeByte((uint8_t)tccp->qmfbid))
		return false;

	if(tccp->csty & J2K_CCP_CSTY_PRT)
	{
		for(uint32_t i = 0; i < tccp->numresolutions; ++i)
		{
			/* SPcoc (I_i) */
			if(!m_stream->writeByte(
				   (uint8_t)(tccp->precinctWidthExp[i] + (tccp->precinctHeightExp[i] << 4))))
			{
				return false;
			}
		}
	}

	return true;
}
uint32_t CodeStreamCompress::get_SQcd_SQcc_size(uint32_t comp_no)
{
	auto tcp = m_cp.tcps;
	auto tccp = tcp->tccps + comp_no;
	assert(comp_no < getHeaderImage()->numcomps);

	uint32_t num_bands =
		(tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ? 1 : (tccp->numresolutions * 3U - 2);

	return (tccp->qntsty == J2K_CCP_QNTSTY_NOQNT) ? 1 + num_bands : 1 + 2 * num_bands;
}
bool CodeStreamCompress::compare_SQcd_SQcc(uint32_t first_comp_no, uint32_t second_comp_no)
{
	auto tcp = m_cp.tcps;
	auto tccp0 = tcp->tccps + first_comp_no;
	auto tccp1 = tcp->tccps + second_comp_no;

	if(tccp0->qntsty != tccp1->qntsty)
		return false;
	if(tccp0->numgbits != tccp1->numgbits)
		return false;
	uint32_t band_no, num_bands;
	if(tccp0->qntsty == J2K_CCP_QNTSTY_SIQNT)
	{
		num_bands = 1U;
	}
	else
	{
		num_bands = tccp0->numresolutions * 3U - 2U;
		if(num_bands != (tccp1->numresolutions * 3U - 2U))
			return false;
	}
	for(band_no = 0; band_no < num_bands; ++band_no)
	{
		if(tccp0->stepsizes[band_no].expn != tccp1->stepsizes[band_no].expn)
			return false;
	}
	if(tccp0->qntsty != J2K_CCP_QNTSTY_NOQNT)
	{
		for(band_no = 0; band_no < num_bands; ++band_no)
		{
			if(tccp0->stepsizes[band_no].mant != tccp1->stepsizes[band_no].mant)
				return false;
		}
	}

	return true;
}
bool CodeStreamCompress::write_SQcd_SQcc(uint32_t comp_no)
{
	assert(comp_no < getHeaderImage()->numcomps);
	auto tcp = m_cp.tcps;
	auto tccp = tcp->tccps + comp_no;
	uint32_t num_bands =
		(tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ? 1 : (tccp->numresolutions * 3U - 2);

	/* Sqcx */
	if(!m_stream->writeByte((uint8_t)(tccp->qntsty + (tccp->numgbits << 5))))
		return false;

	/* SPqcx_i */
	for(uint32_t band_no = 0; band_no < num_bands; ++band_no)
	{
		uint32_t expn = tccp->stepsizes[band_no].expn;
		uint32_t mant = tccp->stepsizes[band_no].mant;
		if(tccp->qntsty == J2K_CCP_QNTSTY_NOQNT)
		{
			if(!m_stream->writeByte((uint8_t)(expn << 3)))
				return false;
		}
		else
		{
			if(!m_stream->writeShort((uint16_t)((expn << 11) + mant)))
				return false;
		}
	}
	return true;
}
uint16_t CodeStreamCompress::getPocSize(uint32_t numComps, uint32_t numPocs)
{
	uint32_t pocRoom = (numComps <= 256) ? 1 : 2;

	return (uint16_t)(4 + (5 + 2 * pocRoom) * numPocs);
}
bool CodeStreamCompress::validateProgressionOrders(const grk_progression* progressions,
												   uint32_t numProgressions, uint8_t numResolutions,
												   uint16_t numComps, uint16_t numLayers)
{
	uint32_t resno, compno, layno;
	uint32_t i;
	uint32_t step_c = 1;
	uint32_t step_r = numComps * step_c;
	uint32_t step_l = numResolutions * step_r;

	auto packet_array = new uint8_t[(size_t)step_l * numLayers];
	memset(packet_array, 0, (size_t)step_l * numLayers * sizeof(uint8_t));

	/* iterate through all the pocs */
	for(i = 0; i < numProgressions; ++i)
	{
		auto currentPoc = progressions + i;
		size_t index = step_r * currentPoc->resS;
		/* take each resolution for each poc */
		for(resno = currentPoc->resS; resno < std::min<uint32_t>(currentPoc->resE, numResolutions);
			++resno)
		{
			size_t res_index = index + currentPoc->compS * step_c;

			/* take each comp of each resolution for each poc */
			for(compno = currentPoc->compS;
				compno < std::min<uint32_t>(currentPoc->compE, numComps); ++compno)
			{
				size_t comp_index = res_index + 0 * step_l;

				/* and finally take each layer of each res of ... */
				for(layno = 0; layno < std::min<uint32_t>(currentPoc->layE, numLayers); ++layno)
				{
					/*index = step_r * resno + step_c * compno + step_l * layno;*/
					packet_array[comp_index] = 1;
					// printf("%d %d\n",i,comp_index);
					comp_index += step_l;
				}
				res_index += step_c;
			}
			index += step_r;
		}
	}
	bool loss = false;
	size_t index = 0;
	for(layno = 0; layno < numLayers; ++layno)
	{
		for(resno = 0; resno < numResolutions; ++resno)
		{
			for(compno = 0; compno < numComps; ++compno)
			{
				if(!packet_array[index])
				{
					loss = true;
					break;
				}
				index += step_c;
			}
		}
	}
	if(loss)
		GRK_ERROR("POC: missing packets");
	delete[] packet_array;

	return !loss;
}
bool CodeStreamCompress::init_mct_encoding(TileCodingParams* p_tcp, GrkImage* p_image)
{
	uint32_t i;
	uint32_t indix = 1;
	grk_mct_data *mct_deco_data = nullptr, *mct_offset_data = nullptr;
	grk_simple_mcc_decorrelation_data* mcc_data;
	uint32_t mct_size, nb_elem;
	float *data, *current_data;
	TileComponentCodingParams* tccp;

	assert(p_tcp != nullptr);

	if(p_tcp->mct != 2)
		return true;

	if(p_tcp->m_mct_decoding_matrix)
	{
		if(p_tcp->m_nb_mct_records == p_tcp->m_nb_max_mct_records)
		{
			p_tcp->m_nb_max_mct_records += default_number_mct_records;

			auto new_mct_records = (grk_mct_data*)grkRealloc(
				p_tcp->m_mct_records, p_tcp->m_nb_max_mct_records * sizeof(grk_mct_data));
			if(!new_mct_records)
			{
				grkFree(p_tcp->m_mct_records);
				p_tcp->m_mct_records = nullptr;
				p_tcp->m_nb_max_mct_records = 0;
				p_tcp->m_nb_mct_records = 0;
				/* GRK_ERROR( "Not enough memory to set up mct compressing"); */
				return false;
			}
			p_tcp->m_mct_records = new_mct_records;
			mct_deco_data = p_tcp->m_mct_records + p_tcp->m_nb_mct_records;

			memset(mct_deco_data, 0,
				   (p_tcp->m_nb_max_mct_records - p_tcp->m_nb_mct_records) * sizeof(grk_mct_data));
		}
		mct_deco_data = p_tcp->m_mct_records + p_tcp->m_nb_mct_records;
		grkFree(mct_deco_data->m_data);
		mct_deco_data->m_data = nullptr;

		mct_deco_data->m_index = indix++;
		mct_deco_data->m_array_type = MCT_TYPE_DECORRELATION;
		mct_deco_data->m_element_type = MCT_TYPE_FLOAT;
		nb_elem = (uint32_t)p_image->numcomps * p_image->numcomps;
		mct_size = nb_elem * MCT_ELEMENT_SIZE[mct_deco_data->m_element_type];
		mct_deco_data->m_data = (uint8_t*)grkMalloc(mct_size);

		if(!mct_deco_data->m_data)
			return false;

		j2k_mct_write_functions_from_float[mct_deco_data->m_element_type](
			p_tcp->m_mct_decoding_matrix, mct_deco_data->m_data, nb_elem);

		mct_deco_data->m_data_size = mct_size;
		++p_tcp->m_nb_mct_records;
	}

	if(p_tcp->m_nb_mct_records == p_tcp->m_nb_max_mct_records)
	{
		grk_mct_data* new_mct_records;
		p_tcp->m_nb_max_mct_records += default_number_mct_records;
		new_mct_records = (grk_mct_data*)grkRealloc(
			p_tcp->m_mct_records, p_tcp->m_nb_max_mct_records * sizeof(grk_mct_data));
		if(!new_mct_records)
		{
			grkFree(p_tcp->m_mct_records);
			p_tcp->m_mct_records = nullptr;
			p_tcp->m_nb_max_mct_records = 0;
			p_tcp->m_nb_mct_records = 0;
			/* GRK_ERROR( "Not enough memory to set up mct compressing"); */
			return false;
		}
		p_tcp->m_mct_records = new_mct_records;
		mct_offset_data = p_tcp->m_mct_records + p_tcp->m_nb_mct_records;

		memset(mct_offset_data, 0,
			   (p_tcp->m_nb_max_mct_records - p_tcp->m_nb_mct_records) * sizeof(grk_mct_data));
		if(mct_deco_data)
			mct_deco_data = mct_offset_data - 1;
	}
	mct_offset_data = p_tcp->m_mct_records + p_tcp->m_nb_mct_records;
	if(mct_offset_data->m_data)
	{
		grkFree(mct_offset_data->m_data);
		mct_offset_data->m_data = nullptr;
	}
	mct_offset_data->m_index = indix++;
	mct_offset_data->m_array_type = MCT_TYPE_OFFSET;
	mct_offset_data->m_element_type = MCT_TYPE_FLOAT;
	nb_elem = p_image->numcomps;
	mct_size = nb_elem * MCT_ELEMENT_SIZE[mct_offset_data->m_element_type];
	mct_offset_data->m_data = (uint8_t*)grkMalloc(mct_size);
	if(!mct_offset_data->m_data)
		return false;

	data = (float*)grkMalloc(nb_elem * sizeof(float));
	if(!data)
	{
		grkFree(mct_offset_data->m_data);
		mct_offset_data->m_data = nullptr;
		return false;
	}
	tccp = p_tcp->tccps;
	current_data = data;

	for(i = 0; i < nb_elem; ++i)
	{
		*(current_data++) = (float)(tccp->m_dc_level_shift);
		++tccp;
	}
	j2k_mct_write_functions_from_float[mct_offset_data->m_element_type](
		data, mct_offset_data->m_data, nb_elem);
	grkFree(data);
	mct_offset_data->m_data_size = mct_size;
	++p_tcp->m_nb_mct_records;

	if(p_tcp->m_nb_mcc_records == p_tcp->m_nb_max_mcc_records)
	{
		grk_simple_mcc_decorrelation_data* new_mcc_records;
		p_tcp->m_nb_max_mcc_records += default_number_mct_records;
		new_mcc_records = (grk_simple_mcc_decorrelation_data*)grkRealloc(
			p_tcp->m_mcc_records,
			p_tcp->m_nb_max_mcc_records * sizeof(grk_simple_mcc_decorrelation_data));
		if(!new_mcc_records)
		{
			grkFree(p_tcp->m_mcc_records);
			p_tcp->m_mcc_records = nullptr;
			p_tcp->m_nb_max_mcc_records = 0;
			p_tcp->m_nb_mcc_records = 0;
			/* GRK_ERROR( "Not enough memory to set up mct compressing"); */
			return false;
		}
		p_tcp->m_mcc_records = new_mcc_records;
		mcc_data = p_tcp->m_mcc_records + p_tcp->m_nb_mcc_records;
		memset(mcc_data, 0,
			   (p_tcp->m_nb_max_mcc_records - p_tcp->m_nb_mcc_records) *
				   sizeof(grk_simple_mcc_decorrelation_data));
	}
	mcc_data = p_tcp->m_mcc_records + p_tcp->m_nb_mcc_records;
	mcc_data->m_decorrelation_array = mct_deco_data;
	mcc_data->m_is_irreversible = 1;
	mcc_data->m_nb_comps = p_image->numcomps;
	mcc_data->m_index = indix++;
	mcc_data->m_offset_array = mct_offset_data;
	++p_tcp->m_nb_mcc_records;

	return true;
}
uint64_t CodeStreamCompress::getNumTilePartsForProgression(uint32_t pino, uint16_t tileno)
{
	uint64_t numTileParts = 1;
	auto cp = &m_cp;

	/*  preconditions */
	assert(tileno < (cp->t_grid_width * cp->t_grid_height));
	assert(pino < (cp->tcps[tileno].getNumProgressions()));

	/* get the given tile coding parameter */
	auto tcp = cp->tcps + tileno;
	assert(tcp != nullptr);

	auto current_poc = &(tcp->progressionOrderChange[pino]);
	assert(current_poc != 0);

	/* get the progression order as a character string */
	auto prog = convertProgressionOrder(tcp->prg);
	assert(strlen(prog) > 0);

	if(cp->m_coding_params.m_enc.m_enableTilePartGeneration)
	{
		for(uint32_t i = 0; i < 4; ++i)
		{
			switch(prog[i])
			{
				/* component wise */
				case 'C':
					numTileParts *= current_poc->tpCompE;
					break;
					/* resolution wise */
				case 'R':
					numTileParts *= current_poc->tpResE;
					break;
					/* precinct wise */
				case 'P':
					numTileParts *= current_poc->tpPrecE;
					break;
					/* layer wise */
				case 'L':
					numTileParts *= current_poc->tpLayE;
					break;
			}
			// we start a new tile part when progression matches specified tile part
			// divider
			if(cp->m_coding_params.m_enc.m_newTilePartProgressionDivider == prog[i])
			{
				assert(prog[i] != 'P');
				cp->m_coding_params.m_enc.newTilePartProgressionPosition = i;
				break;
			}
		}
	}
	else
	{
		numTileParts = 1;
	}
	assert(numTileParts < maxTilePartsPerTileJ2K);

	return numTileParts;
}
bool CodeStreamCompress::getNumTileParts(uint16_t* numTilePartsForAllTiles, GrkImage* image)
{
	assert(numTilePartsForAllTiles != nullptr);
	assert(image != nullptr);

	uint32_t numTiles = (uint16_t)(m_cp.t_grid_width * m_cp.t_grid_height);
	*numTilePartsForAllTiles = 0;
	auto tcp = m_cp.tcps;
	for(uint16_t tileno = 0; tileno < numTiles; ++tileno)
	{
		uint8_t totalTilePartsForTile = 0;
		PacketManager::updateCompressParams(image, &m_cp, tileno);
		for(uint32_t pino = 0; pino < tcp->getNumProgressions(); ++pino)
		{
			uint64_t numTilePartsForProgression = getNumTilePartsForProgression(pino, tileno);
			uint16_t newTotalTilePartsForTile =
				uint16_t(numTilePartsForProgression + totalTilePartsForTile);
			if(newTotalTilePartsForTile > maxTilePartsPerTileJ2K)
			{
				GRK_ERROR("Number of tile parts %d exceeds maximum number of "
						  "tile parts %d",
						  (uint16_t)newTotalTilePartsForTile, maxTilePartsPerTileJ2K);
				return false;
			}
			totalTilePartsForTile = (uint8_t)newTotalTilePartsForTile;

			uint32_t newTotalTilePartsForAllTiles =
				(uint32_t)(*numTilePartsForAllTiles + numTilePartsForProgression);
			if(newTotalTilePartsForAllTiles > maxTotalTilePartsJ2K)
			{
				GRK_ERROR("Total number of tile parts %d for image exceeds JPEG 2000 maximum total "
						  "number of "
						  "tile parts %d",
						  newTotalTilePartsForAllTiles, maxTotalTilePartsJ2K);
				return false;
			}
			*numTilePartsForAllTiles = (uint16_t)newTotalTilePartsForAllTiles;
		}
		tcp->m_numTileParts = totalTilePartsForTile;
		++tcp;
	}

	return true;
}

} // namespace grk
