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
#include "CodeblockDecompressImpl.h"

namespace grk::t1
{

struct CodeblockDecompress : public Codeblock
{
  explicit CodeblockDecompress(uint16_t numLayers) : Codeblock(numLayers), impl_(nullptr) {}
  ~CodeblockDecompress()
  {
    release();
  }
  Buffer8* getCompressedStream(void)
  {
    return getImpl()->getCompressedStream();
  }
  uint8_t numbps(void)
  {
    return getImpl()->numbps();
  }
  void setNumBps(uint8_t bps)
  {
    getImpl()->setNumBps(bps);
  }
  uint8_t numlenbits()
  {
    return getImpl()->numlenbits();
  }
  void setNumLenBits(uint8_t bits)
  {
    getImpl()->setNumLenBits(bits);
  }

  void init()
  {
    getImpl()->init();
  }
  Segment* getSegment(uint16_t segmentIndex)
  {
    return getImpl()->getSegment(segmentIndex);
  }
  void readPacketHeader(std::shared_ptr<t1_t2::BitIO> bio, uint32_t& signalledLayerDataBytes,
                        uint16_t layno, uint8_t cblk_sty)
  {
    getImpl()->readPacketHeader(bio, signalledLayerDataBytes, layno, cblk_sty);
  }

  void parsePacketData(uint16_t layno, size_t& remainingTilePartBytes, bool isHT,
                       uint8_t* layerData, uint32_t& layerDataOffset)
  {
    getImpl()->parsePacketData(layno, remainingTilePartBytes, isHT, layerData, layerDataOffset);
  }
  bool canDecompress(void)
  {
    return getImpl()->canDecompress();
  }
  template<typename T>
  bool decompress(T* coder, uint8_t orientation, uint32_t cblksty)
  {
    return getImpl()->decompress<T>(coder, orientation, cblksty);
  }
  uint16_t getNumDataParsedSegments(void)
  {
    return getImpl()->getNumDataParsedSegments();
  }
  bool dataChunksEmpty()
  {
    return getImpl()->dataChunksEmpty();
  }
  size_t getDataChunksLength()
  {
    return getImpl()->getDataChunksLength();
  }
  bool copyDataChunksToContiguous(uint8_t* buffer)
  {
    return getImpl()->copyDataChunksToContiguous(buffer);
  }
  void release(void)
  {
    delete impl_;
    impl_ = nullptr;
  }

  CodeblockDecompressImpl* getImpl(void)
  {
    if(!impl_)
      impl_ = new CodeblockDecompressImpl(numLayers_);
    return impl_;
  }

private:
  CodeblockDecompressImpl* impl_;
};

} // namespace grk::t1
