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
 */
#pragma once
#include "grk_includes.h"

namespace grk
{
template<typename T, typename P>
class BlockCache : public SparseCache<T>
{
 public:
   BlockCache(uint16_t numLayers, uint64_t maxChunkSize, P* blockInitializer)
	   : SparseCache<T>(maxChunkSize), blockInitializer_(blockInitializer), numLayers_(numLayers)
   {}
   virtual ~BlockCache() = default;

 protected:
   virtual T* create(uint64_t index) override
   {
	  auto item = new T(numLayers_);
	  blockInitializer_->initCodeBlock(item, index);
	  return item;
   }

 private:
   P* blockInitializer_;
   uint16_t numLayers_;
};

struct PrecinctImpl
{
   PrecinctImpl(bool isCompressor, grk_rect32* bounds, grk_pt32 cblk_expn);
   ~PrecinctImpl(void);
   grk_rect32 getCodeBlockBounds(uint64_t cblkno);
   bool initCodeBlocks(uint16_t numLayers, grk_rect32* bounds);
   template<typename T>
   bool initCodeBlock(T* block, uint64_t cblkno);
   void deleteTagTrees();
   TagTreeU16* getIncludeTagTree(void);
   TagTreeU8* getIMsbTagTree(void);
   BlockCache<CompressCodeblock, PrecinctImpl>* enc;
   BlockCache<DecompressCodeblock, PrecinctImpl>* dec;
   grk_rect32 cblk_grid_;
   grk_rect32 bounds_;
   grk_pt32 cblk_expn_;
   bool isCompressor_;

 private:
   TagTreeU16* incltree; /* inclusion tree */
   TagTreeU8* imsbtree; /* IMSB tree */
};
struct Precinct : public grk_rect32
{
   Precinct(TileProcessor* tileProcessor, const grk_rect32& bounds, grk_pt32 cblk_expn);
   virtual ~Precinct();
   void deleteTagTrees();
   grk_rect32 getCodeBlockBounds(uint64_t cblkno);
   TagTreeU16* getInclTree(void);
   TagTreeU8* getImsbTree(void);
   uint32_t getCblkGridwidth(void);
   uint32_t getCblkGridHeight(void);
   uint32_t getNominalBlockSize(void);
   uint64_t getNumCblks(void);
   CompressCodeblock* getCompressedBlockPtr(uint64_t cblkno);
   DecompressCodeblock* getDecompressedBlockPtr(uint64_t cblkno);
   DecompressCodeblock* tryGetDecompressedBlockPtr(uint64_t cblkno);
   grk_pt32 getCblkExpn(void);
   grk_rect32 getCblkGrid(void);
   uint64_t precinctIndex;
   uint16_t numLayers_;

 private:
   PrecinctImpl* impl;
   grk_pt32 cblk_expn_;
   PrecinctImpl* getImpl(void)
   {
	  impl->initCodeBlocks(numLayers_, this);
	  return impl;
   }
};

} // namespace grk
