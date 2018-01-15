/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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
#include "t1_decode_base.h"
#include "t1_decode.h"
#include "t1_encode.h"
#include "t1_decode_opt.h"
#include "t1_interface.h"

namespace grk {

struct tcp_t;

class t1_impl : public t1_interface
{
public:
	t1_impl(bool isEncoder, tcp_t *tcp, uint16_t maxCblkW, uint16_t maxCblkH);
	virtual ~t1_impl();

	void preEncode(encodeBlockInfo* block, tcd_tile_t *tile, uint32_t& max);
	double encode(encodeBlockInfo* block, tcd_tile_t *tile, uint32_t max, bool doRateControl);

	bool decode(decodeBlockInfo* block);
	void postDecode(decodeBlockInfo* block);

private:
	t1_decode_base* t1_decoder;
	t1_encode* t1_encoder;


};


}
