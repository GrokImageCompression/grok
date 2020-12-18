/**
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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

namespace grk {

/**
 *  Class to manage multiple buffers needed to perform DWT transform
 *
 *
 */
template<typename T> struct res_window {

	res_window(uint8_t numresolutions,
				uint8_t resno,
				grk_buffer_2d<T> *top,
				Resolution *full_res,
				Resolution *lower_full_res,
				grk_rect_u32 bounds,
				grk_rect_u32 unreduced_bounds,
				uint32_t HORIZ_PASS_HEIGHT,
				uint32_t FILTER_WIDTH) : allocated(false),
										fullRes(full_res),
										fullResLower(lower_full_res),
										resWindow(new grk_buffer_2d<T>(bounds)),
										resWindowTopLevel(top)
	{
		for (uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
			splitWindow[i] = nullptr;
		if (full_res) {

		  if (FILTER_WIDTH) {

			if (fullResLower) {
	        // 1. set up windows for horizontal and vertical passes
	        grk_rect_u32 bandWindowRect[BAND_NUM_ORIENTATIONS];
	        bandWindowRect[BAND_ORIENT_LL] = grk_band_window(numresolutions,resno,BAND_ORIENT_LL,unreduced_bounds);
	        bandWindowRect[BAND_ORIENT_LL] = bandWindowRect[BAND_ORIENT_LL].pan(-(int64_t)fullRes->band[BAND_INDEX_LH].x0, -(int64_t)fullRes->band[BAND_INDEX_HL].y0);
	        bandWindowRect[BAND_ORIENT_LL].grow(FILTER_WIDTH, fullResLower->width(),  fullResLower->height());
	        auto t = new grk_buffer_2d<T>();
	        t->copy_rect(bandWindowRect[BAND_ORIENT_LL]);
	        bandWindow.push_back(t);

	        bandWindowRect[BAND_ORIENT_HL] = grk_band_window(numresolutions,resno,BAND_ORIENT_HL,unreduced_bounds);
	        bandWindowRect[BAND_ORIENT_HL] = bandWindowRect[BAND_ORIENT_HL].pan(-(int64_t)fullRes->band[BAND_INDEX_HL].x0, -(int64_t)fullRes->band[BAND_INDEX_HL].y0);
	        bandWindowRect[BAND_ORIENT_HL].grow(FILTER_WIDTH, fullRes->width() - fullResLower->width(),  fullResLower->height());
	        t = new grk_buffer_2d<T>();
	        t->copy_rect(bandWindowRect[BAND_ORIENT_HL]);
	        bandWindow.push_back(t);

	        bandWindowRect[BAND_ORIENT_LH] = grk_band_window(numresolutions,resno,BAND_ORIENT_LH,unreduced_bounds);
	        bandWindowRect[BAND_ORIENT_LH] = bandWindowRect[BAND_ORIENT_LH].pan(-(int64_t)fullRes->band[BAND_INDEX_LH].x0, -(int64_t)fullRes->band[BAND_INDEX_LH].y0);
	        bandWindowRect[BAND_ORIENT_LH].grow(FILTER_WIDTH, fullResLower->width(),  fullRes->height() - fullResLower->height());
	        t = new grk_buffer_2d<T>();
	        t->copy_rect(bandWindowRect[BAND_ORIENT_LH]);
	        bandWindow.push_back(t);

	        bandWindowRect[BAND_ORIENT_HH] = grk_band_window(numresolutions,resno,BAND_ORIENT_HH,unreduced_bounds);
	        bandWindowRect[BAND_ORIENT_HH] = bandWindowRect[BAND_ORIENT_HH].pan(-(int64_t)fullRes->band[BAND_INDEX_HH].x0, -(int64_t)fullRes->band[BAND_INDEX_HH].y0);
	        bandWindowRect[BAND_ORIENT_HH].grow(FILTER_WIDTH, fullRes->width() - fullResLower->width(),  fullRes->height() - fullResLower->height());
	        t = new grk_buffer_2d<T>();
	        t->copy_rect(bandWindowRect[BAND_ORIENT_HH]);
	        bandWindow.push_back(t);


	        grk_rect_u32 resWindow;
	        auto win_low 				= bandWindowRect[BAND_ORIENT_LL];
	        auto win_high 				= bandWindowRect[BAND_ORIENT_HL];
	        resWindow.x0 				= min<uint32_t>(2 * win_low.x0, 2 * bandWindowRect[BAND_ORIENT_HL].x0);
	        resWindow.x1 				= min<uint32_t>(max<uint32_t>(2 * win_low.x1, 2 * win_high.x1), fullRes->width());
	        win_low 					= bandWindowRect[BAND_ORIENT_LL];
	        win_high 					= bandWindowRect[BAND_ORIENT_LH];
	        resWindow.y0 				= min<uint32_t>(2 * win_low.y0, 2 * win_high.y0);
	        resWindow.y1 				= min<uint32_t>(max<uint32_t>(2 * win_low.y1, 2 * win_high.y1), fullRes->height());

	        // two windows formed by horizontal pass and used as input for vertical pass
	        grk_rect_u32 splitWindowRect[SPLIT_NUM_ORIENTATIONS];
	        splitWindowRect[SPLIT_L] = grk_rect_u32(resWindow.x0,
										  sat_sub<uint32_t>(bandWindowRect[BAND_ORIENT_LL].y0, HORIZ_PASS_HEIGHT),
										  resWindow.x1,
										  bandWindowRect[BAND_ORIENT_LL].y1);
	        t = new grk_buffer_2d<T>();
	        t->copy_rect(splitWindowRect[SPLIT_L]);
	        splitWindow[SPLIT_L] = t;

	        splitWindowRect[SPLIT_H] = grk_rect_u32(resWindow.x0,
										// note: max is used to avoid vertical overlap between the two intermediate windows
										max<uint32_t>(bandWindowRect[BAND_ORIENT_LL].y1,
										sat_sub<uint32_t>(min<uint32_t>(bandWindowRect[BAND_ORIENT_LH].y0 + fullResLower->height(), fullRes->height()),HORIZ_PASS_HEIGHT)),
										resWindow.x1,
										min<uint32_t>(bandWindowRect[BAND_ORIENT_LH].y1 + fullResLower->height(), fullRes->height()));
	        t = new grk_buffer_2d<T>();
	        t->copy_rect(splitWindowRect[SPLIT_H]);
	        splitWindow[SPLIT_H] = t;

			}

		   } else {
				// dummy LL band window
				bandWindow.push_back( new grk_buffer_2d<T>( grk_rect_u32(0,0,0,0)) );
				assert(full_res->numBandWindows == 3 || !lower_full_res);
				if (lower_full_res) {
					for (uint32_t i = 0; i < full_res->numBandWindows; ++i)
						bandWindow.push_back( new grk_buffer_2d<T>(full_res->band[i]) );
					for (uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
						splitWindow[i] = new grk_buffer_2d<T>(bounds);
					splitWindow[SPLIT_L]->y1 = bounds.y0 + lower_full_res->height();
					splitWindow[SPLIT_H]->y0 = splitWindow[SPLIT_L]->y1;
				}

			}
		}
	}
	~res_window(){
		delete resWindow;
		for (auto &b : bandWindow)
			delete b;
		for (uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
			delete splitWindow[i];
	}

	bool alloc(bool clear){
		if (allocated)
			return true;


		// if top level window is present, then all buffers attach to this window
		if (resWindowTopLevel) {
			// ensure that top level window is allocated
			if (!resWindowTopLevel->alloc(clear))
				return false;

			// attach to top level window
			if (resWindow != resWindowTopLevel)
				resWindow->attach(resWindowTopLevel->data, resWindowTopLevel->stride);

			// fullResLower is null for lowest resolution
			if (fullResLower) {
				for (uint8_t orientation = 0; orientation < bandWindow.size(); ++orientation){
					switch(orientation){
					case BAND_ORIENT_HL:
						bandWindow[orientation]->attach(resWindowTopLevel->data + fullResLower->width(),
												resWindowTopLevel->stride);
						break;
					case BAND_ORIENT_LH:
						bandWindow[orientation]->attach(resWindowTopLevel->data + fullResLower->height() * resWindowTopLevel->stride,
												resWindowTopLevel->stride);
						break;
					case BAND_ORIENT_HH:
						bandWindow[orientation]->attach(resWindowTopLevel->data + fullResLower->width() +
													fullResLower->height() * resWindowTopLevel->stride,
														resWindowTopLevel->stride);
						break;
					default:
						break;
					}
				}
				splitWindow[SPLIT_L]->attach(resWindowTopLevel->data, resWindowTopLevel->stride);
				splitWindow[SPLIT_H]->attach(resWindowTopLevel->data + fullResLower->height() * resWindowTopLevel->stride,
											resWindowTopLevel->stride);


			}
		} else {
			// resolution window is always allocated
			if (!resWindow->alloc(clear))
				return false;
			// band windows are allocated if present
			for (auto &b : bandWindow){
				if (!b->alloc(clear))
					return false;
			}
			if (fullResLower){
				splitWindow[SPLIT_L]->attach(resWindow->data, resWindow->stride);
				splitWindow[SPLIT_H]->attach(resWindow->data + fullResLower->height() * resWindow->stride,resWindow->stride);
			}
		}
		allocated = true;

		return true;
	}

	bool allocated;
	// fullRes triggers creation of bandWindow buffers
	Resolution *fullRes;
	// fullResLower is null for lowest resolution
	Resolution *fullResLower;
	std::vector< grk_buffer_2d<T>* > bandWindow;
	// intermediate buffers for DWT
	grk_buffer_2d<T> *splitWindow[SPLIT_NUM_ORIENTATIONS];
	grk_buffer_2d<T> *resWindow;
	grk_buffer_2d<T> *resWindowTopLevel;
};


/*
 Note: various coordinate systems are used to describe regions in the tile buffer.

 1) Canvas coordinate system:  JPEG 2000 global image coordinates, independent of sub-sampling

 2) Tile coordinate system:  coordinates relative to a tile's top left hand corner, with
 sub-sampling accounted for

 3) Resolution coordinate system:  coordinates relative to a resolution's top left hand corner

 4) Sub-band coordinate system: coordinates relative to a particular sub-band's top left hand corner

 */

template<typename T> struct TileComponentWindowBuffer {
	TileComponentWindowBuffer(bool isCompressor,
						bool lossless,
						bool whole_tile_decoding,
						grk_rect_u32 unreduced_tile_dim,
						grk_rect_u32 reduced_tile_dim,
						grk_rect_u32 unreduced_window_dim,
						Resolution *tile_comp_resolutions,
						uint8_t numresolutions,
						uint8_t reduced_num_resolutions) :
							m_unreduced_bounds(unreduced_tile_dim),
							m_bounds(reduced_tile_dim),
							num_resolutions(numresolutions),
							m_compress(isCompressor),
							whole_tile_decoding(whole_tile_decoding)
	{
		if (!m_compress) {
			m_bounds = unreduced_window_dim.rectceildivpow2(num_resolutions - reduced_num_resolutions);
			m_bounds = m_bounds.intersection(reduced_tile_dim);
			assert(m_bounds.is_valid());

			m_unreduced_bounds = unreduced_window_dim.intersection(unreduced_tile_dim);
			assert(m_unreduced_bounds.is_valid());
		}

		// fill resolutions vector
        assert(reduced_num_resolutions>0);
        for (uint32_t resno = 0; resno < reduced_num_resolutions; ++resno)
        	resolutions.push_back(tile_comp_resolutions+resno);

        auto fullRes = tile_comp_resolutions+reduced_num_resolutions-1;
        auto fullResLower = reduced_num_resolutions > 1 ?
        						tile_comp_resolutions+reduced_num_resolutions-2 : nullptr;

        // create resolution buffers
		 auto topLevel = new res_window<T>(numresolutions,
				 	 	 	 	 	 	 reduced_num_resolutions-1,
				  	 	 	 	 	 	 nullptr,
				 	 	 	 	 	 	 fullRes,
				 	 	 	 	 	 	 fullResLower,
										 m_bounds,
										 m_unreduced_bounds,
										 whole_tile_decoding ? 0 : getHorizontalPassHeight(lossless),
										 whole_tile_decoding ? 0 : getFilterWidth(lossless));
		 // setting top level blocks allocation of bandWindow buffers
		 if (!use_band_windows())
			 topLevel->resWindowTopLevel = topLevel->resWindow;
		 for (uint8_t resno = 0; resno < reduced_num_resolutions-1; ++resno){
			 // resolution window ==  next resolution band window at orientation 0
			  auto res_dims =  grk_band_window(num_resolutions,
												(uint8_t)(resno+1),
												0,
												unreduced_window_dim);
			 res_windows.push_back(new res_window<T>(numresolutions,
					 	 	 	 	 	 	 	 resno,
					  	 	 	 	 	 	 	 use_band_windows() ? nullptr : topLevel->resWindow,
												  tile_comp_resolutions+resno,
												  resno > 0 ? tile_comp_resolutions+resno-1 : nullptr,
												  res_dims,
												  m_unreduced_bounds,
												  whole_tile_decoding ? 0 : getHorizontalPassHeight(lossless),
												  whole_tile_decoding ? 0 : getFilterWidth(lossless)) );
		 }
		 res_windows.push_back(topLevel);
	}
	~TileComponentWindowBuffer(){
		for (auto& b : res_windows)
			delete b;
	}



    /* Note: those values for filter_margin are in part the result of */
    /* experimentation. The value 2 for QMFBID=1 (5x3 filter) can be linked */
    /* to the maximum left/right extension given in tables F.2 and F.3 of the */
    /* standard. The value 3 for QMFBID=0 (9x7 filter) is more suspicious, */
    /* since F.2 and F.3 would lead to 4 instead, so the current 3 might be */
    /* needed to be bumped to 4, in case inconsistencies are found while */
    /* decoding parts of irreversible coded images. */
    /* See dwt_decode_partial_53 and dwt_decode_partial_97 as well */
	uint32_t getFilterWidth(bool lossless) const {
		return  lossless ? 2 : 4;
	}

	/**
	 * Tranform code block offsets
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {LL,HL,LH,HH}
	 * @param offsetx x offset of code block
	 * @param offsety y offset of code block
	 *
	 */
	void transform(uint8_t resno,eBandOrientation orientation, uint32_t &offsetx, uint32_t &offsety) const {
		assert(resno < resolutions.size());

		auto res = resolutions[resno];
		auto band = res->band + getBandIndex(resno,orientation);

		uint32_t x = offsetx;
		uint32_t y = offsety;

		// get offset relative to band
		x -= band->x0;
		y -= band->y0;

		if (global_code_block_offset() && resno > 0){
			auto resLower = resolutions[ resno - 1];

			if (orientation & 1)
				x += resLower->width();
			if (orientation & 2)
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
	const grk_buffer_2d<T>* getCodeBlockDestWindow(uint8_t resno,eBandOrientation orientation) const {
		return (global_code_block_offset()) ? tile_buf() : band_window(resno,orientation);
	}

	/**
	 * Get non-LL band window
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {0,1,2,3} for {LL,HL,LH,HH} band windows
	 *
	 */
	const grk_buffer_2d<T>*  getWindow(uint8_t resno,eBandOrientation orientation) const{
		return band_window(resno,orientation);
	}

	/*
	 * Get intermediate split window.
	 *
	 * @param orientation 0 for upper split window, and 1 for lower split window
	 */
	const grk_buffer_2d<T>*  getSplitWindow(uint8_t resno,eSplitOrientation orientation) const{
		assert(resno > 0 && resno < resolutions.size());

		return res_windows[resno]->splitWindow[orientation];
	}

	/**
	 * Get resolution window
	 *
	 * @param resno resolution number
	 *
	 */
	const grk_buffer_2d<T>*  getWindow(uint32_t resno) const{
		return res_windows[resno]->resWindow;
	}

	/**
	 * Get tile window
	 *
	 *
	 */
	const grk_buffer_2d<T>*  getWindow(void) const{
		return tile_buf();
	}

	bool alloc(){
		for (auto& b : res_windows) {
			if (!b->alloc(!m_compress))
				return false;
		}

		return true;
	}

	/**
	 * Get bounds of tile component
	 * decompress: reduced tile component coordinates of window
	 * compress: unreduced tile component coordinates of entire tile
	 */
	grk_rect_u32 bounds() const{
		return m_bounds;
	}

	grk_rect_u32 unreduced_bounds() const{
		return m_unreduced_bounds;
	}

	uint64_t strided_area(void){
		return tile_buf()->stride * m_bounds.height();
	}

	// set data to buf without owning it
	void attach(T* buffer,uint32_t stride){
		tile_buf()->attach(buffer,stride);
	}
	// transfer data to buf, and cease owning it
	void transfer(T** buffer, bool* owns, uint32_t *stride){
		tile_buf()->transfer(buffer,owns,stride);
	}

private:

	bool use_band_windows() const{
		//return !m_compress && whole_tile_decoding;
		return false;
	}

	bool global_code_block_offset() const {
		return m_compress || !whole_tile_decoding;
	}

	uint8_t getBandIndex(uint8_t resno, eBandOrientation orientation) const{
		uint8_t index = 0;
		if (resno > 0) {
			index = (uint8_t)orientation;
			index--;
		}
		return index;
	}

	/**
	 * If resno is > 0, return HL,LH or HH band window, otherwise return LL resolution window
	 */
	grk_buffer_2d<T>* band_window(uint8_t resno,eBandOrientation orientation) const{
		assert(resno < resolutions.size());

		return resno > 0 ? res_windows[resno]->bandWindow[orientation] : res_windows[resno]->resWindow;
	}

	// top-level buffer
	grk_buffer_2d<T>* tile_buf() const{
		return res_windows.back()->resWindow;
	}

	grk_rect_u32 m_unreduced_bounds;

	// decompress: reduced tile component coordinates of window
	// compress: unreduced tile component coordinates of entire tile
	grk_rect_u32 m_bounds;

	// full boundsband
	std::vector<Resolution*> resolutions;

	// windowed bounds for windowed decompress, otherwise full bounds
	std::vector<res_window<T>* > res_windows;

	// unreduced number of resolutions
	uint8_t num_resolutions;

	bool m_compress;
	bool whole_tile_decoding;
};


}
