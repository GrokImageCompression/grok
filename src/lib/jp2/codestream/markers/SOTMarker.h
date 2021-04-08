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

#pragma once

namespace grk
{
struct TileProcessor;
class CodeStreamCompress;
class CodeStreamDecompress;

class SOTMarker
{
  public:
	SOTMarker(void);

	/**
	 * Writes the SOT marker (Start of tile-part)
	 *
	 */
	bool write(TileProcessor *proc);
	bool write_psot(IBufferedStream* stream, uint32_t tilePartBytesWritten);

	/**
	 * Decompress a SOT marker (Start of tile-part)
	 *
	 * @param       p_header_data   the data contained in the SOT marker.
	 * @param       header_size     the size of the data contained in the PPT marker.

	 */
	bool read(CodeStreamDecompress* codeStream, uint8_t* p_header_data, uint16_t header_size);

	/**
	 * Reads values from a SOT marker (Start of tile-part)
	 *
	 * the j2k decompressor state is not affected. No side effects,
	 *  no checks except for header_size.
	 *
	 * @param       p_header_data   the data contained in the SOT marker.
	 * @param       header_size   the size of the data contained in the SOT marker.
	 * @param       tile_no       Isot.
	 * @param       p_tot_len       Psot.
	 * @param       p_current_part  TPsot.
	 * @param       p_num_parts     TNsot.

	 */
	bool get_sot_values(CodeStreamDecompress* codeStream, uint8_t* p_header_data,
						uint32_t header_size, uint16_t* tile_no, uint32_t* p_tot_len,
						uint8_t* p_current_part, uint8_t* p_num_parts);

  private:
	uint64_t m_psot_location;
};

} /* namespace grk */
