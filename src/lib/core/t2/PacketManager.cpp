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
 */

#include "grk_includes.h"

namespace grk
{
PacketManager::PacketManager(bool compression, GrkImage* img, CodingParams* cparams,
							 uint16_t tilenumber, J2K_T2_MODE t2_mode, TileProcessor* tileProc)
	: image(img), cp(cparams), tileno(tilenumber),
	  includeTracker(new IncludeTracker(image->numcomps)), pi_(nullptr), t2Mode(t2_mode),
	  tileProcessor(tileProc)
{
   assert(cp != nullptr);
   assert(image != nullptr);
   assert(tileno < cp->t_grid_width * cp->t_grid_height);
   auto tcp = cp->tcps + tileno;
   uint32_t numProgressions = tcp->numpocs + 1;
   uint32_t data_stride = 4 * GRK_J2K_MAXRLVLS;
   auto precinct = new uint32_t[data_stride * image->numcomps];
   auto precinctByComponent = new uint32_t*[image->numcomps];
   auto resolutionPrecinctGrid = precinct;
   for(uint16_t compno = 0; compno < image->numcomps; ++compno)
   {
	  precinctByComponent[compno] = resolutionPrecinctGrid;
	  resolutionPrecinctGrid += data_stride;
   }
   uint8_t max_res;
   uint64_t max_precincts;
   uint32_t dx_min;
   uint32_t dy_min;
   getParams(image, cp, tileno, &tileBounds_, &dx_min, &dy_min, includeTracker->numPrecinctsPerRes,
			 &max_precincts, &max_res, precinctByComponent);

   pi_ = new PacketIter[numProgressions];
   for(uint32_t pino = 0; pino < numProgressions; ++pino)
   {
	  auto pi = pi_ + pino;
	  pi->init(this, pino, tcp, tileBounds_, compression, max_res, max_precincts,
			   resolutionPrecinctGrid, precinctByComponent);
   }
   delete[] precinct;
   delete[] precinctByComponent;
   if(compression)
   {
	  bool poc = tcp->hasPoc() && (GRK_IS_CINEMA(cp->rsiz) || t2_mode == FINAL_PASS);
	  updateCompressTcpProgressions(cp, image->numcomps, tileno, tileBounds_, max_precincts,
									max_res, dx_min, dy_min, poc);
   }
}
GrkImage* PacketManager::getImage()
{
   return image;
}
grk_rect32 PacketManager::getTileBounds(void)
{
   return tileBounds_;
}
CodingParams* PacketManager::getCodingParams(void)
{
   return cp;
}
J2K_T2_MODE PacketManager::getT2Mode(void)
{
   return t2Mode;
}
PacketManager::~PacketManager()
{
   if(pi_)
   {
	  pi_->destroy_include();
	  delete[] pi_;
   }
   delete includeTracker;
}
uint32_t PacketManager::getNumProgressions(void)
{
   return (cp->tcps + tileno)->getNumProgressions();
}
PacketIter* PacketManager::getPacketIter(uint32_t poc) const
{
   return pi_ + poc;
}
TileProcessor* PacketManager::getTileProcessor(void)
{
   return tileProcessor;
}
void PacketManager::enableTilePartGeneration(uint32_t pino, bool first_poc_tile_part,
											 uint32_t newTilePartProgressionPosition)
{
   (pi_ + pino)
	   ->enableTilePartGeneration(pino, first_poc_tile_part, newTilePartProgressionPosition);
}
void PacketManager::getParams(const GrkImage* image, const CodingParams* p_cp, uint16_t tileno,
							  grk_rect32* tileBounds, uint32_t* dx_min, uint32_t* dy_min,
							  uint64_t* numPrecinctsPerRes, uint64_t* max_precincts,
							  uint8_t* max_res, uint32_t** precinctInfoByComponent)
{
   assert(p_cp != nullptr);
   assert(image != nullptr);
   assert(tileno < p_cp->t_grid_width * p_cp->t_grid_height);

   uint32_t tile_x = tileno % p_cp->t_grid_width;
   uint32_t tile_y = tileno / p_cp->t_grid_width;
   *tileBounds = p_cp->getTileBounds(image, tile_x, tile_y);

   *max_precincts = 0;
   *max_res = 0;
   *dx_min = UINT_MAX;
   *dy_min = UINT_MAX;

   if(numPrecinctsPerRes)
   {
	  for(uint32_t i = 0; i < GRK_J2K_MAXRLVLS; ++i)
		 numPrecinctsPerRes[i] = 0;
   }
   auto tcp = &p_cp->tcps[tileno];
   for(uint16_t compno = 0; compno < image->numcomps; ++compno)
   {
	  uint32_t* precinctInfo = nullptr;
	  if(precinctInfoByComponent)
		 precinctInfo = precinctInfoByComponent[compno];

	  auto tccp = tcp->tccps + compno;
	  auto comp = image->comps + compno;

	  auto tileCompBounds = tileBounds->scaleDownCeil(comp->dx, comp->dy);
	  if(tccp->numresolutions > *max_res)
		 *max_res = tccp->numresolutions;

	  /* use custom size for precincts*/
	  for(uint8_t resno = 0; resno < tccp->numresolutions; ++resno)
	  {
		 // 1. precinct dimensions
		 uint32_t precWidthExp = tccp->precWidthExp[resno];
		 uint32_t precHeightExp = tccp->precHeightExp[resno];
		 if(precinctInfo)
		 {
			*precinctInfo++ = precWidthExp;
			*precinctInfo++ = precHeightExp;
		 }

		 // 2. precinct grid
		 auto resBounds = tileCompBounds.scaleDownCeilPow2(tccp->numresolutions - 1U - resno);
		 auto resBoundsAdjusted =
			 grk_rect32(floordivpow2(resBounds.x0, precWidthExp) << precWidthExp,
						floordivpow2(resBounds.y0, precHeightExp) << precHeightExp,
						ceildivpow2<uint32_t>(resBounds.x1, precWidthExp) << precWidthExp,
						ceildivpow2<uint32_t>(resBounds.y1, precHeightExp) << precHeightExp);
		 uint32_t precinctGridWidth =
			 resBounds.width() == 0 ? 0 : (resBoundsAdjusted.width() >> precWidthExp);
		 uint32_t precinctGridHeight =
			 resBounds.height() == 0 ? 0 : (resBoundsAdjusted.height() >> precHeightExp);
		 if(precinctInfo)
		 {
			*precinctInfo++ = precinctGridWidth;
			*precinctInfo++ = precinctGridHeight;
		 }
		 uint64_t numPrecincts = (uint64_t)precinctGridWidth * precinctGridHeight;
		 if(numPrecinctsPerRes && numPrecincts > numPrecinctsPerRes[resno])
			numPrecinctsPerRes[resno] = numPrecincts;
		 if(numPrecincts > *max_precincts)
			*max_precincts = numPrecincts;

		 // 3. find minimal precinct subsampling factors over all components and resolutions
		 uint64_t compResDx =
			 comp->dx * ((uint64_t)1u << (precWidthExp + tccp->numresolutions - 1U - resno));
		 uint64_t compResDy =
			 comp->dy * ((uint64_t)1u << (precHeightExp + tccp->numresolutions - 1U - resno));
		 if(compResDx < UINT_MAX)
			*dx_min = std::min<uint32_t>(*dx_min, (uint32_t)compResDx);
		 if(compResDy < UINT_MAX)
			*dy_min = std::min<uint32_t>(*dy_min, (uint32_t)compResDy);
	  }
   }
}
void PacketManager::updateCompressTcpProgressions(CodingParams* p_cp, uint16_t num_comps,
												  uint16_t tileno, grk_rect32 tileBounds,
												  uint64_t max_precincts, uint8_t max_res,
												  uint32_t dx_min, uint32_t dy_min, bool poc)
{
   assert(p_cp != nullptr);
   assert(tileno < p_cp->t_grid_width * p_cp->t_grid_height);
   auto tcp = p_cp->tcps + tileno;
   for(uint32_t pino = 0; pino < tcp->getNumProgressions(); ++pino)
   {
	  auto prog = tcp->progressionOrderChange + pino;
	  prog->progression = poc ? prog->specifiedCompressionPocProg : tcp->prg;
	  prog->tpLayE = poc ? prog->layE : tcp->max_layers_;
	  prog->tpResS = poc ? prog->resS : 0;
	  prog->tpResE = poc ? prog->resE : max_res;
	  prog->tpCompS = poc ? prog->compS : 0;
	  prog->tpCompE = poc ? prog->compE : num_comps;
	  prog->tpPrecE = max_precincts;
	  prog->tp_txS = tileBounds.x0;
	  prog->tp_tyS = tileBounds.y0;
	  prog->tp_txE = tileBounds.x1;
	  prog->tp_tyE = tileBounds.y1;
	  prog->dx = dx_min;
	  prog->dy = dy_min;
   }
}
void PacketManager::updateCompressParams(const GrkImage* image, CodingParams* p_cp, uint16_t tileno)
{
   assert(p_cp != nullptr);
   assert(image != nullptr);
   assert(tileno < p_cp->t_grid_width * p_cp->t_grid_height);

   auto tcp = p_cp->tcps + tileno;

   uint8_t max_res;
   uint64_t max_precincts;
   grk_rect32 tileBounds;
   uint32_t dx_min, dy_min;
   getParams(image, p_cp, tileno, &tileBounds, &dx_min, &dy_min, nullptr, &max_precincts, &max_res,
			 nullptr);
   updateCompressTcpProgressions(p_cp, image->numcomps, tileno, tileBounds, max_precincts, max_res,
								 dx_min, dy_min, tcp->hasPoc());
}
IncludeTracker* PacketManager::getIncludeTracker(void)
{
   return includeTracker;
}

} // namespace grk
