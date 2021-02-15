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
 * Writes the SOC marker (Start Of Codestream)
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_soc(CodeStream *codeStream);

/**
 * Writes the SIZ marker (image and tile size)
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_siz(CodeStream *codeStream);

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
 * Writes the COD marker (Coding style default)
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_cod(CodeStream *codeStream);


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
 * Writes the QCD marker (quantization default)
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_qcd(CodeStream *codeStream);


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
 * Writes the POC marker (Progression Order Change)
 *
 * @param       codeStream          JPEG 2000 code stream
 */
bool j2k_write_poc(CodeStream *codeStream);

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
 * Writes a SPCod or SPCoc element, i.e. the coding style of a given component of a tile.
 *
 * @param       codeStream           JPEG 2000 code stream
 * @param       comp_no       the component number to output.
 *
 * @return true if successful
 */
bool j2k_write_SPCod_SPCoc(CodeStream *codeStream, 	uint32_t comp_no);

/**
 * Writes a SQcd or SQcc element, i.e. the quantization values of a band in the QCD or QCC.
 *
 * @param       codeStream                   JPEG 2000 code stream
 * @param       comp_no               the component number to output.
 *
 */
bool j2k_write_SQcd_SQcc(CodeStream *codeStream,uint32_t comp_no);


/**
 * Writes the MCO marker (Multiple component transformation ordering)
 *
 * @param       codeStream      JPEG 2000 code stream
 */
bool j2k_write_mco(CodeStream *codeStream);

/**
 * Writes the CBD marker (Component bit depth definition)
 *
 * @param       codeStream                           JPEG 2000 code stream
 */
bool j2k_write_cbd(CodeStream *codeStream);


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

