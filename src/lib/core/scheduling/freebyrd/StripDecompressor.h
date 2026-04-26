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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <array>
#include <atomic>
#include <memory>

#include "buffer.h"
#include "StripPartitioner.h"

namespace grk
{

struct ITileProcessor;
struct CoderPool;

namespace t1
{
struct DecompressBlockExec;
}

/**
 * @brief Configuration for strip-based decompression.
 */
struct StripConfig
{
  static constexpr uint32_t outputStripHeight = 64; // configurable strip height at highest resolution
};

// callback invoked for each completed output strip
// params: row0, numRows, rowData (packed pixels, rowStride elements per row), rowStride, userData
using StripOutputCallback = std::function<void(uint32_t row0, uint32_t numRows,
                                               const void* rowData, uint32_t rowStride)>;

/**
 * @class StripDecompressor
 * @brief Banded (strip-based) decompressor for a single tile component.
 *
 * Processes the tile in horizontal strips from top to bottom, allocating
 * only the sub-band rows needed for each strip at each resolution level.
 * T1 blocks are decoded just-in-time for each strip (blocks at strip
 * boundaries may be decoded twice for adjacent strips).
 *
 * Memory usage per strip: ~20-30 MB for 24k×24k image, vs ~2.25 GiB
 * for full-tile allocation.
 *
 * The output is written into a caller-provided buffer (or future: streamed
 * via callback).
 */
class StripDecompressor
{
public:
  StripDecompressor(ITileProcessor* tp, uint16_t compno, uint8_t prec,
                    std::atomic_bool& success, StripConfig config = {});
  ~StripDecompressor();

  /**
   * @brief Decompress the component strip-by-strip, writing output into
   *        the existing tile buffer. Keeps old path's output destination
   *        so we can compare bit-exact results.
   */
  bool decompress();

  /**
   * @brief Decompress the component strip-by-strip with streamed output.
   *        Does NOT require tile buffer allocation — uses strip-local buffers
   *        and calls the callback for each completed strip.
   */
  bool decompressStream(StripOutputCallback outputCallback);

private:
  /**
   * @brief Per-resolution level info (pre-computed during init).
   */
  struct LevelInfo
  {
    uint32_t width = 0, height = 0;       // this resolution's dimensions
    uint32_t prevWidth = 0, prevHeight = 0; // previous resolution's dimensions
    uint32_t h_sn = 0, h_dn = 0, h_parity = 0; // H-DWT parameters
    uint32_t v_sn = 0, v_dn = 0, v_parity = 0; // V-DWT parameters
  };

  /**
   * @brief Pre-indexed T1 block info for selective decode.
   */
  struct BlockRef
  {
    std::shared_ptr<t1::DecompressBlockExec> block;
    uint32_t bandRelX0 = 0; // block's left column in sub-band-relative coords
    uint32_t bandRelY0 = 0; // block's top row in sub-band-relative coords
    uint32_t bandRelY1 = 0; // block's bottom row (exclusive)
    uint32_t blockWidth = 0;
    uint32_t blockHeight = 0;
    // decoded T1 sample data (int32_t), stored once per block
    std::vector<int32_t> decodedData;
    uint16_t decodedStride = 0;
    // reference counting: how many future strips still need this block's data
    std::atomic<int32_t> refCount{0};
    // true once T1 decode has been performed (decodedData is valid)
    std::atomic<bool> decoded{false};

    BlockRef() = default;
    BlockRef(BlockRef&& o) noexcept
      : block(std::move(o.block)),
        bandRelX0(o.bandRelX0), bandRelY0(o.bandRelY0), bandRelY1(o.bandRelY1),
        blockWidth(o.blockWidth), blockHeight(o.blockHeight),
        decodedData(std::move(o.decodedData)), decodedStride(o.decodedStride),
        refCount(o.refCount.load(std::memory_order_relaxed)),
        decoded(o.decoded.load(std::memory_order_relaxed))
    {}
    BlockRef& operator=(BlockRef&& o) noexcept
    {
      block = std::move(o.block);
      bandRelX0 = o.bandRelX0; bandRelY0 = o.bandRelY0; bandRelY1 = o.bandRelY1;
      blockWidth = o.blockWidth; blockHeight = o.blockHeight;
      decodedData = std::move(o.decodedData); decodedStride = o.decodedStride;
      refCount.store(o.refCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
      decoded.store(o.decoded.load(std::memory_order_relaxed), std::memory_order_relaxed);
      return *this;
    }
  };

