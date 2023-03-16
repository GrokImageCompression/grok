/*
 *    Copyright (C) 2016-2023 Grok Image Compression Inc.
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
	: precWidthExp(0), precHeightExp(0), precWidthExpPRJ(0), precHeightExpPRJ(0), resOffsetX0PRJ(0),
	  resOffsetY0PRJ(0), precWidthPRJ(0), precWidthPRJMinusOne(0), precHeightPRJ(0),
	  precHeightPRJMinusOne(0), numPrecincts_(0), dxPRJ(0), dyPRJ(0), resInPrecGridX0(0),
	  resInPrecGridY0(0), resno_(0), decompLevel_(0), innerPrecincts_(0), winPrecinctsLeft_(0),
	  winPrecinctsRight_(0), winPrecinctsTop_(0), winPrecinctsBottom_(0), valid(false)
{}
bool ResPrecinctInfo::init(uint8_t resno, uint8_t decompLevel, grk_rect32 tileBounds,
						   uint32_t compDx, uint32_t compDy, bool windowed, grk_rect32 tileWindow)
{
	valid = false;
	resno_ = resno;
	decompLevel_ = decompLevel;

	uint64_t resDivisorX = (uint64_t)compDx << decompLevel;
	uint64_t resDivisorY = (uint64_t)compDy << decompLevel;
	auto res = tileBounds.scaleDownCeil(resDivisorX, resDivisorY);
	if(res.x0 == res.x1 || res.y0 == res.y1)
		return false;

	precWidthExpPRJ = precWidthExp + decompLevel_;
	precHeightExpPRJ = precHeightExp + decompLevel_;

	// offset of projected resolution relative to projected precinct grid
	// (these are both zero when tile origin equals (0,0))
	resOffsetX0PRJ =
		(uint32_t)(((uint64_t)res.x0 << decompLevel) % ((uint64_t)1 << precWidthExpPRJ));
	resOffsetY0PRJ =
		(uint32_t)(((uint64_t)res.y0 << decompLevel) % ((uint64_t)1 << precHeightExpPRJ));

	precWidthPRJ = (uint64_t)compDx << precWidthExpPRJ;
	precWidthPRJMinusOne = precWidthPRJ - 1;
	precHeightPRJ = (uint64_t)compDy << precHeightExpPRJ;
	precHeightPRJMinusOne = precHeightPRJ - 1;
	dxPRJ = (uint64_t)compDx << decompLevel_;
	dyPRJ = (uint64_t)compDy << decompLevel_;
	resInPrecGridX0 = floordivpow2(res.x0, precWidthExp);
	resInPrecGridY0 = floordivpow2(res.y0, precHeightExp);
	if(windowed)
	{
		auto window = tileWindow;
		auto resWindow = window.scaleDownCeil(resDivisorX, resDivisorY);
		// pad resolution window to next precinct
		resWindow.grow_IN_PLACE(1 << precWidthExp, 1 << precHeightExp).clip_IN_PLACE(res);
		winPrecGrid = resWindow.scaleDown(1 << precWidthExp, 1 << precHeightExp);
		winPrecPRJ = winPrecGrid.scale((uint32_t)precWidthPRJ, (uint32_t)precHeightPRJ);
	}

	tileBoundsPrecGrid = res.scaleDown(1 << precWidthExp, 1 << precHeightExp);
	numPrecincts_ = tileBoundsPrecGrid.area();
	tileBoundsPrecPRJ = tileBoundsPrecGrid.scale((uint32_t)precWidthPRJ, (uint32_t)precHeightPRJ);
	valid = true;

	return true;
}
void ResPrecinctInfo::print(void)
{
	GRK_INFO("\n");
	GRK_INFO("RESOLUTION PRECINCT INFO for resolution level %u", resno_);
	GRK_INFO("precinct exponents: (%u,%u)", precWidthExp, precHeightExp);
	GRK_INFO("precinct dimensions (projected): (%u,%u)", precWidthPRJ, precHeightPRJ);
	GRK_INFO("number of precincts: %u", numPrecincts_);
	GRK_INFO("subsampling (projected): (%u,%u)", dxPRJ, dyPRJ);
	GRK_INFO("tile bounds aligned to precincts (projected) =>");
	tileBoundsPrecPRJ.print();
	GRK_INFO("tile bounds mapped to precinct grid (resolution) =>");
	tileBoundsPrecGrid.print();
	GRK_INFO("window bounds aligned to precincts (projected) =>");
	winPrecPRJ.print();
	GRK_INFO("window bounds mapped to precinct grid (resolution) =>");
	winPrecGrid.print();
}

PacketIter::PacketIter()
	: compno(0), resno(0), precinctIndex(0), layno(0), numcomps(0), comps(nullptr), x(0), y(0),
	  dx(0), dy(0), dxActive(0), dyActive(0), incrementInner(false), packetManager(nullptr),
	  maxNumDecompositionResolutions(0), singleProgression_(false), compression_(false),
	  precinctInfoOPT_(nullptr), px0grid_(0), py0grid_(0), skippedLeft_(false)
{
	memset(&prog, 0, sizeof(prog));
}
PacketIter::~PacketIter()
{
	if(comps)
	{
		delete[] comps;
	}
	delete[] precinctInfoOPT_;
}
void PacketIter::printStaticState(void)
{
	if(precinctInfoOPT_)
	{
		GRK_INFO("Packet Iterator Static State");
		GRK_INFO("progression bounds [C-R-P-L] : [%u %u %u %u] ", prog.compE, prog.resE, prog.precE,
				 prog.layE);
		for(uint32_t resno = 0; resno < comps->numresolutions; resno++)
		{
			auto inf = precinctInfoOPT_ + resno;
			inf->print();
		}
	}
}
void PacketIter::printDynamicState(void)
{
	if(precinctInfoOPT_)
	{
		GRK_INFO("Packet Iterator Dynamic State");
		GRK_INFO("progression state [C-R-P-L] : [%u %u (%u,%u) %u] ", compno, resno, x, y, layno);
		GRK_INFO("precinct index: %" PRIu64 ".", precinctIndex);
	}
}

void PacketIter::genPrecinctInfo()
{
	if(!genPrecinctInfoOPT())
	{
		for(uint16_t c = 0; c < numcomps; ++c)
		{
			auto comp = comps + c;
			for(uint8_t r = 0; r < comp->numresolutions; ++r)
			{
				auto res = comp->resolutions + r;
				genPrecinctInfo(comp, res, r);
			}
		}
	}
}
void PacketIter::genPrecinctInfo(PiComp* comp, PiResolution* res, uint8_t resNumber)
{
	if(res->precinctGridWidth == 0 || res->precinctGridHeight == 0)
		return;

	if(compression_)
		return;

	ResPrecinctInfo* rpInfo = new ResPrecinctInfo();
	rpInfo->precWidthExp = res->precWidthExp;
	rpInfo->precHeightExp = res->precHeightExp;
	if(rpInfo->init(resNumber, (uint8_t)(comp->numresolutions - 1U - resNumber),
					packetManager->getTileBounds(), comp->dx, comp->dy, !isWholeTile(),
					packetManager->getTileProcessor()->getUnreducedTileWindow()))
	{
		res->precinctInfo = rpInfo;
	}
	else
	{
		delete rpInfo;
	}
}

/***
 * Generate and cache precinct info
 *
 * Assumptions: decompression, single progression, no subsampling,
 * constant number of resolutions across components,
 * non-decreasing projected precinct size as resolution decreases (CPRL and PCRL),
 * and tile origin at (0,0)
 */
