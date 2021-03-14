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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"

namespace grk {

static void j2k_write_float_to_int16(const void *p_src_data, void *p_dest_data,uint64_t nb_elem) {
	j2k_write<float, int16_t>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_write_float_to_int32(const void *p_src_data, void *p_dest_data,uint64_t nb_elem) {
	j2k_write<float, int32_t>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_write_float_to_float(const void *p_src_data, void *p_dest_data,uint64_t nb_elem) {
	j2k_write<float, float>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_write_float_to_float64(const void *p_src_data, void *p_dest_data,uint64_t nb_elem) {
	j2k_write<float, double>(p_src_data, p_dest_data, nb_elem);
}

static const j2k_mct_function j2k_mct_write_functions_from_float[] = {
		j2k_write_float_to_int16, j2k_write_float_to_int32,
		j2k_write_float_to_float, j2k_write_float_to_float64 };


struct j2k_prog_order {
	GRK_PROG_ORDER enum_prog;
	char str_prog[5];
};

static j2k_prog_order j2k_prog_order_list[] = { { GRK_CPRL, "CPRL" }, {
		GRK_LRCP, "LRCP" }, { GRK_PCRL, "PCRL" }, { GRK_RLCP, "RLCP" }, {
		GRK_RPCL, "RPCL" }, { (GRK_PROG_ORDER) -1, "" } };

CodeStreamCompress::CodeStreamCompress( BufferedStream *stream) : CodeStream(stream){
}

CodeStreamCompress::~CodeStreamCompress() {
}
char* CodeStreamCompress::convertProgressionOrder(GRK_PROG_ORDER prg_order) {
	j2k_prog_order *po;
	for (po = j2k_prog_order_list; po->enum_prog != -1; po++) {
		if (po->enum_prog == prg_order)
			return (char*) po->str_prog;
	}
	return po->str_prog;
}
bool CodeStreamCompress::mct_validation(void) {
	bool is_valid = true;
	if ((m_cp.rsiz & 0x8200) == 0x8200) {
		uint32_t nb_tiles = m_cp.t_grid_height
				* m_cp.t_grid_width;
		for (uint32_t i = 0; i < nb_tiles; ++i) {
			auto tcp = m_cp.tcps + i;
			if (tcp->mct == 2) {
				is_valid &= (tcp->m_mct_coding_matrix != nullptr);
				for (uint32_t j = 0; j < m_headerImage->numcomps; ++j) {
					auto tccp = tcp->tccps + j;
					is_valid &= !(tccp->qmfbid & 1);
				}
			}
		}
	}

	return is_valid;
}

bool CodeStreamCompress::startCompress(void){
	/* customization of the validation */
	m_validation_list.push_back(std::bind(&CodeStreamCompress::compress_validation, this));
	//custom validation here
	m_validation_list.push_back(std::bind(&CodeStreamCompress::mct_validation, this));

	/* validation of the parameters codec */
	if (!exec(m_validation_list))
		return false;

	/* customization of the compressing */
	if (!init_header_writing())
		return false;

	/* write header */
	return exec(m_procedure_list);
}
bool CodeStreamCompress::initCompress(grk_cparameters  *parameters,GrkImage *image){
	if (!parameters || !image)
		return false;

	//sanity check on image
	if (image->numcomps < 1 || image->numcomps > max_num_components) {
		GRK_ERROR(
				"Invalid number of components specified while setting up JP2 compressor");
		return false;
	}
	if ((image->x1 < image->x0) || (image->y1 < image->y0)) {
		GRK_ERROR(
				"Invalid input image dimensions found while setting up JP2 compressor");
		return false;
	}
	for (uint32_t i = 0; i < image->numcomps; ++i) {
		auto comp = image->comps + i;
		if (comp->w == 0 || comp->h == 0) {
			GRK_ERROR(
					"Invalid input image component dimensions found while setting up JP2 compressor");
			return false;
		}
		if (comp->prec == 0) {
			GRK_ERROR(
					"Invalid component precision of 0 found while setting up JP2 compressor");
			return false;
		}
	}

	// create private sanitized copy of image
	m_headerImage = new GrkImage();
	image->copyHeader(m_headerImage);
	if (image->comps) {
		for (uint32_t compno = 0; compno < image->numcomps; compno++) {
			if (image->comps[compno].data) {
				m_headerImage->comps[compno].data =	image->comps[compno].data;
				image->comps[compno].data = nullptr;

			}
		}
	}

	if ((parameters->numresolution == 0)
			|| (parameters->numresolution > GRK_J2K_MAXRLVLS)) {
		GRK_ERROR("Invalid number of resolutions : %u not in range [1,%u]",
				parameters->numresolution, GRK_J2K_MAXRLVLS);
		return false;
	}

	if (GRK_IS_IMF(parameters->rsiz) && parameters->max_cs_size > 0
			&& parameters->tcp_numlayers == 1
			&& parameters->tcp_rates[0] == 0) {
		parameters->tcp_rates[0] = (float) (image->numcomps * image->comps[0].w
				* image->comps[0].h * image->comps[0].prec)
				/ (float) (((uint32_t) parameters->max_cs_size) * 8
						* image->comps[0].dx * image->comps[0].dy);
	}

	/* if no rate entered, lossless by default */
	if (parameters->tcp_numlayers == 0) {
		parameters->tcp_rates[0] = 0;
		parameters->tcp_numlayers = 1;
		parameters->cp_disto_alloc = true;
	}

	/* see if max_codestream_size does limit input rate */
	double image_bytes = ((double) image->numcomps * image->comps[0].w
			* image->comps[0].h * image->comps[0].prec)
			/ (8 * image->comps[0].dx * image->comps[0].dy);
	if (parameters->max_cs_size == 0) {
		if (parameters->tcp_numlayers > 0
				&& parameters->tcp_rates[parameters->tcp_numlayers - 1] > 0) {
			parameters->max_cs_size = (uint64_t) floor(
					image_bytes
							/ parameters->tcp_rates[parameters->tcp_numlayers
									- 1]);
		}
	} else {
		bool cap = false;
		auto min_rate = image_bytes / (double) parameters->max_cs_size;
		for (uint32_t i = 0; i < parameters->tcp_numlayers; i++) {
			if (parameters->tcp_rates[i] < min_rate) {
				parameters->tcp_rates[i] = min_rate;
				cap = true;
			}
		}
		if (cap) {
			GRK_WARN("The desired maximum code stream size has limited\n"
					"at least one of the desired quality layers");
		}
	}

	/* Manage profiles and applications and set RSIZ */
	/* set cinema parameters if required */
	if (parameters->isHT) {
		parameters->rsiz |= GRK_JPH_RSIZ_FLAG;
	}
	if (GRK_IS_CINEMA(parameters->rsiz)) {
		if ((parameters->rsiz == GRK_PROFILE_CINEMA_S2K)
				|| (parameters->rsiz == GRK_PROFILE_CINEMA_S4K)) {
			GRK_WARN(
					"JPEG 2000 Scalable Digital Cinema profiles not supported");
			parameters->rsiz = GRK_PROFILE_NONE;
		} else {
			if (Profile::is_cinema_compliant(image, parameters->rsiz))
				Profile::set_cinema_parameters(parameters, image);
			else
				parameters->rsiz = GRK_PROFILE_NONE;
		}
	} else if (GRK_IS_STORAGE(parameters->rsiz)) {
		GRK_WARN("JPEG 2000 Long Term Storage profile not supported");
		parameters->rsiz = GRK_PROFILE_NONE;
	} else if (GRK_IS_BROADCAST(parameters->rsiz)) {
		Profile::set_broadcast_parameters(parameters);
		if (!Profile::is_broadcast_compliant(parameters, image))
			parameters->rsiz = GRK_PROFILE_NONE;
	} else if (GRK_IS_IMF(parameters->rsiz)) {
		Profile::set_imf_parameters(parameters, image);
		if (!Profile::is_imf_compliant(parameters, image))
			parameters->rsiz = GRK_PROFILE_NONE;
	} else if (GRK_IS_PART2(parameters->rsiz)) {
		if (parameters->rsiz == ((GRK_PROFILE_PART2) | (GRK_EXTENSION_NONE))) {
			GRK_WARN("JPEG 2000 Part-2 profile defined\n"
					"but no Part-2 extension enabled.\n"
					"Profile set to NONE.");
			parameters->rsiz = GRK_PROFILE_NONE;
		} else if (parameters->rsiz
				!= ((GRK_PROFILE_PART2) | (GRK_EXTENSION_MCT))) {
			GRK_WARN("Unsupported Part-2 extension enabled\n"
					"Profile set to NONE.");
			parameters->rsiz = GRK_PROFILE_NONE;
		}
	}

	if (parameters->numpocs) {
		/* initialisation of POC */
		if (!check_poc_val(parameters->progression, parameters->numpocs,
				parameters->numresolution, image->numcomps,
				parameters->tcp_numlayers)) {
			GRK_ERROR("Failed to initialize POC");
			return false;
		}
	}
	/* keep a link to cp so that we can destroy it later in j2k_destroy_compress */
	auto cp = &(m_cp);

	/* set default values for cp */
	cp->t_grid_width = 1;
	cp->t_grid_height = 1;

	cp->m_coding_params.m_enc.m_max_comp_size = parameters->max_comp_size;
	cp->rsiz = parameters->rsiz;
	cp->m_coding_params.m_enc.m_disto_alloc = parameters->cp_disto_alloc;
	cp->m_coding_params.m_enc.m_fixed_quality = parameters->cp_fixed_quality;
	cp->m_coding_params.m_enc.writePLT = parameters->writePLT;
	cp->m_coding_params.m_enc.writeTLM = parameters->writeTLM;
	cp->m_coding_params.m_enc.rateControlAlgorithm =
			parameters->rateControlAlgorithm;

	/* tiles */
	cp->t_width = parameters->t_width;
	cp->t_height = parameters->t_height;

	/* tile offset */
	cp->tx0 = parameters->tx0;
	cp->ty0 = parameters->ty0;

	/* comment string */
	if (parameters->cp_num_comments) {
		for (size_t i = 0; i < parameters->cp_num_comments; ++i) {
			cp->comment_len[i] = parameters->cp_comment_len[i];
			if (!cp->comment_len[i]) {
				GRK_WARN("Empty comment. Ignoring");
				continue;
			}
			if (cp->comment_len[i] > GRK_MAX_COMMENT_LENGTH) {
				GRK_WARN(
						"Comment length %s is greater than maximum comment length %u. Ignoring",
						cp->comment_len[i], GRK_MAX_COMMENT_LENGTH);
				continue;
			}
			cp->comment[i] = (char*) new uint8_t[cp->comment_len[i]];
			if (!cp->comment[i]) {
				GRK_ERROR(
						"Not enough memory to allocate copy of comment string");
				return false;
			}
			memcpy(cp->comment[i], parameters->cp_comment[i],
					cp->comment_len[i]);
			cp->isBinaryComment[i] = parameters->cp_is_binary_comment[i];
			cp->num_comments++;
		}
	} else {
		/* Create default comment for code stream */
		const char comment[] = "Created by Grok     version ";
		const size_t clen = strlen(comment);
		const char *version = grk_version();

		cp->comment[0] = (char*) new uint8_t[clen + strlen(version) + 1];
		if (!cp->comment[0]) {
			GRK_ERROR("Not enough memory to allocate comment string");
			return false;
		}
		sprintf(cp->comment[0], "%s%s", comment, version);
		cp->comment_len[0] = (uint16_t) strlen(cp->comment[0]);
		cp->num_comments = 1;
		cp->isBinaryComment[0] = false;
	}
	if (parameters->tile_size_on) {
		// avoid divide by zero
		if (cp->t_width == 0 || cp->t_height == 0) {
			GRK_ERROR("Invalid tile dimensions (%u,%u)",cp->t_width, cp->t_height);
			return false;
		}
		cp->t_grid_width  = ceildiv<uint32_t>((image->x1 - cp->tx0), cp->t_width);
		cp->t_grid_height = ceildiv<uint32_t>((image->y1 - cp->ty0), cp->t_height);
	} else {
		cp->t_width  = image->x1 - cp->tx0;
		cp->t_height = image->y1 - cp->ty0;
	}
	if (parameters->tp_on) {
		cp->m_coding_params.m_enc.m_tp_flag = parameters->tp_flag;
		cp->m_coding_params.m_enc.m_tp_on = true;
	}
	uint8_t numgbits = parameters->isHT ? 1 : 2;
	cp->tcps = new TileCodingParams[cp->t_grid_width * cp->t_grid_height];
	for (uint32_t tileno = 0; tileno < cp->t_grid_width * cp->t_grid_height; tileno++) {
		TileCodingParams *tcp = cp->tcps + tileno;
		tcp->setIsHT(parameters->isHT);
		tcp->qcd.generate(numgbits, (uint32_t) (parameters->numresolution - 1),
				!parameters->irreversible, image->comps[0].prec, tcp->mct > 0,
				image->comps[0].sgnd);
		tcp->numlayers = parameters->tcp_numlayers;

		for (uint16_t j = 0; j < tcp->numlayers; j++) {
			if (cp->m_coding_params.m_enc.m_fixed_quality)
				tcp->distoratio[j] = parameters->tcp_distoratio[j];
			else
				tcp->rates[j] = parameters->tcp_rates[j];
		}
		tcp->csty = parameters->csty;
		tcp->prg = parameters->prog_order;
		tcp->mct = parameters->tcp_mct;
		tcp->POC = false;
		if (parameters->numpocs) {
			/* initialisation of POC */
			tcp->POC = true;
			uint32_t numpocs_tile = 0;
			for (uint32_t i = 0; i < parameters->numpocs; i++) {
				if (tileno + 1 == parameters->progression[i].tileno) {
					auto tcp_poc = &tcp->progressionOrderChange[numpocs_tile];

					tcp_poc->resS = parameters->progression[numpocs_tile].resS;
					tcp_poc->compS = parameters->progression[numpocs_tile].compS;
					tcp_poc->layE = parameters->progression[numpocs_tile].layE;
					tcp_poc->resE = parameters->progression[numpocs_tile].resE;
					tcp_poc->compE = parameters->progression[numpocs_tile].compE;
					tcp_poc->specifiedCompressionPocProg = parameters->progression[numpocs_tile].specifiedCompressionPocProg;
					tcp_poc->tileno = parameters->progression[numpocs_tile].tileno;
					numpocs_tile++;
				}
			}
			if (numpocs_tile == 0) {
				GRK_ERROR("Problem with specified progression order changes");
				return false;
			}
			tcp->numpocs = numpocs_tile - 1;
		} else {
			tcp->numpocs = 0;
		}
		tcp->tccps = new TileComponentCodingParams[image->numcomps];
		if (parameters->mct_data) {
			uint32_t lMctSize = (uint32_t)image->numcomps * image->numcomps * (uint32_t)sizeof(float);
			auto lTmpBuf = (float*) grkMalloc(lMctSize);
			auto dc_shift = (int32_t*) ((uint8_t*) parameters->mct_data
					+ lMctSize);
			if (!lTmpBuf) {
				GRK_ERROR("Not enough memory to allocate temp buffer");
				return false;
			}
			tcp->mct = 2;
			tcp->m_mct_coding_matrix = (float*) grkMalloc(lMctSize);
			if (!tcp->m_mct_coding_matrix) {
				grkFree(lTmpBuf);
				lTmpBuf = nullptr;
				GRK_ERROR(
						"Not enough memory to allocate compressor MCT coding matrix ");
				return false;
			}
			memcpy(tcp->m_mct_coding_matrix, parameters->mct_data, lMctSize);
			memcpy(lTmpBuf, parameters->mct_data, lMctSize);

			tcp->m_mct_decoding_matrix = (float*) grkMalloc(lMctSize);
			if (!tcp->m_mct_decoding_matrix) {
				grkFree(lTmpBuf);
				lTmpBuf = nullptr;
				GRK_ERROR(
						"Not enough memory to allocate compressor MCT decoding matrix ");
				return false;
			}
			if (GrkMatrix().matrix_inversion_f(lTmpBuf, (tcp->m_mct_decoding_matrix),
					image->numcomps) == false) {
				grkFree(lTmpBuf);
				lTmpBuf = nullptr;
				GRK_ERROR("Failed to inverse compressor MCT decoding matrix ");
				return false;
			}

			tcp->mct_norms = (double*) grkMalloc(
					image->numcomps * sizeof(double));
			if (!tcp->mct_norms) {
				grkFree(lTmpBuf);
				lTmpBuf = nullptr;
				GRK_ERROR("Not enough memory to allocate compressor MCT norms ");
				return false;
			}
			mct::calculate_norms(tcp->mct_norms, image->numcomps,
					tcp->m_mct_decoding_matrix);
			grkFree(lTmpBuf);

			for (uint32_t i = 0; i < image->numcomps; i++) {
				auto tccp = &tcp->tccps[i];
				tccp->m_dc_level_shift = dc_shift[i];
			}

			if (init_mct_encoding(tcp, image) == false) {
				/* free will be handled by j2k_destroy */
				GRK_ERROR("Failed to set up j2k mct compressing");
				return false;
			}
		} else {
			if (tcp->mct == 1) {
				if (image->color_space == GRK_CLRSPC_EYCC
						|| image->color_space == GRK_CLRSPC_SYCC) {
					GRK_WARN("Disabling MCT for sYCC/eYCC colour space");
					tcp->mct = 0;
				} else if (image->numcomps >= 3) {
					if ((image->comps[0].dx != image->comps[1].dx)
							|| (image->comps[0].dx != image->comps[2].dx)
							|| (image->comps[0].dy != image->comps[1].dy)
							|| (image->comps[0].dy != image->comps[2].dy)) {
						GRK_WARN(
								"Cannot perform MCT on components with different dimensions. Disabling MCT.");
						tcp->mct = 0;
					}
				}
			}
			for (uint32_t i = 0; i < image->numcomps; i++) {
				auto tccp = tcp->tccps + i;
				auto comp = image->comps + i;
				if (!comp->sgnd) {
					tccp->m_dc_level_shift = 1 << (comp->prec - 1);
				}
			}
		}

		for (uint32_t i = 0; i < image->numcomps; i++) {
			auto tccp = &tcp->tccps[i];

			/* 0 => one precinct || 1 => custom precinct  */
			tccp->csty = parameters->csty & J2K_CP_CSTY_PRT;
			tccp->numresolutions = parameters->numresolution;
			tccp->cblkw = (uint8_t)floorlog2<uint32_t>(parameters->cblockw_init);
			tccp->cblkh = (uint8_t)floorlog2<uint32_t>(parameters->cblockh_init);
			tccp->cblk_sty = parameters->cblk_sty;
			tccp->qmfbid = parameters->irreversible ? 0 : 1;
			tccp->qntsty = parameters->irreversible ? J2K_CCP_QNTSTY_SEQNT : J2K_CCP_QNTSTY_NOQNT;
			tccp->numgbits = numgbits;
			if ((int32_t) i == parameters->roi_compno)
				tccp->roishift = (uint8_t)parameters->roi_shift;
			else
				tccp->roishift = 0;
			if ((parameters->csty & J2K_CCP_CSTY_PRT) && parameters->res_spec) {
				uint32_t p = 0;
				int32_t it_res;
				assert(tccp->numresolutions > 0);
				for (it_res = (int32_t) tccp->numresolutions - 1; it_res >= 0;
						it_res--) {
					if (p < parameters->res_spec) {
						if (parameters->prcw_init[p] < 1) {
							tccp->precinctWidthExp[it_res] = 1;
						} else {
							tccp->precinctWidthExp[it_res] = floorlog2<uint32_t>(
									parameters->prcw_init[p]);
						}
						if (parameters->prch_init[p] < 1) {
							tccp->precinctHeightExp[it_res] = 1;
						} else {
							tccp->precinctHeightExp[it_res] = floorlog2<uint32_t>(
									parameters->prch_init[p]);
						}
					} else {
						uint32_t res_spec = parameters->res_spec;
						uint32_t size_prcw = 0;
						uint32_t size_prch = 0;
						size_prcw = parameters->prcw_init[res_spec - 1]	>> (p - (res_spec - 1));
						size_prch = parameters->prch_init[res_spec - 1]	>> (p - (res_spec - 1));
						if (size_prcw < 1) {
							tccp->precinctWidthExp[it_res] = 1;
						} else {
							tccp->precinctWidthExp[it_res] = floorlog2<uint32_t>(size_prcw);
						}
						if (size_prch < 1) {
							tccp->precinctHeightExp[it_res] = 1;
						} else {
							tccp->precinctHeightExp[it_res] = floorlog2<uint32_t>(size_prch);
						}
					}
					p++;
					/*printf("\nsize precinct for level %u : %u,%u\n", it_res,tccp->precinctWidthExp[it_res], tccp->precinctHeightExp[it_res]); */
				} /*end for*/
			} else {
				for (uint32_t j = 0; j < tccp->numresolutions; j++) {
					tccp->precinctWidthExp[j] = 15;
					tccp->precinctHeightExp[j] = 15;
				}
			}
			tcp->qcd.pull(tccp->stepsizes, !parameters->irreversible);
		}
	}
	grkFree(parameters->mct_data);
	parameters->mct_data = nullptr;

	return true;
}
bool CodeStreamCompress::compress(grk_plugin_tile* tile){
	uint32_t nb_tiles = (uint32_t) m_cp.t_grid_height* m_cp.t_grid_width;
	if (nb_tiles > max_num_tiles) {
		GRK_ERROR("Number of tiles %u is greater than %u max tiles "
				"allowed by the standard.", nb_tiles, max_num_tiles);
		return false;
	}
	auto pool_size = std::min<uint32_t>((uint32_t)ThreadPool::get()->num_threads(), nb_tiles);
	ThreadPool pool(pool_size);
	std::vector< std::future<int> > results;
	std::unique_ptr<TileProcessor*[]> procs = std::make_unique<TileProcessor*[]>(nb_tiles);
	std::atomic<bool> success(true);
	bool rc = false;

	for (uint16_t i = 0; i < nb_tiles; ++i)
		procs[i] = nullptr;

	if (pool_size > 1){
		for (uint16_t i = 0; i < nb_tiles; ++i) {
			uint16_t tile_ind = i;
			results.emplace_back(
					pool.enqueue([this,
								  &procs,
								  tile,
								  tile_ind,
								  &success] {
						if (success) {
							auto tileProcessor = new TileProcessor(this,m_stream,true,false);
							procs[tile_ind] = tileProcessor;
							tileProcessor->m_tile_index = tile_ind;
							tileProcessor->current_plugin_tile = tile;
							if (!tileProcessor->pre_write_tile())
								success = false;
							else {
								if (!tileProcessor->do_compress())
									success = false;
							}
						}
						return 0;
					})
				);
		}
	} else {
		for (uint16_t i = 0; i < nb_tiles; ++i) {
			auto tileProcessor = new TileProcessor(this,m_stream,true,false);
			tileProcessor->m_tile_index = i;
			tileProcessor->current_plugin_tile = tile;
			if (!tileProcessor->pre_write_tile()){
				delete tileProcessor;
				goto cleanup;
			}
			if (!tileProcessor->do_compress()){
				delete tileProcessor;
				goto cleanup;
			}
			if (!post_write_tile(tileProcessor)){
				delete tileProcessor;
				goto cleanup;
			}
			delete tileProcessor;
		}
	}
	if (pool_size > 1) {
		for(auto &result: results){
			result.get();
		}
		if (!success)
			goto cleanup;
		for (uint16_t i = 0; i < nb_tiles; ++i) {
			bool write_success = post_write_tile(procs[i]);
			delete procs[i];
			procs[i] = nullptr;
			if (!write_success)
				goto cleanup;
		}
	}
	rc = true;
cleanup:
	for (uint16_t i = 0; i < nb_tiles; ++i)
		delete procs[i];

	return rc;
}
bool CodeStreamCompress::compressTile(uint16_t tile_index,	uint8_t *p_data, uint64_t uncompressed_data_size){
	if (!p_data)
		return false;
	bool rc = false;

	m_currentTileProcessor = new TileProcessor(this,m_stream,true,false);
	m_currentTileProcessor->m_tile_index = tile_index;

	if (!m_currentTileProcessor->pre_write_tile()) {
		GRK_ERROR("Error while pre_write_tile with tile index = %u",
				tile_index);
		goto cleanup;
	}
	/* now copy data into the tile component */
	if (!m_currentTileProcessor->ingestUncompressedData(p_data,	uncompressed_data_size)) {
		GRK_ERROR("Size mismatch between tile data and sent data.");
		goto cleanup;
	}
	if (!m_currentTileProcessor->do_compress())
		goto cleanup;
	if (!post_write_tile(m_currentTileProcessor)) {
		GRK_ERROR("Error while j2k_post_write_tile with tile index = %u",
				tile_index);
		goto cleanup;
	}
	rc = true;
cleanup:
	delete m_currentTileProcessor;

	return rc;
}
bool CodeStreamCompress::endCompress(void){
	/* customization of the compressing */
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_eoc,this));
	if (m_cp.m_coding_params.m_enc.writeTLM)
		m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_tlm_end,this));

