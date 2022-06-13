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

namespace grk
{

enum eSplitOrientation
{
	SPLIT_L,
	SPLIT_H,
	SPLIT_NUM_ORIENTATIONS
};

template<typename T>
struct TileComponentWindow;

/**
 * ResWindow
 *
 * Manage all buffers for a single DWT resolution. This class
 * stores a buffer for the resolution (in REL coordinates),
 * and also buffers for the 4 sub-bands generated by the DWT transform
 * (in Canvas coordinates).
 *
 * Note: if highest resolution window is set, then only this window allocates
 * memory, and all other ResWindow buffers attach themselves to the highest resolution buffer
 *
 */
template<typename T>
struct ResWindow
{
	friend struct TileComponentWindow<T>;
	typedef grk_buf2d<T, AllocatorAligned> Buf2dAligned;

  private:
	ResWindow(uint8_t numresolutions, uint8_t resno, Buf2dAligned* resWindowHighestResREL,
			  Resolution* tileCompAtRes, Resolution* tileCompAtLowerRes, grk_rect32 resWindow,
			  grk_rect32 tileCompWindowUnreduced, grk_rect32 tileCompUnreduced,
			  uint32_t FILTER_WIDTH)
		: allocated_(false), filterWidth_(FILTER_WIDTH), tileCompAtRes_(tileCompAtRes),
		  tileCompAtLowerRes_(tileCompAtLowerRes),
		  resWindowBufferHighestResREL_(resWindowHighestResREL),
		  resWindowBufferREL_(new Buf2dAligned(resWindow.width(), resWindow.height())),
		  resWindowBufferSplitREL_{nullptr, nullptr},
		  resWindowBuffer_(new Buf2dAligned(resWindow)), resWindowBufferSplit_{nullptr, nullptr}
	{
		auto resWindowPadded = resWindow.grow_IN_PLACE(2 * FILTER_WIDTH);
		resWindowBoundsPadded_ = resWindowPadded.intersection(tileCompAtRes_);
		resWindowBoundsPadded_.setOrigin(tileCompAtRes);
		resWindowBuffer_->setOrigin(tileCompAtRes_);

		uint32_t numDecomps =
			(resno == 0) ? (uint32_t)(numresolutions - 1U) : (uint32_t)(numresolutions - resno);
		for(uint8_t orient = 0; orient < ((resno) > 0 ? BAND_NUM_ORIENTATIONS : 1); orient++)
		{
			// todo: should only need padding equal to FILTER_WIDTH, not 2*FILTER_WIDTH
			auto bandWindow = getPaddedBandWindow(numDecomps, orient, tileCompWindowUnreduced,
												  tileCompUnreduced, 2 * FILTER_WIDTH);
			grk_rect32 band = tileCompAtRes_->tileBand[BAND_ORIENT_LL];
			if(resno > 0)
				band = orient == BAND_ORIENT_LL ? *((grk_rect32*)tileCompAtLowerRes_)
												: tileCompAtRes_->tileBand[orient - 1];
			bandWindow.setOrigin(band);
			assert(bandWindow.intersection(band).setOrigin(bandWindow) == bandWindow);
			bandWindowsBoundsPadded_.push_back(bandWindow);
		}

		// windowed decompression
		if(FILTER_WIDTH)
		{
			if(tileCompAtLowerRes_)
			{
				assert(resno > 0);
				for(uint8_t orient = 0; orient < BAND_NUM_ORIENTATIONS; orient++)
				{
					auto bandWindow = bandWindowsBoundsPadded_[orient];
					bandWindowsBuffersPadded_.push_back(new Buf2dAligned(bandWindow));
					bandWindowsBuffersPaddedREL_.push_back(
						new Buf2dAligned(bandWindow.toRelative()));
				}
				padResWindowBufferBounds(resWindowBuffer_, bandWindowsBuffersPadded_,
										 tileCompAtRes_, true);
				genSplitWindowBuffers(resWindowBufferSplit_, resWindowBuffer_,
									  bandWindowsBuffersPadded_);

				resWindowBuffer_->toRelative();
				resWindowBufferREL_->set(resWindowBuffer_);
				resWindowBuffer_->toAbsolute();

				genSplitWindowBuffers(resWindowBufferSplitREL_, resWindowBuffer_,
									  bandWindowsBuffersPaddedREL_);
			}
		}
		else
		{
			assert(tileCompAtRes_->numTileBandWindows == 3 || !tileCompAtLowerRes);

			// dummy LL band window
			bandWindowsBuffersPadded_.push_back(new Buf2dAligned(0, 0));
			bandWindowsBuffersPaddedREL_.push_back(new Buf2dAligned(0, 0));
			if(tileCompAtLowerRes_)
			{
				for(uint32_t i = 0; i < tileCompAtRes_->numTileBandWindows; ++i)
				{
					auto tileCompBand = tileCompAtRes_->tileBand + i;

					auto band = grk_rect32(tileCompBand);
					bandWindowsBuffersPadded_.push_back(new Buf2dAligned(band));
					bandWindowsBuffersPaddedREL_.push_back(new Buf2dAligned(band.toRelative()));
				}
				for(uint8_t i = 0; i < SPLIT_NUM_ORIENTATIONS; i++)
				{
					auto split = resWindowPadded;
					split.y0 = resWindowPadded.y0 == 0
								   ? 0
								   : ceildivpow2<uint32_t>(resWindowPadded.y0 - i, 1);
					split.y1 = resWindowPadded.y1 == 0
								   ? 0
								   : ceildivpow2<uint32_t>(resWindowPadded.y1 - i, 1);
					split.setOrigin(tileCompAtLowerRes_->x0, tileCompAtRes_->y0);
					resWindowBufferSplit_[i] = new Buf2dAligned(split);
					resWindowBufferSplitREL_[i] = new Buf2dAligned(resWindowBufferSplit_[i]);
					resWindowBufferSplitREL_[i]->toRelative();
				}
			}
		}
	}
	~ResWindow()
	{
		delete resWindowBufferREL_;
		for(auto& b : bandWindowsBuffersPaddedREL_)
			delete b;
		for(uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
			delete resWindowBufferSplitREL_[i];

		delete resWindowBuffer_;
		for(auto& b : bandWindowsBuffersPadded_)
			delete b;
		for(uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
			delete resWindowBufferSplit_[i];
	}
	void padResWindowBufferBounds(Buf2dAligned* resWindowBuffer,
								  std::vector<Buf2dAligned*>& bandWindowsBuffersPadded,
								  grk_rect32 resBounds, bool absolute)
	{
		auto winLow = bandWindowsBuffersPadded[BAND_ORIENT_LL];
		auto winHigh = bandWindowsBuffersPadded[BAND_ORIENT_HL];
		resWindowBuffer->x0 = (std::min<uint32_t>)(2 * winLow->x0, 2 * winHigh->x0 + 1);
		resWindowBuffer->x1 = (std::max<uint32_t>)(2 * winLow->x1, 2 * winHigh->x1 + 1);
		winLow = bandWindowsBuffersPadded[BAND_ORIENT_LL];
		winHigh = bandWindowsBuffersPadded[BAND_ORIENT_LH];
		resWindowBuffer->y0 = (std::min<uint32_t>)(2 * winLow->y0, 2 * winHigh->y0 + 1);
		resWindowBuffer->y1 = (std::max<uint32_t>)(2 * winLow->y1, 2 * winHigh->y1 + 1);

		// todo: shouldn't need to clip
		resWindowBuffer->clip_IN_PLACE(resBounds);
		resWindowBuffer->setOrigin(resBounds, absolute);
		assert(resWindowBuffer->x0 >= resBounds.origin_x0);
		assert(resWindowBuffer->y0 >= resBounds.origin_y0);
	}

	void genSplitWindowBuffers(Buf2dAligned** resWindowBufferSplit, Buf2dAligned* resWindowBuffer,
							   std::vector<Buf2dAligned*>& bandWindowsBuffersPadded)
	{
		// two windows formed by horizontal pass and used as input for vertical pass
		grk_rect32 splitResWindowBounds[SPLIT_NUM_ORIENTATIONS];
		splitResWindowBounds[SPLIT_L] =
			grk_rect32(resWindowBuffer->x0, bandWindowsBuffersPadded[BAND_ORIENT_LL]->y0,
					   resWindowBuffer->x1, bandWindowsBuffersPadded[BAND_ORIENT_LL]->y1);

		resWindowBufferSplit[SPLIT_L] = new Buf2dAligned(splitResWindowBounds[SPLIT_L]);

		splitResWindowBounds[SPLIT_H] = grk_rect32(
			resWindowBuffer->x0,
			bandWindowsBuffersPadded[BAND_ORIENT_LH]->y0 + tileCompAtLowerRes_->height(),
			resWindowBuffer->x1,
			bandWindowsBuffersPadded[BAND_ORIENT_LH]->y1 + tileCompAtLowerRes_->height());

		resWindowBufferSplit[SPLIT_H] = new Buf2dAligned(splitResWindowBounds[SPLIT_H]);
	}

	/**
	 * Get band window (in tile component coordinates) for specified number
	 * of decompositions
	 *
	 * Note: if numDecomps is zero, then the band window (and there is only one)
	 * is equal to the unreduced tile component window
	 *
	 * See table F-1 in JPEG 2000 standard
	 *
	 */
	static grk_rect32 getBandWindow(uint32_t numDecomps, uint8_t orientation,
									grk_rect32 tileCompWindowUnreduced)
	{
		assert(orientation < BAND_NUM_ORIENTATIONS);
		if(numDecomps == 0)
			return tileCompWindowUnreduced;

		/* project window onto sub-band generated by `numDecomps` decompositions */
		/* See equation B-15 of the standard. */
		uint32_t bx0 = orientation & 1;
		uint32_t by0 = (uint32_t)(orientation >> 1U);

		uint32_t bx0Offset = (1U << (numDecomps - 1)) * bx0;
		uint32_t by0Offset = (1U << (numDecomps - 1)) * by0;

		uint32_t tc_originx0 = tileCompWindowUnreduced.origin_x0;
		uint32_t tc_originy0 = tileCompWindowUnreduced.origin_y0;
		uint32_t tcx0 = tileCompWindowUnreduced.x0;
		uint32_t tcy0 = tileCompWindowUnreduced.y0;
		uint32_t tcx1 = tileCompWindowUnreduced.x1;
		uint32_t tcy1 = tileCompWindowUnreduced.y1;

		return grk_rect32(
			(tc_originx0 <= bx0Offset) ? 0
									   : ceildivpow2<uint32_t>(tc_originx0 - bx0Offset, numDecomps),
			(tc_originy0 <= by0Offset) ? 0
									   : ceildivpow2<uint32_t>(tc_originy0 - by0Offset, numDecomps),
			(tcx0 <= bx0Offset) ? 0 : ceildivpow2<uint32_t>(tcx0 - bx0Offset, numDecomps),
			(tcy0 <= by0Offset) ? 0 : ceildivpow2<uint32_t>(tcy0 - by0Offset, numDecomps),
			(tcx1 <= bx0Offset) ? 0 : ceildivpow2<uint32_t>(tcx1 - bx0Offset, numDecomps),
			(tcy1 <= by0Offset) ? 0 : ceildivpow2<uint32_t>(tcy1 - by0Offset, numDecomps));
	}

	bool alloc(bool clear)
	{
		if(allocated_)
			return true;

		// if top level window is present, then all buffers attach to this window
		if(resWindowBufferHighestResREL_)
		{
			// ensure that top level window is allocated
			if(!resWindowBufferHighestResREL_->alloc2d(clear))
				return false;

			// don't allocate bandWindows for windowed decompression
			if(filterWidth_)
				return true;

			// attach to top level window
			if(resWindowBufferREL_ != resWindowBufferHighestResREL_)
				resWindowBufferREL_->attach(resWindowBufferHighestResREL_->getBuffer(),
											resWindowBufferHighestResREL_->stride);

			// tileCompResLower_ is null for lowest resolution
			if(tileCompAtLowerRes_)
			{
				for(uint8_t orientation = 0; orientation < bandWindowsBuffersPaddedREL_.size();
					++orientation)
				{
					switch(orientation)
					{
						case BAND_ORIENT_HL:
							bandWindowsBuffersPaddedREL_[orientation]->attach(
								resWindowBufferHighestResREL_->getBuffer() +
									tileCompAtLowerRes_->width(),
								resWindowBufferHighestResREL_->stride);
							break;
						case BAND_ORIENT_LH:
							bandWindowsBuffersPaddedREL_[orientation]->attach(
								resWindowBufferHighestResREL_->getBuffer() +
									tileCompAtLowerRes_->height() *
										resWindowBufferHighestResREL_->stride,
								resWindowBufferHighestResREL_->stride);
							break;
						case BAND_ORIENT_HH:
							bandWindowsBuffersPaddedREL_[orientation]->attach(
								resWindowBufferHighestResREL_->getBuffer() +
									tileCompAtLowerRes_->width() +
									tileCompAtLowerRes_->height() *
										resWindowBufferHighestResREL_->stride,
								resWindowBufferHighestResREL_->stride);
							break;
						default:
							break;
					}
				}
				resWindowBufferSplit_[SPLIT_L]->attach(resWindowBufferHighestResREL_->getBuffer(),
													   resWindowBufferHighestResREL_->stride);
				resWindowBufferSplit_[SPLIT_H]->attach(
					resWindowBufferHighestResREL_->getBuffer() +
						tileCompAtLowerRes_->height() * resWindowBufferHighestResREL_->stride,
					resWindowBufferHighestResREL_->stride);
			}
		}
		else
		{
			// resolution window is always allocated
			if(!resWindowBufferREL_->alloc2d(clear))
				return false;

			// band windows are allocated if present
			for(auto& b : bandWindowsBuffersPaddedREL_)
			{
				if(!b->alloc2d(clear))
					return false;
			}
			if(tileCompAtLowerRes_)
			{
				resWindowBufferSplit_[SPLIT_L]->attach(resWindowBufferREL_->getBuffer(),
													   resWindowBufferREL_->stride);
				resWindowBufferSplit_[SPLIT_H]->attach(resWindowBufferREL_->getBuffer() +
														   tileCompAtLowerRes_->height() *
															   resWindowBufferREL_->stride,
													   resWindowBufferREL_->stride);
			}
		}

		// attach canvas windows to relative windows
		for(uint8_t orientation = 0; orientation < bandWindowsBuffersPaddedREL_.size();
			++orientation)
			bandWindowsBuffersPadded_[orientation]->attach(
				bandWindowsBuffersPaddedREL_[orientation]);
		resWindowBuffer_->attach(resWindowBufferREL_);
		for(uint8_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
		{
			if(resWindowBufferSplitREL_[i])
				resWindowBufferSplitREL_[i]->attach(resWindowBufferSplit_[i]);
		}

		allocated_ = true;

		return true;
	}
	/**
	 * Get band window (in tile component coordinates) for specified number
	 * of decompositions (with padding)
	 *
	 * Note: if numDecomps is zero, then the band window (and there is only one)
	 * is equal to the unreduced tile component window (with padding)
	 */
	static grk_rect32 getPaddedBandWindow(uint32_t numDecomps, uint8_t orientation,
										  grk_rect32 unreducedTileCompWindow,
										  grk_rect32 unreducedTileComp, uint32_t padding)
	{
		assert(orientation < BAND_NUM_ORIENTATIONS);
		if(numDecomps == 0)
		{
			assert(orientation == 0);
			return unreducedTileCompWindow.grow_IN_PLACE(padding).intersection(&unreducedTileComp);
		}
		auto oneLessDecompWindow = unreducedTileCompWindow;
		auto oneLessDecompTile = unreducedTileComp;
		if(numDecomps > 1)
		{
			oneLessDecompWindow = getBandWindow(numDecomps - 1, 0, unreducedTileCompWindow);
			oneLessDecompTile = getBandWindow(numDecomps - 1, 0, unreducedTileComp);
		}

		return getBandWindow(
			1, orientation,
			oneLessDecompWindow.grow_IN_PLACE(2 * padding).intersection(&oneLessDecompTile));
	}

	grk_buf2d_simple<int32_t> getResWindowBufferSimple(void) const
	{
		return resWindowBuffer_->simple();
	}
	grk_buf2d_simple<float> getResWindowBufferSimpleF(void) const
	{
		return resWindowBuffer_->simpleF();
	}
	grk_rect32* getResWindowBoundsPadded(void)
	{
		return &resWindowBoundsPadded_;
	}
	void disableBandWindowAllocation(void)
	{
		resWindowBufferHighestResREL_ = resWindowBufferREL_;
	}
	Buf2dAligned* getResWindowBufferSplitREL(eSplitOrientation orientation) const
	{
		return resWindowBufferSplitREL_[orientation];
	}
	const grk_rect32* getBandWindowPadded(eBandOrientation orientation) const
	{
		return &bandWindowsBoundsPadded_[orientation];
	}
	const Buf2dAligned* getBandWindowBufferPaddedREL(eBandOrientation orientation) const
	{
		return bandWindowsBuffersPaddedREL_[orientation];
	}
	const grk_buf2d_simple<int32_t>
		getBandWindowBufferPaddedSimple(eBandOrientation orientation) const
	{
		return bandWindowsBuffersPadded_[orientation]->simple();
	}
	const grk_buf2d_simple<float>
		getBandWindowBufferPaddedSimpleF(eBandOrientation orientation) const
	{
		return bandWindowsBuffersPadded_[orientation]->simpleF();
	}
	Buf2dAligned* getResWindowBufferREL(void) const
	{
		return resWindowBufferREL_;
	}
	bool allocated_;
	uint32_t filterWidth_;

	Resolution* tileCompAtRes_; // non-null will trigger creation of band window buffers
	Resolution* tileCompAtLowerRes_; // null for lowest resolution
	grk_rect32 resWindowBoundsPadded_;

	Buf2dAligned* resWindowBufferHighestResREL_;
	Buf2dAligned* resWindowBufferREL_;
	Buf2dAligned* resWindowBufferSplitREL_[SPLIT_NUM_ORIENTATIONS];
	std::vector<Buf2dAligned*> bandWindowsBuffersPaddedREL_;

	Buf2dAligned* resWindowBuffer_;
	Buf2dAligned* resWindowBufferSplit_[SPLIT_NUM_ORIENTATIONS];
	std::vector<Buf2dAligned*> bandWindowsBuffersPadded_;

	/*
	bandWindowsBoundsPadded_ is used for determining which precincts and code blocks overlap
	the window of interest, in each respective resolution
	*/
	std::vector<grk_rect32> bandWindowsBoundsPadded_;
};

} // namespace grk
