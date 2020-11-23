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

const bool DEBUG_TILE_COMPONENT = false;

namespace grk {

TileComponent::TileComponent() :resolutions(nullptr),
								numresolutions(0),
								resolutions_to_decompress(0),
								resolutions_decompressed(0),
						#ifdef DEBUG_LOSSLESS_T2
								round_trip_resolutions(nullptr),
						#endif
							   m_sa(nullptr),
							   whole_tile_decoding(true),
							   m_is_encoder(false),
							   buf(nullptr),
							   m_tccp(nullptr)
{}

TileComponent::~TileComponent(){
	release_mem();
	delete buf;
}
void TileComponent::release_mem(){
	if (resolutions) {
		for (uint32_t resno = 0; resno < numresolutions; ++resno) {
			auto res = resolutions + resno;
			for (uint32_t bandno = 0; bandno < 3; ++bandno) {
				auto band = res->bandWindow + bandno;
				delete[] band->precincts;
				band->precincts = nullptr;
			}
		}
		delete[] resolutions;
		resolutions = nullptr;
	}
	delete m_sa;
	m_sa = nullptr;
}
/**
 * Initialize tile component in unreduced tile component coordinates
 * (tile component coordinates take sub-sampling into account).
 *
 */
bool TileComponent::init(bool isCompressor,
						bool whole_tile,
						grk_rect_u32 unreduced_tile_comp_dims,
						grk_rect_u32 unreduced_tile_comp_region_dims,
						uint8_t prec,
						CodingParams *cp,
						TileCodingParams *tcp,
						TileComponentCodingParams* tccp,
						grk_plugin_tile *current_plugin_tile){
	uint32_t state = grk_plugin_get_debug_state();
	m_is_encoder = isCompressor;
	whole_tile_decoding = whole_tile;
	m_tccp = tccp;

	// 1. calculate resolutions
	numresolutions = m_tccp->numresolutions;
	if (numresolutions < cp->m_coding_params.m_dec.m_reduce) {
		resolutions_to_decompress = 1;
	} else {
		resolutions_to_decompress = numresolutions
				- cp->m_coding_params.m_dec.m_reduce;
	}
	resolutions = new Resolution[numresolutions];
	for (uint32_t resno = 0; resno < numresolutions; ++resno) {
		auto res = resolutions + resno;
		uint32_t levelno = numresolutions - 1 - resno;

		/* border for each resolution level (global) */
		auto dim = unreduced_tile_comp_dims;
		*((grk_rect_u32*)res) = dim.rectceildivpow2(levelno);

		/* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
		uint32_t pdx = m_tccp->prcw[resno];
		uint32_t pdy = m_tccp->prch[resno];
		/* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
		grk_rect_u32 precinct_grid;
		precinct_grid.x0 = uint_floordivpow2(res->x0, pdx) << pdx;
		precinct_grid.y0 = uint_floordivpow2(res->y0, pdy) << pdy;
		uint64_t temp = (uint64_t)ceildivpow2<uint32_t>(res->x1, pdx) << pdx;
		if (temp > UINT_MAX){
			GRK_ERROR("Resolution x1 value %u must be less than 2^32", temp);
			return false;
		}
		precinct_grid.x1 = (uint32_t)temp;
		temp = (uint64_t)ceildivpow2<uint32_t>(res->y1, pdy) << pdy;
		if (temp > UINT_MAX){
			GRK_ERROR("Resolution y1 value %u must be less than 2^32", temp);
			return false;
		}
		precinct_grid.y1 = (uint32_t)temp;
		res->pw =	(res->x0 == res->x1) ?	0 : (precinct_grid.width() >> pdx);
		res->ph =	(res->y0 == res->y1) ?	0 : (precinct_grid.height() >> pdy);
		res->numBandWindows = (resno == 0) ? 1 : 3;
		if (DEBUG_TILE_COMPONENT){
			std::cout << "res: " << resno << " ";
			res->print();
		}
		for (uint32_t bandno = 0; bandno < res->numBandWindows; ++bandno) {
			auto band = res->bandWindow + bandno;
			eBandOrientation orientation = (resno ==0) ? BAND_ORIENT_LL : (eBandOrientation)(bandno+1);
			band->orientation = orientation;
		}
	}

	//2. calculate region bandWindow
	auto highestNumberOfResolutions =
			(!m_is_encoder) ? resolutions_to_decompress : numresolutions;
	auto hightestResolution =  resolutions + highestNumberOfResolutions - 1;
	grk_rect_u32::operator=(*(grk_rect_u32*)hightestResolution);

	//3. create region buffer
	create_buffer(unreduced_tile_comp_dims, unreduced_tile_comp_region_dims);

	// calculate padded region windows
	if (!whole_tile_decoding){
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
		uint32_t tcx0 = dims.x0;
		uint32_t tcy0 = dims.y0;
		uint32_t tcx1 = dims.x1;
		uint32_t tcy1 = dims.y1;

		for (uint32_t resno = 0; resno < numresolutions; ++resno) {
			auto res = resolutions + resno;
			/* Compute number of decomposition for this band. See table F-1 */
			uint32_t num_decomps = (resno == 0) ?
							numresolutions - 1 :
							numresolutions - resno;
			for (uint32_t orientation = 0; orientation < BAND_NUM_ORIENTATIONS; ++orientation) {
				/* Map above tile-based coordinates to sub-band-based coordinates per */
				/* equation B-15 of the standard */
				uint32_t x0b = orientation & 1;
				uint32_t y0b = orientation >> 1;
				auto window = res->allBandWindow + orientation;
				window->x0 = (num_decomps == 0) ? tcx0 :
								  (tcx0 <= (1U << (num_decomps - 1)) * x0b) ? 0 :
								  ceildivpow2<uint32_t>(tcx0 - (1U << (num_decomps - 1)) * x0b, num_decomps);
				window->y0 = (num_decomps == 0) ? tcy0 :
								  (tcy0 <= (1U << (num_decomps - 1)) * y0b) ? 0 :
								  ceildivpow2<uint32_t>(tcy0 - (1U << (num_decomps - 1)) * y0b, num_decomps);
				window->x1 = (num_decomps == 0) ? tcx1 :
								  (tcx1 <= (1U << (num_decomps - 1)) * x0b) ? 0 :
								  ceildivpow2<uint32_t>(tcx1 - (1U << (num_decomps - 1)) * x0b, num_decomps);
				window->y1 = (num_decomps == 0) ? tcy1 :
								  (tcy1 <= (1U << (num_decomps - 1)) * y0b) ? 0 :
								  ceildivpow2<uint32_t>(tcy1 - (1U << (num_decomps - 1)) * y0b, num_decomps);

			    window->grow(filter_margin,filter_margin);
			}
		}
	}

