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

#include "grk_includes.h"
#include "ojph_arch.h"

namespace grk {

bool j2k_write_soc(CodeStream *codeStream) {
	return codeStream->write_soc();
}

/**
 * Reads a SOC marker (Start of Codestream)
 * @param       codeStream    JPEG 2000 code stream.
 */
bool j2k_read_soc(CodeStream *codeStream) {
	return codeStream->read_soc();
}

bool j2k_write_siz(CodeStream *codeStream) {
	return codeStream->write_siz();
}

/**
 * Reads a CAP marker
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_cap(CodeStream *codeStream,  uint8_t *p_header_data,
		uint16_t header_size) {
	 return codeStream->read_cap(p_header_data, header_size);
}

bool j2k_write_cap(CodeStream *codeStream) {
	return codeStream->write_cap();
}

/**
 * Reads a SIZ marker (image and tile size)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool j2k_read_siz(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_siz(p_header_data, header_size);
}

bool j2k_write_com(CodeStream *codeStream) {
	return codeStream->write_com();
}

/**
 * Reads a COM marker (comments)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_com(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_com(p_header_data, header_size);
}

bool j2k_write_cod(CodeStream *codeStream) {
	return codeStream->write_cod();
}


/**
 * Reads a COD marker (Coding Style defaults)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_cod(CodeStream *codeStream,uint8_t *p_header_data,
		uint16_t header_size) {
	  return codeStream->read_cod(p_header_data, header_size);
}

bool j2k_write_coc(CodeStream *codeStream, uint32_t comp_no) {
	return codeStream->write_coc(comp_no);
}

bool j2k_compare_coc(CodeStream *codeStream, uint32_t first_comp_no,
		uint32_t second_comp_no) {
	return codeStream->compare_coc(first_comp_no, second_comp_no);
}

/**
 * Reads a COC marker (Coding Style Component)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data

 */
bool j2k_read_coc(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_coc(p_header_data, header_size);
}

bool j2k_write_qcd(CodeStream *codeStream) {
	return codeStream->write_qcd();
}

/**
 * Reads a QCD marker (Quantization defaults)
 *
 * @param       codeStream      JPEG 2000 code stream
  * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool j2k_read_qcd(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_qcd(p_header_data, header_size);
}

bool j2k_write_qcc(CodeStream *codeStream, uint32_t comp_no) {
	return codeStream->write_qcc(comp_no);
}

bool j2k_compare_qcc(CodeStream *codeStream,  uint32_t first_comp_no,
		uint32_t second_comp_no) {
	return codeStream->compare_qcc(first_comp_no, second_comp_no);
}

/**
 * Reads a QCC marker (Quantization component)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool j2k_read_qcc(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_qcc(p_header_data, header_size);
}

bool j2k_write_poc(CodeStream *codeStream) {
	return codeStream->write_poc();
}

/**
 * Reads a POC marker (Progression Order Change)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool j2k_read_poc(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_poc(p_header_data, header_size);
}

/**
 * Reads a CRG marker (Component registration)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_crg(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_crg(p_header_data, header_size);
}

/**
 * Reads a PLM marker (Packet length, main header marker)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_plm(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_plm(p_header_data, header_size);
}

/**
 * Reads a PLT marker (Packet length, tile-part header)
 *
 * @param       codeStream           JPEG 2000 code stream
 * @param       p_header_data   the data contained in the PLT box.
 * @param       header_size   the size of the data contained in the PLT marker.

 */
bool j2k_read_plt(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_plt(p_header_data, header_size);
}

/**
 * Reads a PPM marker (Packed packet headers, main header)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */

bool j2k_read_ppm(CodeStream *codeStream,  uint8_t *p_header_data,
		uint16_t header_size) {
   return codeStream->read_ppm(p_header_data, header_size);
}


/**
 * Reads a PPT marker (Packed packet headers, tile-part header)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_ppt(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_ppt(p_header_data, header_size);
}

/**
 * Read SOT (Start of tile part) marker
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_sot(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_sot(p_header_data, header_size);
}

bool j2k_write_rgn(CodeStream *codeStream, uint16_t tile_no, uint32_t comp_no,
		uint32_t nb_comps) {
	return codeStream->write_rgn(tile_no, comp_no, nb_comps);
}

bool j2k_write_eoc(CodeStream *codeStream) {
	return codeStream->write_eoc();
}

/**
 * Reads a RGN marker (Region Of Interest)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 */
bool j2k_read_rgn(CodeStream *codeStream,  uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_rgn(p_header_data, header_size);
}

bool j2k_write_mct_data_group(CodeStream *codeStream) {
	return codeStream->write_mct_data_group();
}

bool j2k_write_all_coc(CodeStream *codeStream) {
	return codeStream->write_all_coc();
}

bool j2k_write_all_qcc(CodeStream *codeStream) {
	return codeStream->write_all_qcc();
}

bool j2k_write_regions(CodeStream *codeStream) {
	return codeStream->write_regions();
}

bool j2k_write_epc(CodeStream *codeStream) {
	return codeStream->write_epc();
}

/**
 * Reads a MCT marker (Multiple Component Transform)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_mct(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	 return codeStream->read_mct(p_header_data, header_size);
}

/**
 * Reads a MCC marker (Multiple Component Collection)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_mcc(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
   return codeStream->read_mcc(p_header_data, header_size);
}

bool j2k_write_mco(CodeStream *codeStream) {
	return codeStream->write_mco();
}

/**
 * Reads a MCO marker (Multiple Component Transform Ordering)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data.
 * @param       header_size     size of header data

 */
bool j2k_read_mco(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_mco(p_header_data, header_size);
}

bool j2k_write_cbd(CodeStream *codeStream) {
	return codeStream->write_cbd();
}

/**
 * Reads a CBD marker (Component bit depth definition)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_cbd(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
   return codeStream->read_cbd(p_header_data, header_size);
}

/**
 * Reads a TLM marker (Tile Length Marker)
 *
 * @param       codeStream      JPEG 2000 code stream
 * @param       p_header_data   header data
 * @param       header_size     size of header data
 *
 */
bool j2k_read_tlm(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size) {
	return codeStream->read_tlm(p_header_data, header_size);
}

bool j2k_write_tlm_begin(CodeStream *codeStream) {
	return codeStream->write_tlm_begin();
}
bool j2k_write_tlm_end(CodeStream *codeStream) {
	assert(codeStream);
	return codeStream->write_tlm_end();
}

uint32_t j2k_get_SPCod_SPCoc_size(CodeStream *codeStream, uint32_t comp_no) {
	return codeStream->get_SPCod_SPCoc_size(comp_no);
}

bool j2k_compare_SPCod_SPCoc(CodeStream *codeStream,
		uint32_t first_comp_no, uint32_t second_comp_no) {
	return codeStream->compare_SPCod_SPCoc(first_comp_no, second_comp_no);
}

bool j2k_write_SPCod_SPCoc(CodeStream *codeStream,uint32_t comp_no) {
	return codeStream->write_SPCod_SPCoc(comp_no);
}

bool j2k_read_SPCod_SPCoc(CodeStream *codeStream, uint32_t compno, uint8_t *p_header_data, uint16_t *header_size) {
	 return codeStream->read_SPCod_SPCoc(compno, p_header_data, header_size);
}

uint32_t j2k_get_SQcd_SQcc_size(CodeStream *codeStream, uint32_t comp_no) {
	return codeStream->get_SQcd_SQcc_size(comp_no);
}

bool j2k_compare_SQcd_SQcc(CodeStream *codeStream,	uint32_t first_comp_no, uint32_t second_comp_no) {
	return codeStream->compare_SQcd_SQcc(first_comp_no, second_comp_no);
}

bool j2k_write_SQcd_SQcc(CodeStream *codeStream, uint32_t comp_no) {
	return codeStream->write_SQcd_SQcc(comp_no);
}

bool j2k_read_SQcd_SQcc(CodeStream *codeStream, bool fromQCC,uint32_t compno,
		uint8_t *p_header_data, uint16_t *header_size) {
	return codeStream->read_SQcd_SQcc(fromQCC, compno, p_header_data, header_size);
}

}
