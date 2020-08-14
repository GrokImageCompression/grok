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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
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
	J2K_DEC_STATE_NONE = 0x0000, /**< no decode state */
	J2K_DEC_STATE_MH_SOC = 0x0001, /**< a SOC marker is expected */
	J2K_DEC_STATE_MH_SIZ = 0x0002, /**< a SIZ marker is expected */
	J2K_DEC_STATE_MH = 0x0004, /**< the decoding process is in the main header */
	J2K_DEC_STATE_TPH_SOT = 0x0008, /**< the decoding process is in a tile part header and expects a SOT marker */
	J2K_DEC_STATE_TPH = 0x0010, /**< the decoding process is in a tile part header */
	J2K_DEC_STATE_NO_EOC = 0x0020, /**< the decoding process must not expect a EOC marker because the code stream is truncated */
	J2K_DEC_STATE_DATA = 0x0040, /**< the decoding process is expecting to read tile data from the code stream */
	J2K_DEC_STATE_EOC = 0x0080, /**< the decoding process has encountered the EOC marker */
	J2K_DEC_STATE_ERR = 0x0100 /**< the decoding process has encountered an error */
};

struct TileProcessor;
typedef bool (*j2k_procedure)(CodeStream *codeStream, TileProcessor *tileProcessor, BufferedStream *stream);


struct  grk_dec_memory_marker_handler  {
	/** marker value */
	uint16_t id;
	/** value of the state when the marker can appear */
	uint32_t states;
	/** action linked to the marker */
	bool (*handler)(CodeStream *codeStream, TileProcessor *tileProcessor,
			uint8_t *p_header_data, uint16_t header_size);
} ;

struct CodeStream {

	CodeStream(bool decode);
	~CodeStream();

	bool isDecodingTilePartHeader() ;
	TileCodingParams* get_current_decode_tcp(TileProcessor *tileProcessor);

	bool read_marker(BufferedStream *stream, uint16_t *val);

	bool process_marker(const grk_dec_memory_marker_handler* marker_handler,
						uint16_t current_marker, uint16_t marker_size,
						TileProcessor *tileProcessor,	BufferedStream *stream);

	/**
	 * Sets the given area to be decoded. This function should be called right after grk_read_header
	 * and before any tile header reading.
	 *
	 * @param	p_image     image
	 * @param	start_x		the left position of the rectangle to decompress (in image coordinates).
	 * @param	start_y		the up position of the rectangle to decompress (in image coordinates).
	 * @param	end_x		the right position of the rectangle to decompress (in image coordinates).
	 * @param	end_y		the bottom position of the rectangle to decompress (in image coordinates).

	 *
	 * @return	true			if the area could be set.
	 */
	bool set_decompress_area(grk_image *p_image,
						uint32_t start_x,
						uint32_t start_y,
						uint32_t end_x,
						uint32_t end_y);

	/**
	 * Allocate output buffer for multiple tile decode
	 *
	 * @param p_output_image output image
	 *
	 * @return true if successful
	 */
	bool alloc_multi_tile_output_data(grk_image *p_output_image);


	// state of decoder/encoder
	DecoderState m_decoder;
	EncoderState m_encoder;

	/** internal/private encoded / decoded image */
	grk_image *m_input_image;

	/* output image (for decompress) */
	grk_image *m_output_image;

	/** Coding parameters */
	CodingParams m_cp;

	/** the list of procedures to exec **/
	std::vector<j2k_procedure> m_procedure_list;

	/** the list of validation procedures to follow to make sure the code is valid **/
	std::vector<j2k_procedure> m_validation_list;

	/** helper used to write the index file */
	 grk_codestream_index  *cstr_index;

	/** current TileProcessor **/
	TileProcessor *m_tileProcessor;


	/** index of the tile to decompress (used in get_tile);
	 *  !!! initialized to -1 !!! */
	int32_t m_tile_ind_to_dec;

private:

