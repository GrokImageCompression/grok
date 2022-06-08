/**
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
 *
 */
#pragma once

#include "grk_includes.h"
#include <stdexcept>
#include <algorithm>

/*
 Various coordinate systems are used to describe regions in the tile component buffer.

 1) Canvas coordinates:  JPEG 2000 global image coordinates.

 2) Tile component coordinates: canvas coordinates with sub-sampling applied

 3) Band coordinates: coordinates relative to a specified sub-band's origin

 4) Buffer coordinates: coordinate system where all resolutions are translated
	to common origin (0,0). If each code block is translated relative to the origin of the
 resolution that **it belongs to**, the blocks are then all in buffer coordinate system

 Note: the name of any method or variable returning non canvas coordinates is appended
 with "REL", to signify relative coordinates.

 */

#include "ResWindow.h"

namespace grk
{
template<typename T>
struct TileComponentWindow
{
	TileComponentWindow(bool isCompressor, bool lossless, bool wholeTileDecompress,
							  grk_rect32 tileCompUnreduced, grk_rect32 tileCompReduced,
							  grk_rect32 unreducedTileCompOrImageCompWindow,
							  Resolution* resolutions_, uint8_t numresolutions,
							  uint8_t reducedNumResolutions)
		: unreducedBounds_(tileCompUnreduced), bounds_(tileCompReduced),
		  numResolutions_(numresolutions), compress_(isCompressor),
		  wholeTileDecompress_(wholeTileDecompress)
	{
		if(!compress_)
		{
			// for decompress, we are passed the unreduced image component window
			auto unreducedImageCompWindow = unreducedTileCompOrImageCompWindow;
			bounds_ = unreducedImageCompWindow.scaleDownCeilPow2(
				(uint32_t)(numResolutions_ - reducedNumResolutions));
			bounds_ = bounds_.intersection(tileCompReduced);
			assert(bounds_.valid());
			unreducedBounds_ = unreducedImageCompWindow.intersection(tileCompUnreduced);
			assert(unreducedBounds_.valid());
		}
		// fill resolutions vector
		assert(reducedNumResolutions > 0);
		for(uint32_t resno = 0; resno < reducedNumResolutions; ++resno)
			resolution_.push_back(resolutions_ + resno);

		auto tileCompAtRes = resolutions_ + reducedNumResolutions - 1;
		auto tileCompAtLowerRes =
			reducedNumResolutions > 1 ? resolutions_ + reducedNumResolutions - 2 : nullptr;
		// create resolution buffers
		auto topLevel = new ResWindow<T>(
			numresolutions, (uint8_t)(reducedNumResolutions - 1U), nullptr, tileCompAtRes,
			tileCompAtLowerRes, bounds_, unreducedBounds_, tileCompUnreduced,
			wholeTileDecompress ? 0 : getFilterPad<uint32_t>(lossless));
		// setting top level prevents allocation of tileCompBandWindows buffers
		if(!useBandWindows())
			topLevel->disableBandWindowAllocation();

		for(uint8_t resno = 0; resno < reducedNumResolutions - 1; ++resno)
		{
			// resolution window ==  next resolution band window at orientation 0
			auto resWindow = ResWindow<T>::getBandWindow(
				(uint32_t)(numresolutions - 1 - resno), 0, unreducedBounds_);
			resWindows.push_back(new ResWindow<T>(
				numresolutions, resno, useBandWindows() ? nullptr : topLevel->resWindowBufferREL_,
				resolutions_ + resno, resno > 0 ? resolutions_ + resno - 1 : nullptr,
				resWindow, unreducedBounds_, tileCompUnreduced,
				wholeTileDecompress ? 0 : getFilterPad<uint32_t>(lossless)));
		}
		resWindows.push_back(topLevel);
	}
	~TileComponentWindow()
	{
		for(auto& b : resWindows)
			delete b;
	}

