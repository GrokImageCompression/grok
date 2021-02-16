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
const uint32_t max_supported_precision = 16; // maximum supported precision for Grok library
const uint32_t default_numbers_segments = 10;
const uint32_t default_header_size = 4096;
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

struct TileProcessor;
class GrkImage;

typedef std::function<bool(void)>  PROCEDURE_FUNC;
typedef std::function<bool(uint8_t *p_header_data, uint16_t header_size)>  MARKER_FUNC;

struct  marker_handler  {
	marker_handler(uint16_t ID, uint32_t flags, MARKER_FUNC f) :
		id(ID), states(flags), func(f)
	{}
	/** marker value */
	uint16_t id;
	/** value of the state when the marker can appear */
	uint32_t states;
	MARKER_FUNC func;
} ;


struct ICodeStreamCompress {
   virtual ~ICodeStreamCompress(){}
   virtual bool initCompress(grk_cparameters  *p_param,GrkImage *p_image) = 0;
   virtual bool start_compress(void) = 0;
   virtual bool compress(grk_plugin_tile* tile) = 0;
   virtual bool compressTile(uint16_t tile_index,	uint8_t *p_data, uint64_t data_size) = 0;
   virtual bool endCompress(void) = 0;
};

class ICodeStreamDecompress {
public:
   virtual ~ICodeStreamDecompress(){}
   virtual bool readHeader(grk_header_info  *header_info) = 0;
   virtual GrkImage* getImage(uint16_t tileIndex) = 0;
   virtual GrkImage* getImage(void) = 0;
   virtual void initDecompress(grk_dparameters  *p_param) = 0;
   virtual bool setDecompressWindow(grk_rect_u32 window) = 0;
   virtual bool decompress( grk_plugin_tile *tile) = 0;
   virtual bool decompressTile(uint16_t tile_index) = 0;
   virtual bool endDecompress(void) = 0;
};

class ICodeStream : public ICodeStreamCompress, public ICodeStreamDecompress {
public:
   virtual ~ICodeStream(){}
   virtual void dump(uint32_t flag, FILE *out_stream) = 0;
};

class TileCache;

class CodeStream : public ICodeStream {
public:
	CodeStream(bool decompress, BufferedStream *stream);
	virtual ~CodeStream();

	/** Main header reading function handler */
   bool readHeader(grk_header_info  *header_info);

   GrkImage* getImage(uint16_t tileIndex);
   GrkImage* getImage(void);
   std::vector<GrkImage*> getAllImages(void);

   bool decompress( grk_plugin_tile *tile);
   bool decompressTile(uint16_t tile_index);
   bool endDecompress(void);
   void initDecompress(grk_dparameters  *p_param);

   bool start_compress(void);
   bool initCompress(grk_cparameters  *p_param,GrkImage *p_image);
   bool compress(grk_plugin_tile* tile);
   bool compressTile(uint16_t tile_index,	uint8_t *p_data, uint64_t data_size);
   bool endCompress(void);

   void dump(uint32_t flag, FILE *out_stream);

   bool isDecodingTilePartHeader() ;
	TileCodingParams* get_current_decode_tcp(void);

	bool read_marker(void);
	bool read_short(uint16_t *val);
	bool process_marker(const marker_handler* marker_handler, uint16_t marker_size);

	/**
	 * Sets the given area to be decompressed. This function should be called right after grk_decompress_read_header
	 * and before any tile header reading.
	 *
	 * @param	window		decompress window
	 *
	 * @return	true			if the area could be set.
	 */
	bool setDecompressWindow(grk_rect_u32 window);
	bool parse_tile_header_markers(bool *can_decode_tile_data);
	bool init_header_writing(void);
	bool readHeaderProcedure(void);
	bool exec_decompress();
	bool decompress_tile_t2t1(TileProcessor *tileProcessor) ;
	bool decompressTile();
	bool findNextTile(TileProcessor *tileProcessor);
	bool decompressTiles(void);
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
	 *
	 * @return      true                            if all the procedures were successfully executed.
	 */
	bool exec(std::vector<PROCEDURE_FUNC> &p_procedure_list);
	/**
	 * Checks for invalid number of tile-parts in SOT marker (TPsot==TNsot). See issue 254.
	 *
	 * @param       p_correction_needed output value. 	if true, nonconformant code stream needs TNsot correction.

	 *
	 * @return true if the function was successful, false otherwise.
	 */

