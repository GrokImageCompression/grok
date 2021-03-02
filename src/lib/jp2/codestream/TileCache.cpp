/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
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

namespace grk {

TileCacheEntry::TileCacheEntry(TileProcessor *p) : processor(p)
{}
TileCacheEntry::TileCacheEntry() : TileCacheEntry(nullptr)
{}
TileCacheEntry::~TileCacheEntry(){
	delete processor;
}

TileCache::TileCache(GRK_TILE_CACHE_STRATEGY strategy) : tileComposite(nullptr), m_strategy(strategy){
	tileComposite = new GrkImage();
}
TileCache::TileCache() : TileCache(GRK_TILE_CACHE_NONE){
}
TileCache::~TileCache() {
	for (auto &proc : m_cache)
		delete proc.second;
	if (tileComposite)
		grk_object_unref(&tileComposite->obj);
}

TileCacheEntry*  TileCache::put(uint16_t tileIndex, TileProcessor *processor){
	TileCacheEntry *entry = nullptr;
	if (m_cache.find(tileIndex) != m_cache.end()) {
		entry =  m_cache[tileIndex];
		entry->processor = processor;
	}
	else {
		entry = new TileCacheEntry(processor);
		m_cache[tileIndex] = entry;
	}

	return entry;
}

TileCacheEntry* TileCache::get(uint16_t tileIndex){
	if (m_cache.find(tileIndex) != m_cache.end())
		return m_cache[tileIndex];

	return nullptr;
}

void TileCache::setStrategy(GRK_TILE_CACHE_STRATEGY strategy){
	m_strategy = strategy;
}
GrkImage* TileCache::getComposite(){
	return tileComposite;
}

GrkImage* TileCache::getFinalComposite(GrkImage *outputImage){
	auto all = getAllImages();
	if (all.size() == 1)
		return all[0];
	for (auto &img : all){
		if (!outputImage->compositeFrom(img))
			return nullptr;
	}

	return outputImage;

}

std::vector<GrkImage*> TileCache::getAllImages(void){
	std::vector<GrkImage*> rc;
	rc.push_back(tileComposite);
	for (auto &entry : m_cache){
		if (entry.second->processor->getImage())
			rc.push_back(entry.second->processor->getImage());
	}

	return rc;
}

std::vector<GrkImage*> TileCache::getTileImages(void){
	std::vector<GrkImage*> rc;
	for (auto &entry : m_cache)
		if (entry.second->processor->getImage())
			rc.push_back(entry.second->processor->getImage());

	return rc;
}


}