	// set band step size
	for (uint32_t resno = 0; resno < numresolutions; ++resno) {
		auto res = resolutions + resno;
		for (uint8_t bandno = 0; bandno < res->numBandWindows; ++bandno) {
			auto band = res->bandWindow + bandno;
			if (!m_tccp->quant.setBandStepSizeAndBps(tcp,
											band,
											resno,
											bandno,
											m_tccp,
											prec,
											m_is_encoder))
				return false;
		}
	}

	// 4. initialize precincts and code blocks
	for (uint32_t resno = 0; resno < numresolutions; ++resno) {
		auto res = resolutions + resno;

		/* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
		auto precinct_expn = grk_pt(m_tccp->prcw[resno],m_tccp->prch[resno]);
		/* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
		auto precinct_start = grk_pt(uint_floordivpow2(res->x0, precinct_expn.x) << precinct_expn.x,
										uint_floordivpow2(res->y0, precinct_expn.y) << precinct_expn.y);
		uint64_t num_precincts = (uint64_t)res->pw * res->ph;
		if (mult64_will_overflow(num_precincts, sizeof(Precinct))) {
			GRK_ERROR(	"nb_precinct_size calculation would overflow ");
			return false;
		}
		grk_pt cblk_grid_start;
		grk_pt cblk_grid_expn;
		if (resno == 0) {
			cblk_grid_start = precinct_start;
			cblk_grid_expn = precinct_expn;
		} else {
			cblk_grid_start=  grk_pt(	ceildivpow2<uint32_t>(precinct_start.x, 1),
									ceildivpow2<uint32_t>(precinct_start.y, 1));
			cblk_grid_expn.x = precinct_expn.x - 1;
			cblk_grid_expn.y = precinct_expn.y - 1;
		}
		auto cblk_expn    =  grk_pt(	std::min<uint32_t>(m_tccp->cblkw, cblk_grid_expn.x),
										std::min<uint32_t>(m_tccp->cblkh, cblk_grid_expn.y));
		size_t nominalBlockSize = (1 << cblk_expn.x) * (1 << cblk_expn.y);

		for (uint8_t bandno = 0; bandno < res->numBandWindows; ++bandno) {
			auto band = res->bandWindow + bandno;
			band->precincts = new Precinct[num_precincts];
			band->numPrecincts = num_precincts;
			for (uint64_t precno = 0; precno < num_precincts; ++precno) {
				auto current_precinct = band->precincts + precno;

				auto band_precinct_start = grk_pt(cblk_grid_start.x + (uint32_t)((precno % res->pw) << cblk_grid_expn.x),
													cblk_grid_start.y + (uint32_t)((precno / res->pw) << cblk_grid_expn.y));
				*((grk_rect_u32*)current_precinct) = grk_rect_u32(band_precinct_start.x,
																band_precinct_start.y,
																band_precinct_start.x + (1 << cblk_grid_expn.x),
																band_precinct_start.y + (1 << cblk_grid_expn.y)).intersection(band);

				auto cblk_grid = grk_rect_u32(
						uint_floordivpow2(current_precinct->x0,cblk_expn.x),
						uint_floordivpow2(current_precinct->y0,cblk_expn.y),
						ceildivpow2<uint32_t>(current_precinct->x1,cblk_expn.x),
						ceildivpow2<uint32_t>(current_precinct->y1,cblk_expn.y));

				current_precinct->cblk_grid_width 	= cblk_grid.width();
				current_precinct->cblk_grid_height 	= cblk_grid.height();

				uint64_t nb_code_blocks = cblk_grid.area();
				if (!nb_code_blocks)
					continue;

				if (isCompressor)
					current_precinct->enc = new CompressCodeblock[nb_code_blocks];
				else
					current_precinct->dec = new DecompressCodeblock[nb_code_blocks];
				current_precinct->numCodeBlocks = nb_code_blocks;
				current_precinct->initTagTrees();

				for (uint64_t cblkno = 0; cblkno < nb_code_blocks; ++cblkno) {
					auto cblk_start = grk_pt(	(cblk_grid.x0  + (uint32_t) (cblkno % current_precinct->cblk_grid_width)) << cblk_expn.x,
												(cblk_grid.y0  + (uint32_t) (cblkno / current_precinct->cblk_grid_width)) << cblk_expn.y);
					auto cblk_bounds = grk_rect_u32(cblk_start.x,
													cblk_start.y,
													cblk_start.x + (1 << cblk_expn.x),
													cblk_start.y + (1 << cblk_expn.y));

					auto cblk_dims = (m_is_encoder) ?
												(grk_rect_u32*)(current_precinct->enc + cblkno) :
												(grk_rect_u32*)(current_precinct->dec + cblkno);
					if (m_is_encoder) {
						auto code_block = current_precinct->enc + cblkno;
						if (!code_block->alloc())
							return false;
						if (!current_plugin_tile
								|| (state & GRK_PLUGIN_STATE_DEBUG)) {
							if (!code_block->alloc_data(nominalBlockSize))
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
					}
					*cblk_dims = cblk_bounds.intersection(current_precinct);
				}
			}
		}
	}

	return true;
}

bool TileComponent::subbandIntersectsAOI(uint32_t resno,
								uint32_t bandno,
								const grk_rect_u32 *aoi) const
{
	if (whole_tile_decoding)
		return true;
	assert(resno < numresolutions && bandno <=3);
	auto orientation = (resno == 0) ? 0 : bandno+1;
	auto window = ((resolutions + resno)->allBandWindow)[orientation];

    return window.intersection(aoi).is_non_degenerate();
}

void TileComponent::allocSparseBuffer(uint32_t numres){
    auto tr_max = resolutions + numres - 1;
	uint32_t w = tr_max->width();
	uint32_t h = tr_max->height();
	auto sa = new SparseBuffer<6,6>(w, h);

    for (uint32_t resno = 0; resno < numres; ++resno) {
        auto res = &resolutions[resno];
        for (uint32_t bandno = 0; bandno < res->numBandWindows; ++bandno) {
            auto band = res->bandWindow + bandno;
            for (uint64_t precno = 0; precno < (uint64_t)res->pw * res->ph; ++precno) {
                auto precinct = band->precincts + precno;
                for (uint64_t cblkno = 0; cblkno < (uint64_t)precinct->cblk_grid_width * precinct->cblk_grid_height; ++cblkno) {
                    auto cblk = precinct->dec + cblkno;
					// check overlap in band coordinates
					if (subbandIntersectsAOI(resno,	bandno,	cblk)){
						uint32_t x = cblk->x0;
						uint32_t y = cblk->y0;

						// switch from coordinates relative to band,
						// to coordinates relative to current resolution
						x -= band->x0;
						y -= band->y0;

						/* add band offset relative to previous resolution */
						if (band->orientation & 1) {
							auto prev_res = resolutions + resno - 1;
							x += prev_res->x1 - prev_res->x0;
						}
						if (band->orientation & 2) {
							auto prev_res = resolutions + resno - 1;
							y += prev_res->y1 - prev_res->y0;
						}

						if (!sa->alloc(grk_rect_u32(x,
												  y,
												  x + cblk->width(),
												  y + cblk->height()))) {
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

void TileComponent::create_buffer(grk_rect_u32 unreduced_tile_comp_dims,
									grk_rect_u32 unreduced_tile_comp_window_dims) {
	// calculate bandWindow
	for (uint32_t resno = 0; resno < numresolutions; ++resno) {
		auto res = resolutions + resno;
		for (uint32_t bandno = 0; bandno < res->numBandWindows; ++bandno) {
			auto band = res->bandWindow + bandno;
			*((grk_rect_u32*)band) =
					grk_band_window(numresolutions, resno, band->orientation,unreduced_tile_comp_dims);
		}
	}

	delete buf;
	auto highestNumberOfResolutions =
			(!m_is_encoder) ? resolutions_to_decompress : numresolutions;
	auto maxResolution = resolutions + numresolutions - 1;
	buf = new TileComponentWindowBuffer<int32_t>(m_is_encoder,
											*(grk_rect_u32*)maxResolution,
											*(grk_rect_u32*)this,
											unreduced_tile_comp_window_dims,
											resolutions,
											numresolutions,
											highestNumberOfResolutions);
}

TileComponentWindowBuffer<int32_t>* TileComponent::getBuffer(){
	return buf;
}

bool TileComponent::isWholeTileDecoding() {
	return whole_tile_decoding;
}
ISparseBuffer* TileComponent::getSparseBuffer(){
	return m_sa;
}

}

