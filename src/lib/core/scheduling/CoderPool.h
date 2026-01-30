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

namespace grk
{

struct CoderKey
{
public:
  CoderKey(uint8_t w, uint8_t h) : cbw(w), cbh(h) {}
  CoderKey() : CoderKey(0, 0) {}

  uint8_t cbw; // Code block width
  uint8_t cbh; // Code block height

  bool operator==(const CoderKey& other) const
  {
    return cbw == other.cbw && cbh == other.cbh;
  }
};

struct CoderKeyHash
{
  size_t operator()(const CoderKey& key) const
  {
    return std::hash<uint8_t>()(key.cbw) ^ (std::hash<uint8_t>()(key.cbh) << 1);
  }
};

typedef std::unordered_map<CoderKey, std::vector<std::shared_ptr<t1::ICoder>>, CoderKeyHash>
    CODERMAP;

struct CoderPool
{
public:
  CoderPool(void) = default;
  ~CoderPool(void) = default;
  void makeCoders(uint32_t numCoders, uint8_t maxCblkWExp, uint8_t maxCblkHExp,
                  std::function<std::shared_ptr<t1::ICoder>()> creator);
  bool contains(uint8_t maxCblkWExp, uint8_t maxCblkHExp);
  std::shared_ptr<t1::ICoder> getCoder(size_t worker, uint8_t maxCblkWExp, uint8_t maxCblkHExp);

private:
  CODERMAP coderMap_;
};

} // namespace grk