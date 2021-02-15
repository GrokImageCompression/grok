/*
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
 *
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"
#include "PostDecompressFilters.h"

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
							   wholeTileDecompress(true),
							   m_is_encoder(false),
							   buf(nullptr),
							   m_tccp(nullptr)
{}

TileComponent::~TileComponent(){
	release_mem(true);
}
void TileComponent::release_mem(bool releaseBuffer){
	if (resolutions) {
		for (uint32_t resno = 0; resno < numresolutions; ++resno) {
			auto res = resolutions + resno;
			for (uint32_t bandIndex = 0; bandIndex < 3; ++bandIndex) {
				auto band = res->band + bandIndex;
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
	if (releaseBuffer) {
		delete buf;
		buf = nullptr;
	}
}

/**
 * Initialize tile component in unreduced tile component coordinates
 * (tile component coordinates take sub-sampling into account).
 *
 */
bool TileComponent::init(bool isCompressor,
						bool whole_tile,
						grk_rect_u32 unreducedTileComp,
						grk_rect_u32 unreducedTileOrImageCompWindow,
						uint8_t prec,
						CodingParams *cp,
						TileCodingParams *tcp,
						TileComponentCodingParams* tccp,
						grk_plugin_tile *current_plugin_tile){
	m_is_encoder = isCompressor;
	wholeTileDecompress = whole_tile;
	m_tccp = tccp;

	// 1. calculate resolution bounds, precinct bounds and precinct grid
	// all in canvas coordinates (with subsampling)
	numresolutions = m_tccp->numresolutions;
	if (numresolutions < cp->m_coding_params.m_dec.m_reduce) {
		resolutions_to_decompress = 1;
	} else {
		resolutions_to_decompress =	(uint8_t)(numresolutions - cp->m_coding_params.m_dec.m_reduce);
	}
	resolutions = new Resolution[numresolutions];
	for (uint8_t resno = 0; resno < numresolutions; ++resno) {
		auto res = resolutions + resno;

		grk_rect_u32 dim;
		if (resno == numresolutions - 1)
			dim = unreducedTileComp;
		else
			dim = getTileCompBandWindow(numresolutions,(uint8_t)(resno+1U),BAND_ORIENT_LL,unreducedTileComp);
		res->set_rect(dim);

		/* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
		uint32_t pdx = m_tccp->prcw_exp[resno];
		uint32_t pdy = m_tccp->prch_exp[resno];
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
	}

	//2. set tile component and band bounds
	auto highestNumberOfResolutions =
			(!m_is_encoder) ? resolutions_to_decompress : numresolutions;
	auto hightestResolution =  resolutions + highestNumberOfResolutions - 1;
	set_rect(hightestResolution);
	for (uint8_t resno = 0; resno < numresolutions; ++resno) {
		auto res = resolutions + resno;
		for (uint32_t bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
			auto band = res->band + bandIndex;
			eBandOrientation orientation = (resno ==0) ? BAND_ORIENT_LL : (eBandOrientation)(bandIndex+1);
			band->orientation = orientation;
			band->set_rect(getTileCompBandWindow(numresolutions, resno, band->orientation,unreducedTileComp));
		}
	}

	//3. create window buffer
	if (!create_buffer(unreducedTileOrImageCompWindow))
		return false;

	// set band step size
	for (uint32_t resno = 0; resno < numresolutions; ++resno) {
		auto res = resolutions + resno;
		for (uint8_t bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
			auto band = res->band + bandIndex;
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

bool TileComponent::subbandIntersectsAOI(uint8_t resno,
								eBandOrientation orient,
								const grk_rect_u32 *aoi) const
{
	assert(resno < numresolutions);
	if (wholeTileDecompress)
		return true;
    return buf->getPaddedTileBandWindow(resno, orient).intersection(aoi).non_empty();
}

void TileComponent::allocSparseBuffer(uint32_t numres){
	grk_rect_u32 temp(0,0,0,0);
	bool first = true;

    for (uint8_t resno = 0; resno < numres; ++resno) {
        auto res = &resolutions[resno];
        for (uint8_t bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
          	auto band = res->band + bandIndex;
          	auto roi = buf->getPaddedTileBandWindow(resno, band->orientation);
            for (auto precinct : band->precincts) {
            	if (!precinct->non_empty())
            		continue;
            	auto cblk_grid = precinct->getCblkGrid();
            	auto cblk_expn = precinct->getCblkExpn();
				grk_rect_u32 roi_grid = grk_rect_u32( uint_floordivpow2(roi.x0,  cblk_expn.x),
													 uint_floordivpow2(roi.y0,  cblk_expn.y),
													 ceildivpow2(roi.x1,  cblk_expn.x),
													 ceildivpow2(roi.y1,  cblk_expn.y));
				roi_grid.clip(&cblk_grid);
				auto w = cblk_grid.width();
				for (uint32_t j = roi_grid.y0; j < roi_grid.y1; ++j) {
					uint64_t cblkno = (roi_grid.x0 - cblk_grid.x0)  + (uint64_t)(j - cblk_grid.y0 ) * w;
					for (uint32_t i = roi_grid.x0; i < roi_grid.x1; ++i){
						 auto cblk = precinct->getDecompressedBlockPtr(cblkno);

						// transform from band coordinates
						// to canvas coordinates
						uint32_t x = cblk->x0 - band->x0;
						uint32_t y = cblk->y0 - band->y0;
						if (band->orientation & 1) {
							auto prev_res = resolutions + resno - 1;
							x += prev_res->width();
						}
						if (band->orientation & 2) {
							auto prev_res = resolutions + resno - 1;
							y += prev_res->height();
						}
						// add to union of code block bounds
						if (first) {
							temp = grk_rect_u32(x,y,x + cblk->width(), y + cblk->height());
							first = false;
						}
						else {
							temp = temp.rect_union(grk_rect_u32(x,y, x + cblk->width(),y + cblk->height()));
						}
						cblkno++;
					}
				}
            }
        }
    }

    auto tr_max = resolutions + numres - 1;
    temp.grow(5,tr_max->width(),tr_max->height());
	auto sa = new SparseBuffer<6,6>(temp);

    for (uint8_t resno = 0; resno < numres; ++resno) {
        auto res = &resolutions[resno];
        for (uint8_t bandIndex = 0; bandIndex < res->numBandWindows; ++bandIndex) {
          	auto band = res->band + bandIndex;
          	auto roi = buf->getPaddedTileBandWindow(resno, band->orientation);
            for (auto precinct : band->precincts) {
            	if (!precinct->non_empty())
            		continue;
            	auto cblk_grid = precinct->getCblkGrid();
            	auto cblk_expn = precinct->getCblkExpn();
				grk_rect_u32 roi_grid = grk_rect_u32( uint_floordivpow2(roi.x0,  cblk_expn.x),
													 uint_floordivpow2(roi.y0,  cblk_expn.y),
													 ceildivpow2(roi.x1,  cblk_expn.x),
													 ceildivpow2(roi.y1,  cblk_expn.y));
				roi_grid.clip(&cblk_grid);
				auto w = cblk_grid.width();
				for (uint32_t j = cblk_grid.y0; j < cblk_grid.y1; ++j) {
					uint64_t cblkno = (roi_grid.x0 - cblk_grid.x0)  + (uint64_t)(j - cblk_grid.y0 ) * w;
					for (uint32_t i = roi_grid.x0; i < roi_grid.x1; ++i){
						 auto cblk = precinct->getDecompressedBlockPtr(cblkno);

						// transform from band coordinates
						// to canvas coordinates
						uint32_t x = cblk->x0 - band->x0;
						uint32_t y = cblk->y0 - band->y0;
						if (band->orientation & 1) {
							auto prev_res = resolutions + resno - 1;
							x += prev_res->width();
						}
						if (band->orientation & 2) {
							auto prev_res = resolutions + resno - 1;
							y += prev_res->height();
						}

						if (!sa->alloc(grk_rect_u32(x,
												  y,
												  x + cblk->width(),
												  y + cblk->height()))) {
							delete sa;
							throw runtime_error("unable to allocate sparse array");
						}
						cblkno++;
					}
				}
            }
        }
    }
    if (m_sa)
    	delete m_sa;
    m_sa = sa;
}

bool TileComponent::create_buffer(grk_rect_u32 unreducedTileOrImageCompWindow) {
	if (buf)
		return true;
	auto highestNumberOfResolutions =
			(!m_is_encoder) ? resolutions_to_decompress : numresolutions;
	auto maxResolution = resolutions + numresolutions - 1;
	if (!maxResolution->intersection(unreducedTileOrImageCompWindow).is_valid()){
		GRK_ERROR("Decompress window (%d,%d,%d,%d) must overlap image bounds (%d,%d,%d,%d)",
				unreducedTileOrImageCompWindow.x0,
				unreducedTileOrImageCompWindow.y0,
				unreducedTileOrImageCompWindow.x1,
				unreducedTileOrImageCompWindow.y1,
				maxResolution->x0,
				maxResolution->y0,
				maxResolution->x1,
				maxResolution->y1);
		return false;
	}
	buf = new TileComponentWindowBuffer<int32_t>(m_is_encoder,
											m_tccp->qmfbid == 1,
											wholeTileDecompress,
											*(grk_rect_u32*)maxResolution,
											*(grk_rect_u32*)this,
											unreducedTileOrImageCompWindow,
											resolutions,
											numresolutions,
											highestNumberOfResolutions);

	return true;
}

TileComponentWindowBuffer<int32_t>* TileComponent::getBuffer() const{
	return buf;
}

bool TileComponent::isWholeTileDecoding() {
	return wholeTileDecompress;
}
ISparseBuffer* TileComponent::getSparseBuffer(){
	return m_sa;
}

bool TileComponent::postDecompress(int32_t *srcData, DecompressBlockExec *block, bool isHT) {
	if (isHT){
		if (block->roishift) {
			if (block->qmfbid == 1) {
				return postDecompressImpl< RoiShiftHTFilter<int32_t>  >(srcData, block);
			} else {
				return postDecompressImpl< RoiScaleHTFilter<int32_t>  >(srcData, block);
			}
		} else {
			if (block->qmfbid == 1) {
				return postDecompressImpl< ShiftHTFilter<int32_t> >(srcData, block);
			} else {
				return postDecompressImpl< ScaleHTFilter<int32_t> >(srcData, block);
			}
		}
	} else {
		if (block->roishift) {
			if (block->qmfbid == 1) {
				return postDecompressImpl< RoiShiftFilter<int32_t>  >(srcData, block);
			} else {
				return postDecompressImpl< RoiScaleFilter<int32_t>  >(srcData, block);
			}
		} else {
			if (block->qmfbid == 1) {
				return postDecompressImpl< ShiftFilter<int32_t> >(srcData, block);
			} else {
				return postDecompressImpl< ScaleFilter<int32_t> >(srcData, block);
			}
		}
	}
}

template<typename F> bool TileComponent::postDecompressImpl(int32_t *srcData, DecompressBlockExec *block){
	auto cblk = block->cblk;

	grk_buffer_2d<int32_t> dest;
	grk_buffer_2d<int32_t> src = grk_buffer_2d<int32_t>(srcData, false, cblk->width(), cblk->width(), cblk->height());
	buf->transformToCanvasCoordinates(block->resno,block->band_orientation,block->x,block->y);
	if (m_sa) {
		dest = src;
	}
	else {
		src.set_rect(grk_rect_u32(block->x,
								block->y,
								block->x + cblk->width(),
								block->y + cblk->height()));
		dest = buf->getCodeBlockDestWindow(block->resno,block->band_orientation);
	}

	if (!cblk->seg_buffers.empty()){
		F f(block);
		dest.copy<F>(src, f);
	} else {
		srcData = nullptr;
	}

	if (m_sa && srcData){
		if (!m_sa->write(block->resno,
						grk_rect_u32(block->x,
									  block->y,
									  block->x + cblk->width(),
									  block->y + cblk->height()),
						  srcData,
						  1,
						  cblk->width(),
						  true)) {
			  return false;
		}
	}

	return true;
}

}

