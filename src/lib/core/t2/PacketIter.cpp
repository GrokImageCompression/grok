/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
   Logger::logger_.info("\n");
   Logger::logger_.info("RESOLUTION PRECINCT INFO for resolution level %u", resno_);
   Logger::logger_.info("precinct exponents: (%u,%u)", precWidthExp, precHeightExp);
   Logger::logger_.info("precinct dimensions (projected): (%u,%u)", precWidthPRJ, precHeightPRJ);
   Logger::logger_.info("number of precincts: %u", numPrecincts_);
   Logger::logger_.info("subsampling (projected): (%u,%u)", dxPRJ, dyPRJ);
   Logger::logger_.info("tile bounds aligned to precincts (projected) =>");
   tileBoundsPrecPRJ.print();
   Logger::logger_.info("tile bounds mapped to precinct grid (resolution) =>");
   tileBoundsPrecGrid.print();
   Logger::logger_.info("window bounds aligned to precincts (projected) =>");
   winPrecPRJ.print();
   Logger::logger_.info("window bounds mapped to precinct grid (resolution) =>");
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
	  Logger::logger_.info("Packet Iterator Static State");
	  Logger::logger_.info("progression bounds [C-R-P-L] : [%u %u %u %u] ", prog.comp_e, prog.res_e,
						   prog.prec_e, prog.lay_e);
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
	  Logger::logger_.info("Packet Iterator Dynamic State");
	  Logger::logger_.info("progression state [C-R-P-L] : [%u %u (%u,%u) %u] ", compno, resno, x, y,
						   layno);
	  Logger::logger_.info("precinct index: %" PRIu64 ".", precinctIndex);
   }
}

