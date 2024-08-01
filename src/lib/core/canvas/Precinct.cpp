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
#include "grk_includes.h"

namespace grk
{

PrecinctImpl::PrecinctImpl(bool isCompressor, grk_rect32* bounds, grk_pt32 cblk_expn)
	: enc(nullptr), dec(nullptr), bounds_(*bounds), cblk_expn_(cblk_expn),
	  isCompressor_(isCompressor), incltree(nullptr), imsbtree(nullptr)
{
   cblk_grid_ =
	   grk_rect32(floordivpow2(bounds->x0, cblk_expn.x), floordivpow2(bounds->y0, cblk_expn.y),
				  ceildivpow2<uint32_t>(bounds->x1, cblk_expn.x),
				  ceildivpow2<uint32_t>(bounds->y1, cblk_expn.y));
   if(!cblk_grid_.valid())
   {
	  Logger::logger_.error("Invalid code block grid");
	  throw std::exception();
   }
}
PrecinctImpl::~PrecinctImpl()
{
   deleteTagTrees();
   delete enc;
   delete dec;
}
grk_rect32 PrecinctImpl::getCodeBlockBounds(uint64_t cblkno)
{
   auto cblk_start =
	   grk_pt32((cblk_grid_.x0 + (uint32_t)(cblkno % cblk_grid_.width())) << cblk_expn_.x,
				(cblk_grid_.y0 + (uint32_t)(cblkno / cblk_grid_.width())) << cblk_expn_.y);
   auto cblk_bounds = grk_rect32(cblk_start.x, cblk_start.y, cblk_start.x + (1U << cblk_expn_.x),
								 cblk_start.y + (1U << cblk_expn_.y));

   return cblk_bounds.intersection(&bounds_);
}
bool PrecinctImpl::initCodeBlocks(uint16_t numLayers, grk_rect32* bounds)
{
   if((isCompressor_ && enc) || (!isCompressor_ && dec))
	  return true;
   bounds_ = *bounds;
   auto num_blocks = cblk_grid_.area();
   if(!num_blocks)
	  return true;
   if(isCompressor_)
	  enc = new BlockCache<CompressCodeblock, PrecinctImpl>(numLayers, num_blocks, this);
   else
	  dec = new BlockCache<DecompressCodeblock, PrecinctImpl>(numLayers, num_blocks, this);

   return true;
}
template<typename T>
bool PrecinctImpl::initCodeBlock(T* block, uint64_t cblkno)
{
   if(!block->empty())
	  return true;
   block->init();
   block->setRect(getCodeBlockBounds(cblkno));

   return true;
}
void PrecinctImpl::deleteTagTrees()
{
   delete incltree;
   incltree = nullptr;
   delete imsbtree;
   imsbtree = nullptr;
}
TagTreeU16* PrecinctImpl::getIncludeTagTree(void)
{
   // if cw == 0 or ch == 0,
   // then the precinct has no code blocks, therefore
   // no need for inclusion and msb tag trees
   auto grid_width = cblk_grid_.width();
   auto grid_height = cblk_grid_.height();
   if(grid_width > 0 && grid_height > 0)
   {
	  if(!incltree)
	  {
		 try
		 {
			incltree = new TagTreeU16(grid_width, grid_height);
		 }
		 catch([[maybe_unused]] const std::exception& e)
		 {
			Logger::logger_.warn("No incltree created.");
			throw;
		 }
	  }
	  return incltree;
   }
   return nullptr;
}
TagTreeU8* PrecinctImpl::getIMsbTagTree(void)
{
   // if cw == 0 or ch == 0,
   // then the precinct has no code blocks, therefore
   // no need for inclusion and msb tag trees
   auto grid_width = cblk_grid_.width();
   auto grid_height = cblk_grid_.height();
   if(grid_width > 0 && grid_height > 0)
   {
	  if(!imsbtree)
	  {
		 try
		 {
			imsbtree = new TagTreeU8(grid_width, grid_height);
		 }
		 catch([[maybe_unused]] const std::exception& e)
		 {
			Logger::logger_.warn("No imsbtree created.");
			throw;
		 }
	  }
	  return imsbtree;
   }
   return nullptr;
}

Precinct::Precinct(TileProcessor* tileProcessor, const grk_rect32& bounds, grk_pt32 cblk_expn)
	: grk_rect32(bounds), precinctIndex(0),
	  numLayers_(tileProcessor->getTileCodingParams()->num_layers_),
	  impl(new PrecinctImpl(tileProcessor->isCompressor(), this, cblk_expn)), cblk_expn_(cblk_expn)

{}
Precinct::~Precinct()
{
   delete impl;
}
void Precinct::deleteTagTrees()
{
   impl->deleteTagTrees();
}
grk_rect32 Precinct::getCodeBlockBounds(uint64_t cblkno)
{
   return impl->getCodeBlockBounds(cblkno);
}
TagTreeU16* Precinct::getInclTree(void)
{
   return impl->getIncludeTagTree();
}
TagTreeU8* Precinct::getImsbTree(void)
{
   return impl->getIMsbTagTree();
}
uint32_t Precinct::getCblkGridwidth(void)
{
   return impl->cblk_grid_.width();
}
uint32_t Precinct::getCblkGridHeight(void)
{
   return impl->cblk_grid_.height();
}
uint32_t Precinct::getNominalBlockSize(void)
{
   return (1U << impl->cblk_expn_.x) * (1U << impl->cblk_expn_.y);
}
uint64_t Precinct::getNumCblks(void)
{
   return impl->cblk_grid_.area();
}
CompressCodeblock* Precinct::getCompressedBlockPtr(uint64_t cblkno)
{
   return getImpl()->enc->get(cblkno);
}
DecompressCodeblock* Precinct::getDecompressedBlockPtr(uint64_t cblkno)
{
   return getImpl()->dec->get(cblkno);
}
DecompressCodeblock* Precinct::tryGetDecompressedBlockPtr(uint64_t cblkno)
{
   return getImpl()->dec->tryGet(cblkno);
}
grk_pt32 Precinct::getCblkExpn(void)
{
   return cblk_expn_;
}
grk_rect32 Precinct::getCblkGrid(void)
{
   return impl->cblk_grid_;
}

} // namespace grk
