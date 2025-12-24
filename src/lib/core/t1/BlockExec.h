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
struct BlockExec
{
  BlockExec()
      : tilec(nullptr), bandIndex(0), bandNumbps(0), bandOrientation(BAND_ORIENT_LL), stepsize(0),
        cblk_sty(0), qmfbid(0), x(0), y(0), k_msbs(0), R_b(0)
  {}
  virtual ~BlockExec() = default;
  virtual bool open(ICoder* coder) = 0;
  TileComponent* tilec;
  uint8_t bandIndex;
  uint8_t bandNumbps;
  eBandOrientation bandOrientation;
  float stepsize;
  uint8_t cblk_sty;
  uint8_t qmfbid;
  /* code block offset in buffer coordinates*/
  uint32_t x;
  uint32_t y;
  // missing bit planes for all blocks in band
  uint8_t k_msbs;
  uint8_t R_b;

  // Delete copy constructor and assignment operator
  BlockExec(const BlockExec&) = delete;
  BlockExec& operator=(const BlockExec&) = delete;
};
struct DecompressBlockExec : public BlockExec
{
  DecompressBlockExec(bool cacheCoder)
      : cblk(nullptr), resno(0), roishift(0), cachedCoder_(nullptr), shouldCacheCoder_(cacheCoder),
        finalLayer_(false), uncompressedBuf_(nullptr)
  {}
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

  CodeblockDecompress* cblk;
  uint8_t resno;
  uint8_t roishift;
  ICoder* cachedCoder_;
  bool shouldCacheCoder_;
  bool finalLayer_;
  Buffer2dAligned32* uncompressedBuf_;

  // Delete copy constructor and assignment operator
  DecompressBlockExec(const DecompressBlockExec&) = delete;
  DecompressBlockExec& operator=(const DecompressBlockExec&) = delete;
};
struct CompressBlockExec : public BlockExec
{
  CompressBlockExec()
      : cblk(nullptr), tile(nullptr), doRateControl(false), distortion(0), tiledp(nullptr),
        compno(0), resno(0), precinctIndex(0), inv_step_ht(0), mct_norms(nullptr),
#ifdef DEBUG_LOSSLESS_T1
        unencodedData(nullptr),
#endif
        mct_numcomps(0)
  {}
  ~CompressBlockExec() = default;
  bool open(ICoder* coder)
  {
    return coder->compress(this);
  }
  void close(void) {}
  CodeblockCompress* cblk;
  Tile* tile;
  bool doRateControl;
  double distortion;
  int32_t* tiledp;
  uint16_t compno;
  uint8_t resno;
  uint64_t precinctIndex;
  float inv_step_ht;
  const double* mct_norms;
#ifdef DEBUG_LOSSLESS_T1
  int32_t* unencodedData;
#endif
  uint16_t mct_numcomps;

  // Delete copy constructor and assignment operator
  CompressBlockExec(const CompressBlockExec&) = delete;
  CompressBlockExec& operator=(const CompressBlockExec&) = delete;
};

} // namespace grk
