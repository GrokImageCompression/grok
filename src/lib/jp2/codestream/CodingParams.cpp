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
 *    This source code incorporates work covered by the following copyright and
 *    permission notice:
 *
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2006-2007, Parvatha Elangovan
 * Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
 * Copyright (c) 2011-2012, Centre National d'Etudes Spatiales (CNES), France
 * Copyright (c) 2012, CS Systemes d'Information, France
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

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
				0), main_qcd_numStepSizes(0), tccps(nullptr), m_current_tile_part_index(
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
