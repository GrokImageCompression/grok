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


PPMMarker::PPMMarker() : ppm_markers_count(0),
		ppm_markers(nullptr),
		ppm_data(nullptr),
		ppm_len(0),
		ppm_data_read(0),
		ppm_data_current(0),
		ppm_buffer(nullptr),
		ppm_data_size(0)
{}

PPMMarker::~PPMMarker(){
	if (ppm_markers != nullptr) {
			for (uint32_t i = 0U; i < ppm_markers_count; ++i) {
				if (ppm_markers[i].m_data != nullptr) {
					grk_free(ppm_markers[i].m_data);
				}
			}
			ppm_markers_count = 0U;
			grk_free(ppm_markers);
			ppm_markers = nullptr;
		}
		grk_free(ppm_buffer);
		ppm_buffer = nullptr;
		ppm_data = nullptr; /* ppm_data belongs to the allocated buffer pointed by ppm_buffer */
}

bool PPMMarker::read(uint8_t *p_header_data,
		uint16_t header_size){
	uint8_t i_ppm;

	assert(p_header_data != nullptr);

	/* We need to have the i_ppm element + 1 byte of Nppm/Ippm at minimum */
	if (header_size < 2) {
		GROK_ERROR("Error reading PPM marker");
		return false;
	}


	/* i_ppm */
	grk_read<uint8_t>(p_header_data++, &i_ppm);
	--header_size;

	/* check allocation needed */
	if (!ppm_markers) { /* first PPM marker */
		uint32_t newCount = i_ppm + 1U;
		assert(ppm_markers_count == 0U);

		ppm_markers = (grk_ppx*) grk_calloc(newCount, sizeof(grk_ppx));
		if (ppm_markers == nullptr) {
			GROK_ERROR("Not enough memory to read PPM marker");
			return false;
		}
		ppm_markers_count = newCount;
	} else if (ppm_markers_count <= i_ppm) {
		uint32_t newCount = i_ppm + 1U;
		auto new_ppm_markers = (grk_ppx*) grk_realloc(ppm_markers,
				newCount * sizeof(grk_ppx));
		if (new_ppm_markers == nullptr) {
			/* clean up to be done on cp destruction */
			GROK_ERROR("Not enough memory to read PPM marker");
			return false;
		}
		ppm_markers = new_ppm_markers;
		memset(ppm_markers + ppm_markers_count, 0,
				(newCount - ppm_markers_count) * sizeof(grk_ppx));
		ppm_markers_count = newCount;
	}

	if (ppm_markers[i_ppm].m_data != nullptr) {
		/* clean up to be done on cp destruction */
		GROK_ERROR("ippm %u already read", i_ppm);
		return false;
	}

	ppm_markers[i_ppm].m_data = (uint8_t*) grk_malloc(header_size);
	if (ppm_markers[i_ppm].m_data == nullptr) {
		/* clean up to be done on cp destruction */
		GROK_ERROR("Not enough memory to read PPM marker");
		return false;
	}
	ppm_markers[i_ppm].m_data_size = header_size;
	memcpy(ppm_markers[i_ppm].m_data, p_header_data, header_size);

	return true;
}
bool PPMMarker::merge(){
	uint32_t i, ppm_data_size, N_ppm_remaining;
	assert(ppm_buffer == nullptr);

	if (!ppm_markers)
		return true;

	ppm_data_size = 0U;
	N_ppm_remaining = 0U;
	for (i = 0U; i < ppm_markers_count; ++i) {
		if (ppm_markers[i].m_data != nullptr) { /* standard doesn't seem to require contiguous Zppm */
			uint32_t N_ppm;
			uint32_t data_size = ppm_markers[i].m_data_size;
			const uint8_t *data = ppm_markers[i].m_data;

			if (N_ppm_remaining >= data_size) {
				N_ppm_remaining -= data_size;
				data_size = 0U;
			} else {
				data += N_ppm_remaining;
				data_size -= N_ppm_remaining;
				N_ppm_remaining = 0U;
			}
			if (data_size > 0U) {
				do {
					/* read Nppm */
					if (data_size < 4U) {
						/* clean up to be done on cp destruction */
						GROK_ERROR("Not enough bytes to read Nppm");
						return false;
					}
					grk_read<uint32_t>(data, &N_ppm, 4);
					data += 4;
					data_size -= 4;
					m_tile_ppt.push_back(grk_buf(nullptr,ppm_data_size, N_ppm,false));
					ppm_data_size += N_ppm; /* can't overflow, max 256 markers of max 65536 bytes, that is when PPM markers are not corrupted which is checked elsewhere */

					if (data_size >= N_ppm) {
						data_size -= N_ppm;
						data += N_ppm;
					} else {
						N_ppm_remaining = N_ppm - data_size;
						data_size = 0U;
					}
				} while (data_size > 0U);
			}
		}
	}
	if (N_ppm_remaining != 0U) {
		/* clean up to be done on cp destruction */
		GROK_ERROR("Corrupted PPM markers");
		return false;
	}
	ppm_buffer = (uint8_t*) grk_malloc(ppm_data_size);
	if (ppm_buffer == nullptr) {
		GROK_ERROR("Not enough memory to read PPM marker");
		return false;
	}
	for (auto &b : m_tile_ppt){
		b.buf = ppm_buffer + b.offset;
		b.offset = 0;
	}
	ppm_len = ppm_data_size;
	ppm_data_size = 0U;
	N_ppm_remaining = 0U;
	for (i = 0U; i < ppm_markers_count; ++i) {
		if (ppm_markers[i].m_data != nullptr) { /* standard doesn't seem to require contiguous Zppm */
			uint32_t N_ppm;
			uint32_t data_size = ppm_markers[i].m_data_size;
			const uint8_t *data = ppm_markers[i].m_data;

			if (N_ppm_remaining >= data_size) {
				memcpy(ppm_buffer + ppm_data_size, data, data_size);
				ppm_data_size += data_size;
				N_ppm_remaining -= data_size;
				data_size = 0U;
			} else {
				memcpy(ppm_buffer + ppm_data_size, data, N_ppm_remaining);
				ppm_data_size += N_ppm_remaining;
				data += N_ppm_remaining;
				data_size -= N_ppm_remaining;
				N_ppm_remaining = 0U;
			}

			if (data_size > 0U) {
				do {
					/* read Nppm */
					if (data_size < 4U) {
						/* clean up to be done on cp destruction */
						GROK_ERROR("Not enough bytes to read Nppm");
						return false;
					}
					grk_read<uint32_t>(data, &N_ppm, 4);
					data += 4;
					data_size -= 4;

					if (data_size >= N_ppm) {
						memcpy(ppm_buffer + ppm_data_size, data, N_ppm);
						ppm_data_size += N_ppm;
						data_size -= N_ppm;
						data += N_ppm;
					} else {
						memcpy(ppm_buffer + ppm_data_size, data,
								data_size);
						ppm_data_size += data_size;
						N_ppm_remaining = N_ppm - data_size;
						data_size = 0U;
					}
				} while (data_size > 0U);
			}
			grk_free(ppm_markers[i].m_data);
			ppm_markers[i].m_data = nullptr;
			ppm_markers[i].m_data_size = 0U;
		}
	}

	ppm_data = ppm_buffer;
	ppm_data_size = (uint32_t)ppm_len;

	ppm_markers_count = 0U;
	grk_free(ppm_markers);
	ppm_markers = nullptr;

	return true;
}

} /* namespace grk */