  /**
   * @brief Pre-allocated reusable buffers per resolution level.
   * Eliminates thousands of vector alloc/dealloc per strip cycle.
   */
  struct LevelBufs
  {
    // subband buffers for produceRows
    std::vector<uint8_t> ll, hl, lh, hh;
    // DWT synthesis intermediate buffers
    std::vector<uint8_t> splitL, splitH, vOut;
  };

  // -- init helpers --
  void initLevelInfo();
  void buildBlockIndex();
  void computeRefCounts();
  void preAllocateBuffers();

  // -- strip processing --

  // Recursively produce output rows [y0, y1) at resolution resno,
  // writing into destBuf at row offset y0.
  bool produceRows(uint8_t resno, uint32_t y0, uint32_t y1,
                   void* destBuf, uint32_t destStride);

  // Decode T1 blocks that overlap the given sub-band row ranges for a strip.
  // Only decodes blocks not yet decoded (lazy). Submits decode tasks to pool
  // and waits for completion.
  void decodeBlocksForStrip(uint8_t resno, uint32_t y0, uint32_t y1);

  // Release decoded data for blocks whose refCount has dropped to zero.
  void releaseBlocksForStrip(uint8_t resno, uint32_t y0, uint32_t y1);

  // Copy decoded block data to a sub-band strip buffer for the given row range.
  void copyBlocksToBand(uint8_t resno, uint8_t orient,
                        uint32_t rowStart, uint32_t rowEnd,
                        void* bandBuf, uint32_t bandBufStride,
                        uint32_t bandBufRowOffset);

  // Binary search: find range [first, last) of blocks overlapping [rowStart, rowEnd)
  static std::pair<size_t, size_t> findBlockRange(
      const std::vector<BlockRef>& blocks, uint32_t rowStart, uint32_t rowEnd);

  // Run separate H+V DWT synthesis for 5/3 on a strip at resolution resno.
  bool synthesizeStrip53(uint8_t resno, uint32_t outY0, uint32_t outY1,
                         Buffer2dSimple<int32_t> winLL, Buffer2dSimple<int32_t> winHL,
                         Buffer2dSimple<int32_t> winLH, Buffer2dSimple<int32_t> winHH,
                         SubbandRange rangeL, SubbandRange rangeH, uint32_t extFirst,
                         void* destBuf, uint32_t destStride);

  // Run cascade DWT synthesis for 9/7 on a strip at resolution resno.
  bool synthesizeStrip97(uint8_t resno, uint32_t outY0, uint32_t outY1,
                         Buffer2dSimple<float> winLL, Buffer2dSimple<float> winHL,
                         Buffer2dSimple<float> winLH, Buffer2dSimple<float> winHH,
                         SubbandRange rangeL, SubbandRange rangeH, uint32_t extFirst,
                         void* destBuf, uint32_t destStride);

  // Run 16-bit DWT synthesis on a strip at resolution resno.
  bool synthesizeStrip16(uint8_t resno, uint32_t outY0, uint32_t outY1,
                         Buffer2dSimple<int16_t> winLL, Buffer2dSimple<int16_t> winHL,
                         Buffer2dSimple<int16_t> winLH, Buffer2dSimple<int16_t> winHH,
                         SubbandRange rangeL, SubbandRange rangeH, uint32_t extFirst,
                         void* destBuf, uint32_t destStride);

  // -- data --
  ITileProcessor* tp_;
  uint16_t compno_;
  uint8_t prec_;
  std::atomic_bool& success_;
  StripConfig config_;
  CoderPool coderPool_;

  // resolution level info
  std::vector<LevelInfo> levels_;
  uint8_t numRes_ = 0;
  uint8_t qmfbid_ = 0;
  bool is16Bit_ = false;

  // pre-indexed blocks: [resno][orient] → sorted by bandRelY0
  std::vector<std::array<std::vector<BlockRef>, 4>> blockIndex_;

  // pre-allocated reusable buffers per resolution (allocated once, reused per strip)
  std::vector<LevelBufs> levelBufs_;
  size_t elemSize_ = 0;
};

} // namespace grk
