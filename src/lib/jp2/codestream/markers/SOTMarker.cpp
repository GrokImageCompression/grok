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

namespace grk {

SOTMarker::SOTMarker(void) : m_psot_location(0)
{
}
bool SOTMarker::write_psot(CodeStreamCompress *codeStream,
							uint32_t tile_part_bytes_written) {
	auto stream = codeStream->getStream();
	auto currentLocation = stream->tell();
	stream->seek(m_psot_location);
	if (!stream->write_int(tile_part_bytes_written))
		return false;
	stream->seek(currentLocation);

	return true;
}

bool SOTMarker::write(CodeStreamCompress *codeStream){
	auto stream = codeStream->getStream();
	auto proc = codeStream->currentProcessor();

	/* SOT */
	if (!stream->write_short(J2K_MS_SOT))
		return false;

	/* Lsot */
	if (!stream->write_short(10))
		return false;
	/* Isot */
	if (!stream->write_short(
			(uint16_t) proc->m_tile_index))
		return false;

	/* Psot  */
	m_psot_location = stream->tell();
	if (!stream->skip(4))
		return false;

	/* TPsot */
	if (!stream->write_byte(proc->m_tile_part_index))
		return false;

	/* TNsot */
	if (!stream->write_byte(
			codeStream->getCodingParams()->tcps[proc->m_tile_index].m_nb_tile_parts))
		return false;

	return true;
}

bool SOTMarker::get_sot_values(CodeStreamDecompress *codeStream,
		uint8_t *p_header_data, uint32_t header_size,
		uint16_t *tile_no, uint32_t *p_tot_len, uint8_t *p_current_part,
		uint8_t *p_num_parts){

	assert(p_header_data != nullptr);
	if (header_size != sot_marker_segment_len - grk_marker_length) {
		GRK_ERROR("Error reading SOT marker");
		return false;
	}
	uint32_t len;
	uint16_t tile_index;
	uint8_t tile_part_index, num_tile_parts;
	grk_read<uint16_t>(p_header_data, &tile_index);
	p_header_data += 2;
	grk_read<uint32_t>(p_header_data, &len);
	p_header_data += 4;
	grk_read<uint8_t>(p_header_data++, &tile_part_index);
	grk_read<uint8_t>(p_header_data++, &num_tile_parts);

	if (num_tile_parts && (tile_part_index == num_tile_parts)){
		GRK_ERROR("Tile part index (%d) is not less than number of tile parts (%d)",tile_part_index,  num_tile_parts);
		return false;
	}

	if (!codeStream->allocateProcessor(tile_index))
		return false;
	if (tile_no)
		*tile_no = tile_index;
	*p_tot_len = len;
	*p_current_part = tile_part_index;
	*p_num_parts = num_tile_parts;

	return true;
}