	bool need_nb_tile_parts_correction(bool *p_correction_needed);
	bool mct_validation(void);

	/**
	 * Reads an unknown marker
	 *
	 * @param       output_marker         marker value
	 * @return      true                  if the marker could be read
	 */
	bool read_unk(uint16_t *output_marker);
	GrkImage* getCompositeImage();
	GrkImage* getHeaderImage(void);

	// state of decompressor/compressor
	DecoderState m_decompressor;
	EncoderState m_encoder;

	/** Coding parameters */
	CodingParams m_cp;

	/** helper used to write the index file */
	 grk_codestream_index  *cstr_index;

	int32_t tileIndexToDecode();

	TileProcessor* allocateProcessor(uint16_t tile_index);
	TileProcessor* currentProcessor(void);
	BufferedStream* getStream();
	uint16_t getCurrentMarker();
    bool   isWholeTileDecompress();
    grk_plugin_tile* getCurrentPluginTile();


    /**
     Add main header marker information
     @param cstr_index    Codestream information structure
     @param id           marker id
     @param pos          byte offset of marker segment
     @param len          length of marker segment
     */
    bool add_mhmarker( grk_codestream_index  *cstr_index,
    						uint16_t id,
    						uint64_t pos,
    						uint32_t len);

    /**
     * Writes the SOC marker (Start Of Codestream)
     *
     * @param       codeStream          JPEG 2000 code stream
     */
    bool write_soc();

    /**
     * Reads a SOC marker (Start of Codestream)
     * @param       codeStream           JPEG 2000 code stream.
     */
    bool read_soc();

    /**
     * Writes the SIZ marker (image and tile size)
     *
     * @param       codeStream          JPEG 2000 code stream
     */
    bool write_siz();

    /**
     * Reads a SIZ marker (image and tile size)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     */
    bool read_siz( uint8_t *p_header_data,
    		uint16_t header_size);

    /**
     * Reads a CAP marker
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     */
    bool read_cap( uint8_t *p_header_data,
    		uint16_t header_size);

    /**
     * Writes the CAP marker
     *
     * @param       codeStream          JPEG 2000 code stream
     */
    bool write_cap();

    /**
     * Writes the COM marker (comment)
     *
     * @param       codeStream      JPEG 2000 code stream
     */
    bool write_com();

    /**
     * Reads a COM marker (comments)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     *
     */
    bool read_com( uint8_t *p_header_data,
    		uint16_t header_size);
    /**
     * Writes the COD marker (Coding style default)
     *
     * @param       codeStream          JPEG 2000 code stream
     */
    bool write_cod();

    /**
     * Reads a COD marker (Coding Style defaults)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     *
     */
    bool read_cod( uint8_t *p_header_data,uint16_t header_size);

    /**
     * Compares 2 COC markers (Coding style component)
     *
     * @param       codeStream            JPEG 2000 code stream
     * @param       first_comp_no  the index of the first component to compare.
     * @param       second_comp_no the index of the second component to compare.
     *
     * @return      true if equals
     */
    bool compare_coc(  uint32_t first_comp_no,
    		uint32_t second_comp_no);

    /**
     * Writes the COC marker (Coding style component)
     *
     * @param       codeStream  JPEG 2000 code stream
     * @param       comp_no   the index of the component to output.
     * @param       stream    buffered stream.

     */
    bool write_coc( uint32_t comp_no,
    		BufferedStream *stream);

    bool write_coc(uint32_t comp_no);

    /**
     * Reads a COC marker (Coding Style Component)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     *
     */
    bool read_coc( uint8_t *p_header_data,
    		uint16_t header_size);

    /**
     * Writes the QCD marker (quantization default)
     *
     * @param       codeStream          JPEG 2000 code stream
     */
    bool write_qcd();

    /**
     * Reads a QCD marker (Quantization defaults)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     */
    bool read_qcd( uint8_t *p_header_data,
    		uint16_t header_size);

    /**
     * Compare QCC markers (quantization component)
     *
     * @param       codeStream                 JPEG 2000 code stream
     * @param       first_comp_no       the index of the first component to compare.
     * @param       second_comp_no      the index of the second component to compare.
     *
     * @return true if equals.
     */
    bool compare_qcc( uint32_t first_comp_no,
    		uint32_t second_comp_no);

