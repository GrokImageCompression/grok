/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
   bool write(TileProcessor* proc, uint32_t tileLength);
   bool write_psot(BufferedStream* stream, uint32_t tileLength);

   /**
	* Decompress a SOT marker (Start of tile-part)
	*
	* @param       headerData   the data contained in the SOT marker.
	* @param       header_size     the size of the data contained in the PPT marker.

	*/
   bool read(CodeStreamDecompress* codeStream, uint8_t* headerData, uint16_t header_size);

 private:
   uint64_t psot_location_;

   /**
	* Reads values from a SOT marker (Start of tile-part)
	*
	* the j2k decompressor state is not affected. No side effects,
	*  no checks except for header_size.
	*
	* @param       headerData   the data contained in the SOT marker.
	* @param       header_size   the size of the data contained in the SOT marker.
	* @param       tot_len       Psot.
	* @param       current_part  TPsot.
	* @param       num_parts     TNsot.

	*/
   bool read(CodeStreamDecompress* codeStream, uint8_t* headerData, uint32_t header_size,
			 uint32_t* tot_len, uint16_t* tileIndex, uint8_t* current_part, uint8_t* num_parts);
};

} /* namespace grk */
