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

#include "StripDecompressor.h"

namespace grk
{

struct ITileProcessor;
struct CoderPool;

namespace t1
{
  struct DecompressBlockExec;
}

/**
 * @class SchedulerFreebyrd
 * @brief Strip-based decompression scheduler using freebyrd thread pool.
 *
 * Unlike the Taskflow-based CodecScheduler, this scheduler does not inherit
 * from FlowComponent. It drives strip-based T1 decode → cascade DWT → output
 * using freebyrd's dependency_gate and task_domain primitives.
 *
 * Phase 1: Full tile alloc, strip decode + cascade DWT via freebyrd pool.
 *   - T1 decode all code blocks in parallel
 *   - Cascade DWT per strip (reusing WaveletReverse::cascade_strip_97)
 *   - DC shift fused into last-level DWT
 *
 * Phase 3 (future): Strip-local buffer allocation for low RSS.
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
   * @brief Schedule and execute decompression for a tile.
   *
   * Drives the full pipeline synchronously using freebyrd pool:
   *   1. Build block list per resolution (same iteration as DecompressScheduler)
   *   2. T1 decode all blocks in parallel via freebyrd
   *   3. Cascade DWT per strip per resolution (bottom-up)
   *   4. Post-processing (DC shift fused into DWT for 9/7 whole-tile)
   *
   * @param tileProcessor the tile to decompress
   * @return true if decompression succeeded
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
  CoderPool coderPool_;

  // Block list per component, per resolution
  using BlockList = std::vector<std::shared_ptr<t1::DecompressBlockExec>>;
  std::vector<std::vector<BlockList>> blocksByComp_; // [compno][resIdx] → blocks

  CompStripCallback stripOutputCallback_;
};

} // namespace grk
