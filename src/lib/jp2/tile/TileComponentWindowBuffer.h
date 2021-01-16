/**
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
 *
 */
#pragma once

#include "grk_includes.h"
#include <stdexcept>
#include <algorithm>

namespace grk {

/**
 *  Class to manage multiple buffers needed to perform DWT transform
 *
 *
 */
template<typename T> struct ResWindow {

	ResWindow(uint8_t numresolutions,
				uint8_t resno,
				grk_buffer_2d<T> *top,
				Resolution *tileCompFullRes,
				Resolution *tileCompFullResLower,
				grk_rect_u32 tileCompWindowBounds,
				grk_rect_u32 tileCompWindowUnreducedBounds,
				uint32_t FILTER_WIDTH) : m_allocated(false),
										m_tileCompFullRes(tileCompFullRes),
										m_tileCompFullResLower(tileCompFullResLower),
										m_resWindow(new grk_buffer_2d<T>(tileCompWindowBounds.width(), tileCompWindowBounds.height())),
										m_resWindowTopLevel(top),
										m_filterWidth(FILTER_WIDTH)
	{
	  for (uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
			m_splitWindow[i] = nullptr;
	  if (FILTER_WIDTH) {

		for (uint8_t orient = 0; orient < ( (resno) > 0 ? BAND_NUM_ORIENTATIONS : 1); orient++) {
			grk_rect_u32 temp = getTileCompBandWindow(numresolutions, resno, orient,tileCompWindowUnreducedBounds);
			m_paddedBandWindows.push_back(temp.grow(FILTER_WIDTH,FILTER_WIDTH));
		}

		if (m_tileCompFullResLower) {

		// 1. set up windows for horizontal and vertical passes
		grk_rect_u32 bandWindowRect[BAND_NUM_ORIENTATIONS];
		auto bandRect = *((grk_rect_u32*)m_tileCompFullResLower);
		bandWindowRect[BAND_ORIENT_LL] = getTileCompBandWindow(numresolutions,resno,BAND_ORIENT_LL,tileCompWindowUnreducedBounds);
		bandWindowRect[BAND_ORIENT_LL].grow(FILTER_WIDTH, bandRect);
		bandWindowRect[BAND_ORIENT_LL] = bandWindowRect[BAND_ORIENT_LL].pan(-(int64_t)bandRect.x0, -(int64_t)bandRect.y0);
		m_bandWindows.push_back(new grk_buffer_2d<T>(bandWindowRect[BAND_ORIENT_LL]));


		bandWindowRect[BAND_ORIENT_HL] = getTileCompBandWindow(numresolutions,resno,BAND_ORIENT_HL,tileCompWindowUnreducedBounds);
		bandRect = m_tileCompFullRes->band[BAND_ORIENT_HL-1];
		bandWindowRect[BAND_ORIENT_HL].grow(FILTER_WIDTH, bandRect);
		bandWindowRect[BAND_ORIENT_HL] = bandWindowRect[BAND_ORIENT_HL].pan(-(int64_t)bandRect.x0, -(int64_t)bandRect.y0);
		m_bandWindows.push_back(new grk_buffer_2d<T>(bandWindowRect[BAND_ORIENT_HL]));

		bandWindowRect[BAND_ORIENT_LH] = getTileCompBandWindow(numresolutions,resno,BAND_ORIENT_LH,tileCompWindowUnreducedBounds);
		bandRect = m_tileCompFullRes->band[BAND_ORIENT_LH-1];
		bandWindowRect[BAND_ORIENT_LH].grow(FILTER_WIDTH, bandRect);
		bandWindowRect[BAND_ORIENT_LH] = bandWindowRect[BAND_ORIENT_LH].pan(-(int64_t)bandRect.x0, -(int64_t)bandRect.y0);
		m_bandWindows.push_back(new grk_buffer_2d<T>(bandWindowRect[BAND_ORIENT_LH]));

		bandWindowRect[BAND_ORIENT_HH] = getTileCompBandWindow(numresolutions,resno,BAND_ORIENT_HH,tileCompWindowUnreducedBounds);
		bandRect = m_tileCompFullRes->band[BAND_ORIENT_HH-1];
		bandWindowRect[BAND_ORIENT_HH].grow(FILTER_WIDTH, bandRect);
		bandWindowRect[BAND_ORIENT_HH] = bandWindowRect[BAND_ORIENT_HH].pan(-(int64_t)bandRect.x0, -(int64_t)bandRect.y0);
		m_bandWindows.push_back(new grk_buffer_2d<T>(bandWindowRect[BAND_ORIENT_HH]));

		auto win_low 		= bandWindowRect[BAND_ORIENT_LL];
		auto win_high 		= bandWindowRect[BAND_ORIENT_HL];
		m_resWindow->x0 	= min<uint32_t>(2 * win_low.x0, 2 * win_high.x0 + 1);
		m_resWindow->x1 	= min<uint32_t>(max<uint32_t>(2 * win_low.x1, 2 * win_high.x1 + 1), m_tileCompFullRes->width());
		assert(m_resWindow->x0 <= m_resWindow->x1);
		win_low 			= bandWindowRect[BAND_ORIENT_LL];
		win_high 			= bandWindowRect[BAND_ORIENT_LH];
		m_resWindow->y0 	= min<uint32_t>(2 * win_low.y0, 2 * win_high.y0 + 1);
		m_resWindow->y1 	= min<uint32_t>(max<uint32_t>(2 * win_low.y1, 2 * win_high.y1 + 1), m_tileCompFullRes->height());
		assert(m_resWindow->y0 <= m_resWindow->y1);

		// two windows formed by horizontal pass and used as input for vertical pass
		grk_rect_u32 splitWindowRect[SPLIT_NUM_ORIENTATIONS];
		splitWindowRect[SPLIT_L] = grk_rect_u32(m_resWindow->x0,
												  bandWindowRect[BAND_ORIENT_LL].y0,
												  m_resWindow->x1,
												  bandWindowRect[BAND_ORIENT_LL].y1);
		m_splitWindow[SPLIT_L] = new grk_buffer_2d<T>(splitWindowRect[SPLIT_L]);

		splitWindowRect[SPLIT_H] = grk_rect_u32(m_resWindow->x0,
													bandWindowRect[BAND_ORIENT_LH].y0 + m_tileCompFullResLower->height(),
													m_resWindow->x1,
													bandWindowRect[BAND_ORIENT_LH].y1 + m_tileCompFullResLower->height());
		m_splitWindow[SPLIT_H] = new grk_buffer_2d<T>(splitWindowRect[SPLIT_H]);
		}
	   } else {
		    // (NOTE: we use relative coordinates)
			// dummy LL band window
			m_bandWindows.push_back( new grk_buffer_2d<T>( 0,0) );
			assert(tileCompFullRes->numBandWindows == 3 || !tileCompFullResLower);
			if (tileCompFullResLower) {
				for (uint32_t i = 0; i < tileCompFullRes->numBandWindows; ++i)
					m_bandWindows.push_back( new grk_buffer_2d<T>(tileCompFullRes->band[i].width(),tileCompFullRes->band[i].height() ) );
				for (uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
					m_splitWindow[i] = new grk_buffer_2d<T>(tileCompWindowBounds.width(), tileCompWindowBounds.height());
				m_splitWindow[SPLIT_L]->y1 = tileCompWindowBounds.y0 + tileCompFullResLower->height();
				m_splitWindow[SPLIT_H]->y0 = m_splitWindow[SPLIT_L]->y1;
			}

		}
	}
	~ResWindow(){
		delete m_resWindow;
		for (auto &b : m_bandWindows)
			delete b;
		for (uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
			delete m_splitWindow[i];
	}

	bool alloc(bool clear){
		if (m_allocated)
			return true;

		// if top level window is present, then all buffers attach to this window
		if (m_resWindowTopLevel) {
			// ensure that top level window is allocated
			if (!m_resWindowTopLevel->alloc(clear))
				return false;

			// for now, we don't allocate bandWindows for windowed decode
			if (m_filterWidth)
				return true;

			// attach to top level window
			if (m_resWindow != m_resWindowTopLevel)
				m_resWindow->attach(m_resWindowTopLevel->data, m_resWindowTopLevel->stride);

			// m_tileCompFullResLower is null for lowest resolution
			if (m_tileCompFullResLower) {
				for (uint8_t orientation = 0; orientation < m_bandWindows.size(); ++orientation){
					switch(orientation){
					case BAND_ORIENT_HL:
						m_bandWindows[orientation]->attach(m_resWindowTopLevel->data + m_tileCompFullResLower->width(),
												m_resWindowTopLevel->stride);
						break;
					case BAND_ORIENT_LH:
						m_bandWindows[orientation]->attach(m_resWindowTopLevel->data + m_tileCompFullResLower->height() * m_resWindowTopLevel->stride,
												m_resWindowTopLevel->stride);
						break;
					case BAND_ORIENT_HH:
						m_bandWindows[orientation]->attach(m_resWindowTopLevel->data + m_tileCompFullResLower->width() +
													m_tileCompFullResLower->height() * m_resWindowTopLevel->stride,
														m_resWindowTopLevel->stride);
						break;
					default:
						break;
					}
				}
				m_splitWindow[SPLIT_L]->attach(m_resWindowTopLevel->data, m_resWindowTopLevel->stride);
				m_splitWindow[SPLIT_H]->attach(m_resWindowTopLevel->data + m_tileCompFullResLower->height() * m_resWindowTopLevel->stride,
											m_resWindowTopLevel->stride);
			}
		} else {
			// resolution window is always allocated
			if (!m_resWindow->alloc(clear))
				return false;
			// for now, we don't allocate bandWindows for windowed decode
			if (m_filterWidth)
				return true;

			// band windows are allocated if present
			for (auto &b : m_bandWindows){
				if (!b->alloc(clear))
					return false;
			}
			if (m_tileCompFullResLower){
				m_splitWindow[SPLIT_L]->attach(m_resWindow->data, m_resWindow->stride);
				m_splitWindow[SPLIT_H]->attach(m_resWindow->data + m_tileCompFullResLower->height() * m_resWindow->stride,m_resWindow->stride);
			}
		}
		m_allocated = true;

		return true;
	}

	bool m_allocated;
	// note: non-null m_tileCompFullRes triggers creation of band window buffers
	Resolution *m_tileCompFullRes;
	// note: m_tileCompFullResLower is null for lowest resolution
	Resolution *m_tileCompFullResLower;
	std::vector< grk_buffer_2d<T>* > m_bandWindows;	 // sub-band coords
	std::vector< grk_rect_u32 > m_paddedBandWindows; // sub-band coords
	grk_buffer_2d<T> *m_splitWindow[SPLIT_NUM_ORIENTATIONS]; // resolution coords
	grk_buffer_2d<T> *m_resWindow;					 		 // resolution coords
	grk_buffer_2d<T> *m_resWindowTopLevel;					 // resolution coords
	uint32_t m_filterWidth;
};


/*
 Various coordinate systems are used to describe windows in the tile component buffer.

 1) Canvas coordinate system:  JPEG 2000 global image coordinates, independent of sub-sampling

 2) Tile coordinate system:  transformed from canvas coordinates, with sub-sampling accounted for

 3) Resolution coordinate system: coordinates relative to a particular resolution's top left hand corner

 4) Sub-band coordinate system: coordinates relative to a particular sub-band's top left hand corner

 */
template<typename T> struct TileComponentWindowBuffer {
	TileComponentWindowBuffer(bool isCompressor,
								bool lossless,
								bool wholeTileDecompress,
								grk_rect_u32 unreducedTileCompDim,
								grk_rect_u32 reducedTileCompDim,
								grk_rect_u32 unreducedTileWindowDim,
								Resolution *tileCompResolutions,
								uint8_t numresolutions,
								uint8_t reducedNumResolutions) : 	m_unreducedBounds(unreducedTileCompDim),
																	m_bounds(reducedTileCompDim),
																	m_numResolutions(numresolutions),
																	m_compress(isCompressor),
																	m_wholeTileDecompress(wholeTileDecompress) {
		if (!m_compress) {
			m_bounds = unreducedTileWindowDim.rectceildivpow2(m_numResolutions - reducedNumResolutions);
			m_bounds = m_bounds.intersection(reducedTileCompDim);
			assert(m_bounds.is_valid());

			m_unreducedBounds = unreducedTileWindowDim.intersection(
					unreducedTileCompDim);
			assert(m_unreducedBounds.is_valid());
		}
		// fill resolutions vector
		assert(reducedNumResolutions > 0);
		for (uint32_t resno = 0; resno < reducedNumResolutions; ++resno)
			m_tileCompResolutions.push_back(tileCompResolutions + resno);
		auto canvasFullRes = tileCompResolutions + reducedNumResolutions - 1;
		auto canvasFullResLower =
				reducedNumResolutions > 1 ?
						tileCompResolutions + reducedNumResolutions - 2 : nullptr;
		// create resolution buffers
		auto topLevel = new ResWindow<T>(numresolutions, reducedNumResolutions - 1,
				nullptr, canvasFullRes, canvasFullResLower, m_bounds,
				m_unreducedBounds,
				wholeTileDecompress ? 0 : getFilterPad<uint32_t>(lossless));
		// setting top level blocks allocation of tileCompBandWindows buffers
		if (!useBandWindows())
			topLevel->m_resWindowTopLevel = topLevel->m_resWindow;

		for (uint8_t resno = 0; resno < reducedNumResolutions - 1; ++resno) {
			// resolution window ==  next resolution band window at orientation 0
			auto res_dims = getTileCompBandWindow(m_numResolutions, (uint8_t) ((resno + 1)), 0,	unreducedTileWindowDim);
			m_resWindows.push_back(
					new ResWindow<T>(numresolutions, resno,
							useBandWindows() ? nullptr : topLevel->m_resWindow,
							tileCompResolutions + resno,
							resno > 0 ? tileCompResolutions + resno - 1 : nullptr,
							res_dims, m_unreducedBounds,
							wholeTileDecompress ?
									0 : getFilterPad<uint32_t>(lossless)));
		}
		m_resWindows.push_back(topLevel);
	}

	~TileComponentWindowBuffer(){
		for (auto& b : m_resWindows)
			delete b;
	}

	/**
	 * Tranform code block offsets to either band coordinates or resolution coordinates
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {LL,HL,LH,HH}
	 * @param offsetx x offset of code block in tile component coordinates
	 * @param offsety y offset of code block in tile component coordinates
	 *
	 */
	void transform(uint8_t resno,eBandOrientation orientation, uint32_t &offsetx, uint32_t &offsety) const {
		assert(resno < m_tileCompResolutions.size());

		auto res = m_tileCompResolutions[resno];
		auto band = res->band + getBandIndex(resno,orientation);

		uint32_t x = offsetx;
		uint32_t y = offsety;

		// get offset relative to band
		x -= band->x0;
		y -= band->y0;

		if (useResCoordsForCodeBlock() && resno > 0){
			auto resLower = m_tileCompResolutions[ resno - 1];

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
		return (useResCoordsForCodeBlock()) ? getTileBuf() : getBandWindow(resno,orientation);
	}

	/**
	 * Get non-LL band window
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {0,1,2,3} for {LL,HL,LH,HH} band windows
	 *
	 */
	const grk_buffer_2d<T>*  getWindow(uint8_t resno,eBandOrientation orientation) const{
		return getBandWindow(resno,orientation);
	}

	const grk_rect_u32 getPaddedTileBandWindow(uint8_t resno,eBandOrientation orientation) const{
		if (m_resWindows[resno]->m_paddedBandWindows.empty())
			return grk_rect_u32();
		return m_resWindows[resno]->m_paddedBandWindows[orientation];
	}

	/*
	 * Get intermediate split window.
	 *
	 * @param orientation 0 for upper split window, and 1 for lower split window
	 */
	const grk_buffer_2d<T>*  getSplitWindow(uint8_t resno,eSplitOrientation orientation) const{
		assert(resno > 0 && resno < m_tileCompResolutions.size());

		return m_resWindows[resno]->m_splitWindow[orientation];
	}

	/**
	 * Get resolution window
	 *
	 * @param resno resolution number
	 *
	 */
	const grk_buffer_2d<T>*  getWindow(uint32_t resno) const{
		return m_resWindows[resno]->m_resWindow;
	}

	/**
	 * Get tile window
	 *
	 *
	 */
	const grk_buffer_2d<T>*  getWindow(void) const{
		return getTileBuf();
	}

	bool alloc(){
		for (auto& b : m_resWindows) {
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
		return m_unreducedBounds;
	}

	uint64_t strided_area(void) const{
		return getTileBuf()->stride * m_bounds.height();
	}

	// set data to buf without owning it
	void attach(T* buffer,uint32_t stride){
		getTileBuf()->attach(buffer,stride);
	}
	// transfer data to buf, and cease owning it
	void transfer(T** buffer, bool* owns, uint32_t *stride){
		getTileBuf()->transfer(buffer,owns,stride);
	}


private:

	bool useBandWindows() const{
		//return !m_compress && m_wholeTileDecompress;
		return false;
	}

	bool useResCoordsForCodeBlock() const {
		return m_compress || !m_wholeTileDecompress;
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
	grk_buffer_2d<T>* getBandWindow(uint8_t resno,eBandOrientation orientation) const{
		assert(resno < m_tileCompResolutions.size());

		return resno > 0 ? m_resWindows[resno]->m_bandWindows[orientation] : m_resWindows[0]->m_resWindow;
	}

	// top-level buffer
	grk_buffer_2d<T>* getTileBuf() const{
		return m_resWindows.back()->m_resWindow;
	}

	grk_rect_u32 m_unreducedBounds;

	// decompress: reduced tile component coordinates of window
	// compress: unreduced tile component coordinates of entire tile
	grk_rect_u32 m_bounds;

	// tile component coords
	std::vector<Resolution*> m_tileCompResolutions;

	// windowed bounds for windowed decompress, otherwise full bounds
	std::vector<ResWindow<T>* > m_resWindows;

	// unreduced number of resolutions
	uint8_t m_numResolutions;

	bool m_compress;
	bool m_wholeTileDecompress;
};

}
