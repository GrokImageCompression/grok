/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
 *    This source code incorporates work covered by the following copyright and
 *    permission notice:
 *
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
 * Copyright (c) 2006-2007, Parvatha Elangovan
 * Copyright (c) 2010-2011, Kaori Hagihara
 * Copyright (c) 2011-2012, Centre National d'Etudes Spatiales (CNES), France
 * Copyright (c) 2012, CS Systemes d'Information, France
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "grok_includes.h"

namespace grk {



struct j2k_prog_order {
	GRK_PROG_ORDER enum_prog;
	char str_prog[5];
};

static j2k_prog_order j2k_prog_order_list[] = { { GRK_CPRL, "CPRL" }, {
		GRK_LRCP, "LRCP" }, { GRK_PCRL, "PCRL" }, { GRK_RLCP, "RLCP" }, {
		GRK_RPCL, "RPCL" }, { (GRK_PROG_ORDER) -1, "" } };


/**
 * Sets up the procedures to do on reading header. Developers wanting to extend the library can add their own reading procedures.
 */
static bool j2k_init_header_reading(CodeStream *codeStream);

/**
 * The read header procedure.
 *
 * @param       codeStream          JPEG 2000 code stream
 * @param 		tileProcessor		tile processor
 * @param       stream              the stream to read
 *
 */
static bool j2k_read_header_procedure(CodeStream *codeStream,  TileProcessor *tileProcessor,BufferedStream *stream);

/**
 * The default encoding validation procedure without any extension.
 *
 * @param       codeStream          JPEG 2000 code stream
 * @param 		tileProcessor		tile processor
 * @param       stream              the stream to validate.

 *
 * @return true if the parameters are correct.
 */
static bool j2k_compress_validation(CodeStream *codeStream,  TileProcessor *tileProcessor, BufferedStream *stream);

/**
 * The default decoding validation procedure without any extension.
 *
 * @param       codeStream          JPEG 2000 code stream
 * @param 		tileProcessor		tile processor
 * @param       stream              the stream to validate.

 *
 * @return true if the parameters are correct.
 */
static bool j2k_decompress_validation(CodeStream *codeStream,  TileProcessor *tileProcessor, BufferedStream *stream);

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool j2k_init_compress_validation(CodeStream *codeStream);

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool j2k_init_decompress_validation(CodeStream *codeStream);

/**
 * Sets up the validation ,i.e. adds the procedures to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool j2k_init_end_compress(CodeStream *codeStream);

/**
 * The mct encoding validation procedure.
 *
 * @param       codeStream          JPEG 2000 code stream
 * @param 		tileProcessor		tile processor
 * @param       stream              the stream to validate.

 *
 * @return true if the parameters are correct.
 */
static bool j2k_mct_validation(CodeStream *codeStream,  TileProcessor *tileProcessor, BufferedStream *stream);

/**
 * Executes the given procedures on the given codec.
 *
 * @param       codeStream                   the JPEG 2000 code stream to execute the procedures on.
 * @param       p_procedure_list        the list of procedures to execute
 * @param       stream                the stream to execute the procedures on.
 *
 * @return      true                            if all the procedures were successfully executed.
 */
static bool j2k_exec(CodeStream *codeStream, std::vector<j2k_procedure> &p_procedure_list,
		BufferedStream *stream);

/**
 * Updates the rates of the tcp.
 *
 * @param       codeStream          JPEG 2000 code stream
 * @param 		tileProcessor		tile processor
 * @param       stream              the stream to write data to.

 */
static bool j2k_update_rates(CodeStream *codeStream, TileProcessor *tileProcessor, BufferedStream *stream);

/**
 * Copies the decoding tile parameters onto all the tile parameters.
 * Creates also the tile decoder.
 *
 * @param       codeStream          JPEG 2000 code stream
 * @param 		tileProcessor		tile processor
 * @param       stream              the stream.

 */
static bool j2k_copy_default_tcp_and_create_tcd(CodeStream *codeStream, TileProcessor *tileProcessor,
		BufferedStream *stream);

/**
 * Reads the lookup table containing all the marker, status and action, and returns the handler associated
 * with the marker value.
 * @param       id            Marker value to look up
 *
 * @return      the handler associated with the id.
 */
static const  grk_dec_memory_marker_handler  *  j2k_get_marker_handler(
		uint16_t id);

/**
 * Read the tiles.
 *
 * @param       codeStream          JPEG 2000 code stream
 * @param 		tileProcessor		tile processor
 * @param       stream              the stream to read
 *
 */
static bool j2k_decompress_tiles(CodeStream *codeStream,  TileProcessor *tileProcessor, BufferedStream *stream);
static bool j2k_decompress_tile(CodeStream *codeStream,  TileProcessor *tileProcessor, BufferedStream *stream);
static bool j2k_decompress_tile_t2(CodeStream *codeStream, uint16_t tile_index, BufferedStream *stream);
static bool j2k_decompress_tile_t1(CodeStream *codeStream, TileProcessor *tileProcessor,bool multi_tile);

static bool j2k_init_header_writing(CodeStream *codeStream);
static bool j2k_post_write_tile(CodeStream *codeStream, TileProcessor *tileProcessor, BufferedStream *stream);


/**
 * Gets the offset of the header.
 *
 * @param       codeStream          JPEG 2000 code stream
 * @param 		tileProcessor		tile processor
 * @param       stream              the stream to write data to.

 */
static bool j2k_get_end_header(CodeStream *codeStream,  TileProcessor *tileProcessor, BufferedStream *stream);

/**
 * Write a tile part
 *
 * @param       codeStream              JPEG 2000 code stream
 * @param       stream                  the stream to write data to.

 */
static bool j2k_write_tile_part(CodeStream *codeStream, TileProcessor *tileProcessor,
		BufferedStream *stream);

/**
 * Ends the encoding, i.e. frees memory.
 *
 * @param       codeStream          JPEG 2000 code stream
 * @param 		tileProcessor		tile processor
 * @param       stream              the stream to write data to.

 */
static bool j2k_end_encoding(CodeStream *codeStream, TileProcessor *tileProcessor, BufferedStream *stream);

/**
 * Inits the Info
 *
 * @param       codeStream          JPEG 2000 code stream
 * @param 		tileProcessor		tile processor
 * @param       stream              stream.

 */
static bool j2k_init_info(CodeStream *codeStream,  TileProcessor *tileProcessor, BufferedStream *stream);

/**
 * Reads an unknown marker
 *
 * @param       codeStream                   JPEG 2000 code stream
 * @param       stream                the stream object to read from.
 * @param       output_marker           FIXME DOC

 *
 * @return      true                    if the marker could be deduced.
 */
static bool j2k_read_unk(CodeStream *codeStream, BufferedStream *stream,
		uint16_t *output_marker);



/**
 * Checks the progression order changes values. Tells of the poc given as input are valid.
 * A nice message is outputted at errors.
 *
 * @param       p_pocs                  the progression order changes.
 * @param       nb_pocs               the number of progression order changes.
 * @param       nb_resolutions        the number of resolutions.
 * @param       numcomps                the number of components
 * @param       numlayers               the number of layers.

 *
 * @return      true if the pocs are valid.
 */
static bool j2k_check_poc_val(const  grk_poc  *p_pocs, uint32_t nb_pocs,
		uint32_t nb_resolutions, uint32_t numcomps, uint32_t numlayers);

/**
 * Gets the number of tile parts used for the given change of progression (if any) and the given tile.
 *
 * @param               cp                      the coding parameters.
 * @param               pino            the offset of the given poc (i.e. its position in the coding parameter).
 * @param               tileno          the given tile.
 *
 * @return              the number of tile parts.
 */
static uint8_t j2k_get_num_tp(CodingParams *cp, uint32_t pino, uint16_t tileno);

/**
 * Calculates the total number of tile parts needed by the encoder to
 * compress such an image. If not enough memory is available, then the function return false.
 *
 * @param       cp              coding parameters for the image.
 * @param       p_nb_tile_parts total number of tile parts in whole image.
 * @param       image           image to compress.

 *
 * @return true if the function was successful, false else.
 */
static bool j2k_calculate_tp(CodingParams *cp, uint16_t *p_nb_tile_parts, grk_image *image);

/**
 * Checks for invalid number of tile-parts in SOT marker (TPsot==TNsot). See issue 254.
 *
 * @param       stream            the stream to read data from.
 * @param       tile_no             tile number we're looking for.
 * @param       p_correction_needed output value. if true, non conformant code stream needs TNsot correction.

 *
 * @return true if the function was successful, false else.
 */
static bool j2k_need_nb_tile_parts_correction(CodeStream *codeStream, BufferedStream *stream,
		uint16_t tile_no, bool *p_correction_needed);

static const j2k_mct_function j2k_mct_write_functions_from_float[] = {
		j2k_write_float_to_int16, j2k_write_float_to_int32,
		j2k_write_float_to_float, j2k_write_float_to_float64 };

static const grk_dec_memory_marker_handler j2k_memory_marker_handler_tab[] = {
{J2K_MS_SOT, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH_SOT, j2k_read_sot },
{J2K_MS_COD, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_cod },
{J2K_MS_COC, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_coc },
{J2K_MS_RGN, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_rgn },
{J2K_MS_QCD, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_qcd },
{J2K_MS_QCC, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_qcc },
{J2K_MS_POC, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_poc },
{J2K_MS_SIZ, J2K_DEC_STATE_MH_SIZ, j2k_read_siz },
{J2K_MS_CAP, J2K_DEC_STATE_MH,j2k_read_cap },
{J2K_MS_TLM, J2K_DEC_STATE_MH, j2k_read_tlm },
{J2K_MS_PLM, J2K_DEC_STATE_MH, j2k_read_plm },
{J2K_MS_PLT, J2K_DEC_STATE_TPH, j2k_read_plt },
{J2K_MS_PPM, J2K_DEC_STATE_MH,	j2k_read_ppm },
{J2K_MS_PPT, J2K_DEC_STATE_TPH, j2k_read_ppt },
{J2K_MS_SOP, 0, 0 },
{J2K_MS_CRG, J2K_DEC_STATE_MH, j2k_read_crg },
{J2K_MS_COM, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_com },
{J2K_MS_MCT, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_mct },
{J2K_MS_CBD, J2K_DEC_STATE_MH, j2k_read_cbd },
{J2K_MS_MCC, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_mcc },
{J2K_MS_MCO, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, j2k_read_mco },
{J2K_MS_UNK, J2K_DEC_STATE_MH | J2K_DEC_STATE_TPH, 0 }/*j2k_read_unk is directly used*/
};

static bool j2k_read_unk(CodeStream *codeStream, BufferedStream *stream,
		uint16_t *output_marker) {
	uint16_t unknown_marker;
	const grk_dec_memory_marker_handler *marker_handler;
	uint32_t size_unk = 2;

	/* preconditions*/
	assert(codeStream != nullptr);
	assert(stream != nullptr);

	GROK_WARN("Unknown marker 0x%02x", *output_marker);

	while (true) {
		if (!codeStream->read_marker(stream, &unknown_marker))
			return false;
		if (!(unknown_marker < 0xff00)) {

			/* Get the marker handler from the marker ID*/
			marker_handler = j2k_get_marker_handler(unknown_marker);

			if (!(codeStream->m_decoder.m_state
					& marker_handler->states)) {
				GROK_ERROR("Marker is not compliant with its position");
				return false;
			} else {
				if (marker_handler->id != J2K_MS_UNK) {
					/* Add the marker to the code stream index*/
					if (codeStream->cstr_index && marker_handler->id != J2K_MS_SOT) {
						bool res = j2k_add_mhmarker(codeStream->cstr_index,
						J2K_MS_UNK, stream->tell() - size_unk, size_unk);

						if (res == false) {
							GROK_ERROR("Not enough memory to add mh marker");
							return false;
						}
					}
					break; /* next marker is known and well located */
				} else {
					size_unk += 2;
				}
			}
		}
	}
	*output_marker = marker_handler->id;

	return true;
}
static const grk_dec_memory_marker_handler* j2k_get_marker_handler(
		uint16_t id) {
	const grk_dec_memory_marker_handler *e;
	for (e = j2k_memory_marker_handler_tab; e->id != 0; ++e) {
		if (e->id == id) {
			break; /* we find a handler corresponding to the marker ID*/
		}
	}
	return e;
}

void j2k_destroy(CodeStream *codeStream) {
   delete codeStream;
}

static bool j2k_exec(CodeStream *codeStream, std::vector<j2k_procedure> &procs,
		BufferedStream *stream) {
	assert(codeStream != nullptr);
	assert(stream != nullptr);

    bool result = std::all_of(procs.begin(), procs.end(),
		   [&](j2k_procedure &proc){ return proc(codeStream, codeStream->m_tileProcessor, stream); });
	procs.clear();

	return result;
}

static bool j2k_read_header_procedure(CodeStream *codeStream,TileProcessor *tileProcessor,
		BufferedStream *stream) {
	assert(stream != nullptr);
	assert(codeStream != nullptr);

	uint16_t current_marker;
	uint16_t marker_size;
	const grk_dec_memory_marker_handler *marker_handler = nullptr;
	bool has_siz = false;
	bool has_cod = false;
	bool has_qcd = false;

	/*  We enter in the main header */
	codeStream->m_decoder.m_state = J2K_DEC_STATE_MH_SOC;

	/* Try to read the SOC marker, the code stream must begin with SOC marker */
	if (!j2k_read_soc(codeStream, stream)) {
		GROK_ERROR("Expected a SOC marker ");
		return false;
	}
	if (!codeStream->read_marker(stream, &current_marker))
		return false;

	/* Try to read until the SOT is detected */
	while (current_marker != J2K_MS_SOT) {

		/* Check if the current marker ID is valid */
		if (current_marker < 0xff00) {
			GROK_ERROR("A marker ID was expected (0xff--) instead of %.8x",
					current_marker);
			return false;
		}

		/* Get the marker handler from the marker ID */
		marker_handler = j2k_get_marker_handler(current_marker);

		/* Manage case where marker is unknown */
		if (marker_handler->id == J2K_MS_UNK) {
			GROK_WARN("Unknown marker 0x%02x detected.", marker_handler->id);
			if (!j2k_read_unk(codeStream, stream, &current_marker)) {
				GROK_ERROR("Unable to read unknown marker 0x%02x.",
						marker_handler->id);
				return false;
			}

			if (current_marker == J2K_MS_SOT)
				break; /* SOT marker is detected main header is completely read */
			else
				/* Get the marker handler from the marker ID */
				marker_handler = j2k_get_marker_handler(current_marker);
		}

		if (marker_handler->id == J2K_MS_SIZ)
			has_siz = true;
		else if (marker_handler->id == J2K_MS_COD)
			has_cod = true;
		else if (marker_handler->id == J2K_MS_QCD)
			has_qcd = true;

		/* Check if the marker is known and if it is the right place to find it */
		if (!(codeStream->m_decoder.m_state & marker_handler->states)) {
			GROK_ERROR("Marker is not compliant with its position");
			return false;
		}
		if (!codeStream->read_marker(stream, &marker_size))
			return false;

		/* Check marker size (does not include marker ID but includes marker size) */
		if (marker_size < 2) {
			GROK_ERROR("Inconsistent marker size");
			return false;
		}

		marker_size -= 2; /* Subtract the size of the marker ID already read */

		if (!codeStream->process_marker(marker_handler, current_marker, marker_size,
											tileProcessor, stream))
			return false;

		if (codeStream->cstr_index) {
			/* Add the marker to the code stream index*/
			if (!j2k_add_mhmarker(codeStream->cstr_index, marker_handler->id,
					stream->tell() - marker_size - 4, marker_size + 4)) {
				GROK_ERROR("Not enough memory to add mh marker");
				return false;
			}
		}
		if (!codeStream->read_marker(stream, &current_marker))
			return false;
	}
	if (!has_siz) {
		GROK_ERROR("required SIZ marker not found in main header");
		return false;
	} else if (!has_cod) {
		GROK_ERROR("required COD marker not found in main header");
		return false;
	} else if (!has_qcd) {
		GROK_ERROR("required QCD marker not found in main header");
		return false;
	}
	if (!j2k_merge_ppm(&(codeStream->m_cp))) {
		GROK_ERROR("Failed to merge PPM data");
		return false;
	}
	/* Position of the last element if the main header */
	if (codeStream->cstr_index)
		codeStream->cstr_index->main_head_end = (uint32_t) stream->tell() - 2;
	/* Next step: read a tile-part header */
	codeStream->m_decoder.m_state = J2K_DEC_STATE_TPH_SOT;

	return true;
}

bool j2k_read_header(BufferedStream *stream, CodeStream *codeStream,
		grk_header_info *header_info, grk_image **p_image) {
	assert(codeStream != nullptr);
	assert(stream != nullptr);

	/* create an empty image header */
	codeStream->m_input_image = grk_image_create0();
	if (!codeStream->m_input_image) {
		return false;
	}

	/* customization of the validation */
	if (!j2k_init_decompress_validation(codeStream))
		return false;

	/* validation of the parameters codec */
	if (!j2k_exec(codeStream, codeStream->m_validation_list, stream))
		return false;

	/* customization of the encoding */
	if (!j2k_init_header_reading(codeStream))
		return false;


	/* read header */
	if (!j2k_exec(codeStream, codeStream->m_procedure_list, stream))
		return false;

	if (header_info) {
		CodingParams *cp = nullptr;
		TileCodingParams *tcp = nullptr;
		TileComponentCodingParams *tccp = nullptr;

		cp = &(codeStream->m_cp);
		tcp = codeStream->get_current_decode_tcp(codeStream->m_tileProcessor);
		tccp = &tcp->tccps[0];

		header_info->cblockw_init = 1 << tccp->cblkw;
		header_info->cblockh_init = 1 << tccp->cblkh;
		header_info->irreversible = tccp->qmfbid == 0;
		header_info->mct = tcp->mct;
		header_info->rsiz = cp->rsiz;
		header_info->numresolutions = tccp->numresolutions;
		// !!! assume that coding style is constant across all tile components
		header_info->csty = tccp->csty;
		// !!! assume that mode switch is constant across all tiles
		header_info->cblk_sty = tccp->cblk_sty;
		for (uint32_t i = 0; i < header_info->numresolutions; ++i) {
			header_info->prcw_init[i] = 1 << tccp->prcw[i];
			header_info->prch_init[i] = 1 << tccp->prch[i];
		}
		header_info->tx0 = codeStream->m_cp.tx0;
		header_info->ty0 = codeStream->m_cp.ty0;

		header_info->t_width = codeStream->m_cp.t_width;
		header_info->t_height = codeStream->m_cp.t_height;

		header_info->t_grid_width = codeStream->m_cp.t_grid_width;
		header_info->t_grid_height = codeStream->m_cp.t_grid_height;

		header_info->tcp_numlayers = tcp->numlayers;

		header_info->num_comments = codeStream->m_cp.num_comments;
		for (size_t i = 0; i < header_info->num_comments; ++i) {
			header_info->comment[i] = codeStream->m_cp.comment[i];
			header_info->comment_len[i] = codeStream->m_cp.comment_len[i];
			header_info->isBinaryComment[i] = codeStream->m_cp.isBinaryComment[i];
		}
	}
	*p_image = grk_image_create0();
	if (!(*p_image)) {
		return false;
	}
	/* Copy code stream image information to the output image */
	grk_copy_image_header(codeStream->m_input_image, *p_image);
	if (codeStream->cstr_index) {
		/*Allocate and initialize some elements of codestrem index*/
		if (!j2k_allocate_tile_element_cstr_index(codeStream)) {
			return false;
		}
	}
	return true;
}
static bool j2k_decompress_validation(CodeStream *codeStream,TileProcessor *tileProcessor,
		BufferedStream *stream) {

	(void) stream;
	GRK_UNUSED(tileProcessor);
	bool is_valid = true;

	/* preconditions*/
	assert(codeStream != nullptr);
	assert(stream != nullptr);

	/* STATE checking */
	/* make sure the state is at 0 */
	is_valid &=
			(codeStream->m_decoder.m_state == J2K_DEC_STATE_NONE);

	/* PARAMETER VALIDATION */
	return is_valid;
}

void j2k_init_decompressor(void *j2k_void, grk_dparameters *parameters) {
	CodeStream *j2k = (CodeStream*) j2k_void;
	if (j2k && parameters) {
		j2k->m_cp.m_coding_params.m_dec.m_layer = parameters->cp_layer;
		j2k->m_cp.m_coding_params.m_dec.m_reduce = parameters->cp_reduce;
	}
}

/**
 * Sets up the procedures to do on decoding data.
 * Developers wanting to extend the library can add their own reading procedures.
 */
static bool j2k_init_decompress(CodeStream *codeStream) {
	/* preconditions*/
	assert(codeStream != nullptr);

	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_decompress_tiles);
	// custom procedures here