	uint8_t *m_marker_scratch;
	uint16_t m_marker_scratch_size;
    /** Only valid for decoding. Whether the whole tile is decoded, or just the region in win_x0/win_y0/win_x1/win_y1 */

public:
    bool   whole_tile_decoding;
	grk_plugin_tile *current_plugin_tile;
	/** TNsot correction : see issue 254 **/
	bool m_nb_tile_parts_correction_checked;
	bool m_nb_tile_parts_correction;

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

bool j2k_init_compress(CodeStream *codeStream,  grk_cparameters  *parameters,
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
 * @param codeStream  JPEG 2000 code stream.
 * @param header_info  header info struct to store header info
 * @param image  pointer to image
 * @return true if the box is valid.
 */
bool j2k_read_header(BufferedStream *stream, CodeStream *codeStream,
		 grk_header_info  *header_info, grk_image **image);

/**
 * Destroys a JPEG 2000 code stream.
 *
 * @param	codeStream	the jpeg20000 structure to destroy.
 */
void j2k_destroy(CodeStream *codeStream);


/**
 * Reads a tile header.
 * @param	codeStream		JPEG 2000 code stream
 * @param   tileProcessor 	tile processor
 * @param	can_decode_tile_data 		set to true if tile data is ready to be decoded
 * @param	stream			buffered stream.
 *
  * @return	true			if tile header could be read
 */
bool j2k_read_tile_header(CodeStream *codeStream, TileProcessor *tileProcessor,
		bool *can_decode_tile_data, BufferedStream *stream);

/**
 * Set the given area to be decoded. This function should be called
 * right after grk_read_header and before any tile header reading.
 *
 * @param	codeStream		JPEG 2000 code stream
 * @param	image     	image
 * @param	start_x		left position of the rectangle to decompress (in image coordinates).
 * @param	start_y		top position of the rectangle to decompress (in image coordinates).
 * @param	end_x		right position of the rectangle to decompress (in image coordinates).
 * @param	end_y		bottom position of the rectangle to decompress (in image coordinates).
 *
 * @return	true			if the area could be set.
 */
bool j2k_set_decompress_area(CodeStream *codeStream, grk_image *image, uint32_t start_x,
		uint32_t start_y, uint32_t end_x, uint32_t end_y);

/**
 * Decode an image from a JPEG 2000 code stream
 * @param codeStream    code stream
 * @param tile    		plugin tile
 * @param stream  		stream
 * @param image   		image
 *
 * @return true if decompression is successful
 */
bool j2k_decompress(CodeStream *codeStream, grk_plugin_tile *tile, BufferedStream *stream,
		grk_image *image);

bool j2k_decompress_tile(CodeStream *codeStream, BufferedStream *stream, grk_image *p_image, uint16_t tile_index);


/**
 * Writes a tile.
 * @param	codeStream				JPEG 2000 code stream
 * @param	tileProcessor			tile processor
 * @param tile_index 				tile index
 * @param data						uncompressed data
 * @param uncompressed_data_size 	uncompressed data size
 * @param	stream					buffered stream.
 
 */
bool j2k_compress_tile(CodeStream *codeStream, TileProcessor *tileProcessor,
		uint16_t tile_index, uint8_t *p_data,
		uint64_t uncompressed_data_size, BufferedStream *stream);

/**
 * Encodes an image into a JPEG 2000 code stream
 */
bool j2k_compress(CodeStream *codeStream, grk_plugin_tile *tile, BufferedStream *stream);

/**
 * Starts a compression scheme, i.e. validates the codec parameters, writes the header.
 * @param	codeStream		JPEG 2000 code stream
 * @param	stream			the stream object.
 * @return true if the codec is valid.
 */
bool j2k_start_compress(CodeStream *codeStream, BufferedStream *stream);

/**
 * Ends the compression procedures and possibility add data to be read after the
 * code stream.
 */
bool j2k_end_compress(CodeStream *codeStream, BufferedStream *stream);

bool j2k_init_mct_encoding(TileCodingParams *p_tcp, grk_image *p_image);

}
