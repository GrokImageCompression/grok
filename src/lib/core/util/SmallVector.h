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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace grk
{

template<typename T, size_t InlineCapacity>
class SmallVector
{
public:
  size_t size() const
  {
    return size_;
  }
  bool empty() const
  {
    return size_ == 0;
  }
  T* data()
  {
    return size_ <= InlineCapacity ? inline_ : heap_.data();
  }
  const T* data() const
  {
    return size_ <= InlineCapacity ? inline_ : heap_.data();
  }
  T& operator[](size_t index)
  {
    assert(index < size_);
    return data()[index];
  }
  const T& operator[](size_t index) const
  {
    assert(index < size_);
    return data()[index];
  }
  T& back()
  {
    return (*this)[size_ - 1];
  }
  T* begin()
  {
    return data();
  }
  T* end()
  {
    return data() + size_;
  }
  const T* begin() const
  {
    return data();
  }
  const T* end() const
  {
    return data() + size_;
  }
  T& emplace_back()
  {
    resize(size_ + 1);
    return back();
  }
  void push_back(const T& value)
  {
    resize(size_ + 1);
    back() = value;
  }
  void resize(size_t newSize)
  {
    assert(newSize >= size_);
    if(newSize > InlineCapacity)
    {
      if(size_ <= InlineCapacity)
        heap_.assign(std::make_move_iterator(inline_), std::make_move_iterator(inline_ + size_));
      heap_.resize(newSize);
    }
    else
    {
      std::fill(inline_ + size_, inline_ + newSize, T{});
    }
    size_ = newSize;
  }
  void clear()
  {
    heap_.clear();
    size_ = 0;
  }

private:
  T inline_[InlineCapacity];
  std::vector<T> heap_;
  size_t size_ = 0;
};

} // namespace grk
