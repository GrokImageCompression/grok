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

#include <cstdint>
#include <algorithm>

// SparseCanvas stores blocks in the canvas coordinate system. It covers the active sub-bands for
// all (reduced) resolutions

namespace grk
{
class ISparseCanvas
{
  public:
	virtual ~ISparseCanvas() = default;
	virtual bool read(uint8_t resno, eBandOrientation bandOrientation, grk_rect32 window,
					  int32_t* dest, const uint32_t destinationColumnStride,
					  const uint32_t destinationLineStride, bool forceReturnTrue) = 0;
	virtual bool write(uint8_t resno, eBandOrientation bandOrientation, grk_rect32 window,
					   const int32_t* src, const uint32_t src_columnStride,
					   const uint32_t src_lineStride, bool forceReturnTrue) = 0;
	virtual bool alloc(grk_rect32 window, bool zeroOutBuffer) = 0;
};
struct SparseBlock
{
	SparseBlock(void) : data(nullptr) {}
	~SparseBlock()
	{
		delete[] data;
	}
	void alloc(uint32_t block_area, bool zeroOutBuffer)
	{
		data = new int32_t[block_area];
		if(zeroOutBuffer)
			memset(data, 0, block_area * sizeof(int32_t));
	}
	int32_t* data;
};
template<uint32_t LBW, uint32_t LBH>
class SparseCanvas : public ISparseCanvas
{
  public:
	SparseCanvas(grk_rect32 bds)
		: blockWidth(1 << LBW), blockHeight(1 << LBH), blocks(nullptr), bounds(bds)
	{
		if(!bounds.width() || !bounds.height() || !LBW || !LBH)
			throw std::runtime_error("invalid window for sparse buffer");
		uint32_t grid_off_x = floordivpow2(bounds.x0, LBW);
		uint32_t grid_off_y = floordivpow2(bounds.y0, LBH);
		uint32_t grid_x 	= ceildivpow2<uint32_t>(bounds.x1, LBW);
		uint32_t grid_y 	= ceildivpow2<uint32_t>(bounds.y1, LBH);
		gridBounds 			= grk_rect32(grid_off_x, grid_off_y, grid_x, grid_y);
		auto blockCount 	= gridBounds.area();
		blocks = new SparseBlock*[blockCount];
		for(uint64_t i = 0; i < blockCount; ++i)
			blocks[i] = nullptr;
	}
	SparseCanvas(uint32_t width, uint32_t height) : SparseCanvas(grk_rect32(0, 0, width, height)) {}
	~SparseCanvas()
	{
		if(blocks)
		{
			for(uint64_t i = 0; i < (uint64_t)gridBounds.width() * gridBounds.height(); i++)
			{
				delete(blocks[i]);
				blocks[i] = nullptr;
			}
			delete[] blocks;
		}
	}
	bool read(uint8_t resno, eBandOrientation bandOrientation, grk_rect32 window, int32_t* dest,
			  const uint32_t destinationColumnStride, const uint32_t destinationLineStride, bool forceReturnTrue)
	{
		GRK_UNUSED(bandOrientation);
		return readWrite(resno, window, dest, destinationColumnStride, destinationLineStride, forceReturnTrue,
							 true);
	}
	bool write(uint8_t resno, eBandOrientation bandOrientation, grk_rect32 window,
			   const int32_t* src, const uint32_t src_columnStride, const uint32_t src_lineStride,
			   bool forceReturnTrue)
	{
		GRK_UNUSED(bandOrientation);
		return readWrite(resno, window, (int32_t*)src, src_columnStride, src_lineStride,
							 forceReturnTrue, false);
	}
	bool alloc(grk_rect32 win, bool zeroOutBuffer)
	{
		if(!SparseCanvas::isWindowValid(win))
			return true;
		uint32_t yIncrement = 0;
		uint32_t block_y = win.y0 >> LBH;
		for(uint32_t y = win.y0; y < win.y1; block_y++, y += yIncrement)
		{
			yIncrement = (y == win.y0) ? blockHeight - (win.y0 & (blockHeight - 1)) : blockHeight;
			yIncrement = (std::min<uint32_t>)(yIncrement, win.y1 - y);
			uint32_t block_x = win.x0 >> LBW;
			uint32_t xIncrement = 0;
			for(uint32_t x = win.x0; x < win.x1; block_x++, x += xIncrement)
			{
				xIncrement = (x == win.x0) ? blockWidth - (win.x0 & (blockWidth - 1)) : blockWidth;
				xIncrement = (std::min<uint32_t>)(xIncrement, win.x1 - x);
				if(!gridBounds.contains(grk_pt32(block_x, block_y)))
				{
					GRK_ERROR("sparse buffer : attempt to allocate a block (%d,%d) outside block "
							  "grid bounds (%d,%d,%d,%d)",
							  block_x, block_y,
							  gridBounds.x0,
							  gridBounds.y0,
							  gridBounds.x1,
							  gridBounds.y1);
					return false;
				}
				auto srcBlock = getBlock(block_x, block_y);
				if(!srcBlock)
				{
					auto b = new SparseBlock();
					b->alloc(blockWidth * blockHeight, zeroOutBuffer);
					assert(gridBounds.contains(grk_pt32(block_x, block_y)));
					assert(b->data);
					uint64_t index = (uint64_t)(block_y - gridBounds.y0) * gridBounds.width() +
									 (block_x - gridBounds.x0);
					blocks[index] = b;
				}
			}
		}
		return true;
	}
  private:
	inline SparseBlock* getBlock(uint32_t block_x, uint32_t block_y)
	{
		uint64_t index =
			(uint64_t)(block_y - gridBounds.y0) * gridBounds.width() + (block_x - gridBounds.x0);
		return blocks[index];
	}
	bool isWindowValid(grk_rect32 win)
	{
		return !(win.x0 >= bounds.x1 || win.x1 <= win.x0 || win.x1 > bounds.x1 ||
				 win.y0 >= bounds.y1 || win.y1 <= win.y0 || win.y1 > bounds.y1);
	}
	bool readWrite(uint8_t resno, grk_rect32 win, int32_t* buf, const uint32_t buf_columnStride,
					   const uint32_t buf_lineStride, bool forceReturnTrue, bool isReadOperation)
	{
		if(!isWindowValid(win))
		{
			// fill the client buffer with zeros in this case
			if(forceReturnTrue && isReadOperation)
			{
				GRK_WARN("Sparse buffer @ res %d, attempt to read invalid window (%d,%d,%d,%d). "
						 "Filling with zeros.",
						 resno, win.x0, win.y0, win.x1, win.y1);
				for(uint32_t y = win.y0; y < win.y1; ++y)
				{
					auto bufPtr = buf + (y - win.y0) * buf_lineStride;
					for(uint32_t x = win.x0; x < win.x1; ++x)
					{
						*bufPtr = 0;
						bufPtr += buf_columnStride;
					}
				}
			}
			return forceReturnTrue;
		}
		const uint64_t lineStride = buf_lineStride;
		const uint64_t columnStride = buf_columnStride;
		uint32_t block_y = win.y0 >> LBH;
		uint32_t yIncrement = 0;
		for(uint32_t y = win.y0; y < win.y1; block_y++, y += yIncrement)
		{
			yIncrement = (y == win.y0) ? blockHeight - (win.y0 & (blockHeight - 1)) : blockHeight;
			uint32_t blockYOffset = blockHeight - yIncrement;
			yIncrement = (std::min<uint32_t>)(yIncrement, win.y1 - y);
			uint32_t block_x = win.x0 >> LBW;
			uint32_t xIncrement = 0;
			for(uint32_t x = win.x0; x < win.x1; block_x++, x += xIncrement)
			{
				xIncrement = (x == win.x0) ? blockWidth - (win.x0 & (blockWidth - 1)) : blockWidth;
				uint32_t blockXOffset = blockWidth - xIncrement;
				xIncrement = (std::min<uint32_t>)(xIncrement, win.x1 - x);
				if(!gridBounds.contains(grk_pt32(block_x, block_y)))
				{
					GRK_ERROR("sparse buffer @ resno %d, Attempt to access a block (%d,%d) outside "
							  "block grid bounds",
							  resno, block_x, block_y);
					return false;
				}
				auto srcBlock = getBlock(block_x, block_y);
				if(!srcBlock)
				{
					GRK_WARN("sparse buffer @ resno %d, %s op: missing block (%d,%d,%d,%d) for %s "
							 "(%d,%d,%d,%d)",
							 resno, isReadOperation ? "read" : "write",
							 bounds.x0 + block_x * blockWidth, bounds.y0 + block_y * blockHeight,
							 bounds.x0 + (block_x + 1) * blockWidth,
							 bounds.y0 + (block_y + 1) * blockHeight,
							 isReadOperation ? "read" : "write", win.x0, win.y0, win.x1, win.y1);
					continue;
				}
				if(isReadOperation)
				{
					const int32_t*  src =
						srcBlock->data + ((uint64_t)blockYOffset << LBW) + blockXOffset;
					auto  dest =
						buf + (y - win.y0) * lineStride + (x - win.x0) * columnStride;
					for(uint32_t j = 0; j < yIncrement; j++)
					{
						uint64_t index = 0;
						for(uint32_t k = 0; k < xIncrement; k++)
						{
#ifdef GRK_DEBUG_VALGRIND
							size_t val = grk_memcheck<int32_t>(src + k, 1);
							if(val != grk_mem_ok)
								GRK_ERROR("sparse buffer @resno %d, read block(%d,%d) : "
										  "uninitialized at location (%d,%d)",
										  resno, block_x, block_y, x + k, y_);
#endif
							dest[index] = src[k];
							index += columnStride;
						}
						dest += lineStride;
						src += blockWidth;
					}
				}
				else
				{
					const int32_t*  src = nullptr;
					if(buf)
						src = buf + (y - win.y0) * lineStride + (x - win.x0) * columnStride;
					auto  dest =
						srcBlock->data + ((uint64_t)blockYOffset << LBW) + blockXOffset;
					for(uint32_t j = 0; j < yIncrement; j++)
					{
						uint64_t index = 0;
						for(uint32_t k = 0; k < xIncrement; k++)
						{
#ifdef GRK_DEBUG_VALGRIND
							if(src)
							{
								grk_pt32 pt((uint32_t)(x + k), y_);
								size_t val = grk_memcheck<int32_t>(src + index, 1);
								if(val != grk_mem_ok)
									GRK_ERROR("sparse buffer @ resno %d,  write block(%d,%d): "
											  "uninitialized at location (%d,%d)",
											  resno, block_x, block_y, x + k, y_);
							}
#endif
							dest[k] = src ? src[index] : 0;
							index += columnStride;
						}
						if(src)
							src += lineStride;
						dest += blockWidth;
					}
				}
			}
		}
		return true;
	}
  private:
	const uint32_t blockWidth;
	const uint32_t blockHeight;
	SparseBlock** blocks;
	grk_rect32 bounds;
	grk_rect32 gridBounds;
};

} // namespace grk