	return true;
}

bool j2k_end_decompress(CodeStream *codeStream, BufferedStream *stream) {
	(void) codeStream;
	(void) stream;

	return true;
}

static bool j2k_init_header_reading(CodeStream *codeStream) {
	/* preconditions*/
	assert(codeStream != nullptr);

	codeStream->m_procedure_list.push_back(
			(j2k_procedure) j2k_read_header_procedure);
	// custom procedures here
	codeStream->m_procedure_list.push_back(
			(j2k_procedure) j2k_copy_default_tcp_and_create_tcd);

	return true;
}

static bool j2k_init_decompress_validation(CodeStream *codeStream) {
	/* preconditions*/
	assert(codeStream != nullptr);

	codeStream->m_validation_list.push_back(
			(j2k_procedure) j2k_decompress_validation);

	return true;
}

static bool j2k_need_nb_tile_parts_correction(CodeStream *codeStream,
		BufferedStream *stream, uint16_t tile_no, bool *p_correction_needed) {
	uint8_t header_data[10];
	uint16_t current_marker;
	uint16_t marker_size;
	uint16_t read_tile_no;
	uint8_t current_part, num_parts;
	uint32_t tot_len;
	SOTMarker sotMarker;

	/* initialize to no correction needed */
	*p_correction_needed = false;

	/* We can't do much in this case, seek is needed */
	if (!stream->has_seek())
		return true;

	uint64_t stream_pos_backup = stream->tell();
	while (true) {
		if (!codeStream->read_marker(stream, &current_marker))
			/* assume all is OK */
			return stream->seek(stream_pos_backup);

		if (current_marker != J2K_MS_SOT)
			/* assume all is OK */
			return stream->seek(stream_pos_backup);

		if (!codeStream->read_marker(stream, &marker_size)) {
			GROK_ERROR("Stream too short");
			return false;
		}
		/* Check marker size for SOT Marker */
		if (marker_size != 10) {
			GROK_ERROR("Inconsistent marker size");
			return false;
		}
		marker_size -= 2;

		if (stream->read(header_data, marker_size) != marker_size) {
			GROK_ERROR("Stream too short");
			return false;
		}

		if (!sotMarker.get_sot_values(header_data, marker_size, &read_tile_no,
				&tot_len, &current_part, &num_parts))
			return false;

		/* we found what we were looking for */
		if (read_tile_no == tile_no)
			break;

		if (tot_len < 14U) {
			/* last SOT until EOC or invalid Psot value */
			/* assume all is OK */
			return stream->seek(stream_pos_backup);
		}
		tot_len -= sot_marker_segment_len;
		/* look for next SOT marker */
		if (!stream->skip((int64_t) (tot_len)))
			return stream->seek(stream_pos_backup);
	}

	/* check for correction */
	if (current_part == num_parts)
		*p_correction_needed = true;

	return stream->seek(stream_pos_backup);
}

