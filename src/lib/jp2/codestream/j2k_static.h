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

#pragma once

namespace grk {

struct  grk_dec_memory_marker_handler  {
	/** marker value */
	uint16_t id;
	/** value of the state when the marker can appear */
	uint32_t states;
	/** action linked to the marker */
	bool (*handler)(CodeStream *p_j2k, uint8_t *p_header_data,
			uint16_t header_size);
} ;


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
static bool j2k_init_header_reading(CodeStream *p_j2k);

/**
 * The read header procedure.
 */
static bool j2k_read_header_procedure(CodeStream *p_j2k, BufferedStream *stream);

/**
 * The default encoding validation procedure without any extension.
 *
 * @param       p_j2k                   the JPEG 2000 codec to validate.
 * @param       stream                the input stream to validate.

 *
 * @return true if the parameters are correct.
 */
static bool j2k_compress_validation(CodeStream *p_j2k, BufferedStream *stream);

/**
 * The default decoding validation procedure without any extension.
 *
 * @param       p_j2k                   the JPEG 2000 codec to validate.
 * @param       stream                                the input stream to validate.

 *
 * @return true if the parameters are correct.
 */
static bool j2k_decompress_validation(CodeStream *p_j2k, BufferedStream *stream);

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool j2k_init_compress_validation(CodeStream *p_j2k);

/**
 * Sets up the validation ,i.e. adds the procedures to launch to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool j2k_init_decompress_validation(CodeStream *p_j2k);

/**
 * Sets up the validation ,i.e. adds the procedures to make sure the codec parameters
 * are valid. Developers wanting to extend the library can add their own validation procedures.
 */
static bool j2k_init_end_compress(CodeStream *p_j2k);

/**
 * The mct encoding validation procedure.
 *
 * @param       p_j2k                   the JPEG 2000 codec to validate.
 * @param       stream                                the input stream to validate.

 *
 * @return true if the parameters are correct.
 */
static bool j2k_mct_validation(CodeStream *p_j2k, BufferedStream *stream);

/**
 * Executes the given procedures on the given codec.
 *
 * @param       p_j2k                   the JPEG 2000 codec to execute the procedures on.
 * @param       p_procedure_list        the list of procedures to execute
 * @param       stream                the stream to execute the procedures on.
 *
 * @return      true                            if all the procedures were successfully executed.
 */
static bool j2k_exec(CodeStream *p_j2k, std::vector<j2k_procedure> *p_procedure_list,
		BufferedStream *stream);

/**
 * Updates the rates of the tcp.
 *
 * @param       p_j2k                   J2K codec.
 * @param       stream                the stream to write data to.

 */
static bool j2k_update_rates(CodeStream *p_j2k, BufferedStream *stream);

/**
 * Copies the decoding tile parameters onto all the tile parameters.
 * Creates also the tile decoder.
 *
 * @param       p_j2k                   J2K codec.
 * @param       stream                the stream to write data to.

 */
static bool j2k_copy_default_tcp_and_create_tcd(CodeStream *p_j2k,
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
 */
static bool j2k_decompress_tiles(CodeStream *p_j2k, BufferedStream *stream);

static bool j2k_init_header_writing(CodeStream *p_j2k);
static bool j2k_pre_write_tile(CodeStream *p_j2k, uint16_t tile_index);
static bool j2k_post_write_tile(CodeStream *p_j2k, BufferedStream *stream);


/**
 * Gets the offset of the header.
 *
 * @param       p_j2k                   J2K codec.
 * @param       stream                the stream to write data to.

 */
static bool j2k_get_end_header(CodeStream *p_j2k, BufferedStream *stream);

/**
 * Write a tile part
 *
 * @param       p_j2k                    J2K codec.
 * @param       writePOC                 write POC
 * @param       stream                   the stream to write data to.

 */
static bool j2k_write_tile_part(CodeStream *p_j2k,
		bool writePOC,
		BufferedStream *stream);

/**
 * Ends the encoding, i.e. frees memory.
 *
 * @param       p_j2k                   J2K codec.
 * @param       stream                the stream to write data to.

 */
static bool j2k_end_encoding(CodeStream *p_j2k, BufferedStream *stream);

/**
 * Inits the Info
 *
 * @param       p_j2k                   J2K codec.
 * @param       stream                the stream to write data to.

 */
static bool j2k_init_info(CodeStream *p_j2k, BufferedStream *stream);

/**
 * Reads an unknown marker
 *
 * @param       p_j2k                   JPEG 2000 codec
 * @param       stream                the stream object to read from.
 * @param       output_marker           FIXME DOC

 *
 * @return      true                    if the marker could be deduced.
 */
static bool j2k_read_unk(CodeStream *p_j2k, BufferedStream *stream,
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
static bool j2k_need_nb_tile_parts_correction(CodeStream *p_j2k, BufferedStream *stream,
		uint16_t tile_no, bool *p_correction_needed);
}
