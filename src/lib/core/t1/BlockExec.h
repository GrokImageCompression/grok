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

namespace grk::t1
{

struct BlockExec
{
  BlockExec() = default;
  virtual ~BlockExec() = default;
  virtual bool open(ICoder* coder) = 0;
  uint8_t bandIndex = 0;
  uint8_t bandNumbps = 0;
  eBandOrientation bandOrientation = BAND_ORIENT_LL;
  float stepsize = 0;
  uint8_t cblk_sty = 0;
  uint8_t qmfbid = 0;
  /* code block offset in buffer coordinates*/
  uint32_t x = 0;
  uint32_t y = 0;
  // missing bit planes for all blocks in band
  uint8_t k_msbs = 0;
  uint8_t R_b = 0;

  // Delete copy constructor and assignment operator
  BlockExec(const BlockExec&) = delete;
  BlockExec& operator=(const BlockExec&) = delete;
};

struct DecompressBlockExec;

template<typename T>
using DecompressBlockPostProcessor =
    std::function<void(T* srcData, DecompressBlockExec* block, uint16_t stride)>;

struct DecompressBlockExec : public BlockExec
{
  DecompressBlockExec(bool cacheCoder) : shouldCacheCoder_(cacheCoder) {}
  ~DecompressBlockExec()
  {
    delete cachedCoder_;
    delete uncompressedBuf_;
  }
  bool open(ICoder* coder) override
  {
    auto activeCoder = cachedCoder_ ? cachedCoder_ : coder;
    if(!activeCoder)
      return false;
    bool rc = activeCoder->decompress(this);
    if(needsCachedCoder())
      cachedCoder_ = coder;

    return rc;
  }

  bool needsCachedCoder(void)
  {
    return shouldCacheCoder_ && !cachedCoder_;
  }

  bool hasCachedCoder(void)
  {
    return cachedCoder_ != nullptr;
  }

  void clearCachedCoder(void)
  {
    delete cachedCoder_;
    cachedCoder_ = nullptr;
  }
  DecompressBlockPostProcessor<int32_t> postProcessor_;
  CodeblockDecompress* cblk = nullptr;
  uint8_t resno = 0;
  uint8_t roishift = 0;
  ICoder* cachedCoder_ = nullptr;
  bool shouldCacheCoder_ = false;
  bool finalLayer_ = false;
  Buffer2dAligned32* uncompressedBuf_ = nullptr;

  // Delete copy constructor and assignment operator
  DecompressBlockExec(const DecompressBlockExec&) = delete;
  DecompressBlockExec& operator=(const DecompressBlockExec&) = delete;
};
struct CompressBlockExec : public BlockExec
{
  CompressBlockExec() = default;
  ~CompressBlockExec() = default;
  bool open(ICoder* coder)
  {
    return coder->compress(this);
  }
  void close(void) {}
  CodeblockCompress* cblk = nullptr;
  uint32_t tile_width = 0;
  bool doRateControl = false;
  double distortion = 0;
  int32_t* tiledp = nullptr;
  uint16_t compno = 0;
  uint8_t resno = 0;
  uint8_t level = 0;
  uint64_t precinctIndex = 0;
  float inv_step_ht = 0;
  const double* mct_norms = nullptr;
#ifdef DEBUG_LOSSLESS_T1
  int32_t* unencodedData = nullptr;
#endif
  uint16_t mct_numcomps = 0;

  // Delete copy constructor and assignment operator
  CompressBlockExec(const CompressBlockExec&) = delete;
  CompressBlockExec& operator=(const CompressBlockExec&) = delete;
};

} // namespace grk::t1
