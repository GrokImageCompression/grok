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

#include "util.h"
#include "grok_intmath.h"
#include "TagTree.h"
#include "TileProcessor.h"
#include <stdexcept>

namespace grk {

template<typename T> struct res_buf {

	res_buf(grk_resolution *res, grk_rect_u32 res_bounds) : res(new grk_buffer_2d<T>(res_bounds))
	{
		for (uint32_t i = 0; i < 3; ++i)
			bands[i] = res ? new grk_buffer_2d<T>(res->bands[i]) : nullptr;
	}
	~res_buf(){
		delete res;
		for (uint32_t i = 0; i < 3; ++i)
			delete bands[i];
	}
	bool alloc(bool clear){
		if (!res->alloc(clear))
			return false;
		for (uint32_t i = 0; i < 3; ++i){
			if (bands[i] && !bands[i]->alloc(clear))
				return false;
		}
		return true;
	}

	grk_buffer_2d<T> *res;
	grk_buffer_2d<T> *bands[3];
};


/*
 Note: various coordinate systems are used to describe regions in the tile buffer.

 1) Canvas coordinate system:  JPEG 2000 global image coordinates, independent of sub-sampling

 2) Tile coordinate system:  coordinates relative to a tile's top left hand corner, with
 sub-sampling accounted for

 3) Resolution coordinate system:  coordinates relative to a resolution's top left hand corner

 4) Sub-band coordinate system: coordinates relative to a particular sub-band's top left hand corner

 */

template<typename T> struct TileComponentBuffer {
	TileComponentBuffer(grk_image *output_image,
						uint32_t dx,uint32_t dy,
						grk_rect unreduced_dim,
						grk_rect reduced_dim,
						uint32_t reduced_num_resolutions,
						uint32_t numresolutions,
						grk_resolution *tile_comp_resolutions,
						bool whole_tile) :
							m_unreduced_bounds(unreduced_dim),
							m_bounds(reduced_dim),
							num_resolutions(numresolutions),
							m_encode(output_image==nullptr),
							whole_tile_decoding(whole_tile)
	{
		//note: only decoder has output image
		if (output_image) {
			// tile component coordinates
			m_unreduced_bounds = grk_rect(ceildiv<uint32_t>(output_image->x0, dx),
										ceildiv<uint32_t>(output_image->y0, dy),
										ceildiv<uint32_t>(output_image->x1, dx),
										ceildiv<uint32_t>(output_image->y1, dy));

			m_bounds 	= m_unreduced_bounds;
			m_bounds.rectceildivpow2(num_resolutions - reduced_num_resolutions);

			/* clip region dimensions against tile */
			reduced_dim.clip(m_bounds, &m_bounds);
			unreduced_dim.clip(m_unreduced_bounds, &m_unreduced_bounds);
		}

		/* fill resolutions vector */
        assert(reduced_num_resolutions>0);

        for (uint32_t resno = 0; resno < reduced_num_resolutions; ++resno)
        	resolutions.push_back(tile_comp_resolutions+resno);

        if (m_encode || !whole_tile_decoding) {
        	res_buffers.push_back(new res_buf<T>( nullptr, m_bounds.to_u32()) );
        }
        else {
        	// lowest resolution equals 0th band
        	 res_buffers.push_back(new res_buf<T>(nullptr, tile_comp_resolutions->bands[0].to_u32()) );

        	 for (uint32_t resno = 1; resno < reduced_num_resolutions; ++resno)
        		 res_buffers.push_back(new res_buf<T>( tile_comp_resolutions+resno, m_bounds.to_u32()) );
        }
	}
	~TileComponentBuffer(){
		for (auto& b : res_buffers)
			delete b;
	}
	/**
	 * Get pointer to code block region in tile buffer
	 *
	 * @param resno resolution number
	 * @param bandno band number (0 for LL band of 0th resolution, otherwise
	 * 0 for LL band of 0th resolution, or {0,1,2} for {HL,LH,HH} bands
	 * @param offsetx x offset of code block
	 * @param offsety y offset of code block
	 *
	 */
	T* cblk_ptr(uint32_t resno,uint32_t bandno, uint32_t &offsetx, uint32_t &offsety) const {
		(void)bandno;
		if (resno==0)
			assert(bandno==0);
		else
			assert(bandno < 3);

		auto res = resolutions[resno];
		auto pres = resno == 0 ? nullptr : resolutions[ resno - 1];
		auto band = res->bands + bandno;
		uint32_t x = offsetx;
		uint32_t y = offsety;

		// get code block offset relative to band
		x -= band->x0;
		y -= band->y0;

		// add band offset relative to previous resolution
		if (band->bandno & 1)
			x += pres->width();
		if (band->bandno & 2)
			y += pres->height();
		offsetx = x;
		offsety = y;

		return tile_buf()->data + (uint64_t) x + y * (uint64_t) tile_buf()->stride;
	}
	/**
	 * Get pointer to band buffer
	 *
	 * @param resno resolution number
	 * @param bandno band number {0,1,2,3} for LL HL,LH and HH bands
	 *
	 */
	T* ptr(uint32_t resno,uint32_t bandno){
		assert((resno > 0 && bandno <=4) || (resno==0 && bandno==0));
		if (bandno==0)
			return tile_buf()->data;
		auto lower_res = resolutions[resno-1];
		switch(bandno){
		case 1:
			return tile_buf()->data + lower_res->width();
			break;
		case 2:
			return tile_buf()->data + lower_res->height() * stride(resno,bandno);
			break;
		case 3:
			return tile_buf()->data + lower_res->width() +
					lower_res->height() * stride(resno,bandno);
			break;
		default:
			assert(0);
			break;
		}
		return nullptr;
	}
	/**
	 * Get pointer to tile buffer
	 *
	 *
	 */
	T* ptr(void){
		return tile_buf()->data;
	}
	/**
	 * Get stride of band buffer
	 *
	 * @param resno resolution number
	 * @param bandno band number {0,1,2,3} for LL HL,LH and HH bands
	 */
	uint32_t stride(uint32_t resno,uint32_t bandno){
		(void)resno;
		(void)bandno;
		return tile_buf()->stride;
	}

	uint32_t stride(void){
		return tile_buf()->stride;
	}


	bool alloc(){
		for (auto& b : res_buffers) {
			if (!b->alloc(!m_encode))
				return false;
		}
		return true;
	}

	/**
	 * Get reduced coordinates of sub-band region
	 *
	 * @param resno resolution number
	 * @param bandno band number {0,1,2,3} for LL HL,LH and HH bands
	 * @param tbx0	x0 coordinate of region
	 * @param tby0	y0 coordinate of region
	 * @param tbx1	x1 coordinate of region
	 * @param tby1	y1 coordinate of region
	 *
	 *
	 */
	void get_region_band_coordinates(uint32_t resno,
							uint32_t bandno,
							uint32_t* tbx0,
							uint32_t* tby0,
							uint32_t* tbx1,
							uint32_t* tby1){
	    /* Compute number of decomposition for this band. See table F-1 */
	    uint32_t nb = (resno == 0) ?
	                    num_resolutions - 1 :
	                    num_resolutions - resno;

		uint32_t tcx0 = (uint32_t)m_unreduced_bounds.x0;
		uint32_t tcy0 = (uint32_t)m_unreduced_bounds.y0;
		uint32_t tcx1 = (uint32_t)m_unreduced_bounds.x1;
		uint32_t tcy1 = (uint32_t)m_unreduced_bounds.y1;
	    /* Map above tile-based coordinates to sub-band-based coordinates per */
	    /* equation B-15 of the standard */
	    uint32_t x0b = bandno & 1;
	    uint32_t y0b = bandno >> 1;
	    if (tbx0) {
	        *tbx0 = (nb == 0) ? tcx0 :
	                (tcx0 <= (1U << (nb - 1)) * x0b) ? 0 :
	                ceildivpow2<uint32_t>(tcx0 - (1U << (nb - 1)) * x0b, nb);
	    }
	    if (tby0) {
	        *tby0 = (nb == 0) ? tcy0 :
	                (tcy0 <= (1U << (nb - 1)) * y0b) ? 0 :
	                ceildivpow2<uint32_t>(tcy0 - (1U << (nb - 1)) * y0b, nb);
	    }
	    if (tbx1) {
	        *tbx1 = (nb == 0) ? tcx1 :
	                (tcx1 <= (1U << (nb - 1)) * x0b) ? 0 :
	                ceildivpow2<uint32_t>(tcx1 - (1U << (nb - 1)) * x0b, nb);
	    }
	    if (tby1) {
	        *tby1 = (nb == 0) ? tcy1 :
	                (tcy1 <= (1U << (nb - 1)) * y0b) ? 0 :
	                ceildivpow2<uint32_t>(tcy1 - (1U << (nb - 1)) * y0b, nb);
	    }
	}

	/**
	 * Get bounds of subband
	 *
	 * @param resno resolution number
	 * @param bandno band number {0,1,2,3} for LL HL,LH and HH bands
	 */
	grk_rect_u32 bounds(uint32_t resno,uint32_t bandno){
		assert(resno < resolutions.size() && bandno < 4);
		if (bandno == 0){
			if (resno == 0)
				return resolutions[0]->bands[0];
			else
				return *resolutions[resno-1];
		}
		return resolutions[resno]->bands[bandno-1];
	}

	/**
	 * Get bounds of tile component
	 * decode: reduced tile component coordinates of region
	 * encode: unreduced tile component coordinates of entire tile
	 */
	grk_rect bounds(){
		return m_bounds;
	}

	grk_rect unreduced_bounds(){
		return m_unreduced_bounds;
	}

	uint64_t full_area(void){
		return stride() * m_bounds.height();
	}

	// set data to buf without owning it
	void attach(T* buffer,uint32_t stride){
		tile_buf()->attach(buffer,stride);
	}
	// set data to buf and own it
	void acquire(T* buffer, uint32_t stride){
		tile_buf()->acquire(buffer,stride);
	}
	// transfer data to buf, and cease owning it
	void transfer(T** buffer, bool* owns, uint32_t *stride){
		tile_buf()->transfer(buffer,owns,stride);
	}
private:

	grk_buffer_2d<T>* tile_buf() const{
		return res_buffers.back()->res;
	}

	grk_rect m_unreduced_bounds;

	/* decode: reduced tile component coordinates of region  */
	/* encode: unreduced tile component coordinates of entire tile */
	grk_rect m_bounds;

	std::vector<grk_resolution*> resolutions;
	std::vector<res_buf<T>* > res_buffers;
	uint32_t num_resolutions;

	bool m_encode;
	bool whole_tile_decoding;
};


}
