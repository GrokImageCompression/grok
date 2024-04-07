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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "PostT1DecompressFilters.h"
#include "grk_includes.h"
#include "htconfig.h"

const bool DEBUG_TILE_COMPONENT = false;

namespace grk
{
TileComponent::TileComponent()
	: resolutions_(nullptr), numresolutions(0), numResolutionsToDecompress(0),
	  highestResolutionDecompressed(0),
#ifdef DEBUG_LOSSLESS_T2
	  round_trip_resolutions(nullptr),
#endif
	  regionWindow_(nullptr), wholeTileDecompress(true), isCompressor_(false), window_(nullptr),
	  tccp_(nullptr)
{}

TileComponent::~TileComponent()
{
   if(resolutions_)
   {
	  for(uint32_t resno = 0; resno < numresolutions; ++resno)
	  {
		 auto res = resolutions_ + resno;
		 for(uint32_t bandIndex = 0; bandIndex < 3; ++bandIndex)
		 {
			auto band = res->tileBand + bandIndex;
			for(auto prc : band->precincts)
			   delete prc;
			band->precincts.clear();
		 }
	  }
	  delete[] resolutions_;
   }
   dealloc();
}
void TileComponent::dealloc(void)
{
   delete regionWindow_;
   regionWindow_ = nullptr;
   delete window_;
   window_ = nullptr;
}
/**
 * Initialize tile component in unreduced tile component coordinates
 * (tile component coordinates take sub-sampling into account).
 *
 */
bool TileComponent::init(TileProcessor* tileProcessor, grk_rect32 unreducedTileComp, uint8_t prec,
						 TileComponentCodingParams* tccp)
{
   auto cp = tileProcessor->cp_;
   isCompressor_ = tileProcessor->isCompressor();
   wholeTileDecompress = cp->wholeTileDecompress_;
   tccp_ = tccp;

   // 1. calculate resolution bounds, precinct bounds and precinct grid
   // all in canvas coordinates (with subsampling)
   numresolutions = tccp_->numresolutions;
   numResolutionsToDecompress = numresolutions < cp->coding_params_.dec_.reduce_
									? 1
									: (uint8_t)(numresolutions - cp->coding_params_.dec_.reduce_);
   resolutions_ = new Resolution[numresolutions];
   for(uint8_t resno = 0; resno < numresolutions; ++resno)
   {
	  auto res = resolutions_ + resno;

	  res->setRect(ResSimple::getBandWindow((uint8_t)(numresolutions - (resno + 1)), BAND_ORIENT_LL,
											unreducedTileComp));

	  /* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
	  uint32_t precWidthExp = tccp_->precWidthExp[resno];
	  uint32_t precHeightExp = tccp_->precHeightExp[resno];
	  /* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
	  grk_rect32 allPrecinctsBounds;
	  allPrecinctsBounds.x0 = floordivpow2(res->x0, precWidthExp) << precWidthExp;
	  allPrecinctsBounds.y0 = floordivpow2(res->y0, precHeightExp) << precHeightExp;
	  uint64_t temp = (uint64_t)ceildivpow2<uint32_t>(res->x1, precWidthExp) << precWidthExp;
	  if(temp > UINT_MAX)
	  {
		 Logger::logger_.error("Resolution x1 value %u must be less than 2^32", temp);
		 return false;
	  }
	  allPrecinctsBounds.x1 = (uint32_t)temp;
	  temp = (uint64_t)ceildivpow2<uint32_t>(res->y1, precHeightExp) << precHeightExp;
	  if(temp > UINT_MAX)
	  {
		 Logger::logger_.error("Resolution y1 value %u must be less than 2^32", temp);
		 return false;
	  }
	  allPrecinctsBounds.y1 = (uint32_t)temp;
	  res->precinctGridWidth =
		  (res->x0 == res->x1) ? 0 : (allPrecinctsBounds.width() >> precWidthExp);
	  res->precinctGridHeight =
		  (res->y0 == res->y1) ? 0 : (allPrecinctsBounds.height() >> precHeightExp);
	  res->numTileBandWindows = (resno == 0) ? 1 : 3;
	  if(DEBUG_TILE_COMPONENT)
	  {
		 std::cout << "res: " << resno << " ";
		 res->print();
	  }
   }

   // 2. set tile component and band bounds
   auto highestNumberOfResolutions = (!isCompressor_) ? numResolutionsToDecompress : numresolutions;
   auto hightestResolution = resolutions_ + highestNumberOfResolutions - 1;
   setRect(hightestResolution);
   for(uint8_t resno = 0; resno < numresolutions; ++resno)
   {
	  auto res = resolutions_ + resno;
	  for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
	  {
		 auto band = res->tileBand + bandIndex;
		 eBandOrientation orientation =
			 (resno == 0) ? BAND_ORIENT_LL : (eBandOrientation)(bandIndex + 1);
		 band->orientation = orientation;
		 uint8_t numDecomps =
			 (resno == 0) ? (uint8_t)(numresolutions - 1U) : (uint8_t)(numresolutions - resno);
		 band->setRect(ResSimple::getBandWindow(numDecomps, band->orientation, unreducedTileComp));
	  }
   }
   // set band step size
   for(uint32_t resno = 0; resno < numresolutions; ++resno)
   {
	  auto res = resolutions_ + resno;
	  for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
	  {
		 auto band = res->tileBand + bandIndex;

		 /* Table E-1 - Sub-band gains */
		 /* BUG_WEIRD_TWO_INVK (look for this identifier in dwt.c): */
		 /* the test (!isCompressor_ && l_tccp->qmfbid == 0) is strongly */
		 /* linked to the use of two_invK instead of invK */
		 const uint32_t log2_gain = (!isCompressor_ && tccp->qmfbid == 0) ? 0
									: (band->orientation == 0)			  ? 0
									: (band->orientation == 3)			  ? 2
																		  : 1;
		 uint32_t numbps = prec + log2_gain;
		 auto offset = (resno == 0) ? 0 : 3 * resno - 2;
		 auto step_size = tccp->stepsizes + offset + bandIndex;
		 band->stepsize = (float)(((1.0 + step_size->mant / 2048.0) *
								   pow(2.0, (int32_t)(numbps - step_size->expn))));
		 // printf("res=%u, band=%u, mant=%u,expn=%u, numbps=%u, step size=
		 // %f\n",resno,band->orientation,step_size->mant,step_size->expn,numbps,
		 // band->stepsize);

		 // see Taubman + Marcellin - Equation 10.22
		 band->numbps = tccp->roishift +
						(uint8_t)std::max<int8_t>(0, int8_t(step_size->expn + tccp->numgbits - 1U));
		 // assert(band->numbps <= maxBitPlanesGRK);
	  }
   }

