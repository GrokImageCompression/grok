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

#include "grk_includes.h"

namespace grk
{

PacketCache::PacketCache()
{
  parsers_.push_back(nullptr);
  iter_ = parsers_.begin();
}

PacketCache::~PacketCache()
{
  for(const auto& p : parsers_)
    delete p;
}

void PacketCache::next(size_t offset)
{
  SparseBuffer::chunkSkip(offset);
  next();
}

void PacketCache::rewind(void)
{
  SparseBuffer::rewind();
  iter_ = parsers_.begin();
}

PacketParser* PacketCache::gen(TileProcessor* tileProcessor, uint16_t packetSequenceNumber,
                               uint16_t compno, uint8_t resno, uint64_t precinctIndex,
                               uint16_t layno, uint32_t cachedLength)
{
  if(iter_ == parsers_.end())
    next();

  if(!*iter_)
  {
    auto parser = new PacketParser(tileProcessor, packetSequenceNumber, compno, resno,
                                   precinctIndex, layno, cachedLength, this);
    *iter_ = parser;
  }
  return *iter_;
}

void PacketCache::next(void)
{
  if(iter_ == parsers_.end() - 1 || iter_ == parsers_.end())
  {
    parsers_.push_back(nullptr);
    iter_ = parsers_.end() - 1;
  }
  else
  {
    iter_++;
  }
}

} // namespace grk