	/**
	 * Transform code block offsets from canvas coordinates
	 * to either band coordinates (relative to sub band origin),
	 * in the case of whole tile decompression,
	 *
	 * or buffer coordinates (relative to associated resolution origin),
	 * in the case of compression or region decompression
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {LL,HL,LH,HH}
	 * @param offsetx x offset of code block in canvas coordinates
	 * @param offsety y offset of code block in canvas coordinates
	 *
	 */
	void toRelativeCoordinates(uint8_t resno, eBandOrientation orientation, uint32_t& offsetx,
							   uint32_t& offsety) const
	{
		assert(resno < resolution_.size());

		auto res = resolution_[resno];
		auto band = res->tileBand + getBandIndex(resno, orientation);

		uint32_t x = offsetx;
		uint32_t y = offsety;

		// get offset relative to band
		x -= band->x0;
		y -= band->y0;

		if(useBufferCoordinatesForCodeblock() && resno > 0)
		{
			auto resLower = resolution_[resno - 1U];

			if(orientation & 1)
				x += resLower->width();
			if(orientation & 2)
				y += resLower->height();
		}
		offsetx = x;
		offsety = y;
	}
	/**
	 * Get code block destination window
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {LL,HL,LH,HH}
	 *
	 */
	const grk_buf2d<T, AllocatorAligned>*
		getCodeBlockDestWindowREL(uint8_t resno, eBandOrientation orientation) const
	{
		return (useBufferCoordinatesForCodeblock())
				   ? getResWindowBufferHighestREL()
				   : getBandWindowBufferPaddedREL(resno, orientation);
	}
	/**
	 * Get padded band window buffer
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {0,1,2,3} for {LL,HL,LH,HH} band windows
	 *
	 * If resno is > 0, return LL,HL,LH or HH band window, otherwise return LL resolution window
	 *
	 */
	const grk_buf2d<T, AllocatorAligned>*
		getBandWindowBufferPaddedREL(uint8_t resno, eBandOrientation orientation) const
	{
		assert(resno < resolution_.size());
		assert(resno > 0 || orientation == BAND_ORIENT_LL);

		if(resno == 0 && (compress_ || wholeTileDecompress_))
			return resWindows[0]->getResWindowBufferREL();

		return resWindows[resno]->bandWindowsBuffersPaddedREL_[orientation];
	}

	/**
	 * Get padded band window
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {0,1,2,3} for {LL,HL,LH,HH} band windows
	 *
	 */
	const grk_rect32* getBandWindowPadded(uint8_t resno, eBandOrientation orientation) const
	{
		if(resWindows[resno]->bandWindowsBoundsPadded_.empty())
			return nullptr;
		return &resWindows[resno]->bandWindowsBoundsPadded_[orientation];
	}

	const grk_rect32* getResWindowPadded(uint8_t resno) const
	{
		return resWindows[resno]->getResWindowBoundsPadded();
	}
	/*
	 * Get intermediate split window
	 *
	 * @param orientation 0 for upper split window, and 1 for lower split window
	 */
	const grk_buf2d<T, AllocatorAligned>*
		getResWindowBufferSplitREL(uint8_t resno, eSplitOrientation orientation) const
	{
		assert(resno > 0 && resno < resolution_.size());

		return resWindows[resno]->resWindowBufferSplitREL_[orientation];
	}
	/**
	 * Get resolution window
	 *
	 * @param resno resolution number
	 *
	 */
	const grk_buf2d<T, AllocatorAligned>* getResWindowBufferREL(uint32_t resno) const
	{
		return resWindows[resno]->resWindowBufferREL_;
	}
	/**
	 * Get highest resolution window
	 *
	 *
	 */
	grk_buf2d<T, AllocatorAligned>* getResWindowBufferHighestREL(void) const
	{
		return resWindows.back()->getResWindowBufferREL();
	}
	bool alloc()
	{
		for(auto& b : resWindows)
		{
			if(!b->alloc(!compress_))
				return false;
		}

		return true;
	}
	/**
	 * Get bounds of tile component (canvas coordinates)
	 * decompress: reduced canvas coordinates of window
	 * compress: unreduced canvas coordinates of entire tile
	 */
	grk_rect32 bounds() const
	{
		return bounds_;
	}
	grk_rect32 unreducedBounds() const
	{
		return unreducedBounds_;
	}
	uint64_t stridedArea(void) const
	{
		return getResWindowBufferHighestREL()->stride * getResWindowBufferHighestREL()->height();
	}

	// set data to buf without owning it
	void attach(T* buffer, uint32_t stride)
	{
		getResWindowBufferHighestREL()->attach(buffer, stride);
	}
	// transfer data to buf, and cease owning it
	void transfer(T** buffer, uint32_t* stride)
	{
		getResWindowBufferHighestREL()->transfer(buffer, stride);
	}

  private:
	bool useBandWindows() const
	{
		return !wholeTileDecompress_;
	}
	bool useBufferCoordinatesForCodeblock() const
	{
		return compress_ || !wholeTileDecompress_;
	}
	uint8_t getBandIndex(uint8_t resno, eBandOrientation orientation) const
	{
		uint8_t index = 0;
		if(resno > 0)
		{
			index = (uint8_t)orientation;
			index--;
		}
		return index;
	}
	/******************************************************/
	// decompress: unreduced/reduced image component window
	// compress:  unreduced/reduced tile component
	grk_rect32 unreducedBounds_;
	grk_rect32 bounds_;
	/******************************************************/

	std::vector<Resolution*> resolution_;
	// windowed bounds for windowed decompress, otherwise full bounds
	std::vector<ResWindow<T>*> resWindows;

	// unreduced number of resolutions
	uint8_t numResolutions_;

	bool compress_;
	bool wholeTileDecompress_;
};

} // namespace grk
