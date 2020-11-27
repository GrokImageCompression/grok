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
			for (uint32_t bandIndex = 0; bandIndex < 3; ++bandIndex) {
				auto band = res->bandWindow + bandIndex;
				for (auto prc : band->precincts)
					delete prc;
				band->precincts.clear();
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
						grk_rect_u32 unreduced_tile_comp_window_dims,
						uint8_t prec,
						CodingParams *cp,
						TileCodingParams *tcp,
						TileComponentCodingParams* tccp,
						grk_plugin_tile *current_plugin_tile){
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
		for (uint32_t bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
			auto band = res->bandWindow + bandIndex;
			eBandOrientation orientation = (resno ==0) ? BAND_ORIENT_LL : (eBandOrientation)(bandIndex+1);
			band->orientation = orientation;
		}
	}

	//2. calculate region bandWindow
	auto highestNumberOfResolutions =
			(!m_is_encoder) ? resolutions_to_decompress : numresolutions;
	auto hightestResolution =  resolutions + highestNumberOfResolutions - 1;
	grk_rect_u32::operator=(*(grk_rect_u32*)hightestResolution);

	//3. create window buffer
	create_buffer(unreduced_tile_comp_dims, unreduced_tile_comp_window_dims);

	// calculate padded windows
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

	    /* Compute the intersection of the window of interest, expressed in tile component coordinates, */
	    /* with the tile component */
		auto dims = buf->unreduced_bounds();
		for (uint32_t resno = 0; resno < numresolutions; ++resno) {
			auto res = resolutions + resno;
			for (uint32_t index = 0; index < res->numBandWindows; ++index) {
				uint32_t orientation = (resno == 0) ? 0 : index+1;
				auto paddedWindow = res->paddedBandWindow + index;
				*paddedWindow = grk_band_window(numresolutions, resno, orientation,dims);
			    paddedWindow->grow(filter_margin,filter_margin);
			}
		}
	}

	// set band step size
	for (uint32_t resno = 0; resno < numresolutions; ++resno) {
		auto res = resolutions + resno;
		for (uint8_t bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
			auto band = res->bandWindow + bandIndex;
			if (!m_tccp->quant.setBandStepSizeAndBps(tcp,
													band,
													resno,
													bandIndex,
													m_tccp,
													prec,
													m_is_encoder))
				return false;
		}
	}

	// 4. initialize precincts and code blocks
	for (uint32_t resno = 0; resno < numresolutions; ++resno) {
		auto res = resolutions + resno;
		if (!res->init(isCompressor,m_tccp,(uint8_t)resno,current_plugin_tile))
			return false;

	}

	return true;
}

bool TileComponent::subbandIntersectsAOI(uint32_t resno,
								uint32_t bandIndex,
								const grk_rect_u32 *aoi) const
{
	if (whole_tile_decoding)
		return true;
	assert(resno < numresolutions && bandIndex < BAND_NUM_INDICES);
	auto paddedBandWinow = ((resolutions + resno)->paddedBandWindow)[bandIndex];

    return paddedBandWinow.intersection(aoi).is_non_degenerate();
}

void TileComponent::allocSparseBuffer(uint32_t numres){
    auto tr_max = resolutions + numres - 1;
	uint32_t w = tr_max->width();
	uint32_t h = tr_max->height();
	auto sa = new SparseBuffer<6,6>(w, h);

    for (uint32_t resno = 0; resno < numres; ++resno) {
        auto res = &resolutions[resno];
        for (uint32_t bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
            auto band = res->bandWindow + bandIndex;
            for (auto precinct : band->precincts) {
                for (uint64_t cblkno = 0; cblkno < precinct->getNumCblks(); ++cblkno) {
                    auto cblk = precinct->getDecompressedBlockPtr() + cblkno;
					// check overlap in band coordinates
					if (subbandIntersectsAOI(resno,	bandIndex,	cblk)){
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
		for (uint32_t bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
			auto band = res->bandWindow + bandIndex;
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

