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

const uint64_t kSequentialChunkSize = 1024;
template <typename T> class SequentialCache{
public:
	SequentialCache(void) : SequentialCache(kSequentialChunkSize)
	{}
	SequentialCache(uint64_t maxChunkSize) : m_chunkSize(std::min<uint64_t>(maxChunkSize,kSequentialChunkSize )),
												m_currChunk(nullptr),
												m_index(0)
	{}
	virtual ~SequentialCache(void){
		for (auto &ch : chunks){
			for (size_t i = 0; i < m_chunkSize; ++i)
				delete ch[i];
			delete[] ch;
		}
	}
	void rewind(void){
		if (chunks.empty())
			return;
		m_index=0;
		m_currChunk = chunks[0];
	}
	T* get(){
		uint64_t itemIndex = m_index % m_chunkSize;
		uint64_t chunkIndex = m_index / m_chunkSize;
		bool initialized = (m_currChunk != nullptr);
		bool lastChunk = (chunkIndex == chunks.size() - 1);
		bool endOfChunk = (itemIndex == m_chunkSize - 1);
		bool createNew = !initialized || (lastChunk && endOfChunk);
		itemIndex++;
		if (createNew || endOfChunk){
			itemIndex=0;
			chunkIndex++;
			if (createNew) {
				m_currChunk = new T*[m_chunkSize];
				memset(m_currChunk, 0, m_chunkSize * sizeof(T*));
				chunks.push_back(m_currChunk);
			} else {
				m_currChunk = chunks[chunkIndex];
			}
		}
		auto item = m_currChunk[itemIndex];
		if (!item){
			item = create();
			m_currChunk[itemIndex] = item;
		}
		if (initialized)
			m_index++;
		return item;
	}
protected:
	virtual T* create(void){
		auto item = new T();
		return item;
	}
private:
	std::vector<T**> chunks;
	uint64_t m_chunkSize;
	T** m_currChunk;
	uint64_t m_index;
};


}
