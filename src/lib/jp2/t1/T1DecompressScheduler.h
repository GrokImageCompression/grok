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
 *
 */

#pragma once

#include "grk_includes.h"

namespace grk
{
struct DecompressBlockExec;
class T1Interface;

class T1DecompressScheduler
{
  public:
	T1DecompressScheduler(void);
	~T1DecompressScheduler();
	bool decompress(std::vector<DecompressBlockExec*>* blocks);

	bool prepareScheduleDecompress(TileComponent* tilec, TileComponentCodingParams* tccp,
								   std::vector<DecompressBlockExec*>* blocks, uint8_t prec);

	bool scheduleDecompress(TileCodingParams* tcp, uint16_t blockw, uint16_t blockh,
							std::vector<DecompressBlockExec*>* blocks);

  private:
	bool decompressBlock(T1Interface* impl, DecompressBlockExec* block);
	std::vector<T1Interface*> t1Implementations;
	std::atomic_bool success;

	DecompressBlockExec** decodeBlocks;
	const uint8_t gain_b[4] = {0, 1, 1, 2};
};

} // namespace grk
