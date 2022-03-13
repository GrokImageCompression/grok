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
	uint32_t dx_min, dy_min;
	getParams(image, cp, tileno, &tileBounds_, &dx_min, &dy_min, includeTracker->numPrecinctsPerRes,
			  &max_precincts, &max_res, precinctByComponent);
	bool hasPoc = tcp->hasPoc();
	uint32_t step_p = 1;
	uint64_t step_c = max_precincts * step_p;
	uint64_t step_r = image->numcomps * step_c;
	uint64_t step_l = max_res * step_r;
	pi_ = new PacketIter[numProgressions];
	for(uint32_t pino = 0; pino < numProgressions; ++pino)
	{
		auto pi = pi_ + pino;
		pi->init(this, tcp);
		if(!compression)
		{
			auto poc = tcp->progressionOrderChange + pino;
			auto prog = &pi->prog;

			prog->progression = hasPoc ? poc->progression : tcp->prg;
			prog->layS = 0;
			prog->layE = hasPoc ? std::min<uint16_t>(poc->layE, tcp->numlayers) : tcp->numlayers;
			prog->resS = hasPoc ? poc->resS : 0;
			prog->resE = hasPoc ? poc->resE : max_res;
			prog->compS = hasPoc ? poc->compS : 0;
			prog->compE = std::min<uint16_t>(hasPoc ? poc->compE : pi->numcomps, image->numcomps);
			prog->precS = 0;
			prog->precE = max_precincts;
		}
		pi->prog.tx0 = tileBounds_.x0;
		pi->prog.ty0 = tileBounds_.y0;
		pi->prog.tx1 = tileBounds_.x1;
		pi->prog.ty1 = tileBounds_.y1;
		pi->y = pi->prog.ty0;
		pi->x = pi->prog.tx0;
		pi->dx = dx_min;
		pi->dy = dy_min;
		pi->step_p = step_p;
		pi->step_c = step_c;
		pi->step_r = step_r;
		pi->step_l = step_l;

		/* allocation for components and number of components
		 *  has already been calculated by pi_create */
		for(uint16_t compno = 0; compno < pi->numcomps; ++compno)
		{
			auto current_comp = pi->comps + compno;
			resolutionPrecinctGrid = precinctByComponent[compno];
			/* resolutions have already been initialized */
			for(uint32_t resno = 0; resno < current_comp->numresolutions; resno++)
			{
				auto res = current_comp->resolutions + resno;

				res->precinctWidthExp = *(resolutionPrecinctGrid++);
				res->precinctHeightExp = *(resolutionPrecinctGrid++);
				res->precinctGridWidth = *(resolutionPrecinctGrid++);
				res->precinctGridHeight = *(resolutionPrecinctGrid++);
			}
		}
		pi->genPrecinctInfo();
		pi->update_dxy();
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
grkRectU32 PacketManager::getTileBounds(void)
{
	return tileBounds_;
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
	auto tcps = cp->tcps + tileno;
	auto poc = tcps->progressionOrderChange + pino;
	auto prog = CodeStreamCompress::convertProgressionOrder(poc->progression);
	auto cur_pi = pi_ + pino;
	auto cur_pi_prog = &cur_pi->prog;
	cur_pi_prog->progression = poc->progression;

	if(cp->coding_params_.enc_.enableTilePartGeneration_ &&
	   (GRK_IS_CINEMA(cp->rsiz) || GRK_IS_IMF(cp->rsiz) || t2Mode == FINAL_PASS))
	{
		for(uint32_t i = newTilePartProgressionPosition + 1; i < 4; i++)
		{
			switch(prog[i])
			{
				case 'R':
					cur_pi_prog->resS = poc->tpResS;
					cur_pi_prog->resE = poc->tpResE;
					break;
				case 'C':
					cur_pi_prog->compS = poc->tpCompS;
					cur_pi_prog->compE = poc->tpCompE;
					break;
				case 'L':
					cur_pi_prog->layS = 0;
					cur_pi_prog->layE = poc->tpLayE;
					break;
				case 'P':
					switch(poc->progression)
					{
						case GRK_LRCP:
						case GRK_RLCP:
							cur_pi_prog->precS = 0;
							cur_pi_prog->precE = poc->tpPrecE;
							break;
						default:
							cur_pi_prog->tx0 = poc->tp_txS;
							cur_pi_prog->ty0 = poc->tp_tyS;
							cur_pi_prog->tx1 = poc->tp_txE;
							cur_pi_prog->ty1 = poc->tp_tyE;
							break;
					}
					break;
			}
		}
		if(first_poc_tile_part)
		{
			for(int32_t i = (int32_t)newTilePartProgressionPosition; i >= 0; i--)
			{
				switch(prog[i])
				{
					case 'C':
						poc->comp_temp = poc->tpCompS;
						cur_pi_prog->compS = poc->comp_temp;
						cur_pi_prog->compE = poc->comp_temp + 1U;
						poc->comp_temp = poc->comp_temp + 1U;
						break;
					case 'R':
						poc->res_temp = poc->tpResS;
						cur_pi_prog->resS = poc->res_temp;
						cur_pi_prog->resE = poc->res_temp + 1U;
						poc->res_temp = poc->res_temp + 1U;
						break;
					case 'L':
						poc->lay_temp = 0;
						cur_pi_prog->layS = poc->lay_temp;
						cur_pi_prog->layE = poc->lay_temp + 1U;
						poc->lay_temp = poc->lay_temp + 1U;
						break;
					case 'P':
						switch(poc->progression)
						{
							case GRK_LRCP:
							case GRK_RLCP:
								poc->prec_temp = 0;
								cur_pi_prog->precS = poc->prec_temp;
								cur_pi_prog->precE = poc->prec_temp + 1U;
								poc->prec_temp += 1;
								break;
							default:
								poc->tx0_temp = poc->tp_txS;
								poc->ty0_temp = poc->tp_tyS;
								cur_pi_prog->tx0 = poc->tx0_temp;
								cur_pi_prog->tx1 =
									(poc->tx0_temp + poc->dx - (poc->tx0_temp % poc->dx));
								cur_pi_prog->ty0 = poc->ty0_temp;
								cur_pi_prog->ty1 =
									(poc->ty0_temp + poc->dy - (poc->ty0_temp % poc->dy));
								poc->tx0_temp = cur_pi_prog->tx1;
								poc->ty0_temp = cur_pi_prog->ty1;
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
				switch(prog[i])
				{
					case 'C':
						cur_pi_prog->compS = uint16_t(poc->comp_temp - 1);
						cur_pi_prog->compE = poc->comp_temp;
						break;
					case 'R':
						cur_pi_prog->resS = uint8_t(poc->res_temp - 1);
						cur_pi_prog->resE = poc->res_temp;
						break;
					case 'L':
						cur_pi_prog->layS = uint16_t(poc->lay_temp - 1);
						cur_pi_prog->layE = poc->lay_temp;
						break;
					case 'P':
						switch(poc->progression)
						{
							case GRK_LRCP:
							case GRK_RLCP:
								cur_pi_prog->precS = poc->prec_temp - 1;
								cur_pi_prog->precE = poc->prec_temp;
								break;
							default:
								cur_pi_prog->tx0 =
									(poc->tx0_temp - poc->dx - (poc->tx0_temp % poc->dx));
								cur_pi_prog->tx1 = poc->tx0_temp;
								cur_pi_prog->ty0 =
									(poc->ty0_temp - poc->dy - (poc->ty0_temp % poc->dy));
								cur_pi_prog->ty1 = poc->ty0_temp;
								break;
						}
						break;
				}
				if(incr_top == 1)
				{
					switch(prog[i])
					{
						case 'R':
							if(poc->res_temp == poc->tpResE)
							{
								if(checkForRemainingValidProgression(i - 1, pino, prog))
								{
									poc->res_temp = poc->tpResS;
									cur_pi_prog->resS = poc->res_temp;
									cur_pi_prog->resE = poc->res_temp + 1U;
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
								cur_pi_prog->resS = poc->res_temp;
								cur_pi_prog->resE = poc->res_temp + 1U;
								poc->res_temp = poc->res_temp + 1U;
								incr_top = 0;
							}
							break;
						case 'C':
							if(poc->comp_temp == poc->tpCompE)
							{
								if(checkForRemainingValidProgression(i - 1, pino, prog))
								{
									poc->comp_temp = poc->tpCompS;
									cur_pi_prog->compS = poc->comp_temp;
									cur_pi_prog->compE = poc->comp_temp + 1U;
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
								cur_pi_prog->compS = poc->comp_temp;
								cur_pi_prog->compE = poc->comp_temp + 1U;
								poc->comp_temp = poc->comp_temp + 1U;
								incr_top = 0;
							}
							break;
						case 'L':
							if(poc->lay_temp == poc->tpLayE)
							{
								if(checkForRemainingValidProgression(i - 1, pino, prog))
								{
									poc->lay_temp = 0;
									cur_pi_prog->layS = poc->lay_temp;
									cur_pi_prog->layE = poc->lay_temp + 1U;
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
								cur_pi_prog->layS = poc->lay_temp;
								cur_pi_prog->layE = poc->lay_temp + 1U;
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
										if(checkForRemainingValidProgression(i - 1, pino, prog))
										{
											poc->prec_temp = 0;
											cur_pi_prog->precS = poc->prec_temp;
											cur_pi_prog->precE = poc->prec_temp + 1;
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
										cur_pi_prog->precS = poc->prec_temp;
										cur_pi_prog->precE = poc->prec_temp + 1;
										poc->prec_temp += 1;
										incr_top = 0;
									}
									break;
								default:
									if(poc->tx0_temp >= poc->tp_txE)
									{
										if(poc->ty0_temp >= poc->tp_tyE)
										{
											if(checkForRemainingValidProgression(i - 1, pino, prog))
											{
												poc->ty0_temp = poc->tp_tyS;
												cur_pi_prog->ty0 = poc->ty0_temp;
												cur_pi_prog->ty1 =
													(uint32_t)(poc->ty0_temp + poc->dy -
															   (poc->ty0_temp % poc->dy));
												poc->ty0_temp = cur_pi_prog->ty1;
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
											cur_pi_prog->ty0 = poc->ty0_temp;
											cur_pi_prog->ty1 = (poc->ty0_temp + poc->dy -
																(poc->ty0_temp % poc->dy));
											poc->ty0_temp = cur_pi_prog->ty1;
											incr_top = 0;
											resetX = 1;
										}
										if(resetX == 1)
										{
											poc->tx0_temp = poc->tp_txS;
											cur_pi_prog->tx0 = poc->tx0_temp;
											cur_pi_prog->tx1 =
												(uint32_t)(poc->tx0_temp + poc->dx -
														   (poc->tx0_temp % poc->dx));
											poc->tx0_temp = cur_pi_prog->tx1;
										}
									}
									else
									{
										cur_pi_prog->tx0 = poc->tx0_temp;
										cur_pi_prog->tx1 = (uint32_t)(poc->tx0_temp + poc->dx -
																	  (poc->tx0_temp % poc->dx));
										poc->tx0_temp = cur_pi_prog->tx1;
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
		cur_pi_prog->layS = 0;
		cur_pi_prog->layE = poc->tpLayE;
		cur_pi_prog->resS = poc->tpResS;
		cur_pi_prog->resE = poc->tpResE;
		cur_pi_prog->compS = poc->tpCompS;
		cur_pi_prog->compE = poc->tpCompE;
		cur_pi_prog->precS = 0;
		cur_pi_prog->precE = poc->tpPrecE;
		cur_pi_prog->tx0 = poc->tp_txS;
		cur_pi_prog->ty0 = poc->tp_tyS;
		cur_pi_prog->tx1 = poc->tp_txE;
		cur_pi_prog->ty1 = poc->tp_tyE;
	}
}
void PacketManager::getParams(const GrkImage* image, const CodingParams* p_cp, uint16_t tileno,
							  grkRectU32* tileBounds, uint32_t* dx_min, uint32_t* dy_min,
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

		auto tileCompBounds = tileBounds->rectceildiv(comp->dx, comp->dy);
		if(tccp->numresolutions > *max_res)
			*max_res = tccp->numresolutions;

		/* use custom size for precincts*/
		for(uint8_t resno = 0; resno < tccp->numresolutions; ++resno)
		{
			// 1. precinct dimensions
			uint32_t precinctWidthExp = tccp->precinctWidthExp[resno];
			uint32_t precinctHeightExp = tccp->precinctHeightExp[resno];
			if(precinctInfo)
			{
				*precinctInfo++ = precinctWidthExp;
				*precinctInfo++ = precinctHeightExp;
			}

			// 2. precinct grid
			auto resBounds = tileCompBounds.rectceildivpow2(tccp->numresolutions - 1U - resno);
			auto resBoundsAdjusted = grkRectU32(
				floordivpow2(resBounds.x0, precinctWidthExp) << precinctWidthExp,
				floordivpow2(resBounds.y0, precinctHeightExp) << precinctHeightExp,
				ceildivpow2<uint32_t>(resBounds.x1, precinctWidthExp) << precinctWidthExp,
				ceildivpow2<uint32_t>(resBounds.y1, precinctHeightExp) << precinctHeightExp);
			uint32_t precinctGridWidth =
				resBounds.width() == 0 ? 0 : (resBoundsAdjusted.width() >> precinctWidthExp);
			uint32_t precinctGridHeight =
				resBounds.height() == 0 ? 0 : (resBoundsAdjusted.height() >> precinctHeightExp);
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

			// 3. precinct subsampling factors
			uint64_t pdx =
				comp->dx * ((uint64_t)1u << (precinctWidthExp + tccp->numresolutions - 1U - resno));
			uint64_t pdy = comp->dy * ((uint64_t)1u
									   << (precinctHeightExp + tccp->numresolutions - 1U - resno));
			// sanity check
			if(pdx < UINT_MAX)
				*dx_min = std::min<uint32_t>(*dx_min, (uint32_t)pdx);
			if(pdy < UINT_MAX)
				*dy_min = std::min<uint32_t>(*dy_min, (uint32_t)pdy);
		}
	}
}
void PacketManager::updateCompressTcpProgressions(CodingParams* p_cp, uint16_t num_comps,
												  uint16_t tileno, grkRectU32 tileBounds,
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
		prog->tpLayE = poc ? prog->layE : tcp->numlayers;
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
	grkRectU32 tileBounds;
	uint32_t dx_min, dy_min;
	getParams(image, p_cp, tileno, &tileBounds, &dx_min, &dy_min, nullptr, &max_precincts, &max_res,
			  nullptr);
	updateCompressTcpProgressions(p_cp, image->numcomps, tileno, tileBounds, max_precincts, max_res,
								  dx_min, dy_min, tcp->hasPoc());
}
/**
 * Check if there is a remaining valid progression order
 */
bool PacketManager::checkForRemainingValidProgression(int32_t prog, uint32_t pino,
													  const char* progString)
{
	auto tcps = cp->tcps + tileno;
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
					case GRK_LRCP: /* fall through */
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

IncludeTracker* PacketManager::getIncludeTracker(void)
{
	return includeTracker;
}

} // namespace grk
