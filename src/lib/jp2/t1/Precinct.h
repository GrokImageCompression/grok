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
		: SparseCache<T>(maxChunkSize), blockInitializer_(blockInitializer)
	{}
	virtual ~BlockCache() = default;

  protected:
	virtual T* create(uint64_t index) override
	{
		auto item = new T();
		blockInitializer_->initCodeBlock(item, index);
		return item;
	}

  private:
	P* blockInitializer_;
};

struct PrecinctImpl
{
	PrecinctImpl(bool isCompressor, grk_rect32* bounds, grk_pt32 cblk_expn)
		: enc(nullptr), dec(nullptr), bounds_(*bounds), cblk_expn_(cblk_expn),
		  isCompressor_(isCompressor), incltree(nullptr), imsbtree(nullptr)
	{
		cblk_grid_ =
			grk_rect32(floordivpow2(bounds->x0, cblk_expn.x), floordivpow2(bounds->y0, cblk_expn.y),
					   ceildivpow2<uint32_t>(bounds->x1, cblk_expn.x),
					   ceildivpow2<uint32_t>(bounds->y1, cblk_expn.y));
		if (!cblk_grid_.isValid()){
			GRK_ERROR("Invalid code block grid");
			throw std::exception();
		}
	}
	~PrecinctImpl()
	{
		deleteTagTrees();
		delete enc;
		delete dec;
	}
	grk_rect32 getCodeBlockBounds(uint64_t cblkno)
	{
		auto cblk_start =
			grk_pt32((cblk_grid_.x0 + (uint32_t)(cblkno % cblk_grid_.width())) << cblk_expn_.x,
				  (cblk_grid_.y0 + (uint32_t)(cblkno / cblk_grid_.width())) << cblk_expn_.y);
		auto cblk_bounds =
			grk_rect32(cblk_start.x, cblk_start.y, cblk_start.x + (1U << cblk_expn_.x),
					   cblk_start.y + (1U << cblk_expn_.y));

		return cblk_bounds.intersection(&bounds_);
	}
	bool initCodeBlocks(grk_rect32* bounds)
	{
		if((isCompressor_ && enc) || (!isCompressor_ && dec))
			return true;
		bounds_ = *bounds;
		auto numBlocks = cblk_grid_.area();
		if(!numBlocks)
			return true;
		if(isCompressor_)
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
	TagTreeU16* getIncludeTagTree(void)
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
				catch(std::exception& e)
				{
					GRK_UNUSED(e);
					GRK_WARN("No incltree created.");
					throw;
				}
			}
			return incltree;
		}
		return nullptr;
	}
	TagTreeU8* getIMsbTagTree(void)
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
				catch(std::exception& e)
				{
					GRK_UNUSED(e);
					GRK_WARN("No imsbtree created.");
					throw;
				}
			}
			return imsbtree;
		}
		return nullptr;
	}
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
	Precinct(const grk_rect32& bounds, bool isCompressor, grk_pt32 cblk_expn)
		: grk_rect32(bounds), precinctIndex(0),
		  impl(new PrecinctImpl(isCompressor, this, cblk_expn)), cblk_expn_(cblk_expn)
	{}
	~Precinct()
	{
		delete impl;
	}
	void deleteTagTrees()
	{
		impl->deleteTagTrees();
	}
	grk_rect32 getCodeBlockBounds(uint64_t cblkno)
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
		return impl->cblk_grid_.width();
	}
	uint32_t getCblkGridHeight(void)
	{
		return impl->cblk_grid_.height();
	}
	uint32_t getNominalBlockSize(void)
	{
		return (1U << impl->cblk_expn_.x) * (1U << impl->cblk_expn_.y);
	}
	uint64_t getNumCblks(void)
	{
		return impl->cblk_grid_.area();
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
	grk_pt32 getCblkExpn(void)
	{
		return cblk_expn_;
	}
	grk_rect32 getCblkGrid(void)
	{
		return impl->cblk_grid_;
	}
	uint64_t precinctIndex;

  private:
	PrecinctImpl* impl;
	grk_pt32 cblk_expn_;
	PrecinctImpl* getImpl(void)
	{
		impl->initCodeBlocks(this);
		return impl;
	}
};

} // namespace grk