    /**
     * Writes the QCC marker (quantization component)
     *
     * @param       codeStream  JPEG 2000 code stream
     * @param 		tile_index 	current tile index
     * @param       comp_no     the index of the component to output.
     * @param       stream      buffered stream.

     */
    bool write_qcc( uint16_t tile_index, uint32_t comp_no,
    		BufferedStream *stream);

    bool write_qcc(uint32_t comp_no);
    /**
     * Reads a QCC marker (Quantization component)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data

     */
    bool read_qcc( uint8_t *p_header_data,
    		uint16_t header_size);

    uint16_t getPocSize(uint32_t l_nb_comp, uint32_t l_nb_poc);

    /**
     * Writes the POC marker (Progression Order Change)
     *
     * @param       codeStream          JPEG 2000 code stream
     */
    bool write_poc();


    /**
     * Reads a POC marker (Progression Order Change)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     */
    bool read_poc( uint8_t *p_header_data,
    		uint16_t header_size);

    /**
     * Reads a CRG marker (Component registration)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     *
     */
    bool read_crg( uint8_t *p_header_data,
    		uint16_t header_size);
    /**
     * Reads a TLM marker (Tile Length Marker)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     */
    bool read_tlm( uint8_t *p_header_data,
    		uint16_t header_size);

    /**
     * End writing the updated tlm.
     *
     * @param       codeStream          JPEG 2000 code stream
     */
    bool write_tlm_end();


    /**
     * Begin writing the TLM marker (Tile Length Marker)
     *
     * @param       codeStream          JPEG 2000 code stream
     */
    bool write_tlm_begin();


    /**
     * Reads a PLM marker (Packet length, main header marker)
     *
     * @param       codeStream      JPEG 2000 code stream
      * @param       p_header_data   header data
     * @param       header_size     size of header data
     *
     */
    bool read_plm( uint8_t *p_header_data,
    		uint16_t header_size);
    /**
     * Reads a PLT marker (Packet length, tile-part header)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     *
     */
    bool read_plt( uint8_t *p_header_data,
    		uint16_t header_size);

    /**
     * Reads a PPM marker (Packed headers, main header)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     *
     */

    bool read_ppm( uint8_t *p_header_data,
    		uint16_t header_size);

    /**
     * Merges all PPM markers read (Packed headers, main header)
     *
     * @param       p_cp      main coding parameters.

     */
    bool merge_ppm(CodingParams *p_cp);

    /**
     * Reads a PPT marker (Packed packet headers, tile-part header)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     *
     */
    bool read_ppt( uint8_t *p_header_data,
    		uint16_t header_size);

    /**
     * Merges all PPT markers read (Packed headers, tile-part header)
     *
     * @param       p_tcp   the tile.

     */
    bool merge_ppt(TileCodingParams *p_tcp);

    /**
     * Read SOT (Start of tile part) marker
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data.
     * @param       header_size     size of header data
     *
     */
    bool read_sot(	uint8_t *p_header_data, uint16_t header_size);


    /**
     * Compare 2 a SPCod/ SPCoc elements, i.e. the coding style of a given component of a tile.
     *
     * @param       codeStream            JPEG 2000 code stream
     * @param       first_comp_no  The 1st component number to compare.
     * @param       second_comp_no The 1st component number to compare.
     *
     * @return true if SPCdod are equals.
     */
    bool compare_SPCod_SPCoc(
    		uint32_t first_comp_no, uint32_t second_comp_no);

    /**
     * Writes a SPCod or SPCoc element, i.e. the coding style of a given component of a tile.
     *
     * @param       codeStream           JPEG 2000 code stream
     * @param       comp_no       the component number to output.
     *
     * @return true if successful
     */
    bool write_SPCod_SPCoc( 	uint32_t comp_no);

    /**
     * Gets the size taken by writing a SPCod or SPCoc for the given tile and component.
     *
     * @param       codeStream                   the JPEG 2000 code stream
     * @param       comp_no               the component being outputted.
     *
     * @return      the number of bytes taken by the SPCod element.
     */
    uint32_t get_SPCod_SPCoc_size( uint32_t comp_no);

