/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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
typedef std::vector<DecompressBlockExec*> ResDecompressBlocks;
typedef std::vector<ResDecompressBlocks> DecompressBlocks;

class DecompressScheduler : public Scheduler
{
  public:
	DecompressScheduler(void);
	~DecompressScheduler() = default;
	bool scheduleDecompress(TileComponent* tilec, TileCodingParams* tcp,
							TileComponentCodingParams* tccp, uint8_t prec);

  private:
	void prepareScheduleDecompress(TileComponent* tilec, TileComponentCodingParams* tccp,
								   uint8_t prec);
	bool decompressBlock(T1Interface* impl, DecompressBlockExec* block);
	bool decompress(void);
	std::atomic_bool success;
	DecompressBlocks blocks;
};

} // namespace grk
