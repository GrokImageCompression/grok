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

TileComponent::TileComponent() :numresolutions(0),
								numAllocatedResolutions(0),
								resolutions_to_decompress(0),
								resolutions(nullptr),
						#ifdef DEBUG_LOSSLESS_T2
								round_trip_resolutions(nullptr),
						#endif
							   numpix(0),
							   buf(nullptr),
							   whole_tile_decoding(true),
							   m_is_encoder(false),
							   m_sa(nullptr),
							   m_tccp(nullptr)
{}

TileComponent::~TileComponent(){
	release_mem();
	delete buf;
}
void TileComponent::release_mem(){
	if (resolutions) {
		auto nb_resolutions = numAllocatedResolutions;
		for (uint32_t resno = 0; resno < nb_resolutions; ++resno) {
			auto res = resolutions + resno;
			for (uint32_t bandno = 0; bandno < 3; ++bandno) {
				auto band = res->bands + bandno;
				for (uint64_t precno = 0; precno < band->numAllocatedPrecincts;
						++precno) {
					auto precinct = band->precincts + precno;
					precinct->deleteTagTrees();
					if (m_is_encoder)
						delete[] precinct->enc;
					else
						delete[] precinct->dec;
				}
				delete[] band->precincts;
				band->precincts = nullptr;
			} /* for (resno */
		}
		delete[] resolutions;
		resolutions = nullptr;
	}
	delete m_sa;
	m_sa = nullptr;
}
bool TileComponent::init(bool isEncoder,
						bool whole_tile,
						grk_image *output_image,
						CodingParams *cp,
						TileCodingParams *tcp,
						grk_tile* tile,
						grk_image_comp* image_comp,
						TileComponentCodingParams* tccp,
						grk_plugin_tile *current_plugin_tile){
	uint32_t state = grk_plugin_get_debug_state();
	m_is_encoder = isEncoder;
	whole_tile_decoding = whole_tile;
	m_tccp = tccp;

	/* extent of precincts , top left, bottom right**/
	/* number of code blocks for a precinct*/
	uint64_t nb_code_blocks, cblkno;
	uint32_t leveno;
	uint32_t x0b, y0b;

	/* border of each tile component in tile component coordinates */
	auto x0 = ceildiv<uint32_t>(tile->x0, image_comp->dx);
	auto y0 = ceildiv<uint32_t>(tile->y0, image_comp->dy);
	auto x1 = ceildiv<uint32_t>(tile->x1, image_comp->dx);
	auto y1 = ceildiv<uint32_t>(tile->y1, image_comp->dy);
	/*fprintf(stderr, "\tTile compo border = %u,%u,%u,%u\n", x0, y0,x1,y1);*/

	numresolutions = m_tccp->numresolutions;
	if (numresolutions < cp->m_coding_params.m_dec.m_reduce) {
		resolutions_to_decompress = 1;
	} else {
		resolutions_to_decompress = numresolutions
				- cp->m_coding_params.m_dec.m_reduce;
	}
	if (!resolutions) {
		resolutions = new grk_resolution[numresolutions];
		numAllocatedResolutions = numresolutions;
	} else if (numresolutions > numAllocatedResolutions) {
		auto new_resolutions =
				new grk_resolution[numresolutions];
		for (uint32_t i = 0; i < numresolutions; ++i)
			new_resolutions[i] = resolutions[i];
		delete[] resolutions;
		resolutions = new_resolutions;
		numAllocatedResolutions = numresolutions;
	}
	leveno = numresolutions;
	/*fprintf(stderr, "\tleveno=%u\n",leveno);*/

	for (uint32_t resno = 0; resno < numresolutions; ++resno) {
		auto res = resolutions + resno;
		/*fprintf(stderr, "\t\tresno = %u/%u\n", resno, numresolutions);*/
		uint32_t tlcbgxstart, tlcbgystart;
		uint32_t cbgwidthexpn, cbgheightexpn;
		uint32_t cblkwidthexpn, cblkheightexpn;

		--leveno;

		/* border for each resolution level (global) */
		res->x0 = ceildivpow2<uint32_t>(x0, leveno);
		res->y0 = ceildivpow2<uint32_t>(y0, leveno);
		res->x1 = ceildivpow2<uint32_t>(x1, leveno);
		res->y1 = ceildivpow2<uint32_t>(y1, leveno);
		/*fprintf(stderr, "\t\t\tres_x0= %u, res_y0 =%u, res_x1=%u, res_y1=%u\n", res->x0, res->y0, res->x1, res->y1);*/
		/* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
		uint32_t pdx = m_tccp->prcw[resno];
		uint32_t pdy = m_tccp->prch[resno];
		/*fprintf(stderr, "\t\t\tpdx=%u, pdy=%u\n", pdx, pdy);*/
		/* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
		uint32_t tprc_x_start = uint_floordivpow2(res->x0, pdx) << pdx;
		uint32_t tprc_y_start = uint_floordivpow2(res->y0, pdy) << pdy;
		uint64_t temp = (uint64_t)ceildivpow2<uint32_t>(res->x1, pdx) << pdx;
		if (temp > UINT_MAX){
			GRK_ERROR("Resolution x1 value %u must be less than 2^32", temp);
			return false;
		}
		uint32_t br_prc_x_end = (uint32_t)temp;
		temp = (uint64_t)ceildivpow2<uint32_t>(res->y1, pdy) << pdy;
		if (temp > UINT_MAX){
			GRK_ERROR("Resolution y1 value %u must be less than 2^32", temp);
			return false;
		}
		uint32_t br_prc_y_end = (uint32_t)temp;

		/*fprintf(stderr, "\t\t\tprc_x_start=%u, prc_y_start=%u, br_prc_x_end=%u, br_prc_y_end=%u \n", tprc_x_start, tprc_y_start, br_prc_x_end ,br_prc_y_end );*/

		res->pw =
				(res->x0 == res->x1) ?
						0 : ((br_prc_x_end - tprc_x_start) >> pdx);
		res->ph =
				(res->y0 == res->y1) ?
						0 : ((br_prc_y_end - tprc_y_start) >> pdy);
		/*fprintf(stderr, "\t\t\tres_pw=%u, res_ph=%u\n", res->pw, res->ph );*/

		if (mult_will_overflow(res->pw, res->ph)) {
			GRK_ERROR(
					"nb_precincts calculation would overflow ");
			return false;
		}
		/* number of precinct for a resolution */
		uint64_t nb_precincts = (uint64_t)res->pw * res->ph;

		if (mult64_will_overflow(nb_precincts, sizeof(grk_precinct))) {
			GRK_ERROR(	"nb_precinct_size calculation would overflow ");
			return false;
		}
		if (resno == 0) {
			tlcbgxstart = tprc_x_start;
			tlcbgystart = tprc_y_start;
			cbgwidthexpn = pdx;
			cbgheightexpn = pdy;
			res->numbands = 1;
		} else {
			tlcbgxstart = ceildivpow2<uint32_t>(tprc_x_start, 1);
			tlcbgystart = ceildivpow2<uint32_t>(tprc_y_start, 1);
			cbgwidthexpn = pdx - 1;
			cbgheightexpn = pdy - 1;
			res->numbands = 3;
		}

		cblkwidthexpn = std::min<uint32_t>(tccp->cblkw, cbgwidthexpn);
		cblkheightexpn = std::min<uint32_t>(tccp->cblkh, cbgheightexpn);
		size_t nominalBlockSize = (1 << cblkwidthexpn)
				* (1 << cblkheightexpn);

		for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
			auto band = res->bands + bandno;

			/*fprintf(stderr, "\t\t\tband_no=%u/%u\n", bandno, res->numbands );*/

			if (resno == 0) {
				band->bandno = 0;
				band->x0 = ceildivpow2<uint32_t>(x0, leveno);
				band->y0 = ceildivpow2<uint32_t>(y0, leveno);
				band->x1 = ceildivpow2<uint32_t>(x1, leveno);
				band->y1 = ceildivpow2<uint32_t>(y1, leveno);
			} else {
				band->bandno = (uint8_t)(bandno + 1);
				/* x0b = 1 if bandno = 1 or 3 */
				x0b = band->bandno & 1;
				/* y0b = 1 if bandno = 2 or 3 */
				y0b = (uint32_t) ((band->bandno) >> 1);
				/* band border (global) */
				band->x0 = uint64_ceildivpow2(
						x0 - ((uint64_t) x0b << leveno),
						leveno + 1);
				band->y0 = uint64_ceildivpow2(
						y0 - ((uint64_t) y0b << leveno),
						leveno + 1);
				band->x1 = uint64_ceildivpow2(
						x1 - ((uint64_t) x0b << leveno),
						leveno + 1);
				band->y1 = uint64_ceildivpow2(
						y1 - ((uint64_t) y0b << leveno),
						leveno + 1);
			}

			tccp->quant.setBandStepSizeAndBps(tcp,
												band,
												resno,
												(uint8_t)bandno,
												tccp,
												image_comp->prec,
												m_is_encoder);

			if (!band->precincts && (nb_precincts > 0U)) {
				band->precincts = new grk_precinct[nb_precincts];
				band->numAllocatedPrecincts = nb_precincts;
			} else if (band->numAllocatedPrecincts < nb_precincts) {
				auto new_precincts = new grk_precinct[nb_precincts];
				for (size_t i = 0; i < band->numAllocatedPrecincts; ++i)
					new_precincts[i] = band->precincts[i];
				delete[] band->precincts;
				band->precincts = new_precincts;
				band->numAllocatedPrecincts = nb_precincts;
			}
			band->numPrecincts = nb_precincts;
			for (uint64_t precno = 0; precno < nb_precincts; ++precno) {
				auto current_precinct = band->precincts + precno;
				uint32_t tlcblkxstart, tlcblkystart, brcblkxend, brcblkyend;
				uint32_t cbgxstart = tlcbgxstart
						+ (uint32_t)(precno % res->pw) * (1 << cbgwidthexpn);
				uint32_t cbgystart = tlcbgystart
						+ (uint32_t)(precno / res->pw) * (1 << cbgheightexpn);
				uint32_t cbgxend = cbgxstart + (1 << cbgwidthexpn);
				uint32_t cbgyend = cbgystart + (1 << cbgheightexpn);
				/*fprintf(stderr, "\t precno=%u; bandno=%u, resno=%u; compno=%u\n", precno, bandno , resno, compno);*/
				/*fprintf(stderr, "\t tlcbgxstart(=%u) + (precno(=%u) percent res->pw(=%u)) * (1 << cbgwidthexpn(=%u)) \n",tlcbgxstart,precno,res->pw,cbgwidthexpn);*/

				/* precinct size (global) */
				/*fprintf(stderr, "\t cbgxstart=%u, band->x0 = %u \n",cbgxstart, band->x0);*/

				current_precinct->x0 = std::max<uint32_t>(cbgxstart,
						band->x0);
				current_precinct->y0 = std::max<uint32_t>(cbgystart,
						band->y0);
				current_precinct->x1 = std::min<uint32_t>(cbgxend,
						band->x1);
				current_precinct->y1 = std::min<uint32_t>(cbgyend,
						band->y1);
				/*fprintf(stderr, "\t prc_x0=%u; prc_y0=%u, prc_x1=%u; prc_y1=%u\n",current_precinct->x0, current_precinct->y0 ,current_precinct->x1, current_precinct->y1);*/

				tlcblkxstart = uint_floordivpow2(current_precinct->x0,
						cblkwidthexpn) << cblkwidthexpn;
				/*fprintf(stderr, "\t tlcblkxstart =%u\n",tlcblkxstart );*/
				tlcblkystart = uint_floordivpow2(current_precinct->y0,
						cblkheightexpn) << cblkheightexpn;
				/*fprintf(stderr, "\t tlcblkystart =%u\n",tlcblkystart );*/
				brcblkxend = ceildivpow2<uint32_t>(current_precinct->x1,
						cblkwidthexpn) << cblkwidthexpn;
				/*fprintf(stderr, "\t brcblkxend =%u\n",brcblkxend );*/
				brcblkyend = ceildivpow2<uint32_t>(current_precinct->y1,
						cblkheightexpn) << cblkheightexpn;
				/*fprintf(stderr, "\t brcblkyend =%u\n",brcblkyend );*/
				current_precinct->cw = ((brcblkxend - tlcblkxstart)
						>> cblkwidthexpn);
				current_precinct->ch = ((brcblkyend - tlcblkystart)
						>> cblkheightexpn);

				nb_code_blocks = (uint64_t) current_precinct->cw
						* current_precinct->ch;
				/*fprintf(stderr, "\t\t\t\t precinct_cw = %u x recinct_ch = %u\n",current_precinct->cw, current_precinct->ch);      */
				if (nb_code_blocks > 0) {
					if (isEncoder){
						if (!current_precinct->enc){
							current_precinct->enc = new grk_cblk_enc[nb_code_blocks];
						} else if (nb_code_blocks > current_precinct->num_code_blocks){
							auto new_blocks = new grk_cblk_enc[nb_code_blocks];
							for (uint64_t i = 0; i < current_precinct->num_code_blocks; ++i){
								new_blocks[i] = current_precinct->enc[i];
								current_precinct->enc[i].clear();
							}
							delete[] current_precinct->enc;
							current_precinct->enc = new_blocks;
						}
					} else {
						if (!current_precinct->dec){
							current_precinct->dec = new grk_cblk_dec[nb_code_blocks];
						} else if (nb_code_blocks > current_precinct->num_code_blocks){
							auto new_blocks = new grk_cblk_dec[nb_code_blocks];
							for (uint64_t i = 0; i < current_precinct->num_code_blocks; ++i){
								new_blocks[i] = current_precinct->dec[i];
								current_precinct->dec[i].clear();
							}
							delete[] current_precinct->dec;
							current_precinct->dec = new_blocks;
						}
					}
				    current_precinct->num_code_blocks = nb_code_blocks;
				}
				current_precinct->initTagTrees();

				for (cblkno = 0; cblkno < nb_code_blocks; ++cblkno) {
					uint32_t cblkxstart = tlcblkxstart
							+ (uint32_t) (cblkno % current_precinct->cw)
									* (1 << cblkwidthexpn);
					uint32_t cblkystart = tlcblkystart
							+ (uint32_t) (cblkno / current_precinct->cw)
									* (1 << cblkheightexpn);
					uint32_t cblkxend = cblkxstart + (1 << cblkwidthexpn);
					uint32_t cblkyend = cblkystart + (1 << cblkheightexpn);

					if (m_is_encoder) {
						auto code_block = current_precinct->enc + cblkno;

						if (!code_block->alloc())
							return false;
						/* code-block size (global) */
						code_block->x0 = std::max<uint32_t>(cblkxstart,
								current_precinct->x0);
						code_block->y0 = std::max<uint32_t>(cblkystart,
								current_precinct->y0);
						code_block->x1 = std::min<uint32_t>(cblkxend,
								current_precinct->x1);
						code_block->y1 = std::min<uint32_t>(cblkyend,
								current_precinct->y1);

						if (!current_plugin_tile
								|| (state & GRK_PLUGIN_STATE_DEBUG)) {
							if (!code_block->alloc_data(
									nominalBlockSize))
								return false;
						}
					} else {
						auto code_block =
								current_precinct->dec + cblkno;
						if (!current_plugin_tile
								|| (state & GRK_PLUGIN_STATE_DEBUG)) {
							if (!code_block->alloc())
								return false;
						}

						/* code-block size (global) */
						code_block->x0 = std::max<uint32_t>(cblkxstart,
								current_precinct->x0);
						code_block->y0 = std::max<uint32_t>(cblkystart,
								current_precinct->y0);
						code_block->x1 = std::min<uint32_t>(cblkxend,
								current_precinct->x1);
						code_block->y1 = std::min<uint32_t>(cblkyend,
								current_precinct->y1);
					}
				}
			} /* precno */
		} /* bandno */
	} /* resno */
	create_buffer(output_image,
					image_comp->dx,
					image_comp->dy);

	return true;
}


