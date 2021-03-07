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

/*
 Various coordinate systems are used to describe regions in the tile component buffer.

 1) Canvas coordinates:  JPEG 2000 global image coordinates. For tile component, sub-sampling is applied.

 2) Tile coordinates:  coordinates relative to highest resolution's origin

 3) Resolution coordinates: coordinates relative to a specified resolution's origin

 4) Band coordinates: coordinates relative to a specified sub-band's origin

 Note: the name of any method or variable returning non canvas coordinates is appended
 with "REL", for relative coordinates.

 */

/**
 *  Class to manage multiple buffers needed to perform DWT transform
 *
 *
 */
template<typename T> struct ResWindow {
	ResWindow(uint8_t numresolutions,
				uint8_t resno,
				grkBuffer2d<T> *resWindowTopLevelREL,
				Resolution *tileCompAtRes,
				Resolution *tileCompAtLowerRes,
				grkRectU32 tileCompWindow,
				grkRectU32 tileCompWindowUnreduced,
				grkRectU32 tileCompUnreduced,
				uint32_t FILTER_WIDTH) : m_allocated(false),
										m_tileCompRes(tileCompAtRes),
										m_tileCompResLower(tileCompAtLowerRes),
										m_resWindowBufferREL(new grkBuffer2d<T>(tileCompWindow.width(), tileCompWindow.height())),
										m_resWindowTopLevelBufferREL(resWindowTopLevelREL),
										m_filterWidth(FILTER_WIDTH)
	{
	  for (uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
			m_splitResWindowBufferREL[i] = nullptr;
	  // windowed decompression
	  if (FILTER_WIDTH) {
		uint32_t numDecomps = (resno == 0) ?
				(uint32_t)(numresolutions - 1U) : (uint32_t)(numresolutions - resno);

		/*
		m_paddedBandWindow is only used for determining which precincts and code blocks overlap
		the window of interest, in each respective resolution
		*/
		for (uint8_t orient = 0; orient < ( (resno) > 0 ? BAND_NUM_ORIENTATIONS : 1); orient++) {
			m_paddedBandWindow.push_back(
					getTileCompBandWindow(numDecomps, orient,tileCompWindowUnreduced, tileCompUnreduced, 2 * FILTER_WIDTH));
		}

		if (m_tileCompResLower) {
			assert(resno > 0);
			for (uint8_t orient = 0; orient < BAND_NUM_ORIENTATIONS; orient++) {
				// todo: should only need padding equal to FILTER_WIDTH, not 2*FILTER_WIDTH
				auto tileBandWindow = getTileCompBandWindow(numDecomps,orient,tileCompWindowUnreduced,tileCompUnreduced,2*FILTER_WIDTH);
				auto tileBand = orient == BAND_ORIENT_LL ? *((grkRectU32*)m_tileCompResLower) : m_tileCompRes->tileBand[orient-1];
				auto bandWindowREL = tileBandWindow.pan(-(int64_t)tileBand.x0, -(int64_t)tileBand.y0);
				m_paddedBandWindowBufferREL.push_back(new grkBuffer2d<T>(bandWindowREL));
			}
			auto winLow 		= m_paddedBandWindowBufferREL[BAND_ORIENT_LL];
			auto winHigh 		= m_paddedBandWindowBufferREL[BAND_ORIENT_HL];
			m_resWindowBufferREL->x0 	= min<uint32_t>(2 * winLow->x0, 2 * winHigh->x0 + 1);
			m_resWindowBufferREL->x1 	= max<uint32_t>(2 * winLow->x1, 2 * winHigh->x1 + 1);
			winLow 				= m_paddedBandWindowBufferREL[BAND_ORIENT_LL];
			winHigh 			= m_paddedBandWindowBufferREL[BAND_ORIENT_LH];
			m_resWindowBufferREL->y0 	= min<uint32_t>(2 * winLow->y0, 2 * winHigh->y0 + 1);
			m_resWindowBufferREL->y1 	= max<uint32_t>(2 * winLow->y1, 2 * winHigh->y1 + 1);

			//todo: shouldn't need to clip
			auto resBounds = grkRectU32(0,0,m_tileCompRes->width(),m_tileCompRes->height());
			m_resWindowBufferREL->clip(&resBounds);

			// two windows formed by horizontal pass and used as input for vertical pass
			grkRectU32 splitResWindowREL[SPLIT_NUM_ORIENTATIONS];
			splitResWindowREL[SPLIT_L] = grkRectU32(m_resWindowBufferREL->x0,
													  m_paddedBandWindowBufferREL[BAND_ORIENT_LL]->y0,
													  m_resWindowBufferREL->x1,
													  m_paddedBandWindowBufferREL[BAND_ORIENT_LL]->y1);
			m_splitResWindowBufferREL[SPLIT_L] = new grkBuffer2d<T>(splitResWindowREL[SPLIT_L]);
			splitResWindowREL[SPLIT_H] = grkRectU32(m_resWindowBufferREL->x0,
														m_paddedBandWindowBufferREL[BAND_ORIENT_LH]->y0 + m_tileCompResLower->height(),
														m_resWindowBufferREL->x1,
														m_paddedBandWindowBufferREL[BAND_ORIENT_LH]->y1 + m_tileCompResLower->height());
			m_splitResWindowBufferREL[SPLIT_H] = new grkBuffer2d<T>(splitResWindowREL[SPLIT_H]);
		}
	   // compression or full tile decompression
	   } else {
			// dummy LL band window
			m_paddedBandWindowBufferREL.push_back( new grkBuffer2d<T>( 0,0) );
			assert(tileCompAtRes->numTileBandWindows == 3 || !tileCompAtLowerRes);
			if (m_tileCompResLower) {
				for (uint32_t i = 0; i < tileCompAtRes->numTileBandWindows; ++i){
					auto b = tileCompAtRes->tileBand + i;
					m_paddedBandWindowBufferREL.push_back( new grkBuffer2d<T>(b->width(),b->height() ) );
				}
				// note: only dimensions of split resolution windows matter, not coordinates
				for (uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
					m_splitResWindowBufferREL[i] = new grkBuffer2d<T>(tileCompWindow.width(), tileCompWindow.height()/2);
			}
		}
	}
	~ResWindow(){
		delete m_resWindowBufferREL;
		for (auto &b : m_paddedBandWindowBufferREL)
			delete b;
		for (uint32_t i = 0; i < SPLIT_NUM_ORIENTATIONS; ++i)
			delete m_splitResWindowBufferREL[i];
	}
	bool alloc(bool clear){
		if (m_allocated)
			return true;

		// if top level window is present, then all buffers attach to this window
		if (m_resWindowTopLevelBufferREL) {
			// ensure that top level window is allocated
			if (!m_resWindowTopLevelBufferREL->alloc(clear))
				return false;

			// for now, we don't allocate bandWindows for windowed decompression
			if (m_filterWidth)
				return true;

			// attach to top level window
			if (m_resWindowBufferREL != m_resWindowTopLevelBufferREL)
				m_resWindowBufferREL->attach(m_resWindowTopLevelBufferREL->data, m_resWindowTopLevelBufferREL->stride);

			// m_tileCompResLower is null for lowest resolution
			if (m_tileCompResLower) {
				for (uint8_t orientation = 0; orientation < m_paddedBandWindowBufferREL.size(); ++orientation){
					switch(orientation){
					case BAND_ORIENT_HL:
						m_paddedBandWindowBufferREL[orientation]->attach(m_resWindowTopLevelBufferREL->data + m_tileCompResLower->width(),
												m_resWindowTopLevelBufferREL->stride);
						break;
					case BAND_ORIENT_LH:
						m_paddedBandWindowBufferREL[orientation]->attach(m_resWindowTopLevelBufferREL->data + m_tileCompResLower->height() * m_resWindowTopLevelBufferREL->stride,
												m_resWindowTopLevelBufferREL->stride);
						break;
					case BAND_ORIENT_HH:
						m_paddedBandWindowBufferREL[orientation]->attach(m_resWindowTopLevelBufferREL->data + m_tileCompResLower->width() +
													m_tileCompResLower->height() * m_resWindowTopLevelBufferREL->stride,
														m_resWindowTopLevelBufferREL->stride);
						break;
					default:
						break;
					}
				}
				m_splitResWindowBufferREL[SPLIT_L]->attach(m_resWindowTopLevelBufferREL->data, m_resWindowTopLevelBufferREL->stride);
				m_splitResWindowBufferREL[SPLIT_H]->attach(m_resWindowTopLevelBufferREL->data + m_tileCompResLower->height() * m_resWindowTopLevelBufferREL->stride,
											m_resWindowTopLevelBufferREL->stride);
			}
		} else {
			// resolution window is always allocated
			if (!m_resWindowBufferREL->alloc(clear))
				return false;
			// for now, we don't allocate bandWindows for windowed decode
			if (m_filterWidth)
				return true;

			// band windows are allocated if present
			for (auto &b : m_paddedBandWindowBufferREL){
				if (!b->alloc(clear))
					return false;
			}
			if (m_tileCompResLower){
				m_splitResWindowBufferREL[SPLIT_L]->attach(m_resWindowBufferREL->data, m_resWindowBufferREL->stride);
				m_splitResWindowBufferREL[SPLIT_H]->attach(m_resWindowBufferREL->data + m_tileCompResLower->height() * m_resWindowBufferREL->stride,m_resWindowBufferREL->stride);
			}
		}
		m_allocated = true;

		return true;
	}

	bool m_allocated;
	Resolution *m_tileCompRes;   	// when non-null, it triggers creation of band window buffers
	Resolution *m_tileCompResLower; // null for lowest resolution
	std::vector< grkBuffer2d<T>* > m_paddedBandWindowBufferREL;		// coordinates are relative to band origin
	std::vector< grkRectU32 > m_paddedBandWindow; 	 				// canvas coordinates
	grkBuffer2d<T> *m_splitResWindowBufferREL[SPLIT_NUM_ORIENTATIONS]; 	// resolution coordinates
	grkBuffer2d<T> *m_resWindowBufferREL;					 		 		// resolution coordinates
	grkBuffer2d<T> *m_resWindowTopLevelBufferREL;					 		// tile coordinates
	uint32_t m_filterWidth;
};

template<typename T> struct TileComponentWindowBuffer {
	TileComponentWindowBuffer(bool isCompressor,
								bool lossless,
								bool wholeTileDecompress,
								grkRectU32 tileCompUnreduced,
								grkRectU32 tileCompReduced,
								grkRectU32 unreducedTileOrImageCompWindow,
								Resolution *tileCompResolution,
								uint8_t numresolutions,
								uint8_t reducedNumResolutions) : 	m_unreducedBounds(tileCompUnreduced),
																	m_bounds(tileCompReduced),
																	m_numResolutions(numresolutions),
																	m_compress(isCompressor),
																	m_wholeTileDecompress(wholeTileDecompress) {
		if (!m_compress) {
			// for decompress, we are passed the unreduced image component window
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
			m_resolution.push_back(tileCompResolution + resno);

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
		// setting top level prevents allocation of tileCompBandWindows buffers
		if (!useBandWindows())
			topLevel->m_resWindowTopLevelBufferREL = topLevel->m_resWindowBufferREL;

		for (uint8_t resno = 0; resno < reducedNumResolutions - 1; ++resno) {
			// resolution window ==  next resolution band window at orientation 0
			auto resDims = getTileCompBandWindow((uint32_t)(numresolutions - 1 - resno), 0,	m_unreducedBounds);
			m_resWindowREL.push_back(
					new ResWindow<T>(numresolutions,
									resno,
									useBandWindows() ? nullptr : topLevel->m_resWindowBufferREL,
									tileCompResolution + resno,
									resno > 0 ? tileCompResolution + resno - 1 : nullptr,
									resDims,
									m_unreducedBounds,
									tileCompUnreduced,
									wholeTileDecompress ? 0 : getFilterPad<uint32_t>(lossless))
									);
		}
		m_resWindowREL.push_back(topLevel);
	}

	~TileComponentWindowBuffer(){
		for (auto& b : m_resWindowREL)
			delete b;
	}

	/**
	 * Transform code block offsets from canvas coordinates
	 * to either band coordinates (relative to sub band origin)
	 * or tile coordinates (relative to tile origin)
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {LL,HL,LH,HH}
	 * @param offsetx x offset of code block in canvas coordinates
	 * @param offsety y offset of code block in canvas coordinates
	 *
	 */
	void toRelativeCoordinates(uint8_t resno,
								eBandOrientation orientation,
								uint32_t &offsetx,
								uint32_t &offsety) const {
		assert(resno < m_resolution.size());

		auto res = m_resolution[resno];
		auto band = res->tileBand + getBandIndex(resno,orientation);

		uint32_t x = offsetx;
		uint32_t y = offsety;

		// get offset relative to band
		x -= band->x0;
		y -= band->y0;

		if (useTileCoordinatesForCodeblock() && resno > 0){
			auto resLower = m_resolution[ resno - 1U];

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
	const grkBuffer2d<T>* getCodeBlockDestWindowREL(uint8_t resno,eBandOrientation orientation) const {
		return (useTileCoordinatesForCodeblock()) ? getTileWindowREL() : getBandWindowREL(resno,orientation);
	}

	/**
	 * Get band window, in relative band coordinates
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {0,1,2,3} for {LL,HL,LH,HH} band windows
	 *
	 * If resno is > 0, return LL,HL,LH or HH band window, otherwise return LL resolution window
	 *
	 */
	const grkBuffer2d<T>*  getBandWindowREL(uint8_t resno,eBandOrientation orientation) const{
		assert(resno < m_resolution.size());
		assert(resno > 0 || orientation == BAND_ORIENT_LL);

		if (resno==0 && (m_compress || m_wholeTileDecompress) )
			return m_resWindowREL[0]->m_resWindowBufferREL ;

		return m_resWindowREL[resno]->m_paddedBandWindowBufferREL[orientation];
	}

	/**
	 * Get padded band window, in canvas coordinates
	 *
	 * @param resno resolution number
	 * @param orientation band orientation {0,1,2,3} for {LL,HL,LH,HH} band windows
	 *
	 */
	const grkRectU32* getPaddedBandWindow(uint8_t resno,eBandOrientation orientation) const{
		if (m_resWindowREL[resno]->m_paddedBandWindow.empty())
			return nullptr;
		return &m_resWindowREL[resno]->m_paddedBandWindow[orientation];
	}

	/*
	 * Get intermediate split window, in resolution coordinates
	 *
	 * @param orientation 0 for upper split window, and 1 for lower split window
	 */
	const grkBuffer2d<T>*  getSplitWindowREL(uint8_t resno,eSplitOrientation orientation) const{
		assert(resno > 0 && resno < m_resolution.size());

		return m_resWindowREL[resno]->m_splitResWindowBufferREL[orientation];
	}

	/**
	 * Get resolution window, in resolution coordinates
	 *
	 * @param resno resolution number
	 *
	 */
	const grkBuffer2d<T>*  getResWindowREL(uint32_t resno) const{
		return m_resWindowREL[resno]->m_resWindowBufferREL;
	}

	/**
	 * Get tile window i.e. highest resolution window,
	 * in tile coordinates
	 *
	 *
	 */
	grkBuffer2d<T>*  getTileWindowREL(void) const{
		return m_resWindowREL.back()->m_resWindowBufferREL;
	}

	bool alloc(){
		for (auto& b : m_resWindowREL) {
			if (!b->alloc(!m_compress))
				return false;
		}

		return true;
	}

	/**
	 * Get bounds of tile component
	 * decompress: reduced canvas coordinates of window
	 * compress: unreduced canvas coordinates of entire tile
	 */
	grkRectU32 bounds() const{
		return m_bounds;
	}

	grkRectU32 unreducedBounds() const{
		return m_unreducedBounds;
	}

	uint64_t stridedArea(void) const{
		return getTileWindowREL()->stride * m_bounds.height();
	}

	// set data to buf without owning it
	void attach(T* buffer,uint32_t stride){
		getTileWindowREL()->attach(buffer,stride);
	}
	// transfer data to buf, and cease owning it
	void transfer(T** buffer, uint32_t *stride){
		getTileWindowREL()->transfer(buffer,stride);
	}


private:

	bool useBandWindows() const{
		//return !m_compress && m_wholeTileDecompress;
		return false;
	}

	bool useTileCoordinatesForCodeblock() const {
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

	/******************************************************/
	// decompress: unreduced/reduced image component window
	// compress:  unreduced/reduced tile component
	grkRectU32 m_unreducedBounds;
	grkRectU32 m_bounds;
	/******************************************************/

	// tile component coords
	std::vector<Resolution*> m_resolution;

	// windowed bounds for windowed decompress, otherwise full bounds
	std::vector<ResWindow<T>* > m_resWindowREL;

	// unreduced number of resolutions
	uint8_t m_numResolutions;

	bool m_compress;
	bool m_wholeTileDecompress;
};

}