bool j2k_read_tile_header(CodeStream *codeStream, TileProcessor *tileProcessor,
	bool *p_go_on, BufferedStream *stream) {
	assert(codeStream);

	codeStream->m_tileProcessor = tileProcessor;
	auto decoder = &codeStream->m_decoder;
	TileCodingParams *tcp = nullptr;

	uint16_t current_marker = J2K_MS_SOT;
	uint16_t marker_size = 0;

	assert(stream != nullptr);
	assert(codeStream != nullptr);

	/* Reach the End Of Codestream ?*/
	if (decoder->m_state == J2K_DEC_STATE_EOC)
		current_marker = J2K_MS_EOC;
	/* We need to encounter a SOT marker (a new tile-part header) */
	else if (decoder->m_state != J2K_DEC_STATE_TPH_SOT)
		goto fail;

	/* Seek in code stream for SOT marker specifying desired tile index.
	 * If we don't find it, we stop when we read the EOC or run out of data */
	while ((!decoder->ready_to_decode_tile_part_data)
			&& (current_marker != J2K_MS_EOC)) {

		/* read until SOD is detected */
		while (current_marker != J2K_MS_SOD) {
			// end of stream with no EOC
			if (stream->get_number_byte_left() == 0) {
				decoder->m_state = J2K_DEC_STATE_NO_EOC;
				GROK_WARN("Missing EOC marker");
				break;
			}
			if (!codeStream->read_marker(stream, &marker_size))
				goto fail;
			if (marker_size < 2) {
				GROK_ERROR("Inconsistent marker size");
				goto fail;
			}

			// subtract tile part header and header marker size
			if (decoder->m_state & J2K_DEC_STATE_TPH)
				tileProcessor->tile_part_data_length -= (marker_size + 2);

			marker_size -= 2; /* Subtract the size of the marker ID already read */

			auto marker_handler = j2k_get_marker_handler(current_marker);
			if (!(decoder->m_state & marker_handler->states)) {
				GROK_ERROR("Marker is not compliant with its position");
				goto fail;
			}

			if (!codeStream->process_marker(marker_handler, current_marker, marker_size,
											tileProcessor,stream))
				goto fail;

			/* Add the marker to the code stream index*/
			if (codeStream->cstr_index) {
				if (!TileLengthMarkers::add_to_index(
						tileProcessor->m_current_tile_index, codeStream->cstr_index,
						marker_handler->id,
						(uint32_t) stream->tell() - marker_size - 4,
						marker_size + 4)) {
					GROK_ERROR("Not enough memory to add tl marker");
					goto fail;
				}
			}

			// Cache position of last SOT marker read
			if (marker_handler->id == J2K_MS_SOT) {
				uint64_t sot_pos = stream->tell() - marker_size - 4;
				if (sot_pos > decoder->m_last_sot_read_pos)
					decoder->m_last_sot_read_pos = sot_pos;
			}

			if (decoder->m_skip_data) {
				// Skip the rest of the tile part header
				if (!stream->skip(tileProcessor->tile_part_data_length)) {
					GROK_ERROR("Stream too short");
					goto fail;
				}
				current_marker = J2K_MS_SOD; //We force current marker to equal SOD
			} else {
				while (true) {
					// read next marker id
					if (!codeStream->read_marker(stream, &current_marker))
						goto fail;

					/* handle unknown marker */
					if (current_marker == J2K_MS_UNK) {
						GROK_WARN("Unknown marker 0x%02x detected.",
								current_marker);
						if (!j2k_read_unk(codeStream, stream, &current_marker)) {
							GROK_ERROR("Unable to read unknown marker 0x%02x.",
									current_marker);
							goto fail;
						}
						continue;
					}
					break;
				}
			}
		}

		// no bytes left and no EOC marker : we're done!
		if (!stream->get_number_byte_left()
				&& decoder->m_state == J2K_DEC_STATE_NO_EOC)
			break;

		/* If we didn't skip data before, we need to read the SOD marker*/
		if (!decoder->m_skip_data) {
			/* Read the SOD marker and skip data ? FIXME */
			if (!j2k_read_sod(codeStream, tileProcessor, stream))
				return false;
			if (decoder->ready_to_decode_tile_part_data
					&& !codeStream->m_nb_tile_parts_correction_checked) {
				/* Issue 254 */
				bool correction_needed;

				codeStream->m_nb_tile_parts_correction_checked = true;
				if (!j2k_need_nb_tile_parts_correction(codeStream, stream,
						tileProcessor->m_current_tile_index,
						&correction_needed)) {
					GROK_ERROR("j2k_apply_nb_tile_parts_correction error");
					goto fail;
				}
				if (correction_needed) {
					uint32_t nb_tiles = codeStream->m_cp.t_grid_width
							* codeStream->m_cp.t_grid_height;

					decoder->ready_to_decode_tile_part_data = false;
					codeStream->m_nb_tile_parts_correction = true;
					/* correct tiles */
					for (uint32_t tile_no = 0U; tile_no < nb_tiles; ++tile_no) {
						if (codeStream->m_cp.tcps[tile_no].m_nb_tile_parts != 0U) {
							codeStream->m_cp.tcps[tile_no].m_nb_tile_parts =
									(uint8_t) (codeStream->m_cp.tcps[tile_no].m_nb_tile_parts
											+ 1);
						}
					}
					GROK_WARN("Non conformant code stream TPsot==TNsot.");
				}
			}
			if (!decoder->ready_to_decode_tile_part_data) {
				if (!codeStream->read_marker(stream, &current_marker))
					goto fail;
			}
		} else {
			/* Indicate we will try to read a new tile-part header*/
			decoder->m_skip_data = false;
			decoder->ready_to_decode_tile_part_data = false;
			decoder->m_state = J2K_DEC_STATE_TPH_SOT;
			if (!codeStream->read_marker(stream, &current_marker))
				goto fail;
		}
	}
	// do QCD marker quantization step size sanity check
	// see page 553 of Taubman and Marcellin for more details on this check
	tcp = codeStream->get_current_decode_tcp(tileProcessor);
	if (tcp->main_qcd_qntsty != J2K_CCP_QNTSTY_SIQNT) {
		auto numComps = codeStream->m_input_image->numcomps;
		//1. Check main QCD
		uint32_t maxTileDecompositions = 0;
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
			auto decomps = tccp->numresolutions - 1;
			if (maxTileDecompositions < decomps)
				maxTileDecompositions = decomps;
		}
		if ((tcp->main_qcd_numStepSizes < 3 * maxTileDecompositions + 1)) {
			GROK_ERROR("From Main QCD marker, "
					"number of step sizes (%u) is less than "
					"3* (tile decompositions) + 1, "
					"where tile decompositions = %u ",
					tcp->main_qcd_numStepSizes, maxTileDecompositions);
			goto fail;
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
				auto decomps = tccp->numresolutions - 1;
				if (maxTileDecompositions < decomps)
					maxTileDecompositions = decomps;
			}
			if ((qcd_comp->numStepSizes < 3 * maxTileDecompositions + 1)) {
				GROK_ERROR("From Tile QCD marker, "
						"number of step sizes (%u) is less than"
						" 3* (tile decompositions) + 1, "
						"where tile decompositions = %u ",
						qcd_comp->numStepSizes, maxTileDecompositions);

				goto fail;
			}
		}
	}
	/* Current marker is the EOC marker ?*/
	if (current_marker == J2K_MS_EOC && decoder->m_state != J2K_DEC_STATE_EOC)
		decoder->m_state = J2K_DEC_STATE_EOC;

	//if we are not ready to decompress tile part data,
    // then skip tiles with no tile data i.e. no SOD marker
	if (!decoder->ready_to_decode_tile_part_data) {
		tcp = codeStream->m_cp.tcps + tileProcessor->m_current_tile_index;
		if (!tcp->m_tile_data){
			*p_go_on = false;
			return true;
		}
	}

	if (!j2k_merge_ppt(
			codeStream->m_cp.tcps + tileProcessor->m_current_tile_index)) {
		GROK_ERROR("Failed to merge PPT data");
		goto fail;
	}
	if (!tileProcessor->init_tile(tileProcessor->m_current_tile_index,
			codeStream->m_output_image, false)) {
		GROK_ERROR("Cannot decompress tile %u",
				tileProcessor->m_current_tile_index);
		goto fail;
	}
	*p_go_on = true;
	decoder->m_state |= J2K_DEC_STATE_DATA;

	return true;

fail:
	delete codeStream->m_tileProcessor;
	codeStream->m_tileProcessor = nullptr;

	return false;
}

static bool j2k_decompress_tile_t2(CodeStream *codeStream, uint16_t tile_index,
		BufferedStream *stream) {
	assert(stream != nullptr);
	assert(codeStream != nullptr);

	auto tileProcessor = codeStream->m_tileProcessor;
	auto decoder = &codeStream->m_decoder;

	if (!(decoder->m_state & J2K_DEC_STATE_DATA)){
	   GROK_ERROR("j2k_decompress_tile: no data.");
	   return false;
	}
	if (tile_index != tileProcessor->m_current_tile_index){
		   GROK_ERROR("j2k_decompress_tile: desired tile index %u "
				   "does not match current tile index %u.",
				   tile_index,
				   tileProcessor->m_current_tile_index);
		return false;
	}

	auto tcp = codeStream->m_cp.tcps + tile_index;
	if (!tcp->m_tile_data) {
		tcp->destroy();
		return false;
	}

	if (!tileProcessor->decompress_tile_t2(tcp->m_tile_data, tile_index)) {
		tcp->destroy();
		decoder->m_state |= J2K_DEC_STATE_ERR;
		GROK_ERROR("j2k_decompress_tile: failed to decompress.");
		return false;
	}

	// find next tile
	bool rc = true;
	bool doPost = !tileProcessor->current_plugin_tile
			|| (tileProcessor->current_plugin_tile->decode_flags
					& GRK_DECODE_POST_T1);
	if (doPost){
		try {
			rc =  decoder->findNextTile(stream);
		} catch (DecodeUnknownMarkerAtEndOfTileException &e) {
		}
	}

	return rc;
}

static bool j2k_decompress_tile_t1(CodeStream *codeStream, TileProcessor *tileProcessor, bool multi_tile) {
	assert(codeStream != nullptr);

	auto decoder = &codeStream->m_decoder;
	uint16_t tile_index = tileProcessor->m_current_tile_index;
	auto tcp = codeStream->m_cp.tcps + tile_index;
	if (!tcp->m_tile_data) {
		tcp->destroy();
		return false;
	}

	bool rc = true;
	bool doPost = !tileProcessor->current_plugin_tile
			|| (tileProcessor->current_plugin_tile->decode_flags
					& GRK_DECODE_POST_T1);

	// T1 decode of previous tile
	if (!tileProcessor->decompress_tile_t1()) {
		tcp->destroy();
		decoder->m_state |= J2K_DEC_STATE_ERR;
		GROK_ERROR("j2k_decompress_tile: failed to decompress.");
		return false;
	}

	if (doPost) {
		if (codeStream->m_output_image) {
			if (multi_tile) {
				if (!tileProcessor->copy_decompressed_tile_to_output_image(codeStream->m_output_image))
					return false;
			} else {
				/* transfer data from tile component to output image */
				uint32_t compno = 0;
				for (compno = 0; compno < codeStream->m_output_image->numcomps;
						compno++) {
					auto tilec = tileProcessor->tile->comps + compno;
					auto comp = codeStream->m_output_image->comps + compno;

					//transfer memory from tile component to output image
					tilec->buf->transfer(&comp->data, &comp->owns_data);
				}
			}
		}
		/* we only destroy the data, which will be re-read in read_tile_header*/
		delete tcp->m_tile_data;
		tcp->m_tile_data = nullptr;
	}

	return rc;
}


bool j2k_set_decompress_area(CodeStream *codeStream, grk_image *output_image,
		uint32_t start_x, uint32_t start_y, uint32_t end_x, uint32_t end_y) {
	return codeStream->set_decompress_area(output_image,
											start_x,
											start_y,
											end_x,
											end_y);
}

CodeStream* j2k_create_decompress(void) {
	CodeStream *j2k = new CodeStream();

#ifdef GRK_DISABLE_TPSOT_FIX
    j2k->m_coding_params.m_decoder.m_nb_tile_parts_correction_checked = 1;
#endif

	j2k->m_decoder.m_default_tcp = new TileCodingParams();
	if (!j2k->m_decoder.m_default_tcp) {
		j2k_destroy(j2k);
		return nullptr;
	}

	j2k->m_decoder.m_last_sot_read_pos = 0;

	/* code stream index creation */
	j2k->cstr_index = j2k_create_cstr_index();
	if (!j2k->cstr_index) {
		j2k_destroy(j2k);
		return nullptr;
	}
	return j2k;
}

/**
 *
 */
