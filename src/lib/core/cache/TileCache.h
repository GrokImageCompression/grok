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
 */

#pragma once

#include <map>

namespace grk
{
struct TileProcessor;
class GrkImage;

struct TileCacheEntry
{
  explicit TileCacheEntry(TileProcessor* p);
  TileCacheEntry();
  ~TileCacheEntry();

  TileProcessor* processor;
};

class TileCache
{
public:
  TileCache(uint32_t strategy);
  TileCache(void);
  virtual ~TileCache();

  bool empty(void);
  void setStrategy(uint32_t strategy);
  uint32_t getStrategy(void);
  TileCacheEntry* put(uint16_t tile_index, TileProcessor* processor);
  TileCacheEntry* get(uint16_t tile_index);
  GrkImage* getComposite(void);
  std::vector<GrkImage*> getAllImages(void);
  std::vector<GrkImage*> getTileImages(void);

private:
  // each component is sub-sampled and resolution-reduced
  GrkImage* tileComposite;
  std::map<uint32_t, TileCacheEntry*> cache_;
  uint32_t strategy_;
};

} // namespace grk
