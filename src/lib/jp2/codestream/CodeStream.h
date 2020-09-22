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
#include <map>

namespace grk {

// includes marker and marker length (4 bytes)
const uint32_t sot_marker_segment_len = 12U;
const uint32_t grk_marker_length = 4U;

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
 * Status of decoding process when decoding main header.
 * These values may be combined with the | operator.
 * */
enum J2K_STATUS {
	J2K_DEC_STATE_NONE = 0x0000, 		/**< no decode state */
	J2K_DEC_STATE_MH_SOC = 0x0001, 		/**< a SOC marker is expected */
	J2K_DEC_STATE_MH_SIZ = 0x0002, 		/**< a SIZ marker is expected */
	J2K_DEC_STATE_MH = 0x0004, 			/**< the decoding process is in the main header */
	J2K_DEC_STATE_TPH_SOT = 0x0008, 	/**< the decoding process is in a tile part header
	 	 	 	 	 	 	 	 	 	 	 and expects a SOT marker */
	J2K_DEC_STATE_TPH = 0x0010, 		/**< the decoding process is in a tile part header */
	J2K_DEC_STATE_NO_EOC = 0x0020, 		/**< the decoding process must not expect a EOC marker
	 	 	 	 	 	 	 	 	 	 	 because the code stream is truncated */
	J2K_DEC_STATE_DATA = 0x0040, 		/**< the decoding process is expecting
	 	 	 	 	 	 	 	 	 	 	 to read tile data from the code stream */
	J2K_DEC_STATE_EOC = 0x0080, 		/**< the decoding process has encountered the EOC marker */
	J2K_DEC_STATE_ERR = 0x0100 			/**< the decoding process has encountered an error */
};


/**
 * Allocate data for single image component
 *
 * @param image         image
 */
bool grk_image_single_component_data_alloc(	grk_image_comp *image);

struct TileProcessor;
typedef bool (*j2k_procedure)(CodeStream *codeStream);


struct  grk_dec_memory_marker_handler  {
	/** marker value */
	uint16_t id;
	/** value of the state when the marker can appear */
	uint32_t states;
	/** action linked to the marker */
	bool (*handler)(CodeStream *codeStream, uint8_t *p_header_data, uint16_t header_size);
} ;

struct ICodeStream {

   virtual ~ICodeStream(){}

	/** Main header reading function handler */
   virtual bool read_header(grk_header_info  *header_info, grk_image **p_image) = 0;

	/** Decoding function */
   virtual bool decompress( grk_plugin_tile *tile,	grk_image *p_image) = 0;

	/** decompress tile*/
   virtual bool decompress_tile(grk_image *p_image,	uint16_t tile_index) = 0;

	/** Reading function used after code stream if necessary */
   virtual bool end_decompress(void) = 0;

	/** Setup decoder function handler */
   virtual void init_decompress(grk_dparameters  *p_param) = 0;

	/** Set decompress area function handler */
   virtual bool set_decompress_area(grk_image *p_image,
		   uint32_t start_x, uint32_t end_x, uint32_t start_y,	uint32_t end_y) = 0;

   virtual bool start_compress(void) = 0;

   virtual bool init_compress(grk_cparameters  *p_param,grk_image *p_image) = 0;

   virtual bool compress(grk_plugin_tile* tile) = 0;

   virtual bool compress_tile(uint16_t tile_index,	uint8_t *p_data, uint64_t data_size) = 0;

   virtual bool end_compress(void) = 0;

   virtual void dump(int32_t flag, FILE *out_stream) = 0;

   virtual grk_codestream_info_v2* get_cstr_info(void) = 0;

   virtual grk_codestream_index* get_cstr_index(void) = 0;
};

struct CodeStream : public ICodeStream {

	CodeStream(bool decode, BufferedStream *stream);
	~CodeStream();

	/** Main header reading function handler */
   bool read_header(grk_header_info  *header_info, grk_image **p_image);

	/** Decoding function */
   bool decompress( grk_plugin_tile *tile,grk_image *p_image);

