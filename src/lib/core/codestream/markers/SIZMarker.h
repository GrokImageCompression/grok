/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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
 */

#pragma once

namespace grk
{
class CodeStreamDecompress;
class CodeStreamCompress;

class SIZMarker
{
public:
  /**
   * Decompress a SIZ marker (image and tile size)
   * @param       codeStream           JPEG 2000 code stream.
   * @param       headerData   the data contained in the SIZ box.
   * @param       headerSize   the size of the data contained in the SIZ marker.

   */
  bool read(CodeStreamDecompress* codeStream, uint8_t* headerData, uint16_t headerSize);

  /**
   * Write the SIZ marker (image and tile size)
   *
   * @param       codeStream           JPEG 2000 code stream
   * @param       stream        buffered stream.

   */
  bool write(CodeStreamCompress* codeStream, IStream* stream);
};

} // namespace grk
