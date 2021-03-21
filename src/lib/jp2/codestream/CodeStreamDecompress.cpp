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
#include "ojph_arch.h"

namespace grk {

static void j2k_read_int16_to_float(const void *p_src_data, void *p_dest_data,	uint64_t nb_elem) {
	j2k_write<int16_t, float>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_int32_to_float(const void *p_src_data, void *p_dest_data,	uint64_t nb_elem) {
	j2k_write<int32_t, float>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_float32_to_float(const void *p_src_data, void *p_dest_data,uint64_t nb_elem) {
	j2k_write<float, float>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_float64_to_float(const void *p_src_data, void *p_dest_data,uint64_t nb_elem) {
	j2k_write<double, float>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_int16_to_int32(const void *p_src_data, void *p_dest_data,uint64_t nb_elem) {
	j2k_write<int16_t, int32_t>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_int32_to_int32(const void *p_src_data, void *p_dest_data,uint64_t nb_elem) {
	j2k_write<int32_t, int32_t>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_float32_to_int32(const void *p_src_data, void *p_dest_data,uint64_t nb_elem) {
	j2k_write<float, int32_t>(p_src_data, p_dest_data, nb_elem);
}
static void j2k_read_float64_to_int32(const void *p_src_data, void *p_dest_data,uint64_t nb_elem) {
	j2k_write<double, int32_t>(p_src_data, p_dest_data, nb_elem);
}

static const j2k_mct_function j2k_mct_read_functions_to_float[] = {
		j2k_read_int16_to_float, j2k_read_int32_to_float,
		j2k_read_float32_to_float, j2k_read_float64_to_float };
static const j2k_mct_function j2k_mct_read_functions_to_int32[] = {
		j2k_read_int16_to_int32, j2k_read_int32_to_int32,
		j2k_read_float32_to_int32, j2k_read_float64_to_int32 };

CodeStreamDecompress::CodeStreamDecompress( BufferedStream *stream) :
		CodeStream(stream),
		wholeTileDecompress(true),
		m_curr_marker(0),
	    m_headerError(false),
		m_tile_ind_to_dec(-1),
		m_marker_scratch(nullptr),
		m_marker_scratch_size(0),
		m_output_image(nullptr),
		m_tileCache(new TileCache())
{
	m_decompressorState.m_default_tcp = new TileCodingParams();
	m_decompressorState.m_last_sot_read_pos = 0;

	/* code stream index creation */
	codeStreamInfo = new CodeStreamInfo();
	if (!codeStreamInfo) {
		delete m_decompressorState.m_default_tcp;
		throw std::runtime_error("Out of memory");
	}
	marker_map = {
	{J2K_MS_SOT, new marker_handler(J2K_MS_SOT,J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH_SOT,
			[this](uint8_t *data, uint16_t len) { return read_sot(data, len); } )},
	{J2K_MS_COD, new marker_handler(J2K_MS_COD,J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH,
			[this](uint8_t *data, uint16_t len) { return read_cod(data, len); })},
	{J2K_MS_COC, new marker_handler(J2K_MS_COC,J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH,
			[this](uint8_t *data, uint16_t len) { return read_coc(data, len); })},
	{J2K_MS_RGN, new marker_handler(J2K_MS_RGN,J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH,
			[this](uint8_t *data, uint16_t len) { return read_rgn(data, len); })},
	{J2K_MS_QCD, new marker_handler(J2K_MS_QCD,J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH,
			[this](uint8_t *data, uint16_t len) { return read_qcd(data, len); })},
	{J2K_MS_QCC, new marker_handler(J2K_MS_QCC,J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH,
			[this](uint8_t *data, uint16_t len) { return read_qcc(data, len); })},
	{J2K_MS_POC, new marker_handler(J2K_MS_POC,J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH,
			[this](uint8_t *data, uint16_t len) { return read_poc(data, len); })},
	{J2K_MS_SIZ, new marker_handler(J2K_MS_SIZ,J2K_DEC_STATE_MH_SIZ,
			[this](uint8_t *data, uint16_t len) { return read_siz(data, len); })},
	{J2K_MS_CAP, new marker_handler(J2K_MS_CAP,J2K_DEC_STATE_MH,
			[this](uint8_t *data, uint16_t len) { return read_cap(data, len); })},
	{J2K_MS_TLM, new marker_handler(J2K_MS_TLM,J2K_DEC_STATE_MH,
			[this](uint8_t *data, uint16_t len) { return read_tlm(data, len); })},
	{J2K_MS_PLM, new marker_handler(J2K_MS_PLM,J2K_DEC_STATE_MH,
			[this](uint8_t *data, uint16_t len) { return read_plm(data, len); })},
	{J2K_MS_PLT, new marker_handler(J2K_MS_PLT,J2K_DEC_STATE_TPH,
			[this](uint8_t *data, uint16_t len) { return read_plt(data, len); })},
	{J2K_MS_PPM, new marker_handler(J2K_MS_PPM,J2K_DEC_STATE_MH,
			[this](uint8_t *data, uint16_t len) { return read_ppm(data, len); })},
	{J2K_MS_PPT, new marker_handler(J2K_MS_PPT,J2K_DEC_STATE_TPH,
			[this](uint8_t *data, uint16_t len) { return read_ppt(data, len); })},
	{J2K_MS_CRG, new marker_handler(J2K_MS_CRG,J2K_DEC_STATE_MH,
			[this](uint8_t *data, uint16_t len) { return read_crg(data, len); })},
	{J2K_MS_COM, new marker_handler(J2K_MS_COM,J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH,
			[this](uint8_t *data, uint16_t len) { return read_com(data, len); })},
	{J2K_MS_MCT, new marker_handler(J2K_MS_MCT,J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH,
			[this](uint8_t *data, uint16_t len) { return read_mct(data, len); })},
	{J2K_MS_CBD, new marker_handler(J2K_MS_CBD,J2K_DEC_STATE_MH,
			[this](uint8_t *data, uint16_t len) { return read_cbd(data, len); })},
	{J2K_MS_MCC, new marker_handler(J2K_MS_MCC,J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH,
			[this](uint8_t *data, uint16_t len) { return read_mcc(data, len); })},
	{J2K_MS_MCO, new marker_handler(J2K_MS_MCO,J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH,
			[this](uint8_t *data, uint16_t len) { return read_mco(data, len); })}
	};
}
CodeStreamDecompress::~CodeStreamDecompress() {
	for (auto &val : marker_map)
		delete val.second;
	delete m_decompressorState.m_default_tcp;
	delete[] m_marker_scratch;
	if (m_output_image)
		grk_object_unref(&m_output_image->obj);
	delete m_tileCache;
}
GrkImage* CodeStreamDecompress::getCompositeImage(){
	return m_tileCache->getComposite();
}
TileProcessor* CodeStreamDecompress::allocateProcessor(uint16_t tileIndex){
	auto tileCache = m_tileCache->get(tileIndex);
	auto tileProcessor = tileCache ? tileCache->processor : nullptr;
	if (!tileProcessor){
		tileProcessor = new TileProcessor(this,m_stream,false,wholeTileDecompress);
		tileProcessor->m_tile_index = tileIndex;
		m_tileCache->put(tileIndex, tileProcessor);
	}
	m_currentTileProcessor = tileProcessor;
	if (!m_multiTile){
		if (m_output_image)
			grk_object_unref(&m_output_image->obj);
		m_output_image = nullptr;
	}
	if (!m_output_image) {
		m_output_image = new GrkImage();
		getCompositeImage()->copyHeader(m_output_image);
	}

	return m_currentTileProcessor;
}
TileCodingParams* CodeStreamDecompress::get_current_decode_tcp() {
    auto tileProcessor = m_currentTileProcessor;

	return (isDecodingTilePartHeader()) ?
			m_cp.tcps + tileProcessor->m_tile_index :
			m_decompressorState.m_default_tcp;
}
CodeStreamInfo* CodeStreamDecompress::getCodeStreamInfo(void){
	return codeStreamInfo;
}
bool CodeStreamDecompress::isDecodingTilePartHeader() {
	return (m_decompressorState.getState() & J2K_DEC_STATE_TPH);
}
DecompressorState* CodeStreamDecompress::getDecompressorState(void){
	return &m_decompressorState;
}
GrkImage* CodeStreamDecompress::getImage(uint16_t tileIndex){
	auto entry = m_tileCache->get(tileIndex);
	return entry ? entry->processor->getImage() : nullptr;
}
std::vector<GrkImage*> CodeStreamDecompress::getAllImages(void){
	return m_tileCache->getAllImages();
}
GrkImage* CodeStreamDecompress::getImage(){
	return getCompositeImage();
}
bool CodeStreamDecompress::readHeader(grk_header_info  *header_info){
	if (m_headerError)
		return false;

	if (!m_headerImage) {
		m_headerImage = new GrkImage();

		/* customization of the validation */
		m_validation_list.push_back(std::bind(&CodeStreamDecompress::decompressValidation, this));

		/* validation of the parameters codec */
		if (!exec(m_validation_list)){
			m_headerError = true;
			return false;
		}

		m_procedure_list.push_back(std::bind(&CodeStreamDecompress::readHeaderProcedure, this));
		// custom procedures here
		m_procedure_list.push_back(std::bind(&CodeStreamDecompress::copy_default_tcp, this));

		/* read header */
		if (!exec(m_procedure_list)){
			m_headerError = true;
			return false;
		}

		/* Copy code stream image information to composite image */
		m_headerImage->copyHeader(getCompositeImage());
	}

	if (header_info) {
		CodingParams *cp = nullptr;
		TileCodingParams *tcp = nullptr;
		TileComponentCodingParams *tccp = nullptr;

		cp = &(m_cp);
		tcp = m_decompressorState.m_default_tcp;
		tccp = &tcp->tccps[0];

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
		for (uint32_t i = 0; i < header_info->numresolutions; ++i) {
			header_info->prcw_init[i] = 1U << tccp->precinctWidthExp[i];
			header_info->prch_init[i] = 1U << tccp->precinctHeightExp[i];
		}
		header_info->tx0 = m_cp.tx0;
		header_info->ty0 = m_cp.ty0;

		header_info->t_width = m_cp.t_width;
		header_info->t_height = m_cp.t_height;

		header_info->t_grid_width = m_cp.t_grid_width;
		header_info->t_grid_height = m_cp.t_grid_height;

		header_info->tcp_numlayers = tcp->numlayers;

		header_info->num_comments = m_cp.num_comments;
		for (size_t i = 0; i < header_info->num_comments; ++i) {
			header_info->comment[i] = m_cp.comment[i];
			header_info->comment_len[i] = m_cp.comment_len[i];
			header_info->isBinaryComment[i] = m_cp.isBinaryComment[i];
		}
	}
	return true;
}
bool CodeStreamDecompress::setDecompressWindow(grkRectU32 window) {
	auto cp = &(m_cp);
	auto image = m_headerImage;
	auto compositeImage = getCompositeImage();
	auto decompressor = &m_decompressorState;

	/* Check if we have read the main header */
	if (decompressor->getState() != J2K_DEC_STATE_TPH_SOT) {
		GRK_ERROR("Need to read the main header before setting decompress window");
		return false;
	}

	if (window == grkRectU32(0,0,0,0)) {
		decompressor->m_start_tile_x_index = 0;
		decompressor->m_start_tile_y_index = 0;
		decompressor->m_end_tile_x_index = cp->t_grid_width;
		decompressor->m_end_tile_y_index = cp->t_grid_height;
		return true;
	}

	/* Check if the window provided by the user are correct */

	uint32_t start_x = window.x0 + image->x0;
	uint32_t start_y = window.y0 + image->y0;
	uint32_t end_x 	 = window.x1 + image->x0;
	uint32_t end_y   = window.y1 + image->y0;
	/* Left */
	if (start_x > image->x1) {
		GRK_ERROR("Left position of the decompress window (%u)"
				" is outside of the image area (Xsiz=%u).", start_x, image->x1);
		return false;
	} else {
		decompressor->m_start_tile_x_index = (start_x - cp->tx0) / cp->t_width;
		compositeImage->x0 = start_x;
	}

	/* Up */
	if (start_y > image->y1) {
		GRK_ERROR("Top position of the decompress window (%u)"
				" is outside of the image area (Ysiz=%u).", start_y, image->y1);
		return false;
	} else {
		decompressor->m_start_tile_y_index = (start_y - cp->ty0) / cp->t_height;
		compositeImage->y0 = start_y;
	}

	/* Right */
	assert(end_x > 0);
	assert(end_y > 0);
	if (end_x > image->x1) {
		GRK_WARN("Right position of the decompress window (%u)"
				" is outside the image area (Xsiz=%u).", end_x, image->x1);
		decompressor->m_end_tile_x_index = cp->t_grid_width;
		compositeImage->x1 = image->x1;
	} else {
		// avoid divide by zero
		if (cp->t_width == 0)
			return false;
		decompressor->m_end_tile_x_index = ceildiv<uint32_t>(end_x - cp->tx0,
				cp->t_width);
		compositeImage->x1 = end_x;
	}

	/* Bottom */
	if (end_y > image->y1) {
		GRK_WARN("Bottom position of the decompress window (%u)"
				" is outside of the image area (Ysiz=%u).", end_y, image->y1);
		decompressor->m_end_tile_y_index = cp->t_grid_height;
		compositeImage->y1 = image->y1;
	} else {
		// avoid divide by zero
		if (cp->t_height == 0)
			return false;
		decompressor->m_end_tile_y_index = ceildiv<uint32_t>(end_y - cp->ty0,
				cp->t_height);
		compositeImage->y1 = end_y;
	}
	wholeTileDecompress = false;
	if (!compositeImage->subsampleAndReduce(cp->m_coding_params.m_dec.m_reduce))
		return false;

	GRK_INFO("Decompress window set to (%d,%d,%d,%d)",
			compositeImage->x0 - image->x0,
			compositeImage->y0 - image->y0,
			compositeImage->x1 - image->x0,
			compositeImage->y1 - image->y0);

	return true;
}
void CodeStreamDecompress::initDecompress(grk_dparameters  *parameters){
	if (parameters) {
		m_cp.m_coding_params.m_dec.m_layer = parameters->cp_layer;
		m_cp.m_coding_params.m_dec.m_reduce = parameters->cp_reduce;
		m_tileCache->setStrategy(parameters->tileCacheStrategy);
	}
}
bool CodeStreamDecompress::decompress( grk_plugin_tile *tile){
	/* customization of the decoding */
	m_procedure_list.push_back(std::bind(&CodeStreamDecompress::decompressTiles,this));
	current_plugin_tile = tile;

	return decompressExec();
}
bool CodeStreamDecompress::decompressTile(uint16_t tileIndex){
	auto entry = m_tileCache->get(tileIndex);
	if (entry && entry->processor && entry->processor->getImage())
		return true;

	// another tile has already been decoded
	if (m_output_image){
		/* Copy code stream image information to composite image */
		m_headerImage->copyHeader(getCompositeImage());
	}

	uint16_t numTilesToDecompress = (uint16_t)(m_cp.t_grid_width * m_cp.t_grid_height);
	if (codeStreamInfo) {
		if (!codeStreamInfo->allocTileInfo(numTilesToDecompress)) {
			m_headerError = true;
			return false;
		}
	}

	auto compositeImage = getCompositeImage();
	if (tileIndex >= numTilesToDecompress) {
		GRK_ERROR(	"Tile index %u is greater than maximum tile index %u",	tileIndex,
				numTilesToDecompress - 1);
		return false;
	}

	/* Compute the dimension of the desired tile*/
	uint32_t tile_x = tileIndex % m_cp.t_grid_width;
	uint32_t tile_y = tileIndex / m_cp.t_grid_width;

	auto imageBounds = grkRectU32(compositeImage->x0,
									compositeImage->y0,
									compositeImage->x1,
									compositeImage->y1);
	auto tileBounds = m_cp.getTileBounds(compositeImage, tile_x, tile_y);
	// crop tile bounds with image bounds
	auto croppedImageBounds = imageBounds.intersection(tileBounds);
	if (imageBounds.non_empty() && tileBounds.non_empty() && croppedImageBounds.non_empty()) {
		compositeImage->x0 = (uint32_t) croppedImageBounds.x0;
		compositeImage->y0 = (uint32_t) croppedImageBounds.y0;
		compositeImage->x1 = (uint32_t) croppedImageBounds.x1;
		compositeImage->y1 = (uint32_t) croppedImageBounds.y1;
	} else {
		GRK_WARN("Decompress bounds <%u,%u,%u,%u> do not overlap with requested tile %u. Decompressing full image",
				imageBounds.x0,
				imageBounds.y0,
				imageBounds.x1,
				imageBounds.y1,
				tileIndex);
		croppedImageBounds = imageBounds;
	}

	auto reduce = m_cp.m_coding_params.m_dec.m_reduce;
	for (uint32_t compno = 0; compno < compositeImage->numcomps; ++compno) {
		auto comp = compositeImage->comps + compno;
		auto compBounds = croppedImageBounds.rectceildiv(comp->dx,comp->dy);
		auto reducedCompBounds = compBounds.rectceildivpow2(reduce);
		comp->x0 = reducedCompBounds.x0;
		comp->y0 = reducedCompBounds.y0;
		comp->w = reducedCompBounds.width();
		comp->h = reducedCompBounds.height();
	}
	m_tile_ind_to_dec = (int32_t) tileIndex;

	// reset tile part numbers, in case we are re-using the same codec object
	// from previous decompress
	for (uint32_t i = 0; i < numTilesToDecompress; ++i)
		m_cp.tcps[i].m_tile_part_index = -1;

	/* customization of the decoding */
	m_procedure_list.push_back([this] {return decompressTile();}   );

	return decompressExec();
}
bool CodeStreamDecompress::endOfCodeStream(void){
	return m_decompressorState.getState() == J2K_DEC_STATE_EOC ||
				m_decompressorState.getState() == J2K_DEC_STATE_NO_EOC ||
					m_stream->get_number_byte_left() == 0;
}
bool CodeStreamDecompress::decompressTiles(void) {
	uint16_t numTilesToDecompress = (uint16_t)(m_cp.t_grid_height* m_cp.t_grid_width);
	m_multiTile = numTilesToDecompress > 1;
	if (codeStreamInfo) {
		if (!codeStreamInfo->allocTileInfo(numTilesToDecompress)) {
			m_headerError = true;
			return false;
		}
	}
	std::vector< std::future<int> > results;
	std::atomic<bool> success(true);
	std::atomic<uint32_t> numTilesDecompressed(0);
	ThreadPool pool(std::min<uint32_t>((uint32_t)ThreadPool::get()->num_threads(), numTilesToDecompress));
	bool breakAfterT1 = false;
	bool canDecompress = true;
	if (endOfCodeStream()){
		if (m_tileCache->empty()){
			GRK_ERROR("No tiles were decompressed.");
			return false;
		}
		for (uint16_t i = 0; i < numTilesToDecompress; ++i){
			auto entry = m_tileCache->get(i);
			if (!entry || !entry->processor)
				continue;
			auto processor = entry->processor;
			auto exec = [this,processor,
						  numTilesToDecompress,
						  &numTilesDecompressed,
						  &success] {
				if (success) {
					if (!decompressT2T1(processor)){
						GRK_ERROR("Failed to decompress tile %u/%u",
								processor->m_tile_index + 1,numTilesToDecompress);
						success = false;
					} else {
						numTilesDecompressed++;
					}

				}
				return 0;
			};
			if (pool.num_threads() > 1)
				results.emplace_back(pool.enqueue(exec));
			else {
				exec();
				if (!success)
					goto cleanup;
			}
		}
		for(auto &result: results){
			result.get();
		}
		results.clear();
		if (!success)
			return false;

		return true;
	}
	while (!endOfCodeStream() && !breakAfterT1) {
		//1. read header
		try {
			if (!parseTileHeaderMarkers(&canDecompress)){
				success = false;
				goto cleanup;
			}
		} catch (InvalidMarkerException &ime){
			GRK_ERROR("Found invalid marker : 0x%x", ime.m_marker);
			success = false;
			goto cleanup;
		}
		if (!canDecompress)
			continue;
		if (!m_currentTileProcessor){
			GRK_ERROR("Missing SOT marker");
			success = false;
			goto cleanup;
		}
		//2. find next tile
		auto processor = m_currentTileProcessor;
		m_currentTileProcessor = nullptr;
		try {
			if (!findNextTile(processor)){
					GRK_ERROR("Failed to decompress tile %u/%u",
							processor->m_tile_index + 1,
							numTilesToDecompress);
					success = false;
					goto cleanup;
			}
		}  catch (DecodeUnknownMarkerAtEndOfTileException &e){
			GRK_UNUSED(e);
			breakAfterT1 = true;
		}
		//3. T2 + T1 decompress
		// once we schedule a processor for T1 compression, we will destroy it
		// regardless of success or not
		auto exec = [this,processor,
					  numTilesToDecompress,
					  &numTilesDecompressed,
					  &success] {
			if (success) {
				if (!decompressT2T1(processor)){
					GRK_ERROR("Failed to decompress tile %u/%u",
							processor->m_tile_index + 1,numTilesToDecompress);
					success = false;
				} else {
					numTilesDecompressed++;
				}

			}
			return 0;
		};
		if (pool.num_threads() > 1)
			results.emplace_back(pool.enqueue(exec));
		else {
			exec();
			if (!success)
				goto cleanup;
		}
	}
	for(auto &result: results){
		result.get();
	}
	results.clear();
	if (!success)
		return false;

	// check if there is another tile that has not been processed
	// we will reject if it has the TPSot problem
	// (https://github.com/uclouvain/openjpeg/issues/254)
	if (m_curr_marker == J2K_MS_SOT && m_stream->get_number_byte_left()){
		uint16_t marker_size;
		if (!read_short(&marker_size)){
			success = false;
			goto cleanup;
		}
		marker_size = (uint16_t)(marker_size - 2); /* Subtract the size of the marker ID already read */
		auto marker_handler = get_marker_handler(m_curr_marker);
		if (!(m_decompressorState.getState() & marker_handler->states)) {
			GRK_ERROR("Marker %d is not compliant with its position",m_curr_marker);
			success = false;
			goto cleanup;
		}
		if (!process_marker(marker_handler, marker_size)){
			success = false;
			goto cleanup;
		}
	}
	if (numTilesDecompressed == 0) {
		GRK_ERROR("No tiles were decompressed.");
		success = false;
		goto cleanup;
	} else if (numTilesDecompressed < numTilesToDecompress && wholeTileDecompress) {
		uint32_t decompressed = numTilesDecompressed;
		GRK_WARN("Only %u out of %u tiles were decompressed", decompressed,	numTilesToDecompress);
	}
cleanup:
	for(auto &result: results){
		result.get();
	}
	return success;
}
bool CodeStreamDecompress::copy_default_tcp(void) {
	auto image = m_headerImage;
	uint32_t nb_tiles = m_cp.t_grid_height * m_cp.t_grid_width;
	uint32_t tccp_size = image->numcomps * (uint32_t) sizeof(TileComponentCodingParams);
	auto default_tcp = m_decompressorState.m_default_tcp;
	uint32_t mct_size = (uint32_t)image->numcomps * image->numcomps * (uint32_t) sizeof(float);

	/* For each tile */
	for (uint32_t i = 0; i < nb_tiles; ++i) {
		auto tcp = m_cp.tcps + i;
		/* keep the tile-compo coding parameters pointer of the current tile coding parameters*/
		auto current_tccp = tcp->tccps;
		/*Copy default coding parameters into the current tile coding parameters*/
		*tcp = *default_tcp;
		/* Initialize some values of the current tile coding parameters*/
		tcp->cod = false;
		tcp->ppt = false;
		tcp->ppt_data = nullptr;
		/* Remove memory not owned by this tile in case of early error return. */
		tcp->m_mct_decoding_matrix = nullptr;
		tcp->m_nb_max_mct_records = 0;
		tcp->m_mct_records = nullptr;
		tcp->m_nb_max_mcc_records = 0;
		tcp->m_mcc_records = nullptr;
		/* Reconnect the tile-compo coding parameters pointer to the current tile coding parameters*/
		tcp->tccps = current_tccp;

		/* Get the mct_decoding_matrix of the dflt_tile_cp and copy them into the current tile cp*/
		if (default_tcp->m_mct_decoding_matrix) {
			tcp->m_mct_decoding_matrix = (float*) grkMalloc(mct_size);
			if (!tcp->m_mct_decoding_matrix)
				return false;
			memcpy(tcp->m_mct_decoding_matrix,
					default_tcp->m_mct_decoding_matrix, mct_size);
		}

		/* Get the mct_record of the dflt_tile_cp and copy them into the current tile cp*/
		uint32_t mct_records_size = default_tcp->m_nb_max_mct_records
				* (uint32_t) sizeof(grk_mct_data);
		tcp->m_mct_records = (grk_mct_data*) grkMalloc(mct_records_size);
		if (!tcp->m_mct_records)
			return false;
		memcpy(tcp->m_mct_records, default_tcp->m_mct_records,
				mct_records_size);

		/* Copy the mct record data from dflt_tile_cp to the current tile*/
		auto src_mct_rec = default_tcp->m_mct_records;
		auto dest_mct_rec = tcp->m_mct_records;

		for (uint32_t j = 0; j < default_tcp->m_nb_mct_records; ++j) {
			if (src_mct_rec->m_data) {
				dest_mct_rec->m_data = (uint8_t*) grkMalloc(
						src_mct_rec->m_data_size);
				if (!dest_mct_rec->m_data)
					return false;
				memcpy(dest_mct_rec->m_data, src_mct_rec->m_data,
						src_mct_rec->m_data_size);
			}
			++src_mct_rec;
			++dest_mct_rec;
			/* Update with each pass to free exactly what has been allocated on early return. */
			tcp->m_nb_max_mct_records += 1;
		}

		/* Get the mcc_record of the dflt_tile_cp and copy them into the current tile cp*/
		uint32_t mcc_records_size = default_tcp->m_nb_max_mcc_records
				* (uint32_t) sizeof(grk_simple_mcc_decorrelation_data);
		tcp->m_mcc_records = (grk_simple_mcc_decorrelation_data*) grkMalloc(
				mcc_records_size);
		if (!tcp->m_mcc_records)
			return false;
		memcpy(tcp->m_mcc_records, default_tcp->m_mcc_records,
				mcc_records_size);
		tcp->m_nb_max_mcc_records = default_tcp->m_nb_max_mcc_records;

		/* Copy the mcc record data from dflt_tile_cp to the current tile*/
		for (uint32_t j = 0; j < default_tcp->m_nb_max_mcc_records; ++j) {
			auto src_mcc_rec = default_tcp->m_mcc_records + j;
			auto dest_mcc_rec = tcp->m_mcc_records + j;
			if (src_mcc_rec->m_decorrelation_array) {
				uint32_t offset = (uint32_t) (src_mcc_rec->m_decorrelation_array
						- default_tcp->m_mct_records);
				dest_mcc_rec->m_decorrelation_array = tcp->m_mct_records
						+ offset;
			}
			if (src_mcc_rec->m_offset_array) {
				uint32_t offset = (uint32_t) (src_mcc_rec->m_offset_array
						- default_tcp->m_mct_records);
				dest_mcc_rec->m_offset_array = tcp->m_mct_records + offset;
			}
		}
		/* Copy all the dflt_tile_compo_cp to the current tile cp */
		memcpy(current_tccp, default_tcp->tccps, tccp_size);
	}

	return true;
}
void CodeStreamDecompress::addMainHeaderMarker(uint16_t id,
										uint64_t pos,
										uint32_t len) {
	assert(codeStreamInfo != nullptr);
	codeStreamInfo->pushMarker(id,pos,len);
}
uint16_t CodeStreamDecompress::getCurrentMarker(){
	return m_curr_marker;
}
bool   CodeStreamDecompress::isWholeTileDecompress(){
	return wholeTileDecompress;
}
GrkImage* CodeStreamDecompress::getHeaderImage(void){
	return m_headerImage;
}
int32_t CodeStreamDecompress::tileIndexToDecode(){
	return m_tile_ind_to_dec;
}
bool CodeStreamDecompress::readHeaderProcedure(void) {
	bool rc = false;
	try {
		rc = readHeaderProcedureImpl();
	} catch (InvalidMarkerException &ime){
		GRK_ERROR("Found invalid marker : 0x%x", ime.m_marker);
		rc = false;
	}
	return rc;
}
bool CodeStreamDecompress::readHeaderProcedureImpl(void) {
	bool has_siz = false;
	bool has_cod = false;
	bool has_qcd = false;

	/*  We enter in the main header */
	m_decompressorState.setState(J2K_DEC_STATE_MH_SOC);

	/* Try to read the SOC marker, the code stream must begin with SOC marker */
	if (!read_soc()) {
		GRK_ERROR("Code stream must begin with SOC marker ");
		return false;
	}
	// read next marker
	if (!readMarker())
		return false;

	if (m_curr_marker != J2K_MS_SIZ){
		GRK_ERROR("Code-stream must contain a valid SIZ marker segment, immediately after the SOC marker ");
		return false;
	}

	/* Try to read until the SOT is detected */
	while (m_curr_marker != J2K_MS_SOT) {

		/* Get the marker handler from the marker ID */
		auto marker_handler = get_marker_handler(m_curr_marker);

		/* Manage case where marker is unknown */
		if (!marker_handler) {
			if (!read_unk(&m_curr_marker)) {
				GRK_ERROR("Unable to read unknown marker 0x%02x.",
						m_curr_marker);
				return false;
			}
			continue;
		}

		if (marker_handler->id == J2K_MS_SIZ)
			has_siz = true;
		else if (marker_handler->id == J2K_MS_COD)
			has_cod = true;
		else if (marker_handler->id == J2K_MS_QCD)
			has_qcd = true;

		/* Check if the marker is known and if it is in the correct location (main, tile, end of code stream)*/
		if (!(m_decompressorState.getState() & marker_handler->states)) {
			GRK_ERROR("Marker %d is not compliant with its position",m_curr_marker);
			return false;
		}

		uint16_t marker_size;
		if (!read_short(&marker_size))
			return false;
		/* Check marker size (does not include marker ID but includes marker size) */
		else if (marker_size < 2) {
			GRK_ERROR("Marker size %d for marker 0x%x is less than 2",marker_size, marker_handler->id);
			return false;
		}
		else if (marker_size == 2) {
			GRK_ERROR("Zero-size marker in header.");
			return false;
		}
		marker_size = (uint16_t)(marker_size - 2); /* Subtract the size of the marker ID already read */

		if (!process_marker(marker_handler, marker_size))
			return false;

		if (codeStreamInfo) {
			/* Add the marker to the code stream index*/
			addMainHeaderMarker(marker_handler->id,
					m_stream->tell() - marker_size - 4U, marker_size + 4U);
		}
		// read next marker
		if (!readMarker())
			return false;
	}
	if (!has_siz) {
		GRK_ERROR("required SIZ marker not found in main header");
		return false;
	} else if (!has_cod) {
		GRK_ERROR("required COD marker not found in main header");
		return false;
	} else if (!has_qcd) {
		GRK_ERROR("required QCD marker not found in main header");
		return false;
	}
	if (!merge_ppm(&(m_cp))) {
		GRK_ERROR("Failed to merge PPM data");
		return false;
	}
	// we don't include the SOC marker, therefore subtract 2
	if (codeStreamInfo)
		codeStreamInfo->setMainHeaderEnd((uint32_t) m_stream->tell() - 2U);

	/* Next step: read a tile-part header */
	m_decompressorState.setState(J2K_DEC_STATE_TPH_SOT);

	return true;
}
bool CodeStreamDecompress::decompressExec(void){
	if (!exec(m_procedure_list))
		return false;
	if (m_multiTile) {
		if (!m_output_image->allocData())
			return false;
		auto images = m_tileCache->getTileImages();
		for (auto &img : images) {
			if (!m_output_image->compositeFrom(img))
				return false;
		}
	}
	m_output_image->transferDataTo(getCompositeImage());

	return true;
}
/*
 * Read and decompress one tile.
 */
bool CodeStreamDecompress::decompressTile() {
	m_multiTile = false;
	if (tileIndexToDecode() == -1) {
		GRK_ERROR("j2k_decompress_tile: Unable to decompress tile "
				"since first tile SOT has not been detected");
		return false;
	}
	auto tileCache = m_tileCache->get((uint16_t)tileIndexToDecode());
	auto tileProcessor = tileCache ? tileCache->processor : nullptr;
	bool rc = false;
	if (!tileCache || !tileCache->processor->getImage()) {
		if (!codeStreamInfo->allocTileInfo((uint16_t)(m_cp.t_grid_width * m_cp.t_grid_height)))
			return false;
		// if we have a TLM marker, then we can skip tiles until
		// we get to desired tile
		if (m_cp.tlm_markers){
			// for first SOT position, we add two to skip SOC marker
			if (!m_cp.tlm_markers->skipTo((uint16_t)tileIndexToDecode(),
											m_stream,codeStreamInfo->getMainHeaderEnd()+2))
				return false;
		} else {
			/* Move into the code stream to the first SOT used to decompress the desired tile */
			uint16_t tile_index_to_decompress =	(uint16_t) (tileIndexToDecode());
			if (codeStreamInfo->hasTileInfo() && codeStreamInfo->getTileInfo(0)->hasTilePartInfo()) {
				auto tileInfo = codeStreamInfo->getTileInfo(tile_index_to_decompress);
				if (!tileInfo->numTileParts) {
					/* the index for this tile has not been built,
					 *  so move to the last SOT read */
					if (!(m_stream->seek(m_decompressorState.m_last_sot_read_pos	+ 2))) {
						GRK_ERROR("Problem with seek function");
						return false;
					}
				} else {
					if (!(m_stream->seek(tileInfo->getTilePartInfo(0)->startPosition	+ 2))) {
						GRK_ERROR("Problem with seek function");
						return false;
					}
				}
			}
		}
		/* Special case if we have previously read the EOC marker
		 * (if the previous tile decompressed is the last ) */
		if (m_decompressorState.getState() == J2K_DEC_STATE_EOC)
			m_decompressorState.setState(J2K_DEC_STATE_TPH_SOT);
		bool canDecompress = true;
		try {
			if (!parseTileHeaderMarkers(&canDecompress))
				goto cleanup;
		} catch (InvalidMarkerException &ime){
			GRK_ERROR("Found invalid marker : 0x%x", ime.m_marker);
			goto cleanup;
		}
		tileProcessor = m_currentTileProcessor;
		try {
			if (!findNextTile(tileProcessor)){
					GRK_ERROR("Failed to decompress tile %u",
							tileProcessor->m_tile_index + 1);
					goto cleanup;
			}
		}  catch (DecodeUnknownMarkerAtEndOfTileException &e){
			GRK_UNUSED(e);
		}
		if (!decompressT2T1(tileProcessor))
			goto cleanup;
	}
	rc = true;
cleanup:

	return rc;
}
bool CodeStreamDecompress::decompressT2T1(TileProcessor *tileProcessor) {
	auto tcp = m_cp.tcps + tileProcessor->m_tile_index;
	if (!tcp->m_compressedTileData) {
		GRK_ERROR("Decompress: Tile %d has no compressed data", tileProcessor->m_tile_index+1);
		return false;
	}
	bool doPost = !current_plugin_tile ||
			(current_plugin_tile->decompress_flags & GRK_DECODE_POST_T1);
	if (!tileProcessor->decompressT2T1(tcp, m_output_image, m_multiTile, doPost)){
		m_decompressorState.orState(J2K_DEC_STATE_ERR);
		return false;
	}

	return true;
}
bool CodeStreamDecompress::findNextTile(TileProcessor *tileProcessor) {
	auto decompressor = &m_decompressorState;

	if (!(decompressor->getState() & J2K_DEC_STATE_DATA)){
	   GRK_ERROR("j2k_decompress_tile: no data.");
	   return false;
	}
	auto tcp = m_cp.tcps + tileProcessor->m_tile_index;
	if (!tcp->m_compressedTileData) {
		GRK_ERROR("Missing SOD marker");
		tcp->destroy();
		return false;
	}
	// find next tile
	bool rc = true;
	bool doPost = !tileProcessor->current_plugin_tile
			|| (tileProcessor->current_plugin_tile->decompress_flags
					& GRK_DECODE_POST_T1);
	if (doPost){
		rc =  decompressor->findNextTile(this);
	}
	return rc;
}
bool CodeStreamDecompress::decompressValidation(void) {
	bool is_valid = true;
	is_valid &=	(m_decompressorState.getState() == J2K_DEC_STATE_NONE);

	return is_valid;
}
bool CodeStreamDecompress::process_marker(const marker_handler* marker_handler, uint16_t marker_size){
	if (!m_marker_scratch) {
		m_marker_scratch = new uint8_t[default_header_size];
		m_marker_scratch_size = default_header_size;
	}
	if (marker_size > m_marker_scratch_size) {
		if (marker_size > m_stream->get_number_byte_left()) {
			GRK_ERROR("Marker size inconsistent with stream length");
			return false;
		}
		delete[] m_marker_scratch;
		m_marker_scratch = new uint8_t[2 * marker_size];
		m_marker_scratch_size = (uint16_t)(2U * marker_size);
	}
	if (m_stream->read(m_marker_scratch, marker_size) != marker_size) {
		GRK_ERROR("Stream too short");
		return false;
	}

	return marker_handler->func(m_marker_scratch, marker_size);
}
bool CodeStreamDecompress::read_short(uint16_t *val){
	uint8_t temp[2];
	if (m_stream->read(temp, 2) != 2)
		return false;

	grk_read<uint16_t>(temp, val);
	return true;
}
const marker_handler* CodeStreamDecompress::get_marker_handler(	uint16_t id) {
	auto iter = marker_map.find(id);
	if (iter != marker_map.end())
		return iter->second;
	else {
		GRK_WARN("Unknown marker 0x%02x detected.", id);
		return nullptr;
	}
}
bool CodeStreamDecompress::readMarker(){
	if (!read_short(&m_curr_marker))
		return false;

	/* Check if the current marker ID is valid */
	if (m_curr_marker < 0xff00) {
		GRK_WARN("marker ID 0x%.4x does not match JPEG 2000 marker format 0xffxx",
				m_curr_marker);
		throw InvalidMarkerException(m_curr_marker);
	}

	return true;
}
/**
 * Reads a POC marker (Progression Order Change)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool CodeStreamDecompress::read_poc( uint8_t *p_header_data,
		uint16_t header_size) {
	uint32_t old_poc_nb, current_poc_nb, current_poc_remaining;
	uint32_t chunk_size, comp_room;
	assert(p_header_data != nullptr);
	auto image = getHeaderImage();
	uint16_t maxNumResLevels = 0;
	auto tcp = get_current_decode_tcp();
	for (uint16_t i = 0; i < image->numcomps; ++i){
		if (tcp->tccps[i].numresolutions > maxNumResLevels)
			maxNumResLevels = tcp->tccps[i].numresolutions;
	}

	uint16_t nb_comp = image->numcomps;
	comp_room = (nb_comp <= 256) ? 1 : 2;
	chunk_size = 5 + 2 * comp_room;
	current_poc_nb = header_size / chunk_size;
	current_poc_remaining = header_size % chunk_size;

	if ((current_poc_nb == 0) || (current_poc_remaining != 0)) {
		GRK_ERROR("Error reading POC marker");
		return false;
	}
	old_poc_nb = tcp->POC ? tcp->numpocs + 1 : 0;
	current_poc_nb += old_poc_nb;
	if (current_poc_nb >= GRK_J2K_MAXRLVLS) {
		GRK_ERROR("read_poc: number of POCs %u exceeds Grok max %d", current_poc_nb, GRK_J2K_MAXRLVLS);
		return false;
	}

	/* now poc is in use.*/
	tcp->POC = true;

	for (uint32_t i = old_poc_nb; i < current_poc_nb; ++i) {
		auto current_prog = tcp->progressionOrderChange + i;
		/* RSpoc_i */
		grk_read<uint8_t>(p_header_data, &current_prog->resS);
		++p_header_data;
		if (current_prog->resS >= maxNumResLevels){
			GRK_ERROR("read_poc: invalid POC start resolution number %d", current_prog->resS);
			return false;
		}
		/* CSpoc_i */
		grk_read<uint16_t>(p_header_data, &(current_prog->compS), comp_room);
		p_header_data += comp_room;
		if (current_prog->compS > image->numcomps){
			GRK_ERROR("read_poc: invalid POC start component %d", current_prog->compS);
			return false;
		}
		/* LYEpoc_i */
		grk_read<uint16_t>(p_header_data, &(current_prog->layE));
		/* make sure layer end is in acceptable bounds */
		current_prog->layE = std::min<uint16_t>(current_prog->layE, tcp->numlayers);
		p_header_data += 2;
		/* REpoc_i */
		grk_read<uint8_t>(p_header_data, &current_prog->resE);
		++p_header_data;
		if (current_prog->resE <= current_prog->resS){
			GRK_ERROR("read_poc: invalid POC end resolution %d", current_prog->compS);
			return false;
		}
		/* CEpoc_i */
		grk_read<uint16_t>(p_header_data, &(current_prog->compE), comp_room);
		p_header_data += comp_room;
		current_prog->compE = std::min<uint16_t>(current_prog->compE,	nb_comp);
		if (current_prog->compE <= current_prog->compS){
			GRK_ERROR("read_poc: invalid POC end component %d", current_prog->compS);
			return false;
		}
		/* Ppoc_i */
		uint8_t tmp;
		grk_read<uint8_t>(p_header_data++, &tmp);
		if (tmp >= GRK_NUM_PROGRESSION_ORDERS) {
			GRK_ERROR("read_poc: unknown POC progression order %d", tmp);
			return false;
		}
		current_prog->progression = (GRK_PROG_ORDER) tmp;
	}
	tcp->numpocs = current_poc_nb - 1;
	return true;
}
/**
 * Reads a CRG marker (Component registration)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_crg( uint8_t *p_header_data,
		uint16_t header_size) {
	assert(p_header_data != nullptr);
	uint32_t nb_comp = getHeaderImage()->numcomps;
	if (header_size != nb_comp * 4) {
		GRK_ERROR("Error reading CRG marker");
		return false;
	}
	for (uint32_t i = 0; i < nb_comp; ++i) {
		auto comp = getHeaderImage()->comps + i;
		// Xcrg_i
		grk_read<uint16_t>(p_header_data, &comp->Xcrg);
		p_header_data += 2;
		// Xcrg_i
		grk_read<uint16_t>(p_header_data, &comp->Ycrg);
		p_header_data += 2;
	}
	return true;
}
/**
 * Reads a PLM marker (Packet length, main header marker)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_plm( uint8_t *p_header_data,
		uint16_t header_size) {
	assert(p_header_data != nullptr);
	if (!m_cp.plm_markers)
		m_cp.plm_markers = new PacketLengthMarkers();

	return m_cp.plm_markers->readPLM(p_header_data, header_size);
}
/**
 * Reads a PLT marker (Packet length, tile-part header)
 *
 * @param       this           JPEG 2000 code stream
 * @param       p_header_data   the data contained in the PLT box.
 * @param       header_size   the size of the data contained in the PLT marker.

 */
bool CodeStreamDecompress::read_plt( uint8_t *p_header_data,	uint16_t header_size) {
	assert(p_header_data != nullptr);
	auto tileProcessor = currentProcessor();
	if (!tileProcessor->plt_markers)
		tileProcessor->plt_markers = new PacketLengthMarkers();

	return tileProcessor->plt_markers->readPLT(p_header_data,header_size);
}
/**
 * Reads a PPM marker (Packed packet headers, main header)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_ppm(  uint8_t *p_header_data,
		uint16_t header_size) {
    if (!m_cp.ppm_marker) {
    	m_cp.ppm_marker = new PPMMarker();
    }
	return m_cp.ppm_marker->read(p_header_data, header_size);
}
/**
 * Merges all PPM markers read (Packed headers, main header)
 *
 * @param       p_cp      main coding parameters.

 */
bool CodeStreamDecompress::merge_ppm(CodingParams *p_cp) {
	return p_cp->ppm_marker ? p_cp->ppm_marker->merge() : true;
}
/**
 * Reads a PPT marker (Packed packet headers, tile-part header)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_ppt( uint8_t *p_header_data,
		uint16_t header_size) {
	assert(p_header_data != nullptr);
	uint32_t Z_ppt;
	auto tileProcessor = currentProcessor();

	/* We need to have the Z_ppt element + 1 byte of Ippt at minimum */
	if (header_size < 2) {
		GRK_ERROR("Error reading PPT marker");
		return false;
	}

	auto cp = &(m_cp);
	if (cp->ppm_marker) {
		GRK_ERROR(
				"Error reading PPT marker: packet header have been previously found in the main header (PPM marker).");
		return false;
	}

	auto tcp = &(cp->tcps[tileProcessor->m_tile_index]);
	tcp->ppt = true;

	/* Z_ppt */
	grk_read<uint32_t>(p_header_data++, &Z_ppt, 1);
	--header_size;

	/* check allocation needed */
	if (tcp->ppt_markers == nullptr) { /* first PPT marker */
		uint32_t newCount = Z_ppt + 1U; /* can't overflow, Z_ppt is UINT8 */
		assert(tcp->ppt_markers_count == 0U);

		tcp->ppt_markers = (grk_ppx*) grkCalloc(newCount, sizeof(grk_ppx));
		if (tcp->ppt_markers == nullptr) {
			GRK_ERROR("Not enough memory to read PPT marker");
			return false;
		}
		tcp->ppt_markers_count = newCount;
	} else if (tcp->ppt_markers_count <= Z_ppt) {
		uint32_t newCount = Z_ppt + 1U; /* can't overflow, Z_ppt is UINT8 */
		auto new_ppt_markers = (grk_ppx*) grkRealloc(tcp->ppt_markers,
				newCount * sizeof(grk_ppx));

		if (new_ppt_markers == nullptr) {
			/* clean up to be done on tcp destruction */
			GRK_ERROR("Not enough memory to read PPT marker");
			return false;
		}
		tcp->ppt_markers = new_ppt_markers;
		memset(tcp->ppt_markers + tcp->ppt_markers_count, 0,
				(newCount - tcp->ppt_markers_count) * sizeof(grk_ppx));
		tcp->ppt_markers_count = newCount;
	}

	if (tcp->ppt_markers[Z_ppt].m_data != nullptr) {
		/* clean up to be done on tcp destruction */
		GRK_ERROR("Zppt %u already read", Z_ppt);
		return false;
	}

	tcp->ppt_markers[Z_ppt].m_data = (uint8_t*) grkMalloc(header_size);
	if (tcp->ppt_markers[Z_ppt].m_data == nullptr) {
		/* clean up to be done on tcp destruction */
		GRK_ERROR("Not enough memory to read PPT marker");
		return false;
	}
	tcp->ppt_markers[Z_ppt].m_data_size = header_size;
	memcpy(tcp->ppt_markers[Z_ppt].m_data, p_header_data, header_size);
	return true;
}
/**
 * Merges all PPT markers read (Packed packet headers, tile-part header)
 *
 * @param       p_tcp   the tile.

 */
bool CodeStreamDecompress::merge_ppt(TileCodingParams *p_tcp) {
	assert(p_tcp != nullptr);
	assert(p_tcp->ppt_buffer == nullptr);
	if (!p_tcp->ppt)
		return true;

	if (p_tcp->ppt_buffer != nullptr) {
		GRK_ERROR("multiple calls to CodeStreamDecompress::merge_ppt()");
		return false;
	}

	uint32_t ppt_data_size = 0U;
	for (uint32_t i = 0U; i < p_tcp->ppt_markers_count; ++i) {
		ppt_data_size += p_tcp->ppt_markers[i].m_data_size; /* can't overflow, max 256 markers of max 65536 bytes */
	}

	p_tcp->ppt_buffer = new uint8_t[ppt_data_size];
	p_tcp->ppt_len = ppt_data_size;
	ppt_data_size = 0U;
	for (uint32_t i = 0U; i < p_tcp->ppt_markers_count; ++i) {
		if (p_tcp->ppt_markers[i].m_data != nullptr) { /* standard doesn't seem to require contiguous Zppt */
			memcpy(p_tcp->ppt_buffer + ppt_data_size,
					p_tcp->ppt_markers[i].m_data,
					p_tcp->ppt_markers[i].m_data_size);
			ppt_data_size += p_tcp->ppt_markers[i].m_data_size; /* can't overflow, max 256 markers of max 65536 bytes */

			grkFree(p_tcp->ppt_markers[i].m_data);
			p_tcp->ppt_markers[i].m_data = nullptr;
			p_tcp->ppt_markers[i].m_data_size = 0U;
		}
	}

	p_tcp->ppt_markers_count = 0U;
	grkFree(p_tcp->ppt_markers);
	p_tcp->ppt_markers = nullptr;

	p_tcp->ppt_data = p_tcp->ppt_buffer;
	p_tcp->ppt_data_size = p_tcp->ppt_len;

	return true;
}

/**
 * Read SOT (Start of tile part) marker
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_sot( uint8_t *p_header_data,
		uint16_t header_size) {
	SOTMarker sot;

	return sot.read(this,p_header_data, header_size);
}

/**
 * Reads a RGN marker (Region Of Interest)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool CodeStreamDecompress::read_rgn(  uint8_t *p_header_data,
		uint16_t header_size) {
	uint32_t comp_no, roi_sty;
	assert(p_header_data != nullptr);
	auto image = getHeaderImage();
	uint32_t nb_comp = image->numcomps;
	uint32_t comp_room = (nb_comp <= 256) ? 1 : 2;

	if (header_size != 2 + comp_room) {
		GRK_ERROR("Error reading RGN marker");
		return false;
	}

	auto tcp = get_current_decode_tcp();

	/* Crgn */
	grk_read<uint32_t>(p_header_data, &comp_no, comp_room);
	p_header_data += comp_room;
	/* Srgn */
	grk_read<uint32_t>(p_header_data++, &roi_sty, 1);
	if (roi_sty != 0) {
		GRK_ERROR("RGN marker RS value of %u is not supported by JPEG 2000 Part 1",roi_sty);
		return false;
	}

	/* testcase 3635.pdf.asan.77.2930 */
	if (comp_no >= nb_comp) {
		GRK_ERROR("bad component number in RGN (%u is >= number of components %u)",
				comp_no, nb_comp);
		return false;
	}

	/* SPrgn */
	grk_read<uint8_t>(p_header_data++,&(tcp->tccps[comp_no].roishift));
	if (tcp->tccps[comp_no].roishift >= 32){
		GRK_ERROR("Unsupported ROI shift : %u", tcp->tccps[comp_no].roishift);
		return false;
	}

	return true;
}
/**
 * Reads a MCO marker (Multiple Component Transform Ordering)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data.
 * @param       header_size     size of header data

 */
bool CodeStreamDecompress::read_mco( uint8_t *p_header_data,
		uint16_t header_size) {
	uint32_t tmp, i;
	uint32_t nb_stages;
	assert(p_header_data != nullptr);
	auto image = getHeaderImage();
	auto tcp = get_current_decode_tcp();

	if (header_size < 1) {
		GRK_ERROR("Error reading MCO marker");
		return false;
	}
	/* Nmco : only one transform stage*/
	grk_read<uint32_t>(p_header_data, &nb_stages, 1);
	++p_header_data;

	if (nb_stages > 1) {
		GRK_WARN("Cannot take in charge multiple transformation stages.");
		return true;
	}

	if (header_size != nb_stages + 1) {
		GRK_WARN("Error reading MCO marker");
		return false;
	}
	for (i = 0; i < image->numcomps; ++i) {
		auto tccp = tcp->tccps + i;
		tccp->m_dc_level_shift = 0;
	}
	grkFree(tcp->m_mct_decoding_matrix);
	tcp->m_mct_decoding_matrix = nullptr;

	for (i = 0; i < nb_stages; ++i) {
		grk_read<uint32_t>(p_header_data, &tmp, 1);
		++p_header_data;

		if (!CodeStreamDecompress::add_mct(tcp, getHeaderImage(), tmp))
			return false;
	}

	return true;
}
bool CodeStreamDecompress::add_mct(TileCodingParams *p_tcp, GrkImage *p_image, uint32_t index) {
	uint32_t i;
	assert(p_tcp != nullptr);
	auto mcc_record = p_tcp->m_mcc_records;

	for (i = 0; i < p_tcp->m_nb_mcc_records; ++i) {
		if (mcc_record->m_index == index)
			break;
	}

	if (i == p_tcp->m_nb_mcc_records) {
		/** element discarded **/
		return true;
	}

	if (mcc_record->m_nb_comps != p_image->numcomps) {
		/** do not support number of comps != image */
		return true;
	}
	auto deco_array = mcc_record->m_decorrelation_array;
	if (deco_array) {
		uint32_t data_size = MCT_ELEMENT_SIZE[deco_array->m_element_type]
				* p_image->numcomps * p_image->numcomps;
		if (deco_array->m_data_size != data_size)
			return false;

		uint32_t nb_elem = (uint32_t)p_image->numcomps * p_image->numcomps;
		uint32_t mct_size = nb_elem * (uint32_t) sizeof(float);
		p_tcp->m_mct_decoding_matrix = (float*) grkMalloc(mct_size);

		if (!p_tcp->m_mct_decoding_matrix)
			return false;

		j2k_mct_read_functions_to_float[deco_array->m_element_type](
				deco_array->m_data, p_tcp->m_mct_decoding_matrix, nb_elem);
	}

	auto offset_array = mcc_record->m_offset_array;

	if (offset_array) {
		uint32_t data_size = MCT_ELEMENT_SIZE[offset_array->m_element_type]
				* p_image->numcomps;
		if (offset_array->m_data_size != data_size)
			return false;

		uint32_t nb_elem = p_image->numcomps;
		uint32_t offset_size = nb_elem * (uint32_t) sizeof(uint32_t);
		auto offset_data = (uint32_t*) grkMalloc(offset_size);

		if (!offset_data)
			return false;

		j2k_mct_read_functions_to_int32[offset_array->m_element_type](
				offset_array->m_data, offset_data, nb_elem);

		auto current_offset_data = offset_data;

		for (i = 0; i < p_image->numcomps; ++i) {
			auto tccp = p_tcp->tccps + i;
			tccp->m_dc_level_shift = (int32_t) *(current_offset_data++);
		}
		grkFree(offset_data);
	}

	return true;
}
/**
 * Reads a CBD marker (Component bit depth definition)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_cbd( uint8_t *p_header_data,uint16_t header_size) {
	assert(p_header_data != nullptr);
	if (header_size < 2 || (header_size - 2) != getHeaderImage()->numcomps) {
		GRK_ERROR("Error reading CBD marker");
		return false;
	}
	/* Ncbd */
	uint16_t nb_comp;
	grk_read<uint16_t>(p_header_data, &nb_comp);
	p_header_data += 2;

	if (nb_comp != getHeaderImage()->numcomps) {
		GRK_ERROR("Crror reading CBD marker");
		return false;
	}

	for (uint16_t i = 0; i < getHeaderImage()->numcomps; ++i) {
		/* Component bit depth */
		uint8_t comp_def;
		grk_read<uint8_t>(p_header_data++, &comp_def);
		auto comp = getHeaderImage()->comps + i;
		comp->sgnd = ((uint32_t)(comp_def >> 7U) & 1U);
		comp->prec = (uint8_t)((comp_def & 0x7f) + 1U);
	}

	return true;
}
/**
 * Reads a TLM marker (Tile Length Marker)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_tlm( uint8_t *p_header_data,	uint16_t header_size) {
	if (!m_cp.tlm_markers)
		m_cp.tlm_markers = new TileLengthMarkers();

	return m_cp.tlm_markers->read(p_header_data, header_size);
}
bool CodeStreamDecompress::read_SQcd_SQcc( bool fromQCC,uint32_t comp_no,
		uint8_t *p_header_data, uint16_t *header_size) {
	assert(p_header_data != nullptr);
	assert(comp_no < getHeaderImage()->numcomps);
	auto tcp = get_current_decode_tcp();
	auto tccp = tcp->tccps + comp_no;

	return tccp->quant.read_SQcd_SQcc(this, fromQCC, comp_no, p_header_data,
			header_size);
}
bool CodeStreamDecompress::read_SPCod_SPCoc( uint32_t compno, uint8_t *p_header_data, uint16_t *header_size) {
	uint32_t i;
	assert(p_header_data != nullptr);
	assert(compno < getHeaderImage()->numcomps);

	if (compno >= getHeaderImage()->numcomps)
		return false;

	auto cp = &(m_cp);
	auto tcp = get_current_decode_tcp();
	auto tccp = &tcp->tccps[compno];
	auto current_ptr = p_header_data;

	/* make sure room is sufficient */
	if (*header_size < SPCod_SPCoc_len) {
		GRK_ERROR("Error reading SPCod SPCoc element");
		return false;
	}
	/* SPcox (D) */
	// note: we actually read the number of decompositions
	grk_read<uint8_t>(current_ptr++, &tccp->numresolutions);
	if (tccp->numresolutions > GRK_J2K_MAX_DECOMP_LVLS) {
		GRK_ERROR("Invalid number of decomposition levels : %u. The JPEG 2000 standard\n"
				"allows a maximum number of %u decomposition levels.", tccp->numresolutions,
				GRK_J2K_MAX_DECOMP_LVLS);
		return false;
	}
	++tccp->numresolutions;
	if (m_cp.pcap && !tcp->getIsHT()) {
		tcp->setIsHT(true);
		tcp->qcd.generate(tccp->numgbits, tccp->numresolutions - 1U,
				tccp->qmfbid == 1, getHeaderImage()->comps[compno].prec,
				tcp->mct > 0, getHeaderImage()->comps[compno].sgnd);
		tcp->qcd.push(tccp->stepsizes, tccp->qmfbid == 1);
	}

	/* If user wants to remove more resolutions than the code stream contains, return error */
	if (cp->m_coding_params.m_dec.m_reduce >= tccp->numresolutions) {
		GRK_ERROR("Error decoding component %u.\nThe number of resolutions "
				" to remove (%d) must be strictly less than the number "
				"of resolutions (%d) of this component.\n"
				"Please decrease the cp_reduce parameter.",
				compno,cp->m_coding_params.m_dec.m_reduce,tccp->numresolutions);
		m_decompressorState.orState(J2K_DEC_STATE_ERR);
		return false;
	}
	/* SPcoc (E) */
	grk_read<uint8_t>(current_ptr++, &tccp->cblkw);
	/* SPcoc (F) */
	grk_read<uint8_t>(current_ptr++, &tccp->cblkh);

	if ( tccp->cblkw > 8 || tccp->cblkh > 8
			|| (tccp->cblkw + tccp->cblkh) > 8 ) {
		GRK_ERROR("Illegal code-block width/height (2^%d, 2^%d) found in COD/COC marker segment.\n"
		"Code-block dimensions must be powers of 2, must be in the range 4-1024, and their product must "
		"lie in the range 16-4096.",(uint32_t)tccp->cblkw + 2, (uint32_t)tccp->cblkh + 2);
		return false;
	}

	tccp->cblkw = (uint8_t)(tccp->cblkw + 2U);
	tccp->cblkh = (uint8_t)(tccp->cblkh + 2U);

	/* SPcoc (G) */
	tccp->cblk_sty = *current_ptr++;
	if ((tccp->cblk_sty & GRK_CBLKSTY_HT) && tccp->cblk_sty != GRK_CBLKSTY_HT){
		GRK_ERROR("Unrecognized code-block style byte 0x%x found in COD/COC marker segment.\nWith bit-6 "
				"set (HT block coder), the other mode flags from the original J2K block coder must be 0.",tccp->cblk_sty);
		return false;
	}
	uint8_t high_bits = (uint8_t)(tccp->cblk_sty >> 6U);
	if (high_bits == 2) {
		GRK_ERROR("Unrecognized code-block style byte 0x%x found in COD/COC marker segment. "
				"Most significant 2 bits can be 00, 01 or 11, but not 10",tccp->cblk_sty );
		return false;
	}

	/* SPcoc (H) */
	tccp->qmfbid = *current_ptr++;
	if (tccp->qmfbid > 1) {
		GRK_ERROR("Invalid qmfbid : %u. "
				"Should be either 0 or 1", tccp->qmfbid);
		return false;
	}
	*header_size = (uint16_t) (*header_size - SPCod_SPCoc_len);

	/* use custom precinct size ? */
	if (tccp->csty & J2K_CCP_CSTY_PRT) {
		if (*header_size < tccp->numresolutions) {
			GRK_ERROR("Error reading SPCod SPCoc element");
			return false;
		}

		for (i = 0; i < tccp->numresolutions; ++i) {
			uint8_t tmp;
			/* SPcoc (I_i) */
			grk_read<uint8_t>(current_ptr, &tmp);
			++current_ptr;
			/* Precinct exponent 0 is only allowed for lowest resolution level (Table A.21) */
			if ((i != 0) && (((tmp & 0xf) == 0) || ((tmp >> 4) == 0))) {
				GRK_ERROR("Invalid precinct size");
				return false;
			}
			tccp->precinctWidthExp[i] = tmp & 0xf;
			tccp->precinctHeightExp[i] = (uint32_t)(tmp >> 4U);
		}

		*header_size = (uint16_t) (*header_size - tccp->numresolutions);
	} else {
		/* set default size for the precinct width and height */
		for (i = 0; i < tccp->numresolutions; ++i) {
			tccp->precinctWidthExp[i] = 15;
			tccp->precinctHeightExp[i] = 15;
		}
	}

	return true;
}
/**
 * Reads a MCC marker (Multiple Component Collection)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_mcc( uint8_t *p_header_data,
		uint16_t header_size) {
	uint32_t i, j;
	uint32_t tmp;
	uint32_t indix;
	uint32_t nb_collections;
	uint32_t nb_comps;

	assert(p_header_data != nullptr);


	auto tcp = get_current_decode_tcp();

	if (header_size < 2) {
		GRK_ERROR("Error reading MCC marker");
		return false;
	}

	/* first marker */
	/* Zmcc */
	grk_read<uint32_t>(p_header_data, &tmp, 2);
	p_header_data += 2;
	if (tmp != 0) {
		GRK_WARN("Cannot take in charge multiple data spanning");
		return true;
	}
	if (header_size < 7) {
		GRK_ERROR("Error reading MCC marker");
		return false;
	}

	grk_read<uint32_t>(p_header_data, &indix, 1); /* Imcc -> no need for other values, take the first */
	++p_header_data;

	auto mcc_record = tcp->m_mcc_records;

	for (i = 0; i < tcp->m_nb_mcc_records; ++i) {
		if (mcc_record->m_index == indix)
			break;
		++mcc_record;
	}

	/** NOT FOUND */
	bool newmcc = false;
	if (i == tcp->m_nb_mcc_records) {
		// resize tcp->m_nb_mcc_records if necessary
		if (tcp->m_nb_mcc_records == tcp->m_nb_max_mcc_records) {
			grk_simple_mcc_decorrelation_data *new_mcc_records;
			tcp->m_nb_max_mcc_records += default_number_mcc_records;

			new_mcc_records = (grk_simple_mcc_decorrelation_data*) grkRealloc(
					tcp->m_mcc_records,
					tcp->m_nb_max_mcc_records
							* sizeof(grk_simple_mcc_decorrelation_data));
			if (!new_mcc_records) {
				grkFree(tcp->m_mcc_records);
				tcp->m_mcc_records = nullptr;
				tcp->m_nb_max_mcc_records = 0;
				tcp->m_nb_mcc_records = 0;
				GRK_ERROR("Not enough memory to read MCC marker");
				return false;
			}
			tcp->m_mcc_records = new_mcc_records;
			mcc_record = tcp->m_mcc_records + tcp->m_nb_mcc_records;
			memset(mcc_record, 0,
					(tcp->m_nb_max_mcc_records - tcp->m_nb_mcc_records)
							* sizeof(grk_simple_mcc_decorrelation_data));
		}
		// set pointer to prospective new mcc record
		mcc_record = tcp->m_mcc_records + tcp->m_nb_mcc_records;
		newmcc = true;
	}
	mcc_record->m_index = indix;

	/* only one marker atm */
	/* Ymcc */
	grk_read<uint32_t>(p_header_data, &tmp, 2);
	p_header_data += 2;
	if (tmp != 0) {
		GRK_WARN("Cannot take in charge multiple data spanning");
		return true;
	}

	/* Qmcc -> number of collections -> 1 */
	grk_read<uint32_t>(p_header_data, &nb_collections, 2);
	p_header_data += 2;

	if (nb_collections > 1) {
		GRK_WARN("Cannot take in charge multiple collections");
		return true;
	}
	header_size = (uint16_t) (header_size - 7);

	for (i = 0; i < nb_collections; ++i) {
		if (header_size < 3) {
			GRK_ERROR("Error reading MCC marker");
			return false;
		}
		grk_read<uint32_t>(p_header_data++, &tmp, 1); /* Xmcci type of component transformation -> array based decorrelation */

		if (tmp != 1) {
			GRK_WARN(
					"Cannot take in charge collections other than array decorrelation");
			return true;
		}
		grk_read<uint32_t>(p_header_data, &nb_comps, 2);

		p_header_data += 2;
		header_size = (uint16_t) (header_size - 3);

		uint32_t nb_bytes_by_comp = 1 + (nb_comps >> 15);
		mcc_record->m_nb_comps = nb_comps & 0x7fff;

		if (header_size < (nb_bytes_by_comp * mcc_record->m_nb_comps + 2)) {
			GRK_ERROR("Error reading MCC marker");
			return false;
		}

		header_size = (uint16_t) (header_size
				- (nb_bytes_by_comp * mcc_record->m_nb_comps + 2));

		for (j = 0; j < mcc_record->m_nb_comps; ++j) {
			/* Cmccij Component offset*/
			grk_read<uint32_t>(p_header_data, &tmp, nb_bytes_by_comp);
			p_header_data += nb_bytes_by_comp;

			if (tmp != j) {
				GRK_WARN(
						"Cannot take in charge collections with indix shuffle");
				return true;
			}
		}

		grk_read<uint32_t>(p_header_data, &nb_comps, 2);
		p_header_data += 2;

		nb_bytes_by_comp = 1 + (nb_comps >> 15);
		nb_comps &= 0x7fff;

		if (nb_comps != mcc_record->m_nb_comps) {
			GRK_WARN(
					"Cannot take in charge collections without same number of indices");
			return true;
		}

		if (header_size < (nb_bytes_by_comp * mcc_record->m_nb_comps + 3)) {
			GRK_ERROR("Error reading MCC marker");
			return false;
		}

		header_size = (uint16_t) (header_size
				- (nb_bytes_by_comp * mcc_record->m_nb_comps + 3));

		for (j = 0; j < mcc_record->m_nb_comps; ++j) {
			/* Wmccij Component offset*/
			grk_read<uint32_t>(p_header_data, &tmp, nb_bytes_by_comp);
			p_header_data += nb_bytes_by_comp;

			if (tmp != j) {
				GRK_WARN(
						"Cannot take in charge collections with indix shuffle");
				return true;
			}
		}
		/* Wmccij Component offset*/
		grk_read<uint32_t>(p_header_data, &tmp, 3);
		p_header_data += 3;

		mcc_record->m_is_irreversible = !((tmp >> 16) & 1);
		mcc_record->m_decorrelation_array = nullptr;
		mcc_record->m_offset_array = nullptr;

		indix = tmp & 0xff;
		if (indix != 0) {
			for (j = 0; j < tcp->m_nb_mct_records; ++j) {
				auto mct_data = tcp->m_mct_records + j;
				if (mct_data->m_index == indix) {
					mcc_record->m_decorrelation_array = mct_data;
					break;
				}
			}

			if (mcc_record->m_decorrelation_array == nullptr) {
				GRK_ERROR("Error reading MCC marker");
				return false;
			}
		}

		indix = (tmp >> 8) & 0xff;
		if (indix != 0) {
			for (j = 0; j < tcp->m_nb_mct_records; ++j) {
				auto mct_data = tcp->m_mct_records + j;
				if (mct_data->m_index == indix) {
					mcc_record->m_offset_array = mct_data;
					break;
				}
			}

			if (mcc_record->m_offset_array == nullptr) {
				GRK_ERROR("Error reading MCC marker");
				return false;
			}
		}
	}

	if (header_size != 0) {
		GRK_ERROR("Error reading MCC marker");
		return false;
	}

	// only increment mcc record count if we are working on a new mcc
	// and everything succeeded
	if (newmcc)
		++tcp->m_nb_mcc_records;

	return true;
}
/**
 * Reads a MCT marker (Multiple Component Transform)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_mct( uint8_t *p_header_data,	uint16_t header_size) {
	uint32_t i;
	uint32_t tmp;
	uint32_t indix;
	assert(p_header_data != nullptr);
	auto tcp = get_current_decode_tcp();

	if (header_size < 2) {
		GRK_ERROR("Error reading MCT marker");
		return false;
	}
	/* first marker */
	/* Zmct */
	grk_read<uint32_t>(p_header_data, &tmp, 2);
	p_header_data += 2;
	if (tmp != 0) {
		GRK_WARN("Cannot take in charge mct data within multiple MCT records");
		return true;
	}

