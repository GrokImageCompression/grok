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

#include "HTParams.h"

namespace grk {

/**
 * Type of elements storing in the MCT data
 */
enum J2K_MCT_ELEMENT_TYPE {
	MCT_TYPE_INT16 = 0, /** MCT data is stored as signed shorts*/
	MCT_TYPE_INT32 = 1, /** MCT data is stored as signed integers*/
	MCT_TYPE_FLOAT = 2, /** MCT data is stored as floats*/
	MCT_TYPE_DOUBLE = 3 /** MCT data is stored as doubles*/
};

/**
 * Type of MCT array
 */
enum J2K_MCT_ARRAY_TYPE {
	MCT_TYPE_DEPENDENCY = 0, MCT_TYPE_DECORRELATION = 1, MCT_TYPE_OFFSET = 2
};


/**
 Tile-component coding parameters
 */
struct TileComponentCodingParams {
	/** coding style */
	uint8_t csty;
	/** number of resolutions */
	uint32_t numresolutions;
	/** log2(code-blocks width) */
	uint32_t cblkw;
	/** log2(code-blocks height) */
	uint32_t cblkh;

	Quantizer quant;

	/** code-block mode */
	uint8_t cblk_sty;
	/** discrete wavelet transform identifier */
	uint8_t qmfbid;
	// true if quantization marker was read from QCC otherwise false
	bool fromQCC;
	// true if quantization marker was read from tile header
	bool fromTileHeader;
	/** quantisation style */
	uint8_t qntsty;
	/** stepsizes used for quantization */
	grk_stepsize stepsizes[GRK_J2K_MAXBANDS];
	// number of step sizes read from QCC marker
	uint8_t numStepSizes;
	/** number of guard bits */
	uint8_t numgbits;
	/** Region Of Interest shift */
	uint32_t roishift;
	/** precinct width (power of 2 exponent) */
	uint32_t prcw[GRK_J2K_MAXRLVLS];
	/** precinct height (power of 2 exponent) */
	uint32_t prch[GRK_J2K_MAXRLVLS];
	/** the dc_level_shift **/
	int32_t m_dc_level_shift;
};



/**
 * MCT data
 */
struct grk_mct_data {
	J2K_MCT_ELEMENT_TYPE m_element_type;
	J2K_MCT_ARRAY_TYPE m_array_type;
	uint32_t m_index;
	uint8_t *m_data;
	uint32_t m_data_size;
};

/**
 * MCC decorrelation data
 */
struct grk_simple_mcc_decorrelation_data {
	uint32_t m_index;
	uint32_t m_nb_comps;
	grk_mct_data *m_decorrelation_array;
	grk_mct_data *m_offset_array;
	uint32_t m_is_irreversible :1;
}
;

struct grk_ppx {
	uint8_t *m_data; /* m_data == nullptr => Zppx not read yet */
	uint32_t m_data_size;
};

/**
 Tile coding parameters :
 this structure is used to store coding/decoding parameters common to all
 tiles (information like COD, COC in main header)
 */
struct TileCodingParams {
	TileCodingParams();
	~TileCodingParams();

	void destroy();

	/** coding style */
	uint32_t csty;
	/** progression order */
	GRK_PROG_ORDER prg;
	/** number of layers */
	uint32_t numlayers;
	uint32_t num_layers_to_decode;
	/** multi-component transform identifier */
	uint32_t mct;
	/** rates of layers */
	double rates[100];
	/** number of progression order changes */
	uint32_t numpocs;
	/** progression order changes */
	 grk_poc  pocs[32];

	/** number of ppt markers (reserved size) */
	uint32_t ppt_markers_count;
	/** ppt markers data (table indexed by Zppt) */
	grk_ppx *ppt_markers;

	/** packet header store there for future use in t2_decode_packet */
	uint8_t *ppt_data;
	/** used to keep a track of the allocated memory */
	uint8_t *ppt_buffer;
	/** Number of bytes stored inside ppt_data*/
	size_t ppt_data_size;
	/** size of ppt_data*/
	size_t ppt_len;
	/** fixed_quality */
	double distoratio[100];
	// quantization style as read from main QCD marker
	uint32_t main_qcd_qntsty;
	// number of step sizes as read from main QCD marker
	uint32_t main_qcd_numStepSizes;
	/** tile-component coding parameters */
	TileComponentCodingParams *tccps;
	// current tile part number (-1 if not yet initialized
	int16_t m_current_tile_part_index;

	/** number of tile parts for the tile. */
	uint8_t m_nb_tile_parts;

	ChunkBuffer *m_tile_data;

	/** encoding norms */
	double *mct_norms;
	/** the mct decoding matrix */
	float *m_mct_decoding_matrix;
	/** the mct coding matrix */
	float *m_mct_coding_matrix;
	/** mct records */
	grk_mct_data *m_mct_records;
	/** the number of mct records. */
	uint32_t m_nb_mct_records;
	/** the max number of mct records. */
	uint32_t m_nb_max_mct_records;
	/** mcc records */
	grk_simple_mcc_decorrelation_data *m_mcc_records;
	/** the number of mct records. */
	uint32_t m_nb_mcc_records;
	/** the max number of mct records. */
	uint32_t m_nb_max_mcc_records;

