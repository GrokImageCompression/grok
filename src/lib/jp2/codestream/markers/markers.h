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

namespace grk {

const uint32_t MCT_ELEMENT_SIZE[] = { 2, 4, 4, 8 };


typedef void (*j2k_mct_function)(const void *p_src_data, void *p_dest_data,
		uint64_t nb_elem);


void j2k_read_int16_to_float(const void *p_src_data, void *p_dest_data,
		uint64_t nb_elem);
void j2k_read_int32_to_float(const void *p_src_data, void *p_dest_data,
		uint64_t nb_elem);
void j2k_read_float32_to_float(const void *p_src_data, void *p_dest_data,
		uint64_t nb_elem);
void j2k_read_float64_to_float(const void *p_src_data, void *p_dest_data,
		uint64_t nb_elem);

void j2k_read_int16_to_int32(const void *p_src_data, void *p_dest_data,
		uint64_t nb_elem);
void j2k_read_int32_to_int32(const void *p_src_data, void *p_dest_data,
		uint64_t nb_elem);
void j2k_read_float32_to_int32(const void *p_src_data, void *p_dest_data,
		uint64_t nb_elem);
void j2k_read_float64_to_int32(const void *p_src_data, void *p_dest_data,
		uint64_t nb_elem);

void j2k_write_float_to_int16(const void *p_src_data, void *p_dest_data,
		uint64_t nb_elem);
void j2k_write_float_to_int32(const void *p_src_data, void *p_dest_data,
		uint64_t nb_elem);
void j2k_write_float_to_float(const void *p_src_data, void *p_dest_data,
		uint64_t nb_elem);
void j2k_write_float_to_float64(const void *p_src_data,
		void *p_dest_data, uint64_t nb_elem);

/**
 Add main header marker information
 @param cstr_index    Codestream information structure
 @param type         marker type
 @param pos          byte offset of marker segment
 @param len          length of marker segment
 */
bool j2k_add_mhmarker( grk_codestream_index  *cstr_index, uint32_t type,
		uint64_t pos, uint32_t len);

/**
 * Writes the SOC marker (Start Of Codestream)
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_soc(CodeStream *codeStream);

/**
 * Reads a SOC marker (Start of Codestream)
 * @param       codeStream           JPEG 2000 code stream.
 */
bool j2k_read_soc(CodeStream *codeStream);

/**
 * Writes the SIZ marker (image and tile size)
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_siz(CodeStream *codeStream);

/**
 * Reads a SIZ marker (image and tile size)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool j2k_read_siz(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Reads a CAP marker
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool j2k_read_cap(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Writes the CAP marker
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_cap(CodeStream *codeStream);

/**
 * Writes the COM marker (comment)
 *
 * @param       codeStream      JPEG 2000 code stream
 */
bool j2k_write_com(CodeStream *codeStream);

/**
 * Reads a COM marker (comments)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_com(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);
/**
 * Writes the COD marker (Coding style default)
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_cod(CodeStream *codeStream);

/**
 * Reads a COD marker (Coding Style defaults)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_cod(CodeStream *codeStream, uint8_t *p_header_data,uint16_t header_size);

/**
 * Compares 2 COC markers (Coding style component)
 *
 * @param       codeStream            JPEG 2000 code stream
 * @param       first_comp_no  the index of the first component to compare.
 * @param       second_comp_no the index of the second component to compare.
 *
 * @return      true if equals
 */
bool j2k_compare_coc(CodeStream *codeStream,  uint32_t first_comp_no,
		uint32_t second_comp_no);

/**
 * Writes the COC marker (Coding style component)
 *
 * @param       codeStream  JPEG 2000 code stream
 * @param       comp_no   the index of the component to output.
 * @param       stream    buffered stream.

 */
bool j2k_write_coc(CodeStream *codeStream, uint32_t comp_no,
		BufferedStream *stream);

/**
 * Reads a COC marker (Coding Style Component)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_coc(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Writes the QCD marker (quantization default)
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_qcd(CodeStream *codeStream);

/**
 * Reads a QCD marker (Quantization defaults)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool j2k_read_qcd(CodeStream *codeStream, uint8_t *p_header_data,
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
bool j2k_compare_qcc(CodeStream *codeStream, uint32_t first_comp_no,
		uint32_t second_comp_no);

/**
 * Writes the QCC marker (quantization component)
 *
 * @param       codeStream  JPEG 2000 code stream
 * @param 		tile_index 	current tile index
 * @param       comp_no     the index of the component to output.
 * @param       stream      buffered stream.

 */
bool j2k_write_qcc(CodeStream *codeStream, uint16_t tile_index, uint32_t comp_no,
		BufferedStream *stream);
/**
 * Reads a QCC marker (Quantization component)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data

 */
bool j2k_read_qcc(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

uint16_t getPocSize(uint32_t l_nb_comp, uint32_t l_nb_poc);

/**
 * Writes the POC marker (Progression Order Change)
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_poc(CodeStream *codeStream);


/**
 * Reads a POC marker (Progression Order Change)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool j2k_read_poc(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Reads a CRG marker (Component registration)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_crg(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);
/**
 * Reads a TLM marker (Tile Length Marker)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool j2k_read_tlm(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * End writing the updated tlm.
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_tlm_end(CodeStream *codeStream);


/**
 * Begin writing the TLM marker (Tile Length Marker)
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_tlm_begin(CodeStream *codeStream);


/**
 * Reads a PLM marker (Packet length, main header marker)
 *
 * @param       codeStream      JPEG 2000 code stream
  * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_plm(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);
/**
 * Reads a PLT marker (Packet length, tile-part header)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_plt(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Reads a PPM marker (Packed headers, main header)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */

bool j2k_read_ppm(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Merges all PPM markers read (Packed headers, main header)
 *
 * @param       p_cp      main coding parameters.

 */
bool j2k_merge_ppm(CodingParams *p_cp);

/**
 * Reads a PPT marker (Packed packet headers, tile-part header)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_ppt(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Merges all PPT markers read (Packed headers, tile-part header)
 *
 * @param       p_tcp   the tile.

 */
bool j2k_merge_ppt(TileCodingParams *p_tcp);

/**
 * Read SOT (Start of tile part) marker
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data.
 * @param       header_size     size of header data
 *
 */
bool j2k_read_sot(CodeStream *codeStream,	uint8_t *p_header_data, uint16_t header_size);


/**
 * Compare 2 a SPCod/ SPCoc elements, i.e. the coding style of a given component of a tile.
 *
 * @param       codeStream            JPEG 2000 code stream
 * @param       first_comp_no  The 1st component number to compare.
 * @param       second_comp_no The 1st component number to compare.
 *
 * @return true if SPCdod are equals.
 */
bool j2k_compare_SPCod_SPCoc(CodeStream *codeStream,
		uint32_t first_comp_no, uint32_t second_comp_no);

/**
 * Writes a SPCod or SPCoc element, i.e. the coding style of a given component of a tile.
 *
 * @param       codeStream           JPEG 2000 code stream
 * @param       comp_no       the component number to output.
 *
 * @return true if successful
 */
bool j2k_write_SPCod_SPCoc(CodeStream *codeStream, 	uint32_t comp_no);

/**
 * Gets the size taken by writing a SPCod or SPCoc for the given tile and component.
 *
 * @param       codeStream                   the JPEG 2000 code stream
 * @param       comp_no               the component being outputted.
 *
 * @return      the number of bytes taken by the SPCod element.
 */
uint32_t j2k_get_SPCod_SPCoc_size(CodeStream *codeStream, uint32_t comp_no);

/**
 * Reads a SPCod or SPCoc element, i.e. the coding style of a given component of a tile.
 * @param       codeStream           JPEG 2000 code stream
 * @param       compno          component number
 * @param       p_header_data   the data contained in the COM box.
 * @param       header_size   the size of the data contained in the COM marker.

 */
bool j2k_read_SPCod_SPCoc(CodeStream *codeStream,
		uint32_t compno, uint8_t *p_header_data, uint16_t *header_size);

/**
 * Gets the size taken by writing SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
 *
 * @param       codeStream                   the JPEG 2000 code stream
 * @param       comp_no               the component being output.
 *
 * @return      the number of bytes taken by the SPCod element.
 */
uint32_t j2k_get_SQcd_SQcc_size(CodeStream *codeStream,	uint32_t comp_no);

/**
 * Compares 2 SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
 *
 * @param       codeStream                   JPEG 2000 code stream
 * @param       first_comp_no         the first component number to compare.
 * @param       second_comp_no        the second component number to compare.
 *
 * @return true if equals.
 */
bool j2k_compare_SQcd_SQcc(CodeStream *codeStream,
		uint32_t first_comp_no, uint32_t second_comp_no);

/**
 * Writes a SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
 *
 * @param       codeStream                   JPEG 2000 code stream
 * @param       comp_no               the component number to output.
 *
 */
bool j2k_write_SQcd_SQcc(CodeStream *codeStream,uint32_t comp_no);

/**
 * Updates the Tile Length Marker.
 */
void j2k_update_tlm(CodeStream *codeStream, uint16_t tile_index, uint32_t tile_part_size);

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
bool j2k_read_SQcd_SQcc(CodeStream *codeStream, bool fromQCC,uint32_t compno,
		uint8_t *p_header_data, uint16_t *header_size);

/**
 * Writes the MCT marker (Multiple Component Transform)
 *
 * @param       p_mct_record  MCT record
 * @param       stream        buffered stream.

 */
bool j2k_write_mct_record(grk_mct_data *p_mct_record, BufferedStream *stream);

/**
 * Reads a MCT marker (Multiple Component Transform)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_mct(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Writes the MCC marker (Multiple Component Collection)
 *
 * @param       p_mcc_record          MCC record
 * @param       stream                buffered stream.

 */
bool j2k_write_mcc_record(grk_simple_mcc_decorrelation_data *p_mcc_record,
		BufferedStream *stream);

/**
 * Reads a MCC marker (Multiple Component Collection)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_mcc(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Writes the MCO marker (Multiple component transformation ordering)
 *
 * @param       codeStream      JPEG 2000 code stream
 */
bool j2k_write_mco(CodeStream *codeStream);

/**
 * Reads a MCO marker (Multiple Component Transform Ordering)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data.
 * @param       header_size     size of header data
 *
 */
bool j2k_read_mco(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

bool j2k_add_mct(TileCodingParams *p_tcp, grk_image *p_image, uint32_t index);


/**
 * Writes the CBD marker (Component bit depth definition)
 *
 * @param       codeStream                           JPEG 2000 code stream
 */
bool j2k_write_cbd(CodeStream *codeStream);

/**
 * Reads a CBD marker (Component bit depth definition)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_cbd(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Writes COC marker for each component.
 *
 * @param       codeStream          JPEG 2000 code stream
  */
bool j2k_write_all_coc(CodeStream *codeStream);

/**
 * Writes QCC marker for each component.
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_all_qcc(CodeStream *codeStream);

/**
 * Writes regions of interests.
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_regions(CodeStream *codeStream);

/**
 * Writes EPC ????
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_epc(CodeStream *codeStream);

void j2k_update_tlm(CodeStream *codeStream, uint16_t tile_index, uint32_t tile_part_size) ;

/**
 * Writes the RGN marker (Region Of Interest)
 *
 * @param       tile_no               the tile to output
 * @param       comp_no               the component to output
 * @param       nb_comps                the number of components
 * @param       codeStream                   JPEG 2000 code stream

 */
bool j2k_write_rgn(CodeStream *codeStream, uint16_t tile_no, uint32_t comp_no,
		uint32_t nb_comps);

/**
 * Reads a RGN marker (Region Of Interest)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool j2k_read_rgn(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size);

/**
 * Writes the EOC marker (End of Codestream)
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_eoc(CodeStream *codeStream);

/**
 * Writes the CBD-MCT-MCC-MCO markers (Multi components transform)
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_mct_data_group(CodeStream *codeStream);

}