void PacketIter::genPrecinctInfo()
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
			if(poc->res_temp == poc->tp_res_e)
			   return checkForRemainingValidProgression(prog - 1, pino, progString);
			else
			   return true;
			break;
		 case 'C':
			if(poc->comp_temp == poc->tp_comp_e)
			   return checkForRemainingValidProgression(prog - 1, pino, progString);
			else
			   return true;
			break;
		 case 'L':
			if(poc->lay_temp == poc->tp_lay_e)
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
				  if(poc->prec_temp == poc->tp_prec_e)
					 return checkForRemainingValidProgression(prog - 1, pino, progString);
				  else
					 return true;
				  break;
			   default:
				  if(poc->tx0_temp == poc->tp_tx_e)
				  {
					 /*TY*/
					 if(poc->ty0_temp == poc->tp_ty_e)
						return checkForRemainingValidProgression(prog - 1, pino, progString);
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
void PacketIter::enable_tile_part_generation(uint32_t pino, bool first_poc_tile_part,
											 uint32_t newTilePartProgressionPosition)
{
   auto cp = packetManager->getCodingParams();
   auto tcp = cp->tcps + packetManager->getTileProcessor()->getIndex();
   auto poc = tcp->progressionOrderChange + pino;
   auto pocProg = CodeStreamCompress::convertProgressionOrder(poc->progression);
   prog.progression = poc->progression;

   if(cp->coding_params_.enc_.enableTilePartGeneration_ &&
	  (GRK_IS_CINEMA(cp->rsiz) || GRK_IS_IMF(cp->rsiz) || packetManager->getT2Mode() == FINAL_PASS))
   {
	  for(uint32_t i = newTilePartProgressionPosition + 1; i < 4; i++)
	  {
		 switch(pocProg[i])
		 {
			case 'R':
			   prog.res_s = poc->tp_res_s;
			   prog.res_e = poc->tp_res_e;
			   break;
			case 'C':
			   prog.comp_s = poc->tp_comp_s;
			   prog.comp_e = poc->tp_comp_e;
			   break;
			case 'L':
			   prog.lay_s = 0;
			   prog.lay_e = poc->tp_lay_e;
			   break;
			case 'P':
			   switch(poc->progression)
			   {
				  case GRK_LRCP:
				  case GRK_RLCP:
					 prog.prec_s = 0;
					 prog.prec_e = poc->tp_prec_e;
					 break;
				  default:
					 prog.tx0 = poc->tp_tx_s;
					 prog.ty0 = poc->tp_ty_s;
					 prog.tx1 = poc->tp_tx_e;
					 prog.ty1 = poc->tp_ty_e;
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
				  poc->comp_temp = poc->tp_comp_s;
				  prog.comp_s = poc->comp_temp;
				  prog.comp_e = poc->comp_temp + 1U;
				  poc->comp_temp = poc->comp_temp + 1U;
				  break;
			   case 'R':
				  poc->res_temp = poc->tp_res_s;
				  prog.res_s = poc->res_temp;
				  prog.res_e = poc->res_temp + 1U;
				  poc->res_temp = poc->res_temp + 1U;
				  break;
			   case 'L':
				  poc->lay_temp = 0;
				  prog.lay_s = poc->lay_temp;
				  prog.lay_e = poc->lay_temp + 1U;
				  poc->lay_temp = poc->lay_temp + 1U;
				  break;
			   case 'P':
				  switch(poc->progression)
				  {
					 case GRK_LRCP:
					 case GRK_RLCP:
						poc->prec_temp = 0;
						prog.prec_s = poc->prec_temp;
						prog.prec_e = poc->prec_temp + 1U;
						poc->prec_temp += 1;
						break;
					 default:
						poc->tx0_temp = poc->tp_tx_s;
						poc->ty0_temp = poc->tp_ty_s;
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
				  prog.comp_s = uint16_t(poc->comp_temp - 1);
				  prog.comp_e = poc->comp_temp;
				  break;
			   case 'R':
				  prog.res_s = uint8_t(poc->res_temp - 1);
				  prog.res_e = poc->res_temp;
				  break;
			   case 'L':
				  prog.lay_s = uint16_t(poc->lay_temp - 1);
				  prog.lay_e = poc->lay_temp;
				  break;
			   case 'P':
				  switch(poc->progression)
				  {
					 case GRK_LRCP:
					 case GRK_RLCP:
						prog.prec_s = poc->prec_temp - 1;
						prog.prec_e = poc->prec_temp;
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
					 if(poc->res_temp == poc->tp_res_e)
					 {
						if(checkForRemainingValidProgression(i - 1, pino, pocProg))
						{
						   poc->res_temp = poc->tp_res_s;
						   prog.res_s = poc->res_temp;
						   prog.res_e = poc->res_temp + 1U;
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
						prog.res_s = poc->res_temp;
						prog.res_e = poc->res_temp + 1U;
						poc->res_temp = poc->res_temp + 1U;
						incr_top = 0;
					 }
					 break;
				  case 'C':
					 if(poc->comp_temp == poc->tp_comp_e)
					 {
						if(checkForRemainingValidProgression(i - 1, pino, pocProg))
						{
						   poc->comp_temp = poc->tp_comp_s;
						   prog.comp_s = poc->comp_temp;
						   prog.comp_e = poc->comp_temp + 1U;
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
						prog.comp_s = poc->comp_temp;
						prog.comp_e = poc->comp_temp + 1U;
						poc->comp_temp = poc->comp_temp + 1U;
						incr_top = 0;
					 }
					 break;
				  case 'L':
					 if(poc->lay_temp == poc->tp_lay_e)
					 {
						if(checkForRemainingValidProgression(i - 1, pino, pocProg))
						{
						   poc->lay_temp = 0;
						   prog.lay_s = poc->lay_temp;
						   prog.lay_e = poc->lay_temp + 1U;
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
						prog.lay_s = poc->lay_temp;
						prog.lay_e = poc->lay_temp + 1U;
						poc->lay_temp = poc->lay_temp + 1U;
						incr_top = 0;
					 }
					 break;
				  case 'P':
					 switch(poc->progression)
					 {
						case GRK_LRCP:
						case GRK_RLCP:
						   if(poc->prec_temp == poc->tp_prec_e)
						   {
							  if(checkForRemainingValidProgression(i - 1, pino, pocProg))
							  {
								 poc->prec_temp = 0;
								 prog.prec_s = poc->prec_temp;
								 prog.prec_e = poc->prec_temp + 1;
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
							  prog.prec_s = poc->prec_temp;
							  prog.prec_e = poc->prec_temp + 1;
							  poc->prec_temp += 1;
							  incr_top = 0;
						   }
						   break;
						default:
						   if(poc->tx0_temp >= poc->tp_tx_e)
						   {
							  if(poc->ty0_temp >= poc->tp_ty_e)
							  {
								 if(checkForRemainingValidProgression(i - 1, pino, pocProg))
								 {
									poc->ty0_temp = poc->tp_ty_s;
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
								 prog.ty1 = (poc->ty0_temp + poc->dy - (poc->ty0_temp % poc->dy));
								 poc->ty0_temp = prog.ty1;
								 incr_top = 0;
								 resetX = 1;
							  }
							  if(resetX == 1)
							  {
								 poc->tx0_temp = poc->tp_tx_s;
								 prog.tx0 = poc->tx0_temp;
								 prog.tx1 = (uint32_t)(poc->tx0_temp + poc->dx -
													   (poc->tx0_temp % poc->dx));
								 poc->tx0_temp = prog.tx1;
							  }
						   }
						   else
						   {
							  prog.tx0 = poc->tx0_temp;
							  prog.tx1 =
								  (uint32_t)(poc->tx0_temp + poc->dx - (poc->tx0_temp % poc->dx));
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
	  prog.lay_s = 0;
	  prog.lay_e = poc->tp_lay_e;
	  prog.res_s = poc->tp_res_s;
	  prog.res_e = poc->tp_res_e;
	  prog.comp_s = poc->tp_comp_s;
	  prog.comp_e = poc->tp_comp_e;
	  prog.prec_s = 0;
	  prog.prec_e = poc->tp_prec_e;
	  prog.tx0 = poc->tp_tx_s;
	  prog.ty0 = poc->tp_ty_s;
	  prog.tx1 = poc->tp_tx_e;
	  prog.ty1 = poc->tp_ty_e;
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
	  prog.lay_s = 0;
	  prog.lay_e = hasPoc ? std::min<uint16_t>(poc->lay_e, tcp->max_layers_) : tcp->max_layers_;
	  prog.res_s = hasPoc ? poc->res_s : 0;
	  prog.res_e = hasPoc ? poc->res_e : max_res;
	  prog.comp_s = hasPoc ? poc->comp_s : 0;
	  prog.comp_e = std::min<uint16_t>(hasPoc ? poc->comp_e : numcomps, image->numcomps);
	  prog.prec_s = 0;
	  prog.prec_e = max_precincts;
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
			prog.lay_e = (std::min)(
				prog.lay_e,
				packetManager->getTileProcessor()->getTileCodingParams()->numLayersToDecompress);
			break;
		 case GRK_RLCP:
			prog.res_e = (std::min)(prog.res_e, maxNumDecompositionResolutions);
			break;
		 case GRK_RPCL:
			prog.res_e = (std::min)(prog.res_e, maxNumDecompositionResolutions);
			if(precinctInfoOPT_)
			{
			   for(uint8_t resno = 0; resno < comps->numresolutions; ++resno)
			   {
				  auto inf = precinctInfoOPT_ + resno;
				  inf->innerPrecincts_ = (uint64_t)prog.comp_e * prog.lay_e;
				  auto compLayer = inf->innerPrecincts_;
				  inf->winPrecinctsLeft_ = inf->winPrecGrid.x0 * compLayer;
				  inf->winPrecinctsRight_ =
					  (uint64_t)(inf->tileBoundsPrecGrid.x1 - inf->winPrecGrid.x1) * compLayer;
				  inf->winPrecinctsTop_ =
					  (uint64_t)inf->winPrecGrid.y0 * inf->tileBoundsPrecGrid.width() * compLayer;
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
   (void)src;
   for(; compno < prog.comp_e; compno++)
   {
	  auto comp = comps + compno;
	  for(; y < prog.ty1; y += dyActive, dyActive = dy)
	  {
		 for(; x < prog.tx1; x += dxActive, dxActive = dx)
		 {
			for(; resno < prog.res_e; resno++)
			{
			   if(!validatePrecinct())
				  continue;
			   if(incrementInner)
				  layno++;
			   if(layno < prog.lay_e)
			   {
				  incrementInner = true;
				  generatePrecinctIndex();
				  if(update_include())
				  {
					 return true;
				  }
			   }
			   layno = prog.lay_s;
			   incrementInner = false;
			}
			resno = prog.res_s;
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
   (void)src;
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
		 for(; compno < prog.comp_e; compno++)
		 {
			for(; resno < prog.res_e; resno++)
			{
			   if(!validatePrecinct())
				  continue;
			   if(incrementInner)
				  layno++;
			   if(layno < prog.lay_e)
			   {
				  incrementInner = true;
				  generatePrecinctIndex();
				  if(update_include())
					 return true;
			   }
			   layno = prog.lay_s;
			   incrementInner = false;
			}
			resno = prog.res_s;
		 }
		 compno = prog.comp_s;
	  }
	  x = prog.tx0;
	  dxActive = (uint32_t)(dx - (x % dx));
   }

   return false;
}
bool PacketIter::next_lrcp(SparseBuffer* src)
{
   (void)src;
   for(; layno < prog.lay_e; layno++)
   {
	  for(; resno < prog.res_e; resno++)
	  {
		 uint64_t prec_e = 0;
		 if(precinctInfoOPT_)
		 {
			if(resno >= comps->numresolutions)
			   continue;
			auto res = comps->resolutions + resno;
			prec_e = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
		 }
		 for(; compno < prog.comp_e; compno++)
		 {
			auto comp = comps + compno;
			if(!precinctInfoOPT_)
			{
			   // skip resolutions greater than current component resolution
			   if(resno >= comp->numresolutions)
				  continue;
			   auto res = comp->resolutions + resno;
			   prec_e = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
			}
			if(incrementInner)
			   precinctIndex++;
			if(precinctIndex < prec_e)
			{
			   incrementInner = true;
			   if(update_include())
				  return true;
			}
			precinctIndex = prog.prec_s;
			incrementInner = false;
		 }
		 compno = prog.comp_s;
	  }
	  resno = prog.res_s;
   }

   return false;
}
bool PacketIter::next_rlcp(SparseBuffer* src)
{
   (void)src;
   for(; resno < prog.res_e; resno++)
   {
	  uint64_t prec_e = 0;
	  if(precinctInfoOPT_)
	  {
		 if(resno >= comps->numresolutions)
			continue;
		 auto res = comps->resolutions + resno;
		 prec_e = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
	  }
	  for(; layno < prog.lay_e; layno++)
	  {
		 for(; compno < prog.comp_e; compno++)
		 {
			auto comp = comps + compno;
			if(!precinctInfoOPT_)
			{
			   if(resno >= comp->numresolutions)
				  continue;
			   auto res = comp->resolutions + resno;
			   prec_e = (uint64_t)res->precinctGridWidth * res->precinctGridHeight;
			}
			if(incrementInner)
			   precinctIndex++;
			if(precinctIndex < prec_e)
			{
			   incrementInner = true;
			   if(update_include())
				  return true;
			}
			precinctIndex = prog.prec_s;
			incrementInner = false;
		 }
		 compno = prog.comp_s;
	  }
	  layno = prog.lay_s;
   }

   return false;
}
bool PacketIter::next_rpcl(SparseBuffer* src)
{
   (void)src;
   for(; resno < prog.res_e; resno++)
   {
	  // if all remaining components have degenerate precinct grid, then
	  // skip this resolution
	  bool sane = false;
	  for(uint16_t compnoTmp = compno; compnoTmp < prog.comp_e; compnoTmp++)
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
			for(; compno < prog.comp_e; compno++)
			{
			   if(!validatePrecinct())
				  continue;
			   if(incrementInner)
				  layno++;
			   if(layno < prog.lay_e)
			   {
				  incrementInner = true;
				  generatePrecinctIndex();
				  if(update_include())
					 return true;
			   }
			   layno = prog.lay_s;
			   incrementInner = false;
			}
			compno = prog.comp_s;
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
	  Logger::logger_.error("Packet iterator: unable to skip precincts.");
	  return false;
   }
   tp->incNumProcessedPackets(numPackets);

   return true;
}

} // namespace grk
