/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
#include <string>
#include <vector>
#include <thread>

namespace grk {

struct decodeBlockInfo;
class T1Interface;

class T1Decoder {
public:
	T1Decoder(grk_tcp *tcp, uint16_t blockw, uint16_t blockh);
	~T1Decoder();
	bool decompress(std::vector<decodeBlockInfo*> *blocks);

private:
	uint16_t codeblock_width, codeblock_height;  //nominal dimensions of block
	std::vector<T1Interface*> threadStructs;
	std::atomic_bool success;

	decodeBlockInfo** decodeBlocks;
};

}
