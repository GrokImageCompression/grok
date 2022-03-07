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
/** @defgroup PI PI - Implementation of a packet iterator */
/*@{*/

PacketIter::PacketIter()
	: enableTilePartGeneration(false), step_l(0), step_r(0), step_c(0), step_p(0), compno(0),
	  resno(0), precinctIndex(0), layno(0), numcomps(0), comps(nullptr), tx0(0), ty0(0), tx1(0),
	  ty1(0), x(0), y(0), dx(0), dy(0), handledFirstInner(false), packetManager(nullptr),
	  maxNumDecompositionResolutions(0), singleProgression_(false), precinctInfo_(nullptr),
	  px0grid_(0), px0gridFailed_(false), py0grid_(0), py0gridFailed_(false), resPrecinctFailed_(false)
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
void PacketIter::genPrecinctInfo(TileCodingParams* tcp){
     if (prog.progression != GRK_RPCL &&
    		 prog.progression != GRK_PCRL &&
			 	 prog.progression != GRK_CPRL)
    	 return;
	bool fixedNumResolutionsAcrossComponents = true;
	bool fixedSubsamplingAcrossComponents = true;
	for(uint16_t compno = 0; compno < numcomps; ++compno)
	{
		auto comp = comps + compno;
		auto comp0 = comps;
		if (compno > 0) {
			 if (comp->numresolutions != comp0->numresolutions)
				 fixedNumResolutionsAcrossComponents = false;
			 if (comp->dx != comp0->dx || comp->dy != comp0->dy)
				 fixedSubsamplingAcrossComponents = false;
		}
	}
	if (fixedNumResolutionsAcrossComponents && fixedSubsamplingAcrossComponents){
		precinctInfo_ = new ResPrecinctInfo[tcp->tccps->numresolutions];
		for (uint8_t resno = 0; resno < tcp->tccps->numresolutions; ++resno){
			auto inf = precinctInfo_ + resno;
			auto res = comps->resolutions + resno;
			inf->precinctWidthExp = res->precinctWidthExp;
			inf->precinctHeightExp = res->precinctHeightExp;
			inf->init((uint8_t)(comps->numresolutions - 1U - resno), tx0, ty0, tx1, ty1, comps->dx, comps->dy);
		}
	}
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
					if(handledFirstInner)
						layno++;
					if(layno < prog.layE)
					{
						handledFirstInner = true;
						if(update_include())
							return true;
					}
					layno = prog.layS;
					handledFirstInner = false;
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
					if(handledFirstInner)
						layno++;
					if(layno < prog.layE)
					{
						handledFirstInner = true;
						if(update_include())
							return true;
					}
					layno = prog.layS;
					handledFirstInner = false;
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
			for(; compno < prog.compE; compno++)
			{
				auto comp = comps + compno;
				// skip resolutions greater than current component resolution
				if(resno >= comp->numresolutions)
					continue;
				auto res = comp->resolutions + resno;
				auto precE = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
				if(enableTilePartGeneration)
					precE = std::min<uint64_t>(precE, prog.precE);
				if(handledFirstInner)
					precinctIndex++;
				if(precinctIndex < precE)
				{
					handledFirstInner = true;
					if(update_include())
						return true;
				}
				precinctIndex = prog.precS;
				handledFirstInner = false;
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
		for(; layno < prog.layE; layno++)
		{
			for(; compno < prog.compE; compno++)
			{
				auto comp = comps + compno;
				if(resno >= comp->numresolutions)
					continue;
				auto res = comp->resolutions + resno;
				auto precE = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
				if(enableTilePartGeneration)
					precE = std::min<uint64_t>(precE, prog.precE);
				if(handledFirstInner)
					precinctIndex++;
				if(precinctIndex < precE)
				{
					handledFirstInner = true;
					if(update_include())
						return true;
				}
				precinctIndex = prog.precS;
				handledFirstInner = false;
			}
			compno = prog.compS;
		}
		layno = prog.layS;
	}

	return false;
}
bool PacketIter::next_rpcl(void)
{
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
					if(handledFirstInner)
						layno++;
					if(layno < prog.layE)
					{
						handledFirstInner = true;
						if(update_include())
							return true;
					}
					layno = prog.layS;
					handledFirstInner = false;
				}
				compno = prog.compS;
			}
			x = prog.tx0;
		}
		y = prog.ty0;
	}

	return false;
}
bool PacketIter::next(void)
{
	switch(prog.progression)
	{
		case GRK_LRCP:
			return next_lrcp();
		case GRK_RLCP:
			return next_rlcp();
		case GRK_RPCL:
			return next_rpcl();
		case GRK_PCRL:
			return next_pcrl();
		case GRK_CPRL:
			return next_cprl();
		default:
			return false;
	}

	return false;
}


