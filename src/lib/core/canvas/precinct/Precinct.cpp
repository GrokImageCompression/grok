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
#include "BlockCache.h"
#include "CodeblockDecompress.h"
#include "CodeblockCompress.h"
#include "Precinct.h"
#include "PrecinctImpl.h"

namespace grk
{

Precinct::Precinct(uint16_t numLayers, uint64_t precinctIndex, const Rect32_16& bounds,
                   Point8 cblk_expn)
    : Rect32_16(bounds), precinctIndex_(precinctIndex), numLayers_(numLayers), cblk_expn_(cblk_expn)

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
uint32_t Precinct::getNumCblks(void)
{
  return (uint32_t)getImpl()->cblk_grid_.area();
}
t1::CodeblockCompress* Precinct::getCompressBlock(uint32_t cblkno)
{
  return getImpl()->enc_ + cblkno;
}
t1::CodeblockDecompress* Precinct::getDecompressBlock(uint32_t cblkno)
{
  return getImpl()->dec_->get(cblkno);
}
t1::CodeblockDecompress* Precinct::tryGetDecompressBlock(uint32_t cblkno)
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

void PrecinctCompress::compactBlockStorage(void)
{
  auto impl = getImpl();
  if(!impl->blockStorage_)
    return;
  size_t passBytes = 0;
  size_t codeBytes = 0;
  for(uint32_t cblkno = 0; cblkno < impl->numEnc_; ++cblkno)
  {
    auto cblk = impl->enc_ + cblkno;
    passBytes += (size_t)cblk->getNumPasses() * sizeof(t1::CodePass);
    if(cblk->getNumPasses())
      codeBytes += cblk->getLastPass()->rate_;
  }
  size_t compactedBytes = passBytes + codeBytes;
  auto compacted = std::make_unique_for_overwrite<uint8_t[]>(compactedBytes);
  auto passDestination = reinterpret_cast<t1::CodePass*>(compacted.get());
  auto codeDestination = compacted.get() + passBytes;
  for(uint32_t cblkno = 0; cblkno < impl->numEnc_; ++cblkno)
  {
    auto cblk = impl->enc_ + cblkno;
    uint8_t numPasses = cblk->getNumPasses();
    if(!numPasses)
      continue;
    std::memcpy(passDestination, cblk->getPass(0), numPasses * sizeof(t1::CodePass));
    cblk->setPasses(passDestination);
    passDestination += numPasses;
    uint16_t codeLength = cblk->getLastPass()->rate_;
    std::memcpy(codeDestination, cblk->getCompressedBytes(), codeLength);
    cblk->setCompressedBytes(codeDestination);
    codeDestination += codeLength;
  }
  impl->blockStorage_->replaceChunks(std::move(compacted), compactedBytes);
}

PrecinctImpl* PrecinctCompress::makeImpl(void)
{
  return new PrecinctImplCompress(this, numLayers_, maxPasses_);
}
PrecinctImpl* PrecinctDecompress::makeImpl(void)
{
  return new(arena_) PrecinctImplDecompress(this, numLayers_, arena_);
}

} // namespace grk