    /**
     * Reads a SPCod or SPCoc element, i.e. the coding style of a given component of a tile.
     * @param       codeStream           JPEG 2000 code stream
     * @param       compno          component number
     * @param       p_header_data   the data contained in the COM box.
     * @param       header_size   the size of the data contained in the COM marker.

     */
    bool read_SPCod_SPCoc(
    		uint32_t compno, uint8_t *p_header_data, uint16_t *header_size);

    /**
     * Gets the size taken by writing SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
     *
     * @param       codeStream                   the JPEG 2000 code stream
     * @param       comp_no               the component being output.
     *
     * @return      the number of bytes taken by the SPCod element.
     */
    uint32_t get_SQcd_SQcc_size(	uint32_t comp_no);

    /**
     * Compares 2 SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
     *
     * @param       codeStream                   JPEG 2000 code stream
     * @param       first_comp_no         the first component number to compare.
     * @param       second_comp_no        the second component number to compare.
     *
     * @return true if equals.
     */
    bool compare_SQcd_SQcc(
    		uint32_t first_comp_no, uint32_t second_comp_no);

    /**
     * Writes a SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
     *
     * @param       codeStream                   JPEG 2000 code stream
     * @param       comp_no               the component number to output.
     *
     */
    bool write_SQcd_SQcc(uint32_t comp_no);

    /**
     * Reads a SQcd or SQcc element, i.e. the quantization values of a band
     * in the QCD or QCC.
     *
     * @param       codeStream           JPEG 2000 code stream
     * @param		fromQCC			true if reading QCC, otherwise false (reading QCD)
     * @param       compno          the component number to output.
     * @param       p_header_data   the data buffer.
     * @param       header_size   pointer to the size of the data buffer,
     *              it is changed by the function.
     *
     */
    bool read_SQcd_SQcc( bool fromQCC,uint32_t compno,
    		uint8_t *p_header_data, uint16_t *header_size);

    /**
     * Writes the MCT marker (Multiple Component Transform)
     *
     * @param       p_mct_record  MCT record
     * @param       stream        buffered stream.

     */
    bool write_mct_record(grk_mct_data *p_mct_record, BufferedStream *stream);

    /**
     * Reads a MCT marker (Multiple Component Transform)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     *
     */
    bool read_mct( uint8_t *p_header_data,
    		uint16_t header_size);

    /**
     * Writes the MCC marker (Multiple Component Collection)
     *
     * @param       p_mcc_record          MCC record
     * @param       stream                buffered stream.

     */
    bool write_mcc_record(grk_simple_mcc_decorrelation_data *p_mcc_record,
    		BufferedStream *stream);

    /**
     * Reads a MCC marker (Multiple Component Collection)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     *
     */
    bool read_mcc( uint8_t *p_header_data,
    		uint16_t header_size);

    /**
     * Writes the MCO marker (Multiple component transformation ordering)
     *
     * @param       codeStream      JPEG 2000 code stream
     */
    bool write_mco();

    /**
     * Reads a MCO marker (Multiple Component Transform Ordering)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data.
     * @param       header_size     size of header data
     *
     */
    bool read_mco( uint8_t *p_header_data,
    		uint16_t header_size);

    bool add_mct(TileCodingParams *p_tcp, GrkImage *p_image, uint32_t index);


    /**
     * Writes the CBD marker (Component bit depth definition)
     *
     * @param       codeStream                           JPEG 2000 code stream
     */
    bool write_cbd();

    /**
     * Reads a CBD marker (Component bit depth definition)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     *
     */
    bool read_cbd( uint8_t *p_header_data,
    		uint16_t header_size);

    /**
     * Writes COC marker for each component.
     *
     * @param       codeStream          JPEG 2000 code stream
      */
    bool write_all_coc();

    /**
     * Writes QCC marker for each component.
     *
     * @param       codeStream          JPEG 2000 code stream
     */
    bool write_all_qcc();

    /**
     * Writes regions of interests.
     *
     * @param       codeStream          JPEG 2000 code stream
     */
    bool write_regions();

    /**
     * Writes EPC ????
     *
     * @param       codeStream          JPEG 2000 code stream
     */
    bool write_epc();

    /**
     * Writes the RGN marker (Region Of Interest)
     *
     * @param       tile_no               the tile to output
     * @param       comp_no               the component to output
     * @param       nb_comps                the number of components
     * @param       codeStream                   JPEG 2000 code stream

     */
    bool write_rgn( uint16_t tile_no, uint32_t comp_no,
    		uint32_t nb_comps);