static bool j2k_decompress_tiles(CodeStream *codeStream, TileProcessor *tileProcessor, BufferedStream *stream) {
	GRK_UNUSED(tileProcessor);
	bool go_on = true;
	uint32_t num_tiles_to_decode = codeStream->m_cp.t_grid_height
			* codeStream->m_cp.t_grid_width;
	bool multi_tile = num_tiles_to_decode > 1;
	std::atomic<bool> success(true);
	std::atomic<uint32_t> num_tiles_decoded(0);
	ThreadPool pool(std::min<uint32_t>((uint32_t)ThreadPool::get()->num_threads(), num_tiles_to_decode));
	std::vector< std::future<int> > results;

	if (multi_tile && codeStream->m_output_image) {
		if (!codeStream->alloc_output_data(codeStream->m_output_image))
			return false;
	}

	// read header and perform T2
	for (uint32_t tileno = 0; tileno < num_tiles_to_decode; tileno++) {
		//1. read header
		auto processor = new TileProcessor(codeStream);
		if (!j2k_read_tile_header(codeStream,processor, &go_on,stream))
			return false;

		if (!go_on){
			delete processor;
			codeStream->m_tileProcessor = nullptr;
			break;
		}

		//2. T2 decode
		if (!j2k_decompress_tile_t2(codeStream,
				processor->m_current_tile_index, stream)){
				GROK_ERROR("Failed to decompress tile %u/%u",
						processor->m_current_tile_index + 1,
						num_tiles_to_decode);
				delete processor;
				codeStream->m_tileProcessor = nullptr;
				return false;
		}

		if (!processor->m_corrupt_packet) {
			if (pool.num_threads() > 1) {
				results.emplace_back(
					pool.enqueue([codeStream,processor,
								  num_tiles_to_decode,
								  multi_tile,
								  &num_tiles_decoded, &success] {
						if (success) {
							if (!j2k_decompress_tile_t1(codeStream, processor,multi_tile)){
								GROK_ERROR("Failed to decompress tile %u/%u",
										processor->m_current_tile_index + 1,num_tiles_to_decode);
								success = false;
							} else {
								num_tiles_decoded++;
							}
						}
						delete processor;
						return 0;
					})
				);
			} else {
				if (!j2k_decompress_tile_t1(codeStream, processor,multi_tile)){
						GROK_ERROR("Failed to decompress tile %u/%u",
								processor->m_current_tile_index + 1,num_tiles_to_decode);
						delete processor;
						codeStream->m_tileProcessor = nullptr;
						return false;
				} else {
					num_tiles_decoded++;
				}
				delete processor;
			}
		} else {
			delete processor;
		}

		if (stream->get_number_byte_left() == 0
				|| codeStream->m_decoder.m_state
						== J2K_DEC_STATE_NO_EOC)
			break;

	}

	for(auto && result: results){
		result.get();
	}
	// sanity checks
	if (num_tiles_decoded == 0) {
		GROK_ERROR("No tiles were decoded. Exiting");
		return false;
	} else if (num_tiles_decoded < num_tiles_to_decode) {
		uint32_t decoded = num_tiles_decoded;
		GROK_WARN("Only %u out of %u tiles were decoded", decoded,
				num_tiles_to_decode);
	}
	codeStream->m_tileProcessor = nullptr;

	return success;
}

/*
 * Read and decompress one tile.
 */
static bool j2k_decompress_tile(CodeStream *codeStream,TileProcessor *tileProcessor,
		BufferedStream *stream) {
	GRK_UNUSED(tileProcessor);
	bool go_on = true;
	bool rc = false;

	/*Allocate and initialize some elements of code stream index if not already done*/
	if (!codeStream->cstr_index->tile_index) {
		if (!j2k_allocate_tile_element_cstr_index(codeStream))
			return false;
	}
	if (codeStream->m_tile_ind_to_dec == -1) {
		GROK_ERROR("j2k_decompress_tile: Unable to decompress tile "
				"since first tile SOT has not been detected");
		return false;
	}

	/* Move into the code stream to the first SOT used to decompress the desired tile */
	uint16_t tile_index_to_decode =	(uint16_t) (codeStream->m_tile_ind_to_dec);
	if (codeStream->cstr_index->tile_index) {
		if (codeStream->cstr_index->tile_index->tp_index) {
			if (!codeStream->cstr_index->tile_index[tile_index_to_decode].nb_tps) {
				/* the index for this tile has not been built,
				 *  so move to the last SOT read */
				if (!(stream->seek(
						codeStream->m_decoder.m_last_sot_read_pos
								+ 2))) {
					GROK_ERROR("Problem with seek function");
					return false;
				}
			} else {
				if (!(stream->seek(
						codeStream->cstr_index->tile_index[tile_index_to_decode].tp_index[0].start_pos
								+ 2))) {
					GROK_ERROR("Problem with seek function");
					return false;
				}
			}
			/* Special case if we have previously read the EOC marker (if the previous tile decoded is the last ) */
			if (codeStream->m_decoder.m_state == J2K_DEC_STATE_EOC)
				codeStream->m_decoder.m_state =
						J2K_DEC_STATE_TPH_SOT;
		}
	}

	// if we have a TLM marker, then we can skip tiles until
	// we get to desired tile
	if (codeStream->m_cp.tlm_markers){
		codeStream->m_cp.tlm_markers->getInit();
	    auto tl = codeStream->m_cp.tlm_markers->getNext();
	    //GROK_INFO("TLM : index: %u, length : %u", tl.tile_number, tl.length);
	    uint16_t tileNumber = 0;
	    while (stream->get_number_byte_left() != 0 &&
	    		tileNumber != codeStream->m_tile_ind_to_dec){
	    	if (tl.length == 0){
	    		GROK_ERROR("j2k_decompress_tile: corrupt TLM marker");
	    		return false;
	    	}
	    	stream->skip(tl.length);
	    	tl = codeStream->m_cp.tlm_markers->getNext();
	    	if (tl.has_tile_number)
	    		tileNumber = tl.tile_number;
	    	else
	    		tileNumber++;
	    }
	}

	tileProcessor = new TileProcessor(codeStream);
	if (!j2k_read_tile_header(codeStream, tileProcessor, &go_on, stream))
		goto cleanup;

	if (!j2k_decompress_tile_t2(codeStream, tileProcessor->m_current_tile_index, stream))
		goto cleanup;

	if (!j2k_decompress_tile_t1(codeStream, tileProcessor, false))
		goto cleanup;


	if (tileProcessor->m_current_tile_index == tile_index_to_decode) {
		/* move into the code stream to the first SOT (FIXME or not move?)*/
		if (!(stream->seek(codeStream->cstr_index->main_head_end + 2))) {
			GROK_ERROR("Problem with seek function");
			goto cleanup;
		}
	} else {
		GROK_ERROR(
				"Tile read, decoded and updated is not the desired one (%u vs %u).",
				tileProcessor->m_current_tile_index + 1, tile_index_to_decode + 1);
		goto cleanup;
	}
	rc = true;
	cleanup:
	delete tileProcessor;

	return rc;
}

/**
 * Sets up the procedures to decode one tile.
 */
static bool j2k_init_decompress_tile(CodeStream *codeStream) {
	assert(codeStream != nullptr);
	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_decompress_tile);
	//custom procedures here

	return true;
}

bool j2k_decompress(CodeStream *codeStream, grk_plugin_tile *tile,
		BufferedStream *stream, grk_image *p_image) {
	if (!p_image)
		return false;

	codeStream->m_output_image = grk_image_create0();
	if (!(codeStream->m_output_image))
		return false;
	grk_copy_image_header(p_image, codeStream->m_output_image);

	/* customization of the decoding */
	if (!j2k_init_decompress(codeStream))
		return false;

	codeStream->current_plugin_tile = tile;

	/* Decode the code stream */
	if (!j2k_exec(codeStream, codeStream->m_procedure_list, stream))
		return false;

	/* Move data and information from codec output image to user output image*/
	transfer_image_data(codeStream->m_output_image, p_image);
	return true;
}

bool j2k_get_tile(CodeStream *codeStream, BufferedStream *stream, grk_image *p_image,
		uint16_t tile_index) {
	uint32_t compno;
	uint32_t tile_x, tile_y;
	grk_image_comp *img_comp;
	grk_rect original_image_rect, tile_rect, overlap_rect;

	if (!p_image) {
		GROK_ERROR("Image is null");
		return false;
	}

	if (tile_index >= codeStream->m_cp.t_grid_width * codeStream->m_cp.t_grid_height) {
		GROK_ERROR(
				"Tile index provided by the user is incorrect %u (max = %u) ",
				tile_index,
				(codeStream->m_cp.t_grid_width * codeStream->m_cp.t_grid_height) - 1);
		return false;
	}

	/* Compute the dimension of the desired tile*/
	tile_x = tile_index % codeStream->m_cp.t_grid_width;
	tile_y = tile_index / codeStream->m_cp.t_grid_width;

	original_image_rect = grk_rect(p_image->x0, p_image->y0, p_image->x1,
			p_image->y1);

	p_image->x0 = tile_x * codeStream->m_cp.t_width + codeStream->m_cp.tx0;
	if (p_image->x0 < codeStream->m_input_image->x0)
		p_image->x0 = codeStream->m_input_image->x0;
	p_image->x1 = (tile_x + 1) * codeStream->m_cp.t_width + codeStream->m_cp.tx0;
	if (p_image->x1 > codeStream->m_input_image->x1)
		p_image->x1 = codeStream->m_input_image->x1;

	p_image->y0 = tile_y * codeStream->m_cp.t_height + codeStream->m_cp.ty0;
	if (p_image->y0 < codeStream->m_input_image->y0)
		p_image->y0 = codeStream->m_input_image->y0;
	p_image->y1 = (tile_y + 1) * codeStream->m_cp.t_height + codeStream->m_cp.ty0;
	if (p_image->y1 > codeStream->m_input_image->y1)
		p_image->y1 = codeStream->m_input_image->y1;

	tile_rect.x0 = p_image->x0;
	tile_rect.y0 = p_image->y0;
	tile_rect.x1 = p_image->x1;
	tile_rect.y1 = p_image->y1;

	if (original_image_rect.is_non_degenerate() && tile_rect.is_non_degenerate()
			&& original_image_rect.clip(tile_rect, &overlap_rect)
			&& overlap_rect.is_non_degenerate()) {
		p_image->x0 = (uint32_t) overlap_rect.x0;
		p_image->y0 = (uint32_t) overlap_rect.y0;
		p_image->x1 = (uint32_t) overlap_rect.x1;
		p_image->y1 = (uint32_t) overlap_rect.y1;
	} else {
		GROK_WARN(
				"Decode region <%u,%u,%u,%u> does not overlap requested tile %u. Ignoring.",
				original_image_rect.x0, original_image_rect.y0,
				original_image_rect.x1, original_image_rect.y1, tile_index);
	}

	img_comp = p_image->comps;
	auto reduce = codeStream->m_cp.m_coding_params.m_dec.m_reduce;
	for (compno = 0; compno < p_image->numcomps; ++compno) {
		uint32_t comp_x1, comp_y1;

		img_comp->x0 = ceildiv<uint32_t>(p_image->x0, img_comp->dx);
		img_comp->y0 = ceildiv<uint32_t>(p_image->y0, img_comp->dy);
		comp_x1 = ceildiv<uint32_t>(p_image->x1, img_comp->dx);
		comp_y1 = ceildiv<uint32_t>(p_image->y1, img_comp->dy);

		img_comp->w = (uint_ceildivpow2(comp_x1, reduce)
				- uint_ceildivpow2(img_comp->x0, reduce));
		img_comp->h = (uint_ceildivpow2(comp_y1, reduce)
				- uint_ceildivpow2(img_comp->y0, reduce));

		img_comp++;
	}
	if (codeStream->m_output_image)
		grk_image_destroy(codeStream->m_output_image);
	codeStream->m_output_image = grk_image_create0();
	if (!(codeStream->m_output_image))
		return false;
	grk_copy_image_header(p_image, codeStream->m_output_image);
	codeStream->m_tile_ind_to_dec = (int32_t) tile_index;

	// reset tile part numbers, in case we are re-using the same codec object
	// from previous decompress
	uint32_t nb_tiles = codeStream->m_cp.t_grid_width * codeStream->m_cp.t_grid_height;
	for (uint32_t i = 0; i < nb_tiles; ++i)
		codeStream->m_cp.tcps[i].m_current_tile_part_index = -1;

	/* customization of the decoding */
	if (!j2k_init_decompress_tile(codeStream))
		return false;

	/* Decode the code stream */
	if (!j2k_exec(codeStream, codeStream->m_procedure_list, stream))
		return false;


	/* Move data from codec output image to user output image*/
	transfer_image_data(codeStream->m_output_image, p_image);

	return true;
}

CodeStream* j2k_create_compress(void) {
	CodeStream *j2k = new CodeStream();
	return j2k;
}

