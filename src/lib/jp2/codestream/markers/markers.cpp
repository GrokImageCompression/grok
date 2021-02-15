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


bool j2k_write_siz(CodeStream *codeStream) {
	return codeStream->write_siz();
}


bool j2k_write_cap(CodeStream *codeStream) {
	return codeStream->write_cap();
}


bool j2k_write_com(CodeStream *codeStream) {
	return codeStream->write_com();
}


bool j2k_write_cod(CodeStream *codeStream) {
	return codeStream->write_cod();
}

bool j2k_write_coc(CodeStream *codeStream, uint32_t comp_no) {
	return codeStream->write_coc(comp_no);
}

bool j2k_write_qcd(CodeStream *codeStream) {
	return codeStream->write_qcd();
}

bool j2k_write_qcc(CodeStream *codeStream, uint32_t comp_no) {
	return codeStream->write_qcc(comp_no);
}

bool j2k_write_poc(CodeStream *codeStream) {
	return codeStream->write_poc();
}

bool j2k_write_rgn(CodeStream *codeStream, uint16_t tile_no, uint32_t comp_no,
		uint32_t nb_comps) {
	return codeStream->write_rgn(tile_no, comp_no, nb_comps);
}

bool j2k_write_eoc(CodeStream *codeStream) {
	return codeStream->write_eoc();
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


bool j2k_write_mco(CodeStream *codeStream) {
	return codeStream->write_mco();
}


bool j2k_write_cbd(CodeStream *codeStream) {
	return codeStream->write_cbd();
}


bool j2k_write_tlm_begin(CodeStream *codeStream) {
	return codeStream->write_tlm_begin();
}
bool j2k_write_tlm_end(CodeStream *codeStream) {
	assert(codeStream);
	return codeStream->write_tlm_end();
}

bool j2k_write_SPCod_SPCoc(CodeStream *codeStream,uint32_t comp_no) {
	return codeStream->write_SPCod_SPCoc(comp_no);
}

bool j2k_write_SQcd_SQcc(CodeStream *codeStream, uint32_t comp_no) {
	return codeStream->write_SQcd_SQcc(comp_no);
}

}
