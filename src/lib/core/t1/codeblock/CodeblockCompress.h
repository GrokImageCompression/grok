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

#include "Codeblock.h"
#include "CodeblockCompressImpl.h"

namespace grk::t1
{

/**
 * @struct CodeblockCompress
 * @brief Stores information about compression code block
 */
struct CodeblockCompress : public Codeblock
{
  /**
   * @brief Constructs a CodeblockCompress
   * This struct is a wrapper for @ref CodeblockCompressImpl, where
   * the implementation lies.
   * @param numlayers Number of layers in code block
   */
  explicit CodeblockCompress(uint16_t numLayers) : Codeblock(numLayers), impl_(nullptr) {}
  /**
   * @brief Destorys a CodeblockCompress
   */
  ~CodeblockCompress()
  {
    delete impl_;
  }
  /**
   * @brief Gets number of passes in layer
   * @param layno Layer number
   */
  uint8_t getNumPassesInLayer(uint16_t layno)
  {
    return getImpl()->getNumPassesInLayer(layno);
  }
  /**
   * @brief Sets number of passes in layer
   * @param layno Layer number
   * @param passes Number of passes
   */
  void setNumPassesInLayer(uint16_t layno, uint8_t passes)
  {
    getImpl()->setNumPassesInLayer(layno, passes);
  }
  /**
   * @brief Increments the number of passes in layer
   * @param layno Layer number
   * @param delta Change in number of passes in layer
   */
  void incNumPassesInLayer(uint16_t layno, uint8_t delta)
  {
    getImpl()->incNumPassesInLayer(layno, delta);
  }
  /**
   * @brief Gets compressed stream
   * @return @ref Buffer8 containing compressed stream
   */
  Buffer8* getCompressedStream(void)
  {
    return getImpl()->getCompressedStream();
  }
  /**
   * @brief Gets number of bit planes in code block
   * @return number of bit planes
   */
  uint8_t numbps(void)
  {
    return getImpl()->numbps();
  }
  /**
   * @brief Sets number of bit planes in code blocks
   * @param bps number of bit planes
   */
  void setNumBps(uint8_t bps)
  {
    getImpl()->setNumBps(bps);
  }
  /**
   * @brief Gets number of length bits, used to calculate length of code block in bytes
   * @return number of length bits
   */
  uint8_t numlenbits()
  {
    return getImpl()->numlenbits();
  }
  /**
   * @brief Sets number of length bits, used to calculate length of code block in bytes
   * @param bits number of length bits
   */
  void setNumLenBits(uint8_t bits)
  {
    getImpl()->setNumLenBits(bits);
  }

  /**
   * @brief Initializes the code block - allocates resources
   */
  void init()
  {
    getImpl()->init();
  }
  /**
   * @brief Allocates data memory for a compression code block.
   * We actually allocate 2 more bytes than specified, and then offset data by +2.
   * This is done so that we can safely initialize the MQ coder pointer to data-1,
   * without risk of accessing uninitialized memory.
   * @param nominalBlockSize nominal block size - actual size may be smaller
   * @return
   */
  bool allocData(size_t nominalBlockSize)
  {
    return getImpl()->allocData(nominalBlockSize);
  }
  CodePass* getPass(uint8_t passno)
  {
    return getImpl()->getPass(passno);
  }
  uint8_t getNumPasses(void)
  {
    return getImpl()->getNumPasses();
  }
  void setNumPasses(uint8_t numPasses)
  {
    getImpl()->setNumPasses(numPasses);
  }
  CodePass* getLastPass(void)
  {
    return getImpl()->getLastPass();
  }
  Layer* getLayer(uint16_t layno)
  {
    return getImpl()->getLayer(layno);
  }
  uint8_t* getPaddedCompressedStream(void)
  {
    return getImpl()->getPaddedCompressedStream();
  }
  void setPaddedCompressedStream(uint8_t* stream)
  {
    getImpl()->setPaddedCompressedStream(stream);
  }
  uint8_t getNumPassesInPreviousLayers(void)
  {
    return getImpl()->getNumPassesInPreviousLayers();
  }
  void setNumPassesInPreviousLayers(uint8_t numPasses)
  {
    getImpl()->setNumPassesInPreviousLayers(numPasses);
  }
  CodeblockCompressImpl* getImpl(void)
  {
    if(!impl_)
      impl_ = new CodeblockCompressImpl(numLayers_);
    return impl_;
  }

private:
  CodeblockCompressImpl* impl_;
  explicit CodeblockCompress(const CodeblockCompress& rhs) = delete;
  CodeblockCompress& operator=(const CodeblockCompress& rhs) = delete;
};

} // namespace grk::t1