	return  exec(m_procedure_list);
}
///////////////////////////////////////////////////////////////////////////////////////////
bool CodeStreamCompress::write_rgn( uint16_t tile_no, uint32_t comp_no,
		uint32_t nb_comps) {
	uint32_t rgn_size;
	auto cp = &(m_cp);
	auto tcp = &cp->tcps[tile_no];
	auto tccp = &tcp->tccps[comp_no];
	uint32_t comp_room = (nb_comps <= 256) ? 1 : 2;
	rgn_size = 6 + comp_room;

	/* RGN  */
	if (!m_stream->write_short(J2K_MS_RGN))
		return false;
	/* Lrgn */
	if (!m_stream->write_short((uint16_t) (rgn_size - 2)))
		return false;
	/* Crgn */
	if (comp_room == 2) {
		if (!m_stream->write_short((uint16_t) comp_no))
			return false;
	} else {
		if (!m_stream->write_byte((uint8_t) comp_no))
			return false;
	}
	/* Srgn */
	if (!m_stream->write_byte(0))
		return false;

	/* SPrgn */
	return m_stream->write_byte((uint8_t) tccp->roishift);
}

bool CodeStreamCompress::write_eoc() {
	if (!m_stream->write_short(J2K_MS_EOC))
		return false;

	return m_stream->flush();
}
bool CodeStreamCompress::write_mct_record(grk_mct_data *p_mct_record, BufferedStream *stream) {
	uint32_t mct_size;
	uint32_t tmp;

	mct_size = 10 + p_mct_record->m_data_size;

	/* MCT */
	if (!stream->write_short(J2K_MS_MCT))
		return false;
	/* Lmct */
	if (!stream->write_short((uint16_t) (mct_size - 2)))
		return false;
	/* Zmct */
	if (!stream->write_short(0))
		return false;
	/* only one marker atm */
	tmp = (p_mct_record->m_index & 0xff)
			| (uint32_t) (p_mct_record->m_array_type << 8)
			| (uint32_t) (p_mct_record->m_element_type << 10);

	if (!stream->write_short((uint16_t) tmp))
		return false;

	/* Ymct */
	if (!stream->write_short(0))
		return false;

	return stream->write_bytes(p_mct_record->m_data, p_mct_record->m_data_size);
}
bool CodeStreamCompress::get_end_header(void) {
	cstr_index->main_head_end = m_stream->tell();

	return true;
}
bool CodeStreamCompress::init_header_writing(void) {
	m_procedure_list.push_back([this] {return calculate_tp(&m_cp,&m_compressorState.m_total_tile_parts,getHeaderImage());});

	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_soc,this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_siz,this));
	if (m_cp.tcps[0].getIsHT())
		m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_cap,this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_cod,this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_qcd,this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_all_coc,this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_all_qcc,this));

	if (m_cp.m_coding_params.m_enc.writeTLM)
		m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_tlm_begin,this));
	if (m_cp.rsiz == GRK_PROFILE_CINEMA_4K)
		m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_poc,this));

	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_regions,this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_com,this));
	//begin custom procedures
	if ((m_cp.rsiz & (GRK_PROFILE_PART2 | GRK_EXTENSION_MCT))
			== (GRK_PROFILE_PART2 | GRK_EXTENSION_MCT)) {
		m_procedure_list.push_back(std::bind(&CodeStreamCompress::write_mct_data_group,this));
	}
	//end custom procedures

	if (cstr_index)
		m_procedure_list.push_back(std::bind(&CodeStreamCompress::get_end_header,this));
	m_procedure_list.push_back(std::bind(&CodeStreamCompress::update_rates,this));

	return true;
}
bool CodeStreamCompress::write_tile_part(TileProcessor *tileProcessor) {
	uint16_t currentTileNumber = tileProcessor->m_tile_index;
	auto cp = &m_cp;
	bool firstTilePart = tileProcessor->m_tile_part_index == 0;
	//1. write SOT
	SOTMarker sot;
	if (!sot.write(this))
		return false;
	uint32_t tile_part_bytes_written = sot_marker_segment_len;
	//2. write POC (only in first tile part)
	if (firstTilePart) {
		if (!GRK_IS_CINEMA(cp->rsiz)) {
			if (cp->tcps[currentTileNumber].numpocs) {
				auto tcp = m_cp.tcps + currentTileNumber;
				auto image = m_headerImage;
				uint32_t nb_comp = image->numcomps;
				if (!write_poc())
					return false;
				tile_part_bytes_written += getPocSize(nb_comp,
						1 + tcp->numpocs);
			}
		}
		/* set numIteratedPackets to zero when writing the first tile part
		 * (numIteratedPackets is used for SOP markers)
		 */
		tileProcessor->tile->numIteratedPackets = 0;
	}
	// 3. compress tile part
	if (!tileProcessor->compressTilePart(&tile_part_bytes_written)) {
		GRK_ERROR("Cannot compress tile");
		return false;
	}
	/* 4. write Psot in SOT marker */
	if (!sot.write_psot(this,tile_part_bytes_written))
		return false;
	// 5. update TLM
	if (m_cp.tlm_markers)
		m_cp.tlm_markers->writeUpdate(currentTileNumber, tile_part_bytes_written);
	++tileProcessor->m_tile_part_index;

	return true;
}
bool CodeStreamCompress::post_write_tile(TileProcessor *tileProcessor) {
	m_currentTileProcessor = tileProcessor;
	assert(tileProcessor->m_tile_part_index == 0);

	//1. write first tile part
	tileProcessor->pino = 0;
	tileProcessor->m_first_poc_tile_part = true;
	if (!write_tile_part(tileProcessor))
		return false;
	//2. write the other tile parts
	uint32_t pino;
	auto cp = &(m_cp);
	auto tcp = cp->tcps + tileProcessor->m_tile_index;
	// write tile parts for first progression order
	uint64_t num_tp = get_num_tp(cp, 0, tileProcessor->m_tile_index);
	if (num_tp > max_num_tile_parts_per_tile){
		GRK_ERROR("Number of tile parts %d for first POC exceeds maximum number of tile parts %d", num_tp,max_num_tile_parts_per_tile );
		return false;
	}
	tileProcessor->m_first_poc_tile_part = false;
	for (uint8_t tilepartno = 1; tilepartno < (uint8_t)num_tp; ++tilepartno) {
		if (!write_tile_part(tileProcessor))
			return false;
	}
	// write tile parts for remaining progression orders
	for (pino = 1; pino <= tcp->numpocs; ++pino) {
		tileProcessor->pino = pino;
		num_tp = get_num_tp(cp, pino,
				tileProcessor->m_tile_index);
		if (num_tp > max_num_tile_parts_per_tile){
			GRK_ERROR("Number of tile parts %d exceeds maximum number of "
					"tile parts %d", num_tp,max_num_tile_parts_per_tile );
			return false;
		}
		for (uint8_t tilepartno = 0; tilepartno < num_tp; ++tilepartno) {
			tileProcessor->m_first_poc_tile_part = (tilepartno == 0);
			if (!write_tile_part(tileProcessor))
				return false;
		}
	}
	++tileProcessor->m_tile_index;

	return true;
}
bool CodeStreamCompress::update_rates(void) {
	auto cp = &(m_cp);
	auto image = m_headerImage;
	auto tcp = cp->tcps;
	auto width = image->x1 - image->x0;
	auto height = image->y1 - image->y0;
	if (width <= 0 || height <= 0)
		return false;

	uint32_t bits_empty = 8 * image->comps->dx * image->comps->dy;
	uint32_t size_pixel = (uint32_t)image->numcomps * image->comps->prec;
	auto header_size = (double) m_stream->tell();

	for (uint32_t tile_y = 0; tile_y < cp->t_grid_height; ++tile_y) {
		for (uint32_t tile_x = 0; tile_x < cp->t_grid_width; ++tile_x) {
			double stride = 0;
			if (cp->m_coding_params.m_enc.m_tp_on)
				stride = (tcp->m_nb_tile_parts - 1) * 14;
			double offset = stride / tcp->numlayers;
			auto tileBounds = cp->getTileBounds(image,  tile_x, tile_y);
			uint64_t numTilePixels = tileBounds.area();
			for (uint16_t k = 0; k < tcp->numlayers; ++k) {
				double *rates = tcp->rates + k;
				if (*rates > 0.0f)
					*rates = ((((double) size_pixel * (double) numTilePixels))
							/ ((double) *rates * (double) bits_empty)) - offset;
			}
			++tcp;
		}
	}
	tcp = cp->tcps;

	for (uint32_t tile_y = 0; tile_y < cp->t_grid_height; ++tile_y) {
		for (uint32_t tile_x = 0; tile_x < cp->t_grid_width; ++tile_x) {
			double *rates = tcp->rates;

			auto tileBounds = cp->getTileBounds(image,  tile_x, tile_y);
			uint64_t numTilePixels = tileBounds.area();

			double sot_adjust = ((double) numTilePixels * (double) header_size)
					/ ((double) width * height);
			if (*rates > 0.0) {
				*rates -= sot_adjust;
				if (*rates < 30.0f)
					*rates = 30.0f;
			}
			++rates;
			for (uint16_t k = 1; k < (uint16_t)(tcp->numlayers - 1); ++k) {
				if (*rates > 0.0) {
					*rates -= sot_adjust;
					if (*rates < *(rates - 1) + 10.0)
						*rates = (*(rates - 1)) + 20.0;
				}
				++rates;
			}
			if (*rates > 0.0) {
				*rates -= (sot_adjust + 2.0);
				if (*rates < *(rates - 1) + 10.0)
					*rates = (*(rates - 1)) + 20.0;
			}
			++tcp;
		}
	}

	return true;
}
bool CodeStreamCompress::compress_validation() {
	bool is_valid = true;

	/* ISO 15444-1:2004 states between 1 & 33
	 * ergo (number of decomposition levels between 0 -> 32) */
	if ((m_cp.tcps->tccps->numresolutions == 0)
			|| (m_cp.tcps->tccps->numresolutions > GRK_J2K_MAXRLVLS)) {
		GRK_ERROR("Invalid number of resolutions : %u not in range [1,%u]",
				m_cp.tcps->tccps->numresolutions, GRK_J2K_MAXRLVLS);
		return false;
	}
	if (m_cp.t_width == 0) {
		GRK_ERROR("Tile x dimension must be greater than zero ");
		return false;
	}
	if (m_cp.t_height == 0) {
		GRK_ERROR("Tile y dimension must be greater than zero ");
		return false;
	}

	return is_valid;
}
bool CodeStreamCompress::write_soc() {
	return m_stream->write_short(J2K_MS_SOC);
}
bool CodeStreamCompress::write_siz() {
	SIZMarker siz;

	return siz.write(this,m_stream);
}
bool CodeStreamCompress::write_cap() {
	auto cp = &(m_cp);
	auto tcp = &cp->tcps[0];
	auto tccp0 = &tcp->tccps[0];

	//marker size excluding header
	uint16_t Lcap = 8;

	uint32_t Pcap = 0x00020000; //for jph, Pcap^15 must be set, the 15th MSB
	uint16_t Ccap[32]; //a maximum of 32
	memset(Ccap, 0, sizeof(Ccap));

	bool reversible = tccp0->qmfbid == 1;
	if (reversible)
		Ccap[0] &= 0xFFDF;
	else
		Ccap[0] |= 0x0020;
	Ccap[0] &= 0xFFE0;

	uint32_t Bp = 0;
	uint32_t B = tcp->qcd.get_MAGBp();
	if (B <= 8)
		Bp = 0;
	else if (B < 28)
		Bp = B - 8;
	else if (B < 48)
		Bp = 13 + (B >> 2);
	else
		Bp = 31;
	Ccap[0] = (uint16_t) (Ccap[0] | Bp);

	/* CAP */
	if (!m_stream->write_short(J2K_MS_CAP)) {
		return false;
	}

	/* L_CAP */
	if (!m_stream->write_short(Lcap))
		return false;
	/* PCAP */
	if (!m_stream->write_int(Pcap))
		return false;
	/* CCAP */
	if (!m_stream->write_short(Ccap[0]))
		return false;

	return true;
}

