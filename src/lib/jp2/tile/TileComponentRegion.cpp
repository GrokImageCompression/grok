/*
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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */


#include "grk_includes.h"

namespace grk {


TileComponentRegion::TileComponentRegion(Resolution *res,
										uint32_t numres,
										TileComponentCodingParams *tccp) : buf(nullptr),
																		   m_sa(nullptr),
																		   resolutions(res),
																		   numresolutions(numres),
																		   m_tccp(tccp)
{

}

TileComponentRegion::~TileComponentRegion() {
	release_mem();
	delete buf;
}

void TileComponentRegion::release_mem(){
	delete m_sa;
	m_sa = nullptr;
}

bool TileComponentRegion::subbandIntersectsAOI(uint32_t resno,
								uint32_t bandno,
								const grk_rect_u32 *aoi) const
{
    /* Note: those values for filter_margin are in part the result of */
    /* experimentation. The value 2 for QMFBID=1 (5x3 filter) can be linked */
    /* to the maximum left/right extension given in tables F.2 and F.3 of the */
    /* standard. The value 3 for QMFBID=0 (9x7 filter) is more suspicious, */
    /* since F.2 and F.3 would lead to 4 instead, so the current 3 might be */
    /* needed to be bumped to 4, in case inconsistencies are found while */
    /* decoding parts of irreversible coded images. */
    /* See dwt_decode_partial_53 and dwt_decode_partial_97 as well */
	//note: bumped up lossy filter margin to 4
    uint32_t filter_margin = (m_tccp->qmfbid == 1) ? 2 : 4;

    /* Compute the intersection of the area of interest, expressed in tile component coordinates */
    /* Map above tile-based coordinates to sub-band-based coordinates following equation B-15 of the standard */

    auto b = resolutions[resno].bands[bandno];
    b.grow(filter_margin,filter_margin);


#ifdef DEBUG_VERBOSE
    printf("compno=%u resno=%u nb=%u bandno=%u x0b=%u y0b=%u band=%u,%u,%u,%u tb=%u,%u,%u,%u -> %u\n",
           compno, resno, num_decomps, bandno, x0b, y0b,
           aoi_x0, aoi_y0, aoi_x1, aoi_y1,
           tbx0, tby0, tbx1, tby1, intersects);
#endif
    return b.intersection(*aoi).is_non_degenerate();
}


void TileComponentRegion::allocSparseBuffer(Resolution *resolutions, uint32_t numres){
    auto tr_max = resolutions + numres - 1;
	uint32_t w = tr_max->width();
	uint32_t h = tr_max->height();
	auto sa = new SparseBuffer<6,6>(w, h);

    for (uint32_t resno = 0; resno < numres; ++resno) {
        auto res = &resolutions[resno];
        for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
            auto band = res->bands + bandno;
            for (uint64_t precno = 0; precno < (uint64_t)res->pw * res->ph; ++precno) {
                auto precinct = band->precincts + precno;
                for (uint64_t cblkno = 0; cblkno < (uint64_t)precinct->cw * precinct->ch; ++cblkno) {
                    auto cblk = precinct->dec + cblkno;

					uint32_t x = cblk->x0;
					uint32_t y = cblk->y0;
					uint32_t cblk_w = cblk->width();
					uint32_t cblk_h = cblk->height();

					grk_rect_u32 cblk_roi = grk_rect_u32(x,y,x+cblk_w,y+cblk_h);
					// check overlap in absolute coordinates
					if (subbandIntersectsAOI(resno,
													bandno,
													&cblk_roi)){

						// switch from coordinates relative to band,
						// to coordinates relative to current resolution
						x -= band->x0;
						y -= band->y0;

						/* add band offset relative to previous resolution */
						if (band->bandno & 1) {
							auto prev_res = resolutions + resno - 1;
							x += prev_res->x1 - prev_res->x0;
						}
						if (band->bandno & 2) {
							auto prev_res = resolutions + resno - 1;
							y += prev_res->y1 - prev_res->y0;
						}

						if (!sa->alloc(x,
									  y,
									  x + cblk_w,
									  y + cblk_h)) {
							delete sa;
							throw runtime_error("unable to allocate sparse array");
						}
					}
                }
            }
        }
    }
    if (m_sa)
    	delete m_sa;
    m_sa = sa;
}


void TileComponentRegion::create_buffer(grk_image *output_image,
									uint32_t dx,
									uint32_t dy) {
/*
	auto highestRes =
			(!m_is_encoder) ? resolutions_to_decompress : numresolutions;
	auto res =  resolutions + highestRes - 1;
	grk_rect_u32::operator=(*(grk_rect_u32*)res);
	auto maxRes = resolutions + numresolutions - 1;

	delete buf;
	buf = new TileComponentBuffer<int32_t>(output_image, dx,dy,
											grk_rect_u32(maxRes->x0, maxRes->y0, maxRes->x1, maxRes->y1),
											grk_rect_u32(x0, y0, x1, y1),
											highestRes,
											numresolutions,
											resolutions,
											whole_tile_decoding);
											*/
}


TileComponentBuffer<int32_t>* TileComponentRegion::getBuffer(){
	return buf;
}

ISparseBuffer* TileComponentRegion::getSparseBuffer(){
	return m_sa;
}

}

