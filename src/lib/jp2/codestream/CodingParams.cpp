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

#include "grk_includes.h"

namespace grk {

void CodingParams::destroy() {
	if (tcps != nullptr) {
		uint32_t nb_tiles = t_grid_height * t_grid_width;

		for (uint32_t i = 0U; i < nb_tiles; ++i) {
			auto current_tile = tcps + i;
			current_tile->destroy();
		}
		delete[] tcps;
		tcps = nullptr;
	}
	for (uint32_t i = 0; i < num_comments; ++i) {
        delete[] ((uint8_t*) comment[i]);
		comment[i] = nullptr;
	}
	num_comments = 0;
	delete plm_markers;
	delete tlm_markers;
	delete ppm_marker;
}

TileCodingParams::TileCodingParams() :
								csty(0),
								prg(GRK_PROG_UNKNOWN),
								numlayers(0),
								num_layers_to_decode(0),
								mct(0),
								numpocs(0),
								ppt_markers_count(0),
								ppt_markers(nullptr),
								ppt_data(nullptr),
								ppt_buffer(nullptr),
								ppt_data_size(0),
								ppt_len(0),
								main_qcd_qntsty(0),
								main_qcd_numStepSizes(0),
								tccps(nullptr),
								m_tile_part_index(-1),
								m_nb_tile_parts(0),
								m_tile_data(nullptr),
								mct_norms(nullptr),
								m_mct_decoding_matrix(nullptr),
								m_mct_coding_matrix(nullptr),
								m_mct_records(nullptr),
								m_nb_mct_records(0),
								m_nb_max_mct_records(0),
								m_mcc_records(nullptr),
								m_nb_mcc_records(0),
								m_nb_max_mcc_records(0),
								cod(false),
								ppt(false),
								POC(false),
								isHT(false) {
	for (auto i = 0; i < 100; ++i)
		rates[i] = 0.0;
	for (auto i = 0; i < 100; ++i)
		distoratio[i] = 0;
	for (auto i = 0; i < 32; ++i)
		memset(pocs + i, 0, sizeof(grk_poc));
}

TileCodingParams::~TileCodingParams(){
	destroy();
}

void TileCodingParams::destroy() {
	if (ppt_markers != nullptr) {
		for (uint32_t i = 0U; i < ppt_markers_count; ++i)
			grk_free(ppt_markers[i].m_data);
		ppt_markers_count = 0U;
		grk_free(ppt_markers);
		ppt_markers = nullptr;
	}

	delete[] ppt_buffer;
	ppt_buffer = nullptr;
	delete[] tccps;
	tccps = nullptr;
	grk_free(m_mct_coding_matrix);
	m_mct_coding_matrix = nullptr;
	grk_free(m_mct_decoding_matrix);
	m_mct_decoding_matrix = nullptr;

	if (m_mcc_records) {
		grk_free(m_mcc_records);
		m_mcc_records = nullptr;
		m_nb_max_mcc_records = 0;
		m_nb_mcc_records = 0;
	}

	if (m_mct_records) {
		auto mct_data = m_mct_records;
		for (uint32_t i = 0; i < m_nb_mct_records; ++i) {
			grk_free(mct_data->m_data);
			++mct_data;
		}
		grk_free(m_mct_records);
		m_mct_records = nullptr;
	}
	grk_free(mct_norms);
	mct_norms = nullptr;
	delete m_tile_data;
	m_tile_data = nullptr;
}



TileComponentCodingParams::TileComponentCodingParams() : csty(0),
														numresolutions(0),
														cblkw(0),
														cblkh(0),
														cblk_sty(0),
														qmfbid(0),
														quantizationMarkerSet(false),
														fromQCC(false),
														fromTileHeader(false),
														qntsty(0),
														numStepSizes(0),
														numgbits(0),
														roishift(0),
														m_dc_level_shift(0)
{
	for (uint32_t i = 0; i < GRK_J2K_MAXRLVLS; ++i){
		prcw[i] = 0;
		prch[i] = 0;
	}
}

bool DecoderState::findNextTile(CodeStream *codeStream){
	auto stream = codeStream->getStream();
	last_tile_part_was_read = false;
	m_state &= (uint32_t) (~J2K_DEC_STATE_DATA);

	// if there is no EOC marker and there is also no data left, then simply return true
	if (stream->get_number_byte_left() == 0
			&& m_state == J2K_DEC_STATE_NO_EOC) {
		return true;
	}
	// if EOC marker has not been read yet, then try to read the next marker
	// (should be EOC or SOT)
	if (m_state != J2K_DEC_STATE_EOC) {
		if (!codeStream->read_marker_skip_unknown()) {
			GRK_WARN(
					"findNextTile: Not enough data to read another marker.\n"
							"Tile may be truncated.");
			return true;
		}
		switch (codeStream->m_curr_marker) {
		// we found the EOC marker - set state accordingly and return true;
		// we can ignore all data after EOC
		case J2K_MS_EOC:
			m_state = J2K_DEC_STATE_EOC;
			return true;
			break;
			// start of another tile
		case J2K_MS_SOT:
			return true;
			break;
		default: {
			auto bytesLeft = stream->get_number_byte_left();
			// no bytes left - file ends without EOC marker
			if (bytesLeft == 0) {
				m_state = J2K_DEC_STATE_NO_EOC;
				GRK_WARN("findNextTile: stream does not end with EOC");
				return true;
			}
			GRK_WARN("findNextTile: expected EOC or SOT "
					"but found marker 0x%x.\nIgnoring %d bytes "
					"remaining in the stream.", codeStream->m_curr_marker, bytesLeft+2);
			throw DecodeUnknownMarkerAtEndOfTileException();
		}
			break;
		}
	}

	return true;
}


}
