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
#include "HTParams.h"

namespace grk {


const uint32_t  GRK_COMP_PARAM_DEFAULT_CBLOCKW  =      64;
const uint32_t  GRK_COMP_PARAM_DEFAULT_CBLOCKH  =      64;
const GRK_PROG_ORDER  GRK_COMP_PARAM_DEFAULT_PROG_ORDER   =  GRK_LRCP;
const uint32_t  GRK_COMP_PARAM_DEFAULT_NUMRESOLUTION =  6;

// limits defined in JPEG 2000 standard
const uint32_t max_precision_jpeg_2000 = 38; // maximum number of magnitude bits, according to ISO standard
const uint32_t max_num_components = 16384;	// maximum allowed number components
const uint32_t max_passes_per_segment = (max_precision_jpeg_2000-1) * 3 +1;
const uint32_t max_num_tiles = 65535;
const uint32_t max_num_tile_parts_per_tile = 256;
const uint32_t max_num_tile_parts = max_num_tiles * max_num_tile_parts_per_tile;

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

/* ----------------------------------------------------------------------- */

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

/* ----------------------------------------------------------------------- */

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
	J2K_DEC_STATE_NEOC = 0x0040, /**< the decoding process must not expect a EOC marker because the codestream is truncated */
	J2K_DEC_STATE_DATA = 0x0080, /**< the decoding process is expecting to read tile data from the code stream */
	J2K_DEC_STATE_EOC = 0x0100, /**< the decoding process has encountered the EOC marker */
	J2K_DEC_STATE_ERR = 0x8000 /**< the decoding process has encountered an error (FIXME warning V1 = 0x0080)*/
};

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

/* ----------------------------------------------------------------------- */

/**
 T2 encoding mode
 */
enum J2K_T2_MODE {
	THRESH_CALC = 0, /** Function called in Rate allocation process*/
	FINAL_PASS = 1 /** Function called in Tier 2 process*/
};


/**
 Tile-component coding parameters
 */
struct grk_tccp {
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
 * FIXME DOC
 */
struct grk_mct_data {
	J2K_MCT_ELEMENT_TYPE m_element_type;
	J2K_MCT_ARRAY_TYPE m_array_type;
	uint32_t m_index;
	uint8_t *m_data;
	uint32_t m_data_size;
};

/**
 * FIXME DOC
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
struct grk_tcp {
	grk_tcp();

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
	grk_tccp *tccps;
	// current tile part number (-1 if not yet initialized
	int16_t m_current_tile_part_number;

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

	/***** FLAGS *******/
	/** If cod == 1 --> there was a COD marker for the present tile */
	uint32_t cod :1;
	/** If ppt == 1 --> there was a PPT marker for the present tile */
	uint32_t ppt :1;
	/** indicates if a POC marker has been used O:NO, 1:YES */
	uint32_t POC :1;

	bool isHT;
	param_qcd qcd;
};

struct grk_encoding_param {
	/** Maximum rate for each component.
	 * If == 0, component size limitation is not considered */
	size_t m_max_comp_size;
	/** Position of tile part flag in progression order*/
	uint32_t m_tp_pos;
	/** Flag determining tile part generation*/
	uint8_t m_tp_flag;
	/** allocation by rate/distortion */
	uint32_t m_disto_alloc :1;
	/** allocation by fixed_quality */
	uint32_t m_fixed_quality :1;
	/** Enabling Tile part generation*/
	uint32_t m_tp_on :1;
	/* rate control algorithm */
	uint32_t rateControlAlgorithm;
};

struct grk_decoding_param {
	/** if != 0, then original dimension divided by 2^(reduce); if == 0 or not used, image is decoded to the full resolution */
	uint32_t m_reduce;
	/** if != 0, then only the first "layer" layers are decoded; if == 0 or not used, all the quality layers are decoded */
	uint32_t m_layer;
};

struct grk_tl_info {
    grk_tl_info() : tile_number(0),
                    has_tile_number(false),
                    length(0){}
    grk_tl_info(uint32_t len) : tile_number(0),
                                has_tile_number(false),
                                length(len){}
    grk_tl_info(uint16_t tileno, uint32_t len) : tile_number(tileno),
                                                has_tile_number(true),
                                                length(len){}
    uint16_t tile_number;
    bool has_tile_number;
    uint32_t length;

};

typedef std::vector<grk_tl_info> TL_INFO_VEC;
typedef std::map<uint8_t, TL_INFO_VEC> TL_MAP;
struct grk_tl_marker {
    TL_MAP tile_part_lengths;
};

