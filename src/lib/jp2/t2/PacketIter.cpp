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

/** @name Local static functions */
/*@{*/

/**
 * Updates the coding parameters
 *
 * @param	p_cp		the coding parameters to modify
 * @param	num_comps		the number of components
 * @param	tileno	the tile index being concerned.
 * @param	tileBounds tile bounds
 * @param	max_precincts	the maximum number of precincts for all the bands of the tile
 * @param	max_res	the maximum number of resolutions for all the poc inside the tile.
 * @param	dx_min		the minimum dx of all the components of all the resolutions for the tile.
 * @param	dy_min		the minimum dy of all the components of all the resolutions for the tile.
 */
static void pi_update_tcp_progressions_compress(CodingParams *p_cp,
									uint16_t num_comps,
									uint16_t tileno,
									grkRectU32 tileBounds,
									uint64_t max_precincts,
									uint8_t max_res,
									uint32_t dx_min,
									uint32_t dy_min,
									bool poc);
/**
 * Get the compression parameters needed to update the coding parameters and all the pocs.
 * The precinct widths, heights, dx and dy for each component at each resolution will be stored as well.
 * the last parameter of the function should be an array of pointers of size nb components, each pointer leading
 * to an area of size 4 * max_res. The data is stored inside this area with the following pattern :
 * dx_compi_res0 , dy_compi_res0 , w_compi_res0, h_compi_res0 , dx_compi_res1 , dy_compi_res1 , w_compi_res1, h_compi_res1 , ...
 *
 * @param	p_image		the image being encoded.
 * @param	p_cp		the coding parameters.
 * @param	tileno		the tile index of the tile being encoded.
 * @param	tileBounds	tile bounds
 * @param	max_precincts	maximum number of precincts for all the bands of the tile
 * @param	max_res		maximum number of resolutions for all the poc inside the tile.
 * @param	dx_min		minimum dx of all the components of all the resolutions for the tile.
 * @param	dy_min		minimum dy of all the components of all the resolutions for the tile.
 * @param	precinctByComponent	stores log2 precinct grid dimensions by component
 */
static void pi_get_params(const GrkImage *image,
											const CodingParams *p_cp,
											uint16_t tileno,
											grkRectU32 *tileBounds,
											uint32_t *dx_min,
											uint32_t *dy_min,
											uint64_t *precincts,
											uint64_t *max_precincts,
											uint8_t *max_res,
											uint32_t **precinctByComponent);
/**
 * Allocates memory for a packet iterator. Data and data sizes are set by this operation.
 * No other data is set. The include section of the packet  iterator is not allocated.
 *
 * @param	p_image		the image used to initialize the packet iterator (in fact only the number of components is relevant.
 * @param	p_cp		the coding parameters.
 * @param	tileno	the index of the tile from which creating the packet iterator.
 */
static PacketIter* pi_create(const GrkImage *p_image,
								const CodingParams *p_cp,
								uint16_t tileno,
								IncludeTracker *include);
/**
* Check if there is a remaining valid progression order
 */
static bool pi_check_next_for_valid_progression(int32_t prog,
												CodingParams *cp,
												uint16_t tileno,
												uint32_t pino,
												const char *progString);