bool TileComponent::is_subband_area_of_interest(uint32_t resno,
								uint32_t bandno,
								uint32_t aoi_x0,
								uint32_t aoi_y0,
								uint32_t aoi_x1,
								uint32_t aoi_y1) const
{
	if (whole_tile_decoding)
		return true;

    /* Note: those values for filter_margin are in part the result of */
    /* experimentation. The value 2 for QMFBID=1 (5x3 filter) can be linked */
    /* to the maximum left/right extension given in tables F.2 and F.3 of the */
    /* standard. The value 3 for QMFBID=0 (9x7 filter) is more suspicious, */
    /* since F.2 and F.3 would lead to 4 instead, so the current 3 might be */
    /* needed to be bumped to 4, in case inconsistencies are found while */
    /* decoding parts of irreversible coded images. */
    /* See dwt_decode_partial_53 and dwt_decode_partial_97 as well */
    uint32_t filter_margin = (m_tccp->qmfbid == 1) ? 2 : 3;

    /* Compute the intersection of the area of interest, expressed in tile component coordinates */
    /* with the tile coordinates */
	auto dims = buf->unreduced_bounds();
	uint32_t tcx0 = (uint32_t)dims.x0;
	uint32_t tcy0 = (uint32_t)dims.y0;
	uint32_t tcx1 = (uint32_t)dims.x1;
	uint32_t tcy1 = (uint32_t)dims.y1;

    /* Compute number of decomposition for this band. See table F-1 */
    uint32_t nb = (resno == 0) ?
                    numresolutions - 1 :
                    numresolutions - resno;
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

    if (tbx0 < filter_margin)
        tbx0 = 0;
    else
        tbx0 -= filter_margin;
    if (tby0 < filter_margin)
        tby0 = 0;
    else
        tby0 -= filter_margin;

    tbx1 = uint_adds(tbx1, filter_margin);
    tby1 = uint_adds(tby1, filter_margin);

    bool intersects = aoi_x0 < tbx1 && aoi_y0 < tby1 && aoi_x1 > tbx0 &&
                 aoi_y1 > tby0;

#ifdef DEBUG_VERBOSE
    printf("compno=%u resno=%u nb=%u bandno=%u x0b=%u y0b=%u band=%u,%u,%u,%u tb=%u,%u,%u,%u -> %u\n",
           compno, resno, nb, bandno, x0b, y0b,
           aoi_x0, aoi_y0, aoi_x1, aoi_y1,
           tbx0, tby0, tbx1, tby1, intersects);
#endif
    return intersects;
}


