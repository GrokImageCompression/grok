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

class TileCache {
public:
	TileCache();
	virtual ~TileCache();

	void clear(void);
	void clear(uint32_t tileIndex);
	void put(uint32_t tileIndex, TileProcessor *processor);
	TileProcessor* get(uint32_t tileIndex);

private:
	std::map<uint32_t, TileProcessor*> m_processors;
};

}


