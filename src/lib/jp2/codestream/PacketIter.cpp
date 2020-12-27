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
 Get next packet in layer-resolution-component-precinct order.
 @param pi packet iterator to modify
 @return returns false if pi pointed to the last packet or else returns true
 */
static bool pi_next_lrcp(PacketIter *pi);
/**
 Get next packet in resolution-layer-component-precinct order.
 @param pi packet iterator to modify
 @return returns false if pi pointed to the last packet or else returns true
 */
static bool pi_next_rlcp(PacketIter *pi);
/**
 Get next packet in resolution-precinct-component-layer order.
 @param pi packet iterator to modify
 @return returns false if pi pointed to the last packet or else returns true
 */
static bool pi_next_rpcl(PacketIter *pi);
/**
 Get next packet in precinct-component-resolution-layer order.
 @param pi packet iterator to modify
 @return returns false if pi pointed to the last packet or else returns true
 */
static bool pi_next_pcrl(PacketIter *pi);
/**
 Get next packet in component-precinct-resolution-layer order.
 @param pi packet iterator to modify
 @return returns false if pi pointed to the last packet or else returns true
 */
static bool pi_next_cprl(PacketIter *pi);

/**
 * Updates the coding parameters if compression uses progression order changes
 * and is final (or cinema parameters are used).
 *
 * @param	p_cp		the coding parameters to modify
 * @param	tileno	the tile index being concerned.
 * @param	tileBounds	tileBounds
 * @param	max_precincts	the maximum number of precincts for all the bands of the tile
 * @param	dx_min		the minimum dx of all the components of all the resolutions for the tile.
 * @param	dy_min		the minimum dy of all the components of all the resolutions for the tile.
 */
static void pi_update_compress_poc_and_final(CodingParams *p_cp,
											uint16_t tileno,
											grk_rect_u32 tileBounds,
											uint64_t max_precincts,
											uint32_t dx_min,
											uint32_t dy_min);

/**
 * Updates the coding parameters if compression does not use progression order changes
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
static void pi_update_compress_no_poc(CodingParams *p_cp,
									uint16_t num_comps,
									uint16_t tileno,
									grk_rect_u32 tileBounds,
									uint64_t max_precincts,
									uint8_t max_res,
									uint32_t dx_min,
									uint32_t dy_min);
/**
 * Gets the compressing parameters needed to update the coding parameters and all the pocs.
 *
 * @param	p_image		the image being encoded.
 * @param	p_cp		the coding parameters.
 * @param	tileno		the tile index of the tile being encoded.
 * @param	tileBounds	tile bounds
 * @param	max_precincts	maximum number of precincts for all the bands of the tile
 * @param	max_res		maximum number of resolutions for all the poc inside the tile.
 * @param	dx_min		minimum dx of all the components of all the resolutions for the tile.
 * @param	dy_min		minimum dy of all the components of all the resolutions for the tile.
 */
static void grk_get_encoding_parameters(const grk_image *p_image,
										const CodingParams *p_cp,
										uint16_t tileno,
										grk_rect_u32 *tileBounds,
										uint32_t *dx_min,
										uint32_t *dy_min,
										uint64_t *max_precincts,
										uint8_t *max_res);

/**
 * Gets the compressing parameters needed to update the coding parameters and all the pocs.
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
 * @param	p_resolutions	pointer to an area corresponding to the one described above.
 */
static void grk_get_all_encoding_parameters(const grk_image *image,
											const CodingParams *p_cp,
											uint16_t tileno,
											uint32_t *tileBounds,
											uint32_t *dx_min,
											uint32_t *dy_min,
											uint64_t *max_precincts,
											uint8_t *max_res,
											uint32_t **p_resolutions);
/**
 * Allocates memory for a packet iterator. Data and data sizes are set by this operation.
 * No other data is set. The include section of the packet  iterator is not allocated.
 *
 * @param	p_image		the image used to initialize the packet iterator (in fact only the number of components is relevant.
 * @param	p_cp		the coding parameters.
 * @param	tileno	the index of the tile from which creating the packet iterator.
 */
static PacketIter* pi_create(const grk_image *p_image,
								const CodingParams *p_cp,
								uint16_t tileno,
								std::vector<uint8_t*> *include);
/**
 * Update decompress packet iterator with no POC
 */
static void pi_update_decompress_no_poc(PacketIter *p_pi,
									TileCodingParams *p_tcp,
									uint64_t max_precincts,
									uint8_t max_res);
/**
 * Upgrade decompress packet iterator with POC
 */
static void pi_update_decompress_poc(PacketIter *p_pi,
								TileCodingParams *p_tcp,
								uint64_t max_precincts);

/**
* Check if there is a remaining valid progression order
 */
static bool pi_check_next_for_valid_progression(int32_t prog,
												CodingParams *cp,
												uint16_t tileno,
												uint32_t pino,
												const char *progString);

static void update_pi_dxy(PacketIter *pi);
static void update_pi_dxy_for_comp(PacketIter *pi, grk_pi_comp *comp);

/*@}*/

/*@}*/

/*
 ==========================================================
 local functions
 ==========================================================
 */
static void update_pi_dxy_for_comp(PacketIter *pi, grk_pi_comp *comp) {
	for (uint32_t resno = 0; resno < comp->numresolutions; resno++) {
		auto res = comp->resolutions + resno;
		uint64_t dx = comp->dx
				* ((uint64_t) 1u
						<< (res->pdx + comp->numresolutions - 1 - resno));
		uint64_t dy = comp->dy
				* ((uint64_t) 1u
						<< (res->pdy + comp->numresolutions - 1 - resno));
		if (dx < UINT_MAX) {
			pi->dx =
					!pi->dx ?
							(uint32_t) dx :
							std::min<uint32_t>(pi->dx, (uint32_t) dx);
		}
		if (dy < UINT_MAX) {
			pi->dy =
					!pi->dy ?
							(uint32_t) dy :
							std::min<uint32_t>(pi->dy, (uint32_t) dy);
		}
	}
}
static void update_pi_dxy(PacketIter *pi) {
	pi->first = true;
	pi->dx = 0;
	pi->dy = 0;
	for (uint32_t compno = 0; compno < pi->numcomps; compno++)
		update_pi_dxy_for_comp(pi, pi->comps + compno);
}

