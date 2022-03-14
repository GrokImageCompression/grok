/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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

namespace grk
{
ResPrecinctInfo::ResPrecinctInfo()
	: precinctWidthExp(0), precinctHeightExp(0), canvasResOffsetX0(0), canvasResOffsetY0(0), canvasPrecWidth(0), canvasPrecHeight(0), canvasDx(0), canvasDy(0),
	  canvasResInPrecGridX0(0), canvasResInPrecGridY0(0), decompLevel_(0),
	  valid(false)
{}
void ResPrecinctInfo::init(uint8_t decompLevel, grkRectU32 tileBounds, uint32_t compDx, uint32_t compDy,
						   bool windowed, grkRectU32 tileWindow)
{
	valid = false;
	uint64_t resDivisor = ((uint64_t)compDx << decompLevel);
	grkRectU32 resBounds(ceildiv<uint64_t>((uint64_t)tileBounds.x0, resDivisor),
						 ceildiv<uint64_t>((uint64_t)tileBounds.y0, resDivisor),
						 ceildiv<uint64_t>((uint64_t)tileBounds.x1, resDivisor),
						 ceildiv<uint64_t>((uint64_t)tileBounds.y1, resDivisor));
	if(resBounds.x0 == resBounds.x1 || resBounds.y0 == resBounds.y1)
		return;

	decompLevel_ = decompLevel;
	uint32_t canvasPrecShiftX = precinctWidthExp + decompLevel_;
	uint32_t canvasPrecShiftY = precinctHeightExp + decompLevel_;

	// offset of projected resolution relative to projected precinct grid
	// these are both zero when tile origin equals (0,0)
	canvasResOffsetX0 = (uint32_t)(((uint64_t)resBounds.x0 << decompLevel) % ((uint64_t)1 << canvasPrecShiftX));
	canvasResOffsetY0 = (uint32_t)(((uint64_t)resBounds.y0 << decompLevel) % ((uint64_t)1 << canvasPrecShiftY));

	canvasPrecWidth = ((uint64_t)compDx << canvasPrecShiftX);
	canvasPrecHeight = ((uint64_t)compDy << canvasPrecShiftY);
	canvasDx = ((uint64_t)compDx << decompLevel_);
	canvasDy = ((uint64_t)compDy << decompLevel_);
	canvasResInPrecGridX0 = floordivpow2(resBounds.x0, precinctWidthExp);
	canvasResInPrecGridY0 = floordivpow2(resBounds.y0, precinctHeightExp);
	if(windowed)
	{
		window = tileWindow;
		window.grow(10);
		windowPrecGrid = grkRectU32(window.x0 / canvasPrecWidth,
									window.y0 / canvasPrecHeight,
									ceildiv<uint64_t>(window.x1, canvasPrecWidth),
									ceildiv<uint64_t>(window.y1, canvasPrecHeight));
		windowPrec =  grkRectU32(windowPrecGrid.x0 * canvasPrecWidth,
								 windowPrecGrid.y0 * canvasPrecHeight,
								 windowPrecGrid.x1 * canvasPrecWidth,
								 windowPrecGrid.y1 * canvasPrecHeight);
	}
	valid = true;
}

PacketIter::PacketIter()
	: step_l(0), step_r(0), step_c(0), step_p(0), compno(0), resno(0), precinctIndex(0), layno(0),
	  numcomps(0), comps(nullptr), x(0), y(0), dx(0), dy(0),valid(true),optimized(false),
	  incrementInner(false),
	  packetManager(nullptr), maxNumDecompositionResolutions(0), singleProgression_(false),
	  precinctInfo_(nullptr), px0grid_(0), py0grid_(0), skippedLeft_(false)
{
	memset(&prog, 0, sizeof(prog));
}
PacketIter::~PacketIter()
{
	if(comps)
	{
		for(uint16_t compno = 0; compno < numcomps; compno++)
			delete[](comps + compno)->resolutions;
		delete[] comps;
	}
	delete[] precinctInfo_;
}
void PacketIter::init(PacketManager* packetMan, TileCodingParams* tcp)
{
	packetManager = packetMan;
	maxNumDecompositionResolutions =
		packetManager->getTileProcessor()->getMaxNumDecompressResolutions();
	singleProgression_ = packetManager->getNumProgressions() == 1;
	auto image = packetMan->getImage();
	comps = new PiComp[image->numcomps];
	numcomps = image->numcomps;
	for(uint16_t compno = 0; compno < numcomps; ++compno)
	{
		auto img_comp = image->comps + compno;
		auto comp = comps + compno;
		auto tccp = tcp->tccps + compno;
		comp->resolutions = new PiResolution[tccp->numresolutions];
		comp->numresolutions = tccp->numresolutions;
		comp->dx = img_comp->dx;
		comp->dy = img_comp->dy;
	}
}
/***
 * Generate and cache precinct info
 *
 * Assumptions: single progression, no subsampling and tile origin at (0,0)
 */
void PacketIter::genPrecinctInfo(void)
{
	if(prog.progression != GRK_RPCL && prog.progression != GRK_PCRL && prog.progression != GRK_CPRL)
		return;

	if (!singleProgression_)
		return;

	auto tb = packetManager->getTileBounds();
	// tile origin at (0,0) will simplify computations
	if(tb.x0 || tb.y0)
	{
		return;
	}
	uint16_t maxResolutions = 0;
	for(uint16_t compno = 0; compno < numcomps; ++compno)
	{
		auto comp = comps + compno;
		if(comp->dx != 1 || comp->dy != 1)
			return;
		if(maxResolutions < comp->numresolutions)
			maxResolutions = comp->numresolutions;
	}
	precinctInfo_ = new ResPrecinctInfo[maxResolutions];
	for(uint8_t resno = 0; resno < maxResolutions; ++resno)
	{
		auto inf = precinctInfo_ + resno;
		auto res = comps->resolutions + resno;
		inf->precinctWidthExp = res->precinctWidthExp;
		inf->precinctHeightExp = res->precinctHeightExp;
		inf->init((uint8_t)(comps->numresolutions - 1U - resno), tb, comps->dx, comps->dy,
				  !packetManager->getTileProcessor()->wholeTileDecompress,
				  packetManager->getTileProcessor()->getUnreducedTileWindow());
	}
	optimized = prog.progression == GRK_RPCL;
}
bool PacketIter::next_cprl(void)
{
	if(compno >= numcomps)
	{
		GRK_ERROR("Packet iterator component %d must be strictly less than "
				  "total number of components %d",
				  compno, numcomps);
		return false;
	}
	for(; compno < prog.compE; compno++)
	{
		auto comp = comps + compno;
		dx = 0;
		dy = 0;
		update_dxy_for_comp(comp);
		for(; y < prog.ty1; y += dy - (y % dy))
		{
			for(; x < prog.tx1; x += dx - (x % dx))
			{
				for(; resno < prog.resE; resno++)
				{
					if(!generatePrecinctIndex())
						continue;
					if(incrementInner)
						layno++;
					if(layno < prog.layE)
					{
						incrementInner = true;
						if(update_include())
							return true;
					}
					layno = prog.layS;
					incrementInner = false;
				}
				resno = prog.resS;
			}
			x = prog.tx0;
		}
		y = prog.ty0;
	}

	return false;
}
bool PacketIter::next_pcrl(void)
{
	if(compno >= numcomps)
	{
		GRK_ERROR("Packet iterator component %d must be strictly less than "
				  "total number of components %d",
				  compno, numcomps);
		return false;
	}
	for(; y < prog.ty1; y += dy - (y % dy))
	{
		for(; x < prog.tx1; x += dx - (x % dx))
		{
			// windowed decode:
			// bail out if we reach a precinct which is past the
			// bottom, right hand corner of the tile window
			if(singleProgression_)
			{
				auto win = packetManager->getTileProcessor()->getUnreducedTileWindow();
				if(win.non_empty() &&
				   (y >= win.y1 || (win.y1 > 0 && y == win.y1 - 1 && x >= win.x1)))
					return false;
			}
			for(; compno < prog.compE; compno++)
			{
				for(; resno < prog.resE; resno++)
				{
					if(!generatePrecinctIndex())
						continue;
					if(incrementInner)
						layno++;
					if(layno < prog.layE)
					{
						incrementInner = true;
						if(update_include())
							return true;
					}
					layno = prog.layS;
					incrementInner = false;
				}
				resno = prog.resS;
			}
			compno = prog.compS;
		}
		x = prog.tx0;
	}

	return false;
}
bool PacketIter::next_lrcp(void)
{
	for(; layno < prog.layE; layno++)
	{
		if(singleProgression_)
		{
			auto maxLayer =
				packetManager->getTileProcessor()->getTileCodingParams()->numLayersToDecompress;
			if(maxLayer > 0 && layno >= maxLayer)
				return false;
		}
		for(; resno < prog.resE; resno++)
		{
			uint64_t precE = 0;
			if(precinctInfo_)
			{
				if(resno >= comps->numresolutions)
					continue;
				auto res = comps->resolutions + resno;
				precE = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
			}
			for(; compno < prog.compE; compno++)
			{
				auto comp = comps + compno;
				if(!precinctInfo_)
				{
					// skip resolutions greater than current component resolution
					if(resno >= comp->numresolutions)
						continue;
					auto res = comp->resolutions + resno;
					precE = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
				}
				if(incrementInner)
					precinctIndex++;
				if(precinctIndex < precE)
				{
					incrementInner = true;
					if(update_include())
						return true;
				}
				precinctIndex = prog.precS;
				incrementInner = false;
			}
			compno = prog.compS;
		}
		resno = prog.resS;
	}

	return false;
}
bool PacketIter::next_rlcp(void)
{
	if(compno >= numcomps)
	{
		GRK_ERROR("Packet iterator component %d must be strictly less than "
				  "total number of components %d",
				  compno, numcomps);
		return false;
	}
	for(; resno < prog.resE; resno++)
	{
		if(singleProgression_)
		{
			if(resno >= maxNumDecompositionResolutions)
				return false;
		}
		uint64_t precE = 0;
		if(precinctInfo_)
		{
			if(resno >= comps->numresolutions)
				continue;
			auto res = comps->resolutions + resno;
			precE = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
		}
		for(; layno < prog.layE; layno++)
		{
			for(; compno < prog.compE; compno++)
			{
				auto comp = comps + compno;
				if(!precinctInfo_)
				{
					if(resno >= comp->numresolutions)
						continue;
					auto res = comp->resolutions + resno;
					precE = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
				}
				if(incrementInner)
					precinctIndex++;
				if(precinctIndex < precE)
				{
					incrementInner = true;
					if(update_include())
						return true;
				}
				precinctIndex = prog.precS;
				incrementInner = false;
			}
			compno = prog.compS;
		}
		layno = prog.layS;
	}

	return false;
}
bool PacketIter::next_rpcl(SparseBuffer* src)
{
	if(precinctInfo_)
		return next_rpclOPT(src);

	for(; resno < prog.resE; resno++)
	{
		if(singleProgression_ && resno >= maxNumDecompositionResolutions)
			return false;

		// if all remaining components have degenerate precinct grid, then
		// skip this resolution
		bool sane = false;
		for(uint16_t compnoTmp = compno; compnoTmp < prog.compE; compnoTmp++)
		{
			auto comp = comps + compnoTmp;
			if(resno >= comp->numresolutions)
				continue;
			auto res = comp->resolutions + resno;
			if(res->precinctGridWidth > 0 && res->precinctGridHeight > 0)
			{
				sane = true;
				break;
			}
		}
		if(!sane)
			continue;

		for(; y < prog.ty1; y += dy - (y % dy))
		{
			// calculate y
			for(; x < prog.tx1; x += dx - (x % dx))
			{
				// calculate x
				for(; compno < prog.compE; compno++)
				{
					if(!generatePrecinctIndex())
						continue;
					if(incrementInner)
						layno++;
					if(layno < prog.layE)
					{
						incrementInner = true;
						if(update_include())
							return true;
					}
					layno = prog.layS;
					incrementInner = false;
				}
				compno = prog.compS;
			}
			x = prog.tx0;
		}
		y = prog.ty0;
	}

	return false;
}
bool PacketIter::next_rpclOPT(SparseBuffer* src)
{
	for(; resno < prog.resE; resno++)
	{
		if(resno >= maxNumDecompositionResolutions)
			return false;

		auto precInfo = precinctInfo_ + resno;
		if(!precInfoCheck(precInfo))
			continue;

		for(; y < prog.ty1; y += precInfo->canvasPrecHeight)
		{
			auto wholeTile = packetManager->getTileProcessor()->wholeTileDecompress;
			auto markers = packetManager->getTileProcessor()->packetLengthCache.getMarkers();

			auto win = packetManager->getTileProcessor()->getUnreducedTileWindow();
			// windowed decode:
			// bail out if we reach a precinct which is past the
			// bottom of the tile window
			if(resno == maxNumDecompositionResolutions - 1 && y >= win.y1)
				return false;

			// skip over packets outside of window
			auto precWin = &precInfo->window;
			auto res = comps->resolutions + resno;
			if (!wholeTile && src){
				// skip all precincts above window
				if (y < precInfo->windowPrec.y0){
					auto len = markers->pop(precInfo->windowPrecGrid.y0 * res->precinctGridWidth * prog.compE * prog.layE);
					auto skipLen = src->skip(len);
					if (len != skipLen)
						return false;
					y = precInfo->windowPrec.y0;
				}

				// skip precincts below window
				if (y >= precInfo->windowPrec.y1){
					auto len = markers->pop(res->precinctGridWidth * prog.compE * prog.layE);
					auto skipLen = src->skip(len);
					if (len != skipLen)
						return false;
					continue;
				}

				// skip precincts to the left of window
				if (!skippedLeft_) {
					if (x < precInfo->windowPrec.x0){
						auto len = markers->pop(precInfo->windowPrecGrid.x0 * prog.compE * prog.layE);
						auto skipLen = src->skip(len);
						if (len != skipLen)
							return false;
						x = precInfo->windowPrec.x0;
					}
					skippedLeft_ = true;
				}
			}

			genPrecinctY0GridOPT(precInfo);
			uint64_t precIndexY = (uint64_t)py0grid_ * res->precinctGridWidth;
			for(; x < prog.tx1; x += precInfo->canvasPrecWidth)
			{
				// windowed decode:
				// bail out if we reach a precinct which is past the
				// bottom, right hand corner of the tile window
				if(resno == maxNumDecompositionResolutions - 1)
				{
					if(win.y1 > 0 && y == win.y1 - 1 && x > win.x1)
						return false;
				}

				// skip packets to the right of window and break;

				valid = true;
				if (!wholeTile)
					valid =  x <= precWin->x1 && x + precInfo->canvasPrecWidth >= precWin->x0;
				if (valid)
					genPrecinctX0GridOPT(precInfo);
				for(; compno < prog.compE; compno++)
				{
					if(incrementInner)
						layno++;
					if(layno < prog.layE)
					{
						incrementInner = true;
						precinctIndex = px0grid_ + precIndexY;
						return true;
					}
					layno = prog.layS;
					incrementInner = false;
				}
				compno = prog.compS;
			}
			x = 0;
			skippedLeft_ = false;
		}
		y = 0;
	}

	return false;
}
bool PacketIter::next(SparseBuffer* src)
{
	switch(prog.progression)
	{
		case GRK_LRCP:
			return next_lrcp();
		case GRK_RLCP:
			return next_rlcp();
		case GRK_RPCL:
			return next_rpcl(src);
		case GRK_PCRL:
			return next_pcrl();
		case GRK_CPRL:
			return next_cprl();
		default:
			return false;
	}

	return false;
}
bool PacketIter::generatePrecinctIndex(void)
{
	auto comp = comps + compno;
	if(resno >= comp->numresolutions)
		return false;
	auto res = comp->resolutions + resno;
	if(res->precinctGridWidth == 0 || res->precinctGridHeight == 0)
		return false;

	if(precinctInfo_)
	{
		auto rpInfo = precinctInfo_ + resno;
		if(!rpInfo->valid)
			return false;
		if(!genPrecinctY0Grid(rpInfo))
			return false;
		if(!genPrecinctX0Grid(rpInfo))
			return false;
	}
	else
	{
		ResPrecinctInfo rpInfo;
		rpInfo.precinctWidthExp = res->precinctWidthExp;
		rpInfo.precinctHeightExp = res->precinctHeightExp;
		rpInfo.init((uint8_t)(comp->numresolutions - 1U - resno), packetManager->getTileBounds(),
					comp->dx, comp->dy, !packetManager->getTileProcessor()->wholeTileDecompress,
					packetManager->getTileProcessor()->getUnreducedTileWindow());

		if(!rpInfo.valid)
			return false;
		if(!genPrecinctY0Grid(&rpInfo))
			return false;
		if(!genPrecinctX0Grid(&rpInfo))
			return false;
	}
	precinctIndex = px0grid_ + (uint64_t)py0grid_ * res->precinctGridWidth;

	return true;
}
bool PacketIter::precInfoCheck(ResPrecinctInfo* rpInfo)
{
	if(!rpInfo->valid)
		return false;
	if(resno >= comps->numresolutions)
		return false;
	auto res = comps->resolutions + resno;

	return (res->precinctGridWidth > 0 && res->precinctGridHeight > 0);
}

bool PacketIter::genPrecinctY0Grid(ResPrecinctInfo* rpInfo)
{
	if(!(((uint64_t)y % rpInfo->canvasPrecHeight == 0) ||
		 ((y == packetManager->getTileBounds().y0) && rpInfo->canvasResOffsetY0)))
		return false;

	py0grid_ =
		floordivpow2(ceildiv<uint64_t>((uint64_t)y, rpInfo->canvasDy), rpInfo->precinctHeightExp) -
		rpInfo->canvasResInPrecGridY0;

	return true;
}
bool PacketIter::genPrecinctX0Grid(ResPrecinctInfo* rpInfo)
{
	if(!(((uint64_t)x % rpInfo->canvasPrecWidth == 0) ||
		 ((x == packetManager->getTileBounds().x0) && rpInfo->canvasResOffsetX0)))
		return false;

	px0grid_ = floordivpow2(ceildiv<uint64_t>((uint64_t)x, rpInfo->canvasDx), rpInfo->precinctWidthExp) -
			   rpInfo->canvasResInPrecGridX0;

	return true;
}
void PacketIter::genPrecinctY0GridOPT(ResPrecinctInfo* rpInfo)
{
	py0grid_ = ceildiv<uint64_t>((uint64_t)y, rpInfo->canvasDy) >> rpInfo->precinctHeightExp;
}
void PacketIter::genPrecinctX0GridOPT(ResPrecinctInfo* rpInfo)
{
	px0grid_ = ceildiv<uint64_t>((uint64_t)x, rpInfo->canvasDx) >> rpInfo->precinctWidthExp;

}
void PacketIter::update_dxy(void)
{
	dx = 0;
	dy = 0;
	for(uint16_t compno = 0; compno < numcomps; compno++)
		update_dxy_for_comp(comps + compno);
}
void PacketIter::update_dxy_for_comp(PiComp* comp)
{
	for(uint32_t resno = 0; resno < comp->numresolutions; resno++)
	{
		auto res = comp->resolutions + resno;
		uint64_t dx_temp =
			comp->dx * ((uint64_t)1u << (res->precinctWidthExp + comp->numresolutions - 1 - resno));
		uint64_t dy_temp =
			comp->dy *
			((uint64_t)1u << (res->precinctHeightExp + comp->numresolutions - 1 - resno));
		if(dx_temp < UINT_MAX)
			dx = !dx ? (uint32_t)dx_temp : std::min<uint32_t>(dx, (uint32_t)dx_temp);
		if(dy_temp < UINT_MAX)
			dy = !dy ? (uint32_t)dy_temp : std::min<uint32_t>(dy, (uint32_t)dy_temp);
	}
}
uint8_t* PacketIter::get_include(uint16_t layerno)
{
	return packetManager->getIncludeTracker()->get_include(layerno, resno);
}
bool PacketIter::update_include(void)
{
	if(singleProgression_)
		return true;
	return packetManager->getIncludeTracker()->update(layno, resno, compno, precinctIndex);
}
void PacketIter::destroy_include(void)
{
	packetManager->getIncludeTracker()->clear();
}

} // namespace grk
