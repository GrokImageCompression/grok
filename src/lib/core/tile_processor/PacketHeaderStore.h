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
#include <cstdint>
#include <span>
#include <tuple>
#include <vector>

#include "IStreamWriter.h"

namespace grk
{

class PacketHeaderStore : public t1_t2::IStreamWriter
{
public:
  void clear()
  {
    bytes_.clear();
    headers_.clear();
    complete_ = false;
  }
  void beginHeader(uint16_t layno, uint16_t compno, uint8_t resno, uint64_t precinctIndex)
  {
    headers_.push_back({{layno, compno, resno, precinctIndex}, bytes_.size(), 0});
  }
  size_t endHeader()
  {
    auto& header = headers_.back();
    header.length = bytes_.size() - header.offset;
    return header.length;
  }
  void markComplete()
  {
    std::sort(headers_.begin(), headers_.end(),
              [](const Header& a, const Header& b) { return a.key < b.key; });
    complete_ = true;
  }
  std::span<const uint8_t> find(uint16_t layno, uint16_t compno, uint8_t resno,
                                uint64_t precinctIndex) const
  {
    if(!complete_)
      return {};
    Key key{layno, compno, resno, precinctIndex};
    auto header = std::lower_bound(headers_.begin(), headers_.end(), key,
                                   [](const Header& h, const Key& k) { return h.key < k; });
    if(header == headers_.end() || header->key != key)
      return {};
    return {bytes_.data() + header->offset, header->length};
  }
  bool write8u(uint8_t value) override
  {
    bytes_.push_back(value);
    return true;
  }

protected:
  bool write_non_template(const uint8_t* value, [[maybe_unused]] uint8_t sizeOfType,
                          uint8_t numBytes) override
  {
    bytes_.insert(bytes_.end(), value, value + numBytes);
    return true;
  }

private:
  using Key = std::tuple<uint16_t, uint16_t, uint8_t, uint64_t>;
  struct Header
  {
    Key key;
    size_t offset;
    size_t length;
  };
  std::vector<uint8_t> bytes_;
  std::vector<Header> headers_;
  bool complete_ = false;
};

} // namespace grk
