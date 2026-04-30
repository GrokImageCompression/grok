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

#include <cstdint>
#include <atomic>
#include <functional>

namespace grk
{

struct ITileProcessor;

/**
 * @brief Configuration for strip-based decompression.
 */
struct StripConfig
{
  static constexpr uint32_t outputStripHeight =
      64; // configurable strip height at highest resolution
};

// callback invoked for each completed output strip
using StripOutputCallback =
    std::function<void(uint32_t row0, uint32_t numRows, const void* rowData, uint32_t rowStride)>;

/**
 * @class StripDecompressor
 * @brief Stub — freebyrd thread pool has been removed.
 *
 * The class interface is preserved for compilation but decompress()
 * always returns false.
 */
class StripDecompressor
{
public:
  StripDecompressor(ITileProcessor* tp, uint16_t compno, uint8_t prec, std::atomic_bool& success,
                    StripConfig config = {});
  ~StripDecompressor();

  bool decompress();
  bool decompressStream(StripOutputCallback outputCallback);

private:
  ITileProcessor* tp_;
  uint16_t compno_;
  uint8_t prec_;
  std::atomic_bool& success_;
  StripConfig config_;
};

} // namespace grk
