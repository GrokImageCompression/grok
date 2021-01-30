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

TileCache::TileCache() {
}

TileCache::~TileCache() {
	clear();
}

void TileCache::clear(void){
	for (auto &proc : m_processors)
		delete proc.second;
	m_processors.clear();
}

void TileCache::clear(uint32_t tileIndex){
	m_processors.erase(tileIndex);
}

void TileCache::put(uint32_t tileIndex, TileProcessor *processor){
	if (m_processors.find(tileIndex) != m_processors.end())
		delete m_processors[tileIndex];
	m_processors[tileIndex] = processor;
}

TileProcessor* TileCache::get(uint32_t tileIndex){
	if (m_processors.find(tileIndex) != m_processors.end())
		return m_processors[tileIndex];

	return nullptr;
}

}