    /**
     * Reads a RGN marker (Region Of Interest)
     *
     * @param       codeStream      JPEG 2000 code stream
     * @param       p_header_data   header data
     * @param       header_size     size of header data
     */
    bool read_rgn( uint8_t *p_header_data,
    		uint16_t header_size);

    /**
     * Writes the EOC marker (End of Codestream)
     *
     * @param       codeStream          JPEG 2000 code stream
     */
    bool write_eoc();

    /**
     * Writes the CBD-MCT-MCC-MCO markers (Multi components transform)
     *
     * @param       codeStream          JPEG 2000 code stream
     */
    bool write_mct_data_group();

    void copy_tile_component_parameters(void);

    /**
     Converts an enum type progression order to string type
     */
    static char* convert_progression_order(GRK_PROG_ORDER prg_order);

   /**
      * Calculates the total number of tile parts needed by the compressor to
      * compress such an image. If not enough memory is available, then the function return false.
      *
      * @param       cp              coding parameters for the image.
      * @param       p_nb_tile_parts total number of tile parts in whole image.
      * @param       image           image to compress.

      *
      * @return true if the function was successful, false else.
      */
     bool calculate_tp(CodingParams *cp, uint16_t *p_nb_tile_parts, GrkImage *image);

     bool read_header_procedure(void);

private:


     /**
      * Gets the number of tile parts used for the given change of progression (if any) and the given tile.
      *
      * @param               cp                      the coding parameters.
      * @param               pino            the offset of the given poc (i.e. its position in the coding parameter).
      * @param               tileno          the given tile.
      *
      * @return              the number of tile parts.
      */
     uint64_t get_num_tp(CodingParams *cp, uint32_t pino, uint16_t tileno);


    /**
     * Checks the progression order changes values. Tells of the poc given as input are valid.
     * A nice message is outputted at errors.
     *
     * @param       p_pocs                the progression order changes.
     * @param       nb_pocs               the number of progression order changes.
     * @param       nb_resolutions        the number of resolutions.
     * @param       numcomps              the number of components
     * @param       numlayers             the number of layers.

     *
     * @return      true if the pocs are valid.
     */
    bool check_poc_val(const  grk_progression  *p_pocs, uint32_t nb_pocs,
    		uint32_t nb_resolutions, uint32_t numcomps, uint32_t numlayers);

    bool init_mct_encoding(TileCodingParams *p_tcp, GrkImage *p_image);

	/* output image (for decompress) */
	GrkImage *m_output_image;

	/** the list of procedures to exec **/
	std::vector<PROCEDURE_FUNC> m_procedure_list;

	/** the list of validation procedures to follow to make sure the code is valid **/
	std::vector<PROCEDURE_FUNC> m_validation_list;


	// stores header image information (decompress/compress)
	// decompress: components are subsampled and resolution-reduced
	GrkImage *m_headerImage;

	/**
	 * Reads the lookup table containing all the marker, status and action,
	 * and returns the handler associated with the marker value.
	 * @param       id            Marker value to look up
	 *
	 * @return      the handler associated with the id.
	 */
	const marker_handler* get_marker_handler(	uint16_t id);

	std::map<uint16_t, marker_handler*>  marker_map;

	/** current TileProcessor **/
	TileProcessor *m_tileProcessor;

	TileCache *m_tileCache;

	BufferedStream *m_stream;
	std::map<uint32_t, TileProcessor*> m_processors;

	/** index of single tile to decompress;
	 *  !!! initialized to -1 !!! */
	int32_t m_tile_ind_to_dec;

	uint8_t *m_marker_scratch;
	uint16_t m_marker_scratch_size;
    bool m_multiTile;
	uint16_t m_curr_marker;
	/** Only valid for decoding. Whether the whole tile is decompressed,
	 *  or just the window in win_x0/win_y0/win_x1/win_y1 */
	bool   wholeTileDecompress;
	grk_plugin_tile *current_plugin_tile;
	bool m_headerError;
};

/** @name Exported functions */
/*@{*/
/* ----------------------------------------------------------------------- */



/* ----------------------------------------------------------------------- */
/*@}*/

/*@}*/

bool j2k_decompress_tile(CodeStream *codeStream, GrkImage *p_image, uint16_t tile_index);


}