ResPrecinctInfo::ResPrecinctInfo() : precinctWidthExp(0),
					precinctHeightExp(0),
					rpxshift(0),
					rpyshift(0),
					rpx0(0),
					rpy0(0),
					rpdx(0),
					rpdy(0),
					rdx(0),
					rdy(0),
					px0(0),
					py0(0),
					valid(false)
{}
void ResPrecinctInfo::init(uint8_t levelno,
		uint32_t tx0,
		uint32_t ty0,
		uint32_t tx1,
		uint32_t ty1,
		uint32_t dx,
		uint32_t dy){
	valid = false;
	grkRectU32 resBounds(ceildiv<uint64_t>((uint64_t)tx0, ((uint64_t)dx << levelno)),
						 ceildiv<uint64_t>((uint64_t)ty0, ((uint64_t)dy << levelno)),
						 ceildiv<uint64_t>((uint64_t)tx1, ((uint64_t)dx << levelno)),
						 ceildiv<uint64_t>((uint64_t)ty1, ((uint64_t)dy << levelno)));
	if(resBounds.x0 == resBounds.x1 || resBounds.y0 == resBounds.y1)
		return;
	rpxshift  = precinctWidthExp + levelno;
	rpyshift  = precinctHeightExp + levelno;
	rpx0 = ((uint64_t)resBounds.x0 << levelno) % ((uint64_t)1 << rpxshift);
	rpy0 = ((uint64_t)resBounds.y0 << levelno) % ((uint64_t)1 << rpyshift);
	rpdx = ((uint64_t)dx << rpxshift);
	rpdy = ((uint64_t)dy << rpyshift);
	rdx  = ((uint64_t)dx << levelno);
	rdy  = ((uint64_t)dy << levelno);
	px0 = floordivpow2(resBounds.x0, precinctWidthExp);
	py0 = floordivpow2(resBounds.y0, precinctHeightExp);
	valid = true;
}

bool PacketIter::generatePrecinctIndex(void)
{
	return precinctInfo_ ? generatePrecinctIndexOpt() : generatePrecinctIndexUnopt();
}
bool PacketIter::generatePrecinctIndexUnopt(void)
{
	auto comp = comps + compno;
	if(resno >= comp->numresolutions)
		return false;
	auto res = comp->resolutions + resno;
	if(res->precinctGridWidth == 0 || res->precinctGridHeight == 0)
		return false;

	auto rpInfo = new ResPrecinctInfo();
	rpInfo->precinctWidthExp = res->precinctWidthExp;
	rpInfo->precinctHeightExp = res->precinctHeightExp;
	rpInfo->init((uint8_t)(comp->numresolutions - 1U - resno), tx0, ty0, tx1, ty1, comp->dx, comp->dy);
	if (!rpInfo->valid)
		return false;

	// generate index
	genPrecinctY0Grid(rpInfo);
	if (py0gridFailed_)
		return false;

	genPrecinctX0Grid(rpInfo);
	if (px0gridFailed_)
		return false;

	precinctIndex = (px0grid_ + (uint64_t)py0grid_ * res->precinctGridWidth);
	delete rpInfo;

	return true;
}
void PacketIter::genPrecinctResCheck(ResPrecinctInfo *rpInfo){
	resPrecinctFailed_ = false;
	if(resno >= comps->numresolutions) {
		resPrecinctFailed_ = true;
		return;
	}
	auto res = comps->resolutions + resno;
	if(res->precinctGridWidth == 0 || res->precinctGridHeight == 0) {
		resPrecinctFailed_ = true;
		return;
	}
	if (!rpInfo->valid) {
		resPrecinctFailed_ = true;
		return;
	}
}
bool PacketIter::generatePrecinctIndexOpt(void)
{
	auto rpInfo = precinctInfo_ + resno;
	genPrecinctResCheck(rpInfo);
	if (resPrecinctFailed_)
		return false;

	// generate index
	genPrecinctY0Grid(rpInfo);
	if (py0gridFailed_)
		return false;

	genPrecinctX0Grid(rpInfo);
	if (px0gridFailed_)
		return false;

	auto res = comps->resolutions + resno;
	precinctIndex = (px0grid_ + (uint64_t)py0grid_ * res->precinctGridWidth);

	return true;
}