bool CodeStreamCompress::write_com() {
	for (uint32_t i = 0; i < m_cp.num_comments; ++i) {
		const char *comment = m_cp.comment[i];
		uint16_t comment_size = m_cp.comment_len[i];
		if (!comment_size) {
			GRK_WARN("Empty comment. Ignoring");
			continue;
		}
		if (comment_size > GRK_MAX_COMMENT_LENGTH) {
			GRK_WARN(
					"Comment length %s is greater than maximum comment length %u. Ignoring",
					comment_size, GRK_MAX_COMMENT_LENGTH);
			continue;
		}
		uint32_t totacom_size = (uint32_t) comment_size + 6;

		/* COM */
		if (!m_stream->write_short(J2K_MS_COM))
			return false;
		/* L_COM */
		if (!m_stream->write_short((uint16_t) (totacom_size - 2)))
			return false;
		if (!m_stream->write_short(m_cp.isBinaryComment[i] ? 0 : 1))
			return false;
		if (!m_stream->write_bytes((uint8_t*) comment, comment_size))
			return false;
	}

	return true;
}
bool CodeStreamCompress::write_cod() {
	uint32_t code_size;
	auto cp = &(m_cp);
	auto tcp = &cp->tcps[0];
	code_size = 9
			+ get_SPCod_SPCoc_size(0);

	/* COD */
	if (!m_stream->write_short(J2K_MS_COD))
		return false;
	/* L_COD */
	if (!m_stream->write_short((uint16_t) (code_size - 2)))
		return false;
	/* Scod */
	if (!m_stream->write_byte((uint8_t) tcp->csty))
		return false;
	/* SGcod (A) */
	if (!m_stream->write_byte((uint8_t) tcp->prg))
		return false;
	/* SGcod (B) */
	if (!m_stream->write_short((uint16_t) tcp->numlayers))
		return false;
	/* SGcod (C) */
	if (!m_stream->write_byte((uint8_t) tcp->mct))
		return false;
	if (!write_SPCod_SPCoc(0)) {
		GRK_ERROR("Error writing COD marker");
		return false;
	}

	return true;
}
bool CodeStreamCompress::write_coc( uint32_t comp_no) {
	uint32_t coc_size;
	uint32_t comp_room;
	auto cp = &(m_cp);
	auto tcp = &cp->tcps[0];
	auto image = getHeaderImage();
	comp_room = (image->numcomps <= 256) ? 1 : 2;
	coc_size = cod_coc_len + comp_room
			+ get_SPCod_SPCoc_size(comp_no);

	/* COC */
	if (!m_stream->write_short(J2K_MS_COC))
		return false;
	/* L_COC */
	if (!m_stream->write_short((uint16_t) (coc_size - 2)))
		return false;
	/* Ccoc */
	if (comp_room == 2) {
		if (!m_stream->write_short((uint16_t) comp_no))
			return false;
	} else {
		if (!m_stream->write_byte((uint8_t) comp_no))
			return false;
	}

	/* Scoc */
	if (!m_stream->write_byte((uint8_t) tcp->tccps[comp_no].csty))
		return false;

	return write_SPCod_SPCoc(0);

}
bool CodeStreamCompress::compare_coc( uint32_t first_comp_no,	uint32_t second_comp_no) {
	auto cp = &(m_cp);
	auto tcp = &cp->tcps[0];

	if (tcp->tccps[first_comp_no].csty != tcp->tccps[second_comp_no].csty)
		return false;

	return compare_SPCod_SPCoc(first_comp_no,second_comp_no);
}