struct grk_pl_info {
    grk_pl_info() : length(0){}
    grk_pl_info(uint32_t len) : length(len){}
    uint32_t length;
};
typedef std::vector<grk_pl_info> PL_INFO_VEC;
typedef std::map<uint8_t, PL_INFO_VEC> PL_MAP;
struct grk_pl_marker{
    PL_MAP packet_lengths;
};

/**
 * Coding parameters
 */
struct grk_coding_parameters {
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
	uint32_t tdx;
	/** YTsiz */
	uint32_t tdy;
	/** comments */
	size_t num_comments;
	char *comment[GRK_NUM_COMMENTS_SUPPORTED];
	uint16_t comment_len[GRK_NUM_COMMENTS_SUPPORTED];
	bool isBinaryComment[GRK_NUM_COMMENTS_SUPPORTED];
	/** number of tiles in width */
	uint32_t tw;
	/** number of tiles in height */
	uint32_t th;

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
	grk_tcp *tcps;

	union {
		grk_decoding_param m_dec;
		grk_encoding_param m_enc;
	} m_coding_param;

	/******** FLAGS *********/
	/** if ppm == 1 --> there was a PPM marker*/
	uint32_t ppm :1;
	/** tells if the parameter is a coding or decoding one */
	uint32_t m_is_decoder :1;

	grk_pl_marker *pl_marker;
	grk_tl_marker *tl_marker;

	void destroy();

};

struct grk_j2k_dec {
	/** Decoder state: used to indicate in which part of the codestream
	 *  the decoder is (main header, tile header, end) */
	uint32_t m_state;

	//store decoding parameters common to all tiles (information
	// like COD, COC and RGN in main header)
	grk_tcp *m_default_tcp;
	/** Only tile indices in the correct range will be decoded.*/
	uint32_t m_start_tile_x_index;
	uint32_t m_start_tile_y_index;
	uint32_t m_end_tile_x_index;
	uint32_t m_end_tile_y_index;

	/** Position of the last SOT marker read */
	uint64_t m_last_sot_read_pos;

	/**
	 * Indicate that the current tile-part is assumed to be the last tile part of the codestream.
	 * This is useful in the case when PSot is equal to zero. The sot length will be computed in the
	 * SOD reader function.
	 */
	bool m_last_tile_part;
	// Indicates that a tile's data can be decoded
	uint32_t ready_to_decode_tile_part_data :1;
	uint32_t m_discard_tiles :1;
	uint32_t m_skip_data :1;

};

struct grk_j2k_enc {

	/** Total num of tile parts in whole image = num tiles* num tileparts in each tile*/
	/** used in TLMmarker*/
	uint32_t m_total_tile_parts; /* totnum_tp */

};

struct grk_j2k;
typedef bool (*j2k_procedure)(grk_j2k *j2k, BufferedStream*);

struct TileProcessor;
/**
 JPEG-2000 codestream reader/writer
 */
struct grk_j2k {


	bool decodingTilePartHeader() ;
	grk_tcp* get_current_decode_tcp();


	/* J2K codestream is decoded*/
	bool m_is_decoder;

	/* FIXME DOC*/
	union {
		grk_j2k_dec m_decoder;
		grk_j2k_enc m_encoder;
	} m_specific_param;

	/** pointer to the internal/private encoded / decoded image */
	grk_image *m_private_image;

	/* pointer to the output image (decoded)*/
	grk_image *m_output_image;

	/** Coding parameters */
	grk_coding_parameters m_cp;

	/** the list of procedures to exec **/
	std::vector<j2k_procedure> *m_procedure_list;

	/** the list of validation procedures to follow to make sure the code is valid **/
	std::vector<j2k_procedure> *m_validation_list;

	/** helper used to write the index file */
	 grk_codestream_index  *cstr_index;

