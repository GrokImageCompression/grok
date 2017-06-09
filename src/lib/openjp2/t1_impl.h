/*
*    Copyright (C) 2016-2017 Grok Image Compression Inc.
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
*
*/

#pragma once

#include "t1_interface.h"

namespace grk {

struct t1_t;
struct t1_opt_t;
class t1_impl : public t1_interface
{
public:
	t1_impl(bool opt, uint32_t maxCblkW,uint32_t maxCblkH);
	virtual ~t1_impl();
	double encode(encodeBlockInfo* block, tcd_tile_t *tile, uint32_t max);
	void prepareEncode(encodeBlockInfo* block, tcd_tile_t *tile, uint32_t& max);
private:
	bool doOpt;
	t1_t* t1;
	t1_opt_t* t1_opt;

};


}