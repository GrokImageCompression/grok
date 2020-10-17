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
#include "grk_includes.h"

namespace grk {

grk_rect_u32 grk_get_region_band_coordinates(uint32_t num_res, uint32_t resno, uint32_t bandno, grk_rect_u32 unreduced_region){
    /* Compute number of decomposition for this band. See table F-1 */
    uint32_t nb = (resno == 0) ? num_res - 1 :num_res - resno;

    uint32_t tcx0 = unreduced_region.x0;
	uint32_t tcy0 = unreduced_region.y0;
	uint32_t tcx1 = unreduced_region.x1;
	uint32_t tcy1 = unreduced_region.y1;
    /* Map above tile-based coordinates to sub-band-based coordinates per */
    /* equation B-15 of the standard */
    uint32_t x0b = bandno & 1;
    uint32_t y0b = bandno >> 1;
	uint32_t tbx0 = (nb == 0) ? tcx0 :
			(tcx0 <= (1U << (nb - 1)) * x0b) ? 0 :
			ceildivpow2<uint32_t>(tcx0 - (1U << (nb - 1)) * x0b, nb);

	uint32_t tby0 = (nb == 0) ? tcy0 :
			(tcy0 <= (1U << (nb - 1)) * y0b) ? 0 :
			ceildivpow2<uint32_t>(tcy0 - (1U << (nb - 1)) * y0b, nb);

	uint32_t tbx1 = (nb == 0) ? tcx1 :
			(tcx1 <= (1U << (nb - 1)) * x0b) ? 0 :
			ceildivpow2<uint32_t>(tcx1 - (1U << (nb - 1)) * x0b, nb);

	uint32_t tby1 = (nb == 0) ? tcy1 :
			(tcy1 <= (1U << (nb - 1)) * y0b) ? 0 :
			ceildivpow2<uint32_t>(tcy1 - (1U << (nb - 1)) * y0b, nb);


	return grk_rect_u32(tbx0,tby0,tbx1,tby1);
}


}
