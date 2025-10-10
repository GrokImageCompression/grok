/*
 *    Copyright (C) 2016-2025 Grok Image Compression Inc.
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
// note: block lives in canvas coordinates
struct CodeblockImpl
{
  CodeblockImpl(uint16_t numLayers)
      : numbps_(0), numlenbits_(0), signalledPassesByLayer_(nullptr), numLayers_(numLayers)
  {}
  ~CodeblockImpl()
  {
    compressedStream.dealloc();
    delete[] signalledPassesByLayer_;
  }
  uint8_t getNumPassesInLayer(uint16_t layno)
  {
    assert(layno < numLayers_);
    return signalledPassesByLayer_[layno];
  }
  void setNumPassesInLayer(uint16_t layno, uint8_t passes)
  {
    assert(layno < numLayers_);
    signalledPassesByLayer_[layno] = passes;
  }
  void incNumPassesInLayer(uint16_t layno, uint8_t delta)
  {
    assert(layno < numLayers_);
    signalledPassesByLayer_[layno] += delta;
  }
  Buffer8* getCompressedStream(void)
  {
    return &compressedStream;
  }
  uint8_t numbps(void)
  {
    return numbps_;
  }
  void setNumBps(uint8_t bps)
  {
    numbps_ = bps;
  }
  uint8_t numlenbits()
  {
    return numlenbits_;
  }
  void setNumLenBits(uint8_t bits)
  {
    numlenbits_ = bits;
  }

protected:
  void init()
  {
    assert(!signalledPassesByLayer_);
    signalledPassesByLayer_ = new uint8_t[numLayers_];
    memset(signalledPassesByLayer_, 0, numLayers_);
  }
  Buffer8 compressedStream;
  uint8_t numbps_;
  uint8_t numlenbits_;
  uint8_t* signalledPassesByLayer_;
  uint16_t numLayers_;

private:
  explicit CodeblockImpl(const CodeblockImpl& rhs) = default;
  CodeblockImpl& operator=(const CodeblockImpl& rhs) = default;
};

} // namespace grk