	/* Imct -> no need for other values, take the first,
	 * type is double with decorrelation x0000 1101 0000 0000*/
	grk_read<uint32_t>(p_header_data, &tmp, 2); /* Imct */
	p_header_data += 2;

	indix = tmp & 0xff;
	auto mct_data = tcp->m_mct_records;

	for (i = 0; i < tcp->m_nb_mct_records; ++i) {
		if (mct_data->m_index == indix)
			break;
		++mct_data;
	}

	bool newmct = false;
	// NOT FOUND
	if (i == tcp->m_nb_mct_records) {
		if (tcp->m_nb_mct_records == tcp->m_nb_max_mct_records) {
			grk_mct_data *new_mct_records;
			tcp->m_nb_max_mct_records += default_number_mct_records;

			new_mct_records = (grk_mct_data*) grkRealloc(tcp->m_mct_records,
					tcp->m_nb_max_mct_records * sizeof(grk_mct_data));
			if (!new_mct_records) {
				grkFree(tcp->m_mct_records);
				tcp->m_mct_records = nullptr;
				tcp->m_nb_max_mct_records = 0;
				tcp->m_nb_mct_records = 0;
				GRK_ERROR("Not enough memory to read MCT marker");
				return false;
			}

			/* Update m_mcc_records[].m_offset_array and m_decorrelation_array
			 * to point to the new addresses */
			if (new_mct_records != tcp->m_mct_records) {
				for (i = 0; i < tcp->m_nb_mcc_records; ++i) {
					grk_simple_mcc_decorrelation_data *mcc_record =
							&(tcp->m_mcc_records[i]);
					if (mcc_record->m_decorrelation_array) {
						mcc_record->m_decorrelation_array = new_mct_records
								+ (mcc_record->m_decorrelation_array
										- tcp->m_mct_records);
					}
					if (mcc_record->m_offset_array) {
						mcc_record->m_offset_array = new_mct_records
								+ (mcc_record->m_offset_array
										- tcp->m_mct_records);
					}
				}
			}

			tcp->m_mct_records = new_mct_records;
			mct_data = tcp->m_mct_records + tcp->m_nb_mct_records;
			memset(mct_data, 0,
					(tcp->m_nb_max_mct_records - tcp->m_nb_mct_records)
							* sizeof(grk_mct_data));
		}

		mct_data = tcp->m_mct_records + tcp->m_nb_mct_records;
		newmct = true;
	}
	if (mct_data->m_data) {
		grkFree(mct_data->m_data);
		mct_data->m_data = nullptr;
		mct_data->m_data_size = 0;
	}
	mct_data->m_index = indix;
	mct_data->m_array_type = (J2K_MCT_ARRAY_TYPE) ((tmp >> 8) & 3);
	mct_data->m_element_type = (J2K_MCT_ELEMENT_TYPE) ((tmp >> 10) & 3);
	/* Ymct */
	grk_read<uint32_t>(p_header_data, &tmp, 2);
	p_header_data += 2;
	if (tmp != 0) {
		GRK_WARN("Cannot take in charge multiple MCT markers");
		return true;
	}
	if (header_size <= 6) {
		GRK_ERROR("Error reading MCT markers");
		return false;
	}
	header_size = (uint16_t) (header_size - 6);

