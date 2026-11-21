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
#include <memory>
#include <mutex>
#include "CodeStreamLimits.h"
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

struct PrecinctCodeblockStorage
{
  // a small block with many bit planes can emit more than nominalBlockSize * 4 bytes
  static constexpr uint32_t minCompressedStreamBytes = 4096;

  PrecinctCodeblockStorage(uint32_t numBlocks, uint16_t numLayers, uint16_t nominalBlockSize,
                           uint8_t maxPasses)
      : numBlocks_(numBlocks), numLayers_(numLayers), maxPasses_(maxPasses),
        compressedStreamBytes_(std::max((uint32_t)nominalBlockSize * (uint32_t)sizeof(uint32_t),
                                        minCompressedStreamBytes) +
                               grk_cblk_enc_compressed_data_pad_left),
        passesOffset_((size_t)numBlocks * numLayers * sizeof(Layer)),
        storage_(new uint8_t[passesOffset_ + (size_t)numBlocks * maxPasses * sizeof(CodePass)])
  {
    std::uninitialized_value_construct_n(reinterpret_cast<Layer*>(storage_.get()),
                                         (size_t)numBlocks * numLayers);
    std::uninitialized_value_construct_n(
        reinterpret_cast<CodePass*>(storage_.get() + passesOffset_), (size_t)numBlocks * maxPasses);
  }
  uint8_t maxPasses(void) const
  {
    return maxPasses_;
  }
  Layer* getLayers(uint32_t cblkno)
  {
    return reinterpret_cast<Layer*>(storage_.get()) + (size_t)cblkno * numLayers_;
  }
  CodePass* getPasses(uint32_t cblkno)
  {
    return reinterpret_cast<CodePass*>(storage_.get() + passesOffset_) +
           (size_t)cblkno * maxPasses_;
  }
  // a plugin that codes the blocks never asks for these
  uint8_t* getCompressedStream(uint32_t cblkno)
  {
    std::call_once(streamsAllocated_, [this] {
      streams_.reset(new uint8_t[(size_t)numBlocks_ * compressedStreamBytes_]);
    });
    return streams_.get() + (size_t)cblkno * compressedStreamBytes_;
  }
  uint32_t getCompressedStreamBytes(void) const
  {
    return compressedStreamBytes_;
  }

private:
  uint32_t numBlocks_;
  uint16_t numLayers_;
  uint8_t maxPasses_;
  uint32_t compressedStreamBytes_;
  size_t passesOffset_;
  std::unique_ptr<uint8_t[]> storage_;
  std::once_flag streamsAllocated_;
  std::unique_ptr<uint8_t[]> streams_;
};

struct CodeblockCompressImpl : public CodeblockImpl
{
  explicit CodeblockCompressImpl(uint16_t numLayers)
      : CodeblockImpl(numLayers), paddedCompressedStream(nullptr), layers(nullptr), passes(nullptr),
        storage_(nullptr), cblkno_(0), numPassesInPreviousPackets(0), totalPasses_(0)
#ifdef PLUGIN_DEBUG_ENCODE
        ,
        context_stream(nullptr)
#endif
  {}
  ~CodeblockCompressImpl() = default;
  // the mq coder is initialized to data[-1], so the output starts two bytes into the slice
  void init(PrecinctCodeblockStorage* storage, uint32_t cblkno)
  {
    CodeblockImpl::init();
    layers = storage->getLayers(cblkno);
    passes = storage->getPasses(cblkno);
    storage_ = storage;
    cblkno_ = cblkno;
  }
  CodePass* getPass(uint8_t passno)
  {
    return passes + passno;
  }
  void setPasses(CodePass* externalPasses)
  {
    passes = externalPasses;
  }
  uint8_t maxPasses(void) const
  {
    return storage_ ? storage_->maxPasses() : 0;
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
    if(!paddedCompressedStream && storage_)
    {
      auto buf = storage_->getCompressedStream(cblkno_);
      buf[0] = 0;
      buf[1] = 0;
      paddedCompressedStream = buf + grk_cblk_enc_compressed_data_pad_left;
      compressedStream.set_buf(buf, storage_->getCompressedStreamBytes() -
                                        grk_cblk_enc_compressed_data_pad_left);
    }
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
  PrecinctCodeblockStorage* storage_;
  uint32_t cblkno_;
  uint8_t numPassesInPreviousPackets;
  uint8_t totalPasses_; /* total number of passes in all layers */
#ifdef PLUGIN_DEBUG_ENCODE
  uint32_t* context_stream;
#endif
};

} // namespace grk::t1