static PacketIter* pi_create(const GrkImage *image,
							const CodingParams *cp,
							uint16_t tileno,
							IncludeTracker *include) {
	assert(cp != nullptr);
	assert(image != nullptr);
	assert(tileno < cp->t_grid_width * cp->t_grid_height);
	auto tcp = &cp->tcps[tileno];
	uint32_t poc_bound = tcp->numpocs + 1;
	auto pi = new PacketIter[poc_bound];

	for (uint32_t i = 0; i < poc_bound; ++i){
		pi[i].includeTracker = include;
	}
	for (uint32_t pino = 0; pino < poc_bound; ++pino) {
		auto current_pi = pi + pino;
		current_pi->comps = new PiComp[image->numcomps];
		current_pi->numcomps = image->numcomps;
		for (uint32_t compno = 0; compno < image->numcomps; ++compno) {
			auto comp = current_pi->comps + compno;
			auto tccp = tcp->tccps + compno;
			comp->resolutions =  new PiResolution[tccp->numresolutions];
			comp->numresolutions = tccp->numresolutions;
		}
	}
	return pi;
}
static void pi_initialize_progressions_decompress(TileCodingParams *tcp,
													PacketIter *pi,
													uint8_t max_res,
													uint64_t max_precincts){
	bool poc = tcp->POC;
	for (uint32_t pino = 0; pino <= tcp->numpocs; ++pino) {
		auto cur_pi = pi + pino;
		auto current_poc = tcp->progressionOrderChange + pino;
		auto cur_pi_prog = &cur_pi->prog;

		cur_pi_prog->progression 	= poc ? current_poc->progression : tcp->prg; /* Progression Order #0 */
		cur_pi_prog->layS 	= 0;
		cur_pi_prog->layE 	= poc ? std::min<uint16_t>(current_poc->layE,
				tcp->numlayers) : tcp->numlayers; /* Layer Index #0 (End) */
		cur_pi_prog->resS 	= poc ? current_poc->resS : 0; /* Resolution Level Index #0 (Start) */
		cur_pi_prog->resE 	= poc ? current_poc->resE : max_res; /* Resolution Level Index #0 (End) */
		cur_pi_prog->compS 	= poc ? current_poc->compS : 0; /* Component Index #0 (Start) */
		cur_pi_prog->compE 	= poc ? current_poc->compE : cur_pi->numcomps; /* Component Index #0 (End) */
		cur_pi_prog->precS 	= 0;
		cur_pi_prog->precE = max_precincts;
	}
}
PacketIter* pi_create_compress_decompress(bool compression,
								const GrkImage *image,
								CodingParams *p_cp,
								uint16_t tileno,
								J2K_T2_MODE p_t2_mode,
								IncludeTracker *include) {
	assert(p_cp != nullptr);
	assert(image != nullptr);
	assert(tileno < p_cp->t_grid_width * p_cp->t_grid_height);

	auto tcp = &p_cp->tcps[tileno];
	auto pi = pi_create(image, p_cp, tileno,include);

	uint32_t data_stride = 4 * GRK_J2K_MAXRLVLS;
	auto precinct =  new uint32_t[data_stride * image->numcomps];
	auto precinctByComponent = new uint32_t*[image->numcomps];
	auto resolutionPrecinctGrid = precinct;
	for (uint32_t compno = 0; compno < image->numcomps; ++compno) {
		precinctByComponent[compno] = resolutionPrecinctGrid;
		resolutionPrecinctGrid += data_stride;
	}

	uint8_t max_res;
	uint64_t max_precincts;
	grkRectU32 tileBounds;
	uint32_t dx_min, dy_min;
	pi_get_params(image,
				p_cp,
				tileno,
				&tileBounds,
				&dx_min,
				&dy_min,
				include->precincts,
				&max_precincts,
				&max_res,
				precinctByComponent);

	if (!compression)
		pi_initialize_progressions_decompress(tcp, pi, max_res,max_precincts);

	uint32_t step_p = 1;
	uint64_t step_c = max_precincts * step_p;
	uint64_t step_r = image->numcomps * step_c;
	uint64_t step_l = max_res * step_r;

	/* set values for first packet iterator*/
	pi->tp_on = p_cp->m_coding_params.m_enc.m_tp_on;
	for (uint32_t pino = 0; pino <= tcp->numpocs; ++pino) {
		auto cur_pi = pi + pino;

		cur_pi->tx0 = tileBounds.x0;
		cur_pi->ty0 = tileBounds.y0;
		cur_pi->tx1 = tileBounds.x1;
		cur_pi->ty1 = tileBounds.y1;
		cur_pi->prog.ty0 = cur_pi->ty0;
		cur_pi->prog.tx0 = cur_pi->tx0;
		cur_pi->prog.ty1 = cur_pi->ty1;
		cur_pi->prog.tx1 = cur_pi->tx1;
		cur_pi->y = cur_pi->prog.ty0;
		cur_pi->x = cur_pi->prog.tx0;
		cur_pi->dx = dx_min;
		cur_pi->dy = dy_min;
		cur_pi->step_p = step_p;
		cur_pi->step_c = step_c;
		cur_pi->step_r = step_r;
		cur_pi->step_l = step_l;

		/* allocation for components and number of components
		 *  has already been calculated by pi_create */
		for (uint32_t compno = 0; compno < cur_pi->numcomps; ++compno) {
			auto current_comp = cur_pi->comps + compno;
			auto img_comp = image->comps + compno;

			resolutionPrecinctGrid = precinctByComponent[compno];
			current_comp->dx = img_comp->dx;
			current_comp->dy = img_comp->dy;
			/* resolutions have already been initialized */
			for (uint32_t resno = 0; resno < current_comp->numresolutions; resno++) {
				auto res = current_comp->resolutions + resno;

				res->precinctWidthExp = *(resolutionPrecinctGrid++);
				res->precinctHeightExp = *(resolutionPrecinctGrid++);
				res->precinctGridWidth = *(resolutionPrecinctGrid++);
				res->precinctGridHeight = *(resolutionPrecinctGrid++);
			}
		}


		cur_pi->update_dxy();
	}
	delete[] precinct;
	delete[] precinctByComponent;

	if (compression) {
		bool poc = tcp->POC && (GRK_IS_CINEMA(p_cp->rsiz) || p_t2_mode == FINAL_PASS);
		pi_update_tcp_progressions_compress(p_cp,
											image->numcomps,
											tileno,
											tileBounds,
											max_precincts,
											max_res,
											dx_min,
											dy_min,
											poc);
	}

	return pi;
}