void TileComponent::alloc_sparse_array(uint32_t numres){
    auto tr_max = &(resolutions[numres - 1]);
	uint32_t w = (uint32_t)(tr_max->x1 - tr_max->x0);
	uint32_t h = (uint32_t)(tr_max->y1 - tr_max->y0);
	auto sa = new sparse_array(w, h, min<uint32_t>(w, 64), min<uint32_t>(h, 64));
    for (uint32_t resno = 0; resno < numres; ++resno) {
        auto res = &resolutions[resno];

        for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
            auto band = &res->bands[bandno];

            for (uint64_t precno = 0; precno < (uint64_t)res->pw * res->ph; ++precno) {
                auto precinct = &band->precincts[precno];

                for (uint64_t cblkno = 0; cblkno < (uint64_t)precinct->cw * precinct->ch; ++cblkno) {
                    auto cblk = &precinct->dec[cblkno];
					uint32_t x = cblk->x0;
					uint32_t y = cblk->y0;
					uint32_t cblk_w = cblk->width();
					uint32_t cblk_h = cblk->height();

					// check overlap in absolute coordinates
					if (is_subband_area_of_interest(resno,
							bandno,
							x,
							y,
							x+cblk_w,
							y+cblk_h)){

						x -= band->x0;
						y -= band->y0;

						/* add band offset relative to previous resolution */
						if (band->bandno & 1) {
							grk_resolution *pres = &resolutions[resno - 1];
							x += pres->x1 - pres->x0;
						}
						if (band->bandno & 2) {
							grk_resolution *pres = &resolutions[resno - 1];
							y += pres->y1 - pres->y0;
						}

						// allocate in relative coordinates
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


void TileComponent::create_buffer(grk_image *output_image,
									uint32_t dx,
									uint32_t dy) {

	auto highestRes =
			(!m_is_encoder) ? resolutions_to_decompress : numresolutions;
	auto res =  resolutions + highestRes - 1;
	grk_rect_u32::operator=(*(grk_rect_u32*)res);
	auto maxRes = resolutions + numresolutions - 1;

	delete buf;
	buf = new TileComponentBuffer<int32_t>(output_image, dx,dy,
											grk_rect(maxRes->x0, maxRes->y0, maxRes->x1, maxRes->y1),
											grk_rect(x0, y0, x1, y1),
											highestRes,
											numresolutions,
											resolutions,
											whole_tile_decoding);
}

}