bool j2k_init_compress(CodeStream *codeStream, grk_cparameters *parameters,
		grk_image *image) {
	uint32_t i, j, tileno, numpocs_tile;
	CodingParams *cp = nullptr;

	if (!codeStream || !parameters || !image) {
		return false;
	}
	//sanity check on image
	if (image->numcomps < 1 || image->numcomps > max_num_components) {
		GROK_ERROR(
				"Invalid number of components specified while setting up JP2 encoder");
		return false;
	}
	if ((image->x1 < image->x0) || (image->y1 < image->y0)) {
		GROK_ERROR(
				"Invalid input image dimensions found while setting up JP2 encoder");
		return false;
	}
	for (i = 0; i < image->numcomps; ++i) {
		auto comp = image->comps + i;
		if (comp->w == 0 || comp->h == 0) {
			GROK_ERROR(
					"Invalid input image component dimensions found while setting up JP2 encoder");
			return false;
		}
		if (comp->prec == 0) {
			GROK_ERROR(
					"Invalid component precision of 0 found while setting up JP2 encoder");
			return false;
		}
	}

	// create private sanitized copy of imagte
	codeStream->m_input_image = grk_image_create0();
	if (!codeStream->m_input_image) {
		GROK_ERROR("Failed to allocate image header.");
		return false;
	}
	grk_copy_image_header(image, codeStream->m_input_image);
	if (image->comps) {
		for (uint32_t compno = 0; compno < image->numcomps; compno++) {
			if (image->comps[compno].data) {
				codeStream->m_input_image->comps[compno].data =
						image->comps[compno].data;
				image->comps[compno].data = nullptr;

			}
		}
	}

	if ((parameters->numresolution == 0)
			|| (parameters->numresolution > GRK_J2K_MAXRLVLS)) {
		GROK_ERROR("Invalid number of resolutions : %u not in range [1,%u]",
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
		for (i = 0; i < parameters->tcp_numlayers; i++) {
			if (parameters->tcp_rates[i] < min_rate) {
				parameters->tcp_rates[i] = min_rate;
				cap = true;
			}
		}
		if (cap) {
			GROK_WARN("The desired maximum code stream size has limited\n"
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
			GROK_WARN(
					"JPEG 2000 Scalable Digital Cinema profiles not supported");
			parameters->rsiz = GRK_PROFILE_NONE;
		} else {
			if (Profile::is_cinema_compliant(image, parameters->rsiz))
				Profile::set_cinema_parameters(parameters, image);
			else
				parameters->rsiz = GRK_PROFILE_NONE;
		}
	} else if (GRK_IS_STORAGE(parameters->rsiz)) {
		GROK_WARN("JPEG 2000 Long Term Storage profile not supported");
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
			GROK_WARN("JPEG 2000 Part-2 profile defined\n"
					"but no Part-2 extension enabled.\n"
					"Profile set to NONE.");
			parameters->rsiz = GRK_PROFILE_NONE;
		} else if (parameters->rsiz
				!= ((GRK_PROFILE_PART2) | (GRK_EXTENSION_MCT))) {
			GROK_WARN("Unsupported Part-2 extension enabled\n"
					"Profile set to NONE.");
			parameters->rsiz = GRK_PROFILE_NONE;
		}
	}

	if (parameters->numpocs) {
		/* initialisation of POC */
		if (!j2k_check_poc_val(parameters->POC, parameters->numpocs,
				parameters->numresolution, image->numcomps,
				parameters->tcp_numlayers)) {
			GROK_ERROR("Failed to initialize POC");
			return false;
		}
	}

	/*
	 copy user encoding parameters
	 */

	/* keep a link to cp so that we can destroy it later in j2k_destroy_compress */
	cp = &(codeStream->m_cp);

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
				GROK_WARN("Empty comment. Ignoring");
				continue;
			}
			if (cp->comment_len[i] > GRK_MAX_COMMENT_LENGTH) {
				GROK_WARN(
						"Comment length %s is greater than maximum comment length %u. Ignoring",
						cp->comment_len[i], GRK_MAX_COMMENT_LENGTH);
				continue;
			}
			cp->comment[i] = (char*) grk_buffer_new(cp->comment_len[i]);
			if (!cp->comment[i]) {
				GROK_ERROR(
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

		cp->comment[0] = (char*) grk_buffer_new(clen + strlen(version) + 1);
		if (!cp->comment[0]) {
			GROK_ERROR("Not enough memory to allocate comment string");
			return false;
		}
		sprintf(cp->comment[0], "%s%s", comment, version);
		cp->comment_len[0] = (uint16_t) strlen(cp->comment[0]);
		cp->num_comments = 1;
		cp->isBinaryComment[0] = false;
	}

	/*
	 calculate other encoding parameters
	 */

	if (parameters->tile_size_on) {
		// avoid divide by zero
		if (cp->t_width == 0 || cp->t_height == 0) {
			GROK_ERROR("Invalid tile dimensions (%u,%u)",cp->t_width, cp->t_height);
			return false;
		}
		cp->t_grid_width = ceildiv<uint32_t>((image->x1 - cp->tx0),
				cp->t_width);
		cp->t_grid_height = ceildiv<uint32_t>((image->y1 - cp->ty0),
				cp->t_height);
	} else {
		cp->t_width = image->x1 - cp->tx0;
		cp->t_height = image->y1 - cp->ty0;
	}

	if (parameters->tp_on) {
		cp->m_coding_params.m_enc.m_tp_flag = parameters->tp_flag;
		cp->m_coding_params.m_enc.m_tp_on = true;
	}

	uint8_t numgbits = parameters->isHT ? 1 : 2;
	cp->tcps = new TileCodingParams[cp->t_grid_width * cp->t_grid_height];
	for (tileno = 0; tileno < cp->t_grid_width * cp->t_grid_height; tileno++) {
		TileCodingParams *tcp = cp->tcps + tileno;
		tcp->isHT = parameters->isHT;
		tcp->qcd.generate(numgbits, (uint32_t) (parameters->numresolution - 1),
				!parameters->irreversible, image->comps[0].prec, tcp->mct > 0,
				image->comps[0].sgnd);
		tcp->numlayers = parameters->tcp_numlayers;

		for (j = 0; j < tcp->numlayers; j++) {
			if (cp->m_coding_params.m_enc.m_fixed_quality)
				tcp->distoratio[j] = parameters->tcp_distoratio[j];
			else
				tcp->rates[j] = parameters->tcp_rates[j];
		}

		tcp->csty = parameters->csty;
		tcp->prg = parameters->prog_order;
		tcp->mct = parameters->tcp_mct;

		numpocs_tile = 0;
		tcp->POC = false;

		if (parameters->numpocs) {
			/* initialisation of POC */
			tcp->POC = true;
			for (i = 0; i < parameters->numpocs; i++) {
				if (tileno + 1 == parameters->POC[i].tile) {
					auto tcp_poc = &tcp->pocs[numpocs_tile];

					tcp_poc->resno0 = parameters->POC[numpocs_tile].resno0;
					tcp_poc->compno0 = parameters->POC[numpocs_tile].compno0;
					tcp_poc->layno1 = parameters->POC[numpocs_tile].layno1;
					tcp_poc->resno1 = parameters->POC[numpocs_tile].resno1;
					tcp_poc->compno1 = parameters->POC[numpocs_tile].compno1;
					tcp_poc->prg1 = parameters->POC[numpocs_tile].prg1;
					tcp_poc->tile = parameters->POC[numpocs_tile].tile;
					numpocs_tile++;
				}
			}
			if (numpocs_tile == 0) {
				GROK_ERROR("Problem with specified progression order changes");
				return false;
			}
			tcp->numpocs = numpocs_tile - 1;
		} else {
			tcp->numpocs = 0;
		}

		tcp->tccps = (TileComponentCodingParams*) grk_calloc(image->numcomps,
				sizeof(TileComponentCodingParams));
		if (!tcp->tccps) {
			GROK_ERROR(
					"Not enough memory to allocate tile component coding parameters");
			return false;
		}
		if (parameters->mct_data) {

			uint32_t lMctSize = image->numcomps * image->numcomps
					* (uint32_t) sizeof(float);
			auto lTmpBuf = (float*) grk_malloc(lMctSize);
			auto dc_shift = (int32_t*) ((uint8_t*) parameters->mct_data
					+ lMctSize);

			if (!lTmpBuf) {
				GROK_ERROR("Not enough memory to allocate temp buffer");
				return false;
			}

			tcp->mct = 2;
			tcp->m_mct_coding_matrix = (float*) grk_malloc(lMctSize);
			if (!tcp->m_mct_coding_matrix) {
				grk_free(lTmpBuf);
				lTmpBuf = nullptr;
				GROK_ERROR(
						"Not enough memory to allocate encoder MCT coding matrix ");
				return false;
			}
			memcpy(tcp->m_mct_coding_matrix, parameters->mct_data, lMctSize);
			memcpy(lTmpBuf, parameters->mct_data, lMctSize);

			tcp->m_mct_decoding_matrix = (float*) grk_malloc(lMctSize);
			if (!tcp->m_mct_decoding_matrix) {
				grk_free(lTmpBuf);
				lTmpBuf = nullptr;
				GROK_ERROR(
						"Not enough memory to allocate encoder MCT decoding matrix ");
				return false;
			}
			if (matrix_inversion_f(lTmpBuf, (tcp->m_mct_decoding_matrix),
					image->numcomps) == false) {
				grk_free(lTmpBuf);
				lTmpBuf = nullptr;
				GROK_ERROR("Failed to inverse encoder MCT decoding matrix ");
				return false;
			}

			tcp->mct_norms = (double*) grk_malloc(
					image->numcomps * sizeof(double));
			if (!tcp->mct_norms) {
				grk_free(lTmpBuf);
				lTmpBuf = nullptr;
				GROK_ERROR("Not enough memory to allocate encoder MCT norms ");
				return false;
			}
			mct::calculate_norms(tcp->mct_norms, image->numcomps,
					tcp->m_mct_decoding_matrix);
			grk_free(lTmpBuf);

			for (i = 0; i < image->numcomps; i++) {
				auto tccp = &tcp->tccps[i];
				tccp->m_dc_level_shift = dc_shift[i];
			}

			if (j2k_init_mct_encoding(tcp, image) == false) {
				/* free will be handled by j2k_destroy */
				GROK_ERROR("Failed to set up j2k mct encoding");
				return false;
			}
		} else {
			if (tcp->mct == 1) {
				if (image->color_space == GRK_CLRSPC_EYCC
						|| image->color_space == GRK_CLRSPC_SYCC) {
					GROK_WARN("Disabling MCT for sYCC/eYCC colour space");
					tcp->mct = 0;
				} else if (image->numcomps >= 3) {
					if ((image->comps[0].dx != image->comps[1].dx)
							|| (image->comps[0].dx != image->comps[2].dx)
							|| (image->comps[0].dy != image->comps[1].dy)
							|| (image->comps[0].dy != image->comps[2].dy)) {
						GROK_WARN(
								"Cannot perform MCT on components with different dimensions. Disabling MCT.");
						tcp->mct = 0;
					}
				}
			}
			for (i = 0; i < image->numcomps; i++) {
				auto tccp = tcp->tccps + i;
				auto comp = image->comps + i;
				if (!comp->sgnd) {
					tccp->m_dc_level_shift = 1 << (comp->prec - 1);
				}
			}
		}

		for (i = 0; i < image->numcomps; i++) {
			auto tccp = &tcp->tccps[i];

			/* 0 => one precinct || 1 => custom precinct  */
			tccp->csty = parameters->csty & J2K_CP_CSTY_PRT;
			tccp->numresolutions = parameters->numresolution;
			tccp->cblkw = uint_floorlog2(parameters->cblockw_init);
			tccp->cblkh = uint_floorlog2(parameters->cblockh_init);
			tccp->cblk_sty = parameters->cblk_sty;
			tccp->qmfbid = parameters->irreversible ? 0 : 1;
			tccp->qntsty = parameters->irreversible ?
			J2K_CCP_QNTSTY_SEQNT :
														J2K_CCP_QNTSTY_NOQNT;
			tccp->numgbits = numgbits;

			if ((int32_t) i == parameters->roi_compno)
				tccp->roishift = parameters->roi_shift;
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
							tccp->prcw[it_res] = 1;
						} else {
							tccp->prcw[it_res] = uint_floorlog2(
									parameters->prcw_init[p]);
						}
						if (parameters->prch_init[p] < 1) {
							tccp->prch[it_res] = 1;
						} else {
							tccp->prch[it_res] = uint_floorlog2(
									parameters->prch_init[p]);
						}
					} else {
						uint32_t res_spec = parameters->res_spec;
						uint32_t size_prcw = 0;
						uint32_t size_prch = 0;
						size_prcw = parameters->prcw_init[res_spec - 1]
								>> (p - (res_spec - 1));
						size_prch = parameters->prch_init[res_spec - 1]
								>> (p - (res_spec - 1));
						if (size_prcw < 1) {
							tccp->prcw[it_res] = 1;
						} else {
							tccp->prcw[it_res] = uint_floorlog2(size_prcw);
						}
						if (size_prch < 1) {
							tccp->prch[it_res] = 1;
						} else {
							tccp->prch[it_res] = uint_floorlog2(size_prch);
						}
					}
					p++;
					/*printf("\nsize precinct for level %u : %u,%u\n", it_res,tccp->prcw[it_res], tccp->prch[it_res]); */
				} /*end for*/
			} else {
				for (j = 0; j < tccp->numresolutions; j++) {
					tccp->prcw[j] = 15;
					tccp->prch[j] = 15;
				}
			}
			tcp->qcd.pull(tccp->stepsizes, !parameters->irreversible);
		}
	}
	grk_free(parameters->mct_data);
	parameters->mct_data = nullptr;

	return true;
}