static bool pi_next_lrcp(PacketIter *pi) {
	grk_pi_comp *comp = nullptr;
	grk_pi_resolution *res = nullptr;

	if (pi->first) {
		if (pi->compno >= pi->numcomps){
			GRK_ERROR("Packet iterator component %d must be strictly less than "
					"total number of components %d",pi->compno , pi->numcomps);
			return false;
		}
		comp = pi->comps + pi->compno;
		goto LABEL_SKIP;
	}
	for (pi->layno = pi->poc.layno0; pi->layno < pi->poc.layno1; pi->layno++) {
		for (pi->resno = pi->poc.resno0; pi->resno < pi->poc.resno1;pi->resno++) {
			for (pi->compno = pi->poc.compno0; pi->compno < pi->poc.compno1;pi->compno++) {
				comp = pi->comps + pi->compno;
				//skip resolutions greater than current component resolution
				if (pi->resno >= comp->numresolutions)
					continue;
				res = comp->resolutions + pi->resno;
				if (!pi->tp_on)
					pi->poc.precno1 = (uint64_t)res->pw * res->ph;

				for (pi->precinctIndex = pi->poc.precno0; pi->precinctIndex < pi->poc.precno1;
						pi->precinctIndex++) {

					//skip precinct numbers greater than total number of precincts
					// for this resolution
					if (pi->precinctIndex >= (uint64_t)res->pw * res->ph)
						continue;

					if (pi->update_include())
						return true;

					LABEL_SKIP: ;
				}
			}
		}
	}

	return false;
}

static bool pi_next_rlcp(PacketIter *pi) {
	grk_pi_comp *comp = nullptr;
	grk_pi_resolution *res = nullptr;

	if (pi->compno >= pi->numcomps){
		GRK_ERROR("Packet iterator component %d must be strictly less than "
				"total number of components %d",pi->compno , pi->numcomps);
		return false;
	}
	if (pi->first) {
		comp = pi->comps + pi->compno;
		goto LABEL_SKIP;
	}
	for (pi->resno = pi->poc.resno0; pi->resno < pi->poc.resno1; pi->resno++) {
		for (pi->layno = pi->poc.layno0; pi->layno < pi->poc.layno1;	pi->layno++) {
			for (pi->compno = pi->poc.compno0; pi->compno < pi->poc.compno1;pi->compno++) {
				comp = &pi->comps[pi->compno];
				if (pi->resno >= comp->numresolutions)
					continue;
				res = comp->resolutions + pi->resno;
				if (!pi->tp_on)
					pi->poc.precno1 = (uint64_t)res->pw * res->ph;
				for (pi->precinctIndex = pi->poc.precno0; pi->precinctIndex < pi->poc.precno1;
						pi->precinctIndex++) {
					//skip precinct numbers greater than total number of precincts
					// for this resolution
					if (pi->precinctIndex >= (uint64_t)res->pw * res->ph)
						continue;

					if (pi->update_include())
						return true;

					LABEL_SKIP: ;
				}
			}
		}
	}
	return false;
}

/**
 *
 * @return 0 : continue; 1 : do not continue
 */
static uint8_t pi_next_l(PacketIter *pi){
	uint32_t levelno;
	uint32_t trx0, try0;
	uint32_t trx1, try1;
	uint32_t rpx, rpy;
	uint32_t prci, prcj;

	if (pi->compno >= pi->numcomps){
		GRK_ERROR("Packet iterator component %d must be strictly less than "
				"total number of components %d",pi->compno , pi->numcomps);
		return false;
	}

	auto comp = pi->comps + pi->compno;
	if (pi->resno >= comp->numresolutions)
		return 0;

	auto res = comp->resolutions + pi->resno;
	levelno = comp->numresolutions - 1 - pi->resno;
	if (levelno >= GRK_J2K_MAXRLVLS)
		return 0;
	trx0 = ceildiv<uint64_t>((uint64_t) pi->tx0,
			((uint64_t) comp->dx << levelno));
	try0 = ceildiv<uint64_t>((uint64_t) pi->ty0,
			((uint64_t) comp->dy << levelno));
	trx1 = ceildiv<uint64_t>((uint64_t) pi->tx1,
			((uint64_t) comp->dx << levelno));
	try1 = ceildiv<uint64_t>((uint64_t) pi->ty1,
			((uint64_t) comp->dy << levelno));
	rpx = res->pdx + levelno;
	rpy = res->pdy + levelno;
	if (!(((uint64_t) pi->y % ((uint64_t) comp->dy << rpy) == 0)
			|| ((pi->y == pi->ty0)
					&& (((uint64_t) try0 << levelno)
							% ((uint64_t) 1 << rpy))))) {
		return 0;
	}
	if (!(((uint64_t) pi->x % ((uint64_t) comp->dx << rpx) == 0)
			|| ((pi->x == pi->tx0)
					&& (((uint64_t) trx0 << levelno)
							% ((uint64_t) 1 << rpx))))) {
		return 0;
	}

	if ((res->pw == 0) || (res->ph == 0))
		return 0;

	if ((trx0 == trx1) || (try0 == try1))
		return 0;

	prci = uint_floordivpow2(
			ceildiv<uint64_t>((uint64_t) pi->x,
					((uint64_t) comp->dx << levelno)), res->pdx)
			- uint_floordivpow2(trx0, res->pdx);
	prcj = uint_floordivpow2(
			ceildiv<uint64_t>((uint64_t) pi->y,
					((uint64_t) comp->dy << levelno)), res->pdy)
			- uint_floordivpow2(try0, res->pdy);
	pi->precinctIndex = (prci + (uint64_t)prcj * res->pw);
	//skip precinct numbers greater than total number of precincts
	// for this resolution
	if (pi->precinctIndex >= (uint64_t)res->pw * res->ph)
		return 0;

	return 1;
}

static bool pi_next_rpcl(PacketIter *pi) {
	if (pi->first) {
		goto LABEL_SKIP;
	} else {
		update_pi_dxy(pi);
	}
	if (!pi->tp_on) {
		pi->poc.ty0 = pi->ty0;
		pi->poc.tx0 = pi->tx0;
		pi->poc.ty1 = pi->ty1;
		pi->poc.tx1 = pi->tx1;
	}
	for (pi->resno = pi->poc.resno0; pi->resno < pi->poc.resno1; pi->resno++) {
		for (pi->y = pi->poc.ty0; pi->y < pi->poc.ty1;	pi->y += pi->dy - (pi->y % pi->dy)) {
			for (pi->x = pi->poc.tx0; pi->x < pi->poc.tx1;	pi->x += pi->dx - (pi->x % pi->dx)) {
				for (pi->compno = pi->poc.compno0; pi->compno < pi->poc.compno1; pi->compno++) {
					if (pi_next_l(pi) == 0)
						continue;
					for (pi->layno = pi->poc.layno0; pi->layno < pi->poc.layno1; pi->layno++) {
						if (pi->update_include())
							return true;
						LABEL_SKIP: ;
					}
				}
			}
		}
	}

	return false;
}