void PacketIter::genPrecinctY0Grid(ResPrecinctInfo *rpInfo){
	py0gridFailed_ = false;
	// generate y condition
	if(!(((uint64_t)y % rpInfo->rpdy == 0) || ((y == ty0) && rpInfo->rpy0)) ){
		py0gridFailed_ = true;
	} else {
		// generate ygrid0
		py0grid_ = floordivpow2(ceildiv<uint64_t>((uint64_t)y, rpInfo->rdy), rpInfo->precinctHeightExp) - rpInfo->py0;
	}
}

void PacketIter::genPrecinctX0Grid(ResPrecinctInfo *rpInfo){
	px0gridFailed_ = false;
	// generate x condition
	if(!(((uint64_t)x % rpInfo->rpdx == 0) || ((x == tx0) && rpInfo->rpx0)) ) {
		px0gridFailed_ = true;
	} else {
		px0grid_ = floordivpow2(ceildiv<uint64_t>((uint64_t)x, rpInfo->rdx), rpInfo->precinctWidthExp) - rpInfo->px0;
	}
}


grkRectU32 PacketIter::generatePrecinct(uint64_t precinctIndex)
{
	auto comp = comps + compno;
	if(resno >= comp->numresolutions)
		return grkRectU32(0, 0, 0, 0);
	auto res = comp->resolutions + resno;
	uint8_t levelno = (uint8_t)(comp->numresolutions - 1U - resno);
	if(levelno >= GRK_J2K_MAXRLVLS)
		return grkRectU32(0, 0, 0, 0);
	grkRectU32 resBounds(ceildiv<uint64_t>((uint64_t)tx0, ((uint64_t)comp->dx << levelno)),
						 ceildiv<uint64_t>((uint64_t)ty0, ((uint64_t)comp->dy << levelno)),
						 ceildiv<uint64_t>((uint64_t)tx1, ((uint64_t)comp->dx << levelno)),
						 ceildiv<uint64_t>((uint64_t)ty1, ((uint64_t)comp->dy << levelno)));
	uint64_t xGrid = precinctIndex % res->precinctGridWidth;
	uint64_t yGrid = precinctIndex / res->precinctGridWidth;

	uint32_t x = (uint32_t)((xGrid + floordivpow2(resBounds.x0, res->precinctWidthExp))
							<< res->precinctWidthExp)
				 << ((uint64_t)comp->dx << levelno);
	uint32_t y = (uint32_t)((yGrid + floordivpow2(resBounds.y0, res->precinctHeightExp))
							<< res->precinctHeightExp)
				 << ((uint64_t)comp->dy << levelno);

	auto rc = grkRectU32(x, y, x + (1 << res->precinctWidthExp), y + (1 << res->precinctHeightExp));
	rc.clip(&resBounds);

	return rc;
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
