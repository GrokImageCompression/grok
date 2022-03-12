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

ResPrecinctInfo::ResPrecinctInfo() : precinctWidthExp(0),
					precinctHeightExp(0),
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
void ResPrecinctInfo::init(uint8_t decompLevel,
							grkRectU32 tileBounds,
							uint32_t dx,
							uint32_t dy){
	valid = false;
	grkRectU32 resBounds(ceildiv<uint64_t>((uint64_t)tileBounds.x0, ((uint64_t)dx << decompLevel)),
						 ceildiv<uint64_t>((uint64_t)tileBounds.y0, ((uint64_t)dy << decompLevel)),
						 ceildiv<uint64_t>((uint64_t)tileBounds.x1, ((uint64_t)dx << decompLevel)),
						 ceildiv<uint64_t>((uint64_t)tileBounds.y1, ((uint64_t)dy << decompLevel)));
	if(resBounds.x0 == resBounds.x1 || resBounds.y0 == resBounds.y1)
		return;
	uint32_t rpxshift  = precinctWidthExp + decompLevel;
	uint32_t rpyshift  = precinctHeightExp + decompLevel;
	rpx0 = (uint32_t)(((uint64_t)resBounds.x0 << decompLevel) % ((uint64_t)1 << rpxshift));
	rpy0 = (uint32_t)(((uint64_t)resBounds.y0 << decompLevel) % ((uint64_t)1 << rpyshift));
	rpdx = ((uint64_t)dx << rpxshift);
	rpdy = ((uint64_t)dy << rpyshift);
	rdx  = ((uint64_t)dx << decompLevel);
	rdy  = ((uint64_t)dy << decompLevel);
	px0 = floordivpow2(resBounds.x0, precinctWidthExp);
	py0 = floordivpow2(resBounds.y0, precinctHeightExp);
	valid = true;
}