	/** If cod == true --> there was a COD marker for the present tile */
	bool cod;
	/** If ppt == true --> there was a PPT marker for the present tile */
	bool ppt;
	/** indicates if a POC marker has been used*/
	bool POC;

	bool isHT;
	param_qcd qcd;
};

struct EncodingParams {
	/** Maximum rate for each component.
	 * If == 0, component size limitation is not considered */
	size_t m_max_comp_size;
	/** Position of tile part flag in progression order*/
	uint32_t m_tp_pos;
	/** Flag determining tile part generation*/
	uint8_t m_tp_flag;
	/** allocation by rate/distortion */
	bool m_disto_alloc;
	/** allocation by fixed_quality */
	bool m_fixed_quality;
	/** Enabling Tile part generation*/
	bool m_tp_on;
	/* write plt marker */
	bool writePLT;

	bool writeTLM;
	/* rate control algorithm */
	uint32_t rateControlAlgorithm;
};

struct DecodingParams {
	/** if != 0, then original dimension divided by 2^(reduce); if == 0 or not used, image is decoded to the full resolution */
	uint32_t m_reduce;
	/** if != 0, then only the first "layer" layers are decoded; if == 0 or not used, all the quality layers are decoded */
	uint32_t m_layer;
};

/**
 * Coding parameters
 */
struct CodingParams {
	/** Rsiz*/
	uint16_t rsiz;
	/* Pcap */
	uint32_t pcap;
	/* Ccap */
	uint16_t ccap;
	/** XTOsiz */
	uint32_t tx0;
	/** YTOsiz */
	uint32_t ty0;
	/** XTsiz */
	uint32_t t_width;
	/** YTsiz */
	uint32_t t_height;
	/** comments */
	size_t num_comments;
	char *comment[GRK_NUM_COMMENTS_SUPPORTED];
	uint16_t comment_len[GRK_NUM_COMMENTS_SUPPORTED];
	bool isBinaryComment[GRK_NUM_COMMENTS_SUPPORTED];
	/** number of tiles in width */
	uint32_t t_grid_width;
	/** number of tiles in height */
	uint32_t t_grid_height;

	/** number of ppm markers (reserved size) */
	uint32_t ppm_markers_count;
	/** ppm markers data (table indexed by Zppm) */
	grk_ppx *ppm_markers;

	/** packet header store there for future use in t2_decode_packet */
	uint8_t *ppm_data;
	/** size of the ppm_data*/
	size_t ppm_len;
	/** size of the ppm_data*/
	size_t ppm_data_read;

	uint8_t *ppm_data_current;

	/** packet header storage original buffer */
	uint8_t *ppm_buffer;
	/** pointer remaining on the first byte of the first header if ppm is used */
	uint8_t *ppm_data_first;
	/** Number of bytes actually stored inside the ppm_data */
	size_t ppm_data_size;
	/** use in case of multiple marker PPM (number of info already store) */
	int32_t ppm_store;
	/** use in case of multiple marker PPM (case on non-finished previous info) */
	int32_t ppm_previous;

	/** tile coding parameters */
	TileCodingParams *tcps;

	union {
		DecodingParams m_dec;
		EncodingParams m_enc;
	} m_coding_params;

	/** if ppm is true --> there was a PPM marker*/
	bool ppm;

	TileLengthMarkers *tlm_markers;
	PacketLengthMarkers *plm_markers;

	void destroy();

};

struct DecoderState {
	DecoderState() : m_state(0),
					m_default_tcp(nullptr),
					m_start_tile_x_index(0),
					m_start_tile_y_index(0),
					m_end_tile_x_index(0),
					m_end_tile_y_index(0),
					m_last_sot_read_pos(0),
					m_last_tile_part(false),
					ready_to_decode_tile_part_data(false),
					m_discard_tiles(false),
					m_skip_data(false)
	{}


	bool findNextTile(BufferedStream *stream);

	/** Decoder state: used to indicate in which part of the code stream
	 *  the decoder is (main header, tile header, end) */
	uint32_t m_state;

	//store decoding parameters common to all tiles (information
	// like COD, COC and RGN in main header)
	TileCodingParams *m_default_tcp;
	/** Only tile indices in the correct range will be decoded.*/
	uint32_t m_start_tile_x_index;
	uint32_t m_start_tile_y_index;
	uint32_t m_end_tile_x_index;
	uint32_t m_end_tile_y_index;

	/** Position of the last SOT marker read */
	uint64_t m_last_sot_read_pos;

	/**
	 * Indicate that the current tile-part is assumed to be the last tile part of the code stream.
	 * This is useful in the case when PSot is equal to zero. The sot length will be computed in the
	 * SOD reader function.
	 */
	bool m_last_tile_part;
	// Indicates that a tile's data can be decoded
	bool ready_to_decode_tile_part_data;
	bool m_discard_tiles;
	bool m_skip_data;

};

struct EncoderState {

	EncoderState() : m_total_tile_parts(0) {}

	/** Total num of tile parts in whole image = num tiles* num tileparts in each tile*/
	/** used in TLMmarker*/
	uint16_t m_total_tile_parts; /* totnum_tp */

};

}