	/** the current tile coder/decoder **/
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
void j2k_setup_decoder(void *j2k_void,  grk_dparameters  *parameters);

/**
 * Creates a J2K compression structure
 *
 * @return a handle to a J2K compressor if successful, returns nullptr otherwise
 */
grk_j2k* j2k_create_compress(void);

bool j2k_setup_encoder(grk_j2k *p_j2k,  grk_cparameters  *parameters,
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
 * codestream.
 */
bool j2k_end_decompress(grk_j2k *j2k, BufferedStream *p_stream);

/**
 * Reads a jpeg2000 codestream header structure.
 *
 * @param p_stream the stream to read data from.
 * @param p_j2k the jpeg2000 codec.
 * @param p_image FIXME DOC
 
 *
 * @return true if the box is valid.
 */
bool j2k_read_header(BufferedStream *p_stream, grk_j2k *p_j2k,
		 grk_header_info  *header_info, grk_image **p_image);

/**
 * Destroys a jpeg2000 codec.
 *
 * @param	p_j2k	the jpeg20000 structure to destroy.
 */
void j2k_destroy(grk_j2k *p_j2k);

/**
 * Decode tile data.
 * @param	p_j2k		the jpeg2000 codec.
 * @param	tile_index
 * @param p_data       FIXME DOC
 * @param data_size  FIXME DOC
 * @param	p_stream			the stream to write data to.
 
 */
bool j2k_decode_tile(grk_j2k *p_j2k, uint16_t tile_index, uint8_t *p_data,
		uint64_t data_size, BufferedStream *p_stream);

/**
 * Reads a tile header.
 * @param	p_j2k		the jpeg2000 codec.
 * @param	tile_index FIXME DOC
 * @param	data_size FIXME DOC
 * @param	p_tile_x0 FIXME DOC
 * @param	p_tile_y0 FIXME DOC
 * @param	p_tile_x1 FIXME DOC
 * @param	p_tile_y1 FIXME DOC
 * @param	p_nb_comps FIXME DOC
 * @param	p_go_on FIXME DOC
 * @param	p_stream			the stream to write data to.
 
 */
bool j2k_read_tile_header(grk_j2k *p_j2k, uint16_t *tile_index,
		uint64_t *data_size, uint32_t *p_tile_x0, uint32_t *p_tile_y0,
		uint32_t *p_tile_x1, uint32_t *p_tile_y1, uint32_t *p_nb_comps,
		bool *p_go_on, BufferedStream *p_stream);

/**
 * Sets the given area to be decoded. This function should be called right after grk_read_header and before any tile header reading.
 *
 * @param	p_j2k			the jpeg2000 codec.
 * @param	p_image     FIXME DOC
 * @param	start_x		the left position of the rectangle to decode (in image coordinates).
 * @param	start_y		the up position of the rectangle to decode (in image coordinates).
 * @param	end_x			the right position of the rectangle to decode (in image coordinates).
 * @param	end_y			the bottom position of the rectangle to decode (in image coordinates).
 
 *
 * @return	true			if the area could be set.
 */
bool j2k_set_decode_area(grk_j2k *p_j2k, grk_image *p_image, uint32_t start_x,
		uint32_t start_y, uint32_t end_x, uint32_t end_y);

/**
 * Creates a J2K decompression structure.
 *
 * @return a handle to a J2K decompressor if successful, nullptr otherwise.
 */
grk_j2k* j2k_create_decompress(void);

/**
 * Decode an image from a JPEG-2000 codestream
 * @param j2k J2K decompressor handle
 * @param p_stream  FIXME DOC
 * @param p_image   FIXME DOC

 * @return FIXME DOC
 */
bool j2k_decode(grk_j2k *j2k, grk_plugin_tile *tile, BufferedStream *p_stream,
		grk_image *p_image);

bool j2k_get_tile(grk_j2k *p_j2k, BufferedStream *p_stream, grk_image *p_image, uint16_t tile_index);


/**
 * Writes a tile.
 * @param	p_j2k		the jpeg2000 codec.
 * @param tile_index FIXME DOC
 * @param p_data FIXME DOC
 * @param data_size FIXME DOC
 * @param	p_stream			the stream to write data to.
 
 */
bool j2k_write_tile(grk_j2k *p_j2k, uint16_t tile_index, uint8_t *p_data,
		uint64_t data_size, BufferedStream *p_stream);

/**
 * Encodes an image into a JPEG-2000 codestream
 */
bool j2k_encode(grk_j2k *p_j2k, grk_plugin_tile *tile, BufferedStream *cio);

/**
 * Starts a compression scheme, i.e. validates the codec parameters, writes the header.
 *
 * @param	p_j2k		the jpeg2000 codec.
 * @param	p_stream			the stream object.
 * @param	p_image FIXME DOC
 
 *
 * @return true if the codec is valid.
 */
bool j2k_start_compress(grk_j2k *p_j2k, BufferedStream *p_stream,
		grk_image *p_image);

/**
 * Ends the compression procedures and possibility add data to be read after the
 * codestream.
 */
bool j2k_end_compress(grk_j2k *p_j2k, BufferedStream *cio);

bool j2k_setup_mct_encoding(grk_tcp *p_tcp, grk_image *p_image);

}
