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

#pragma once

#include <map>

namespace grk {

struct TileProcessor;

struct TileCacheEntry{
	explicit TileCacheEntry(TileProcessor *p) : processor(p), image(nullptr)
	{}
	TileCacheEntry() : TileCacheEntry(nullptr)
	{}
	~TileCacheEntry()
	{
		delete processor;
		delete image;
	}
	TileProcessor* processor;
	grk_image *image;
};

class TileCache {
public:
	TileCache(GRK_TILE_CACHE_STRATEGY strategy);
	TileCache(void);
	virtual ~TileCache();

	void put(uint16_t tileIndex, TileProcessor *processor);
	TileCacheEntry* get(uint16_t tileIndex);

	void setStrategy(GRK_TILE_CACHE_STRATEGY strategy);
	void flush(uint16_t tileIndex);

private:
	grk_image *tileComposite;
	std::map<uint32_t, TileCacheEntry*> m_processors;
	GRK_TILE_CACHE_STRATEGY m_strategy;
};

}


