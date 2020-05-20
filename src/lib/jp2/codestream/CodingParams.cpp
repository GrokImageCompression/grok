#include "grok_includes.h"

namespace grk {

void CodingParams::destroy() {
	uint32_t nb_tiles;
	TileCodingParams *current_tile = nullptr;
	if (tcps != nullptr) {
		uint32_t i;
		current_tile = tcps;
		nb_tiles = t_grid_height * t_grid_width;

		for (i = 0U; i < nb_tiles; ++i) {
			current_tile->destroy();
			++current_tile;
		}
		delete[] tcps;
		tcps = nullptr;
	}
	if (ppm_markers != nullptr) {
		uint32_t i;
		for (i = 0U; i < ppm_markers_count; ++i) {
			if (ppm_markers[i].m_data != nullptr) {
				grok_free(ppm_markers[i].m_data);
			}
		}
		ppm_markers_count = 0U;
		grok_free(ppm_markers);
		ppm_markers = nullptr;
	}
	grok_free(ppm_buffer);
	ppm_buffer = nullptr;
	ppm_data = nullptr; /* ppm_data belongs to the allocated buffer pointed by ppm_buffer */
	for (size_t i = 0; i < num_comments; ++i) {
		grk_buffer_delete((uint8_t*) comment[i]);
		comment[i] = nullptr;
	}
	num_comments = 0;
	delete plm_markers;
	delete tlm_markers;
}

TileCodingParams::TileCodingParams() :
		csty(0), prg(GRK_PROG_UNKNOWN), numlayers(0), num_layers_to_decode(0), mct(
				0), numpocs(0), ppt_markers_count(0), ppt_markers(nullptr), ppt_data(
				nullptr), ppt_buffer(nullptr), ppt_data_size(0), ppt_len(0), main_qcd_qntsty(
				0), main_qcd_numStepSizes(0), tccps(nullptr), m_current_tile_part_number(
				-1), m_nb_tile_parts(0), m_tile_data(nullptr), mct_norms(
				nullptr), m_mct_decoding_matrix(nullptr), m_mct_coding_matrix(
				nullptr), m_mct_records(nullptr), m_nb_mct_records(0), m_nb_max_mct_records(
				0), m_mcc_records(nullptr), m_nb_mcc_records(0), m_nb_max_mcc_records(
				0), cod(false), ppt(false), POC(false), isHT(false) {
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
			grok_free(ppt_markers[i].m_data);
		ppt_markers_count = 0U;
		grok_free(ppt_markers);
		ppt_markers = nullptr;
	}

	grok_free(ppt_buffer);
	ppt_buffer = nullptr;
	grok_free(tccps);
	tccps = nullptr;
	grok_free(m_mct_coding_matrix);
	m_mct_coding_matrix = nullptr;
	grok_free(m_mct_decoding_matrix);
	m_mct_decoding_matrix = nullptr;

	if (m_mcc_records) {
		grok_free(m_mcc_records);
		m_mcc_records = nullptr;
		m_nb_max_mcc_records = 0;
		m_nb_mcc_records = 0;
	}

	if (m_mct_records) {
		auto mct_data = m_mct_records;
		for (uint32_t i = 0; i < m_nb_mct_records; ++i) {
			grok_free(mct_data->m_data);
			++mct_data;
		}
		grok_free(m_mct_records);
		m_mct_records = nullptr;
	}
	grok_free(mct_norms);
	mct_norms = nullptr;
	delete m_tile_data;
	m_tile_data = nullptr;
}


}