PacketIter::PacketIter()
	: step_l(0), step_r(0), step_c(0), step_p(0), compno(0),
	  resno(0), precinctIndex(0), layno(0), numcomps(0), comps(nullptr),
	  x(0), y(0), dx(0), dy(0), incrementInner(false), packetManager(nullptr),
	  maxNumDecompositionResolutions(0), singleProgression_(false), precinctInfo_(nullptr),
	  px0grid_(0), py0grid_(0)
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
void PacketIter::init(PacketManager* packetMan,
					TileCodingParams* tcp)
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
void PacketIter::genPrecinctInfo(void){
     if (prog.progression != GRK_RPCL &&
    		 prog.progression != GRK_PCRL &&
			 	 prog.progression != GRK_CPRL)
    	 return;

    auto tb = packetManager->getTileBounds();
    // tile origin at (0,0) will simplify computations
    if (tb.x0 || tb.y0) {
    	return;
    }
	bool fixedSubsamplingAcrossComponents = true;
	uint16_t maxResolutions = 0;
	for(uint16_t compno = 0; compno < numcomps; ++compno)
	{
		auto comp = comps + compno;
		auto comp0 = comps;
		if (compno > 0) {
			 if (comp->dx != comp0->dx || comp->dy != comp0->dy)
				 fixedSubsamplingAcrossComponents = false;
		}
		 if (maxResolutions < comp->numresolutions)
			 maxResolutions = comp->numresolutions;
	}
	if (fixedSubsamplingAcrossComponents){
		precinctInfo_ = new ResPrecinctInfo[maxResolutions];
		auto tb = packetManager->getTileBounds();
		for (uint8_t resno = 0; resno < maxResolutions; ++resno){
			auto inf = precinctInfo_ + resno;
			auto res = comps->resolutions + resno;
			inf->precinctWidthExp = res->precinctWidthExp;
			inf->precinctHeightExp = res->precinctHeightExp;
			inf->init((uint8_t)(comps->numresolutions - 1U - resno), tb, comps->dx, comps->dy);
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
			if (precinctInfo_){
				if(resno >= comps->numresolutions)
					continue;
				auto res = comps->resolutions + resno;
				precE = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
			}
			for(; compno < prog.compE; compno++)
			{
				auto comp = comps + compno;
				if (!precinctInfo_){
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
		if (precinctInfo_){
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
				if (!precinctInfo_){
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
bool PacketIter::next_rpcl(void)
{
	if (precinctInfo_)
		return next_rpclOPT();

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
bool PacketIter::next_rpclOPT(void)
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

		auto precInfo = precinctInfo_ + resno;
		auto res = comps->resolutions + resno;
		if (!genPrecinctResCheck(precInfo))
			continue;
		auto win = packetManager->getTileProcessor()->getUnreducedTileWindow();
		for(; y < prog.ty1; y += precInfo->rpdy)
		{
			genPrecinctY0GridOPT(precInfo);
			uint64_t precIndexY = (uint64_t)py0grid_ * res->precinctGridWidth;
			for(; x < prog.tx1; x += precInfo->rpdx)
			{
				// windowed decode:
				// bail out if we reach a precinct which is past the
				// bottom, right hand corner of the tile window
				if(singleProgression_ && resno == maxNumDecompositionResolutions - 1)
				{
					if(win.non_empty() &&
					   (y >= win.y1 || (win.y1 > 0 && y > win.y1 - 1 && x > win.x1))){
						return false;
					}
				}
				genPrecinctX0GridOPT(precInfo);
				for(; compno < prog.compE; compno++)
				{
					if(incrementInner)
						layno++;
					if(layno < prog.layE)
					{
						incrementInner = true;
						if(update_include()){
							precinctIndex = px0grid_ + precIndexY;
							return true;
						}
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
bool PacketIter::generatePrecinctIndex(void)
{
	auto comp = comps + compno;
	if(resno >= comp->numresolutions)
		return false;
	auto res = comp->resolutions + resno;
	if(res->precinctGridWidth == 0 || res->precinctGridHeight == 0)
		return false;

	if (precinctInfo_) {
		auto rpInfo = precinctInfo_ + resno;
		if (!rpInfo->valid)
			return false;
		if (!genPrecinctY0Grid(rpInfo))
			return false;
		if (!genPrecinctX0Grid(rpInfo))
			return false;
	} else {
		ResPrecinctInfo rpInfo;
		rpInfo.precinctWidthExp = res->precinctWidthExp;
		rpInfo.precinctHeightExp = res->precinctHeightExp;
		rpInfo.init((uint8_t)(comp->numresolutions - 1U - resno), packetManager->getTileBounds(), comp->dx, comp->dy);

		if (!rpInfo.valid)
			return false;
		if (!genPrecinctY0Grid(&rpInfo))
			return false;
		if (!genPrecinctX0Grid(&rpInfo))
			return false;
	}
	precinctIndex = px0grid_ + (uint64_t)py0grid_ * res->precinctGridWidth;

	return true;
}
bool PacketIter::genPrecinctResCheck(ResPrecinctInfo *rpInfo){
	if (!rpInfo->valid)
		return false;
	if(resno >= comps->numresolutions)
		return false;
	auto res = comps->resolutions + resno;

	return  (res->precinctGridWidth > 0 && res->precinctGridHeight > 0);
}

bool PacketIter::genPrecinctY0Grid(ResPrecinctInfo *rpInfo){
	if(!(((uint64_t)y % rpInfo->rpdy == 0) || ((y == packetManager->getTileBounds().y0) && rpInfo->rpy0)) )
		return false;

	py0grid_ = floordivpow2(ceildiv<uint64_t>((uint64_t)y, rpInfo->rdy), rpInfo->precinctHeightExp) - rpInfo->py0;

	return true;
}
bool PacketIter::genPrecinctX0Grid(ResPrecinctInfo *rpInfo){
	if(!(((uint64_t)x % rpInfo->rpdx == 0) || ((x == packetManager->getTileBounds().x0) && rpInfo->rpx0)) )
		return false;

	px0grid_ = floordivpow2(ceildiv<uint64_t>((uint64_t)x, rpInfo->rdx), rpInfo->precinctWidthExp) - rpInfo->px0;

	return true;
}
void PacketIter::genPrecinctY0GridOPT(ResPrecinctInfo *rpInfo){
	py0grid_ = floordivpow2(ceildiv<uint64_t>((uint64_t)y, rpInfo->rdy), rpInfo->precinctHeightExp);
}
void PacketIter::genPrecinctX0GridOPT(ResPrecinctInfo *rpInfo){
	px0grid_ = floordivpow2(ceildiv<uint64_t>((uint64_t)x, rpInfo->rdx), rpInfo->precinctWidthExp);
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