bool j2k_start_compress(CodeStream *codeStream, BufferedStream *stream) {

	assert(codeStream != nullptr);
	assert(stream != nullptr);

	/* customization of the validation */
	if (!j2k_init_compress_validation(codeStream))
		return false;

	/* validation of the parameters codec */
	if (!j2k_exec(codeStream, codeStream->m_validation_list, stream))
		return false;

	/* customization of the encoding */
	if (!j2k_init_header_writing(codeStream))
		return false;

	/* write header */
	codeStream->m_tileProcessor = new TileProcessor(codeStream);
	return j2k_exec(codeStream, codeStream->m_procedure_list, stream);
}

bool j2k_compress(CodeStream *codeStream, grk_plugin_tile *tile,
		BufferedStream *stream) {
	assert(codeStream != nullptr);
	assert(stream != nullptr);

	auto tileProcessor = codeStream->m_tileProcessor;
	tileProcessor->current_plugin_tile = tile;

	uint32_t nb_tiles = (uint32_t) codeStream->m_cp.t_grid_height
			* codeStream->m_cp.t_grid_width;
	if (nb_tiles > max_num_tiles) {
		GROK_ERROR("Number of tiles %u is greater than %u max tiles "
				"allowed by the standard.", nb_tiles, max_num_tiles);
		return false;
	}
	bool transfer_image_to_tile = (nb_tiles == 1);
	for (uint16_t i = 0; i < nb_tiles; ++i) {
		if (!tileProcessor->pre_write_tile(i))
			return false;

		/* if we only have one tile, then simply set tile component data equal to
		 * image component data. Otherwise, allocate tile data and copy */
		for (uint32_t j = 0; j < tileProcessor->image->numcomps; ++j) {
			auto tilec = tileProcessor->tile->comps + j;
			if (transfer_image_to_tile) {
				tilec->buf->attach((tileProcessor->image->comps + j)->data);
			} else {
				if (!tilec->buf->alloc()) {
					GROK_ERROR("Error allocating tile component data.");
					return false;
				}
			}
		}
		if (!transfer_image_to_tile)
			tileProcessor->copy_image_to_tile();
		if (!j2k_post_write_tile(codeStream, tileProcessor, stream))
			return false;
	}
	return true;
}

bool j2k_end_compress(CodeStream *codeStream, BufferedStream *stream) {
	/* customization of the encoding */
	if (!j2k_init_end_compress(codeStream))
		return false;

	bool rc =  j2k_exec(codeStream, codeStream->m_procedure_list, stream);
	delete codeStream->m_tileProcessor;
	codeStream = nullptr;
	return rc;
}


bool j2k_check_poc_val(const grk_poc *p_pocs, uint32_t nb_pocs,
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
		index = step_r * p_pocs->resno0;
		/* take each resolution for each poc */
		for (resno = p_pocs->resno0;
				resno < std::min<uint32_t>(p_pocs->resno1, nb_resolutions);
				++resno) {
			uint32_t res_index = index + p_pocs->compno0 * step_c;

			/* take each comp of each resolution for each poc */
			for (compno = p_pocs->compno0;
					compno < std::min<uint32_t>(p_pocs->compno1, num_comps);
					++compno) {
				uint32_t comp_index = res_index + 0 * step_l;

				/* and finally take each layer of each res of ... */
				for (layno = 0;
						layno < std::min<uint32_t>(p_pocs->layno1, num_layers);
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
		GROK_ERROR("Missing packets possible loss of data");
	delete[] packet_array;
	return !loss;
}

static bool j2k_write_tile_part(CodeStream *codeStream,	TileProcessor *tileProcessor, BufferedStream *stream) {
	assert(codeStream != nullptr);
	assert(stream != nullptr);
	uint16_t currentTileNumber = tileProcessor->m_current_tile_index;
	auto cp = &codeStream->m_cp;
	bool firstTilePart = tileProcessor->m_current_tile_part_index == 0;

	//1. write SOT
	SOTMarker sot(stream);

	if (!sot.write(codeStream, tileProcessor))
		return false;
	uint32_t tile_part_bytes_written = sot_marker_segment_len;

	//2. write POC (only in first tile part)
	if (firstTilePart) {
		if (!GRK_IS_CINEMA(cp->rsiz)) {
			if (cp->tcps[currentTileNumber].numpocs) {
				auto tcp = codeStream->m_cp.tcps + currentTileNumber;
				auto image = codeStream->m_input_image;
				uint32_t nb_comp = image->numcomps;
				if (!j2k_write_poc(codeStream, tileProcessor, stream))
					return false;
				tile_part_bytes_written += getPocSize(nb_comp,
						1 + tcp->numpocs);
			}
		}
		/* set packno to zero when writing the first tile part
		 * (packno is used for SOP markers)
		 */
		tileProcessor->tile->packno = 0;
	}

	// 3. compress tile part
	if (!tileProcessor->compress_tile_part(currentTileNumber, stream,
			&tile_part_bytes_written)) {
		GROK_ERROR("Cannot compress tile");
		return false;
	}

	/* 4. write Psot in SOT marker */
	if (!sot.write_psot(tile_part_bytes_written))
		return false;

	// 5. update TLM
	if (codeStream->m_cp.tlm_markers)
		j2k_update_tlm(codeStream, tileProcessor->m_current_tile_index, tile_part_bytes_written);
	++tileProcessor->m_current_tile_part_index;

	return true;
}

static bool j2k_post_write_tile(CodeStream *codeStream, TileProcessor *tileProcessor, BufferedStream *stream) {
	assert(tileProcessor->m_current_tile_part_index == 0);

	//1. write first tile part
	tileProcessor->cur_pino = 0;
	tileProcessor->m_current_poc_tile_part_index = 0;
	if (!j2k_write_tile_part(codeStream, tileProcessor, stream))
		return false;

	//2. write the other tile parts
	uint8_t tot_num_tp;
	uint32_t pino;

	auto cp = &(codeStream->m_cp);
	auto tcp = cp->tcps + tileProcessor->m_current_tile_index;

	// write tile parts for first progression order
	tot_num_tp = j2k_get_num_tp(cp, 0, tileProcessor->m_current_tile_index);
	for (uint8_t tilepartno = 1; tilepartno < tot_num_tp; ++tilepartno) {
		tileProcessor->m_current_poc_tile_part_index = tilepartno;
		if (!j2k_write_tile_part(codeStream, tileProcessor, stream))
			return false;
	}

	// write tile parts for remaining progression orders
	for (pino = 1; pino <= tcp->numpocs; ++pino) {
		tileProcessor->cur_pino = pino;

		tot_num_tp = j2k_get_num_tp(cp, pino,
				tileProcessor->m_current_tile_index);
		for (uint8_t tilepartno = 0; tilepartno < tot_num_tp; ++tilepartno) {
			tileProcessor->m_current_poc_tile_part_index = tilepartno;
			if (!j2k_write_tile_part(codeStream, tileProcessor, stream))
				return false;
		}
	}
	++tileProcessor->m_current_tile_index;

	return true;
}

static bool j2k_init_end_compress(CodeStream *codeStream) {
	assert(codeStream != nullptr);
	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_eoc);
	if (codeStream->m_cp.m_coding_params.m_enc.writeTLM)
		codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_tlm_end);
	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_epc);
	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_end_encoding);
	//custom procedures here

	return true;
}

static bool j2k_mct_validation(CodeStream *codeStream,TileProcessor *tileProcessor, BufferedStream *stream) {
	(void) stream;
	GRK_UNUSED(tileProcessor);

	bool is_valid = true;
	uint32_t i, j;

	assert(codeStream != nullptr);
	assert(stream != nullptr);

	if ((codeStream->m_cp.rsiz & 0x8200) == 0x8200) {
		uint32_t nb_tiles = codeStream->m_cp.t_grid_height
				* codeStream->m_cp.t_grid_width;
		auto tcp = codeStream->m_cp.tcps;

		for (i = 0; i < nb_tiles; ++i) {
			if (tcp->mct == 2) {
				auto tccp = tcp->tccps;
				is_valid &= (tcp->m_mct_coding_matrix != nullptr);

				for (j = 0; j < codeStream->m_input_image->numcomps; ++j) {
					is_valid &= !(tccp->qmfbid & 1);
					++tccp;
				}
			}
			++tcp;
		}
	}

	return is_valid;
}

static bool j2k_compress_validation(CodeStream *codeStream,TileProcessor *tileProcessor,  BufferedStream *stream) {
	(void) stream;
	GRK_UNUSED(tileProcessor);
	bool is_valid = true;

	assert(codeStream != nullptr);
	assert(stream != nullptr);

	/* STATE checking */
	/* make sure the state is at 0 */
	is_valid &=
			(codeStream->m_decoder.m_state == J2K_DEC_STATE_NONE);

	/* ISO 15444-1:2004 states between 1 & 33 (decomposition levels between 0 -> 32) */
	if ((codeStream->m_cp.tcps->tccps->numresolutions == 0)
			|| (codeStream->m_cp.tcps->tccps->numresolutions > GRK_J2K_MAXRLVLS)) {
		GROK_ERROR("Invalid number of resolutions : %u not in range [1,%u]",
				codeStream->m_cp.tcps->tccps->numresolutions, GRK_J2K_MAXRLVLS);
		return false;
	}

	if (codeStream->m_cp.t_width == 0) {
		GROK_ERROR("Tile x dimension must be greater than zero ");
		return false;
	}

	if (codeStream->m_cp.t_height == 0) {
		GROK_ERROR("Tile y dimension must be greater than zero ");
		return false;
	}

	/* PARAMETER VALIDATION */
	return is_valid;
}

static bool j2k_init_compress_validation(CodeStream *codeStream) {
	assert(codeStream != nullptr);

	codeStream->m_validation_list.push_back(
			(j2k_procedure) j2k_compress_validation);
	//custom validation here
	codeStream->m_validation_list.push_back((j2k_procedure) j2k_mct_validation);

	return true;
}

static bool j2k_init_header_writing(CodeStream *codeStream) {
	assert(codeStream != nullptr);
	assert(!codeStream->m_tileProcessor);
	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_init_info);
	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_soc);
	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_siz);
	if (codeStream->m_cp.tcps[0].isHT)
		codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_cap);
	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_cod);
	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_qcd);
	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_all_coc);
	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_all_qcc);

	if (codeStream->m_cp.m_coding_params.m_enc.writeTLM)
		codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_tlm_begin);
	if (codeStream->m_cp.rsiz == GRK_PROFILE_CINEMA_4K)
		codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_poc);

	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_regions);
	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_write_com);
	//begin custom procedures
	if ((codeStream->m_cp.rsiz & (GRK_PROFILE_PART2 | GRK_EXTENSION_MCT))
			== (GRK_PROFILE_PART2 | GRK_EXTENSION_MCT)) {
		codeStream->m_procedure_list.push_back(
				(j2k_procedure) j2k_write_mct_data_group);
	}
	//end custom procedures

	if (codeStream->cstr_index)
		codeStream->m_procedure_list.push_back((j2k_procedure) j2k_get_end_header);
	codeStream->m_procedure_list.push_back((j2k_procedure) j2k_update_rates);

	return true;
}

