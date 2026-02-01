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

Precinct::Precinct(uint16_t numLayers, const Rect32_16& bounds, Point8 cblk_expn)
    : Rect32_16(bounds), numLayers_(numLayers), cblk_expn_(cblk_expn)

{}
Precinct::~Precinct()
{
  delete impl_;
}
void Precinct::deleteTagTrees()
{
  getImpl()->deleteTagTrees();
}
Rect32_16 Precinct::getCodeBlockBounds(uint32_t cblkno)
{
  return getImpl()->getCodeBlockBounds(cblkno);
}
TagTreeU16* Precinct::getInclTree(void)
{
  return getImpl()->getIncludeTagTree();
}
TagTreeU8* Precinct::getImsbTree(void)
{
  return getImpl()->getIMsbTagTree();
}
uint16_t Precinct::getCblkGridwidth(void)
{
  return getImpl()->cblk_grid_.width();
}
uint16_t Precinct::getCblkGridHeight(void)
{
  return impl_->cblk_grid_.height();
}
uint16_t Precinct::getNominalBlockSize(void)
{
  return uint16_t((1U << cblk_expn_.x) * (1U << cblk_expn_.y));
}
uint32_t Precinct::getNumCblks(void)
{
  return (uint32_t)getImpl()->cblk_grid_.area();
}
t1::CodeblockCompress* Precinct::getCompressedBlock(uint32_t cblkno)
{
  return getImpl()->enc_->get(cblkno);
}
t1::CodeblockDecompress* Precinct::getDecompressedBlock(uint32_t cblkno)
{
  return getImpl()->dec_->get(cblkno);
}
t1::CodeblockDecompress* Precinct::tryGetDecompressedBlock(uint32_t cblkno)
{
  return getImpl()->dec_->tryGet(cblkno);
}
Point8 Precinct::getCblkExpn(void)
{
  return cblk_expn_;
}
Rect32_16 Precinct::getCblkGrid(void)
{
  return getImpl()->cblk_grid_;
}

PrecinctImpl* Precinct::getImpl(void)
{
  if(!impl_)
    impl_ = makeImpl();
  return impl_;
}

PrecinctImpl* PrecinctCompress::makeImpl(void)
{
  return new PrecinctImplCompress(this, numLayers_);
}
PrecinctImpl* PrecinctDecompress::makeImpl(void)
{
  return new PrecinctImplDecompress(this, numLayers_);
}

} // namespace grk
