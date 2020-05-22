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
 * Copyright (c) 2006-2007, Parvatha Elangovan
 * Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
 * Copyright (c) 2011-2012, Centre National d'Etudes Spatiales (CNES), France
 * Copyright (c) 2012, CS Systemes d'Information, France
 *
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

#include <vector>
#include <map>
#include "CodingParams.h"

namespace grk {

// includes marker and marker length (4 bytes)
const uint32_t sot_marker_segment_len = 12U;

const uint32_t SPCod_SPCoc_len = 5U;
const uint32_t cod_coc_len = 5U;
const uint32_t tlm_len_per_tile_part = 5;


const uint32_t  GRK_COMP_PARAM_DEFAULT_CBLOCKW  =      64;
const uint32_t  GRK_COMP_PARAM_DEFAULT_CBLOCKH  =      64;
const GRK_PROG_ORDER  GRK_COMP_PARAM_DEFAULT_PROG_ORDER   =  GRK_LRCP;
const uint32_t  GRK_COMP_PARAM_DEFAULT_NUMRESOLUTION =  6;

// limits defined in JPEG 2000 standard
const uint32_t max_precision_jpeg_2000 = 38; // maximum number of magnitude bits, according to ISO standard
const uint32_t max_num_components = 16384;	// maximum allowed number components
const uint32_t max_passes_per_segment = (max_precision_jpeg_2000-1) * 3 +1;
const uint32_t max_num_tiles = 65535;
const uint32_t max_num_tile_parts_per_tile = 255;
const uint32_t max_num_tile_parts = 65535;
// includes tile part header
const uint32_t max_tile_part_size = UINT_MAX;

// limits in Grok library
const uint64_t max_tile_area = 67108864000;
const uint32_t max_supported_precision = 16; // maximum supported precision for Grok library
const uint32_t default_numbers_segments = 10;
const uint32_t default_header_size = 1000;
const uint32_t default_number_mcc_records = 10;
const uint32_t default_number_mct_records = 10;

#define J2K_CP_CSTY_PRT 0x01
#define J2K_CP_CSTY_SOP 0x02
#define J2K_CP_CSTY_EPH 0x04
#define J2K_CCP_CSTY_PRT 0x01
#define J2K_CCP_QNTSTY_NOQNT 0 // no quantization
#define J2K_CCP_QNTSTY_SIQNT 1 // derived quantization
#define J2K_CCP_QNTSTY_SEQNT 2 // expounded quantization

#define GRK_J2K_DEFAULT_CBLK_DATA_SIZE 8192

#define J2K_MS_SOC 0xff4f	/**< SOC marker value */
#define J2K_MS_SOT 0xff90	/**< SOT marker value */
#define J2K_MS_SOD 0xff93	/**< SOD marker value */
#define J2K_MS_EOC 0xffd9	/**< EOC marker value */
#define J2K_MS_CAP 0xff50	/**< CAP marker value */
#define J2K_MS_SIZ 0xff51	/**< SIZ marker value */
#define J2K_MS_COD 0xff52	/**< COD marker value */
#define J2K_MS_COC 0xff53	/**< COC marker value */
#define J2K_MS_RGN 0xff5e	/**< RGN marker value */
#define J2K_MS_QCD 0xff5c	/**< QCD marker value */
#define J2K_MS_QCC 0xff5d	/**< QCC marker value */
#define J2K_MS_POC 0xff5f	/**< POC marker value */
#define J2K_MS_TLM 0xff55	/**< TLM marker value */
#define J2K_MS_PLM 0xff57	/**< PLM marker value */
#define J2K_MS_PLT 0xff58	/**< PLT marker value */
#define J2K_MS_PPM 0xff60	/**< PPM marker value */
#define J2K_MS_PPT 0xff61	/**< PPT marker value */
#define J2K_MS_SOP 0xff91	/**< SOP marker value */
#define J2K_MS_EPH 0xff92	/**< EPH marker value */
#define J2K_MS_CRG 0xff63	/**< CRG marker value */
#define J2K_MS_COM 0xff64	/**< COM marker value */
#define J2K_MS_CBD 0xff78	/**< CBD marker value */
#define J2K_MS_MCC 0xff75	/**< MCC marker value */
#define J2K_MS_MCT 0xff74	/**< MCT marker value */
#define J2K_MS_MCO 0xff77	/**< MCO marker value */

#define J2K_MS_UNK 0		/**< UNKNOWN marker value */

/**
 * Values that specify the status of the decoding process when decoding the main header.
 * These values may be combined with a | operator.
 * */
enum J2K_STATUS {
	J2K_DEC_STATE_NONE = 0x0000, /**< a SOC marker is expected */
	J2K_DEC_STATE_MHSOC = 0x0001, /**< a SOC marker is expected */
	J2K_DEC_STATE_MHSIZ = 0x0002, /**< a SIZ marker is expected */
	J2K_DEC_STATE_MH = 0x0004, /**< the decoding process is in the main header */
	J2K_DEC_STATE_TPHSOT = 0x0008, /**< the decoding process is in a tile part header and expects a SOT marker */
	J2K_DEC_STATE_TPH = 0x0010, /**< the decoding process is in a tile part header */
	J2K_DEC_STATE_MT = 0x0020, /**< the EOC marker has just been read */
	J2K_DEC_STATE_NEOC = 0x0040, /**< the decoding process must not expect a EOC marker because the code stream is truncated */
	J2K_DEC_STATE_DATA = 0x0080, /**< the decoding process is expecting to read tile data from the code stream */
	J2K_DEC_STATE_EOC = 0x0100, /**< the decoding process has encountered the EOC marker */
	J2K_DEC_STATE_ERR = 0x0200 /**< the decoding process has encountered an error */
};

typedef bool (*j2k_procedure)(CodeStream *j2k, BufferedStream*);
struct TileProcessor;

struct CodeStream {