	mct_data->m_data = (uint8_t*) grkMalloc(header_size);
	if (!mct_data->m_data) {
		GRK_ERROR("Error reading MCT marker");
		return false;
	}
	memcpy(mct_data->m_data, p_header_data, header_size);
	mct_data->m_data_size = header_size;
	if (newmct)
		++tcp->m_nb_mct_records;

	return true;
}
bool CodeStreamDecompress::read_unk(uint16_t *output_marker) {
	const marker_handler *marker_handler = nullptr;
	uint32_t size_unk = 2;
	while (true) {
		if (!readMarker())
			return false;
		/* Get the marker handler from the marker ID*/
		marker_handler = get_marker_handler(m_curr_marker);
		if (marker_handler == nullptr)	{
			size_unk += 2;
		} else {
			if (!(m_decompressorState.getState() & marker_handler->states)) {
				GRK_ERROR("Marker %d is not compliant with its position",m_curr_marker);
				return false;
			} else {
				/* Add the marker to the code stream index*/
				if (codeStreamInfo && marker_handler->id != J2K_MS_SOT)
					 addMainHeaderMarker(J2K_MS_UNK, m_stream->tell() - size_unk, size_unk);
				break; /* next marker is known and located correctly  */
			}
		}
	}
	if (!marker_handler){
		GRK_ERROR("Unable to read unknown marker");
		return false;
	}
	*output_marker = marker_handler->id;

	return true;
}
/** Reading function used after code stream if necessary */
bool CodeStreamDecompress::endDecompress(void){
	return true;
}
bool CodeStreamDecompress::parseTileHeaderMarkers(bool *canDecompress) {
	if (m_decompressorState.getState() == J2K_DEC_STATE_EOC) {
		m_curr_marker = J2K_MS_EOC;
		return true;
	}
	/* We need to encounter a SOT marker (a new tile-part header) */
	if (m_decompressorState.getState() != J2K_DEC_STATE_TPH_SOT){
		GRK_ERROR("parse_markers: no SOT marker found");
		return false;
	}
	/* Seek in code stream for SOT marker specifying desired tile index.
	 * If we don't find it, we stop when we read the EOC or run out of data */
	while (!m_decompressorState.last_tile_part_was_read && (m_curr_marker != J2K_MS_EOC)) {
		/* read markers until SOD is detected */
		while (m_curr_marker != J2K_MS_SOD) {
			// end of stream with no EOC
			if (m_stream->get_number_byte_left() == 0) {
				m_decompressorState.setState(J2K_DEC_STATE_NO_EOC);
				break;
			}
			uint16_t marker_size;
			if (!read_short(&marker_size))
				return false;
			else if (marker_size < 2) {
				GRK_ERROR("Marker size %d for marker 0x%x is less than 2",marker_size, m_curr_marker);
				return false;
			}
			else if (marker_size == 2) {
				GRK_ERROR("Zero-size marker in header.");
				return false;
			}
			// subtract tile part header and header marker size
			if (m_decompressorState.getState() & J2K_DEC_STATE_TPH)
				m_currentTileProcessor->tile_part_data_length -= (uint32_t)(marker_size + 2);

			marker_size = (uint16_t)(marker_size - 2); /* Subtract the size of the marker ID already read */
			auto marker_handler = get_marker_handler(m_curr_marker);
			if (!marker_handler) {
				GRK_ERROR("Unknown marker encountered while seeking SOT marker");
				return false;
			}
			if (!(m_decompressorState.getState() & marker_handler->states)) {
				GRK_ERROR("Marker 0x%x is not compliant with its expected position", m_curr_marker);
				return false;
			}
			if (!process_marker(marker_handler, marker_size))
				return false;
			/* Add the marker to the code stream index*/
			if (codeStreamInfo) {
				if (!TileLengthMarkers::addTileMarkerInfo(m_currentTileProcessor->m_tile_index, codeStreamInfo,
													marker_handler->id,
													(uint32_t) m_stream->tell() - marker_size - grk_marker_length,
													marker_size + grk_marker_length)) {
					GRK_ERROR("Not enough memory to add tl marker");
					return false;
				}
			}
			if (marker_handler->id == J2K_MS_SOT) {
				uint64_t sot_pos = m_stream->tell() - marker_size - grk_marker_length;
				if (sot_pos > m_decompressorState.m_last_sot_read_pos)
					m_decompressorState.m_last_sot_read_pos = sot_pos;
				if (m_decompressorState.m_skip_tile_data) {
					if (!m_stream->skip(m_currentTileProcessor->tile_part_data_length)) {
						GRK_ERROR("Stream too short");
						return false;
					}
					break;
				}
			}
			if (!readMarker())
				return false;
		}
		// no bytes left and no EOC marker : we're done!
		if (!m_stream->get_number_byte_left() && m_decompressorState.getState() == J2K_DEC_STATE_NO_EOC)
			break;
		/* If we didn't skip data before, we need to read the SOD marker*/
		if (!m_decompressorState.m_skip_tile_data) {
			if (!m_currentTileProcessor->prepareSodDecompress(this))
				return false;
			if (!m_decompressorState.last_tile_part_was_read) {
				if (!readMarker()){
					m_decompressorState.setState(J2K_DEC_STATE_NO_EOC);
					break;
				}
			}
		} else {
			if (!readMarker()){
				m_decompressorState.setState(J2K_DEC_STATE_NO_EOC);
				break;
			}
			/* Indicate we will try to read a new tile-part header*/
			m_decompressorState.m_skip_tile_data = false;
			m_decompressorState.last_tile_part_was_read = false;
			m_decompressorState.setState(J2K_DEC_STATE_TPH_SOT);
		}
	}
	if (!m_currentTileProcessor) {
		GRK_ERROR("Missing SOT marker");
		return false;
	}
	// ensure lossy wavelet has quantization set
	auto tcp = get_current_decode_tcp();
	auto numComps = m_headerImage->numcomps;
	for (uint32_t k = 0; k < numComps; ++k) {
		auto tccp = tcp->tccps + k;
		if (tccp->qmfbid == 0 && tccp->qntsty == J2K_CCP_QNTSTY_NOQNT) {
			GRK_ERROR("Tile-components compressed using the irreversible processing path\n"
					"must have quantization parameters specified in the QCD/QCC marker segments,\n"
					"either explicitly, or through implicit derivation from the quantization\n"
					"parameters for the LL subband, as explained in the JPEG2000 standard, ISO/IEC\n"
					"15444-1.  The present set of code-stream parameters is not legal.");
			return false;
		}
	}
	// do QCD marker quantization step size sanity check
	// see page 553 of Taubman and Marcellin for more details on this check
	if (tcp->main_qcd_qntsty != J2K_CCP_QNTSTY_SIQNT) {
		//1. Check main QCD
		uint8_t maxDecompositions = 0;
		for (uint32_t k = 0; k < numComps; ++k) {
			auto tccp = tcp->tccps + k;
			if (tccp->numresolutions == 0)
				continue;
			// only consider number of resolutions from a component
			// whose scope is covered by main QCD;
			// ignore components that are out of scope
			// i.e. under main QCC scope, or tile QCD/QCC scope
			if (tccp->fromQCC || tccp->fromTileHeader)
				continue;
			auto decomps = (uint8_t)(tccp->numresolutions - 1);
			if (maxDecompositions < decomps)
				maxDecompositions = decomps;
		}
		if ((tcp->main_qcd_numStepSizes < 3 * (uint32_t)maxDecompositions + 1)) {
			GRK_ERROR("From Main QCD marker, "
					"number of step sizes (%u) is less than "
					"3* (maximum decompositions) + 1, "
					"where maximum decompositions = %u ",
					tcp->main_qcd_numStepSizes, maxDecompositions);
			return false;
		}
		//2. Check Tile QCD
		TileComponentCodingParams *qcd_comp = nullptr;
		for (uint32_t k = 0; k < numComps; ++k) {
			auto tccp = tcp->tccps + k;
			if (tccp->fromTileHeader && !tccp->fromQCC) {
				qcd_comp = tccp;
				break;
			}
		}
		if (qcd_comp && (qcd_comp->qntsty != J2K_CCP_QNTSTY_SIQNT)) {
			uint32_t maxTileDecompositions = 0;
			for (uint32_t k = 0; k < numComps; ++k) {
				auto tccp = tcp->tccps + k;
				if (tccp->numresolutions == 0)
					continue;
				// only consider number of resolutions from a component
				// whose scope is covered by Tile QCD;
				// ignore components that are out of scope
				// i.e. under Tile QCC scope
				if (tccp->fromQCC && tccp->fromTileHeader)
					continue;
				auto decomps = (uint8_t)(tccp->numresolutions - 1);
				if (maxTileDecompositions < decomps)
					maxTileDecompositions = decomps;
			}
			if ((qcd_comp->numStepSizes < 3 * maxTileDecompositions + 1)) {
				GRK_ERROR("From Tile QCD marker, "
						"number of step sizes (%u) is less than"
						" 3* (maximum tile decompositions) + 1, "
						"where maximum tile decompositions = %u ",
						qcd_comp->numStepSizes, maxTileDecompositions);

				return false;
			}
		}
	}
	/* Current marker is the EOC marker ?*/
	if (m_curr_marker == J2K_MS_EOC && m_decompressorState.getState() != J2K_DEC_STATE_EOC)
		m_decompressorState.setState( J2K_DEC_STATE_EOC);
	//if we are not ready to decompress tile part data,
    // then skip tiles with no tile data i.e. no SOD marker
	if (!m_decompressorState.last_tile_part_was_read) {
		tcp = m_cp.tcps + m_currentTileProcessor->m_tile_index;
		if (!tcp->m_compressedTileData){
			*canDecompress = false;
			return true;
		}
	}
	if (!merge_ppt(m_cp.tcps + m_currentTileProcessor->m_tile_index)) {
		GRK_ERROR("Failed to merge PPT data");
		return false;
	}
	if (!m_currentTileProcessor->init()) {
		GRK_ERROR("Cannot decompress tile %u",
				m_currentTileProcessor->m_tile_index);
		return false;
	}
	*canDecompress = true;
	m_decompressorState.orState(J2K_DEC_STATE_DATA);

	return true;
}
/**
 * Reads a COD marker (Coding Style defaults)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_cod(uint8_t *p_header_data,	uint16_t header_size) {
	uint32_t i;
	assert(p_header_data != nullptr);
	auto image = getHeaderImage();
	auto cp = &(m_cp);

	/* If we are in the first tile-part header of the current tile */
	auto tcp = get_current_decode_tcp();

	/* Only one COD per tile */
	if (tcp->cod) {
		GRK_WARN("Multiple COD markers detected for tile part %u."
				" The JPEG 2000 standard does not allow more than one COD marker per tile.",
				tcp->m_tile_part_index);
	}
	tcp->cod = true;

	/* Make sure room is sufficient */
	if (header_size < cod_coc_len) {
		GRK_ERROR("Error reading COD marker");
		return false;
	}
	grk_read<uint8_t>(p_header_data++, &tcp->csty); /* Scod */
	/* Make sure we know how to decompress this */
	if ((tcp->csty
			& ~(uint32_t) (J2K_CP_CSTY_PRT | J2K_CP_CSTY_SOP | J2K_CP_CSTY_EPH))
			!= 0U) {
		GRK_ERROR("Unknown Scod value in COD marker");
		return false;
	}
	uint8_t tmp;
	grk_read<uint8_t>(p_header_data++, &tmp); /* SGcod (A) */
	/* Make sure progression order is valid */
	if (tmp >= GRK_NUM_PROGRESSION_ORDERS) {
		GRK_ERROR("Unknown progression order %d in COD marker", tmp);
		return false;
	}
	tcp->prg = (GRK_PROG_ORDER) tmp;
	grk_read<uint16_t>(p_header_data, &tcp->numlayers); /* SGcod (B) */
	p_header_data += 2;

	if (tcp->numlayers  == 0) {
		GRK_ERROR("Number of layers must be positive");
		return false;
	}

	/* If user didn't set a number layer to decompress take the max specify in the code stream. */
	if (cp->m_coding_params.m_dec.m_layer) {
		tcp->num_layers_to_decompress = cp->m_coding_params.m_dec.m_layer;
	} else {
		tcp->num_layers_to_decompress = tcp->numlayers;
	}

	grk_read<uint8_t>(p_header_data++, &tcp->mct); /* SGcod (C) */
	if (tcp->mct > 1) {
		GRK_ERROR("Invalid MCT value : %u. Should be either 0 or 1", tcp->mct);
		return false;
	}
	header_size = (uint16_t) (header_size - cod_coc_len);
	for (i = 0; i < image->numcomps; ++i) {
		tcp->tccps[i].csty = tcp->csty & J2K_CCP_CSTY_PRT;
	}

	if (!read_SPCod_SPCoc(0, p_header_data, &header_size)) {
		return false;
	}

	if (header_size != 0) {
		GRK_ERROR("Error reading COD marker");
		return false;
	}
	/* Apply the coding style to other components of the current tile or the m_default_tcp*/
	/* loop */
	uint32_t prc_size;
	auto ref_tccp = &tcp->tccps[0];
	prc_size = ref_tccp->numresolutions * (uint32_t) sizeof(uint32_t);

	for (i = 1; i < getHeaderImage()->numcomps; ++i) {
		auto copied_tccp = ref_tccp + i;

		copied_tccp->numresolutions = ref_tccp->numresolutions;
		copied_tccp->cblkw = ref_tccp->cblkw;
		copied_tccp->cblkh = ref_tccp->cblkh;
		copied_tccp->cblk_sty = ref_tccp->cblk_sty;
		copied_tccp->qmfbid = ref_tccp->qmfbid;
		memcpy(copied_tccp->precinctWidthExp, ref_tccp->precinctWidthExp, prc_size);
		memcpy(copied_tccp->precinctHeightExp, ref_tccp->precinctHeightExp, prc_size);
	}

	return true;
}
/**
 * Reads a COC marker (Coding Style Component)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data

 */
