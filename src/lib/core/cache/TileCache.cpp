/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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

#include <grk_includes.h>

namespace grk
{
TileCacheEntry::TileCacheEntry(TileProcessor* p) : processor(p) {}
TileCacheEntry::TileCacheEntry() : TileCacheEntry(nullptr) {}
TileCacheEntry::~TileCacheEntry()
{
   delete processor;
}
TileCache::TileCache(GRK_TILE_CACHE_STRATEGY strategy) : tileComposite(nullptr), strategy_(strategy)
{
   tileComposite = new GrkImage();
}
TileCache::TileCache() : TileCache(GRK_TILE_CACHE_NONE) {}
TileCache::~TileCache()
{
   for(const auto& proc : cache_)
	  delete proc.second;
   if(tileComposite)
	  grk_object_unref(&tileComposite->obj);
}
bool TileCache::empty()
{
   return cache_.empty();
}
TileCacheEntry* TileCache::put(uint16_t tile_index, TileProcessor* processor)
{
   TileCacheEntry* entry = nullptr;
   if(cache_.find(tile_index) != cache_.end())
   {
	  entry = cache_[tile_index];
	  entry->processor = processor;
   }
   else
   {
	  entry = new TileCacheEntry(processor);
	  cache_[tile_index] = entry;
   }

   return entry;
}
TileCacheEntry* TileCache::get(uint16_t tile_index)
{
   if(cache_.find(tile_index) != cache_.end())
	  return cache_[tile_index];

   return nullptr;
}
void TileCache::setStrategy(GRK_TILE_CACHE_STRATEGY strategy)
{
   strategy_ = strategy;
}
GRK_TILE_CACHE_STRATEGY TileCache::getStrategy(void)
{
   return strategy_;
}
GrkImage* TileCache::getComposite()
{
   return tileComposite;
}
std::vector<GrkImage*> TileCache::getAllImages(void)
{
   std::vector<GrkImage*> rc = getTileImages();
   rc.push_back(tileComposite);

   return rc;
}
std::vector<GrkImage*> TileCache::getTileImages(void)
{
   std::vector<GrkImage*> rc;
   for(const auto& entry : cache_)
   {
	  auto image = entry.second->processor->getImage();
	  if(image)
		 rc.push_back(image);
   }
   return rc;
}

} // namespace grk
