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
	bool writePlt;
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
	/** specifies if the parameter is a coding or decoding one */
	bool m_is_decoder;

	TileLengthMarkers *tlm_markers;
	PacketLengthMarkers *plm_markers;

	void destroy();

};

struct DecoderState {
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
	uint32_t ready_to_decode_tile_part_data :1;
	uint32_t m_discard_tiles :1;
	uint32_t m_skip_data :1;

};

struct EncoderState {

	/** Total num of tile parts in whole image = num tiles* num tileparts in each tile*/
	/** used in TLMmarker*/
	uint32_t m_total_tile_parts; /* totnum_tp */

};

}
