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

	// note:
	// 1. any variable with 'tile' in its name is in canvas coordinates
	// 2. band windows are relative to band origin
	// 3. res windows are relate to LL band windows
	ResWindow(uint8_t numresolutions,
				uint8_t resno,
				grk_buffer_2d<T> *resWindowTopLevel,
				Resolution *tileCompAtRes,
				Resolution *tileCompAtLowerRes,
				grk_rect_u32 tileCompWindow,
				grk_rect_u32 tileCompWindowUnreduced,
				grk_rect_u32 tileCompUnreduced,
				uint32_t FILTER_WIDTH) : m_allocated(false),
										m_tileCompRes(tileCompAtRes),
										m_tileCompResLower(tileCompAtLowerRes),
										m_resWindow(new grk_buffer_2d<T>(tileCompWindow.width(), tileCompWindow.height())),
										m_resWindowTopLevel(resWindowTopLevel),
										m_filterWidth(FILTER_WIDTH)
	{
	  (void)tileCompUnreduced;
	  for (uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
			m_splitResWindow[i] = nullptr;
	  // windowed decompression
	  if (FILTER_WIDTH) {
		uint32_t numDecomps = (resno == 0) ?
				(uint32_t)(numresolutions - 1U) : (uint32_t)(numresolutions - resno);

		/*
		m_paddedTileBandWindow is only used for determining which precincts and code blocks overlap
		the window of interest, in each respective resolution
		*/
		for (uint8_t orient = 0; orient < ( (resno) > 0 ? BAND_NUM_ORIENTATIONS : 1); orient++) {
			m_paddedTileBandWindow.push_back(
					getTileCompBandWindow(numDecomps, orient,tileCompWindowUnreduced, tileCompUnreduced, FILTER_WIDTH));
		}

		if (m_tileCompResLower) {
/*
			auto b0 = getTileCompBandWindow(numresolutions,resno,BAND_ORIENT_LL,tileCompWindowUnreduced);
			auto b1 = getTileCompBandWindow(numresolutions,resno,BAND_ORIENT_HL,tileCompWindowUnreduced);
			auto b2 = getTileCompBandWindow(numresolutions,resno,BAND_ORIENT_LH,tileCompWindowUnreduced);
			auto b3 = getTileCompBandWindow(numresolutions,resno,BAND_ORIENT_HH,tileCompWindowUnreduced);

			assert(b0.width() + b1.width() == tileCompWindow.width());
			assert(b2.width() + b3.width() == tileCompWindow.width());
			assert(b0.height() + b2.height() == tileCompWindow.height());
			assert(b1.height() + b3.height() == tileCompWindow.height());
*/
			for (uint8_t orient = 0; orient < BAND_NUM_ORIENTATIONS; ++orient) {
				auto tileBandWindow = getTileCompBandWindow(numDecomps,orient,tileCompWindowUnreduced,tileCompUnreduced,2*FILTER_WIDTH);
				auto tileBand = orient == BAND_ORIENT_LL ? *((grk_rect_u32*)m_tileCompResLower) : m_tileCompRes->band[orient-1];
				auto bandWindow = tileBandWindow.pan(-(int64_t)tileBand.x0, -(int64_t)tileBand.y0);
				m_bandWindowBufferDim.push_back(new grk_buffer_2d<T>(bandWindow));
			}
			auto win_low 		= m_bandWindowBufferDim[BAND_ORIENT_LL];
			auto win_high 		= m_bandWindowBufferDim[BAND_ORIENT_HL];
			m_resWindow->x0 	= min<uint32_t>(2 * win_low->x0, 2 * win_high->x0 + 1);
			m_resWindow->x1 	= min<uint32_t>(max<uint32_t>(2 * win_low->x1, 2 * win_high->x1 + 1), m_tileCompRes->width());
			win_low 			= m_bandWindowBufferDim[BAND_ORIENT_LL];
			win_high 			= m_bandWindowBufferDim[BAND_ORIENT_LH];
			m_resWindow->y0 	= min<uint32_t>(2 * win_low->y0, 2 * win_high->y0 + 1);
			m_resWindow->y1 	= min<uint32_t>(max<uint32_t>(2 * win_low->y1, 2 * win_high->y1 + 1), m_tileCompRes->height());

			// two windows formed by horizontal pass and used as input for vertical pass
			grk_rect_u32 splitResWindowRect[SPLIT_NUM_ORIENTATIONS];
			splitResWindowRect[SPLIT_L] = grk_rect_u32(m_resWindow->x0,
													  m_bandWindowBufferDim[BAND_ORIENT_LL]->y0,
													  m_resWindow->x1,
													  m_bandWindowBufferDim[BAND_ORIENT_LL]->y1);
			m_splitResWindow[SPLIT_L] = new grk_buffer_2d<T>(splitResWindowRect[SPLIT_L]);
			splitResWindowRect[SPLIT_H] = grk_rect_u32(m_resWindow->x0,
														m_bandWindowBufferDim[BAND_ORIENT_LH]->y0 + m_tileCompResLower->height(),
														m_resWindow->x1,
														m_bandWindowBufferDim[BAND_ORIENT_LH]->y1 + m_tileCompResLower->height());
			m_splitResWindow[SPLIT_H] = new grk_buffer_2d<T>(splitResWindowRect[SPLIT_H]);
		}
	   // compression or full tile decompression
	   } else {
			// dummy LL band window
			m_bandWindowBufferDim.push_back( new grk_buffer_2d<T>( 0,0) );
			assert(tileCompAtRes->numBandWindows == 3 || !tileCompAtLowerRes);
			if (m_tileCompResLower) {
				for (uint32_t i = 0; i < tileCompAtRes->numBandWindows; ++i){
					auto b = tileCompAtRes->band + i;
					m_bandWindowBufferDim.push_back( new grk_buffer_2d<T>(b->width(),b->height() ) );
				}
				for (uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i) {
					auto b = &tileCompWindow;
					m_splitResWindow[i] = new grk_buffer_2d<T>(b->width(), b->height());
				}
				m_splitResWindow[SPLIT_L]->y1 = tileCompWindow.y0 + tileCompAtLowerRes->height();
				m_splitResWindow[SPLIT_H]->y0 = m_splitResWindow[SPLIT_L]->y1;
			}
		}
	}
	~ResWindow(){
		delete m_resWindow;
		for (auto &b : m_bandWindowBufferDim)
			delete b;
		for (uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
			delete m_splitResWindow[i];
	}
	bool alloc(bool clear){
		if (m_allocated)
			return true;

		// if top level window is present, then all buffers attach to this window
		if (m_resWindowTopLevel) {
			// ensure that top level window is allocated
			if (!m_resWindowTopLevel->alloc(clear))
				return false;

			// for now, we don't allocate bandWindows for windowed decompression
			if (m_filterWidth)
				return true;

			// attach to top level window
			if (m_resWindow != m_resWindowTopLevel)
				m_resWindow->attach(m_resWindowTopLevel->data, m_resWindowTopLevel->stride);

			// m_tileCompResLower is null for lowest resolution
			if (m_tileCompResLower) {
				for (uint8_t orientation = 0; orientation < m_bandWindowBufferDim.size(); ++orientation){
					switch(orientation){
					case BAND_ORIENT_HL:
						m_bandWindowBufferDim[orientation]->attach(m_resWindowTopLevel->data + m_tileCompResLower->width(),
												m_resWindowTopLevel->stride);
						break;
					case BAND_ORIENT_LH:
						m_bandWindowBufferDim[orientation]->attach(m_resWindowTopLevel->data + m_tileCompResLower->height() * m_resWindowTopLevel->stride,
												m_resWindowTopLevel->stride);
						break;
					case BAND_ORIENT_HH:
						m_bandWindowBufferDim[orientation]->attach(m_resWindowTopLevel->data + m_tileCompResLower->width() +
													m_tileCompResLower->height() * m_resWindowTopLevel->stride,
														m_resWindowTopLevel->stride);
						break;
					default:
						break;
					}
				}
				m_splitResWindow[SPLIT_L]->attach(m_resWindowTopLevel->data, m_resWindowTopLevel->stride);
				m_splitResWindow[SPLIT_H]->attach(m_resWindowTopLevel->data + m_tileCompResLower->height() * m_resWindowTopLevel->stride,
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
			for (auto &b : m_bandWindowBufferDim){
				if (!b->alloc(clear))
					return false;
			}
			if (m_tileCompResLower){
				m_splitResWindow[SPLIT_L]->attach(m_resWindow->data, m_resWindow->stride);
				m_splitResWindow[SPLIT_H]->attach(m_resWindow->data + m_tileCompResLower->height() * m_resWindow->stride,m_resWindow->stride);
			}
		}
		m_allocated = true;

		return true;
	}

	bool m_allocated;
	Resolution *m_tileCompRes;   	// when non-null, it triggers creation of band window buffers
	Resolution *m_tileCompResLower; // null for lowest resolution
	std::vector< grk_buffer_2d<T>* > m_bandWindowBufferDim;	 	// coordinates are relative to band origin
	std::vector< grk_rect_u32 > m_paddedTileBandWindow; 	 		// canvas coordinates
	grk_buffer_2d<T> *m_splitResWindow[SPLIT_NUM_ORIENTATIONS]; 	// resolution coordinates
	grk_buffer_2d<T> *m_resWindow;					 		 	// resolution coordinates
	grk_buffer_2d<T> *m_resWindowTopLevel;					 	// resolution coordinates
	uint32_t m_filterWidth;
};


/*
 Various coordinate systems are used to describe windows in the tile component buffer.

 1) Canvas coordinate system:  JPEG 2000 global image coordinates, independent of sub-sampling

 2) Tile coordinate system:  canvas coordinates with sub-sampling accounted for

 3) Resolution coordinate system: coordinates relative to a particular resolution's top left hand corner

 4) Sub-band coordinate system: coordinates relative to a particular sub-band's top left hand corner

 */
template<typename T> struct TileComponentWindowBuffer {
	TileComponentWindowBuffer(bool isCompressor,
								bool lossless,
								bool wholeTileDecompress,
								grk_rect_u32 tileCompUnreduced,
								grk_rect_u32 tileCompReduced,
								grk_rect_u32 unreducedTileOrImageCompWindow,
								Resolution *tileCompResolution,
								uint8_t numresolutions,
								uint8_t reducedNumResolutions) : 	m_unreducedBounds(tileCompUnreduced),
																	m_bounds(tileCompReduced),
																	m_numResolutions(numresolutions),
																	m_compress(isCompressor),
																	m_wholeTileDecompress(wholeTileDecompress) {
		if (!m_compress) {
			auto unreducedImageCompWindow =  unreducedTileOrImageCompWindow;
			m_bounds = unreducedImageCompWindow.rectceildivpow2((uint32_t)(m_numResolutions - reducedNumResolutions));
			m_bounds = m_bounds.intersection(tileCompReduced);
			assert(m_bounds.is_valid());
			m_unreducedBounds = unreducedImageCompWindow.intersection(tileCompUnreduced);
			assert(m_unreducedBounds.is_valid());
		}
		// fill resolutions vector
		assert(reducedNumResolutions > 0);
		for (uint32_t resno = 0; resno < reducedNumResolutions; ++resno)
			m_tileCompResolutions.push_back(tileCompResolution + resno);

		auto tileCompAtRes = tileCompResolution + reducedNumResolutions - 1;
		auto tileCompAtLowerRes =	reducedNumResolutions > 1 ?	tileCompResolution + reducedNumResolutions - 2 : nullptr;
		// create resolution buffers
		auto topLevel = new ResWindow<T>(numresolutions,
										(uint8_t)(reducedNumResolutions - 1U),
										nullptr,
										tileCompAtRes,
										tileCompAtLowerRes,
										m_bounds,
										m_unreducedBounds,
										tileCompUnreduced,
										wholeTileDecompress ? 0 : getFilterPad<uint32_t>(lossless));
		// setting top level blocks allocation of tileCompBandWindows buffers
		if (!useBandWindows())
			topLevel->m_resWindowTopLevel = topLevel->m_resWindow;

		for (uint8_t resno = 0; resno < reducedNumResolutions - 1; ++resno) {
			// resolution window ==  next resolution band window at orientation 0
			auto res_dims = getTileCompBandWindow((uint32_t)(numresolutions - 1 - resno), 0,	m_unreducedBounds);
			m_resWindows.push_back(
					new ResWindow<T>(numresolutions,
									resno,
									useBandWindows() ? nullptr : topLevel->m_resWindow,
									tileCompResolution + resno,
									resno > 0 ? tileCompResolution + resno - 1 : nullptr,
									res_dims,
									m_unreducedBounds,
									tileCompUnreduced,
									wholeTileDecompress ? 0 : getFilterPad<uint32_t>(lossless))
									);
		}
		m_resWindows.push_back(topLevel);
	}

	~TileComponentWindowBuffer(){
		for (auto& b : m_resWindows)
			delete b;
	}

	/**
	 * Tranform code block offsets to canvas coordinates
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {LL,HL,LH,HH}
	 * @param offsetx x offset of code block in tile component coordinates
	 * @param offsety y offset of code block in tile component coordinates
	 *
	 */
	void transformToCanvasCoordinates(uint8_t resno,eBandOrientation orientation, uint32_t &offsetx, uint32_t &offsety) const {
		assert(resno < m_tileCompResolutions.size());

		auto res = m_tileCompResolutions[resno];
		auto band = res->band + getBandIndex(resno,orientation);

		uint32_t x = offsetx;
		uint32_t y = offsety;

		// get offset relative to band
		x -= band->x0;
		y -= band->y0;

		if (useResCoordsForCodeBlock() && resno > 0){
			auto resLower = m_tileCompResolutions[ resno - 1U];

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

	const grk_rect_u32* getPaddedTileBandWindow(uint8_t resno,eBandOrientation orientation) const{
		if (m_resWindows[resno]->m_paddedTileBandWindow.empty())
			return nullptr;
		return &m_resWindows[resno]->m_paddedTileBandWindow[orientation];
	}

	/*
	 * Get intermediate split window.
	 *
	 * @param orientation 0 for upper split window, and 1 for lower split window
	 */
	const grk_buffer_2d<T>*  getSplitWindow(uint8_t resno,eSplitOrientation orientation) const{
		assert(resno > 0 && resno < m_tileCompResolutions.size());

		return m_resWindows[resno]->m_splitResWindow[orientation];
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
	void transfer(T** buffer, uint32_t *stride){
		getTileBuf()->transfer(buffer,stride);
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
	 * If resno is > 0, return LL,HL,LH or HH band window, otherwise return LL resolution window
	 */
	grk_buffer_2d<T>* getBandWindow(uint8_t resno,eBandOrientation orientation) const{
		assert(resno < m_tileCompResolutions.size());

		return resno > 0 ? m_resWindows[resno]->m_bandWindowBufferDim[orientation] : m_resWindows[0]->m_resWindow;
	}

	// top-level buffer
	grk_buffer_2d<T>* getTileBuf() const{
		return m_resWindows.back()->m_resWindow;
	}

	/******************************************************/
	// decompress: unreduced/reduced image component window
	// compress:  unreduced/reduced tile component
	grk_rect_u32 m_unreducedBounds;
	grk_rect_u32 m_bounds;
	/******************************************************/

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
