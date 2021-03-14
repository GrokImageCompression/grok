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

namespace grk {

enum GrkCacheState {
	GRK_CACHE_STATE_CLOSED,
	GRK_CACHE_STATE_OPEN,
	GRK_CACHE_STATE_ERROR
};


class ICacheable {
public:
	ICacheable() : m_state(GRK_CACHE_STATE_CLOSED)
	{}
	virtual ~ICacheable() = default;
	bool isOpen(void){
		return m_state == GRK_CACHE_STATE_OPEN;
	}
	bool isClosed(void){
		return m_state == GRK_CACHE_STATE_CLOSED;
	}
	bool isError(void){
		return m_state == GRK_CACHE_STATE_ERROR;
	}
	void setCacheState(GrkCacheState state){
		m_state = state;
	}
private:
	GrkCacheState m_state;

};

}

