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
 */
#pragma once
#include "grk_includes.h"

namespace grk
{
template<typename T, typename P>
class BlockCache : public SparseCache<T>
{
  public:
	BlockCache(uint64_t maxChunkSize, P* blockInitializer)
		: SparseCache<T>(maxChunkSize), m_blockInitializer(blockInitializer)
	{}
	virtual ~BlockCache() = default;

  protected:
	virtual T* create(uint64_t index) override
	{
		auto item = new T();
		m_blockInitializer->initCodeBlock(item, index);
		return item;
	}

  private:
	P* m_blockInitializer;
};

struct PrecinctImpl
{
	PrecinctImpl(bool isCompressor, grkRectU32* bounds, grkPointU32 cblk_expn)
		: enc(nullptr), dec(nullptr), m_bounds(*bounds),
		  m_cblk_expn(cblk_expn), m_isCompressor(isCompressor),
		  incltree(nullptr), imsbtree(nullptr)
	{
		m_cblk_grid =
			grkRectU32(floordivpow2(bounds->x0, cblk_expn.x), floordivpow2(bounds->y0, cblk_expn.y),
					   ceildivpow2<uint32_t>(bounds->x1, cblk_expn.x),
					   ceildivpow2<uint32_t>(bounds->y1, cblk_expn.y));
	}
	~PrecinctImpl()
	{
		deleteTagTrees();
		delete enc;
		delete dec;
	}
	grkRectU32 getCodeBlockBounds(uint64_t cblkno)
	{
		auto cblk_start = grkPointU32(
			(m_cblk_grid.x0 + (uint32_t)(cblkno % m_cblk_grid.width())) << m_cblk_expn.x,
			(m_cblk_grid.y0 + (uint32_t)(cblkno / m_cblk_grid.width())) << m_cblk_expn.y);
		auto cblk_bounds =
			grkRectU32(cblk_start.x, cblk_start.y, cblk_start.x + (1U << m_cblk_expn.x),
					   cblk_start.y + (1U << m_cblk_expn.y));

		return cblk_bounds.intersection(&m_bounds);
	}
	bool initCodeBlocks(grkRectU32* bounds)
	{
		if((m_isCompressor && enc) || (!m_isCompressor && dec))
			return true;
		m_bounds = *bounds;
		auto numBlocks = m_cblk_grid.area();
		if(!numBlocks)
			return true;
		if(m_isCompressor)
			enc = new BlockCache<CompressCodeblock, PrecinctImpl>(numBlocks, this);
		else
			dec = new BlockCache<DecompressCodeblock, PrecinctImpl>(numBlocks, this);

		return true;
	}
	template<typename T>
	bool initCodeBlock(T* block, uint64_t cblkno)
	{
		if(block->non_empty())
			return true;
		if(!block->init())
			return false;
		block->setRect(getCodeBlockBounds(cblkno));

		return true;
	}
	void deleteTagTrees()
	{
		delete incltree;
		incltree = nullptr;
		delete imsbtree;
		imsbtree = nullptr;
	}
	TagTreeU16* getIncludeTagTree(void){
		// if cw == 0 or ch == 0,
		// then the precinct has no code blocks, therefore
		// no need for inclusion and msb tag trees
		auto grid_width = m_cblk_grid.width();
		auto grid_height = m_cblk_grid.height();
		if(grid_width > 0 && grid_height > 0)
		{
			if(!incltree)
			{
				try
				{
					incltree = new TagTreeU16(grid_width, grid_height);
				}
				catch(std::exception& e)
				{
					GRK_UNUSED(e);
					GRK_WARN("No incltree created.");
				}
				if(incltree && !incltree->init(grid_width, grid_height))
				{
					GRK_WARN("Failed to re-initialize incltree.");
					delete incltree;
					incltree = nullptr;
				}
			}
			return incltree;
		}
		return nullptr;
	}
	TagTreeU8* getIMsbTagTree(void){
		// if cw == 0 or ch == 0,
		// then the precinct has no code blocks, therefore
		// no need for inclusion and msb tag trees
		auto grid_width = m_cblk_grid.width();
		auto grid_height = m_cblk_grid.height();
		if(grid_width > 0 && grid_height > 0)
		{
			if(!imsbtree)
			{
				try
				{
					imsbtree = new TagTreeU8(grid_width, grid_height);
				}
				catch(std::exception& e)
				{
					GRK_UNUSED(e);
					GRK_WARN("No imsbtree created.");
				}
				if(imsbtree && !imsbtree->init(grid_width, grid_height))
				{
					GRK_WARN("Failed to re-initialize imsbtree.");
					delete imsbtree;
					imsbtree = nullptr;
				}
			}
			return imsbtree;
		}
		return nullptr;
	}
	BlockCache<CompressCodeblock, PrecinctImpl>* enc;
	BlockCache<DecompressCodeblock, PrecinctImpl>* dec;
	grkRectU32 m_cblk_grid;
	grkRectU32 m_bounds;
	grkPointU32 m_cblk_expn;
	bool m_isCompressor;
private:
	TagTreeU16* incltree; /* inclusion tree */
	TagTreeU8* imsbtree; /* IMSB tree */
};
struct Precinct : public grkRectU32
{
	Precinct(const grkRectU32& bounds, bool isCompressor, grkPointU32 cblk_expn)
		: grkRectU32(bounds), precinctIndex(0),
		  impl(new PrecinctImpl(isCompressor, this, cblk_expn)), m_cblk_expn(cblk_expn)
	{}
	~Precinct()
	{
		delete impl;
	}
	void deleteTagTrees()
	{
		impl->deleteTagTrees();
	}
	grkRectU32 getCodeBlockBounds(uint64_t cblkno)
	{
		return impl->getCodeBlockBounds(cblkno);
	}
	TagTreeU16* getInclTree(void)
	{
		return impl->getIncludeTagTree();
	}
	TagTreeU8* getImsbTree(void)
	{
		return impl->getIMsbTagTree();
	}
	uint32_t getCblkGridwidth(void)
	{
		return impl->m_cblk_grid.width();
	}
	uint32_t getCblkGridHeight(void)
	{
		return impl->m_cblk_grid.height();
	}
	uint32_t getNominalBlockSize(void)
	{
		return (1U << impl->m_cblk_expn.x) * (1U << impl->m_cblk_expn.y);
	}
	uint64_t getNumCblks(void)
	{
		return impl->m_cblk_grid.area();
	}
	CompressCodeblock* getCompressedBlockPtr(uint64_t cblkno)
	{
		return getImpl()->enc->get(cblkno);
	}
	DecompressCodeblock* getDecompressedBlockPtr(uint64_t cblkno)
	{
		return getImpl()->dec->get(cblkno);
	}
	DecompressCodeblock* tryGetDecompressedBlockPtr(uint64_t cblkno)
	{
		return getImpl()->dec->tryGet(cblkno);
	}
	grkPointU32 getCblkExpn(void)
	{
		return m_cblk_expn;
	}
	grkRectU32 getCblkGrid(void)
	{
		return impl->m_cblk_grid;
	}
	uint64_t precinctIndex;

  private:
	PrecinctImpl* impl;
	grkPointU32 m_cblk_expn;
	PrecinctImpl* getImpl(void)
	{
		impl->initCodeBlocks(this);
		return impl;
	}
};

} // namespace grk