 bool SOTMarker::read(CodeStreamDecompress *codeStream,
		 	 	 	 	 uint8_t *p_header_data,uint16_t header_size){
	uint32_t tot_len = 0;
	uint8_t num_parts = 0;
	uint8_t current_part;
	uint32_t tile_x, tile_y;

	if (!get_sot_values(codeStream,
						p_header_data,
						header_size,
						nullptr,
						&tot_len,
						&current_part,
						&num_parts)) {
		GRK_ERROR("Error reading SOT marker");
		return false;
	}
	auto tile_number = codeStream->currentProcessor()->m_tile_index;
	auto cp = codeStream->getCodingParams();

	/* testcase 2.pdf.SIGFPE.706.1112 */
	if (tile_number >= cp->t_grid_width * cp->t_grid_height) {
		GRK_ERROR("Invalid tile number %u", tile_number);
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
		GRK_ERROR("Invalid tile part index for tile number %u. "
				"Got %u, expected %u", tile_number, current_part,
				tcp->m_tile_part_index + 1);
		return false;
	}
	++tcp->m_tile_part_index;
	/* PSot should be equal to zero or >=14 or <= 2^32-1 */
	if ((tot_len != 0) && (tot_len < 14)) {
		if (tot_len == sot_marker_segment_len) {
			GRK_WARN("Empty SOT marker detected: Psot=%u.", tot_len);
		} else {
			GRK_ERROR(
					"Psot value is not correct regards to the JPEG2000 norm: %u.",
					tot_len);
			return false;
		}
	}

	/* Ref A.4.2: Psot may equal zero if it is the last tile-part of the code stream.*/
	if (!tot_len) {
		codeStream->getDecompressorState()->m_last_tile_part_in_code_stream = true;
	}

	// ensure that current tile part number read from SOT marker
	// is not larger than total number of tile parts
	if (tcp->m_nb_tile_parts != 0 && current_part >= tcp->m_nb_tile_parts) {
		/* Fixes https://bugs.chromium.org/p/oss-fuzz/issues/detail?id=2851 */
		GRK_ERROR(
				"Current tile part number (%u) read from SOT marker is greater\n than total "
						"number of tile-parts (%u).", current_part,	tcp->m_nb_tile_parts);
		codeStream->getDecompressorState()->m_last_tile_part_in_code_stream = true;
		return false;
	}

	if (num_parts != 0) { /* Number of tile-part header is provided by this tile-part header */
		/* Useful to manage the case of textGBR.jp2 file because two values
		 *  of TNSot are allowed: the correct numbers of
		 * tile-parts for that tile and zero (A.4.2 of 15444-1 : 2002). */
		if (tcp->m_nb_tile_parts) {
			if (current_part >= tcp->m_nb_tile_parts) {
				GRK_ERROR("In SOT marker, TPSot (%u) is not valid with regards to the current "
								"number of tile-part (%u)",
						current_part, tcp->m_nb_tile_parts);
				codeStream->getDecompressorState()->m_last_tile_part_in_code_stream = true;
				return false;
			}
		}
		if (current_part >= num_parts) {
			/* testcase 451.pdf.SIGSEGV.ce9.3723 */
			GRK_ERROR("In SOT marker, TPSot (%u) is not valid with regards to the current "
							"number of tile-part (header) (%u)",current_part, num_parts);
			codeStream->getDecompressorState()->m_last_tile_part_in_code_stream = true;
			return false;
		}
		tcp->m_nb_tile_parts = num_parts;
	}

	/* If we know the number of tile part header we check whether we have read the last one*/
	if (tcp->m_nb_tile_parts && (tcp->m_nb_tile_parts == (current_part + 1))) {
		/* indicate that we are now ready to read the tile data */
		codeStream->getDecompressorState()->last_tile_part_was_read =	true;
	}

	if (!codeStream->getDecompressorState()->m_last_tile_part_in_code_stream) {
		/* Keep the size of data to skip after this marker */
		codeStream->currentProcessor()->tile_part_data_length = tot_len - sot_marker_segment_len;
	} else {
		codeStream->currentProcessor()->tile_part_data_length = 0;
	}

	codeStream->getDecompressorState()->setState(J2K_DEC_STATE_TPH);

	/* Check if the current tile is outside the area we want
	 *  to decompress or not, corresponding to the tile index*/
	if (codeStream->tileIndexToDecode() == -1) {
		codeStream->getDecompressorState()->m_skip_tile_data =
						   (tile_x < codeStream->getDecompressorState()->m_start_tile_x_index)
						|| (tile_x	>= codeStream->getDecompressorState()->m_end_tile_x_index)
						|| (tile_y	< codeStream->getDecompressorState()->m_start_tile_y_index)
						|| (tile_y	>= codeStream->getDecompressorState()->m_end_tile_y_index);
	} else {
		codeStream->getDecompressorState()->m_skip_tile_data = (tile_number
				!= (uint32_t) codeStream->tileIndexToDecode());
	}

	/* Index */
	auto index = codeStream->getIndex();
	if (index) {
		assert(index->tile_index != nullptr);
		index->tile_index[tile_number].tileno = tile_number;
		index->tile_index[tile_number].current_tpsno = current_part;

		if (num_parts != 0) {
			index->tile_index[tile_number].nb_tps = num_parts;
			index->tile_index[tile_number].current_nb_tps =	num_parts;

			if (!index->tile_index[tile_number].tp_index) {
				index->tile_index[tile_number].tp_index =
						(grk_tp_index*) grkCalloc(num_parts,sizeof(grk_tp_index));
				if (!index->tile_index[tile_number].tp_index) {
					GRK_ERROR("Not enough memory to read SOT marker. "
							"Tile index allocation failed");
					return false;
				}
			} else {
				auto new_tp_index = (grk_tp_index*) grkRealloc(
						index->tile_index[tile_number].tp_index,
						num_parts * sizeof(grk_tp_index));
				if (!new_tp_index) {
					grkFree(index->tile_index[tile_number].tp_index);
					index->tile_index[tile_number].tp_index =
							nullptr;
					GRK_ERROR("Not enough memory to read SOT marker. "
							"Tile index allocation failed");
					return false;
				}
				index->tile_index[tile_number].tp_index =
						new_tp_index;
			}
		} else {
			if (!index->tile_index[tile_number].tp_index) {
				index->tile_index[tile_number].current_nb_tps = 10;
				index->tile_index[tile_number].tp_index =
						(grk_tp_index*) grkCalloc(
								index->tile_index[tile_number].current_nb_tps,
								sizeof(grk_tp_index));
				if (!index->tile_index[tile_number].tp_index) {
					index->tile_index[tile_number].current_nb_tps =	0;
					GRK_ERROR("Not enough memory to read SOT marker. "
							"Tile index allocation failed");
					return false;
				}
			}

			if (current_part
					>= index->tile_index[tile_number].current_nb_tps) {
				grk_tp_index *new_tp_index;
				index->tile_index[tile_number].current_nb_tps =	current_part + 1U;
				new_tp_index =
						(grk_tp_index*) grkRealloc(index->tile_index[tile_number].tp_index,
								index->tile_index[tile_number].current_nb_tps
										* sizeof(grk_tp_index));
				if (!new_tp_index) {
					grkFree(index->tile_index[tile_number].tp_index);
					index->tile_index[tile_number].tp_index =
							nullptr;
					index->tile_index[tile_number].current_nb_tps =	0;
					GRK_ERROR("Not enough memory to read SOT marker. Tile index allocation failed");
					return false;
				}
				index->tile_index[tile_number].tp_index = new_tp_index;
			}
		}
	}

	return true;
 }


} /* namespace grk */