bool PacketIter::genPrecinctInfoOPT(void)
{
	if(compression_ || !singleProgression_)
		return false;

	auto tb = packetManager->getTileBounds();
	// tile origin at (0,0) will simplify computations
	if(tb.x0 || tb.y0)
		return false;
	// no subsampling
	for(uint16_t compno = 0; compno < numcomps; ++compno)
	{
		auto comp = comps + compno;
		if(comp->dx != 1 || comp->dy != 1)
			return false;
		if(compno > 0 && comp->numresolutions != comps->numresolutions)
			return false;
	}
	switch(prog.progression)
	{
		case GRK_LRCP:
		case GRK_RLCP:
		case GRK_RPCL:
			break;
		case GRK_PCRL:
		case GRK_CPRL:
			// if P occurs before R, then we must ensure that for all resolutions, the precinct
			// projected onto canvas is a "multiple" of the highest resolution precinct,
			// so that the P loops covers all precincts from all resolutions
			{
				auto highestRes = comps->resolutions + comps->numresolutions - 1;
				for(uint8_t resno = 0; resno < comps->numresolutions - 1; ++resno)
				{
					auto res = comps->resolutions + resno;
					auto decompLevel = (uint8_t)(comps->numresolutions - 1U - resno);
					if(res->precWidthExp + decompLevel < highestRes->precWidthExp ||
					   res->precHeightExp + decompLevel < highestRes->precHeightExp)
						return false;
				}
			}
			break;
		default:
			break;
	}

	precinctInfoOPT_ = new ResPrecinctInfo[comps->numresolutions];
	for(uint8_t resno = 0; resno < comps->numresolutions; ++resno)
	{
		auto inf = precinctInfoOPT_ + resno;
		auto res = comps->resolutions + resno;
		inf->precWidthExp = res->precWidthExp;
		inf->precHeightExp = res->precHeightExp;
		inf->init(resno, (uint8_t)(comps->numresolutions - 1U - resno), tb, comps->dx, comps->dy,
				  !isWholeTile(), packetManager->getTileProcessor()->getUnreducedTileWindow());
	}

	return true;
}
bool PacketIter::validatePrecinct(void)
{
	auto comp = comps + compno;
	if(resno >= comp->numresolutions)
		return false;
	auto res = comp->resolutions + resno;
	if(res->precinctGridWidth == 0 || res->precinctGridHeight == 0)
		return false;

	if(precinctInfoOPT_)
	{
		auto rpInfo = precinctInfoOPT_ + resno;
		if(!rpInfo->valid)
			return false;
		if(!genPrecinctY0Grid(rpInfo))
			return false;
		if(!genPrecinctX0Grid(rpInfo))
			return false;
	}
	else
	{
		if(compression_)
		{
			ResPrecinctInfo rpInfo;
			rpInfo.precWidthExp = res->precWidthExp;
			rpInfo.precHeightExp = res->precHeightExp;
			if(!rpInfo.init(resno, (uint8_t)(comp->numresolutions - 1U - resno),
							packetManager->getTileBounds(), comp->dx, comp->dy, !isWholeTile(),
							packetManager->getTileProcessor()->getUnreducedTileWindow()))
			{
				return false;
			}
			if(!genPrecinctY0Grid(&rpInfo))
				return false;
			if(!genPrecinctX0Grid(&rpInfo))
				return false;
		}
		else
		{
			auto rpInfo = res->precinctInfo;
			if(!rpInfo)
				return false;
			if(!genPrecinctY0Grid(rpInfo))
				return false;
			if(!genPrecinctX0Grid(rpInfo))
				return false;
		}
	}
	return true;
}
void PacketIter::generatePrecinctIndex(void)
{
	auto comp = comps + compno;
	auto res = comp->resolutions + resno;
	precinctIndex = px0grid_ + (uint64_t)py0grid_ * res->precinctGridWidth;
}

