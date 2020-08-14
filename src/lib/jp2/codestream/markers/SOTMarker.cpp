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

#include "grok_includes.h"

namespace grk {

SOTMarker::SOTMarker(BufferedStream *stream) : m_stream(stream),
		m_psot_location(0) {
}
SOTMarker::SOTMarker(void) : m_stream(nullptr),
		m_psot_location(0){
}

bool SOTMarker::write_psot(uint32_t tile_part_bytes_written) {
	auto currentLocation = m_stream->tell();
	m_stream->seek(m_psot_location);
	if (!m_stream->write_int(tile_part_bytes_written))
		return false;
	m_stream->seek(currentLocation);

	return true;
}

bool SOTMarker::write(CodeStream *codeStream, TileProcessor *tileProcessor){
	assert(codeStream != nullptr);

	/* SOT */
	if (!m_stream->write_short(J2K_MS_SOT))
		return false;

	/* Lsot */
	if (!m_stream->write_short(10))
		return false;
	/* Isot */
	if (!m_stream->write_short(
			(uint16_t) tileProcessor->m_tile_index))
		return false;

	/* Psot  */
	m_psot_location = m_stream->tell();
	if (!m_stream->skip(4))
		return false;

	/* TPsot */
	if (!m_stream->write_byte(tileProcessor->m_tile_part_index))
		return false;

	/* TNsot */
	if (!m_stream->write_byte(
			codeStream->m_cp.tcps[tileProcessor->m_tile_index].m_nb_tile_parts))
		return false;

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
	grk_read<uint32_t>(p_header_data++, &temp, 1);
	*p_current_part = (uint8_t) temp;
	/* TNsot */
	grk_read<uint32_t>(p_header_data++, &temp, 1);
	*p_num_parts = (uint8_t) temp;

	return true;
}

