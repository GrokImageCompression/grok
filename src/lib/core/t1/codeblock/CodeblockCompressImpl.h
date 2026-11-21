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
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>
#include "CodeStreamLimits.h"
#include "CodeblockImpl.h"
const uint8_t grk_cblk_enc_compressed_data_pad_left = 2;

// a small block with many bit planes can emit more than four bytes per sample
inline size_t compressedStreamScratchBytes(uint32_t numSamples)
{
  return std::max<size_t>((size_t)numSamples * sizeof(uint32_t), 4096) +
         grk_cblk_enc_compressed_data_pad_left;
}

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
  PrecinctCodeblockStorage(uint32_t numBlocks, uint16_t numLayers, uint8_t maxPasses)
      : numLayers_(numLayers), maxPasses_(maxPasses),
        layers_(new Layer[(size_t)numBlocks * numLayers]())
  {}
  uint8_t maxPasses(void) const
  {
    return maxPasses_;
  }
  Layer* getLayers(uint32_t cblkno)
  {
    return layers_.get() + (size_t)cblkno * numLayers_;
  }
  uint8_t* storeCompressedBytes(const uint8_t* bytes, uint32_t length)
  {
    auto dest = allocate(length);
    memcpy(dest, bytes, length);
    return dest;
  }
  CodePass* allocatePasses(uint8_t numPasses)
  {
    auto passes = reinterpret_cast<CodePass*>(allocate((size_t)numPasses * sizeof(CodePass)));
    std::uninitialized_value_construct_n(passes, numPasses);
    return passes;
  }

  void replaceChunks(std::unique_ptr<uint8_t[]> chunk, size_t size)
  {
    chunks_.clear();
    chunks_.push_back(std::move(chunk));
    chunkCapacity_ = size;
    chunkUsed_ = size;
  }

private:
  uint8_t* allocate(size_t length)
  {
    // blocks of one precinct are coded on different threads
    std::lock_guard<std::mutex> lock(chunksMutex_);
    size_t offset = (chunkUsed_ + alignof(CodePass) - 1) & ~(alignof(CodePass) - 1);
    if(chunks_.empty() || offset + length > chunkCapacity_)
    {
      chunkCapacity_ = std::max<size_t>(length, std::clamp<size_t>(2 * chunkCapacity_, 4096, 262144));
      chunks_.emplace_back(new uint8_t[chunkCapacity_]);
      offset = 0;
    }
    chunkUsed_ = offset + length;
    return chunks_.back().get() + offset;
  }

  uint16_t numLayers_;
  uint8_t maxPasses_;
  std::unique_ptr<Layer[]> layers_;
  std::mutex chunksMutex_;
  std::vector<std::unique_ptr<uint8_t[]>> chunks_;
  size_t chunkCapacity_ = 0;
  size_t chunkUsed_ = 0;
};

struct CodeblockCompressImpl : public CodeblockImpl
{
  explicit CodeblockCompressImpl(uint16_t numLayers)
      : CodeblockImpl(numLayers), compressedBytes_(nullptr), layers(nullptr), passes(nullptr),
        storage_(nullptr), numPassesInPreviousPackets(0), totalPasses_(0)
#ifdef PLUGIN_DEBUG_ENCODE
        ,
        context_stream(nullptr)
#endif
  {}
  ~CodeblockCompressImpl() = default;
  void init(PrecinctCodeblockStorage* storage, uint32_t cblkno)
  {
    CodeblockImpl::init();
    layers = storage->getLayers(cblkno);
    passes = nullptr;
    storage_ = storage;
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
  CodePass* allocatePasses(uint8_t numPasses)
  {
    totalPasses_ = numPasses;
    passes = storage_->allocatePasses(numPasses);
    return passes;
  }
  CodePass* getLastPass(void)
  {
    return passes + totalPasses_ - 1;
  }
  Layer* getLayer(uint16_t layno)
  {
    return layers + layno;
  }
  uint8_t* getCompressedBytes(void)
  {
    return compressedBytes_;
  }
  void setCompressedBytes(uint8_t* bytes)
  {
    compressedBytes_ = bytes;
  }
  void storeCompressedBytes(const uint8_t* bytes, uint32_t length)
  {
    compressedBytes_ = storage_->storeCompressedBytes(bytes, length);
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
  uint8_t* compressedBytes_;
  Layer* layers;
  CodePass* passes;
  PrecinctCodeblockStorage* storage_;
  uint8_t numPassesInPreviousPackets;
  uint8_t totalPasses_; /* total number of passes in all layers */
#ifdef PLUGIN_DEBUG_ENCODE
  uint32_t* context_stream;
#endif
};

} // namespace grk::t1