static bool pi_next_pcrl(PacketIter *pi) {
	grk_pi_comp *comp = nullptr;

	if (pi->compno >= pi->numcomps){
		GRK_ERROR("Packet iterator component %d must be strictly less than "
				"total number of components %d",pi->compno , pi->numcomps);
		return false;
	}

	if (pi->first) {
		comp = pi->comps + pi->compno;
		goto LABEL_SKIP;
	}

	update_pi_dxy(pi);
	if (!pi->tp_on) {
		pi->poc.ty0 = pi->ty0;
		pi->poc.tx0 = pi->tx0;
		pi->poc.ty1 = pi->ty1;
		pi->poc.tx1 = pi->tx1;
	}
	for (pi->y = pi->poc.ty0; pi->y < pi->poc.ty1;
			pi->y += pi->dy - (pi->y % pi->dy)) {
		for (pi->x = pi->poc.tx0; pi->x < pi->poc.tx1;
				pi->x += pi->dx - (pi->x % pi->dx)) {
			for (pi->compno = pi->poc.compno0; pi->compno < pi->poc.compno1; pi->compno++) {
				comp = &pi->comps[pi->compno];
				for (pi->resno = pi->poc.resno0; pi->resno
								< std::min<uint32_t>(pi->poc.resno1,
										comp->numresolutions); pi->resno++) {
					if (pi_next_l(pi) == 0)
						continue;
					for (pi->layno = pi->poc.layno0; pi->layno < pi->poc.layno1; pi->layno++) {
						if (pi->update_include())
							return true;
						LABEL_SKIP: ;
					}
				}
			}
		}
	}

	return false;
}

static bool pi_next_cprl(PacketIter *pi) {
	grk_pi_comp *comp = nullptr;

	if (pi->compno >= pi->numcomps){
		GRK_ERROR("Packet iterator component %d must be strictly less than "
				"total number of components %d",pi->compno , pi->numcomps);
		return false;
	}

	if (pi->first) {
		comp = &pi->comps[pi->compno];
		goto LABEL_SKIP;
	}
	for (pi->compno = pi->poc.compno0; pi->compno < pi->poc.compno1; pi->compno++) {
		comp = &pi->comps[pi->compno];
		pi->dx = 0;
		pi->dy = 0;
		update_pi_dxy_for_comp(pi, comp);
		if (!pi->tp_on) {
			pi->poc.ty0 = pi->ty0;
			pi->poc.tx0 = pi->tx0;
			pi->poc.ty1 = pi->ty1;
			pi->poc.tx1 = pi->tx1;
		}
		for (pi->y = pi->poc.ty0; pi->y < pi->poc.ty1;	pi->y += pi->dy - (pi->y % pi->dy)) {
			for (pi->x = pi->poc.tx0; pi->x < pi->poc.tx1;	pi->x += pi->dx - (pi->x % pi->dx)) {
				for (pi->resno = pi->poc.resno0; pi->resno
						< std::min<uint32_t>(pi->poc.resno1, comp->numresolutions); pi->resno++) {
					if (pi_next_l(pi) == 0)
						continue;
					for (pi->layno = pi->poc.layno0; pi->layno < pi->poc.layno1; pi->layno++) {
						if (pi->update_include())
							return true;
						LABEL_SKIP: ;
					}
				}
			}
		}
	}

	return false;
}


static void grk_get_encoding_parameters(const grk_image *image,
										const CodingParams *p_cp,
										uint16_t tileno,
										grk_rect_u32 *tileBounds,
										uint32_t *dx_min,
										uint32_t *dy_min,
										uint64_t *max_precincts,
										uint8_t *max_res) {
	assert(p_cp != nullptr);
	assert(image != nullptr);
	assert(tileno < p_cp->t_grid_width * p_cp->t_grid_height);

	/* position in x and y of tile */
	uint32_t tile_x = tileno % p_cp->t_grid_width;
	uint32_t tile_y = tileno / p_cp->t_grid_width;

	*tileBounds = p_cp->getTileBounds(image,tile_x,tile_y);

	*max_precincts = 0;
	*max_res = 0;
	*dx_min = UINT_MAX;
	*dy_min = UINT_MAX;

	auto tcp = &p_cp->tcps[tileno];
	for (uint32_t compno = 0; compno < image->numcomps; ++compno) {
		auto comp = image->comps + compno;
		auto tccp = tcp->tccps + compno;

		auto tileCompBounds = tileBounds->rectceildiv(comp->dx, comp->dy);
		if (tccp->numresolutions > *max_res)
			*max_res = tccp->numresolutions;

		/* use custom size for precincts */
		for (uint32_t resno = 0; resno < tccp->numresolutions; ++resno) {

			/* precinct width and height */
			uint32_t pdx = tccp->prcw[resno];
			uint32_t pdy = tccp->prch[resno];

			uint64_t dx = 	comp->dx* ((uint64_t) 1u << (pdx + tccp->numresolutions - 1 - resno));
			uint64_t dy = 	comp->dy* ((uint64_t) 1u << (pdy + tccp->numresolutions - 1 - resno));

			/* take the minimum size for dx for each comp and resolution */
			if (dx < UINT_MAX)
				*dx_min = std::min<uint32_t>(*dx_min, (uint32_t) dx);
			if (dy < UINT_MAX)
				*dy_min = std::min<uint32_t>(*dy_min, (uint32_t) dy);

			uint32_t level_no = tccp->numresolutions - 1 - resno;
			auto resBounds = tileCompBounds.rectceildivpow2(level_no);
			uint32_t px0 = uint_floordivpow2(resBounds.x0, pdx) << pdx;
			uint32_t py0 = uint_floordivpow2(resBounds.y0, pdy) << pdy;
			uint32_t px1 = ceildivpow2<uint32_t>(resBounds.x1, pdx) << pdx;
			uint32_t py1 = ceildivpow2<uint32_t>(resBounds.y1, pdy) << pdy;
			uint32_t pw = (resBounds.width() == 0) ? 0 : ((px1 - px0) >> pdx);
			uint32_t ph = (resBounds.height() ==0 ) ? 0 : ((py1 - py0) >> pdy);
			uint64_t product = (uint64_t)pw * ph;
			if (product > *max_precincts)
				*max_precincts = product;
		}
	}
}