bool CodeStreamDecompress::read_coc( uint8_t *p_header_data,	uint16_t header_size) {
	uint32_t comp_room;
	uint32_t comp_no;
	assert(p_header_data != nullptr);
	auto tcp = get_current_decode_tcp();
	auto image = getHeaderImage();

	comp_room = image->numcomps <= 256 ? 1 : 2;

	/* make sure room is sufficient*/
	if (header_size < comp_room + 1) {
		GRK_ERROR("Error reading COC marker");
		return false;
	}
	header_size = (uint16_t) (header_size - (comp_room + 1));

	grk_read<uint32_t>(p_header_data, &comp_no, comp_room); /* Ccoc */
	p_header_data += comp_room;
	if (comp_no >= image->numcomps) {
		GRK_ERROR("Error reading COC marker : invalid component number %d", comp_no);
		return false;
	}

	tcp->tccps[comp_no].csty = *p_header_data++; /* Scoc */

	if (!read_SPCod_SPCoc(comp_no, p_header_data, &header_size)) {
		return false;
	}

	if (header_size != 0) {
		GRK_ERROR("Error reading COC marker");
		return false;
	}
	return true;
}
/**
 * Reads a QCD marker (Quantization defaults)
 *
 * @param       this      JPEG 2000 code stream
  * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool CodeStreamDecompress::read_qcd( uint8_t *p_header_data,
		uint16_t header_size) {
	assert(p_header_data != nullptr);
	if (!read_SQcd_SQcc(false, 0, p_header_data, &header_size)) {
		return false;
	}
	if (header_size != 0) {
		GRK_ERROR("Error reading QCD marker");
		return false;
	}

	// Apply the quantization parameters to the other components
	// of the current tile or m_default_tcp
	auto tcp = get_current_decode_tcp();
	auto ref_tccp = tcp->tccps;
	for (uint32_t i = 1; i < getHeaderImage()->numcomps; ++i) {
		auto target_tccp = ref_tccp + i;
		target_tccp->quant.apply_quant(ref_tccp, target_tccp);
	}
	return true;
}
/**
 * Reads a QCC marker (Quantization component)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool CodeStreamDecompress::read_qcc( uint8_t *p_header_data,
		uint16_t header_size) {
	assert(p_header_data != nullptr);
	uint32_t comp_no;
	uint16_t num_comp = getHeaderImage()->numcomps;
	if (num_comp <= 256) {
		if (header_size < 1) {
			GRK_ERROR("Error reading QCC marker");
			return false;
		}
		grk_read<uint32_t>(p_header_data++, &comp_no, 1);
		--header_size;
	} else {
		if (header_size < 2) {
			GRK_ERROR("Error reading QCC marker");
			return false;
		}
		grk_read<uint32_t>(p_header_data, &comp_no, 2);
		p_header_data += 2;
		header_size = (uint16_t) (header_size - 2);
	}

	if (comp_no >= getHeaderImage()->numcomps) {
		GRK_ERROR("QCC component: component number: %u must be less than"
				" total number of components: %u",
				comp_no, getHeaderImage()->numcomps);
		return false;
	}

	if (!read_SQcd_SQcc(true, comp_no, p_header_data,
			&header_size)) {
		return false;
	}

	if (header_size != 0) {
		GRK_ERROR("Error reading QCC marker");
		return false;
	}

	return true;
}
/**
 * Reads a SOC marker (Start of Codestream)
 * @param       this    JPEG 2000 code stream.
 */
