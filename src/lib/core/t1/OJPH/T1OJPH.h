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
#include "T1Interface.h"
#include "TileProcessor.h"

namespace ojph
{
class mem_fixed_allocator;
class mem_elastic_allocator;

struct TileCodingParams;

class T1OJPH : public grk::T1Interface
{
 public:
   T1OJPH(bool isCompressor, grk::TileCodingParams* tcp, uint32_t maxCblkW, uint32_t maxCblkH);
   virtual ~T1OJPH();

   bool compress(grk::CompressBlockExec* block);
   bool decompress(grk::DecompressBlockExec* block);

 private:
   void preCompress(grk::CompressBlockExec* block, grk::Tile* tile);
   bool postProcess(grk::DecompressBlockExec* block);

   uint32_t coded_data_size;
   uint8_t* coded_data;
   uint32_t unencoded_data_size;
   int32_t* unencoded_data;

   mem_fixed_allocator* allocator;
   mem_elastic_allocator* elastic_alloc;
};
} // namespace ojph