static void grk_get_all_encoding_parameters(const grk_image *image,
											const CodingParams *p_cp,
											uint16_t tileno,
											grk_rect_u32 *tileBounds,
											uint32_t *dx_min,
											uint32_t *dy_min,
											uint64_t *max_precincts,
											uint8_t *max_res,
											uint32_t **p_resolutions) {
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

	auto tcp = &p_cp->tcps[tileno];
	for (uint32_t compno = 0; compno < image->numcomps; ++compno) {
		uint32_t level_no;
		auto lResolutionPtr = p_resolutions[compno];
		auto tccp = tcp->tccps + compno;
		auto comp = image->comps + compno;

		auto tileCompBounds = tileBounds->rectceildiv(comp->dx,comp->dy);
		if (tccp->numresolutions > *max_res)
			*max_res = tccp->numresolutions;

		/* use custom size for precincts*/
		level_no = tccp->numresolutions - 1;
		for (uint32_t resno = 0; resno < tccp->numresolutions; ++resno) {
			uint32_t pdx = tccp->prcw[resno];
			uint32_t pdy = tccp->prch[resno];
			*lResolutionPtr++ = pdx;
			*lResolutionPtr++ = pdy;

			uint64_t dx = comp->dx * ((uint64_t) 1u << (pdx + level_no));
			uint64_t dy = comp->dy * ((uint64_t) 1u << (pdy + level_no));
			if (dx < UINT_MAX)
				*dx_min = std::min<uint32_t>(*dx_min, (uint32_t) dx);
			if (dy < UINT_MAX)
				*dy_min = std::min<uint32_t>(*dy_min, (uint32_t) dy);
			auto resBounds 	= tileCompBounds.rectceildivpow2(level_no);
			uint32_t px0 	= uint_floordivpow2(resBounds.x0, pdx) << pdx;
			uint32_t py0 	= uint_floordivpow2(resBounds.y0, pdy) << pdy;
			uint32_t px1 	= ceildivpow2<uint32_t>(resBounds.x1, pdx) << pdx;
			uint32_t py1 	= ceildivpow2<uint32_t>(resBounds.y1, pdy) << pdy;
			uint32_t pw 	= (resBounds.width()==0) ? 0 : ((px1 - px0) >> pdx);
			uint32_t ph 	= (resBounds.height()==0) ? 0 : ((py1 - py0) >> pdy);
			*lResolutionPtr++ = pw;
			*lResolutionPtr++ = ph;
			uint64_t product = (uint64_t)pw * ph;
			if (product > *max_precincts)
				*max_precincts = product;
			--level_no;
		}
	}
}

static PacketIter* pi_create(const grk_image *image,
							const CodingParams *cp,
							uint16_t tileno,
							std::vector<uint8_t*> *include) {
	assert(cp != nullptr);
	assert(image != nullptr);
	assert(tileno < cp->t_grid_width * cp->t_grid_height);
	auto tcp = &cp->tcps[tileno];
	uint32_t poc_bound = tcp->numpocs + 1;

	auto pi = new PacketIter[poc_bound];
	for (uint32_t i = 0; i < poc_bound; ++i)
		pi[i].include = include;
	for (uint32_t pino = 0; pino < poc_bound; ++pino) {
		auto current_pi = pi + pino;

		current_pi->comps = (grk_pi_comp*) grk_calloc(image->numcomps,
				sizeof(grk_pi_comp));
		if (!current_pi->comps) {
			pi_destroy(pi);
			return nullptr;
		}
		current_pi->numcomps = image->numcomps;
		for (uint32_t compno = 0; compno < image->numcomps; ++compno) {
			grk_pi_comp *comp = current_pi->comps + compno;
			auto tccp = &tcp->tccps[compno];
			comp->resolutions = (grk_pi_resolution*) grk_calloc(
					tccp->numresolutions, sizeof(grk_pi_resolution));

			if (!comp->resolutions) {
				pi_destroy(pi);
				return nullptr;
			}
			comp->numresolutions = tccp->numresolutions;
		}
	}
	return pi;
}

static void pi_update_compress_poc_and_final(CodingParams *p_cp,
											uint16_t tileno,
											grk_rect_u32 tileBounds,
											uint64_t max_precincts,
											uint32_t dx_min,
											uint32_t dy_min) {
	assert(p_cp != nullptr);
	assert(tileno < p_cp->t_grid_width * p_cp->t_grid_height);

	auto tcp = &p_cp->tcps[tileno];
	/* number of iterations in the loop */
	uint32_t poc_bound = tcp->numpocs + 1;

	for (uint32_t pino = 0; pino < poc_bound; ++pino) {
		auto current_poc = tcp->pocs + pino;

		current_poc->compS = current_poc->compno0;
		current_poc->compE = current_poc->compno1;
		current_poc->resS = current_poc->resno0;
		current_poc->resE = current_poc->resno1;
		current_poc->layE = current_poc->layno1;
		current_poc->prg = current_poc->prg1;
		current_poc->prcE = max_precincts;
		current_poc->txS = tileBounds.x0;
		current_poc->txE = tileBounds.x1;
		current_poc->tyS = tileBounds.y0;
		current_poc->tyE = tileBounds.y1;
		current_poc->dx = dx_min;
		current_poc->dy = dy_min;
	}
}

static void pi_update_compress_no_poc(CodingParams *p_cp,
									uint16_t num_comps,
									uint16_t tileno,
									grk_rect_u32 tileBounds,
									uint64_t max_precincts,
									uint8_t max_res,
									uint32_t dx_min,
									uint32_t dy_min) {
	assert(p_cp != nullptr);
	assert(tileno < p_cp->t_grid_width * p_cp->t_grid_height);

	auto tcp = &p_cp->tcps[tileno];

	/* number of iterations in the loop */
	uint32_t poc_bound = tcp->numpocs + 1;

	for (uint32_t pino = 0; pino < poc_bound; ++pino) {
		auto current_poc = tcp->pocs + pino;

		current_poc->compS = 0;
		current_poc->compE = num_comps;
		current_poc->resS = 0;
		current_poc->resE = max_res;
		current_poc->layE = tcp->numlayers;
		current_poc->prg = tcp->prg;
		current_poc->prcE = max_precincts;
		current_poc->txS = tileBounds.x0;
		current_poc->txE = tileBounds.x1;
		current_poc->tyS = tileBounds.y0;
		current_poc->tyE = tileBounds.y1;
		current_poc->dx = dx_min;
		current_poc->dy = dy_min;
	}
}