/**
 * Check if there is a remaining valid progression order
 */
bool PacketIter::checkForRemainingValidProgression(int32_t prog, uint32_t pino,
												   const char* progString)
{
	auto tcps =
		packetManager->getCodingParams()->tcps + packetManager->getTileProcessor()->getIndex();
	auto poc = tcps->progressionOrderChange + pino;

	if(prog >= 0)
	{
		switch(progString[prog])
		{
			case 'R':
				if(poc->res_temp == poc->tpResE)
					return checkForRemainingValidProgression(prog - 1, pino, progString);
				else
					return true;
				break;
			case 'C':
				if(poc->comp_temp == poc->tpCompE)
					return checkForRemainingValidProgression(prog - 1, pino, progString);
				else
					return true;
				break;
			case 'L':
				if(poc->lay_temp == poc->tpLayE)
					return checkForRemainingValidProgression(prog - 1, pino, progString);
				else
					return true;
				break;
			case 'P':
				switch(poc->progression)
				{
					case GRK_LRCP:
						[[fallthrough]];
					case GRK_RLCP:
						if(poc->prec_temp == poc->tpPrecE)
							return checkForRemainingValidProgression(prog - 1, pino, progString);
						else
							return true;
						break;
					default:
						if(poc->tx0_temp == poc->tp_txE)
						{
							/*TY*/
							if(poc->ty0_temp == poc->tp_tyE)
								return checkForRemainingValidProgression(prog - 1, pino,
																		 progString);
							else
								return true;
							/*TY*/
						}
						else
						{
							return true;
						}
						break;
				}
		}
	}
	return false;
}
void PacketIter::enableTilePartGeneration(uint32_t pino, bool first_poc_tile_part,
										  uint32_t newTilePartProgressionPosition)
{
	auto cp = packetManager->getCodingParams();
	auto tcp = cp->tcps + packetManager->getTileProcessor()->getIndex();
	auto poc = tcp->progressionOrderChange + pino;
	auto pocProg = CodeStreamCompress::convertProgressionOrder(poc->progression);
	prog.progression = poc->progression;

	if(cp->coding_params_.enc_.enableTilePartGeneration_ &&
	   (GRK_IS_CINEMA(cp->rsiz) || GRK_IS_IMF(cp->rsiz) ||
		packetManager->getT2Mode() == FINAL_PASS))
	{
		for(uint32_t i = newTilePartProgressionPosition + 1; i < 4; i++)
		{
			switch(pocProg[i])
			{
				case 'R':
					prog.resS = poc->tpResS;
					prog.resE = poc->tpResE;
					break;
				case 'C':
					prog.compS = poc->tpCompS;
					prog.compE = poc->tpCompE;
					break;
				case 'L':
					prog.layS = 0;
					prog.layE = poc->tpLayE;
					break;
				case 'P':
					switch(poc->progression)
					{
						case GRK_LRCP:
						case GRK_RLCP:
							prog.precS = 0;
							prog.precE = poc->tpPrecE;
							break;
						default:
							prog.tx0 = poc->tp_txS;
							prog.ty0 = poc->tp_tyS;
							prog.tx1 = poc->tp_txE;
							prog.ty1 = poc->tp_tyE;
							break;
					}
					break;
			}
		}
		if(first_poc_tile_part)
		{
			for(int32_t i = (int32_t)newTilePartProgressionPosition; i >= 0; i--)
			{
				switch(pocProg[i])
				{
					case 'C':
						poc->comp_temp = poc->tpCompS;
						prog.compS = poc->comp_temp;
						prog.compE = poc->comp_temp + 1U;
						poc->comp_temp = poc->comp_temp + 1U;
						break;
					case 'R':
						poc->res_temp = poc->tpResS;
						prog.resS = poc->res_temp;
						prog.resE = poc->res_temp + 1U;
						poc->res_temp = poc->res_temp + 1U;
						break;
					case 'L':
						poc->lay_temp = 0;
						prog.layS = poc->lay_temp;
						prog.layE = poc->lay_temp + 1U;
						poc->lay_temp = poc->lay_temp + 1U;
						break;
					case 'P':
						switch(poc->progression)
						{
							case GRK_LRCP:
							case GRK_RLCP:
								poc->prec_temp = 0;
								prog.precS = poc->prec_temp;
								prog.precE = poc->prec_temp + 1U;
								poc->prec_temp += 1;
								break;
							default:
								poc->tx0_temp = poc->tp_txS;
								poc->ty0_temp = poc->tp_tyS;
								prog.tx0 = poc->tx0_temp;
								prog.tx1 = (poc->tx0_temp + poc->dx - (poc->tx0_temp % poc->dx));
								prog.ty0 = poc->ty0_temp;
								prog.ty1 = (poc->ty0_temp + poc->dy - (poc->ty0_temp % poc->dy));
								poc->tx0_temp = prog.tx1;
								poc->ty0_temp = prog.ty1;
								break;
						}
						break;
				}
			}
		}
		else
		{
			uint32_t incr_top = 1;
			uint32_t resetX = 0;
			for(int32_t i = (int32_t)newTilePartProgressionPosition; i >= 0; i--)
			{
				switch(pocProg[i])
				{
					case 'C':
						prog.compS = uint16_t(poc->comp_temp - 1);
						prog.compE = poc->comp_temp;
						break;
					case 'R':
						prog.resS = uint8_t(poc->res_temp - 1);
						prog.resE = poc->res_temp;
						break;
					case 'L':
						prog.layS = uint16_t(poc->lay_temp - 1);
						prog.layE = poc->lay_temp;
						break;
					case 'P':
						switch(poc->progression)
						{
							case GRK_LRCP:
							case GRK_RLCP:
								prog.precS = poc->prec_temp - 1;
								prog.precE = poc->prec_temp;
								break;
							default:
								prog.tx0 = (poc->tx0_temp - poc->dx - (poc->tx0_temp % poc->dx));
								prog.tx1 = poc->tx0_temp;
								prog.ty0 = (poc->ty0_temp - poc->dy - (poc->ty0_temp % poc->dy));
								prog.ty1 = poc->ty0_temp;
								break;
						}
						break;
				}
				if(incr_top == 1)
				{
					switch(pocProg[i])
					{
						case 'R':
							if(poc->res_temp == poc->tpResE)
							{
								if(checkForRemainingValidProgression(i - 1, pino, pocProg))
								{
									poc->res_temp = poc->tpResS;
									prog.resS = poc->res_temp;
									prog.resE = poc->res_temp + 1U;
									poc->res_temp = poc->res_temp + 1U;
									incr_top = 1;
								}
								else
								{
									incr_top = 0;
								}
							}
							else
							{
								prog.resS = poc->res_temp;
								prog.resE = poc->res_temp + 1U;
								poc->res_temp = poc->res_temp + 1U;
								incr_top = 0;
							}
							break;
						case 'C':
							if(poc->comp_temp == poc->tpCompE)
							{
								if(checkForRemainingValidProgression(i - 1, pino, pocProg))
								{
									poc->comp_temp = poc->tpCompS;
									prog.compS = poc->comp_temp;
									prog.compE = poc->comp_temp + 1U;
									poc->comp_temp = poc->comp_temp + 1U;
									incr_top = 1;
								}
								else
								{
									incr_top = 0;
								}
							}
							else
							{
								prog.compS = poc->comp_temp;
								prog.compE = poc->comp_temp + 1U;
								poc->comp_temp = poc->comp_temp + 1U;
								incr_top = 0;
							}
							break;
						case 'L':
							if(poc->lay_temp == poc->tpLayE)
							{
								if(checkForRemainingValidProgression(i - 1, pino, pocProg))
								{
									poc->lay_temp = 0;
									prog.layS = poc->lay_temp;
									prog.layE = poc->lay_temp + 1U;
									poc->lay_temp = poc->lay_temp + 1U;
									incr_top = 1;
								}
								else
								{
									incr_top = 0;
								}
							}
							else
							{
								prog.layS = poc->lay_temp;
								prog.layE = poc->lay_temp + 1U;
								poc->lay_temp = poc->lay_temp + 1U;
								incr_top = 0;
							}
							break;
						case 'P':
							switch(poc->progression)
							{
								case GRK_LRCP:
								case GRK_RLCP:
									if(poc->prec_temp == poc->tpPrecE)
									{
										if(checkForRemainingValidProgression(i - 1, pino, pocProg))
										{
											poc->prec_temp = 0;
											prog.precS = poc->prec_temp;
											prog.precE = poc->prec_temp + 1;
											poc->prec_temp += 1;
											incr_top = 1;
										}
										else
										{
											incr_top = 0;
										}
									}
									else
									{
										prog.precS = poc->prec_temp;
										prog.precE = poc->prec_temp + 1;
										poc->prec_temp += 1;
										incr_top = 0;
									}
									break;
								default:
									if(poc->tx0_temp >= poc->tp_txE)
									{
										if(poc->ty0_temp >= poc->tp_tyE)
										{
											if(checkForRemainingValidProgression(i - 1, pino,
																				 pocProg))
											{
												poc->ty0_temp = poc->tp_tyS;
												prog.ty0 = poc->ty0_temp;
												prog.ty1 = (uint32_t)(poc->ty0_temp + poc->dy -
																	  (poc->ty0_temp % poc->dy));
												poc->ty0_temp = prog.ty1;
												incr_top = 1;
												resetX = 1;
											}
											else
											{
												incr_top = 0;
												resetX = 0;
											}
										}
										else
										{
											prog.ty0 = poc->ty0_temp;
											prog.ty1 = (poc->ty0_temp + poc->dy -
														(poc->ty0_temp % poc->dy));
											poc->ty0_temp = prog.ty1;
											incr_top = 0;
											resetX = 1;
										}
										if(resetX == 1)
										{
											poc->tx0_temp = poc->tp_txS;
											prog.tx0 = poc->tx0_temp;
											prog.tx1 = (uint32_t)(poc->tx0_temp + poc->dx -
																  (poc->tx0_temp % poc->dx));
											poc->tx0_temp = prog.tx1;
										}
									}
									else
									{
										prog.tx0 = poc->tx0_temp;
										prog.tx1 = (uint32_t)(poc->tx0_temp + poc->dx -
															  (poc->tx0_temp % poc->dx));
										poc->tx0_temp = prog.tx1;
										incr_top = 0;
									}
									break;
							}
							break;
					}
				}
			}
		}
	}
	else
	{
		prog.layS = 0;
		prog.layE = poc->tpLayE;
		prog.resS = poc->tpResS;
		prog.resE = poc->tpResE;
		prog.compS = poc->tpCompS;
		prog.compE = poc->tpCompE;
		prog.precS = 0;
		prog.precE = poc->tpPrecE;
		prog.tx0 = poc->tp_txS;
		prog.ty0 = poc->tp_tyS;
		prog.tx1 = poc->tp_txE;
		prog.ty1 = poc->tp_tyE;
	}
}
GRK_PROG_ORDER PacketIter::getProgression(void) const
{
	return prog.progression;
}
uint16_t PacketIter::getCompno(void) const
{
	return compno;
}
uint8_t PacketIter::getResno(void) const
{
	return resno;
}
uint64_t PacketIter::getPrecinctIndex(void) const
{
	return precinctIndex;
}
uint16_t PacketIter::getLayno(void) const
{
	return layno;
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
	if(!((y % rpInfo->precHeightPRJ == 0) ||
		 ((y == packetManager->getTileBounds().y0) && rpInfo->resOffsetY0PRJ)))
		return false;

	py0grid_ =
		floordivpow2(ceildiv(y, rpInfo->dyPRJ), rpInfo->precHeightExp) - rpInfo->resInPrecGridY0;

	return true;
}
bool PacketIter::genPrecinctX0Grid(ResPrecinctInfo* rpInfo)
{
	if(!((x % rpInfo->precWidthPRJ == 0) ||
		 ((x == packetManager->getTileBounds().x0) && rpInfo->resOffsetX0PRJ)))
		return false;

	px0grid_ =
		floordivpow2(ceildiv(x, rpInfo->dxPRJ), rpInfo->precWidthExp) - rpInfo->resInPrecGridX0;

	return true;
}

