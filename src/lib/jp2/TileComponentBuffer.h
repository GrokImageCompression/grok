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
							reduced_region_dim(reduced_dim),
							unreduced_tile_comp_dim(unreduced_dim),
							m_encode(output_image==nullptr)
	{
		//note: only decoder has output image
		if (output_image) {
			// tile component coordinates
			unreduced_region_dim = grk_rect(ceildiv<uint32_t>(output_image->x0, dx),
										ceildiv<uint32_t>(output_image->y0, dy),
										ceildiv<uint32_t>(output_image->x1, dx),
										ceildiv<uint32_t>(output_image->y1, dy));

			reduced_region_dim 	= unreduced_region_dim;
			reduced_region_dim.ceildivpow2(numresolutions - reduced_num_resolutions);

			/* clip region dimensions against tile */
			reduced_dim.clip(reduced_region_dim, &reduced_region_dim);
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
		return buf.buf + (uint64_t) offsetx
				+ offsety * (uint64_t) (reduced_region_dim.x1 - reduced_region_dim.x0);
	}
	/**
	 * Get pointer to band buffer
	 *
	 * @param resno resolution number
	 * @param bandno band number (0 for LL band of 0th resolution, otherwise
	 * 0,1,2 for HL,LH,HH bands
	 *
	 */
	T* ptr(uint32_t resno,uint32_t bandno){
		if (resno==0)
			return buf.buf;
		auto lower_res = resolutions[resno-1];
		switch(bandno){
		case 0:
			return buf.buf + lower_res->width();
			break;
		case 1:
			return buf.buf + lower_res->height() * stride(resno,bandno);
			break;
		case 2:
			return buf.buf + lower_res->width() +
					lower_res->height() * stride(resno,bandno);
			break;
		default:
			assert(0);
			break;
		}
		return nullptr;
	}
	/**
	 * Get pointer to resolution buffer (LL band of next resolution)
	 *
	 * @param resno resolution number
	 *
	 */
	T* ptr(uint32_t resno){
		return ptr(resno, 0, 0,0);
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
	 * @param bandno band number (0 for LL band of 0th resolution, otherwise
	 * 0,1,2 for HL,LH,HH bands
	 */
	uint32_t stride(uint32_t resno,uint32_t bandno){
		(void)resno;
		(void)bandno;
		return buf.stride;
	}
	/**
	 * Get stride of resolution buffer (LL band of next resolution)
	 *
	 * @resno resolution number
	 */
	uint32_t stride(uint32_t resno){
		(void)resno;
		return buf.stride;
	}
	/**
	 * Get stride of tile buffer (highest resolution)
	 */
	uint32_t stride(void){
		return buf.stride;
	}
	bool alloc(){
		return buf.alloc(!m_encode);
	}

	grk_rect bounds(){
		return m_encode ? unreduced_tile_comp_dim : reduced_region_dim;
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
	void transfer(T** buffer, bool* owns){
		buf.transfer(buffer,owns);
	}
	// unreduced tile component coordinates of region
	grk_rect unreduced_region_dim;

	 /* reduced tile component coordinates of region  */
	grk_rect reduced_region_dim;

private:
	/* unreduced tile component coordinates of tile */
	grk_rect unreduced_tile_comp_dim;

	std::vector<grk_resolution*> resolutions;

	grk_buffer_2d<T> buf;
	bool m_encode;
};


}
