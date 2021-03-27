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

namespace grk {

/** @defgroup PI PI - Implementation of a packet iterator */
/*@{*/

PacketIter::PacketIter() : tp_on(false),
							step_l(0),
							step_r(0),
							step_c(0),
							step_p(0),
							compno(0),
							resno(0),
							precinctIndex(0),
							layno(0),
							numcomps(0),
							comps(nullptr),
							tx0(0),
							ty0(0),
							tx1(0),
							ty1(0),
							x(0),
							y(0),
							dx(0),
							dy(0),
							handledFirstInner(false),
							packetManager(nullptr)
{
	memset(&prog, 0, sizeof(prog));
}
PacketIter::~PacketIter(){
	if (comps) {
		for (uint32_t compno = 0; compno < numcomps; compno++)
			delete[] (comps + compno)->resolutions;
		delete[] comps;
	}
}
bool PacketIter::next_cprl(void) {
	if (compno >= numcomps){
		GRK_ERROR("Packet iterator component %d must be strictly less than "
				"total number of components %d",compno , numcomps);
		return false;
	}
	for (; compno < prog.compE; compno++) {
		auto comp = comps + compno;
		dx = 0;
		dy = 0;
		update_dxy_for_comp(comp);
		for (; y < prog.ty1;	y += dy - (y % dy)) {
			for (; x < prog.tx1;	x += dx - (x % dx)) {
				for (; resno < std::min<uint32_t>(prog.resE, comp->numresolutions); resno++) {
					if (!generatePrecinctIndex())
						continue;
					if (handledFirstInner)
						layno++;
					if (layno < prog.layE) {
						handledFirstInner = true;
						if (update_include())
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
bool PacketIter::next_pcrl(void) {
	if (compno >= numcomps){
		GRK_ERROR("Packet iterator component %d must be strictly less than "
				"total number of components %d",compno , numcomps);
		return false;
	}
	//auto b = packetManager->getTileProcessor()->tile->
	// ToDo: if windowed with single progression order, then bail
	// after we are outside the bottom right hand
	// corner of the padded window, expanded to sit in the precinct grid
	for (; y < prog.ty1;	y += dy - (y % dy)) {
		for (; x < prog.tx1;	x += dx - (x % dx)) {
			for (; compno < prog.compE; compno++) {
				auto comp = comps + compno;
				for (; resno< std::min<uint32_t>(prog.resE,comp->numresolutions); resno++) {
					if (!generatePrecinctIndex())
						continue;
					if (handledFirstInner)
						layno++;
					if (layno < prog.layE) {
						handledFirstInner = true;
						if (update_include())
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
bool PacketIter::next_lrcp(void) {
	for (; layno < prog.layE; layno++) {
		for (; resno < prog.resE;resno++) {
			for (; compno < prog.compE;compno++) {
				auto comp = comps + compno;
				//skip resolutions greater than current component resolution
				if (resno >= comp->numresolutions)
					continue;
				auto res = comp->resolutions + resno;
				auto precE = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
				if (tp_on)
					precE = std::min<uint64_t>(precE, prog.precE);
				if (handledFirstInner)
					precinctIndex++;
				if (precinctIndex < precE) {
					handledFirstInner = true;
					if (update_include())
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
bool PacketIter::next_rlcp(void) {
	if (compno >= numcomps){
		GRK_ERROR("Packet iterator component %d must be strictly less than "
				"total number of components %d",compno , numcomps);
		return false;
	}
	for (; resno < prog.resE; resno++) {
		for (; layno < prog.layE;	layno++) {
			for (; compno < prog.compE;compno++) {
				auto comp = comps + compno;
				if (resno >= comp->numresolutions)
					continue;
				auto res = comp->resolutions + resno;
				auto precE = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
				if (tp_on)
					precE = std::min<uint64_t>(precE, prog.precE);
				if (handledFirstInner)
					precinctIndex++;
				if (precinctIndex < precE) {
					handledFirstInner = true;
					if (update_include())
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
bool PacketIter::next_rpcl(void) {
	for (; resno < prog.resE; resno++) {
		for (; y < prog.ty1;	y += dy - (y % dy)) {
			for (; x < prog.tx1;	x += dx - (x % dx)) {
				for (; compno < prog.compE; compno++) {
					if (!generatePrecinctIndex())
						continue;
					if (handledFirstInner)
						layno++;
					if (layno < prog.layE) {
						handledFirstInner = true;
						if (update_include())
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
bool PacketIter::next(void) {
	switch (prog.progression) {
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
bool PacketIter::generatePrecinctIndex(void){
	if (compno >= numcomps){
		GRK_ERROR("Packet iterator component %d must be strictly less than "
				"total number of components %d",compno , numcomps);
		return false;
	}
	auto comp = comps + compno;
	if (resno >= comp->numresolutions)
		return false;

	auto res = comp->resolutions + resno;
	uint32_t levelno = comp->numresolutions - 1 - resno;
	assert(levelno < GRK_J2K_MAXRLVLS);
	if (levelno >= GRK_J2K_MAXRLVLS)
		return false;
	grkRectU32 resBounds(ceildiv<uint64_t>((uint64_t) tx0,((uint64_t) comp->dx << levelno)),
							ceildiv<uint64_t>((uint64_t) ty0,((uint64_t) comp->dy << levelno)),
							ceildiv<uint64_t>((uint64_t) tx1,((uint64_t) comp->dx << levelno)),
							 ceildiv<uint64_t>((uint64_t) ty1,((uint64_t) comp->dy << levelno)));
	uint32_t rpx = res->precinctWidthExp + levelno;
	uint32_t rpy = res->precinctHeightExp + levelno;
	if (!(((uint64_t) x % ((uint64_t) comp->dx << rpx) == 0)
			|| ((x == tx0)	&& (((uint64_t) resBounds.x0 << levelno) % ((uint64_t) 1 << rpx))))) {
		return false;
	}
	if (!(((uint64_t) y % ((uint64_t) comp->dy << rpy) == 0)
			|| ((y == ty0)	&& (((uint64_t) resBounds.y0 << levelno) % ((uint64_t) 1 << rpy))))) {
		return false;
	}
	if ((res->precinctGridWidth == 0) || (res->precinctGridHeight == 0))
		return false;
	if (resBounds.area() == 0)
		return false;
	uint32_t currPrecinctX0Grid = floordivpow2(ceildiv<uint64_t>((uint64_t) x,	((uint64_t) comp->dx << levelno)), res->precinctWidthExp)
									- floordivpow2(resBounds.x0, res->precinctWidthExp);
	uint32_t currPrecinctY0Grid = floordivpow2(	ceildiv<uint64_t>((uint64_t) y,	((uint64_t) comp->dy << levelno)), res->precinctHeightExp)
									- floordivpow2(resBounds.y0, res->precinctHeightExp);
	precinctIndex = (currPrecinctX0Grid + (uint64_t)currPrecinctY0Grid * res->precinctGridWidth);

	return true;
}
grkRectU32 PacketIter::generatePrecinct(uint64_t precinctIndex){
	auto comp = comps + compno;
	if (resno >= comp->numresolutions)
		return grkRectU32(0,0,0,0);
	auto res = comp->resolutions + resno;
	uint32_t levelno = comp->numresolutions - 1 - resno;
	assert(levelno < GRK_J2K_MAXRLVLS);
	if (levelno >= GRK_J2K_MAXRLVLS)
		return grkRectU32(0,0,0,0);
	grkRectU32 resBounds(ceildiv<uint64_t>((uint64_t) tx0,((uint64_t) comp->dx << levelno)),
							ceildiv<uint64_t>((uint64_t) ty0,((uint64_t) comp->dy << levelno)),
							ceildiv<uint64_t>((uint64_t) tx1,((uint64_t) comp->dx << levelno)),
							 ceildiv<uint64_t>((uint64_t) ty1,((uint64_t) comp->dy << levelno)));
	uint64_t xGrid = precinctIndex % res->precinctGridWidth;
	uint64_t yGrid = precinctIndex / res->precinctGridWidth;

	uint32_t x = (uint32_t)((xGrid + floordivpow2(resBounds.x0, res->precinctWidthExp)) << res->precinctWidthExp) << ((uint64_t) comp->dx << levelno);
	uint32_t y = (uint32_t)((yGrid + floordivpow2(resBounds.y0, res->precinctHeightExp)) << res->precinctHeightExp) << ((uint64_t) comp->dy << levelno);

	auto rc =  grkRectU32(x,
							y,
							x + (1 << res->precinctWidthExp) ,
							y + (1 << res->precinctHeightExp));
	rc.clip(&resBounds);

	return rc;
}
void PacketIter::update_dxy(void) {
	dx = 0;
	dy = 0;
	for (uint32_t compno = 0; compno < numcomps; compno++)
		update_dxy_for_comp(comps + compno);
}
void PacketIter::update_dxy_for_comp(PiComp *comp) {
	for (uint32_t resno = 0; resno < comp->numresolutions; resno++) {
		auto res = comp->resolutions + resno;
		uint64_t dx_temp = comp->dx	* ((uint64_t) 1u << (res->precinctWidthExp + comp->numresolutions - 1 - resno));
		uint64_t dy_temp = comp->dy	* ((uint64_t) 1u << (res->precinctHeightExp + comp->numresolutions - 1 - resno));
		if (dx_temp < UINT_MAX)
			dx = !dx ?	(uint32_t) dx_temp : std::min<uint32_t>(dx, (uint32_t) dx_temp);
		if (dy_temp < UINT_MAX)
			dy = !dy ?	(uint32_t) dy_temp : std::min<uint32_t>(dy, (uint32_t) dy_temp);
	}
}
uint8_t* PacketIter::get_include(uint16_t layerno){
	return packetManager->getIncludeTracker()->get_include(layerno, resno);
}
bool PacketIter::update_include(void){
	if (packetManager->getNumProgressions() == 1)
		return true;
	return packetManager->getIncludeTracker()->update(layno, resno, compno, precinctIndex);
}
void PacketIter::destroy_include(void){
	packetManager->getIncludeTracker()->clear();
}

}
