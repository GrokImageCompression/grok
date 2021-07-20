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

#include "PostT1DecompressFilters.h"
#include "OJPH/PostT1DecompressFiltersOJPH.h"
#include "grk_includes.h"

const bool DEBUG_TILE_COMPONENT = false;

namespace grk
{
TileComponent::TileComponent()
	: tileCompResolution(nullptr), numresolutions(0), resolutions_to_decompress(0),
	  resolutions_decompressed(0),
#ifdef DEBUG_LOSSLESS_T2
	  round_trip_resolutions(nullptr),
#endif
	  m_sa(nullptr), wholeTileDecompress(true), m_is_encoder(false), buf(nullptr), m_tccp(nullptr)
{}

TileComponent::~TileComponent()
{
	if(tileCompResolution)
	{
		for(uint32_t resno = 0; resno < numresolutions; ++resno)
		{
			auto res = tileCompResolution + resno;
			for(uint32_t bandIndex = 0; bandIndex < 3; ++bandIndex)
			{
				auto band = res->tileBand + bandIndex;
				for(auto prc : band->precincts)
					delete prc;
				band->precincts.clear();
			}
		}
		delete[] tileCompResolution;
	}
	deallocBuffers();
}
void TileComponent::deallocBuffers(void)
{
	delete m_sa;
	m_sa = nullptr;
	delete buf;
	buf = nullptr;
}
/**
 * Initialize tile component in unreduced tile component coordinates
 * (tile component coordinates take sub-sampling into account).
 *
 */
bool TileComponent::init(bool isCompressor, bool whole_tile, grkRectU32 unreducedTileComp,
						 uint8_t prec, CodingParams* cp,
						 TileComponentCodingParams* tccp, grk_plugin_tile* current_plugin_tile)
{
	m_is_encoder = isCompressor;
	wholeTileDecompress = whole_tile;
	m_tccp = tccp;

	// 1. calculate resolution bounds, precinct bounds and precinct grid
	// all in canvas coordinates (with subsampling)
	numresolutions = m_tccp->numresolutions;
	if(numresolutions < cp->m_coding_params.m_dec.m_reduce)
		resolutions_to_decompress = 1;
	else
		resolutions_to_decompress = (uint8_t)(numresolutions - cp->m_coding_params.m_dec.m_reduce);
	tileCompResolution = new Resolution[numresolutions];
	for(uint8_t resno = 0; resno < numresolutions; ++resno)
	{
		auto res = tileCompResolution + resno;

		res->set(BufferResWindow<int32_t>::getTileCompBandWindow(
			(uint32_t)(numresolutions - (resno + 1)), BAND_ORIENT_LL, unreducedTileComp));

		/* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
		uint32_t precinctWidthExp = m_tccp->precinctWidthExp[resno];
		uint32_t precinctHeightExp = m_tccp->precinctHeightExp[resno];
		/* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
		grkRectU32 allPrecinctsBounds;
		allPrecinctsBounds.x0 = floordivpow2(res->x0, precinctWidthExp) << precinctWidthExp;
		allPrecinctsBounds.y0 = floordivpow2(res->y0, precinctHeightExp) << precinctHeightExp;
		uint64_t temp = (uint64_t)ceildivpow2<uint32_t>(res->x1, precinctWidthExp)
						<< precinctWidthExp;
		if(temp > UINT_MAX)
		{
			GRK_ERROR("Resolution x1 value %u must be less than 2^32", temp);
			return false;
		}
		allPrecinctsBounds.x1 = (uint32_t)temp;
		temp = (uint64_t)ceildivpow2<uint32_t>(res->y1, precinctHeightExp) << precinctHeightExp;
		if(temp > UINT_MAX)
		{
			GRK_ERROR("Resolution y1 value %u must be less than 2^32", temp);
			return false;
		}
		allPrecinctsBounds.y1 = (uint32_t)temp;
		res->precinctGridWidth =
			(res->x0 == res->x1) ? 0 : (allPrecinctsBounds.width() >> precinctWidthExp);
		res->precinctGridHeight =
			(res->y0 == res->y1) ? 0 : (allPrecinctsBounds.height() >> precinctHeightExp);
		res->numTileBandWindows = (resno == 0) ? 1 : 3;
		if(DEBUG_TILE_COMPONENT)
		{
			std::cout << "res: " << resno << " ";
			res->print();
		}
	}

	// 2. set tile component and band bounds
	auto highestNumberOfResolutions = (!m_is_encoder) ? resolutions_to_decompress : numresolutions;
	auto hightestResolution = tileCompResolution + highestNumberOfResolutions - 1;
	set(hightestResolution);
	for(uint8_t resno = 0; resno < numresolutions; ++resno)
	{
		auto res = tileCompResolution + resno;
		for(uint32_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
		{
			auto band = res->tileBand + bandIndex;
			eBandOrientation orientation =
				(resno == 0) ? BAND_ORIENT_LL : (eBandOrientation)(bandIndex + 1);
			band->orientation = orientation;
			uint32_t numDecomps =
				(resno == 0) ? (uint32_t)(numresolutions - 1U) : (uint32_t)(numresolutions - resno);
			band->set(BufferResWindow<int32_t>::getTileCompBandWindow(numDecomps, band->orientation,
																	  unreducedTileComp));
		}
	}
	// set band step size
	for(uint32_t resno = 0; resno < numresolutions; ++resno)
	{
		auto res = tileCompResolution + resno;
		for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
		{
			auto band = res->tileBand + bandIndex;

			/* Table E-1 - Sub-band gains */
			/* BUG_WEIRD_TWO_INVK (look for this identifier in dwt.c): */
			/* the test (!isEncoder && l_tccp->qmfbid == 0) is strongly */
			/* linked to the use of two_invK instead of invK */
			const uint32_t log2_gain = (!m_is_encoder && tccp->qmfbid == 0) ? 0
									   : (band->orientation == 0)		? 0
									   : (band->orientation == 3)		? 2
																		: 1;
			uint32_t numbps = prec + log2_gain;
			auto offset = (resno == 0) ? 0 : 3 * resno - 2;
			auto step_size = tccp->stepsizes + offset + bandIndex;
			band->stepsize =
				(float)(((1.0 + step_size->mant / 2048.0) * pow(2.0, (int32_t)(numbps - step_size->expn))));
			// printf("res=%d, band=%d, mant=%d,expn=%d, numbps=%d, step size=
			// %f\n",resno,band->orientation,step_size->mant,step_size->expn,numbps, band->stepsize);

			// see Taubman + Marcellin - Equation 10.22
			band->numbps = tccp->roishift +
						   (uint8_t)std::max<int8_t>(0, int8_t(step_size->expn + tccp->numgbits - 1U));
			// assert(band->numbps <= maxBitPlanesGRK);
		}
	}