bool CodeStreamCompress::write_qcd() {
	uint32_t qcd_size;
	qcd_size = 4 + get_SQcd_SQcc_size(0);

	/* QCD */
	if (!m_stream->write_short(J2K_MS_QCD))
		return false;
	/* L_QCD */
	if (!m_stream->write_short((uint16_t) (qcd_size - 2)))
		return false;
	if (!write_SQcd_SQcc(0)) {
		GRK_ERROR("Error writing QCD marker");
		return false;
	}

	return true;
}
bool CodeStreamCompress::write_qcc( uint32_t comp_no) {
	uint32_t qcc_size = 6 + get_SQcd_SQcc_size(comp_no);

	/* QCC */
	if (!m_stream->write_short(J2K_MS_QCC)) {
		return false;
	}

	if (getHeaderImage()->numcomps <= 256) {
		--qcc_size;

		/* L_QCC */
		if (!m_stream->write_short((uint16_t) (qcc_size - 2)))
			return false;
		/* Cqcc */
		if (!m_stream->write_byte((uint8_t) comp_no))
			return false;
	} else {
		/* L_QCC */
		if (!m_stream->write_short((uint16_t) (qcc_size - 2)))
			return false;
		/* Cqcc */
		if (!m_stream->write_short((uint16_t) comp_no))
			return false;
	}

	return write_SQcd_SQcc(comp_no);
}
bool CodeStreamCompress::compare_qcc(  uint32_t first_comp_no,
		uint32_t second_comp_no) {
	return compare_SQcd_SQcc(first_comp_no,second_comp_no);
}
bool CodeStreamCompress::write_poc() {
	auto tcp = &m_cp.tcps[0];
	auto tccp = &tcp->tccps[0];
	auto image = getHeaderImage();
	uint16_t nb_comp = image->numcomps;
	uint32_t nb_poc = tcp->numpocs + 1;
	uint32_t poc_room = (nb_comp <= 256) ? 1 : 2;

	auto poc_size = getPocSize(nb_comp, 1 + tcp->numpocs);

	/* POC  */
	if (!m_stream->write_short(J2K_MS_POC))
		return false;

	/* Lpoc */
	if (!m_stream->write_short((uint16_t) (poc_size - 2)))
		return false;

	for (uint32_t i = 0; i < nb_poc; ++i) {
		auto current_prog = tcp->progressionOrderChange + i;
		/* RSpoc_i */
		if (!m_stream->write_byte((uint8_t) current_prog->resS))
			return false;
		/* CSpoc_i */
		if (!m_stream->write_byte((uint8_t) current_prog->compS))
			return false;
		/* LYEpoc_i */
		if (!m_stream->write_short((uint16_t) current_prog->layE))
			return false;
		/* REpoc_i */
		if (!m_stream->write_byte((uint8_t) current_prog->resE))
			return false;
		/* CEpoc_i */
		if (poc_room == 2) {
			if (!m_stream->write_short((uint16_t) current_prog->compE))
				return false;
		} else {
			if (!m_stream->write_byte((uint8_t) current_prog->compE))
				return false;
		}
		/* Ppoc_i */
		if (!m_stream->write_byte((uint8_t) current_prog->progression))
			return false;

		/* change the value of the max layer according to the actual number of layers in the file, components and resolutions*/
		current_prog->layE = std::min<uint16_t>(current_prog->layE,
				tcp->numlayers);
		current_prog->resE = std::min<uint8_t>(current_prog->resE,
				tccp->numresolutions);
		current_prog->compE = std::min<uint16_t>(current_prog->compE,
				nb_comp);
	}

	return true;
}

