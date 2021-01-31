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

TileCacheEntry::TileCacheEntry(TileProcessor *p, GrkImage *img) : processor(p), image(img)
{}
TileCacheEntry::TileCacheEntry(TileProcessor *p) : TileCacheEntry(p,nullptr)
{}
TileCacheEntry::TileCacheEntry() : TileCacheEntry(nullptr,nullptr)
{}
TileCacheEntry::~TileCacheEntry(){
	delete processor;
	delete image;
}

TileCache::TileCache(GRK_TILE_CACHE_STRATEGY strategy) : tileComposite(nullptr), m_strategy(strategy){
	tileComposite = new GrkImage();
}
TileCache::TileCache() : TileCache(GRK_TILE_CACHE_NONE){
}
TileCache::~TileCache() {
	for (auto &proc : m_processors)
		delete proc.second;
	delete tileComposite;
}

void TileCache::put(uint16_t tileIndex, TileCacheEntry *entry){
	if (m_processors.find(tileIndex) != m_processors.end())
		delete m_processors[tileIndex];
	m_processors[tileIndex] = entry;
}

void TileCache::put(uint16_t tileIndex, GrkImage* src_image, grk_tile *src_tile){
	if (m_processors.find(tileIndex) == m_processors.end())
		return;
	switch(m_strategy){
		case GRK_TILE_CACHE_NONE:
			break;
		case GRK_TILE_CACHE_ALL:
			{
			auto copy = src_image->duplicate(src_tile);
			m_processors[tileIndex]->image = copy;
			}
			break;
		default:
			break;
	}

}

TileCacheEntry* TileCache::get(uint16_t tileIndex){
	if (m_processors.find(tileIndex) != m_processors.end())
		return m_processors[tileIndex];

	return nullptr;
}

void TileCache::setStrategy(GRK_TILE_CACHE_STRATEGY strategy){
	m_strategy = strategy;
}
GrkImage* TileCache::getComposite(){
	return tileComposite;
}

}
