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
#include "grk_includes.h"

#include <map>

namespace grk {

template <typename T> class SparseCache{
public:
	SparseCache(uint64_t maxChunkSize) : m_chunkSize(std::min<uint64_t>(maxChunkSize, 1024)),
											m_currChunk(nullptr),
											m_currChunkIndex(0)
	{}
	virtual ~SparseCache(void){
		for (auto &ch : chunks){
			for (size_t i = 0; i < m_chunkSize; ++i)
				delete ch.second[i];
			delete[] ch.second;
		}
	}
	T* get(uint64_t index){
		uint64_t chunkIndex = index / m_chunkSize;
		uint64_t itemIndex =  index % m_chunkSize;
		if (m_currChunk == nullptr || (chunkIndex != m_currChunkIndex)){
			m_currChunkIndex = chunkIndex;
			auto iter = chunks.find(chunkIndex);
			if (iter != chunks.end()){
				m_currChunk =  iter->second;
			} else {
				m_currChunk = new T*[m_chunkSize];
				memset(m_currChunk, 0, m_chunkSize * sizeof(T*));
				chunks[chunkIndex] = m_currChunk;
			}
		}
		auto item = m_currChunk[itemIndex];
		if (!item){
			item = create(index);
			m_currChunk[itemIndex] = item;
		}
		return item;
	}
protected:
	virtual T* create(uint64_t index){
		GRK_UNUSED(index);
		auto item = new T();
		return item;
	}
private:
	std::map<uint64_t, T**> chunks;
	uint64_t m_chunkSize;
	T** m_currChunk;
	uint64_t m_currChunkIndex;
};


}
