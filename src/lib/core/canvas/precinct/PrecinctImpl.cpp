/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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

#include "geometry.h"
#include "BitIO.h"
#include "TagTree.h"
#include "SparseCache.h"
#include "CodeblockDecompress.h"
#include "CodeblockCompress.h"
#include "Precinct.h"
#include "PrecinctImpl.h"

namespace grk
{

PrecinctImpl::PrecinctImpl(Precinct* prec)
    : enc_(nullptr), dec_(nullptr), prec_(prec), incltree_(nullptr), imsbtree_(nullptr)
{
  if(!genCodeBlockGrid())
    throw std::runtime_error("PrecinctImpl: unable to generate code block grid");
}
PrecinctImpl::~PrecinctImpl()
{
  deleteTagTrees();
  delete enc_;
  delete dec_;
}
bool PrecinctImpl::genCodeBlockGrid(void)
{
  auto cblk_expn = prec_->getCblkExpn();
  uint32_t x = floordivpow2(prec_->x0(), cblk_expn.x);
  uint32_t y = floordivpow2(prec_->y0(), cblk_expn.y);
  uint16_t w = uint16_t(ceildivpow2<uint32_t>(prec_->x1(), cblk_expn.x) - x);
  uint16_t h = uint16_t(ceildivpow2<uint32_t>(prec_->y1(), cblk_expn.y) - y);
  cblk_grid_ = Rect32_16(x, y, w, h);
  return cblk_grid_.valid();
}
Rect32_16 PrecinctImpl::getCodeBlockBounds(uint32_t cblkno)
{
  auto cblk_expn = prec_->getCblkExpn();
  auto cblk_start =
      Point32((cblk_grid_.x0() + (uint32_t)(cblkno % cblk_grid_.width())) << cblk_expn.x,
              (cblk_grid_.y0() + (uint32_t)(cblkno / cblk_grid_.width())) << cblk_expn.y);
  auto cblk_bounds = Rect32_16(cblk_start.x, cblk_start.y, uint16_t(1U << cblk_expn.x),
                               uint16_t(1U << cblk_expn.y));

  return cblk_bounds.intersection(prec_);
}

template<typename T>
void PrecinctImpl::initCodeBlock(T* block, uint32_t cblkno)
{
  if(!block->empty())
    return;
  block->init();
  block->setRect(getCodeBlockBounds(cblkno));
}
void PrecinctImpl::deleteTagTrees()
{
  delete incltree_;
  incltree_ = nullptr;
  delete imsbtree_;
  imsbtree_ = nullptr;
}
bool PrecinctImpl::hasCodeBlocks(void)
{
  return cblk_grid_.width() > 0 && cblk_grid_.height() > 0;
}
TagTreeU16* PrecinctImpl::getIncludeTagTree(void)
{
  // if the precinct has no code blocks, then
  // no need for inclusion and msb tag trees
  if(!hasCodeBlocks())
    return nullptr;
  if(incltree_)
    return incltree_;
  try
  {
    incltree_ = new TagTreeU16(cblk_grid_.width(), cblk_grid_.height());
  }
  catch([[maybe_unused]] const std::exception& e)
  {
    grklog.warn("No incltree created.");
    throw;
  }
  return incltree_;
}
TagTreeU8* PrecinctImpl::getIMsbTagTree(void)
{
  // if the precinct has no code blocks, then
  // no need for inclusion and msb tag trees
  if(!hasCodeBlocks())
    return nullptr;
  if(imsbtree_)
    return imsbtree_;
  try
  {
    imsbtree_ = new TagTreeU8(cblk_grid_.width(), cblk_grid_.height());
  }
  catch([[maybe_unused]] const std::exception& e)
  {
    grklog.warn("No imsbtree created.");
    throw;
  }

  return imsbtree_;
}

PrecinctImplCompress::PrecinctImplCompress(Precinct* prec, uint16_t numLayers) : PrecinctImpl(prec)
{
  auto num_blocks = cblk_grid_.area();
  if(num_blocks)
  {
    enc_ = new BlockCache<t1::CodeblockCompress, PrecinctImpl>(numLayers, num_blocks, this);
  }
}
PrecinctImplDecompress::PrecinctImplDecompress(Precinct* prec, uint16_t numLayers)
    : PrecinctImpl(prec)
{
  auto num_blocks = cblk_grid_.area();
  if(num_blocks)
  {
    dec_ = new BlockCache<t1::CodeblockDecompress, PrecinctImpl>(numLayers, num_blocks, this);
  }
}

} // namespace grk