	/** decompress tile*/
   bool decompress_tile(grk_image *p_image,	uint16_t tile_index);

	/** Reading function used after code stream if necessary */
   bool end_decompress(void);

	/** Setup decoder function handler */
   void init_decompress(grk_dparameters  *p_param);

   bool start_compress(void);

   bool init_compress(grk_cparameters  *p_param,grk_image *p_image);

   bool compress(grk_plugin_tile* tile);

   bool compress_tile(uint16_t tile_index,	uint8_t *p_data, uint64_t data_size);

   bool end_compress(void);

   void dump(int32_t flag, FILE *out_stream);

   grk_codestream_info_v2* get_cstr_info(void);

   grk_codestream_index* get_cstr_index();


   bool isDecodingTilePartHeader() ;
	TileCodingParams* get_current_decode_tcp(void);

	bool read_marker(void);
	bool read_marker_skip_unknown(void);
	bool read_short(uint16_t *val);

	bool process_marker(const grk_dec_memory_marker_handler* marker_handler,
						uint16_t current_marker, uint16_t marker_size);

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

	bool parse_markers(bool *can_decode_tile_data);

	bool init_header_writing(void);

	bool read_header_procedure(void);

	bool do_decompress(grk_image *p_image);

	bool decompress_tile_t2t1(TileProcessor *tileProcessor, bool multi_tile) ;

	bool decompress_tile();

	bool decompress_tile_t2(TileProcessor *tileProcessor);

	bool decompress_tiles(void);

	bool decompress_validation(void);

	bool write_tile_part(TileProcessor *tileProcessor);

	bool post_write_tile(TileProcessor *tileProcessor);

	bool get_end_header(void);

	bool copy_default_tcp(void);

	bool update_rates(void);

	bool compress_validation(void);
	/**
	 * Executes the given procedures on the given codec.
	 *
	 * @param       p_procedure_list        the list of procedures to execute
	 * @param       stream                the stream to execute the procedures on.
	 *
	 * @return      true                            if all the procedures were successfully executed.
	 */
	bool exec(std::vector<j2k_procedure> &p_procedure_list);


	/**
	 * Checks for invalid number of tile-parts in SOT marker (TPsot==TNsot). See issue 254.
	 *
	 * @param		codeStream							JPEG 2000 code stream
	 * @param       p_correction_needed output value. 	if true, nonconformant code stream needs TNsot correction.

	 *
	 * @return true if the function was successful, false otherwise.
	 */

	bool need_nb_tile_parts_correction(bool *p_correction_needed);

	bool mct_validation(void);

	bool read_unk(uint16_t *output_marker);



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

	int32_t tileIndexToDecode();

	TileProcessor* allocateProcessor(uint16_t tile_index);
	TileProcessor* currentProcessor(void);

	BufferedStream* getStream();

private:

	/** current TileProcessor **/
	TileProcessor *m_tileProcessor;

	BufferedStream *m_stream;


	std::map<uint32_t, TileProcessor*> m_processors;


	/** index of single tile to decompress;
	 *  !!! initialized to -1 !!! */
	int32_t m_tile_ind_to_dec;


	uint8_t *m_marker_scratch;
	uint16_t m_marker_scratch_size;
    /** Only valid for decoding. Whether the whole tile is decoded, or just the region in win_x0/win_y0/win_x1/win_y1 */

public:
	uint16_t m_curr_marker;
    bool   whole_tile_decoding;
	grk_plugin_tile *current_plugin_tile;
	bool m_nb_tile_parts_correction_checked;
	uint32_t m_nb_tile_parts_correction;

};

/** @name Exported functions */
/*@{*/
/* ----------------------------------------------------------------------- */

/**
 Converts an enum type progression order to string type
 */
char* j2k_convert_progression_order(GRK_PROG_ORDER prg_order);

/* ----------------------------------------------------------------------- */
/*@}*/

/*@}*/

bool j2k_decompress_tile(CodeStream *codeStream, grk_image *p_image, uint16_t tile_index);

bool j2k_init_mct_encoding(TileCodingParams *p_tcp, grk_image *p_image);

}
