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
						grk_resolution *tile_comp_resolutions) :
							unreduced_region_dim(unreduced_dim),
							m_bounds(reduced_dim),
							unreduced_tile_comp_dim(unreduced_dim),
							num_resolutions(numresolutions),
							m_encode(output_image==nullptr)
	{
		//note: only decoder has output image
		if (output_image) {
			// tile component coordinates
			unreduced_region_dim = grk_rect(ceildiv<uint32_t>(output_image->x0, dx),
										ceildiv<uint32_t>(output_image->y0, dy),
										ceildiv<uint32_t>(output_image->x1, dx),
										ceildiv<uint32_t>(output_image->y1, dy));

			m_bounds 	= unreduced_region_dim;
			m_bounds.ceildivpow2(num_resolutions - reduced_num_resolutions);

			/* clip region dimensions against tile */
			reduced_dim.clip(m_bounds, &m_bounds);
			unreduced_tile_comp_dim.clip(unreduced_region_dim, &unreduced_region_dim);

			/* fill resolutions vector */
	        assert(reduced_num_resolutions>0);

	        for (uint32_t resno = 0; resno < reduced_num_resolutions; ++resno)
	        	resolutions.push_back(tile_comp_resolutions+resno);
		}
		grk_rect b = bounds();
		buf = grk_buffer_2d<T>((uint32_t)b.width(), (uint32_t)b.height());
	}
	~TileComponentBuffer(){
	}
	/**
	 * Get pointer to band buffer
	 *
	 * @param resno resolution number
	 * @param bandno band number (0 for LL band of 0th resolution, otherwise
	 * 0,1,2 for HL,LH,HH bands
	 * @param offsetx x offset into buffer
	 * @param offsety y offset into buffer
	 *
	 */
	T* ptr(uint32_t resno,uint32_t bandno, uint32_t offsetx, uint32_t offsety) const {
		if (resno==0)
			assert(bandno==0);
		else
			assert(bandno < 3);
		return buf.data + (uint64_t) offsetx
				+ offsety * (uint64_t) (m_bounds.x1 - m_bounds.x0);
	}
	/**
	 * Get pointer to band buffer
	 *
	 * @param resno resolution number
	 * @param bandno band number {0,1,2,3} for LL HL,LH and HH bands
	 *
	 */
	T* ptr(uint32_t resno,uint32_t bandno){
		if (bandno==0)
			return buf.data;
		auto lower_res = resolutions[resno-1];
		switch(bandno){
		case 1:
			return buf.data + lower_res->width();
			break;
		case 2:
			return buf.data + lower_res->height() * stride(resno,bandno);
			break;
		case 3:
			return buf.data + lower_res->width() +
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
		return ptr(0, 0, 0,0);
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
		return buf.stride;
	}

	bool alloc(){
		return buf.alloc(!m_encode);
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

		uint32_t tcx0 = (uint32_t)unreduced_region_dim.x0;
		uint32_t tcy0 = (uint32_t)unreduced_region_dim.y0;
		uint32_t tcx1 = (uint32_t)unreduced_region_dim.x1;
		uint32_t tcy1 = (uint32_t)unreduced_region_dim.y1;
	    /* Map above tile-based coordinates to sub-band-based coordinates per */
	    /* equation B-15 of the standard */
	    uint32_t x0b = bandno & 1;
	    uint32_t y0b = bandno >> 1;
	    if (tbx0) {
	        *tbx0 = (nb == 0) ? tcx0 :
	                (tcx0 <= (1U << (nb - 1)) * x0b) ? 0 :
	                uint_ceildivpow2(tcx0 - (1U << (nb - 1)) * x0b, nb);
	    }
	    if (tby0) {
	        *tby0 = (nb == 0) ? tcy0 :
	                (tcy0 <= (1U << (nb - 1)) * y0b) ? 0 :
	                uint_ceildivpow2(tcy0 - (1U << (nb - 1)) * y0b, nb);
	    }
	    if (tbx1) {
	        *tbx1 = (nb == 0) ? tcx1 :
	                (tcx1 <= (1U << (nb - 1)) * x0b) ? 0 :
	                uint_ceildivpow2(tcx1 - (1U << (nb - 1)) * x0b, nb);
	    }
	    if (tby1) {
	        *tby1 = (nb == 0) ? tcy1 :
	                (tcy1 <= (1U << (nb - 1)) * y0b) ? 0 :
	                uint_ceildivpow2(tcy1 - (1U << (nb - 1)) * y0b, nb);
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
	// set data to buf without owning it
	void attach(T* buffer){
		buf.attach(buffer);
	}
	// set data to buf and own it
	void acquire(T* buffer){
		buf.acquire(buffer);
	}
	// transfer data to buf, and cease owning it
	void transfer(T** buffer, bool* owns, uint32_t *stride){
		buf.transfer(buffer,owns);
		*stride = buf.stride;
	}

	grk_rect unreduced_region_dim;

private:
	/* decode: reduced tile component coordinates of region  */
	/* encode: unreduced tile component coordinates of entire tile */
	grk_rect m_bounds;

	/* unreduced tile component coordinates of entire tile */
	grk_rect unreduced_tile_comp_dim;

	std::vector<grk_resolution*> resolutions;

	uint32_t num_resolutions;

	grk_buffer_2d<T> buf;
	bool m_encode;
};


}