void pi_enable_tile_part_generation(PacketIter *pi,
									CodingParams *cp,
									uint16_t tileno,
									uint32_t pino,
									bool first_poc_tile_part,
									uint32_t tppos,
									J2K_T2_MODE t2_mode) {
	auto tcps = cp->tcps + tileno;
	auto poc = tcps->progressionOrderChange + pino;
	auto prog = CodeStreamCompress::convertProgressionOrder(poc->progression);
	auto cur_pi = pi + pino;
	auto cur_pi_prog = &cur_pi->prog;
	cur_pi_prog->progression = poc->progression;

	if (cp->m_coding_params.m_enc.m_tp_on
			&& (GRK_IS_CINEMA(cp->rsiz)|| GRK_IS_IMF(cp->rsiz) || t2_mode == FINAL_PASS)) {
		for (uint32_t i = tppos + 1; i < 4; i++) {
			switch (prog[i]) {
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
				switch (poc->progression) {
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
		if (first_poc_tile_part) {
			for (int32_t i = (int32_t) tppos; i >= 0; i--) {
				switch (prog[i]) {
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
					switch (poc->progression) {
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
						cur_pi_prog->tx1 = (poc->tx0_temp + poc->dx
								- (poc->tx0_temp % poc->dx));
						cur_pi_prog->ty0 = poc->ty0_temp;
						cur_pi_prog->ty1 = (poc->ty0_temp + poc->dy
								- (poc->ty0_temp % poc->dy));
						poc->tx0_temp = cur_pi_prog->tx1;
						poc->ty0_temp = cur_pi_prog->ty1;
						break;
					}
					break;
				}
			}
		} else {
			uint32_t incr_top = 1;
			uint32_t resetX = 0;
			for (int32_t i = (int32_t) tppos; i >= 0; i--) {
				switch (prog[i]) {
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
					switch (poc->progression) {
					case GRK_LRCP:
					case GRK_RLCP:
						cur_pi_prog->precS = poc->prec_temp - 1;
						cur_pi_prog->precE = poc->prec_temp;
						break;
					default:
						cur_pi_prog->tx0 = (poc->tx0_temp - poc->dx
								- (poc->tx0_temp % poc->dx));
						cur_pi_prog->tx1 = poc->tx0_temp;
						cur_pi_prog->ty0 = (poc->ty0_temp - poc->dy
								- (poc->ty0_temp % poc->dy));
						cur_pi_prog->ty1 = poc->ty0_temp;
						break;
					}
					break;
				}
				if (incr_top == 1) {
					switch (prog[i]) {
					case 'R':
						if (poc->res_temp == poc->tpResE) {
							if (pi_check_next_for_valid_progression(i - 1, cp, tileno, pino,prog)) {
								poc->res_temp = poc->tpResS;
								cur_pi_prog->resS = poc->res_temp;
								cur_pi_prog->resE = poc->res_temp + 1U;
								poc->res_temp = poc->res_temp + 1U;
								incr_top = 1;
							} else {
								incr_top = 0;
							}
						} else {
							cur_pi_prog->resS = poc->res_temp;
							cur_pi_prog->resE = poc->res_temp + 1U;
							poc->res_temp = poc->res_temp + 1U;
							incr_top = 0;
						}
						break;
					case 'C':
						if (poc->comp_temp == poc->tpCompE) {
							if (pi_check_next_for_valid_progression(i - 1, cp, tileno, pino,prog)) {
								poc->comp_temp = poc->tpCompS;
								cur_pi_prog->compS = poc->comp_temp;
								cur_pi_prog->compE = poc->comp_temp + 1U;
								poc->comp_temp = poc->comp_temp + 1U;
								incr_top = 1;
							} else {
								incr_top = 0;
							}
						} else {
							cur_pi_prog->compS = poc->comp_temp;
							cur_pi_prog->compE = poc->comp_temp + 1U;
							poc->comp_temp = poc->comp_temp + 1U;
							incr_top = 0;
						}
						break;
					case 'L':
						if (poc->lay_temp == poc->tpLayE) {
							if (pi_check_next_for_valid_progression(i - 1, cp, tileno, pino,prog)) {
								poc->lay_temp = 0;
								cur_pi_prog->layS = poc->lay_temp;
								cur_pi_prog->layE =  poc->lay_temp + 1U;
								poc->lay_temp = poc->lay_temp + 1U;
								incr_top = 1;
							} else {
								incr_top = 0;
							}
						} else {
							cur_pi_prog->layS = poc->lay_temp;
							cur_pi_prog->layE = poc->lay_temp + 1U;
							poc->lay_temp = poc->lay_temp + 1U;
							incr_top = 0;
						}
						break;
					case 'P':
						switch (poc->progression) {
						case GRK_LRCP:
						case GRK_RLCP:
							if (poc->prec_temp == poc->tpPrecE) {
								if (pi_check_next_for_valid_progression(i - 1, cp, tileno, pino,prog)) {
									poc->prec_temp = 0;
									cur_pi_prog->precS = poc->prec_temp;
									cur_pi_prog->precE = poc->prec_temp + 1;
									poc->prec_temp += 1;
									incr_top = 1;
								} else {
									incr_top = 0;
								}
							} else {
								cur_pi_prog->precS = poc->prec_temp;
								cur_pi_prog->precE = poc->prec_temp + 1;
								poc->prec_temp += 1;
								incr_top = 0;
							}
							break;
						default:
							if (poc->tx0_temp >= poc->tp_txE) {
								if (poc->ty0_temp >= poc->tp_tyE) {
									if (pi_check_next_for_valid_progression(i - 1, cp, tileno,pino, prog)) {
										poc->ty0_temp = poc->tp_tyS;
										cur_pi_prog->ty0 = poc->ty0_temp;
										cur_pi_prog->ty1 =
												(uint32_t) (poc->ty0_temp + poc->dy - (poc->ty0_temp % poc->dy));
										poc->ty0_temp = cur_pi_prog->ty1;
										incr_top = 1;
										resetX = 1;
									} else {
										incr_top = 0;
										resetX = 0;
									}
								} else {
									cur_pi_prog->ty0 = poc->ty0_temp;
									cur_pi_prog->ty1 = (poc->ty0_temp + poc->dy - (poc->ty0_temp % poc->dy));
									poc->ty0_temp = cur_pi_prog->ty1;
									incr_top = 0;
									resetX = 1;
								}
								if (resetX == 1) {
									poc->tx0_temp = poc->tp_txS;
									cur_pi_prog->tx0 = poc->tx0_temp;
									cur_pi_prog->tx1 = (uint32_t) (poc->tx0_temp + poc->dx - (poc->tx0_temp % poc->dx));
									poc->tx0_temp = cur_pi_prog->tx1;
								}
							} else {
								cur_pi_prog->tx0 = poc->tx0_temp;
								cur_pi_prog->tx1 = (uint32_t) (poc->tx0_temp + poc->dx - (poc->tx0_temp % poc->dx));
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
	} else {
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
void pi_destroy(PacketIter *p_pi) {
	if (p_pi) {
		p_pi->destroy_include();
		delete[] p_pi;
	}
}
static void pi_get_params(const GrkImage *image,
								const CodingParams *p_cp,
								uint16_t tileno,
								grkRectU32 *tileBounds,
								uint32_t *dx_min,
								uint32_t *dy_min,
								uint64_t *precincts,
								uint64_t *max_precincts,
								uint8_t *max_res,
								uint32_t **precinctByComponent) {
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

	if (precincts) {
		for (uint32_t i = 0; i < GRK_J2K_MAXRLVLS; ++i)
			precincts[i] = 0;
	}
	auto tcp = &p_cp->tcps[tileno];
	for (uint32_t compno = 0; compno < image->numcomps; ++compno) {
		uint32_t* precinct = nullptr;
		if (precinctByComponent)
			precinct = precinctByComponent[compno];

		auto tccp = tcp->tccps + compno;
		auto comp = image->comps + compno;

		auto tileCompBounds = tileBounds->rectceildiv(comp->dx,comp->dy);
		if (tccp->numresolutions > *max_res)
			*max_res = tccp->numresolutions;

		/* use custom size for precincts*/
		for (uint32_t resno = 0; resno < tccp->numresolutions; ++resno) {
			uint32_t precinctWidthExp = tccp->precinctWidthExp[resno];
			uint32_t precinctHeightExp = tccp->precinctHeightExp[resno];
			if (precinct) {
				*precinct++ = precinctWidthExp;
				*precinct++ = precinctHeightExp;
			}
			uint64_t dx = comp->dx * ((uint64_t) 1u << (precinctWidthExp + tccp->numresolutions - 1U - resno));
			uint64_t dy = comp->dy * ((uint64_t) 1u << (precinctHeightExp + tccp->numresolutions - 1U - resno));
			if (dx < UINT_MAX)
				*dx_min = std::min<uint32_t>(*dx_min, (uint32_t) dx);
			if (dy < UINT_MAX)
				*dy_min = std::min<uint32_t>(*dy_min, (uint32_t) dy);
			auto resBounds 	= tileCompBounds.rectceildivpow2(tccp->numresolutions - 1U - resno);
			auto precinctDims = grkRectU32(floordivpow2(resBounds.x0, precinctWidthExp) << precinctWidthExp,
											floordivpow2(resBounds.y0, precinctHeightExp) << precinctHeightExp,
											ceildivpow2<uint32_t>(resBounds.x1, precinctWidthExp) << precinctWidthExp,
											ceildivpow2<uint32_t>(resBounds.y1, precinctHeightExp) << precinctHeightExp);

			precinctWidthExp	= (resBounds.width()==0) ? 0 : (precinctDims.width() >> precinctWidthExp);
			precinctHeightExp	= (resBounds.height()==0) ? 0: (precinctDims.height() >> precinctHeightExp);
			if (precinct) {
				*precinct++ = precinctWidthExp;
				*precinct++ = precinctHeightExp;
			}
			uint64_t product = (uint64_t)precinctWidthExp * precinctHeightExp;
			if (precincts && product > precincts[resno])
				precincts[resno] = product;
			if (product > *max_precincts)
				*max_precincts = product;
		}
	}
}
static void pi_update_tcp_progressions_compress(CodingParams *p_cp,
									uint16_t num_comps,
									uint16_t tileno,
									grkRectU32 tileBounds,
									uint64_t max_precincts,
									uint8_t max_res,
									uint32_t dx_min,
									uint32_t dy_min,
									bool poc) {
	assert(p_cp != nullptr);
	assert(tileno < p_cp->t_grid_width * p_cp->t_grid_height);
	auto tcp = &p_cp->tcps[tileno];
	for (uint32_t pino = 0; pino <= tcp->numpocs; ++pino) {
		auto cur_prog 		= tcp->progressionOrderChange + pino;
		cur_prog->progression 		= poc ? cur_prog->specifiedCompressionPocProg : tcp->prg;
		cur_prog->tpLayE 	= poc ? cur_prog->layE : tcp->numlayers;
		cur_prog->tpResS 	= poc ? cur_prog->resS : 0;
		cur_prog->tpResE 	= poc ? cur_prog->resE : max_res;
		cur_prog->tpCompS 	= poc ? cur_prog->compS : 0;
		cur_prog->tpCompE 	= poc ? cur_prog->compE : num_comps;
		cur_prog->tpPrecE 	= max_precincts;
		cur_prog->tp_txS 	= tileBounds.x0;
		cur_prog->tp_txE 	= tileBounds.x1;
		cur_prog->tp_tyS 	= tileBounds.y0;
		cur_prog->tp_tyE 	= tileBounds.y1;
		cur_prog->dx 		= dx_min;
		cur_prog->dy 		= dy_min;
	}
}
void pi_update_params_compress(const GrkImage *image,
									CodingParams *p_cp,
									uint16_t tileno) {
	assert(p_cp != nullptr);
	assert(image != nullptr);
	assert(tileno < p_cp->t_grid_width * p_cp->t_grid_height);

	auto tcp = p_cp->tcps + tileno;

	uint8_t max_res;
	uint64_t max_precincts;
	grkRectU32 tileBounds;
	uint32_t dx_min, dy_min;
	pi_get_params(image,
				p_cp,
				tileno,
				&tileBounds,
				&dx_min,
				&dy_min,
				nullptr,
				&max_precincts,
				&max_res,
				nullptr);
	pi_update_tcp_progressions_compress(p_cp,
						image->numcomps,
						tileno,
						tileBounds,
						max_precincts,
						max_res,
						dx_min,
						dy_min,
						tcp->POC);
}
/**
 * Check if there is a remaining valid progression order
 */
static bool pi_check_next_for_valid_progression(int32_t prog,
												CodingParams *cp,
												uint16_t tileno,
												uint32_t pino,
												const char *progString) {
	auto tcps = cp->tcps + tileno;
	auto poc = tcps->progressionOrderChange + pino;

	if (prog >= 0) {
		switch (progString[prog]) {
		case 'R':
			if (poc->res_temp == poc->tpResE)
				return pi_check_next_for_valid_progression(prog - 1, cp, tileno, pino, progString);
			else
				return true;
			break;
		case 'C':
			if (poc->comp_temp == poc->tpCompE)
				return pi_check_next_for_valid_progression(prog - 1, cp, tileno, pino, progString);
			else
				return true;
			break;
		case 'L':
			if (poc->lay_temp == poc->tpLayE)
				return pi_check_next_for_valid_progression(prog - 1, cp, tileno, pino, progString);
			else
				return true;
			break;
		case 'P':
			switch (poc->progression) {
			case GRK_LRCP: /* fall through */
			case GRK_RLCP:
				if (poc->prec_temp == poc->tpPrecE)
					return pi_check_next_for_valid_progression(prog - 1, cp, tileno, pino,	progString);
				else
					return true;
				break;
			default:
				if (poc->tx0_temp == poc->tp_txE) {
					/*TY*/
					if (poc->ty0_temp == poc->tp_tyE)
						return pi_check_next_for_valid_progression(prog - 1, cp, tileno, pino,	progString);
					else
						return true;
					/*TY*/
				} else {
					return true;
				}
				break;
			}
		}
	}
	return false;
}
PacketIter::PacketIter() : tp_on(false),
							includeTracker(nullptr),
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
							dy(0)
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
					for (; layno < prog.layE; layno++) {
						if (update_include())
							return true;
					}
					layno = prog.layS;
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
	for (; y < prog.ty1;	y += dy - (y % dy)) {
		for (; x < prog.tx1;	x += dx - (x % dx)) {
			for (; compno < prog.compE; compno++) {
				auto comp = comps + compno;
				for (; resno< std::min<uint32_t>(prog.resE,comp->numresolutions); resno++) {
					if (!generatePrecinctIndex())
						continue;
					for (; layno < prog.layE; layno++) {
						if (update_include())
							return true;
					}
					layno = prog.layS;
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
				for (; precinctIndex < precE;	precinctIndex++) {
					if (update_include())
						return true;
				}
				precinctIndex = prog.precS;
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
				for (; precinctIndex < precE;	precinctIndex++) {
					if (update_include())
						return true;
				}
				precinctIndex = prog.precS;
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
					for (; layno < prog.layE; layno++) {
						if (update_include())
							return true;
					}
					layno = prog.layS;
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
	return includeTracker->get_include(layerno, resno);
}
bool PacketIter::update_include(void){
	return includeTracker->update(layno, resno, compno, precinctIndex);
}
void PacketIter::destroy_include(void){
	includeTracker->clear();
}

}
