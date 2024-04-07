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

#include <cstdint>
#include <algorithm>

// SparseCanvas stores blocks in the canvas coordinate system. It covers the active sub-bands for
// all (reduced) resolutions

/***
 *
 * SparseCanvas stores blocks of size LBW x LBH in canvase coordinate system (with offset)
 * Blocks are only allocated for active sub-bands for reduced resolutions
 *
 * Data is pass in and out in a linear array, chunked either along the y axis
 * or along the x axis, depending on whether we are working with a horizontal strip
 * or a vertical strip of data.
 *
 *
 */

namespace grk
{
class ISparseCanvas
{
 public:
   virtual ~ISparseCanvas() = default;
   /**
	* Read window of data into dest buffer.
	*/
   virtual bool read(uint8_t resno, grk_rect32 window, int32_t* dest, const uint32_t destChunkY,
					 const uint32_t destChunkX) = 0;
   /**
	* Write window of data from src buffer
	*/
   virtual bool write(uint8_t resno, grk_rect32 window, const int32_t* src,
					  const uint32_t srcChunkY, const uint32_t srcChunkX) = 0;

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
		 throw std::runtime_error("invalid window for sparse canvas");
	  grid = bounds.scaleDownPow2(LBW, LBH);
	  auto blockCount = grid.area();
	  blocks = new SparseBlock*[blockCount];
	  for(uint64_t i = 0; i < blockCount; ++i)
		 blocks[i] = nullptr;
   }
   SparseCanvas(uint32_t width, uint32_t height) : SparseCanvas(grk_rect32(0, 0, width, height)) {}
   ~SparseCanvas()
   {
	  if(blocks)
	  {
		 for(uint64_t i = 0; i < (uint64_t)grid.width() * grid.height(); i++)
		 {
			delete(blocks[i]);
			blocks[i] = nullptr;
		 }
		 delete[] blocks;
	  }
   }
   bool read(uint8_t resno, grk_rect32 window, int32_t* dest, const uint32_t destChunkY,
			 const uint32_t destChunkX)
   {
	  return readWrite(resno, window, dest, destChunkY, destChunkX, true);
   }
   bool write(uint8_t resno, grk_rect32 window, const int32_t* src, const uint32_t srcChunkY,
			  const uint32_t srcChunkX)
   {
	  return readWrite(resno, window, (int32_t*)src, srcChunkY, srcChunkX, false);
   }
   bool alloc(grk_rect32 win, bool zeroOutBuffer)
   {
	  if(!SparseCanvas::isWindowValid(win))
		 return true;
	  uint32_t blockWinHeight = 0;
	  uint32_t gridY = win.y0 >> LBH;
	  for(uint32_t y = win.y0; y < win.y1; gridY++, y += blockWinHeight)
	  {
		 blockWinHeight = (y == win.y0) ? blockHeight - (win.y0 & (blockHeight - 1)) : blockHeight;
		 blockWinHeight = (std::min<uint32_t>)(blockWinHeight, win.y1 - y);
		 uint32_t gridX = win.x0 >> LBW;
		 uint32_t blockWinWidth = 0;
		 for(uint32_t x = win.x0; x < win.x1; gridX++, x += blockWinWidth)
		 {
			blockWinWidth = (x == win.x0) ? blockWidth - (win.x0 & (blockWidth - 1)) : blockWidth;
			blockWinWidth = (std::min<uint32_t>)(blockWinWidth, win.x1 - x);
			if(!grid.contains(gridX, gridY))
			{
			   Logger::logger_.warn(
				   "sparse canvas : attempt to allocate a block (%u,%u) outside block "
				   "grid bounds (%u,%u,%u,%u)",
				   gridX, gridY, grid.x0, grid.y0, grid.x1, grid.y1);
			   return false;
			}
			auto srcBlock = getBlock(gridX, gridY);
			if(!srcBlock)
			{
			   auto b = new SparseBlock();
			   b->alloc(blockWidth * blockHeight, zeroOutBuffer);
			   assert(grid.contains(gridX, gridY));
			   assert(b->data);
			   uint64_t blockInd = (uint64_t)(gridY - grid.y0) * grid.width() + (gridX - grid.x0);
			   blocks[blockInd] = b;
			}
		 }
	  }
	  return true;
   }

