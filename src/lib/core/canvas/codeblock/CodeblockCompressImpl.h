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

#include "CodeblockImpl.h"
const uint8_t grk_cblk_enc_compressed_data_pad_left = 2;

namespace grk::t1
{

/**
 * @struct CodePass
 * @brief Information about compression/decompression coding pass
 */
struct CodePass
{
  /**
   * @brief Constructs a CodePass
   */
  CodePass() : rate_(0), distortiondec_(0), len_(0), term_(false), slope_(0) {}
  /**
   * @brief Destroys a CodePass
   */
  ~CodePass() = default;
  /**
   * @brief total rate of block up to and including this pass
   */
  uint16_t rate_;
  /**
   * @brief distortion decrease of pass
   */
  double distortiondec_;
  /**
   * @brief length of pass in bytes
   */
  uint16_t len_;

  /**
   * @brief True if this pass terminates a segment, otherwise false
   */
  bool term_;

  /**
   * @brief ln(slope) in 8.8 fixed point
   */
  uint16_t slope_;
};

/**
 * @struct Layer
 * @brief Store information on quality layer
 */
struct Layer
{
  /**
   * @brief Constructs a Layer
   */
  Layer() : totalPasses_(0), len(0), distortion(0), data(nullptr) {}
  /**
   * @brief Destroys a Layer
   */
  ~Layer() = default;
  /**
   * @brief Number of passes in the layer
   */
  uint8_t totalPasses_;
  /**
   * @brief Number of bytes in layer
   */
  uint16_t len;
  /**
   * @brief Layer distortion decrease
   */
  double distortion;
  /**
   * @brief Compressed layer data
   */
  uint8_t* data;
};

struct CodeblockCompressImpl : public CodeblockImpl
{
  explicit CodeblockCompressImpl(uint16_t numLayers)
      : CodeblockImpl(numLayers), paddedCompressedStream(nullptr), layers(nullptr), passes(nullptr),
        numPassesInPreviousPackets(0), totalPasses_(0)
#ifdef PLUGIN_DEBUG_ENCODE
        ,
        context_stream(nullptr)
#endif
  {}
  ~CodeblockCompressImpl()
  {
    delete[] layers;
    delete[] passes;
  }
  void init()
  {
    CodeblockImpl::init();
    if(!layers)
      layers = new Layer[numLayers_];
    if(!passes)
      passes = new CodePass[3 * 32 - 2];
  }
  /**
   * Allocates data memory for a compression code block.
   * We actually allocate 2 more bytes than specified, and then offset data by +2.
   * This is done so that we can safely initialize the MQ coder pointer to data-1,
   * without risk of accessing uninitialized memory.
   */
  bool allocData(size_t nominalBlockSize)
  {
    uint32_t desired_data_size = (uint32_t)(nominalBlockSize * sizeof(uint32_t));
    // we add two fake zero bytes at beginning of buffer, so that mq coder
    // can be initialized to data[-1] == actualData[1], and still point
    // to a valid memory location
    auto buf = new uint8_t[desired_data_size + grk_cblk_enc_compressed_data_pad_left];
    buf[0] = 0;
    buf[1] = 0;

    paddedCompressedStream = buf + grk_cblk_enc_compressed_data_pad_left;
    compressedStream.set_buf(buf, desired_data_size);
    compressedStream.set_owns_data(true);

    return true;
  }
  CodePass* getPass(uint8_t passno)
  {
    return passes + passno;
  }
  uint8_t getNumPasses(void) const
  {
    return totalPasses_;
  }
  void setNumPasses(uint8_t numPasses)
  {
    totalPasses_ = numPasses;
  }
  CodePass* getLastPass(void)
  {
    return passes + totalPasses_ - 1;
  }
  Layer* getLayer(uint16_t layno)
  {
    return layers + layno;
  }
  uint8_t* getPaddedCompressedStream(void)
  {
    return paddedCompressedStream;
  }
  void setPaddedCompressedStream(uint8_t* stream)
  {
    paddedCompressedStream = stream;
  }
  uint8_t getNumPassesInPreviousLayers(void)
  {
    return numPassesInPreviousPackets;
  }
  void setNumPassesInPreviousLayers(uint8_t numPasses)
  {
    numPassesInPreviousPackets = numPasses;
  }

private:
  uint8_t* paddedCompressedStream;
  Layer* layers;
  CodePass* passes;
  uint8_t numPassesInPreviousPackets;
  uint8_t totalPasses_; /* total number of passes in all layers */
#ifdef PLUGIN_DEBUG_ENCODE
  uint32_t* context_stream;
#endif
};

} // namespace grk::t1