bool CodeStreamCompress::write_mct_data_group() {
	uint32_t i;
	if (!write_cbd())
		return false;

	auto tcp = &(m_cp.tcps[0]);
	auto mct_record = tcp->m_mct_records;

	for (i = 0; i < tcp->m_nb_mct_records; ++i) {
		if (!write_mct_record(mct_record, m_stream))
			return false;
		++mct_record;
	}

	auto mcc_record = tcp->m_mcc_records;
	for (i = 0; i < tcp->m_nb_mcc_records; ++i) {
		if (!write_mcc_record(mcc_record, m_stream))
			return false;
		++mcc_record;
	}

	return write_mco();
}
bool CodeStreamCompress::write_all_coc() {
	for (uint16_t compno = 1; compno < getHeaderImage()->numcomps; ++compno) {
		/* cod is first component of first tile */
		if (!compare_coc(0, compno)) {
			if (!write_coc(compno))
				return false;
		}
	}

	return true;
}
bool CodeStreamCompress::write_all_qcc() {
	for (uint16_t compno = 1; compno < getHeaderImage()->numcomps; ++compno) {
		/* qcd is first component of first tile */
		if (!compare_qcc(0, compno)) {
			if (!write_qcc(compno))
				return false;
		}
	}
	return true;
}
bool CodeStreamCompress::write_regions() {
	for (uint16_t compno = 0; compno < getHeaderImage()->numcomps; ++compno) {
		auto tccp = m_cp.tcps->tccps + compno;
		if (tccp->roishift) {
			if (!write_rgn(0, compno,
					getHeaderImage()->numcomps))
				return false;
		}
	}

	return true;
}
bool CodeStreamCompress::write_mcc_record(grk_simple_mcc_decorrelation_data *p_mcc_record,	BufferedStream *stream) {
	uint32_t i;
	uint32_t mcc_size;
	uint32_t nb_bytes_for_comp;
	uint32_t mask;
	uint32_t tmcc;

	assert(stream != nullptr);

	if (p_mcc_record->m_nb_comps > 255) {
		nb_bytes_for_comp = 2;
		mask = 0x8000;
	} else {
		nb_bytes_for_comp = 1;
		mask = 0;
	}

	mcc_size = p_mcc_record->m_nb_comps * 2 * nb_bytes_for_comp + 19;

	/* MCC */
	if (!stream->write_short(J2K_MS_MCC))
		return false;
	/* Lmcc */
	if (!stream->write_short((uint16_t) (mcc_size - 2)))
		return false;
	/* first marker */
	/* Zmcc */
	if (!stream->write_short(0))
		return false;
	/* Imcc -> no need for other values, take the first */
	if (!stream->write_byte((uint8_t) p_mcc_record->m_index))
		return false;
	/* only one marker atm */
	/* Ymcc */
	if (!stream->write_short(0))
		return false;
	/* Qmcc -> number of collections -> 1 */
	if (!stream->write_short(1))
		return false;
	/* Xmcci type of component transformation -> array based decorrelation */
	if (!stream->write_byte(0x1))
		return false;
	/* Nmcci number of input components involved and size for each component offset = 8 bits */
	if (!stream->write_short((uint16_t) (p_mcc_record->m_nb_comps | mask)))
		return false;

	for (i = 0; i < p_mcc_record->m_nb_comps; ++i) {
		/* Cmccij Component offset*/
		if (nb_bytes_for_comp == 2) {
			if (!stream->write_short((uint16_t) i))
				return false;
		} else {
			if (!stream->write_byte((uint8_t) i))
				return false;
		}
	}

	/* Mmcci number of output components involved and size for each component offset = 8 bits */
	if (!stream->write_short((uint16_t) (p_mcc_record->m_nb_comps | mask)))
		return false;

	for (i = 0; i < p_mcc_record->m_nb_comps; ++i) {
		/* Wmccij Component offset*/
		if (nb_bytes_for_comp == 2) {
			if (!stream->write_short((uint16_t) i))
				return false;
		} else {
			if (!stream->write_byte((uint8_t) i))
				return false;
		}
	}

	tmcc = ((uint32_t) ((!p_mcc_record->m_is_irreversible) & 1U)) << 16;

	if (p_mcc_record->m_decorrelation_array)
		tmcc |= p_mcc_record->m_decorrelation_array->m_index;

	if (p_mcc_record->m_offset_array)
		tmcc |= ((p_mcc_record->m_offset_array->m_index) << 8);

	/* Tmcci : use MCT defined as number 1 and irreversible array based. */
	return stream->write_24(tmcc);
}
bool CodeStreamCompress::write_mco() {
	uint32_t mco_size;
	uint32_t i;
	auto tcp = &(m_cp.tcps[0]);
	mco_size = 5 + tcp->m_nb_mcc_records;

	/* MCO */
	if (!m_stream->write_short(J2K_MS_MCO))
		return false;

	/* Lmco */
	if (!m_stream->write_short((uint16_t) (mco_size - 2)))
		return false;

	/* Nmco : only one transform stage*/
	if (!m_stream->write_byte((uint8_t) tcp->m_nb_mcc_records))
		return false;

	auto mcc_record = tcp->m_mcc_records;
	for (i = 0; i < tcp->m_nb_mcc_records; ++i) {
		/* Imco -> use the mcc indicated by 1*/
		if (!m_stream->write_byte((uint8_t) mcc_record->m_index))
			return false;
		++mcc_record;
	}
	return true;
}