   // 4. initialize precincts and code blocks
   for(uint32_t resno = 0; resno < numresolutions; ++resno)
   {
	  auto res = resolutions_ + resno;
	  if(!res->init(tileProcessor, tccp_, (uint8_t)resno))
		 return false;
   }

   return true;
}
bool TileComponent::subbandIntersectsAOI(uint8_t resno, eBandOrientation orient,
										 const grk_rect32* aoi) const
{
   return window_->getBandWindowPadded(resno, orient)->nonEmptyIntersection(aoi);
}
bool TileComponent::allocRegionWindow(uint32_t numres, bool truncatedTile)
{
   grk_rect32 temp(0, 0, 0, 0);
   bool first = true;

   // 1. find outside bounds of all relevant code blocks, in relative coordinates
   for(uint8_t resno = 0; resno < numres; ++resno)
   {
	  auto res = &resolutions_[resno];
	  for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
	  {
		 auto band = res->tileBand + bandIndex;
		 auto roi = window_->getBandWindowPadded(resno, band->orientation);
		 for(auto precinct : band->precincts)
		 {
			if(precinct->empty())
			   continue;
			auto cblk_grid = precinct->getCblkGrid();
			auto cblk_expn = precinct->getCblkExpn();
			auto roi_grid = roi->scaleDownPow2(cblk_expn).clip(cblk_grid);
			auto w = cblk_grid.width();
			for(uint32_t j = roi_grid.y0; j < roi_grid.y1; ++j)
			{
			   uint64_t cblkno = (roi_grid.x0 - cblk_grid.x0) + (uint64_t)(j - cblk_grid.y0) * w;
			   for(uint32_t i = roi_grid.x0; i < roi_grid.x1; ++i)
			   {
				  auto cblkBounds = precinct->getCodeBlockBounds(cblkno);

				  // transform from canvas coordinates
				  // to buffer coordinates (relative to associated resolution origin)
				  uint32_t x = cblkBounds.x0 - band->x0;
				  uint32_t y = cblkBounds.y0 - band->y0;
				  if(band->orientation & 1)
				  {
					 auto prev_res = resolutions_ + resno - 1;
					 x += prev_res->width();
				  }
				  if(band->orientation & 2)
				  {
					 auto prev_res = resolutions_ + resno - 1;
					 y += prev_res->height();
				  }
				  // add to union of code block bounds
				  if(first)
				  {
					 temp = grk_rect32(x, y, x + cblkBounds.width(), y + cblkBounds.height());
					 first = false;
				  }
				  else
				  {
					 temp = temp.rectUnion(
						 grk_rect32(x, y, x + cblkBounds.width(), y + cblkBounds.height()));
				  }
				  cblkno++;
			   }
			}
		 }
	  }
   }

   // 2. create (padded) sparse canvas, in buffer space,
   const uint32_t blockSizeExp = 6;
   temp.grow_IN_PLACE(8);
   auto regionWindow = new SparseCanvas<blockSizeExp, blockSizeExp>(temp);

   // 3. allocate sparse blocks
   for(uint8_t resno = 0; resno < numres; ++resno)
   {
	  auto res = resolutions_ + resno;
	  for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
	  {
		 auto band = res->tileBand + bandIndex;
		 auto roi = window_->getBandWindowPadded(resno, band->orientation);
		 for(auto precinct : band->precincts)
		 {
			if(precinct->empty())
			   continue;
			auto cblk_grid = precinct->getCblkGrid();
			auto cblk_expn = precinct->getCblkExpn();
			auto roi_grid = roi->scaleDownPow2(cblk_expn).clip(cblk_grid);
			auto w = cblk_grid.width();
			for(uint32_t gridY = roi_grid.y0; gridY < roi_grid.y1; ++gridY)
			{
			   uint64_t cblkno =
				   (roi_grid.x0 - cblk_grid.x0) + (uint64_t)(gridY - cblk_grid.y0) * w;
			   for(uint32_t gridX = roi_grid.x0; gridX < roi_grid.x1; ++gridX)
			   {
				  auto cblkBounds = precinct->getCodeBlockBounds(cblkno);

				  // transform from canvas coordinates
				  // to buffer coordinates (relative to associated resolution origin)
				  uint32_t x = cblkBounds.x0 - band->x0;
				  uint32_t y = cblkBounds.y0 - band->y0;
				  if(band->orientation & 1)
				  {
					 auto prev_res = resolutions_ + resno - 1;
					 x += prev_res->width();
				  }
				  if(band->orientation & 2)
				  {
					 auto prev_res = resolutions_ + resno - 1;
					 y += prev_res->height();
				  }

				  if(!regionWindow->alloc(
						 grk_rect32(x, y, x + cblkBounds.width(), y + cblkBounds.height()),
						 truncatedTile))
				  {
					 delete regionWindow;
					 throw std::runtime_error("unable to allocate sparse array");
				  }
				  cblkno++;
			   }
			}
		 }
	  }
   }

   if(regionWindow_)
	  delete regionWindow_;
   regionWindow_ = regionWindow;

   return true;
}
bool TileComponent::canCreateWindow(grk_rect32 windowBounds)
{
   auto maxResolution = resolutions_ + numresolutions - 1;
   if(!maxResolution->intersection(windowBounds).valid())
   {
	  Logger::logger_.error(
		  "Decompress region (%u,%u,%u,%u) must overlap image bounds (%u,%u,%u,%u)",
		  windowBounds.x0, windowBounds.y0, windowBounds.x1, windowBounds.y1, maxResolution->x0,
		  maxResolution->y0, maxResolution->x1, maxResolution->y1);
	  return false;
   }

   return true;
}
void TileComponent::createWindow(grk_rect32 unreducedImageCompWindow)
{
   dealloc();
   window_ = new TileComponentWindow<int32_t>(
	   isCompressor_, tccp_->qmfbid == 1, wholeTileDecompress, resolutions_ + numresolutions - 1,
	   this, unreducedImageCompWindow, numresolutions,
	   isCompressor_ ? numresolutions : numResolutionsToDecompress);
}
TileComponentWindow<int32_t>* TileComponent::getWindow() const
{
   return window_;
}
bool TileComponent::isWholeTileDecoding()
{
   return wholeTileDecompress;
}
ISparseCanvas* TileComponent::getRegionWindow()
{
   return regionWindow_;
}
void TileComponent::postProcess(int32_t* srcData, DecompressBlockExec* block)
{
   if(block->roishift)
   {
	  if(block->qmfbid == 1)
		 postDecompressImpl<RoiShiftFilter<int32_t>>(srcData, block,
													 (uint16_t)block->cblk->width());
	  else
		 postDecompressImpl<RoiScaleFilter<int32_t>>(srcData, block,
													 (uint16_t)block->cblk->width());
   }
   else
   {
	  if(block->qmfbid == 1)
		 postDecompressImpl<ShiftFilter<int32_t>>(srcData, block, (uint16_t)block->cblk->width());
	  else
		 postDecompressImpl<ScaleFilter<int32_t>>(srcData, block, (uint16_t)block->cblk->width());
   }
}
void TileComponent::postProcessHT(int32_t* srcData, DecompressBlockExec* block, uint16_t stride)
{
   if(block->roishift)
   {
	  if(block->qmfbid == 1)
		 postDecompressImpl<ojph::RoiShiftOJPHFilter<int32_t>>(srcData, block, stride);
	  else
		 postDecompressImpl<ojph::RoiScaleOJPHFilter<int32_t>>(srcData, block, stride);
   }
   else
   {
	  if(block->qmfbid == 1)
		 postDecompressImpl<ojph::ShiftOJPHFilter<int32_t>>(srcData, block, stride);
	  else
		 postDecompressImpl<ojph::ScaleOJPHFilter<int32_t>>(srcData, block, stride);
   }
}
template<typename F>
void TileComponent::postDecompressImpl(int32_t* srcData, DecompressBlockExec* block,
									   uint16_t stride)
{
   auto cblk = block->cblk;
   bool empty = cblk->seg_buffers.empty();

   window_->toRelativeCoordinates(block->resno, block->bandOrientation, block->x, block->y);
   auto src =
	   grk_buf2d<int32_t, AllocatorAligned>(srcData, false, cblk->width(), stride, cblk->height());
   auto blockBounds =
	   grk_rect32(block->x, block->y, block->x + cblk->width(), block->y + cblk->height());
   if(!empty)
   {
	  if(regionWindow_)
	  {
		 src.copyFrom<F>(src, F(block));
	  }
	  else
	  {
		 src.setRect(blockBounds);
		 window_->postProcess<F>(src, block->resno, block->bandOrientation, block);
	  }
   }
   if(regionWindow_)
	  regionWindow_->write(block->resno, blockBounds, empty ? nullptr : srcData, 1,
						   blockBounds.width());
}

} // namespace grk