bool CodeStreamDecompress::read_soc() {
	uint8_t data[2];
	uint16_t marker;
	if (m_stream->read(data, 2) != 2)
		return false;

	grk_read<uint16_t>(data, &marker);
	if (marker != J2K_MS_SOC)
		return false;

	/* Next marker should be a SIZ marker in the main header */
	m_decompressorState.setState(J2K_DEC_STATE_MH_SIZ);

	if (codeStreamInfo) {
		codeStreamInfo->setMainHeaderStart(m_stream->tell() - 2);
		addMainHeaderMarker(J2K_MS_SOC,codeStreamInfo->getMainHeaderStart(), 2);
	}
	return true;
}
/**
 * Reads a CAP marker
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_cap(  uint8_t *p_header_data,
		uint16_t header_size) {
	CodingParams *cp = &(m_cp);
	if (header_size < sizeof(cp->pcap)) {
		GRK_ERROR("Error with SIZ marker size");
		return false;
	}

	uint32_t tmp;
	grk_read<uint32_t>(p_header_data, &tmp); /* Pcap */
	if (tmp & 0xFFFDFFFF) {
		GRK_ERROR("Pcap in CAP marker has unsupported options.");
		return false;
	}
	if ((tmp & 0x00020000) == 0) {
		GRK_ERROR("Pcap in CAP marker should have its 15th MSB set. ");
		return false;
	}
	p_header_data += sizeof(uint32_t);
	cp->pcap = tmp;
    uint32_t count = ojph::population_count(cp->pcap);
    uint32_t expected_size = (uint32_t)sizeof(cp->pcap) + 2U * count;
	if (header_size != expected_size) {
	  GRK_ERROR("CAP marker size %d != expected size %d",header_size, expected_size);
	  return false;
	}
    for (uint32_t i = 0; i < count; ++i) {
    	grk_read<uint16_t>(p_header_data, cp->ccap+i);
    }

	return true;
}
/**
 * Reads a SIZ marker (image and tile size)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool CodeStreamDecompress::read_siz( uint8_t *p_header_data,	uint16_t header_size) {
	SIZMarker siz;

	return siz.read(this, p_header_data, header_size);
}
/**
 * Reads a COM marker (comments)
 *
 * @param       this      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool CodeStreamDecompress::read_com( uint8_t *p_header_data,	uint16_t header_size) {
	assert(p_header_data != nullptr);
	assert(header_size != 0);
	if (header_size < 2) {
		GRK_ERROR("CodeStreamDecompress::read_com: Corrupt COM segment ");
		return false;
	} else if (header_size == 2) {
		GRK_WARN("CodeStreamDecompress::read_com: Empty COM segment. Ignoring ");
		return true;
	}
	if (m_cp.num_comments == GRK_NUM_COMMENTS_SUPPORTED) {
		GRK_WARN("CodeStreamDecompress::read_com: Only %u comments are supported. Ignoring",
		GRK_NUM_COMMENTS_SUPPORTED);
		return true;
	}

	uint16_t commentType;
	grk_read<uint16_t>(p_header_data, &commentType);
	auto numComments = m_cp.num_comments;
	m_cp.isBinaryComment[numComments] = (commentType == 0);
	if (commentType > 1) {
		GRK_WARN(
				"CodeStreamDecompress::read_com: Unrecognized comment type 0x%x. Assuming IS 8859-15:1999 (Latin) values",
				commentType);
	}

	p_header_data += 2;
	uint16_t commentSize = (uint16_t) (header_size - 2);
	size_t commentSizeToAlloc = commentSize;
	if (!m_cp.isBinaryComment[numComments])
		commentSizeToAlloc++;
	m_cp.comment[numComments] = (char*) new uint8_t[commentSizeToAlloc];
	if (!m_cp.comment[numComments]) {
		GRK_ERROR(
				"CodeStreamDecompress::read_com: Out of memory when allocating memory for comment ");
		return false;
	}
	memcpy(m_cp.comment[numComments], p_header_data, commentSize);
	m_cp.comment_len[numComments] = commentSize;

	// make null-terminated string
	if (!m_cp.isBinaryComment[numComments])
		m_cp.comment[numComments][commentSize] = 0;
	m_cp.num_comments++;
	return true;
}

void CodeStreamDecompress::dump_tile_info(TileCodingParams *default_tile,
		uint32_t numcomps, FILE *out_stream) {
	if (default_tile) {
		uint32_t compno;

		fprintf(out_stream, "\t default tile {\n");
		fprintf(out_stream, "\t\t csty=%#x\n", default_tile->csty);
		fprintf(out_stream, "\t\t prg=%#x\n", default_tile->prg);
		fprintf(out_stream, "\t\t numlayers=%d\n", default_tile->numlayers);
		fprintf(out_stream, "\t\t mct=%x\n", default_tile->mct);

		for (compno = 0; compno < numcomps; compno++) {
			auto tccp = &(default_tile->tccps[compno]);
			uint32_t resno;
			uint32_t bandIndex, numBandWindows;

			assert(tccp->numresolutions > 0);

			/* coding style*/
			fprintf(out_stream, "\t\t comp %u {\n", compno);
			fprintf(out_stream, "\t\t\t csty=%#x\n", tccp->csty);
			fprintf(out_stream, "\t\t\t numresolutions=%d\n",
					tccp->numresolutions);
			fprintf(out_stream, "\t\t\t cblkw=2^%d\n", tccp->cblkw);
			fprintf(out_stream, "\t\t\t cblkh=2^%d\n", tccp->cblkh);
			fprintf(out_stream, "\t\t\t cblksty=%#x\n", tccp->cblk_sty);
			fprintf(out_stream, "\t\t\t qmfbid=%d\n", tccp->qmfbid);

			fprintf(out_stream, "\t\t\t preccintsize (w,h)=");
			for (resno = 0; resno < tccp->numresolutions; resno++) {
				fprintf(out_stream, "(%d,%d) ", tccp->precinctWidthExp[resno],
						tccp->precinctHeightExp[resno]);
			}
			fprintf(out_stream, "\n");

			/* quantization style*/
			fprintf(out_stream, "\t\t\t qntsty=%d\n", tccp->qntsty);
			fprintf(out_stream, "\t\t\t numgbits=%d\n", tccp->numgbits);
			fprintf(out_stream, "\t\t\t stepsizes (m,e)=");
			numBandWindows =
					(tccp->qntsty == J2K_CCP_QNTSTY_SIQNT) ?
							1 : (uint32_t) (tccp->numresolutions * 3 - 2);
			for (bandIndex = 0; bandIndex < numBandWindows; bandIndex++) {
				fprintf(out_stream, "(%d,%d) ", tccp->stepsizes[bandIndex].mant,
						tccp->stepsizes[bandIndex].expn);
			}
			fprintf(out_stream, "\n");

			/* RGN value*/
			fprintf(out_stream, "\t\t\t roishift=%d\n", tccp->roishift);

			fprintf(out_stream, "\t\t }\n");
		} /*end of component of default tile*/
		fprintf(out_stream, "\t }\n"); /*end of default tile*/
	}
}