 bool SOTMarker::read(CodeStream *codeStream, TileProcessor *tileProcessor, uint8_t *p_header_data,
		uint16_t header_size){
	uint32_t tot_len = 0;
	uint8_t num_parts = 0;
	uint8_t current_part;
	uint32_t tile_x, tile_y;

	assert(codeStream != nullptr);

	if (!get_sot_values(p_header_data, header_size,
			&tileProcessor->m_tile_index, &tot_len,
			&current_part, &num_parts)) {
		GROK_ERROR("Error reading SOT marker");
		return false;
	}
	auto tile_number = tileProcessor->m_tile_index;

	auto cp = &(codeStream->m_cp);

	/* testcase 2.pdf.SIGFPE.706.1112 */
	if (tile_number >= cp->t_grid_width * cp->t_grid_height) {
		GROK_ERROR("Invalid tile number %u", tile_number);
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
	if (tcp->m_tile_part_index + 1 != (int32_t) current_part) {
		GROK_ERROR("Invalid tile part index for tile number %u. "
				"Got %u, expected %u", tile_number, current_part,
				tcp->m_tile_part_index + 1);
		return false;
	}
	++tcp->m_tile_part_index;
	/* PSot should be equal to zero or >=14 or <= 2^32-1 */
	if ((tot_len != 0) && (tot_len < 14)) {
		if (tot_len == sot_marker_segment_len) {
			GROK_WARN("Empty SOT marker detected: Psot=%u.", tot_len);
		} else {
			GROK_ERROR(
					"Psot value is not correct regards to the JPEG2000 norm: %u.",
					tot_len);
			return false;
		}
	}

	/* Ref A.4.2: Psot may equal zero if it is the last tile-part of the code stream.*/
	if (!tot_len) {
		//GROK_WARN( "Psot value of the current tile-part is equal to zero; "
		//              "we assume it is the last tile-part of the code stream.");
		codeStream->m_decoder.m_last_tile_part = 1;
	}

	// ensure that current tile part number read from SOT marker
	// is not larger than total number of tile parts
	if (tcp->m_nb_tile_parts != 0 && current_part >= tcp->m_nb_tile_parts) {
		/* Fixes https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=2851 */
		GROK_ERROR(
				"Current tile part number (%u) read from SOT marker is greater\n than total "
						"number of tile-parts (%u).", current_part,
				tcp->m_nb_tile_parts);
		codeStream->m_decoder.m_last_tile_part = 1;
		return false;
	}

	if (num_parts != 0) { /* Number of tile-part header is provided
	 by this tile-part header */
		num_parts = (uint8_t) (num_parts
				+ codeStream->m_nb_tile_parts_correction);
		/* Useful to manage the case of textGBR.jp2 file because two values
		 *  of TNSot are allowed: the correct numbers of
		 * tile-parts for that tile and zero (A.4.2 of 15444-1 : 2002). */
		if (tcp->m_nb_tile_parts) {
			if (current_part >= tcp->m_nb_tile_parts) {
				GROK_ERROR(
						"In SOT marker, TPSot (%u) is not valid with regards to the current "
								"number of tile-part (%u)",
						current_part, tcp->m_nb_tile_parts);
				codeStream->m_decoder.m_last_tile_part = 1;
				return false;
			}
		}
		if (current_part >= num_parts) {
			/* testcase 451.pdf.SIGSEGV.ce9.3723 */
			GROK_ERROR(
					"In SOT marker, TPSot (%u) is not valid with regards to the current "
							"number of tile-part (header) (%u)",
					current_part, num_parts);
			codeStream->m_decoder.m_last_tile_part = 1;
			return false;
		}
		tcp->m_nb_tile_parts = num_parts;
	}

	/* If know the number of tile part header we will check if we didn't read the last*/
	if (tcp->m_nb_tile_parts) {
		if (tcp->m_nb_tile_parts == (current_part + 1)) {
			codeStream->m_decoder.ready_to_decode_tile_part_data =
					true; /* Process the last tile-part header*/
		}
	}

	if (!codeStream->m_decoder.m_last_tile_part) {
		/* Keep the size of data to skip after this marker */
		tileProcessor->tile_part_data_length = tot_len
				- sot_marker_segment_len;
	} else {
		/* FIXME: need to be computed from the number of bytes
		 *  remaining in the code stream */
		tileProcessor->tile_part_data_length = 0;
	}

	codeStream->m_decoder.m_state = J2K_DEC_STATE_TPH;

	/* Check if the current tile is outside the area we want
	 *  decompress or not corresponding to the tile index*/
	if (codeStream->m_tile_ind_to_dec == -1) {
		codeStream->m_decoder.m_skip_data =
				(tile_x < codeStream->m_decoder.m_start_tile_x_index)
						|| (tile_x
								>= codeStream->m_decoder.m_end_tile_x_index)
						|| (tile_y
								< codeStream->m_decoder.m_start_tile_y_index)
						|| (tile_y
								>= codeStream->m_decoder.m_end_tile_y_index);
	} else {
		codeStream->m_decoder.m_skip_data = (tile_number
				!= (uint32_t) codeStream->m_tile_ind_to_dec);
	}

	/* Index */
	if (codeStream->cstr_index) {
		assert(codeStream->cstr_index->tile_index != nullptr);
		codeStream->cstr_index->tile_index[tile_number].tileno = tile_number;
		codeStream->cstr_index->tile_index[tile_number].current_tpsno = current_part;

		if (num_parts != 0) {
			codeStream->cstr_index->tile_index[tile_number].nb_tps = num_parts;
			codeStream->cstr_index->tile_index[tile_number].current_nb_tps =
					num_parts;

			if (!codeStream->cstr_index->tile_index[tile_number].tp_index) {
				codeStream->cstr_index->tile_index[tile_number].tp_index =
						(grk_tp_index*) grk_calloc(num_parts,
								sizeof(grk_tp_index));
				if (!codeStream->cstr_index->tile_index[tile_number].tp_index) {
					GROK_ERROR("Not enough memory to read SOT marker. "
							"Tile index allocation failed");
					return false;
				}
			} else {
				auto new_tp_index = (grk_tp_index*) grk_realloc(
						codeStream->cstr_index->tile_index[tile_number].tp_index,
						num_parts * sizeof(grk_tp_index));
				if (!new_tp_index) {
					grk_free(
							codeStream->cstr_index->tile_index[tile_number].tp_index);
					codeStream->cstr_index->tile_index[tile_number].tp_index =
							nullptr;
					GROK_ERROR("Not enough memory to read SOT marker. "
							"Tile index allocation failed");
					return false;
				}
				codeStream->cstr_index->tile_index[tile_number].tp_index =
						new_tp_index;
			}
		} else {
			if (!codeStream->cstr_index->tile_index[tile_number].tp_index) {
				codeStream->cstr_index->tile_index[tile_number].current_nb_tps = 10;
				codeStream->cstr_index->tile_index[tile_number].tp_index =
						(grk_tp_index*) grk_calloc(
								codeStream->cstr_index->tile_index[tile_number].current_nb_tps,
								sizeof(grk_tp_index));
				if (!codeStream->cstr_index->tile_index[tile_number].tp_index) {
					codeStream->cstr_index->tile_index[tile_number].current_nb_tps =
							0;
					GROK_ERROR("Not enough memory to read SOT marker. "
							"Tile index allocation failed");
					return false;
				}
			}

			if (current_part
					>= codeStream->cstr_index->tile_index[tile_number].current_nb_tps) {
				grk_tp_index *new_tp_index;
				codeStream->cstr_index->tile_index[tile_number].current_nb_tps =
						current_part + 1;
				new_tp_index =
						(grk_tp_index*) grk_realloc(
								codeStream->cstr_index->tile_index[tile_number].tp_index,
								codeStream->cstr_index->tile_index[tile_number].current_nb_tps
										* sizeof(grk_tp_index));
				if (!new_tp_index) {
					grk_free(
							codeStream->cstr_index->tile_index[tile_number].tp_index);
					codeStream->cstr_index->tile_index[tile_number].tp_index =
							nullptr;
					codeStream->cstr_index->tile_index[tile_number].current_nb_tps =
							0;
					GROK_ERROR(
							"Not enough memory to read SOT marker. Tile index allocation failed");
					return false;
				}
				codeStream->cstr_index->tile_index[tile_number].tp_index =
						new_tp_index;
			}
		}

	}

	return true;
 }


} /* namespace grk */