static void pi_update_decompress_poc(PacketIter *p_pi,
								TileCodingParams *p_tcp,
								uint64_t max_precincts) {
	assert(p_pi != nullptr);
	assert(p_tcp != nullptr);

	uint32_t bound = p_tcp->numpocs + 1;

	for (uint32_t pino = 0; pino < bound; ++pino) {
		auto current_pi = p_pi + pino;
		auto current_poc = p_tcp->pocs + pino;

		current_pi->poc.prg = current_poc->prg; /* Progression Order #0 */
		current_pi->first = false;
		current_pi->poc.resno0 = current_poc->resno0; /* Resolution Level Index #0 (Start) */
		current_pi->poc.compno0 = current_poc->compno0; /* Component Index #0 (Start) */
		current_pi->poc.layno0 = 0;
		current_pi->poc.precno0 = 0;
		current_pi->poc.resno1 = current_poc->resno1; /* Resolution Level Index #0 (End) */
		current_pi->poc.compno1 = current_poc->compno1; /* Component Index #0 (End) */
		current_pi->poc.layno1 = std::min<uint16_t>(current_poc->layno1,
				p_tcp->numlayers); /* Layer Index #0 (End) */
		current_pi->poc.precno1 = max_precincts;
	}
}

static void pi_update_decompress_no_poc(PacketIter *p_pi,
									TileCodingParams *p_tcp,
									uint64_t max_precincts,
									uint8_t max_res) {
	assert(p_tcp != nullptr);
	assert(p_pi != nullptr);

	uint32_t bound = p_tcp->numpocs + 1;

	for (uint32_t pino = 0; pino < bound; ++pino) {
		auto current_pi = p_pi + pino;

		current_pi->poc.prg = p_tcp->prg;
		current_pi->first = false;
		current_pi->poc.resno0 = 0;
		current_pi->poc.compno0 = 0;
		current_pi->poc.layno0 = 0;
		current_pi->poc.precno0 = 0;
		current_pi->poc.resno1 = max_res;
		current_pi->poc.compno1 = current_pi->numcomps;
		current_pi->poc.layno1 = p_tcp->numlayers;
		current_pi->poc.precno1 = max_precincts;
	}
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
	auto poc = tcps->pocs + pino;

	if (prog >= 0) {
		switch (progString[prog]) {
		case 'R':
			if (poc->res_t == poc->resE)
				return pi_check_next_for_valid_progression(prog - 1, cp, tileno, pino, progString);
			else
				return true;
			break;
		case 'C':
			if (poc->comp_t == poc->compE)
				return pi_check_next_for_valid_progression(prog - 1, cp, tileno, pino, progString);
			else
				return true;
			break;
		case 'L':
			if (poc->lay_t == poc->layE)
				return pi_check_next_for_valid_progression(prog - 1, cp, tileno, pino, progString);
			else
				return true;
			break;
		case 'P':
			switch (poc->prg) {
			case GRK_LRCP: /* fall through */
			case GRK_RLCP:
				if (poc->prc_t == poc->prcE)
					return pi_check_next_for_valid_progression(prog - 1, cp, tileno, pino,	progString);
				else
					return true;
				break;
			default:
				if (poc->tx0_t == poc->txE) {
					/*TY*/
					if (poc->ty0_t == poc->tyE)
						return pi_check_next_for_valid_progression(prog - 1, cp, tileno, pino,	progString);
					else
						return true;
					/*TY*/
				} else {
					return true;
				}
				break;
			}/*end case P*/
		}/*end switch*/
	}/*end if*/
	return false;
}

/*
 ==========================================================
 Packet iterator interface
 ==========================================================
 */
PacketIter* pi_create_decompress(grk_image *p_image,
								CodingParams *p_cp,
								uint16_t tile_no,
								std::vector<uint8_t*> *include) {
	assert(p_cp != nullptr);
	assert(p_image != nullptr);
	assert(tile_no < p_cp->t_grid_width * p_cp->t_grid_height);

	auto tcp = &p_cp->tcps[tile_no];
	uint32_t bound = tcp->numpocs + 1;

	uint32_t data_stride = 4 * GRK_J2K_MAXRLVLS;
	auto tmp_data = (uint32_t*) grk_malloc((size_t)data_stride * p_image->numcomps * sizeof(uint32_t));
	if (!tmp_data)
		return nullptr;
	auto tmp_ptr = (uint32_t**) grk_malloc(p_image->numcomps * sizeof(uint32_t*));
	if (!tmp_ptr) {
		grk_free(tmp_data);
		return nullptr;
	}

	/* memory allocation for pi */
	auto pi = pi_create(p_image, p_cp, tile_no, include);
	if (!pi) {
		grk_free(tmp_data);
		grk_free(tmp_ptr);
		return nullptr;
	}

	auto encoding_value_ptr = tmp_data;
	/* update pointer array */
	for (uint32_t compno = 0; compno < p_image->numcomps; ++compno) {
		tmp_ptr[compno] = encoding_value_ptr;
		encoding_value_ptr += data_stride;
	}

	uint8_t max_res;
	uint64_t max_precincts;
	grk_rect_u32 tileBounds;
	uint32_t dx_min, dy_min;
	grk_get_all_encoding_parameters(p_image, p_cp, tile_no, &tileBounds, &dx_min, &dy_min, &max_precincts, &max_res, tmp_ptr);

	/* step calculations */
	uint32_t step_p = 1;
	uint64_t step_c = max_precincts * step_p;
	uint64_t step_r = p_image->numcomps * step_c;
	uint64_t step_l = max_res * step_r;

	/* set values for first packet iterator */
	auto current_pi = pi;

	/* special treatment for the first packet iterator */
	current_pi->tx0 = tileBounds.x0;
	current_pi->ty0 = tileBounds.y0;
	current_pi->tx1 = tileBounds.x1;
	current_pi->ty1 = tileBounds.y1;

	current_pi->step_p = step_p;
	current_pi->step_c = step_c;
	current_pi->step_r = step_r;
	current_pi->step_l = step_l;

	/* allocation for components and number of components has already been calculated by pi_create */
	for (uint32_t compno = 0; compno < current_pi->numcomps; ++compno) {
		auto current_comp = current_pi->comps + compno;
		auto img_comp = p_image->comps + compno;
		encoding_value_ptr = tmp_ptr[compno];

		current_comp->dx = img_comp->dx;
		current_comp->dy = img_comp->dy;
		/* resolutions have already been initialized */
		for (uint32_t resno = 0; resno < current_comp->numresolutions;	resno++) {
			auto res = current_comp->resolutions + resno;

			res->pdx = *(encoding_value_ptr++);
			res->pdy = *(encoding_value_ptr++);
			res->pw = *(encoding_value_ptr++);
			res->ph = *(encoding_value_ptr++);
		}
	}
	for (uint32_t pino = 1; pino < bound; ++pino) {
		current_pi = pi + pino;
		current_pi->tx0 = tileBounds.x0;
		current_pi->ty0 = tileBounds.y0;
		current_pi->tx1 = tileBounds.x1;
		current_pi->ty1 = tileBounds.y1;
		/*current_pi->dx = dx_min;*/
		/*current_pi->dy = dy_min;*/
		current_pi->step_p = step_p;
		current_pi->step_c = step_c;
		current_pi->step_r = step_r;
		current_pi->step_l = step_l;

		/* allocation for components and number of components has already been calculated by pi_create */
		for (uint32_t compno = 0; compno < current_pi->numcomps; ++compno) {
			auto current_comp = current_pi->comps + compno;
			auto img_comp = p_image->comps + compno;

			encoding_value_ptr = tmp_ptr[compno];

			current_comp->dx = img_comp->dx;
			current_comp->dy = img_comp->dy;
			/* resolutions have already been initialized */
			for (uint32_t resno = 0; resno < current_comp->numresolutions;	resno++) {
				auto res = current_comp->resolutions + resno;

				res->pdx = *(encoding_value_ptr++);
				res->pdy = *(encoding_value_ptr++);
				res->pw = *(encoding_value_ptr++);
				res->ph = *(encoding_value_ptr++);
			}
		}
	}
	grk_free(tmp_data);
	grk_free(tmp_ptr);
	if (tcp->POC)
		pi_update_decompress_poc(pi, tcp, max_precincts);
	else
		pi_update_decompress_no_poc(pi, tcp, max_precincts, max_res);

	return pi;
}