void CodeStreamDecompress::dump(uint32_t flag, FILE *out_stream) {
	/* Check if the flag is compatible with j2k file*/
	if ((flag & GRK_JP2_INFO) || (flag & GRK_JP2_IND)) {
		fprintf(out_stream, "Wrong flag\n");
		return;
	}

	/* Dump the image_header */
	if (flag & GRK_IMG_INFO) {
		if (getHeaderImage())
			dump_image_header(getHeaderImage(), 0, out_stream);
	}

	/* Dump the code stream info from main header */
	if (flag & GRK_J2K_MH_INFO) {
		if (getHeaderImage())
			dump_MH_info(out_stream);
	}
	/* Dump all tile/code stream info */
	auto cp = getCodingParams();
	if (flag & GRK_J2K_TCH_INFO) {
		uint32_t nb_tiles = cp->t_grid_height* cp->t_grid_width;
		if (getHeaderImage()) {
			for (uint32_t i = 0; i < nb_tiles; ++i) {
				auto tcp = cp->tcps + i;
				dump_tile_info(tcp, getHeaderImage()->numcomps,
						out_stream);
			}
		}
	}

	/* Dump the code stream info of the current tile */
	if (flag & GRK_J2K_TH_INFO) {

	}

	/* Dump the code stream index from main header */
	if ((flag & GRK_J2K_MH_IND) && codeStreamInfo)
		codeStreamInfo->dump(out_stream);

	/* Dump the code stream index of the current tile */
	if (flag & GRK_J2K_TH_IND) {

	}
}
void CodeStreamDecompress::dump_MH_info(FILE *out_stream) {
	fprintf(out_stream, "Codestream info from main header: {\n");
	fprintf(out_stream, "\t tx0=%d, ty0=%d\n", m_cp.tx0,
			m_cp.ty0);
	fprintf(out_stream, "\t tdx=%d, tdy=%d\n", m_cp.t_width,
			m_cp.t_height);
	fprintf(out_stream, "\t tw=%d, th=%d\n", m_cp.t_grid_width,
			m_cp.t_grid_height);
	CodeStreamDecompress::dump_tile_info(getDecompressorState()->m_default_tcp,
			getHeaderImage()->numcomps, out_stream);
	fprintf(out_stream, "}\n");
}
void CodeStreamDecompress::dump_image_header(GrkImage *img_header, bool dev_dump_flag,
		FILE *out_stream) {
	char tab[2];

	if (dev_dump_flag) {
		fprintf(stdout, "[DEV] Dump an image_header struct {\n");
		tab[0] = '\0';
	} else {
		fprintf(out_stream, "Image info {\n");
		tab[0] = '\t';
		tab[1] = '\0';
	}
	fprintf(out_stream, "%s x0=%d, y0=%d\n", tab, img_header->x0,
			img_header->y0);
	fprintf(out_stream, "%s x1=%d, y1=%d\n", tab, img_header->x1,
			img_header->y1);
	fprintf(out_stream, "%s numcomps=%d\n", tab, img_header->numcomps);
	if (img_header->comps) {
		uint32_t compno;
		for (compno = 0; compno < img_header->numcomps; compno++) {
			fprintf(out_stream, "%s\t component %u {\n", tab, compno);
			CodeStreamDecompress::dump_image_comp_header(&(img_header->comps[compno]),
					dev_dump_flag, out_stream);
			fprintf(out_stream, "%s}\n", tab);
		}
	}
	fprintf(out_stream, "}\n");
}
void CodeStreamDecompress::dump_image_comp_header(grk_image_comp *comp_header, bool dev_dump_flag,
		FILE *out_stream) {
	char tab[3];

	if (dev_dump_flag) {
		fprintf(stdout, "[DEV] Dump an image_comp_header struct {\n");
		tab[0] = '\0';
	} else {
		tab[0] = '\t';
		tab[1] = '\t';
		tab[2] = '\0';
	}

	fprintf(out_stream, "%s dx=%d, dy=%d\n", tab, comp_header->dx,
			comp_header->dy);
	fprintf(out_stream, "%s prec=%d\n", tab, comp_header->prec);
	fprintf(out_stream, "%s sgnd=%d\n", tab, comp_header->sgnd ? 1 : 0);

	if (dev_dump_flag)
		fprintf(out_stream, "}\n");
}

}