 private:
   inline SparseBlock* getBlock(uint32_t block_x, uint32_t block_y)
   {
	  uint64_t index = (uint64_t)(block_y - grid.y0) * grid.width() + (block_x - grid.x0);
	  return blocks[index];
   }
   bool isWindowValid(grk_rect32 win)
   {
	  return !(win.x0 >= bounds.x1 || win.x1 <= win.x0 || win.x1 > bounds.x1 ||
			   win.y0 >= bounds.y1 || win.y1 <= win.y0 || win.y1 > bounds.y1);
   }
   bool readWrite(uint8_t resno, grk_rect32 win, int32_t* buf, const uint32_t spacingX,
				  const uint32_t spacingY, bool isReadOperation)
   {
	  if(!win.valid())
		 return false;
	  assert(!isReadOperation || buf);

	  if(!isWindowValid(win))
	  {
		 Logger::logger_.warn(
			 "Sparse canvas @ res %u, attempt to read/write invalid window (%u,%u,%u,%u) "
			 "for bounds (%u,%u,%u,%u).",
			 resno, win.x0, win.y0, win.x1, win.y1, bounds.x0, bounds.y0, bounds.x1, bounds.y1);
		 return false;
	  }
	  assert(spacingY != 0 || win.height() == 1);
	  assert((spacingY <= 1 && spacingX >= 1) || (spacingY >= 1 && spacingX == 1));

	  uint32_t gridY = win.y0 >> LBH;
	  uint32_t blockWinHeight = 0;
	  for(uint32_t y = win.y0; y < win.y1; gridY++, y += blockWinHeight)
	  {
		 blockWinHeight = (y == win.y0) ? blockHeight - (win.y0 & (blockHeight - 1)) : blockHeight;
		 uint32_t blockOffsetY = blockHeight - blockWinHeight;
		 blockWinHeight = (std::min<uint32_t>)(blockWinHeight, win.y1 - y);
		 uint32_t gridX = win.x0 >> LBW;
		 uint32_t blockWinWidth = 0;
		 for(uint32_t x = win.x0; x < win.x1; gridX++, x += blockWinWidth)
		 {
			blockWinWidth = (x == win.x0) ? blockWidth - (win.x0 & (blockWidth - 1)) : blockWidth;
			uint32_t blockOffsetX = blockWidth - blockWinWidth;
			blockWinWidth = (std::min<uint32_t>)(blockWinWidth, win.x1 - x);
			if(!grid.contains(gridX, gridY))
			{
			   Logger::logger_.warn(
				   "sparse canvas @ resno %u, Attempt to access a block (%u,%u) outside "
				   "block grid bounds",
				   resno, gridX, gridY);
			   return false;
			}
			auto srcBlock = getBlock(gridX, gridY);
			if(!srcBlock)
			{
			   Logger::logger_.warn(
				   "sparse canvas @ resno %u, %s op: missing block (%u,%u,%u,%u) for %s "
				   "(%u,%u,%u,%u). Skipping.",
				   resno, isReadOperation ? "read" : "write", bounds.x0 + gridX * blockWidth,
				   bounds.y0 + gridY * blockHeight, bounds.x0 + (gridX + 1) * blockWidth,
				   bounds.y0 + (gridY + 1) * blockHeight, isReadOperation ? "read" : "write",
				   win.x0, win.y0, win.x1, win.y1);
			   continue;
			}
			if(isReadOperation)
			{
			   auto src = srcBlock->data + ((uint64_t)blockOffsetY << LBW) + blockOffsetX;
			   auto dest = buf + (y - win.y0) * spacingY + (x - win.x0) * spacingX;
			   for(uint32_t blockY = 0; blockY < blockWinHeight; blockY++)
			   {
				  uint64_t destInd = 0;
				  for(uint32_t blockX = 0; blockX < blockWinWidth; blockX++)
				  {
#ifdef GRK_DEBUG_VALGRIND
					 size_t val = grk_memcheck<int32_t>(src + blockX, 1);
					 if(val != grk_mem_ok)
						Logger::logger_.error("sparse canvas @resno %u, read block(%u,%u) : "
											  "uninitialized at location (%u,%u)",
											  resno, gridX, gridY, x + blockX, y_);
#endif
					 dest[destInd] = src[blockX];
					 destInd += spacingX;
				  }
				  dest += spacingY;
				  src += blockWidth;
			   }
			}
			else
			{
			   const int32_t* src = nullptr;
			   if(buf)
				  src = buf + (y - win.y0) * spacingY + (x - win.x0) * spacingX;
			   auto dest = srcBlock->data + ((uint64_t)blockOffsetY << LBW) + blockOffsetX;
			   for(uint32_t blockY = 0; blockY < blockWinHeight; blockY++)
			   {
				  uint64_t srcInd = 0;
				  for(uint32_t blockX = 0; blockX < blockWinWidth; blockX++)
				  {
#ifdef GRK_DEBUG_VALGRIND
					 if(src)
					 {
						grk_pt32 pt((uint32_t)(x + blockX), y_);
						size_t val = grk_memcheck<int32_t>(src + srcInd, 1);
						if(val != grk_mem_ok)
						   Logger::logger_.error("sparse canvas @ resno %u,  write block(%u,%u): "
												 "uninitialized at location (%u,%u)",
												 resno, gridX, gridY, x + blockX, y_);
					 }
#endif
					 dest[blockX] = src ? src[srcInd] : 0;
					 srcInd += spacingX;
				  }
				  if(src)
					 src += spacingY;
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
   grk_rect32 bounds; // canvas bounds
   grk_rect32 grid; // block grid bounds
};

} // namespace grk
