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
#include <vector>

namespace grk::t1
{
// storage grows to cover the highest layer written, and a read of a layer
// that was never written returns zero
template<typename T>
struct PerLayerValues
{
  T get(uint16_t layno) const
  {
    return layno < values_.size() ? values_[layno] : (T)0;
  }
  void set(uint16_t layno, T value)
  {
    grow(layno);
    values_[layno] = value;
  }
  void increment(uint16_t layno, T delta)
  {
    grow(layno);
    values_[layno] = (T)(values_[layno] + delta);
  }
  void clear()
  {
    values_.clear();
  }
  size_t size() const
  {
    return values_.size();
  }

private:
  void grow(uint16_t layno)
  {
    if(layno >= values_.size())
      values_.resize((size_t)layno + 1);
  }
  std::vector<T> values_;
};

// note: block lives in canvas coordinates
struct CodeblockImpl
{
  CodeblockImpl(uint16_t numLayers) : numbps_(0), numlenbits_(0), numLayers_(numLayers) {}
  ~CodeblockImpl()
  {
    compressedStream.dealloc();
  }
  uint8_t getNumPassesInLayer(uint16_t layno)
  {
    assert(layno < numLayers_);
    return signalledPassesByLayer_.get(layno);
  }
  void setNumPassesInLayer(uint16_t layno, uint8_t passes)
  {
    assert(layno < numLayers_);
    signalledPassesByLayer_.set(layno, passes);
  }
  void incNumPassesInLayer(uint16_t layno, uint8_t delta)
  {
    assert(layno < numLayers_);
    signalledPassesByLayer_.increment(layno, delta);
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
  void init() {}
  Buffer8 compressedStream;
  uint8_t numbps_;
  uint8_t numlenbits_;
  PerLayerValues<uint8_t> signalledPassesByLayer_;
  uint16_t numLayers_;

private:
  explicit CodeblockImpl(const CodeblockImpl& rhs) = default;
  CodeblockImpl& operator=(const CodeblockImpl& rhs) = default;
};

} // namespace grk::t1