	// 4. initialize precincts and code blocks
	for(uint32_t resno = 0; resno < numresolutions; ++resno)
	{
		auto res = tileCompResolution + resno;
		if(!res->init(isCompressor, m_tccp, (uint8_t)resno, current_plugin_tile))
			return false;
	}

	return true;
}
bool TileComponent::subbandIntersectsAOI(uint8_t resno, eBandOrientation orient,
										 const grkRectU32* aoi) const
{
	return buf->getPaddedBandWindow(resno, orient)->non_empty_intersection(aoi);
}
bool TileComponent::allocSparseCanvas(uint32_t numres, bool truncatedTile)
{
	grkRectU32 temp(0, 0, 0, 0);
	bool first = true;

	for(uint8_t resno = 0; resno < numres; ++resno)
	{
		auto res = &tileCompResolution[resno];
		for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
		{
			auto band = res->tileBand + bandIndex;
			auto roi = buf->getPaddedBandWindow(resno, band->orientation);
			for(auto precinct : band->precincts)
			{
				if(!precinct->non_empty())
					continue;
				auto cblk_grid = precinct->getCblkGrid();
				auto cblk_expn = precinct->getCblkExpn();
				grkRectU32 roi_grid = grkRectU32(
					floordivpow2(roi->x0, cblk_expn.x), floordivpow2(roi->y0, cblk_expn.y),
					ceildivpow2(roi->x1, cblk_expn.x), ceildivpow2(roi->y1, cblk_expn.y));
				roi_grid.clip(&cblk_grid);
				auto w = cblk_grid.width();
				for(uint32_t j = roi_grid.y0; j < roi_grid.y1; ++j)
				{
					uint64_t cblkno =
						(roi_grid.x0 - cblk_grid.x0) + (uint64_t)(j - cblk_grid.y0) * w;
					for(uint32_t i = roi_grid.x0; i < roi_grid.x1; ++i)
					{
						auto cblkBounds = precinct->getCodeBlockBounds(cblkno);

						// transform from canvas coordinates
						// to buffer coordinates (relative to associated resolution origin)
						uint32_t x = cblkBounds.x0 - band->x0;
						uint32_t y = cblkBounds.y0 - band->y0;
						if(band->orientation & 1)
						{
							auto prev_res = tileCompResolution + resno - 1;
							x += prev_res->width();
						}
						if(band->orientation & 2)
						{
							auto prev_res = tileCompResolution + resno - 1;
							y += prev_res->height();
						}
						// add to union of code block bounds
						if(first)
						{
							temp =
								grkRectU32(x, y, x + cblkBounds.width(), y + cblkBounds.height());
							first = false;
						}
						else
						{
							temp = temp.rectUnion(
								grkRectU32(x, y, x + cblkBounds.width(), y + cblkBounds.height()));
						}
						cblkno++;
					}
				}
			}
		}
	}

	auto tr_max = tileCompResolution + numres - 1;
	temp.grow(5, tr_max->width(), tr_max->height());
	auto sa = new SparseCanvas<6, 6>(temp);

	for(uint8_t resno = 0; resno < numres; ++resno)
	{
		auto res = &tileCompResolution[resno];
		for(uint8_t bandIndex = 0; bandIndex < res->numTileBandWindows; ++bandIndex)
		{
			auto band = res->tileBand + bandIndex;
			auto roi = buf->getPaddedBandWindow(resno, band->orientation);
			for(auto precinct : band->precincts)
			{
				if(!precinct->non_empty())
					continue;
				auto cblk_grid = precinct->getCblkGrid();
				auto cblk_expn = precinct->getCblkExpn();
				grkRectU32 roi_grid = grkRectU32(
					floordivpow2(roi->x0, cblk_expn.x), floordivpow2(roi->y0, cblk_expn.y),
					ceildivpow2(roi->x1, cblk_expn.x), ceildivpow2(roi->y1, cblk_expn.y));
				roi_grid.clip(&cblk_grid);
				auto w = cblk_grid.width();
				for(uint32_t j = cblk_grid.y0; j < cblk_grid.y1; ++j)
				{
					uint64_t cblkno =
						(roi_grid.x0 - cblk_grid.x0) + (uint64_t)(j - cblk_grid.y0) * w;
					for(uint32_t i = roi_grid.x0; i < roi_grid.x1; ++i)
					{
						auto cblkBounds = precinct->getCodeBlockBounds(cblkno);

						// transform from canvas coordinates
						// to buffer coordinates (relative to associated resolution origin)
						uint32_t x = cblkBounds.x0 - band->x0;
						uint32_t y = cblkBounds.y0 - band->y0;
						if(band->orientation & 1)
						{
							auto prev_res = tileCompResolution + resno - 1;
							x += prev_res->width();
						}
						if(band->orientation & 2)
						{
							auto prev_res = tileCompResolution + resno - 1;
							y += prev_res->height();
						}

						if(!sa->alloc(
							   grkRectU32(x, y, x + cblkBounds.width(), y + cblkBounds.height()),
							   truncatedTile))
						{
							delete sa;
							throw runtime_error("unable to allocate sparse array");
						}
						cblkno++;
					}
				}
			}
		}
	}
	if(m_sa)
		delete m_sa;
	m_sa = sa;

	return true;
}
bool TileComponent::allocWindowBuffer(grkRectU32 unreducedTileCompOrImageCompWindow)
{
	deallocBuffers();
	auto highestNumberOfResolutions = (!m_is_encoder) ? resolutions_to_decompress : numresolutions;
	auto maxResolution = tileCompResolution + numresolutions - 1;
	if(!maxResolution->intersection(unreducedTileCompOrImageCompWindow).is_valid())
	{
		GRK_ERROR("Decompress window (%d,%d,%d,%d) must overlap image bounds (%d,%d,%d,%d)",
				  unreducedTileCompOrImageCompWindow.x0, unreducedTileCompOrImageCompWindow.y0,
				  unreducedTileCompOrImageCompWindow.x1, unreducedTileCompOrImageCompWindow.y1,
				  maxResolution->x0, maxResolution->y0, maxResolution->x1, maxResolution->y1);
		return false;
	}
	buf = new TileComponentWindowBuffer<int32_t>(
		m_is_encoder, m_tccp->qmfbid == 1, wholeTileDecompress, *(grkRectU32*)maxResolution,
		*(grkRectU32*)this, unreducedTileCompOrImageCompWindow, tileCompResolution, numresolutions,
		highestNumberOfResolutions);

	return true;
}
TileComponentWindowBuffer<int32_t>* TileComponent::getBuffer() const
{
	return buf;
}
bool TileComponent::isWholeTileDecoding()
{
	return wholeTileDecompress;
}
ISparseCanvas* TileComponent::getSparseCanvas()
{
	return m_sa;
}
bool TileComponent::postProcess(int32_t* srcData, DecompressBlockExec* block)
{
	if(block->roishift)
	{
		if(block->qmfbid == 1)
			return postDecompressImpl<RoiShiftFilter<int32_t>>(srcData, block,
															   (uint16_t)block->cblk->width());
		else
			return postDecompressImpl<RoiScaleFilter<int32_t>>(srcData, block,
															   (uint16_t)block->cblk->width());
	}
	else
	{
		if(block->qmfbid == 1)
			return postDecompressImpl<ShiftFilter<int32_t>>(srcData, block,
															(uint16_t)block->cblk->width());
		else
			return postDecompressImpl<ScaleFilter<int32_t>>(srcData, block,
															(uint16_t)block->cblk->width());
	}
}
bool TileComponent::postProcessHT(int32_t* srcData, DecompressBlockExec* block, uint16_t stride)
{
	if(block->roishift)
	{
		if(block->qmfbid == 1)
			return postDecompressImpl<ojph::RoiShiftHTFilter<int32_t>>(srcData, block, stride);
		else
			return postDecompressImpl<ojph::RoiScaleHTFilter<int32_t>>(srcData, block, stride);
	}
	else
	{
		if(block->qmfbid == 1)
			return postDecompressImpl<ojph::ShiftHTFilter<int32_t>>(srcData, block, stride);
		else
			return postDecompressImpl<ojph::ScaleHTFilter<int32_t>>(srcData, block, stride);
	}
}
template<typename F>
bool TileComponent::postDecompressImpl(int32_t* srcData, DecompressBlockExec* block,
									   uint16_t stride)
{
	auto cblk = block->cblk;

	grkBuffer2d<int32_t, AllocatorAligned> dest;
	grkBuffer2d<int32_t, AllocatorAligned> src = grkBuffer2d<int32_t, AllocatorAligned>(
		srcData, false, cblk->width(), stride, cblk->height());
	buf->toRelativeCoordinates(block->resno, block->bandOrientation, block->x, block->y);
	if(m_sa)
	{
		dest = src;
	}
	else
	{
		src.set(
			grkRectU32(block->x, block->y, block->x + cblk->width(), block->y + cblk->height()));
		dest = buf->getCodeBlockDestWindowREL(block->resno, block->bandOrientation);
	}

	if(!cblk->seg_buffers.empty())
	{
		F f(block);
		dest.copy<F>(src, f);
	}
	else
	{
		srcData = nullptr;
	}

	if(m_sa)
	{
		if(!m_sa->write(
			   block->resno,
			   grkRectU32(block->x, block->y, block->x + cblk->width(), block->y + cblk->height()),
			   srcData, 1, cblk->width(), true))
		{
			return false;
		}
	}

	return true;
}

} // namespace grk
