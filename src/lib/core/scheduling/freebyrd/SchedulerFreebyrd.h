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
#include <memory>
#include <vector>
#include <functional>
#include <cstdlib>
#include <string>

#include "StripDecompressor.h"

namespace grk
{

struct ITileProcessor;

namespace t1
{
  struct DecompressBlockExec;
}

/**
 * @class SchedulerFreebyrd
 * @brief Stub — freebyrd thread pool has been removed.
 *
 * The class interface is preserved so TileProcessor still compiles,
 * but decompressTile() always returns false with an error message.
 */
class SchedulerFreebyrd
{
public:
  SchedulerFreebyrd(uint16_t numcomps, uint8_t prec);
  ~SchedulerFreebyrd();

  // check for strip-based decompression mode via GRK_STRIP env var
  static bool isStripMode()
  {
    const char* env = std::getenv("GRK_STRIP");
    return env && std::string(env) == "1";
  }

  /**
   * @brief Stub — always returns false (freebyrd removed).
   */
  bool decompressTile(ITileProcessor* tileProcessor);

  // set per-component strip output callback for streaming mode
  // callback receives: (compno, row0, numRows, rowData, rowStride)
  using CompStripCallback = std::function<void(uint16_t compno, uint32_t row0, uint32_t numRows,
                                               const void* rowData, uint32_t rowStride)>;
  void setStripOutputCallback(CompStripCallback cb)
  {
    stripOutputCallback_ = std::move(cb);
  }
  bool hasStripOutput() const
  {
    return !!stripOutputCallback_;
  }

  void release();

private:
  bool decodeBlocks(ITileProcessor* tileProcessor);
  bool runDWT(ITileProcessor* tileProcessor);
  bool runCascadeDWT97(ITileProcessor* tileProcessor, uint16_t compno);
  bool runSeparateDWT53(ITileProcessor* tileProcessor, uint16_t compno);
  bool runSeparateDWT16(ITileProcessor* tileProcessor, uint16_t compno);
  bool postProcess(ITileProcessor* tileProcessor);

  uint16_t numcomps_;
  uint8_t prec_;
  std::atomic_bool success_;

  // Block list per component, per resolution
  using BlockList = std::vector<std::shared_ptr<t1::DecompressBlockExec>>;
  std::vector<std::vector<BlockList>> blocksByComp_; // [compno][resIdx] → blocks

  CompStripCallback stripOutputCallback_;
};

} // namespace grk