bool j2k_init_mct_encoding(TileCodingParams *p_tcp, grk_image *p_image) {
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

			auto new_mct_records = (grk_mct_data*) grk_realloc(p_tcp->m_mct_records,
					p_tcp->m_nb_max_mct_records * sizeof(grk_mct_data));
			if (!new_mct_records) {
				grk_free(p_tcp->m_mct_records);
				p_tcp->m_mct_records = nullptr;
				p_tcp->m_nb_max_mct_records = 0;
				p_tcp->m_nb_mct_records = 0;
				/* GROK_ERROR( "Not enough memory to set up mct encoding"); */
				return false;
			}
			p_tcp->m_mct_records = new_mct_records;
			mct_deco_data = p_tcp->m_mct_records + p_tcp->m_nb_mct_records;

			memset(mct_deco_data, 0,
					(p_tcp->m_nb_max_mct_records - p_tcp->m_nb_mct_records)
							* sizeof(grk_mct_data));
		}
		mct_deco_data = p_tcp->m_mct_records + p_tcp->m_nb_mct_records;
		grk_free(mct_deco_data->m_data);
		mct_deco_data->m_data = nullptr;

		mct_deco_data->m_index = indix++;
		mct_deco_data->m_array_type = MCT_TYPE_DECORRELATION;
		mct_deco_data->m_element_type = MCT_TYPE_FLOAT;
		nb_elem = p_image->numcomps * p_image->numcomps;
		mct_size = nb_elem * MCT_ELEMENT_SIZE[mct_deco_data->m_element_type];
		mct_deco_data->m_data = (uint8_t*) grk_malloc(mct_size);

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
		new_mct_records = (grk_mct_data*) grk_realloc(p_tcp->m_mct_records,
				p_tcp->m_nb_max_mct_records * sizeof(grk_mct_data));
		if (!new_mct_records) {
			grk_free(p_tcp->m_mct_records);
			p_tcp->m_mct_records = nullptr;
			p_tcp->m_nb_max_mct_records = 0;
			p_tcp->m_nb_mct_records = 0;
			/* GROK_ERROR( "Not enough memory to set up mct encoding"); */
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
		grk_free(mct_offset_data->m_data);
		mct_offset_data->m_data = nullptr;
	}
	mct_offset_data->m_index = indix++;
	mct_offset_data->m_array_type = MCT_TYPE_OFFSET;
	mct_offset_data->m_element_type = MCT_TYPE_FLOAT;
	nb_elem = p_image->numcomps;
	mct_size = nb_elem * MCT_ELEMENT_SIZE[mct_offset_data->m_element_type];
	mct_offset_data->m_data = (uint8_t*) grk_malloc(mct_size);
	if (!mct_offset_data->m_data)
		return false;

	data = (float*) grk_malloc(nb_elem * sizeof(float));
	if (!data) {
		grk_free(mct_offset_data->m_data);
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
	grk_free(data);
	mct_offset_data->m_data_size = mct_size;
	++p_tcp->m_nb_mct_records;

	if (p_tcp->m_nb_mcc_records == p_tcp->m_nb_max_mcc_records) {
		grk_simple_mcc_decorrelation_data *new_mcc_records;
		p_tcp->m_nb_max_mcc_records += default_number_mct_records;
		new_mcc_records = (grk_simple_mcc_decorrelation_data*) grk_realloc(
				p_tcp->m_mcc_records,
				p_tcp->m_nb_max_mcc_records
						* sizeof(grk_simple_mcc_decorrelation_data));
		if (!new_mcc_records) {
			grk_free(p_tcp->m_mcc_records);
			p_tcp->m_mcc_records = nullptr;
			p_tcp->m_nb_max_mcc_records = 0;
			p_tcp->m_nb_mcc_records = 0;
			/* GROK_ERROR( "Not enough memory to set up mct encoding"); */
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

static bool j2k_end_encoding(CodeStream *codeStream, TileProcessor *tileProcessor, BufferedStream *stream) {
	(void) stream;
	(void) codeStream;
	assert(codeStream);
	GRK_UNUSED(tileProcessor);

	return true;
}

static bool j2k_init_info(CodeStream *codeStream, TileProcessor *tileProcessor, BufferedStream *stream) {
	(void) stream;
	assert(codeStream);
	GRK_UNUSED(tileProcessor);

	return j2k_calculate_tp(&codeStream->m_cp,
			&codeStream->m_encoder.m_total_tile_parts,
			codeStream->m_input_image);
}

static bool j2k_get_end_header(CodeStream *codeStream, TileProcessor *tileProcessor, BufferedStream *stream) {
	assert(codeStream != nullptr);
	assert(stream != nullptr);
	GRK_UNUSED(tileProcessor);

	codeStream->cstr_index->main_head_end = stream->tell();

	return true;
}

bool j2k_compress_tile(CodeStream *codeStream, TileProcessor *tp,
		uint16_t tile_index, uint8_t *p_data,
		uint64_t uncompressed_data_size, BufferedStream *stream) {
	auto tileProcessor = tp ? tp : codeStream->m_tileProcessor;
	if (!tileProcessor->pre_write_tile(tile_index)) {
		GROK_ERROR("Error while pre_write_tile with tile index = %u",
				tile_index);
		return false;
	} else {
		/* Allocate data */
		for (uint32_t j = 0; j < tileProcessor->image->numcomps; ++j) {
			auto tilec = tileProcessor->tile->comps + j;

			if (!tilec->buf->alloc()) {
				GROK_ERROR("Error allocating tile component data.");
				return false;
			}
		}

		/* now copy data into the tile component */
		if (!tileProcessor->copy_image_data_to_tile(p_data,
				uncompressed_data_size)) {
			GROK_ERROR("Size mismatch between tile data and sent data.");
			return false;
		}
		if (!j2k_post_write_tile(codeStream, tileProcessor, stream)) {
			GROK_ERROR("Error while j2k_post_write_tile with tile index = %u",
					tile_index);
			return false;
		}
	}

	return true;
}

/* FIXME DOC*/
static bool j2k_copy_default_tcp_and_create_tcd(CodeStream *codeStream,TileProcessor *tileProcessor,
		BufferedStream *stream) {
	(void) stream;
	GRK_UNUSED(tileProcessor);

	uint32_t nb_tiles;
	uint32_t i, j;
	uint32_t tccp_size;
	uint32_t mct_size;
	uint32_t mcc_records_size, mct_records_size;
	grk_mct_data *src_mct_rec, *dest_mct_rec;
	grk_simple_mcc_decorrelation_data *src_mcc_rec, *dest_mcc_rec;
	uint32_t offset;

	assert(codeStream != nullptr);
	assert(stream != nullptr);

	auto image = codeStream->m_input_image;
	nb_tiles = codeStream->m_cp.t_grid_height * codeStream->m_cp.t_grid_width;
	auto tcp = codeStream->m_cp.tcps;
	tccp_size = image->numcomps * (uint32_t) sizeof(TileComponentCodingParams);
	auto default_tcp = codeStream->m_decoder.m_default_tcp;
	mct_size = image->numcomps * image->numcomps * (uint32_t) sizeof(float);

	/* For each tile */
	for (i = 0; i < nb_tiles; ++i) {
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
			tcp->m_mct_decoding_matrix = (float*) grk_malloc(mct_size);
			if (!tcp->m_mct_decoding_matrix)
				return false;
			memcpy(tcp->m_mct_decoding_matrix,
					default_tcp->m_mct_decoding_matrix, mct_size);
		}

		/* Get the mct_record of the dflt_tile_cp and copy them into the current tile cp*/
		mct_records_size = default_tcp->m_nb_max_mct_records
				* (uint32_t) sizeof(grk_mct_data);
		tcp->m_mct_records = (grk_mct_data*) grk_malloc(mct_records_size);
		if (!tcp->m_mct_records)
			return false;
		memcpy(tcp->m_mct_records, default_tcp->m_mct_records,
				mct_records_size);

		/* Copy the mct record data from dflt_tile_cp to the current tile*/
		src_mct_rec = default_tcp->m_mct_records;
		dest_mct_rec = tcp->m_mct_records;

		for (j = 0; j < default_tcp->m_nb_mct_records; ++j) {
			if (src_mct_rec->m_data) {
				dest_mct_rec->m_data = (uint8_t*) grk_malloc(
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
		mcc_records_size = default_tcp->m_nb_max_mcc_records
				* (uint32_t) sizeof(grk_simple_mcc_decorrelation_data);
		tcp->m_mcc_records = (grk_simple_mcc_decorrelation_data*) grk_malloc(
				mcc_records_size);
		if (!tcp->m_mcc_records)
			return false;
		memcpy(tcp->m_mcc_records, default_tcp->m_mcc_records,
				mcc_records_size);
		tcp->m_nb_max_mcc_records = default_tcp->m_nb_max_mcc_records;

		/* Copy the mcc record data from dflt_tile_cp to the current tile*/
		src_mcc_rec = default_tcp->m_mcc_records;
		dest_mcc_rec = tcp->m_mcc_records;
		for (j = 0; j < default_tcp->m_nb_max_mcc_records; ++j) {
			if (src_mcc_rec->m_decorrelation_array) {
				offset = (uint32_t) (src_mcc_rec->m_decorrelation_array
						- default_tcp->m_mct_records);
				dest_mcc_rec->m_decorrelation_array = tcp->m_mct_records
						+ offset;
			}
			if (src_mcc_rec->m_offset_array) {
				offset = (uint32_t) (src_mcc_rec->m_offset_array
						- default_tcp->m_mct_records);
				dest_mcc_rec->m_offset_array = tcp->m_mct_records + offset;
			}
			++src_mcc_rec;
			++dest_mcc_rec;
		}

		/* Copy all the dflt_tile_compo_cp to the current tile cp */
		memcpy(current_tccp, default_tcp->tccps, tccp_size);

		/* Move to next tile cp*/
		++tcp;
	}

	return true;
}

static bool j2k_update_rates(CodeStream *codeStream, TileProcessor *tileProcessor, BufferedStream *stream) {
	GRK_UNUSED(tileProcessor);
	uint32_t i, j, k;
	double *rates = 0;
	uint32_t bits_empty, size_pixel;
	uint32_t last_res;
	assert(codeStream != nullptr);
	assert(stream != nullptr);

	auto cp = &(codeStream->m_cp);
	auto image = codeStream->m_input_image;
	auto tcp = cp->tcps;

	auto width = image->x1 - image->x0;
	auto height = image->y1 - image->y0;
	if (width <= 0 || height <= 0)
		return false;

	bits_empty = 8 * image->comps->dx * image->comps->dy;
	size_pixel = image->numcomps * image->comps->prec;
	auto header_size = (double) stream->tell();

	for (i = 0; i < cp->t_grid_height; ++i) {
		for (j = 0; j < cp->t_grid_width; ++j) {
			double stride = 0;
			if (cp->m_coding_params.m_enc.m_tp_on)
				stride = (tcp->m_nb_tile_parts - 1) * 14;

			double offset = stride / tcp->numlayers;

			/* 4 borders of the tile rescale on the image if necessary */
			uint32_t x0 = std::max<uint32_t>((cp->tx0 + j * cp->t_width),
					image->x0);
			uint32_t y0 = std::max<uint32_t>((cp->ty0 + i * cp->t_height),
					image->y0);
			uint32_t x1 = std::min<uint32_t>((cp->tx0 + (j + 1) * cp->t_width),
					image->x1);
			uint32_t y1 = std::min<uint32_t>((cp->ty0 + (i + 1) * cp->t_height),
					image->y1);
			uint64_t numTilePixels = (uint64_t) (x1 - x0) * (y1 - y0);

			for (k = 0; k < tcp->numlayers; ++k) {
				rates = tcp->rates + k;
				if (*rates > 0.0f)
					*rates = ((((double) size_pixel * (double) numTilePixels))
							/ ((double) *rates * (double) bits_empty)) - offset;
			}
			++tcp;
		}
	}
	tcp = cp->tcps;

	for (i = 0; i < cp->t_grid_height; ++i) {
		for (j = 0; j < cp->t_grid_width; ++j) {
			rates = tcp->rates;
			/* 4 borders of the tile rescale on the image if necessary */
			uint32_t x0 = std::max<uint32_t>((cp->tx0 + j * cp->t_width),
					image->x0);
			uint32_t y0 = std::max<uint32_t>((cp->ty0 + i * cp->t_height),
					image->y0);
			uint32_t x1 = std::min<uint32_t>((cp->tx0 + (j + 1) * cp->t_width),
					image->x1);
			uint32_t y1 = std::min<uint32_t>((cp->ty0 + (i + 1) * cp->t_height),
					image->y1);
			uint64_t numTilePixels = (uint64_t) (x1 - x0) * (y1 - y0);

			double sot_adjust = ((double) numTilePixels * (double) header_size)
					/ ((double) width * height);
			if (*rates > 0.0) {
				*rates -= sot_adjust;
				if (*rates < 30.0f)
					*rates = 30.0f;
			}
			++rates;
			last_res = tcp->numlayers - 1;
			for (k = 1; k < last_res; ++k) {
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

char* j2k_convert_progression_order(GRK_PROG_ORDER prg_order) {
	j2k_prog_order *po;
	for (po = j2k_prog_order_list; po->enum_prog != -1; po++) {
		if (po->enum_prog == prg_order)
			return (char*) po->str_prog;
	}

	return po->str_prog;
}

static uint8_t j2k_get_num_tp(CodingParams *cp, uint32_t pino,
		uint16_t tileno) {
	uint64_t num_tp = 1;

	/*  preconditions */
	assert(tileno < (cp->t_grid_width * cp->t_grid_height));
	assert(pino < (cp->tcps[tileno].numpocs + 1));

	/* get the given tile coding parameter */
	auto tcp = &cp->tcps[tileno];
	assert(tcp != nullptr);

	auto current_poc = &(tcp->pocs[pino]);
	assert(current_poc != 0);

	/* get the progression order as a character string */
	auto prog = j2k_convert_progression_order(tcp->prg);
	assert(strlen(prog) > 0);

	if (cp->m_coding_params.m_enc.m_tp_on) {
		for (uint32_t i = 0; i < 4; ++i) {
			switch (prog[i]) {
			/* component wise */
			case 'C':
				num_tp *= current_poc->compE;
				break;
				/* resolution wise */
			case 'R':
				num_tp *= current_poc->resE;
				break;
				/* precinct wise */
			case 'P':
				num_tp *= current_poc->prcE;
				break;
				/* layer wise */
			case 'L':
				num_tp *= current_poc->layE;
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
	assert(num_tp <= 255);
	return (uint8_t) num_tp;
}

static bool j2k_calculate_tp(CodingParams *cp, uint16_t *p_nb_tile_parts,
		grk_image *image) {
	uint32_t pino;
	uint16_t tileno;
	uint16_t nb_tiles;

	assert(p_nb_tile_parts != nullptr);
	assert(cp != nullptr);
	assert(image != nullptr);

	nb_tiles = (uint16_t) (cp->t_grid_width * cp->t_grid_height);
	*p_nb_tile_parts = 0;
	auto tcp = cp->tcps;

	/* INDEX >> */
	/* TODO mergeV2: check this part which use cstr_info */
	/*if (codeStream->cstr_info) {
	 grk_tile_info  * info_tile_ptr = codeStream->cstr_info->tile;
	 for (tileno = 0; tileno < nb_tiles; ++tileno) {
	 uint32_t cur_totnum_tp = 0;
	 pi_update_encoding_parameters(image,cp,tileno);
	 for (pino = 0; pino <= tcp->numpocs; ++pino)
	 {
	 uint32_t tp_num = j2k_get_num_tp(cp,pino,tileno);
	 *p_nb_tiles = *p_nb_tiles + tp_num;
	 cur_totnum_tp += tp_num;
	 }
	 tcp->m_nb_tile_parts = cur_totnum_tp;
	 info_tile_ptr->tp = ( grk_tp_info  *) grk_malloc(cur_totnum_tp * sizeof( grk_tp_info) );
	 if (info_tile_ptr->tp == nullptr) {
	 return false;
	 }
	 memset(info_tile_ptr->tp,0,cur_totnum_tp * sizeof( grk_tp_info) );
	 info_tile_ptr->num_tps = cur_totnum_tp;
	 ++info_tile_ptr;
	 ++tcp;
	 }
	 }
	 else */{
		for (tileno = 0; tileno < nb_tiles; ++tileno) {
			uint8_t cur_totnum_tp = 0;
			pi_update_encoding_parameters(image, cp, tileno);
			for (pino = 0; pino <= tcp->numpocs; ++pino) {
				uint8_t tp_num = j2k_get_num_tp(cp, pino, tileno);

				*p_nb_tile_parts += tp_num;
				cur_totnum_tp = (uint8_t) (cur_totnum_tp + tp_num);
			}
			tcp->m_nb_tile_parts = cur_totnum_tp;
			++tcp;
		}
	}
	return true;
}

CodeStream::CodeStream() : m_input_image(nullptr),
							m_output_image(nullptr),
							cstr_index(nullptr),
							m_tileProcessor(nullptr),
							m_tile_ind_to_dec(-1),
							m_marker_scratch(nullptr),
							m_marker_scratch_size(0),
						    whole_tile_decoding(true),
							current_plugin_tile(nullptr),
							 m_nb_tile_parts_correction_checked(false),
							 m_nb_tile_parts_correction(false)
{
    memset(&m_cp, 0 , sizeof(CodingParams));
}
CodeStream::~CodeStream(){
	delete m_decoder.m_default_tcp;
	m_cp.destroy();
	j2k_destroy_cstr_index(cstr_index);
	grk_image_destroy(m_input_image);
	grk_image_destroy(m_output_image);
	grk_free(m_marker_scratch);
}


bool CodeStream::set_decompress_area(grk_image *output_image,
		uint32_t start_x, uint32_t start_y, uint32_t end_x, uint32_t end_y) {

	auto cp = &(m_cp);
	auto image = m_input_image;
	auto decoder = &m_decoder;

	/* Check if we have read the main header */
	if (decoder->m_state != J2K_DEC_STATE_TPH_SOT) {
		GROK_ERROR(
				"Need to decompress the main header before setting decompress area");
		return false;
	}

	if (!start_x && !start_y && !end_x && !end_y) {
		decoder->m_start_tile_x_index = 0;
		decoder->m_start_tile_y_index = 0;
		decoder->m_end_tile_x_index = cp->t_grid_width;
		decoder->m_end_tile_y_index = cp->t_grid_height;

		return true;
	}

	/* Check if the positions provided by the user are correct */

	/* Left */
	if (start_x > image->x1) {
		GROK_ERROR("Left position of the decoded area (region_x0=%u)"
				" is outside the image area (Xsiz=%u).", start_x, image->x1);
		return false;
	} else if (start_x < image->x0) {
		GROK_WARN("Left position of the decoded area (region_x0=%u)"
				" is outside the image area (XOsiz=%u).", start_x, image->x0);
		decoder->m_start_tile_x_index = 0;
		output_image->x0 = image->x0;
	} else {
		decoder->m_start_tile_x_index = (start_x - cp->tx0) / cp->t_width;
		output_image->x0 = start_x;
	}

	/* Up */
	if (start_y > image->y1) {
		GROK_ERROR("Up position of the decoded area (region_y0=%u)"
				" is outside the image area (Ysiz=%u).", start_y, image->y1);
		return false;
	} else if (start_y < image->y0) {
		GROK_WARN("Up position of the decoded area (region_y0=%u)"
				" is outside the image area (YOsiz=%u).", start_y, image->y0);
		decoder->m_start_tile_y_index = 0;
		output_image->y0 = image->y0;
	} else {
		decoder->m_start_tile_y_index = (start_y - cp->ty0) / cp->t_height;
		output_image->y0 = start_y;
	}

	/* Right */
	assert(end_x > 0);
	assert(end_y > 0);
	if (end_x < image->x0) {
		GROK_ERROR("Right position of the decoded area (region_x1=%u)"
				" is outside the image area (XOsiz=%u).", end_x, image->x0);
		return false;
	} else if (end_x > image->x1) {
		GROK_WARN("Right position of the decoded area (region_x1=%u)"
				" is outside the image area (Xsiz=%u).", end_x, image->x1);
		decoder->m_end_tile_x_index = cp->t_grid_width;
		output_image->x1 = image->x1;
	} else {
		// avoid divide by zero
		if (cp->t_width == 0) {
			return false;
		}
		decoder->m_end_tile_x_index = ceildiv<uint32_t>(end_x - cp->tx0,
				cp->t_width);
		output_image->x1 = end_x;
	}

	/* Bottom */
	if (end_y < image->y0) {
		GROK_ERROR("Bottom position of the decoded area (region_y1=%u)"
				" is outside the image area (YOsiz=%u).", end_y, image->y0);
		return false;
	}
	if (end_y > image->y1) {
		GROK_WARN("Bottom position of the decoded area (region_y1=%u)"
				" is outside the image area (Ysiz=%u).", end_y, image->y1);
		decoder->m_end_tile_y_index = cp->t_grid_height;
		output_image->y1 = image->y1;
	} else {
		// avoid divide by zero
		if (cp->t_height == 0) {
			return false;
		}
		decoder->m_end_tile_y_index = ceildiv<uint32_t>(end_y - cp->ty0,
				cp->t_height);
		output_image->y1 = end_y;
	}
	decoder->m_discard_tiles = true;
	whole_tile_decoding = false;
	if (!update_image_dimensions(output_image,
			cp->m_coding_params.m_dec.m_reduce))
		return false;

	GROK_INFO("Setting decoding area to %u,%u,%u,%u", output_image->x0,
			output_image->y0, output_image->x1, output_image->y1);
	return true;
}


bool CodeStream::process_marker(const grk_dec_memory_marker_handler* marker_handler,
		uint16_t current_marker, uint16_t marker_size, TileProcessor *tileProcessor,
		BufferedStream *stream){

	if (!m_marker_scratch) {
		m_marker_scratch = (uint8_t*) grk_calloc(1, default_header_size);
		if (!m_marker_scratch)
			return false;
		m_marker_scratch_size = default_header_size;
	}

	// need more scratch memory
	if (marker_size > m_marker_scratch_size) {
		uint8_t *new_header_data = nullptr;
		if (marker_size > stream->get_number_byte_left()) {
			GROK_ERROR("Marker size inconsistent with stream length");
			return false;
		}
		new_header_data = (uint8_t*) grk_realloc(
				m_marker_scratch, marker_size);
		if (!new_header_data) {
			grk_free(m_marker_scratch);
			m_marker_scratch = nullptr;
			m_marker_scratch_size = 0;
			GROK_ERROR("Not enough memory to read header");
			return false;
		}
		m_marker_scratch = new_header_data;
		m_marker_scratch_size = marker_size;
	}

	if (stream->read(m_marker_scratch, marker_size)
			!= marker_size) {
		GROK_ERROR("Stream too short");
		return false;
	}

	/* Handle the marker */
	if (!marker_handler->handler) {
		/* See issue #175 */
		GROK_ERROR("Not sure how that happened.");
		return false;
	}
	if (!(*(marker_handler->handler))(this,tileProcessor,
			m_marker_scratch, marker_size)) {
		GROK_ERROR("Fail to read the current marker segment (%#x)",
				current_marker);
		return false;
	}

	return true;
}


bool CodeStream::isDecodingTilePartHeader() {
	return (m_decoder.m_state & J2K_DEC_STATE_TPH);
}
TileCodingParams* CodeStream::get_current_decode_tcp(TileProcessor *tileProcessor) {
	return (isDecodingTilePartHeader()) ?
			m_cp.tcps + tileProcessor->m_current_tile_index :
			m_decoder.m_default_tcp;
}

bool CodeStream::read_marker(BufferedStream *stream, uint16_t *val){
	uint8_t temp[2];
	if (stream->read(temp, 2) != 2) {
		GROK_WARN("read marker: stream too short");
		return false;
	}
	grk_read<uint16_t>(temp, val);

	return true;

}

bool CodeStream::alloc_output_data(grk_image *p_output_image){
	auto image_src = m_input_image;
	for (uint32_t i = 0; i < image_src->numcomps; i++) {
		auto comp_dest = p_output_image->comps + i;

		if (comp_dest->w * comp_dest->h == 0) {
			GROK_ERROR("Output image has invalid dimensions %u x %u",
					comp_dest->w, comp_dest->h);
			return false;
		}

		/* Allocate output component buffer if necessary */
		if (!comp_dest->data) {
			if (!grk_image_single_component_data_alloc(comp_dest))
				return false;
			memset(comp_dest->data, 0,
						(uint64_t)comp_dest->w * comp_dest->h * sizeof(int32_t));
		}
	}

	return true;

}
}