bool CodeStreamCompress::write_cbd() {
	uint32_t i;
	auto image = getHeaderImage();
	uint16_t cbd_size = (uint16_t)(6U + getHeaderImage()->numcomps);

	/* CBD */
	if (!m_stream->write_short(J2K_MS_CBD))
		return false;

	/* L_CBD */
	if (!m_stream->write_short((uint16_t)(cbd_size - 2U)))
		return false;

	/* Ncbd */
	if (!m_stream->write_short(image->numcomps))
		return false;

	for (i = 0; i < image->numcomps; ++i) {
		auto comp = image->comps + i;
		/* Component bit depth */
		uint8_t bpc = (uint8_t) (comp->prec - 1);
		if (comp->sgnd)
			bpc = (uint8_t)(bpc + (1 << 7));
		if (!m_stream->write_byte(bpc))
			return false;
	}
	return true;
}

bool CodeStreamCompress::write_tlm_begin() {
	if (!m_cp.tlm_markers)
		m_cp.tlm_markers = new TileLengthMarkers(m_stream);

	return m_cp.tlm_markers->writeBegin(
			m_compressorState.m_total_tile_parts);
}
bool CodeStreamCompress::write_tlm_end() {

	return m_cp.tlm_markers->writeEnd();
}
uint32_t CodeStreamCompress::get_SPCod_SPCoc_size( uint32_t comp_no) {
	auto cp = &(m_cp);
	auto tcp = &cp->tcps[0];
	auto tccp = &tcp->tccps[comp_no];
	assert(comp_no < getHeaderImage()->numcomps);

	uint32_t rc = SPCod_SPCoc_len;
	if (tccp->csty & J2K_CCP_CSTY_PRT)
		rc += tccp->numresolutions;

	return rc;
}
bool CodeStreamCompress::compare_SPCod_SPCoc(
	uint32_t first_comp_no, uint32_t second_comp_no) {
	auto cp = &(m_cp);
	auto tcp = &cp->tcps[0];
	auto tccp0 = &tcp->tccps[first_comp_no];
	auto tccp1 = &tcp->tccps[second_comp_no];

	if (tccp0->numresolutions != tccp1->numresolutions)
		return false;
	if (tccp0->cblkw != tccp1->cblkw)
		return false;
	if (tccp0->cblkh != tccp1->cblkh)
		return false;
	if (tccp0->cblk_sty != tccp1->cblk_sty)
		return false;
	if (tccp0->qmfbid != tccp1->qmfbid)
		return false;
	if ((tccp0->csty & J2K_CCP_CSTY_PRT) != (tccp1->csty & J2K_CCP_CSTY_PRT))
		return false;
	for (uint32_t i = 0U; i < tccp0->numresolutions; ++i) {
		if (tccp0->precinctWidthExp[i] != tccp1->precinctWidthExp[i])
			return false;
		if (tccp0->precinctHeightExp[i] != tccp1->precinctHeightExp[i])
			return false;
	}

	return true;
}

