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
template<typename T> struct res_buf {

	res_buf(grk_buffer_2d<T> *top,
				Resolution *full_res,
				Resolution *lower_full_res,
				grk_rect_u32 bounds) :  allocated(false),
										fullRes(full_res),
										fullResLower(lower_full_res),
										resWindow(new grk_buffer_2d<T>(bounds)),
										resWindowTopLevel(top)
	{
		if (full_res) {
			for (uint32_t i = 0; i < full_res->numBandWindows; ++i) {
				bandWindow.push_back( new grk_buffer_2d<T>(full_res->band[i]) );
			}
		}
		for (uint32_t i = 0; i < 2; ++i)
			intermediateWindow[i] = nullptr;
	}
	~res_buf(){
		delete resWindow;
		for (auto &b : bandWindow)
			delete b;
		for (uint32_t i = 0; i < 2; ++i)
			delete intermediateWindow[i];
	}
	bool alloc(bool clear){
		if (allocated)
			return true;

		if (resWindowTopLevel) {
			if (!resWindowTopLevel->alloc(clear))
				return false;

			if (resWindow != resWindowTopLevel)
				resWindow->attach(resWindowTopLevel->data, resWindowTopLevel->stride);

			if (fullRes) {
				assert(fullResLower || bandWindow.size()== 1);
				if (fullResLower) {
					for (uint32_t i = 0; i < bandWindow.size(); ++i){
						switch(i){
						case 0:
							bandWindow[i]->attach(resWindowTopLevel->data + fullResLower->width(),
													resWindowTopLevel->stride);
							break;
						case 1:
							bandWindow[i]->attach(resWindowTopLevel->data + fullResLower->height() * resWindowTopLevel->stride,
													resWindowTopLevel->stride);
							break;
						case 2:
							bandWindow[i]->attach(resWindowTopLevel->data + fullResLower->width() +
														fullResLower->height() * resWindowTopLevel->stride,
													resWindowTopLevel->stride);
							break;
						default:
							break;
						}
					}
				}
			}
		} else {
			if (!resWindow->alloc(clear))
				return false;
			for (auto &b : bandWindow){
				if (!b->alloc(clear))
					return false;
			}
		}
		allocated = true;

		return true;
	}

	bool allocated;
	Resolution *fullRes;
	Resolution *fullResLower;
	std::vector< grk_buffer_2d<T>* > bandWindow;
	// destination buffers for horizontal synthesis DWT transform
	grk_buffer_2d<T> *intermediateWindow[2];
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

        auto current_full_res = tile_comp_resolutions+reduced_num_resolutions-1;
        Resolution *lower_full_res = reduced_num_resolutions > 1 ?
        									tile_comp_resolutions+reduced_num_resolutions-2 : nullptr;

        // create resolution buffers
		 auto topLevel = new res_buf<T>( nullptr,
				 	 	 	 	 	 	 whole_tile_decoding ? current_full_res: nullptr,
				 	 	 	 	 	 	 whole_tile_decoding ? lower_full_res: nullptr,
										 m_bounds);
		 // setting top level blocks allocation of bandWindow buffers
		 if (!use_band_buffers())
			 topLevel->resWindowTopLevel = topLevel->resWindow;
		 for (uint8_t resno = 0; resno < reduced_num_resolutions-1; ++resno){
			 // band window of next resolution up at orientation = 0
			 // is equal to resolution window
			  auto res_dims =  grk_band_window(num_resolutions,
												(uint8_t)(resno+1),
												0,
												unreduced_window_dim);
			 res_buffers.push_back(new res_buf<T>(use_band_buffers() ? nullptr : topLevel->resWindow,
												  whole_tile_decoding ? tile_comp_resolutions+resno : nullptr,
												  (whole_tile_decoding && resno > 0) ? tile_comp_resolutions+resno-1 : nullptr,
												  res_dims) );
		 }
		 res_buffers.push_back(topLevel);
	}
	~TileComponentWindowBuffer(){
		for (auto& b : res_buffers)
			delete b;
	}

	/**
	 * Tranform code block offsets
	 *
	 * @param resno resolution number
	 * @param bandIndex band index (0 for LL band of 0th resolution, otherwise {0,1,2} for {HL,LH,HH} bandWindow
	 * @param offsetx x offset of code block
	 * @param offsety y offset of code block
	 *
	 */
	void transform(uint8_t resno,uint8_t bandIndex, uint32_t &offsetx, uint32_t &offsety) const {
		assert(bandIndex < BAND_NUM_INDICES && resno < resolutions.size());
		assert(resno > 0 || bandIndex==BAND_RES_ZERO_INDEX_LL);

		auto res = resolutions[resno];
		auto band = res->band + bandIndex;

		uint32_t x = offsetx;
		uint32_t y = offsety;

		// get offset relative to band
		x -= band->x0;
		y -= band->y0;

		if (global_code_block_offset()){
			auto res = (resno == 0) ? nullptr : resolutions[ resno - 1];

			if (band->orientation & 1)
				x += res->width();
			if (band->orientation & 2)
				y += res->height();
		}
		offsetx = x;
		offsety = y;
	}

	/**
	 * Get destination buffer
	 *
	 * @param resno resolution number
	 * @param bandIndex band index (0 for LL band of 0th resolution, otherwise {0,1,2} for {HL,LH,HH} bandWindow
	 *
	 */
	grk_buffer_2d<T>* code_block_dest_buf(uint8_t resno,uint8_t bandIndex) const {
		return (global_code_block_offset()) ? tile_buf() : band_buf(resno,bandIndex);
	}

	/**
	 * Get pointer to band buffer
	 *
	 * @param resno resolution number
	 * @param bandIndex band index 0 for resno==0 LL band, or {0,1,2} for {HL,LH,HH} bandWindow
	 *
	 */
	T* ptr(uint32_t resno,uint32_t bandIndex) const{
		assert(bandIndex < 3 && resno > 0 && resno < resolutions.size());

		return band_buf(resno,bandIndex)->data;
	}

	/**
	 * Get pointer to resolution buffer
	 *
	 * @param resno resolution number
	 *
	 */
	T* ptr(uint32_t resno) const{
		return res_buffers[resno]->resWindow->data;
	}

	/**
	 * Get pointer to tile buffer
	 *
	 *
	 */
	T* ptr(void) const{
		return tile_buf()->data;
	}
	/**
	 * Get stride of band buffer
	 *
	 * @param resno resolution number
	 * @param bandIndex band index 0 for resno==0 LL band, or {0,1,2} for {HL,LH,HH} bandWindow
	 */
	uint32_t stride(uint32_t resno,uint32_t bandIndex) const{
		assert(bandIndex < 3 && resno < resolutions.size());

		return band_buf(resno,bandIndex)->stride;
	}

	uint32_t stride(uint32_t resno) const{
		return res_buffers[resno]->resWindow->stride;
	}

	uint32_t stride(void) const{
		return tile_buf()->stride;
	}

	bool alloc(){
		for (auto& b : res_buffers) {
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
		return stride() * m_bounds.height();
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

	bool use_band_buffers() const{
		//return !m_compress && whole_tile_decoding;
		return false;
	}

	bool global_code_block_offset() const {
		return m_compress || !whole_tile_decoding;
	}

	grk_buffer_2d<T>* band_buf(uint32_t resno,uint32_t bandIndex) const{
		assert(bandIndex < 3 && resno < resolutions.size());

		return resno > 0 ? res_buffers[resno]->bandWindow[bandIndex] : res_buffers[resno]->resWindow;
	}

	// top-level buffer
	grk_buffer_2d<T>* tile_buf() const{
		return res_buffers.back()->resWindow;
	}

	grk_rect_u32 m_unreduced_bounds;

	// decompress: reduced tile component coordinates of window
	// compress: unreduced tile component coordinates of entire tile
	grk_rect_u32 m_bounds;

	// full bounds
	std::vector<Resolution*> resolutions;

	// windowed bounds for windowed decompress, otherwise full bounds
	std::vector<res_buf<T>* > res_buffers;

	// unreduced number of resolutions
	uint8_t num_resolutions;

	bool m_compress;
	bool whole_tile_decoding;
};


}
