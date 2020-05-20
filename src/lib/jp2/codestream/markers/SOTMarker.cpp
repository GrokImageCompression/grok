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


bool SOTMarker::write(CodeStream *p_j2k, BufferedStream *stream,
		uint64_t *psot_location, uint64_t *p_data_written){
	assert(p_j2k != nullptr);

	/* SOT */
	if (!stream->write_short(J2K_MS_SOT))
		return false;

	/* Lsot */
	if (!stream->write_short(10))
		return false;
	/* Isot */
	if (!stream->write_short(
			(uint16_t) p_j2k->m_tileProcessor->m_current_tile_number))
		return false;

	/* Psot  */
	*psot_location = stream->tell();
	if (!stream->skip(4))
		return false;

	/* TPsot */
	if (!stream->write_byte(p_j2k->m_tileProcessor->m_current_tile_part_number))
		return false;

	/* TNsot */
	if (!stream->write_byte(
			p_j2k->m_cp.tcps[p_j2k->m_tileProcessor->m_current_tile_number].m_nb_tile_parts))
		return false;

	*p_data_written += sot_marker_segment_len;

	return true;
}

bool SOTMarker::get_sot_values(uint8_t *p_header_data, uint32_t header_size,
		uint16_t *tile_no, uint32_t *p_tot_len, uint8_t *p_current_part,
		uint8_t *p_num_parts){

	assert(p_header_data != nullptr);

	/* Size of this marker is fixed = 12 (we have already read marker and its size)*/
	if (header_size != 8) {
		GROK_ERROR("Error reading SOT marker");
		return false;
	}
	/* Isot */
	uint32_t temp;
	grk_read<uint32_t>(p_header_data, &temp, 2);
	p_header_data += 2;
	*tile_no = (uint16_t) temp;
	/* Psot */
	grk_read<uint32_t>(p_header_data, p_tot_len, 4);
	p_header_data += 4;
	/* TPsot */
	grk_read<uint32_t>(p_header_data, &temp, 1);
	*p_current_part = (uint8_t) temp;
	++p_header_data;
	/* TNsot */
	grk_read<uint32_t>(p_header_data, &temp, 1);
	*p_num_parts = (uint8_t) temp;
	++p_header_data;
	return true;
}

 bool SOTMarker::read(CodeStream *p_j2k, uint8_t *p_header_data,
		uint16_t header_size){
	 uint32_t tot_len = 0;
	uint8_t num_parts = 0;
	uint8_t current_part;
	uint32_t tile_x, tile_y;

	assert(p_j2k != nullptr);

	if (!get_sot_values(p_header_data, header_size,
			&p_j2k->m_tileProcessor->m_current_tile_number, &tot_len,
			&current_part, &num_parts)) {
		GROK_ERROR("Error reading SOT marker");
		return false;
	}
	auto tile_number = p_j2k->m_tileProcessor->m_current_tile_number;

	auto cp = &(p_j2k->m_cp);

	/* testcase 2.pdf.SIGFPE.706.1112 */
	if (tile_number >= cp->t_grid_width * cp->t_grid_height) {
		GROK_ERROR("Invalid tile number %d", tile_number);
		return false;
	}

	auto tcp = &cp->tcps[tile_number];
	tile_x = tile_number % cp->t_grid_width;
	tile_y = tile_number / cp->t_grid_width;

	/* Fixes issue with id_000020,sig_06,src_001958,op_flip4,pos_149 */
	/* of https://github.com/uclouvain/openjpeg/issues/939 */
	/* We must avoid reading the same tile part number twice for a given tile */
	/* to avoid various issues, like grk_j2k_merge_ppt being called several times. */
	/* ISO 15444-1 A.4.2 Start of tile-part (SOT) mandates that tile parts */
	/* should appear in increasing order. */
	if (tcp->m_current_tile_part_number + 1 != (int32_t) current_part) {
		GROK_ERROR("Invalid tile part index for tile number %d. "
				"Got %d, expected %d", tile_number, current_part,
				tcp->m_current_tile_part_number + 1);
		return false;
	}
	++tcp->m_current_tile_part_number;
	/* PSot should be equal to zero or >=14 or <= 2^32-1 */
	if ((tot_len != 0) && (tot_len < 14)) {
		if (tot_len == sot_marker_segment_len) {
			GROK_WARN("Empty SOT marker detected: Psot=%d.", tot_len);
		} else {
			GROK_ERROR(
					"Psot value is not correct regards to the JPEG2000 norm: %d.",
					tot_len);
			return false;
		}
	}

	/* Ref A.4.2: Psot may equal zero if it is the last tile-part of the code stream.*/
	if (!tot_len) {
		//GROK_WARN( "Psot value of the current tile-part is equal to zero; "
		//              "we assume it is the last tile-part of the code stream.");
		p_j2k->m_specific_param.m_decoder.m_last_tile_part = 1;
	}

	// ensure that current tile part number read from SOT marker
	// is not larger than total number of tile parts
	if (tcp->m_nb_tile_parts != 0 && current_part >= tcp->m_nb_tile_parts) {
		/* Fixes https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=2851 */
		GROK_ERROR(
				"Current tile part number (%d) read from SOT marker is greater than total "
						"number of tile-parts (%d).", current_part,
				tcp->m_nb_tile_parts);
		p_j2k->m_specific_param.m_decoder.m_last_tile_part = 1;
		return false;
	}

	if (num_parts != 0) { /* Number of tile-part header is provided
	 by this tile-part header */
		num_parts = (uint8_t) (num_parts
				+ p_j2k->m_tileProcessor->m_nb_tile_parts_correction);
		/* Useful to manage the case of textGBR.jp2 file because two values
		 *  of TNSot are allowed: the correct numbers of
		 * tile-parts for that tile and zero (A.4.2 of 15444-1 : 2002). */
		if (tcp->m_nb_tile_parts) {
			if (current_part >= tcp->m_nb_tile_parts) {
				GROK_ERROR(
						"In SOT marker, TPSot (%d) is not valid regards to the current "
								"number of tile-part (%d), giving up",
						current_part, tcp->m_nb_tile_parts);
				p_j2k->m_specific_param.m_decoder.m_last_tile_part = 1;
				return false;
			}
		}
		if (current_part >= num_parts) {
			/* testcase 451.pdf.SIGSEGV.ce9.3723 */
			GROK_ERROR(
					"In SOT marker, TPSot (%d) is not valid regards to the current "
							"number of tile-part (header) (%d), giving up",
					current_part, num_parts);
			p_j2k->m_specific_param.m_decoder.m_last_tile_part = 1;
			return false;
		}
		tcp->m_nb_tile_parts = num_parts;
	}

	/* If know the number of tile part header we will check if we didn't read the last*/
	if (tcp->m_nb_tile_parts) {
		if (tcp->m_nb_tile_parts == (current_part + 1)) {
			p_j2k->m_specific_param.m_decoder.ready_to_decode_tile_part_data =
					1; /* Process the last tile-part header*/
		}
	}

	if (!p_j2k->m_specific_param.m_decoder.m_last_tile_part) {
		/* Keep the size of data to skip after this marker */
		p_j2k->m_tileProcessor->tile_part_data_length = tot_len
				- sot_marker_segment_len;
	} else {
		/* FIXME: need to be computed from the number of bytes
		 *  remaining in the code stream */
		p_j2k->m_tileProcessor->tile_part_data_length = 0;
	}

	p_j2k->m_specific_param.m_decoder.m_state = J2K_DEC_STATE_TPH;

	/* Check if the current tile is outside the area we want
	 *  decompress or not corresponding to the tile index*/
	if (p_j2k->m_tileProcessor->m_tile_ind_to_dec == -1) {
		p_j2k->m_specific_param.m_decoder.m_skip_data =
				(tile_x < p_j2k->m_specific_param.m_decoder.m_start_tile_x_index)
						|| (tile_x
								>= p_j2k->m_specific_param.m_decoder.m_end_tile_x_index)
						|| (tile_y
								< p_j2k->m_specific_param.m_decoder.m_start_tile_y_index)
						|| (tile_y
								>= p_j2k->m_specific_param.m_decoder.m_end_tile_y_index);
	} else {
		assert(p_j2k->m_tileProcessor->m_tile_ind_to_dec >= 0);
		p_j2k->m_specific_param.m_decoder.m_skip_data = (tile_number
				!= (uint32_t) p_j2k->m_tileProcessor->m_tile_ind_to_dec);
	}

	/* Index */
	if (p_j2k->cstr_index) {
		assert(p_j2k->cstr_index->tile_index != nullptr);
		p_j2k->cstr_index->tile_index[tile_number].tileno = tile_number;
		p_j2k->cstr_index->tile_index[tile_number].current_tpsno = current_part;

		if (num_parts != 0) {
			p_j2k->cstr_index->tile_index[tile_number].nb_tps = num_parts;
			p_j2k->cstr_index->tile_index[tile_number].current_nb_tps =
					num_parts;

			if (!p_j2k->cstr_index->tile_index[tile_number].tp_index) {
				p_j2k->cstr_index->tile_index[tile_number].tp_index =
						(grk_tp_index*) grk_calloc(num_parts,
								sizeof(grk_tp_index));
				if (!p_j2k->cstr_index->tile_index[tile_number].tp_index) {
					GROK_ERROR("Not enough memory to read SOT marker. "
							"Tile index allocation failed");
					return false;
				}
			} else {
				grk_tp_index *new_tp_index = (grk_tp_index*) grk_realloc(
						p_j2k->cstr_index->tile_index[tile_number].tp_index,
						num_parts * sizeof(grk_tp_index));
				if (!new_tp_index) {
					grok_free(
							p_j2k->cstr_index->tile_index[tile_number].tp_index);
					p_j2k->cstr_index->tile_index[tile_number].tp_index =
							nullptr;
					GROK_ERROR("Not enough memory to read SOT marker. "
							"Tile index allocation failed");
					return false;
				}
				p_j2k->cstr_index->tile_index[tile_number].tp_index =
						new_tp_index;
			}
		} else {
			if (!p_j2k->cstr_index->tile_index[tile_number].tp_index) {
				p_j2k->cstr_index->tile_index[tile_number].current_nb_tps = 10;
				p_j2k->cstr_index->tile_index[tile_number].tp_index =
						(grk_tp_index*) grk_calloc(
								p_j2k->cstr_index->tile_index[tile_number].current_nb_tps,
								sizeof(grk_tp_index));
				if (!p_j2k->cstr_index->tile_index[tile_number].tp_index) {
					p_j2k->cstr_index->tile_index[tile_number].current_nb_tps =
							0;
					GROK_ERROR("Not enough memory to read SOT marker. "
							"Tile index allocation failed");
					return false;
				}
			}

			if (current_part
					>= p_j2k->cstr_index->tile_index[tile_number].current_nb_tps) {
				grk_tp_index *new_tp_index;
				p_j2k->cstr_index->tile_index[tile_number].current_nb_tps =
						current_part + 1;
				new_tp_index =
						(grk_tp_index*) grk_realloc(
								p_j2k->cstr_index->tile_index[tile_number].tp_index,
								p_j2k->cstr_index->tile_index[tile_number].current_nb_tps
										* sizeof(grk_tp_index));
				if (!new_tp_index) {
					grok_free(
							p_j2k->cstr_index->tile_index[tile_number].tp_index);
					p_j2k->cstr_index->tile_index[tile_number].tp_index =
							nullptr;
					p_j2k->cstr_index->tile_index[tile_number].current_nb_tps =
							0;
					GROK_ERROR(
							"Not enough memory to read SOT marker. Tile index allocation failed");
					return false;
				}
				p_j2k->cstr_index->tile_index[tile_number].tp_index =
						new_tp_index;
			}
		}

	}
	return true;

 }


} /* namespace grk */
