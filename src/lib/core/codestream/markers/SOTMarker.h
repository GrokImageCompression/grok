/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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
class SOTMarker
{
public:
  /**
   * @brief Constructs a new SOTMarker object
   *
   */
  SOTMarker(void);

  /**
   * @brief Write SOT marker
   *
   * @param compressor compressor
   * @param tilePartLength tile part length
   * @return true if successful
   */
  bool write(TileProcessorCompress* compressor, uint32_t tilePartLength);

  /**
   * @brief write psot i.e. tile part length
   *
   * @param stream @ref IStream
   * @param tileLength tile length
   * @return true if successful
   */
  bool write_psot(IStream* stream, uint32_t tilePartLength);

private:
  uint64_t psot_location_;
};

} /* namespace grk */