bool PacketIter::genPrecinctY0GridPCRL_OPT(ResPrecinctInfo* rpInfo)
{
	if((((y & rpInfo->precHeightPRJMinusOne) != 0)))
		return false;

	py0grid_ = ((uint32_t)ceildivpow2(y, (uint32_t)rpInfo->decompLevel_) >> rpInfo->precHeightExp) -
			   rpInfo->resInPrecGridY0;
	return true;
}
bool PacketIter::genPrecinctX0GridPCRL_OPT(ResPrecinctInfo* rpInfo)
{
	if((((x & rpInfo->precWidthPRJMinusOne) != 0)))
		return false;

	px0grid_ = ((uint32_t)ceildivpow2(x, (uint32_t)rpInfo->decompLevel_) >> rpInfo->precWidthExp) -
			   rpInfo->resInPrecGridX0;
	return true;
}
void PacketIter::genPrecinctY0GridRPCL_OPT(ResPrecinctInfo* rpInfo)
{
	py0grid_ = (uint32_t)(ceildivpow2(y, rpInfo->decompLevel_) >> rpInfo->precHeightExp);
}
void PacketIter::genPrecinctX0GridRPCL_OPT(ResPrecinctInfo* rpInfo)
{
	px0grid_ = (uint32_t)(ceildivpow2(x, rpInfo->decompLevel_) >> rpInfo->precWidthExp);
}
void PacketIter::update_dxy(void)
{
	dx = 0;
	dy = 0;
	for(uint16_t compno = 0; compno < numcomps; compno++)
		update_dxy_for_comp(comps + compno, false);
	dxActive = (uint32_t)(dx - (x % dx));
	dyActive = (uint32_t)(dy - (y % dy));
}
void PacketIter::update_dxy_for_comp(PiComp* comp, bool updateActive)
{
	for(uint32_t resno = 0; resno < comp->numresolutions; resno++)
	{
		auto res = comp->resolutions + resno;
		uint64_t dx_temp =
			comp->dx * ((uint64_t)1u << (res->precWidthExp + comp->numresolutions - 1 - resno));
		uint64_t dy_temp =
			comp->dy * ((uint64_t)1u << (res->precHeightExp + comp->numresolutions - 1 - resno));
		if(dx_temp < UINT_MAX)
			dx = !dx ? (uint32_t)dx_temp : std::min<uint32_t>(dx, (uint32_t)dx_temp);
		if(dy_temp < UINT_MAX)
			dy = !dy ? (uint32_t)dy_temp : std::min<uint32_t>(dy, (uint32_t)dy_temp);
	}
	if(updateActive)
	{
		dxActive = (uint32_t)(dx - (x % dx));
		dyActive = (uint32_t)(dy - (y % dy));
	}
}
void PacketIter::init(PacketManager* packetMan, uint32_t pino, TileCodingParams* tcp,
					  grk_rect32 tileBounds, bool compression, uint8_t max_res,
					  uint64_t max_precincts, uint32_t* resolutionPrecinctGrid,
					  uint32_t** precinctByComponent)
{
	packetManager = packetMan;
	maxNumDecompositionResolutions =
		packetManager->getTileProcessor()->getMaxNumDecompressResolutions();
	singleProgression_ = packetManager->getNumProgressions() == 1;
	compression_ = compression;
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
	bool hasPoc = tcp->hasPoc();
	if(!compression)
	{
		auto poc = tcp->progressionOrderChange + pino;

		prog.progression = hasPoc ? poc->progression : tcp->prg;
		prog.layS = 0;
		prog.layE = hasPoc ? std::min<uint16_t>(poc->layE, tcp->max_layers_) : tcp->max_layers_;
		prog.resS = hasPoc ? poc->resS : 0;
		prog.resE = hasPoc ? poc->resE : max_res;
		prog.compS = hasPoc ? poc->compS : 0;
		prog.compE = std::min<uint16_t>(hasPoc ? poc->compE : numcomps, image->numcomps);
		prog.precS = 0;
		prog.precE = max_precincts;
	}
	prog.tx0 = tileBounds.x0;
	prog.ty0 = tileBounds.y0;
	prog.tx1 = tileBounds.x1;
	prog.ty1 = tileBounds.y1;
	x = prog.tx0;
	y = prog.ty0;

	// generate precinct grids
	for(uint16_t compno = 0; compno < numcomps; ++compno)
	{
		auto current_comp = comps + compno;
		resolutionPrecinctGrid = precinctByComponent[compno];
		/* resolutions have already been initialized */
		for(uint32_t resno = 0; resno < current_comp->numresolutions; resno++)
		{
			auto res = current_comp->resolutions + resno;

			res->precWidthExp = *(resolutionPrecinctGrid++);
			res->precHeightExp = *(resolutionPrecinctGrid++);
			res->precinctGridWidth = *(resolutionPrecinctGrid++);
			res->precinctGridHeight = *(resolutionPrecinctGrid++);
		}
	}
	genPrecinctInfo();
	update_dxy();

	if(singleProgression_)
	{
		switch(prog.progression)
		{
			case GRK_LRCP:
				prog.layE = (std::min)(prog.layE, packetManager->getTileProcessor()
													  ->getTileCodingParams()
													  ->numLayersToDecompress);
				break;
			case GRK_RLCP:
				prog.resE = (std::min)(prog.resE, maxNumDecompositionResolutions);
				break;
			case GRK_RPCL:
				prog.resE = (std::min)(prog.resE, maxNumDecompositionResolutions);
				if(precinctInfoOPT_)
				{
					for(uint8_t resno = 0; resno < comps->numresolutions; ++resno)
					{
						auto inf = precinctInfoOPT_ + resno;
						inf->innerPrecincts_ = prog.compE * prog.layE;
						auto compLayer = inf->innerPrecincts_;
						inf->winPrecinctsLeft_ = inf->winPrecGrid.x0 * compLayer;
						inf->winPrecinctsRight_ =
							(uint64_t)(inf->tileBoundsPrecGrid.x1 - inf->winPrecGrid.x1) *
							compLayer;
						inf->winPrecinctsTop_ = (uint64_t)inf->winPrecGrid.y0 *
												inf->tileBoundsPrecGrid.width() * compLayer;
						inf->winPrecinctsBottom_ =
							(uint64_t)(inf->tileBoundsPrecGrid.y1 - inf->winPrecGrid.y1) *
							inf->tileBoundsPrecGrid.width() * compLayer;
					}
				}
				break;
			case GRK_PCRL:
				break;
			case GRK_CPRL:
				break;
			default:
				break;
		}
	}
}
bool PacketIter::isWholeTile(void)
{
	return compression_ || packetManager->getTileProcessor()->cp_->wholeTileDecompress_;
}
bool PacketIter::next(SparseBuffer* src)
{
	switch(prog.progression)
	{
		case GRK_LRCP:
			return next_lrcp(src);
		case GRK_RLCP:
			return next_rlcp(src);
		case GRK_RPCL:
			return next_rpcl(src);
		case GRK_PCRL:
			return next_pcrl(src);
		case GRK_CPRL:
			return next_cprl(src);
		default:
			return false;
	}

	return false;
}

