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

#pragma once

namespace grk {

struct TileProcessor;

class SOTMarker {
public:

	SOTMarker(CodeStream *stream);

	/**
	 * Writes the SOT marker (Start of tile-part)
	 *
	 */
	bool write(void);

	bool write_psot(uint32_t tile_part_bytes_written);

	/**
	 * Decode a SOT marker (Start of tile-part)
	 *
	 * @param       p_header_data   the data contained in the SOT marker.
	 * @param       header_size     the size of the data contained in the PPT marker.

	 */
	 bool read(uint8_t *p_header_data,
			uint16_t header_size);

	 /**
	  * Reads values from a SOT marker (Start of tile-part)
	  *
	  * the j2k decoder state is not affected. No side effects,
	  *  no checks except for header_size.
	  *
	  * @param       p_header_data   the data contained in the SOT marker.
	  * @param       header_size   the size of the data contained in the SOT marker.
	  * @param       tile_no       Isot.
	  * @param       p_tot_len       Psot.
	  * @param       p_current_part  TPsot.
	  * @param       p_num_parts     TNsot.

	  */
	 bool get_sot_values(uint8_t *p_header_data, uint32_t header_size,
	 		uint16_t *tile_no, uint32_t *p_tot_len, uint8_t *p_current_part,
	 		uint8_t *p_num_parts);
private:
	 CodeStream *m_codeStream;
	 uint64_t m_psot_location;

};

} /* namespace grk */