PacketIter* pi_create_compress(const grk_image *p_image,
								CodingParams *p_cp,
								uint16_t tile_no,
								J2K_T2_MODE p_t2_mode,
								std::vector<uint8_t*> *include) {
	assert(p_cp != nullptr);
	assert(p_image != nullptr);
	assert(tile_no < p_cp->t_grid_width * p_cp->t_grid_height);

	auto tcp = &p_cp->tcps[tile_no];
	uint32_t bound = tcp->numpocs + 1;
	uint32_t data_stride = 4 * GRK_J2K_MAXRLVLS;
	auto tmp_data = (uint32_t*) grk_malloc((size_t)data_stride * p_image->numcomps * sizeof(uint32_t));
	if (!tmp_data) {
		return nullptr;
	}

	auto tmp_ptr = (uint32_t**) grk_malloc(p_image->numcomps * sizeof(uint32_t*));
	if (!tmp_ptr) {
		grk_free(tmp_data);
		return nullptr;
	}

	auto pi = pi_create(p_image, p_cp, tile_no,include);
	if (!pi) {
		grk_free(tmp_data);
		grk_free(tmp_ptr);
		return nullptr;
	}

	auto encoding_value_ptr = tmp_data;
	for (uint32_t compno = 0; compno < p_image->numcomps; ++compno) {
		tmp_ptr[compno] = encoding_value_ptr;
		encoding_value_ptr += data_stride;
	}

	uint8_t max_res;
	uint64_t max_precincts;
	grk_rect_u32 tileBounds;
	uint32_t dx_min, dy_min;
	grk_get_all_encoding_parameters(p_image, p_cp, tile_no, &tileBounds, &dx_min, &dy_min, &max_precincts, &max_res, tmp_ptr);

	uint32_t step_p = 1;
	uint64_t step_c = max_precincts * step_p;
	uint64_t step_r = p_image->numcomps * step_c;
	uint64_t step_l = max_res * step_r;

	/* set values for first packet iterator*/
	pi->tp_on = p_cp->m_coding_params.m_enc.m_tp_on;
	auto current_pi = pi;

	/* special treatment for the first packet iterator*/
	current_pi->tx0 = tileBounds.x0;
	current_pi->ty0 = tileBounds.y0;
	current_pi->tx1 = tileBounds.x1;
	current_pi->ty1 = tileBounds.y1;
	current_pi->dx = dx_min;
	current_pi->dy = dy_min;
	current_pi->step_p = step_p;
	current_pi->step_c = step_c;
	current_pi->step_r = step_r;
	current_pi->step_l = step_l;

	/* allocation for components and number of components has already been calculated by pi_create */
	for (uint32_t compno = 0; compno < current_pi->numcomps; ++compno) {
		encoding_value_ptr = tmp_ptr[compno];

		auto current_comp = current_pi->comps + compno;
		auto img_comp = p_image->comps + compno;

		current_comp->dx = img_comp->dx;
		current_comp->dy = img_comp->dy;

		/* resolutions have already been initialized */
		for (uint32_t resno = 0; resno < current_comp->numresolutions;
				resno++) {
			auto res = current_comp->resolutions + resno;

			res->pdx = *(encoding_value_ptr++);
			res->pdy = *(encoding_value_ptr++);
			res->pw = *(encoding_value_ptr++);
			res->ph = *(encoding_value_ptr++);
		}
	}
	for (uint32_t pino = 1; pino < bound; ++pino) {
		current_pi = pi + pino;

		current_pi->tx0 = tileBounds.x0;
		current_pi->ty0 = tileBounds.y0;
		current_pi->tx1 = tileBounds.x1;
		current_pi->ty1 = tileBounds.y1;
		current_pi->dx = dx_min;
		current_pi->dy = dy_min;
		current_pi->step_p = step_p;
		current_pi->step_c = step_c;
		current_pi->step_r = step_r;
		current_pi->step_l = step_l;

		/* allocation for components and number of components
		 *  has already been calculated by pi_create */
		for (uint32_t compno = 0; compno < current_pi->numcomps; ++compno) {
			auto current_comp = current_pi->comps + compno;
			auto img_comp = p_image->comps + compno;

			encoding_value_ptr = tmp_ptr[compno];

			current_comp->dx = img_comp->dx;
			current_comp->dy = img_comp->dy;
			/* resolutions have already been initialized */
			for (uint32_t resno = 0; resno < current_comp->numresolutions; resno++) {
				auto res = current_comp->resolutions + resno;

				res->pdx = *(encoding_value_ptr++);
				res->pdy = *(encoding_value_ptr++);
				res->pw = *(encoding_value_ptr++);
				res->ph = *(encoding_value_ptr++);
			}
		}
	}

	grk_free(tmp_data);
	tmp_data = nullptr;
	grk_free(tmp_ptr);
	tmp_ptr = nullptr;

	if (tcp->POC && (GRK_IS_CINEMA(p_cp->rsiz) || p_t2_mode == FINAL_PASS))
		pi_update_compress_poc_and_final(p_cp, tile_no, tileBounds,
				max_precincts, dx_min, dy_min);
	else
		pi_update_compress_no_poc(p_cp, p_image->numcomps, tile_no, tileBounds, max_precincts, max_res, dx_min, dy_min);

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
	auto poc = tcps->pocs + pino;
	auto prog = j2k_convert_progression_order(poc->prg);

	pi[pino].first = false;
	pi[pino].poc.prg = poc->prg;

	if (!(cp->m_coding_params.m_enc.m_tp_on
			&& ((!GRK_IS_CINEMA(cp->rsiz) && !GRK_IS_IMF(cp->rsiz)
					&& (t2_mode == FINAL_PASS)) || GRK_IS_CINEMA(cp->rsiz)
					|| GRK_IS_IMF(cp->rsiz)))) {
		pi[pino].poc.resno0 = poc->resS;
		pi[pino].poc.resno1 = poc->resE;
		pi[pino].poc.compno0 = poc->compS;
		pi[pino].poc.compno1 = poc->compE;
		pi[pino].poc.layno0 = 0;
		pi[pino].poc.layno1 = poc->layE;
		pi[pino].poc.precno0 = 0;
		pi[pino].poc.precno1 = poc->prcE;
		pi[pino].poc.tx0 = poc->txS;
		pi[pino].poc.ty0 = poc->tyS;
		pi[pino].poc.tx1 = poc->txE;
		pi[pino].poc.ty1 = poc->tyE;
	} else {
		for (uint32_t i = tppos + 1; i < 4; i++) {
			switch (prog[i]) {
			case 'R':
				pi[pino].poc.resno0 = poc->resS;
				pi[pino].poc.resno1 = poc->resE;
				break;
			case 'C':
				pi[pino].poc.compno0 = poc->compS;
				pi[pino].poc.compno1 = poc->compE;
				break;
			case 'L':
				pi[pino].poc.layno0 = 0;
				pi[pino].poc.layno1 = poc->layE;
				break;
			case 'P':
				switch (poc->prg) {
				case GRK_LRCP:
				case GRK_RLCP:
					pi[pino].poc.precno0 = 0;
					pi[pino].poc.precno1 = poc->prcE;
					break;
				default:
					pi[pino].poc.tx0 = poc->txS;
					pi[pino].poc.ty0 = poc->tyS;
					pi[pino].poc.tx1 = poc->txE;
					pi[pino].poc.ty1 = poc->tyE;
					break;
				}
				break;
			}
		}

		if (first_poc_tile_part) {
			for (int32_t i = (int32_t) tppos; i >= 0; i--) {
				switch (prog[i]) {
				case 'C':
					poc->comp_t = poc->compS;
					pi[pino].poc.compno0 = poc->comp_t;
					pi[pino].poc.compno1 = poc->comp_t + 1;
					poc->comp_t += 1;
					break;
				case 'R':
					poc->res_t = poc->resS;
					pi[pino].poc.resno0 = poc->res_t;
					pi[pino].poc.resno1 = poc->res_t + 1;
					poc->res_t += 1;
					break;
				case 'L':
					poc->lay_t = 0;
					pi[pino].poc.layno0 = poc->lay_t;
					pi[pino].poc.layno1 = poc->lay_t + 1;
					poc->lay_t += 1;
					break;
				case 'P':
					switch (poc->prg) {
					case GRK_LRCP:
					case GRK_RLCP:
						poc->prc_t = 0;
						pi[pino].poc.precno0 = poc->prc_t;
						pi[pino].poc.precno1 = poc->prc_t + 1;
						poc->prc_t += 1;
						break;
					default:
						poc->tx0_t = poc->txS;
						poc->ty0_t = poc->tyS;
						pi[pino].poc.tx0 = poc->tx0_t;
						pi[pino].poc.tx1 = (poc->tx0_t + poc->dx
								- (poc->tx0_t % poc->dx));
						pi[pino].poc.ty0 = poc->ty0_t;
						pi[pino].poc.ty1 = (poc->ty0_t + poc->dy
								- (poc->ty0_t % poc->dy));
						poc->tx0_t = pi[pino].poc.tx1;
						poc->ty0_t = pi[pino].poc.ty1;
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
					pi[pino].poc.compno0 = poc->comp_t - 1;
					pi[pino].poc.compno1 = poc->comp_t;
					break;
				case 'R':
					pi[pino].poc.resno0 = poc->res_t - 1;
					pi[pino].poc.resno1 = poc->res_t;
					break;
				case 'L':
					pi[pino].poc.layno0 = poc->lay_t - 1;
					pi[pino].poc.layno1 = poc->lay_t;
					break;
				case 'P':
					switch (poc->prg) {
					case GRK_LRCP:
					case GRK_RLCP:
						pi[pino].poc.precno0 = poc->prc_t - 1;
						pi[pino].poc.precno1 = poc->prc_t;
						break;
					default:
						pi[pino].poc.tx0 = (poc->tx0_t - poc->dx
								- (poc->tx0_t % poc->dx));
						pi[pino].poc.tx1 = poc->tx0_t;
						pi[pino].poc.ty0 = (poc->ty0_t - poc->dy
								- (poc->ty0_t % poc->dy));
						pi[pino].poc.ty1 = poc->ty0_t;
						break;
					}
					break;
				}
				if (incr_top == 1) {
					switch (prog[i]) {
					case 'R':
						if (poc->res_t == poc->resE) {
							if (pi_check_next_for_valid_progression(i - 1, cp, tileno, pino,
									prog)) {
								poc->res_t = poc->resS;
								pi[pino].poc.resno0 = poc->res_t;
								pi[pino].poc.resno1 = poc->res_t + 1;
								poc->res_t += 1;
								incr_top = 1;
							} else {
								incr_top = 0;
							}
						} else {
							pi[pino].poc.resno0 = poc->res_t;
							pi[pino].poc.resno1 = poc->res_t + 1;
							poc->res_t += 1;
							incr_top = 0;
						}
						break;
					case 'C':
						if (poc->comp_t == poc->compE) {
							if (pi_check_next_for_valid_progression(i - 1, cp, tileno, pino,
									prog)) {
								poc->comp_t = poc->compS;
								pi[pino].poc.compno0 = poc->comp_t;
								pi[pino].poc.compno1 = poc->comp_t + 1;
								poc->comp_t += 1;
								incr_top = 1;
							} else {
								incr_top = 0;
							}
						} else {
							pi[pino].poc.compno0 = poc->comp_t;
							pi[pino].poc.compno1 = poc->comp_t + 1;
							poc->comp_t += 1;
							incr_top = 0;
						}
						break;
					case 'L':
						if (poc->lay_t == poc->layE) {
							if (pi_check_next_for_valid_progression(i - 1, cp, tileno, pino,
									prog)) {
								poc->lay_t = 0;
								pi[pino].poc.layno0 = poc->lay_t;
								pi[pino].poc.layno1 = poc->lay_t + 1;
								poc->lay_t += 1;
								incr_top = 1;
							} else {
								incr_top = 0;
							}
						} else {
							pi[pino].poc.layno0 = poc->lay_t;
							pi[pino].poc.layno1 = poc->lay_t + 1;
							poc->lay_t += 1;
							incr_top = 0;
						}
						break;
					case 'P':
						switch (poc->prg) {
						case GRK_LRCP:
						case GRK_RLCP:
							if (poc->prc_t == poc->prcE) {
								if (pi_check_next_for_valid_progression(i - 1, cp, tileno, pino,
										prog)) {
									poc->prc_t = 0;
									pi[pino].poc.precno0 = poc->prc_t;
									pi[pino].poc.precno1 = poc->prc_t + 1;
									poc->prc_t += 1;
									incr_top = 1;
								} else {
									incr_top = 0;
								}
							} else {
								pi[pino].poc.precno0 = poc->prc_t;
								pi[pino].poc.precno1 = poc->prc_t + 1;
								poc->prc_t += 1;
								incr_top = 0;
							}
							break;
						default:
							if (poc->tx0_t >= poc->txE) {
								if (poc->ty0_t >= poc->tyE) {
									if (pi_check_next_for_valid_progression(i - 1, cp, tileno,
											pino, prog)) {
										poc->ty0_t = poc->tyS;
										pi[pino].poc.ty0 = poc->ty0_t;
										pi[pino].poc.ty1 =
												(uint32_t) (poc->ty0_t + poc->dy
														- (poc->ty0_t % poc->dy));
										poc->ty0_t = pi[pino].poc.ty1;
										incr_top = 1;
										resetX = 1;
									} else {
										incr_top = 0;
										resetX = 0;
									}
								} else {
									pi[pino].poc.ty0 = poc->ty0_t;
									pi[pino].poc.ty1 = (poc->ty0_t + poc->dy
											- (poc->ty0_t % poc->dy));
									poc->ty0_t = pi[pino].poc.ty1;
									incr_top = 0;
									resetX = 1;
								}
								if (resetX == 1) {
									poc->tx0_t = poc->txS;
									pi[pino].poc.tx0 = poc->tx0_t;
									pi[pino].poc.tx1 = (uint32_t) (poc->tx0_t
											+ poc->dx - (poc->tx0_t % poc->dx));
									poc->tx0_t = pi[pino].poc.tx1;
								}
							} else {
								pi[pino].poc.tx0 = poc->tx0_t;
								pi[pino].poc.tx1 = (uint32_t) (poc->tx0_t
										+ poc->dx - (poc->tx0_t % poc->dx));
								poc->tx0_t = pi[pino].poc.tx1;
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
}

void pi_destroy(PacketIter *p_pi) {
	if (p_pi) {
		p_pi->destroy_include();
		delete[] p_pi;
	}
}

void pi_update_encoding_parameters(const grk_image *p_image,
									CodingParams *p_cp,
									uint16_t tile_no) {
	assert(p_cp != nullptr);
	assert(p_image != nullptr);
	assert(tile_no < p_cp->t_grid_width * p_cp->t_grid_height);

	auto tcp = p_cp->tcps + tile_no;
	uint8_t max_res;
	uint64_t max_precincts;
	grk_rect_u32 tileBounds;
	uint32_t dx_min, dy_min;
	grk_get_encoding_parameters(p_image,
								p_cp,
								tile_no,
								&tileBounds,
								&dx_min,
								&dy_min,
								&max_precincts,
								&max_res);

	if (tcp->POC)
		pi_update_compress_poc_and_final(p_cp,
										tile_no,
										tileBounds,
										max_precincts,
										dx_min,
										dy_min);
	else
		pi_update_compress_no_poc(p_cp,
								p_image->numcomps,
								tile_no,
								tileBounds,
								max_precincts,
								max_res,
								dx_min,
								dy_min);
}

bool pi_next(PacketIter *pi) {
	switch (pi->poc.prg) {
		case GRK_LRCP:
			return pi_next_lrcp(pi);
		case GRK_RLCP:
			return pi_next_rlcp(pi);
		case GRK_RPCL:
			return pi_next_rpcl(pi);
		case GRK_PCRL:
			return pi_next_pcrl(pi);
		case GRK_CPRL:
			return pi_next_cprl(pi);
		default:
			return false;
	}

	return false;
}

PacketIter::PacketIter() : tp_on(false),
							include(nullptr),
							step_l(0),
							step_r(0),
							step_c(0),
							step_p(0),
							compno(0),
							resno(0),
							precinctIndex(0),
							layno(0),
							first(true),
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
	memset(&poc, 0, sizeof(poc));
}

PacketIter::~PacketIter(){
	if (comps) {
		for (uint32_t compno = 0; compno < numcomps;
				compno++) {
			auto current_component = comps + compno;

			grk_free(current_component->resolutions);
		}
		grk_free(comps);
	}
}

uint8_t* PacketIter::get_include(uint16_t layerno){
	assert(layerno <= include->size());
	if (layerno == include->size()){
		size_t len = (step_l + 7)/8;
		auto buf = new uint8_t[len];
		memset(buf, 0, len);
		include->push_back(buf);
		return buf;
	}
	return include->operator[](layerno);
}

bool PacketIter::update_include(void){
	uint64_t index = resno * step_r + compno * step_c + precinctIndex * step_p;
	assert(index < step_l);

	auto include = get_include(layno);
	uint64_t include_index 	= (index >> 3);
	uint32_t shift 	= (index & 7);
	bool rc = false;
	uint8_t val = include[include_index];
	if ( ((val >> shift)& 1) == 0 ) {
		include[include_index] = (uint8_t)(val | (1 << shift));
		rc = true;
	}

	return rc;
}

void PacketIter::destroy_include(void){
	for (auto it = include->begin(); it != include->end(); ++it)
		delete[] *it;
	include->clear();
}

}