bool PacketIter::next_cprl(SparseBuffer* src)
{
	if(precinctInfoOPT_)
		return next_cprlOPT(src);
	for(; compno < prog.compE; compno++)
	{
		auto comp = comps + compno;
		for(; y < prog.ty1; y += dyActive, dyActive = dy)
		{
			for(; x < prog.tx1; x += dxActive, dxActive = dx)
			{
				for(; resno < prog.resE; resno++)
				{
					if(!validatePrecinct())
						continue;
					if(incrementInner)
						layno++;
					if(layno < prog.layE)
					{
						incrementInner = true;
						generatePrecinctIndex();
						if(update_include())
						{
							return true;
						}
					}
					layno = prog.layS;
					incrementInner = false;
				}
				resno = prog.resS;
			}
			x = prog.tx0;
			dxActive = (uint32_t)(dx - (x % dx));
		}
		y = prog.ty0;
		dx = 0;
		dy = 0;
		update_dxy_for_comp(comp, true);
	}

	return false;
}
bool PacketIter::next_pcrl(SparseBuffer* src)
{
	if(precinctInfoOPT_)
		return next_pcrlOPT(src);
	for(; y < prog.ty1; y += dyActive, dyActive = dy)
	{
		for(; x < prog.tx1; x += dxActive, dxActive = dx)
		{
			// windowed decode:
			// bail out if we reach a precinct which is past the
			// bottom, right hand corner of the tile window
			if(singleProgression_)
			{
				auto win = packetManager->getTileProcessor()->getUnreducedTileWindow();
				if(!win.empty() && (y >= win.y1 || (win.y1 > 0 && y == win.y1 - 1 && x >= win.x1)))
					return false;
			}
			for(; compno < prog.compE; compno++)
			{
				for(; resno < prog.resE; resno++)
				{
					if(!validatePrecinct())
						continue;
					if(incrementInner)
						layno++;
					if(layno < prog.layE)
					{
						incrementInner = true;
						generatePrecinctIndex();
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
		dxActive = (uint32_t)(dx - (x % dx));
	}

	return false;
}
bool PacketIter::next_lrcp(SparseBuffer* src)
{
	if(precinctInfoOPT_)
		return next_lrcpOPT(src);

	for(; layno < prog.layE; layno++)
	{
		for(; resno < prog.resE; resno++)
		{
			uint64_t precE = 0;
			if(precinctInfoOPT_)
			{
				if(resno >= comps->numresolutions)
					continue;
				auto res = comps->resolutions + resno;
				precE = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
			}
			for(; compno < prog.compE; compno++)
			{
				auto comp = comps + compno;
				if(!precinctInfoOPT_)
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
bool PacketIter::next_rlcp(SparseBuffer* src)
{
	if(precinctInfoOPT_)
		return next_rlcpOPT(src);
	for(; resno < prog.resE; resno++)
	{
		uint64_t precE = 0;
		if(precinctInfoOPT_)
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
				if(!precinctInfoOPT_)
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
	if(precinctInfoOPT_)
		return next_rpclOPT(src);

	for(; resno < prog.resE; resno++)
	{
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

		for(; y < prog.ty1; y += dyActive, dyActive = dy)
		{
			for(; x < prog.tx1; x += dxActive, dxActive = dx)
			{
				// calculate x
				for(; compno < prog.compE; compno++)
				{
					if(!validatePrecinct())
						continue;
					if(incrementInner)
						layno++;
					if(layno < prog.layE)
					{
						incrementInner = true;
						generatePrecinctIndex();
						if(update_include())
							return true;
					}
					layno = prog.layS;
					incrementInner = false;
				}
				compno = prog.compS;
			}
			x = prog.tx0;
			dxActive = (uint32_t)(dx - (x % dx));
		}
		y = prog.ty0;
		dyActive = (uint32_t)(dy - (y % dy));
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////////////

bool PacketIter::skipPackets(SparseBuffer* src, uint64_t numPackets)
{
	auto tp = packetManager->getTileProcessor();
	auto markers = tp->packetLengthCache.getMarkers();
	auto len = markers->pop(numPackets);
	auto skipLen = src->skip(len);
	if(len != skipLen)
	{
		GRK_ERROR("Packet iterator: unable to skip precincts.");
		return false;
	}
	tp->incNumProcessedPackets(numPackets);

	return true;
}
bool PacketIter::next_lrcpOPT([[maybe_unused]] SparseBuffer* src)
{
	for(; layno < prog.layE; layno++)
	{
		for(; resno < prog.resE; resno++)
		{
			auto precInfo = precinctInfoOPT_ + resno;
			if(!precInfoCheck(precInfo))
				continue;

			uint64_t precE = precInfo->numPrecincts_;
			for(; compno < prog.compE; compno++)
			{
				if(incrementInner)
					precinctIndex++;
				if(precinctIndex < precE)
				{
					incrementInner = true;
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
bool PacketIter::next_rlcpOPT([[maybe_unused]] SparseBuffer* src)
{
	for(; resno < prog.resE; resno++)
	{
		auto precInfo = precinctInfoOPT_ + resno;
		if(!precInfoCheck(precInfo))
			continue;

		uint64_t precE = precInfo->numPrecincts_;
		for(; layno < prog.layE; layno++)
		{
			for(; compno < prog.compE; compno++)
			{
				if(incrementInner)
					precinctIndex++;
				if(precinctIndex < precE)
				{
					incrementInner = true;
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

bool PacketIter::next_cprlOPT(SparseBuffer* src)
{
	auto wholeTile = isWholeTile();
	auto precInfo = precinctInfoOPT_ + prog.resE - 1;
	if(!precInfoCheck(precInfo))
		return false;
	auto win = &precInfo->winPrecPRJ;
	for(; compno < prog.compE; compno++)
	{
		// note: no need to update dx and dy here since all components
		// have the same number of resolutions and subsampling factors
		for(; y < precInfo->tileBoundsPrecPRJ.y1; y += dy)
		{
			// skip over packets outside of window
			if(!wholeTile)
			{
				if(y == win->y1)
				{
					// bail out if we reach row of precincts that are out of bound of the window
					if(compno == prog.compE - 1)
						return false;

					// otherwise, skip remaining precincts for this component
					if(src)
					{
						uint64_t precCount = 0;
						for(uint8_t i = 0; i < prog.resE; ++i)
						{
							auto info = precinctInfoOPT_ + i;
							auto reg = grk_rect<uint64_t>(0U, y, precInfo->tileBoundsPrecPRJ.x1,
														  precInfo->tileBoundsPrecPRJ.y1);
							reg = reg.scaleDownCeilPow2((uint32_t)info->precWidthExpPRJ,
														(uint32_t)info->precHeightExpPRJ);
							precCount += reg.area();
						}
						precCount *= prog.layE;
						if(!skipPackets(src, precCount))
							return false;
						break;
					}
				}
			}
			for(; x < precInfo->tileBoundsPrecPRJ.x1; x += dx)
			{
				for(; resno < prog.resE; resno++)
				{
					auto comp = comps + compno;
					auto res = comp->resolutions + resno;
					auto rpInfo = precinctInfoOPT_ + resno;
					if(!rpInfo->valid)
						continue;
					if(!genPrecinctY0GridPCRL_OPT(rpInfo))
						continue;
					if(!genPrecinctX0GridPCRL_OPT(rpInfo))
						continue;
					precinctIndex = px0grid_ + (uint64_t)py0grid_ * res->precinctGridWidth;
					if(incrementInner)
						layno++;
					if(layno < prog.layE)
					{
						incrementInner = true;
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
bool PacketIter::next_pcrlOPT([[maybe_unused]] SparseBuffer* src)
{
	auto wholeTile = isWholeTile();
	auto precInfo = precinctInfoOPT_ + prog.resE - 1;
	if(!precInfoCheck(precInfo))
		return false;
	auto win = &precInfo->winPrecPRJ;
	for(; y < precInfo->tileBoundsPrecPRJ.y1; y += dy)
	{
		// skip over packets outside of window
		if(!wholeTile)
		{
			// bail out if we reach row of precincts that are out of bounds of the window
			if(y == win->y1)
				return false;
		}
		for(; x < precInfo->tileBoundsPrecPRJ.x1; x += dx)
		{
			// windowed decode:
			// bail out if we reach a precinct which is past the
			// bottom, right hand corner of the tile window
			if(!wholeTile)
			{
				if((y >= win->y1 || (win->y1 > 0 && y == win->y1 - 1 && x >= win->x1)))
					return false;
			}
			for(; compno < prog.compE; compno++)
			{
				for(; resno < prog.resE; resno++)
				{
					auto comp = comps + compno;
					auto res = comp->resolutions + resno;
					auto rpInfo = precinctInfoOPT_ + resno;
					if(!rpInfo->valid)
						continue;
					if(!genPrecinctY0GridPCRL_OPT(rpInfo))
						continue;
					if(!genPrecinctX0GridPCRL_OPT(rpInfo))
						continue;
					precinctIndex = px0grid_ + (uint64_t)py0grid_ * res->precinctGridWidth;
					if(incrementInner)
						layno++;
					if(layno < prog.layE)
					{
						incrementInner = true;
						return true;
					}
					layno = prog.layS;
					incrementInner = false;
				}
				resno = prog.resS;
			}
			compno = prog.compS;
		}
		x = precInfo->tileBoundsPrecPRJ.x0;
	}

	return false;
}
bool PacketIter::next_rpclOPT(SparseBuffer* src)
{
	auto wholeTile = isWholeTile();
	for(; resno < prog.resE; resno++)
	{
		auto precInfo = precinctInfoOPT_ + resno;
		if(!precInfoCheck(precInfo))
			continue;
		auto win = &precInfo->winPrecPRJ;
		for(; y < precInfo->tileBoundsPrecPRJ.y1; y += precInfo->precHeightPRJ)
		{
			// skip over packets outside of window
			if(!wholeTile)
			{
				// windowed decode:
				// bail out if we reach row of precincts that are out of bound of the window
				if(resno == maxNumDecompositionResolutions - 1 && y == win->y1)
					return false;

				if(src)
				{
					// skip all precincts above window
					if(y < win->y0)
					{
						if(!skipPackets(src, precInfo->winPrecinctsTop_))
							return false;
						y = win->y0;
					}
					// skip all precincts below window
					else if(y == win->y1 && precInfo->winPrecinctsBottom_)
					{
						if(!skipPackets(src, precInfo->winPrecinctsBottom_))
							return false;
						break;
					}
					// skip precincts to the left of window
					if(!skippedLeft_ && precInfo->winPrecinctsLeft_)
					{
						if(x < win->x0)
						{
							if(!skipPackets(src, precInfo->winPrecinctsLeft_))
								return false;
							x = win->x0;
						}
						skippedLeft_ = true;
					}
				}
			}
			genPrecinctY0GridRPCL_OPT(precInfo);
			uint64_t precIndexY = (uint64_t)py0grid_ * precInfo->tileBoundsPrecGrid.width();
			for(; x < (wholeTile || !src ? precInfo->tileBoundsPrecPRJ.x1 : win->x1);
				x += precInfo->precWidthPRJ)
			{
				// windowed decode:
				// bail out if we reach a precinct which is past the
				// bottom, right hand corner of the tile window
				if(!wholeTile && resno == maxNumDecompositionResolutions - 1)
				{
					if((win->y1 == 0 || (win->y1 > 0 && y == win->y1 - 1)) && x >= win->x1)
						return false;
				}
				genPrecinctX0GridRPCL_OPT(precInfo);
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
			x = precInfo->tileBoundsPrecPRJ.x0;
			// skip precincts to the right of window
			if(!wholeTile && src && precInfo->winPrecinctsRight_)
			{
				if(!skipPackets(src, precInfo->winPrecinctsRight_))
					return false;
			}
			skippedLeft_ = false;
		}
		y = precInfo->tileBoundsPrecPRJ.y0;
	}

	return false;
}

} // namespace grk