bool CodeStreamCompress::write_SPCod_SPCoc(uint32_t comp_no) {
	auto cp = &(m_cp);
	auto tcp = &cp->tcps[0];
	auto tccp = &tcp->tccps[comp_no];

	assert(comp_no < (getHeaderImage()->numcomps));

	/* SPcoc (D) */
	if (!m_stream->write_byte((uint8_t) (tccp->numresolutions - 1)))
		return false;
	/* SPcoc (E) */
	if (!m_stream->write_byte((uint8_t) (tccp->cblkw - 2)))
		return false;
	/* SPcoc (F) */
	if (!m_stream->write_byte((uint8_t) (tccp->cblkh - 2)))
		return false;
	/* SPcoc (G) */
	if (!m_stream->write_byte(tccp->cblk_sty))
		return false;
	/* SPcoc (H) */
	if (!m_stream->write_byte((uint8_t) tccp->qmfbid))
		return false;

	if (tccp->csty & J2K_CCP_CSTY_PRT) {
		for (uint32_t i = 0; i < tccp->numresolutions; ++i) {
			/* SPcoc (I_i) */
			if (!m_stream->write_byte(
					(uint8_t) (tccp->precinctWidthExp[i] + (tccp->precinctHeightExp[i] << 4)))) {
				return false;
			}
		}
	}

	return true;
}
uint32_t CodeStreamCompress::get_SQcd_SQcc_size( uint32_t comp_no) {
	auto cp = &(m_cp);
	auto tcp = &cp->tcps[0];
	auto tccp = &tcp->tccps[comp_no];

	return tccp->quant.get_SQcd_SQcc_size(this, comp_no);
}
bool CodeStreamCompress::compare_SQcd_SQcc(	uint32_t first_comp_no, uint32_t second_comp_no) {
	auto cp = &(m_cp);
	auto tcp = &cp->tcps[0];
	auto tccp0 = &tcp->tccps[first_comp_no];

	return tccp0->quant.compare_SQcd_SQcc(this, first_comp_no,
			second_comp_no);
}
bool CodeStreamCompress::write_SQcd_SQcc( uint32_t comp_no) {
	auto cp = &(m_cp);
	auto tcp = &cp->tcps[0];
	auto tccp = &tcp->tccps[comp_no];

	return tccp->quant.write_SQcd_SQcc(this, comp_no, m_stream);
}
uint16_t CodeStreamCompress::getPocSize(uint32_t nb_comp, uint32_t nb_poc) {
	uint32_t poc_room = (nb_comp <= 256) ? 1 : 2;

	return (uint16_t) (4 + (5 + 2 * poc_room) * nb_poc);
}
bool CodeStreamCompress::check_poc_val(const grk_progression *p_pocs, uint32_t nb_pocs,
		uint32_t nb_resolutions, uint32_t num_comps, uint32_t num_layers) {
	uint32_t index, resno, compno, layno;
	uint32_t i;
	uint32_t step_c = 1;
	uint32_t step_r = num_comps * step_c;
	uint32_t step_l = nb_resolutions * step_r;
	if (nb_pocs == 0)
		return true;

	auto packet_array = new uint32_t[step_l * num_layers];
	memset(packet_array, 0, step_l * num_layers * sizeof(uint32_t));

	/* iterate through all the pocs */
	for (i = 0; i < nb_pocs; ++i) {
		index = step_r * p_pocs->resS;
		/* take each resolution for each poc */
		for (resno = p_pocs->resS;	resno < std::min<uint32_t>(p_pocs->resE, nb_resolutions);++resno) {
			uint32_t res_index = index + p_pocs->compS * step_c;

			/* take each comp of each resolution for each poc */
			for (compno = p_pocs->compS;compno < std::min<uint32_t>(p_pocs->compE, num_comps);	++compno) {
				uint32_t comp_index = res_index + 0 * step_l;

				/* and finally take each layer of each res of ... */
				for (layno = 0;
						layno < std::min<uint32_t>(p_pocs->layE, num_layers);
						++layno) {
					/*index = step_r * resno + step_c * compno + step_l * layno;*/
					packet_array[comp_index] = 1;
					comp_index += step_l;
				}
				res_index += step_c;
			}
			index += step_r;
		}
		++p_pocs;
	}
	bool loss = false;
	index = 0;
	for (layno = 0; layno < num_layers; ++layno) {
		for (resno = 0; resno < nb_resolutions; ++resno) {
			for (compno = 0; compno < num_comps; ++compno) {
				loss |= (packet_array[index] != 1);
				index += step_c;
			}
		}
	}
	if (loss)
		GRK_ERROR("Missing packets possible loss of data");
	delete[] packet_array;

	return !loss;
}
bool CodeStreamCompress::init_mct_encoding(TileCodingParams *p_tcp, GrkImage *p_image) {
	uint32_t i;
	uint32_t indix = 1;
	grk_mct_data *mct_deco_data = nullptr, *mct_offset_data = nullptr;
	grk_simple_mcc_decorrelation_data *mcc_data;
	uint32_t mct_size, nb_elem;
	float *data, *current_data;
	TileComponentCodingParams *tccp;

	assert(p_tcp != nullptr);

	if (p_tcp->mct != 2)
		return true;

	if (p_tcp->m_mct_decoding_matrix) {
		if (p_tcp->m_nb_mct_records == p_tcp->m_nb_max_mct_records) {
			p_tcp->m_nb_max_mct_records += default_number_mct_records;

			auto new_mct_records = (grk_mct_data*) grkRealloc(p_tcp->m_mct_records,
					p_tcp->m_nb_max_mct_records * sizeof(grk_mct_data));
			if (!new_mct_records) {
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
					(p_tcp->m_nb_max_mct_records - p_tcp->m_nb_mct_records)
							* sizeof(grk_mct_data));
		}
		mct_deco_data = p_tcp->m_mct_records + p_tcp->m_nb_mct_records;
		grkFree(mct_deco_data->m_data);
		mct_deco_data->m_data = nullptr;

		mct_deco_data->m_index = indix++;
		mct_deco_data->m_array_type = MCT_TYPE_DECORRELATION;
		mct_deco_data->m_element_type = MCT_TYPE_FLOAT;
		nb_elem = (uint32_t)p_image->numcomps * p_image->numcomps;
		mct_size = nb_elem * MCT_ELEMENT_SIZE[mct_deco_data->m_element_type];
		mct_deco_data->m_data = (uint8_t*) grkMalloc(mct_size);

		if (!mct_deco_data->m_data)
			return false;

		j2k_mct_write_functions_from_float[mct_deco_data->m_element_type](
				p_tcp->m_mct_decoding_matrix, mct_deco_data->m_data, nb_elem);

		mct_deco_data->m_data_size = mct_size;
		++p_tcp->m_nb_mct_records;
	}

	if (p_tcp->m_nb_mct_records == p_tcp->m_nb_max_mct_records) {
		grk_mct_data *new_mct_records;
		p_tcp->m_nb_max_mct_records += default_number_mct_records;
		new_mct_records = (grk_mct_data*) grkRealloc(p_tcp->m_mct_records,
				p_tcp->m_nb_max_mct_records * sizeof(grk_mct_data));
		if (!new_mct_records) {
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
				(p_tcp->m_nb_max_mct_records - p_tcp->m_nb_mct_records)
						* sizeof(grk_mct_data));
		if (mct_deco_data)
			mct_deco_data = mct_offset_data - 1;
	}
	mct_offset_data = p_tcp->m_mct_records + p_tcp->m_nb_mct_records;
	if (mct_offset_data->m_data) {
		grkFree(mct_offset_data->m_data);
		mct_offset_data->m_data = nullptr;
	}
	mct_offset_data->m_index = indix++;
	mct_offset_data->m_array_type = MCT_TYPE_OFFSET;
	mct_offset_data->m_element_type = MCT_TYPE_FLOAT;
	nb_elem = p_image->numcomps;
	mct_size = nb_elem * MCT_ELEMENT_SIZE[mct_offset_data->m_element_type];
	mct_offset_data->m_data = (uint8_t*) grkMalloc(mct_size);
	if (!mct_offset_data->m_data)
		return false;

	data = (float*) grkMalloc(nb_elem * sizeof(float));
	if (!data) {
		grkFree(mct_offset_data->m_data);
		mct_offset_data->m_data = nullptr;
		return false;
	}
	tccp = p_tcp->tccps;
	current_data = data;

	for (i = 0; i < nb_elem; ++i) {
		*(current_data++) = (float) (tccp->m_dc_level_shift);
		++tccp;
	}
	j2k_mct_write_functions_from_float[mct_offset_data->m_element_type](data,
			mct_offset_data->m_data, nb_elem);
	grkFree(data);
	mct_offset_data->m_data_size = mct_size;
	++p_tcp->m_nb_mct_records;

	if (p_tcp->m_nb_mcc_records == p_tcp->m_nb_max_mcc_records) {
		grk_simple_mcc_decorrelation_data *new_mcc_records;
		p_tcp->m_nb_max_mcc_records += default_number_mct_records;
		new_mcc_records = (grk_simple_mcc_decorrelation_data*) grkRealloc(
				p_tcp->m_mcc_records,
				p_tcp->m_nb_max_mcc_records
						* sizeof(grk_simple_mcc_decorrelation_data));
		if (!new_mcc_records) {
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
				(p_tcp->m_nb_max_mcc_records - p_tcp->m_nb_mcc_records)
						* sizeof(grk_simple_mcc_decorrelation_data));

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
uint64_t CodeStreamCompress::get_num_tp(CodingParams *cp, uint32_t pino,	uint16_t tileno) {
	uint64_t num_tp = 1;

	/*  preconditions */
	assert(tileno < (cp->t_grid_width * cp->t_grid_height));
	assert(pino < (cp->tcps[tileno].numpocs + 1));

	/* get the given tile coding parameter */
	auto tcp = &cp->tcps[tileno];
	assert(tcp != nullptr);

	auto current_poc = &(tcp->progressionOrderChange[pino]);
	assert(current_poc != 0);

	/* get the progression order as a character string */
	auto prog = convertProgressionOrder(tcp->prg);
	assert(strlen(prog) > 0);

	if (cp->m_coding_params.m_enc.m_tp_on) {
		for (uint32_t i = 0; i < 4; ++i) {
			switch (prog[i]) {
			/* component wise */
			case 'C':
				num_tp *= current_poc->tpCompE;
				break;
				/* resolution wise */
			case 'R':
				num_tp *= current_poc->tpResE;
				break;
				/* precinct wise */
			case 'P':
				num_tp *= current_poc->tpPrecE;
				break;
				/* layer wise */
			case 'L':
				num_tp *= current_poc->tpLayE;
				break;
			}
			//we start a new tile part with every progression change
			if (cp->m_coding_params.m_enc.m_tp_flag == prog[i]) {
				cp->m_coding_params.m_enc.m_tp_pos = i;
				break;
			}
		}
	} else {
		num_tp = 1;
	}
	return num_tp;
}
bool CodeStreamCompress::calculate_tp(CodingParams *cp, uint16_t *p_nb_tile_parts,
		GrkImage *image) {
	assert(p_nb_tile_parts != nullptr);
	assert(cp != nullptr);
	assert(image != nullptr);

	uint32_t nb_tiles = (uint16_t) (cp->t_grid_width * cp->t_grid_height);
	*p_nb_tile_parts = 0;
	auto tcp = cp->tcps;
	for (uint16_t tileno = 0; tileno < nb_tiles; ++tileno) {
		uint8_t totnum_tp = 0;
		pi_update_params_compress(image, cp, tileno);
		for (uint32_t pino = 0; pino <= tcp->numpocs; ++pino) {
			uint64_t num_tp = get_num_tp(cp, pino, tileno);
			if (num_tp > max_num_tile_parts_per_tile){
				GRK_ERROR("Number of tile parts %d exceeds maximum number of "
						"tile parts %d", num_tp,max_num_tile_parts_per_tile );
				return false;
			}

			uint64_t total = num_tp + *p_nb_tile_parts;
			if (total > max_num_tile_parts){
				GRK_ERROR("Total number of tile parts %d exceeds maximum total number of "
						"tile parts %d", total,max_num_tile_parts );
				return false;
			}

			*p_nb_tile_parts = (uint16_t)(*p_nb_tile_parts + num_tp);
			totnum_tp = (uint8_t) (totnum_tp + num_tp);
		}
		tcp->m_nb_tile_parts = totnum_tp;
		++tcp;
	}

	return true;
}

}

