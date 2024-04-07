/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
struct ResDecompressBlocks
{
   ResDecompressBlocks(void) = default;
   void clear(void);
   bool empty(void) const;
   void release(void);

   std::vector<DecompressBlockExec*> blocks_;
};

typedef std::vector<ResDecompressBlocks> ComponentDecompressBlocks;
typedef std::vector<ComponentDecompressBlocks> TileDecompressBlocks;

class DecompressScheduler : public Scheduler
{
 public:
   DecompressScheduler(TileProcessor* tileProcessor, Tile* tile, TileCodingParams* tcp,
					   uint8_t prec);
   ~DecompressScheduler();

   bool schedule(uint16_t compno) override;

 private:
   bool scheduleBlocks(uint16_t compno);
   bool scheduleWavelet(uint16_t compno);
   bool decompressBlock(T1Interface* impl, DecompressBlockExec* block);
   void releaseBlocks(uint16_t compno);
   TileProcessor* tileProcessor_;
   TileCodingParams* tcp_;
   uint8_t prec_;
   uint16_t numcomps_;
   TileDecompressBlocks tileBlocks_;
   WaveletReverse** waveletReverse_;
};

} // namespace grk