	bool isDecodingTilePartHeader() ;
	TileCodingParams* get_current_decode_tcp();

	// state of decoder/encoder
	union {
		DecoderState m_decoder;
		EncoderState m_encoder;
	} m_specific_param;

	/** internal/private encoded / decoded image */
	grk_image *m_private_image;

	/* output image (for decompress) */
	grk_image *m_output_image;

	/** Coding parameters */
	CodingParams m_cp;

	/** the list of procedures to exec **/
	std::vector<j2k_procedure> *m_procedure_list;

	/** the list of validation procedures to follow to make sure the code is valid **/
	std::vector<j2k_procedure> *m_validation_list;

	/** helper used to write the index file */
	 grk_codestream_index  *cstr_index;

	/** current TileProcessor **/
	TileProcessor *m_tileProcessor;

};

/** @name Exported functions */
/*@{*/
/* ----------------------------------------------------------------------- */

/**
 Setup the decoder decoding parameters using user parameters.
 Decoding parameters are returned in j2k->cp.
 @param j2k J2K decompressor handle
 @param parameters decompression parameters
 */
void j2k_init_decompressor(void *j2k,  grk_dparameters  *parameters);

/**
 * Creates a J2K compression structure
 *
 * @return a handle to a J2K compressor if successful, returns nullptr otherwise
 */
CodeStream* j2k_create_compress(void);

bool j2k_init_compress(CodeStream *p_j2k,  grk_cparameters  *parameters,
		grk_image *image);

/**
 Converts an enum type progression order to string type
 */
char* j2k_convert_progression_order(GRK_PROG_ORDER prg_order);

/* ----------------------------------------------------------------------- */
/*@}*/

/*@}*/

/**
 * Ends the decompression procedures and possibiliy add data to be read after the
 * code stream.
 */
bool j2k_end_decompress(CodeStream *j2k, BufferedStream *stream);

/**
 * Read a JPEG 2000 code stream header.
 *
 * @param stream stream to read data from.
 * @param p_j2k  JPEG 2000 codec.
 * @param header_info  header info struct to store header info
 * @param image  pointer to image
 * @return true if the box is valid.
 */
bool j2k_read_header(BufferedStream *stream, CodeStream *p_j2k,
		 grk_header_info  *header_info, grk_image **image);

/**
 * Destroys a JPEG 2000 codec.
 *
 * @param	p_j2k	the jpeg20000 structure to destroy.
 */
void j2k_destroy(CodeStream *p_j2k);

/**
 * Decode tile data.
 * @param	p_j2k		JPEG 2000 codec
 * @param	tile_index
 * @param p_data       FIXME DOC
 * @param data_size  FIXME DOC
 * @param	stream			the stream to write data to.
 
 */
bool j2k_decompress_tile(CodeStream *p_j2k, uint16_t tile_index, uint8_t *p_data,
		uint64_t data_size, BufferedStream *stream);

/**
 * Reads a tile header.
 * @param	p_j2k		JPEG 2000 codec
 * @param	tile_index 	index of tile
 * @param	data_size FIXME DOC
 * @param	p_tile_x0 tile x0 coordinate
 * @param	p_tile_y0 tile y0 coordinate
 * @param	p_tile_x1 tile x1 coordinate
 * @param	p_tile_y1 tile y1 coordinate
 * @param	p_nb_comps number of componets
 * @param	p_go_on FIXME DOC
 * @param	stream			the stream to write data to.
 
 */
bool j2k_read_tile_header(CodeStream *p_j2k, uint16_t *tile_index,
		uint64_t *data_size, uint32_t *p_tile_x0, uint32_t *p_tile_y0,
		uint32_t *p_tile_x1, uint32_t *p_tile_y1, uint32_t *p_nb_comps,
		bool *p_go_on, BufferedStream *stream);

/**
 * Set the given area to be decoded. This function should be called
 * right after grk_read_header and before any tile header reading.
 *
 * @param	p_j2k		JPEG 2000 codec
 * @param	image     	image
 * @param	start_x		left position of the rectangle to decompress (in image coordinates).
 * @param	start_y		top position of the rectangle to decompress (in image coordinates).
 * @param	end_x		right position of the rectangle to decompress (in image coordinates).
 * @param	end_y		bottom position of the rectangle to decompress (in image coordinates).
 
 *
 * @return	true			if the area could be set.
 */
bool j2k_set_decompress_area(CodeStream *p_j2k, grk_image *image, uint32_t start_x,
		uint32_t start_y, uint32_t end_x, uint32_t end_y);

/**
 * Creates a J2K decompression structure.
 *
 * @return a handle to a J2K decompressor if successful, nullptr otherwise.
 */
CodeStream* j2k_create_decompress(void);

/**
 * Decode an image from a JPEG 2000 code stream
 * @param j2k J2K decompressor handle
 * @param tile    plugin tile
 * @param stream  stream
 * @param image   image
 * @return FIXME DOC
 */
bool j2k_decompress(CodeStream *j2k, grk_plugin_tile *tile, BufferedStream *stream,
		grk_image *image);

bool j2k_get_tile(CodeStream *p_j2k, BufferedStream *stream, grk_image *p_image, uint16_t tile_index);


/**
 * Writes a tile.
 * @param	p_j2k		JPEG 2000 codec
 * @param tile_index FIXME DOC
 * @param uncompressed_data_size FIXME DOC
 * @param data_size FIXME DOC
 * @param	stream			the stream to write data to.
 
 */
bool j2k_compress_tile(CodeStream *p_j2k, uint16_t tile_index, uint8_t *p_data,
		uint64_t uncompressed_data_size, BufferedStream *stream);

/**
 * Encodes an image into a JPEG 2000 code stream
 */
bool j2k_compress(CodeStream *p_j2k, grk_plugin_tile *tile, BufferedStream *stream);

/**
 * Starts a compression scheme, i.e. validates the codec parameters, writes the header.
 * @param	p_j2k		JPEG 2000 codec
 * @param	stream			the stream object.
 * @return true if the codec is valid.
 */
bool j2k_start_compress(CodeStream *p_j2k, BufferedStream *stream);

/**
 * Ends the compression procedures and possibility add data to be read after the
 * code stream.
 */
bool j2k_end_compress(CodeStream *p_j2k, BufferedStream *stream);

bool j2k_init_mct_encoding(TileCodingParams *p_tcp, grk_image *p_image);

}
