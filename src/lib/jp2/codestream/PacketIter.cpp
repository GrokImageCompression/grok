/*
 *    Copyright (C) 2016-2020 Grok Image Compression Inc.
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
 * Updates the coding parameters if the compressing is used with Progression order changes and final (or cinema parameters are used).
 *
 * @param	p_cp		the coding parameters to modify
 * @param	tileno	the tile index being concerned.
 * @param	tx0		X0 parameter for the tile
 * @param	tx1		X1 parameter for the tile
 * @param	ty0		Y0 parameter for the tile
 * @param	ty1		Y1 parameter for the tile
 * @param	max_precincts	the maximum number of precincts for all the bands of the tile
 * @param	dx_min		the minimum dx of all the components of all the resolutions for the tile.
 * @param	dy_min		the minimum dy of all the components of all the resolutions for the tile.
 */
static void pi_update_encode_poc_and_final(CodingParams *p_cp,
		uint16_t tileno, uint32_t tx0, uint32_t tx1, uint32_t ty0, uint32_t ty1,
		uint64_t max_precincts, uint32_t dx_min, uint32_t dy_min);

/**
 * Updates the coding parameters if the compressing is not used with Progression order changes and final (and cinema parameters are used).
 *
 * @param	p_cp		the coding parameters to modify
 * @param	num_comps		the number of components
 * @param	tileno	the tile index being concerned.
 * @param	tx0		X0 parameter for the tile
 * @param	tx1		X1 parameter for the tile
 * @param	ty0		Y0 parameter for the tile
 * @param	ty1		Y1 parameter for the tile
 * @param	max_precincts	the maximum number of precincts for all the bands of the tile
 * @param	max_res	the maximum number of resolutions for all the poc inside the tile.
 * @param	dx_min		the minimum dx of all the components of all the resolutions for the tile.
 * @param	dy_min		the minimum dy of all the components of all the resolutions for the tile.
 */
static void pi_update_encode_no_poc(CodingParams *p_cp,
		uint16_t num_comps, uint16_t tileno, uint32_t tx0, uint32_t tx1,
		uint32_t ty0, uint32_t ty1, uint64_t max_precincts, uint8_t max_res,
		uint32_t dx_min, uint32_t dy_min);
/**
 * Gets the compressing parameters needed to update the coding parameters and all the pocs.
 *
 * @param	p_image		the image being encoded.
 * @param	p_cp		the coding parameters.
 * @param	tileno		the tile index of the tile being encoded.
 * @param	tx0			X0 parameter for the tile
 * @param	tx1			X1 parameter for the tile
 * @param	ty0			Y0 parameter for the tile
 * @param	ty1			Y1 parameter for the tile
 * @param	max_precincts	maximum number of precincts for all the bands of the tile
 * @param	max_res		maximum number of resolutions for all the poc inside the tile.
 * @param	dx_min		minimum dx of all the components of all the resolutions for the tile.
 * @param	dy_min		minimum dy of all the components of all the resolutions for the tile.
 */
static void grk_get_encoding_parameters(const grk_image *p_image,
		const CodingParams *p_cp, uint16_t tileno, uint32_t *tx0,
		uint32_t *tx1, uint32_t *ty0, uint32_t *ty1, uint32_t *dx_min,
		uint32_t *dy_min, uint64_t *max_precincts, uint8_t *max_res);

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
 * @param	tx0			X0 parameter for the tile
 * @param	tx1			X1 parameter for the tile
 * @param	ty0			Y0 parameter for the tile
 * @param	ty1			Y1 parameter for the tile
 * @param	max_precincts	maximum number of precincts for all the bands of the tile
 * @param	max_res		maximum number of resolutions for all the poc inside the tile.
 * @param	dx_min		minimum dx of all the components of all the resolutions for the tile.
 * @param	dy_min		minimum dy of all the components of all the resolutions for the tile.
 * @param	p_resolutions	pointer to an area corresponding to the one described above.
 */
static void grk_get_all_encoding_parameters(const grk_image *p_image,
		const CodingParams *p_cp, uint16_t tileno, uint32_t *tx0,
		uint32_t *tx1, uint32_t *ty0, uint32_t *ty1, uint32_t *dx_min,
		uint32_t *dy_min, uint64_t *max_precincts, uint8_t *max_res,
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
		const CodingParams *p_cp, uint16_t tileno);
/**
 * Update decompress packet iterator with no POC
 */
static void pi_update_decode_no_poc(PacketIter *p_pi, TileCodingParams *p_tcp,
		uint64_t max_precincts, uint8_t max_res);
/**
 * Upgrade decompress packet iterator with POC
 */
static void pi_update_decode_poc(PacketIter *p_pi, TileCodingParams *p_tcp,
		uint64_t max_precincts);

/**
 * Check packet iterator's nexxt level
 */
static bool pi_check_next_level(int32_t pos, CodingParams *cp,
		uint16_t tileno, uint32_t pino, const char *prog);

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
	uint64_t index = 0;

	if (pi->first) {
		comp = &pi->comps[pi->compno];
		res = &comp->resolutions[pi->resno];
		goto LABEL_SKIP;
	}
	for (pi->layno = pi->poc.layno0; pi->layno < pi->poc.layno1; pi->layno++) {

		for (pi->resno = pi->poc.resno0; pi->resno < pi->poc.resno1;
				pi->resno++) {

			for (pi->compno = pi->poc.compno0; pi->compno < pi->poc.compno1;
					pi->compno++) {

				comp = &pi->comps[pi->compno];
				//skip resolutions greater than current component resolution
				if (pi->resno >= comp->numresolutions)
					continue;
				res = &comp->resolutions[pi->resno];
				if (!pi->tp_on)
					pi->poc.precno1 = (uint64_t)res->pw * res->ph;

				for (pi->precno = pi->poc.precno0; pi->precno < pi->poc.precno1;
						pi->precno++) {

					//skip precinct numbers greater than total number of precincts
					// for this resolution
					if (pi->precno >= (uint64_t)res->pw * res->ph)
						continue;

					index = pi->layno * pi->step_l + pi->resno * pi->step_r
							+ pi->compno * pi->step_c + pi->precno * pi->step_p;
					if (!pi->include[index]) {
						pi->include[index] = true;
						return true;
					}
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
	uint64_t index = 0;

	if (pi->first) {
		comp = &pi->comps[pi->compno];
		res = &comp->resolutions[pi->resno];
		goto LABEL_SKIP;
	}
	for (pi->resno = pi->poc.resno0; pi->resno < pi->poc.resno1; pi->resno++) {
		for (pi->layno = pi->poc.layno0; pi->layno < pi->poc.layno1;
				pi->layno++) {
			for (pi->compno = pi->poc.compno0; pi->compno < pi->poc.compno1;
					pi->compno++) {
				comp = &pi->comps[pi->compno];
				if (pi->resno >= comp->numresolutions)
					continue;
				res = &comp->resolutions[pi->resno];
				if (!pi->tp_on)
					pi->poc.precno1 = (uint64_t)res->pw * res->ph;
				for (pi->precno = pi->poc.precno0; pi->precno < pi->poc.precno1;
						pi->precno++) {
					//skip precinct numbers greater than total number of precincts
					// for this resolution
					if (pi->precno >= (uint64_t)res->pw * res->ph)
						continue;

					index = pi->layno * pi->step_l + pi->resno * pi->step_r
							+ pi->compno * pi->step_c + pi->precno * pi->step_p;
					if (!pi->include[index]) {
						pi->include[index] = true;
						return true;
					}
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
	auto comp = &pi->comps[pi->compno];
	if (pi->resno >= comp->numresolutions)
		return 0;

	auto res = &comp->resolutions[pi->resno];
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
	pi->precno = (prci + (uint64_t)prcj * res->pw);
	//skip precinct numbers greater than total number of precincts
	// for this resolution
	if (pi->precno >= (uint64_t)res->pw * res->ph)
		return 0;

	return 1;
}

static bool pi_next_rpcl(PacketIter *pi) {
	uint64_t index = 0;

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
		for (pi->y = pi->poc.ty0; pi->y < pi->poc.ty1;
				pi->y += pi->dy - (pi->y % pi->dy)) {
			for (pi->x = pi->poc.tx0; pi->x < pi->poc.tx1;
					pi->x += pi->dx - (pi->x % pi->dx)) {
				for (pi->compno = pi->poc.compno0; pi->compno < pi->poc.compno1;
						pi->compno++) {
					if (pi_next_l(pi) == 0)
						continue;
					for (pi->layno = pi->poc.layno0; pi->layno < pi->poc.layno1;
							pi->layno++) {
						index = pi->layno * pi->step_l + pi->resno * pi->step_r
								+ pi->compno * pi->step_c
								+ pi->precno * pi->step_p;
						if (!pi->include[index]) {
							pi->include[index] = true;
							return true;
						}
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
	uint64_t index = 0;

	if (pi->first) {
		comp = &pi->comps[pi->compno];
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
			for (pi->compno = pi->poc.compno0; pi->compno < pi->poc.compno1;
					pi->compno++) {
				comp = &pi->comps[pi->compno];
				for (pi->resno = pi->poc.resno0;
						pi->resno
								< std::min<uint32_t>(pi->poc.resno1,
										comp->numresolutions); pi->resno++) {
					if (pi_next_l(pi) == 0)
						continue;
					for (pi->layno = pi->poc.layno0; pi->layno < pi->poc.layno1;
							pi->layno++) {
						index = pi->layno * pi->step_l + pi->resno * pi->step_r
								+ pi->compno * pi->step_c
								+ pi->precno * pi->step_p;
						if (!pi->include[index]) {
							pi->include[index] = true;
							return true;
						}
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
	uint64_t index = 0;

	if (pi->first) {
		comp = &pi->comps[pi->compno];
		goto LABEL_SKIP;
	}
	for (pi->compno = pi->poc.compno0; pi->compno < pi->poc.compno1;
			pi->compno++) {
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
		for (pi->y = pi->poc.ty0; pi->y < pi->poc.ty1;
				pi->y += pi->dy - (pi->y % pi->dy)) {
			for (pi->x = pi->poc.tx0; pi->x < pi->poc.tx1;
					pi->x += pi->dx - (pi->x % pi->dx)) {
				for (pi->resno = pi->poc.resno0;
						pi->resno
								< std::min<uint32_t>(pi->poc.resno1,
										comp->numresolutions); pi->resno++) {
					if (pi_next_l(pi) == 0)
						continue;
					for (pi->layno = pi->poc.layno0; pi->layno < pi->poc.layno1;
							pi->layno++) {
						index = pi->layno * pi->step_l + pi->resno * pi->step_r
								+ pi->compno * pi->step_c
								+ pi->precno * pi->step_p;
						if (!pi->include[index]) {
							pi->include[index] = true;
							return true;
						}
						LABEL_SKIP: ;
					}
				}
			}
		}
	}

	return false;
}

static void grk_get_encoding_parameters(const grk_image *p_image,
		const CodingParams *p_cp, uint16_t tileno, uint32_t *tx0,
		uint32_t *tx1, uint32_t *ty0, uint32_t *ty1, uint32_t *dx_min,
		uint32_t *dy_min, uint64_t *max_precincts, uint8_t *max_res) {
	assert(p_cp != nullptr);
	assert(p_image != nullptr);
	assert(tileno < p_cp->t_grid_width * p_cp->t_grid_height);

	/* position in x and y of tile */
	uint32_t p = tileno % p_cp->t_grid_width;
	uint32_t q = tileno / p_cp->t_grid_width;

	/* find extent of tile */
	*tx0 = std::max<uint32_t>(p_cp->tx0 + p * p_cp->t_width, p_image->x0);
	*tx1 = std::min<uint32_t>(p_cp->tx0 + (p + 1) * p_cp->t_width, p_image->x1);
	*ty0 = std::max<uint32_t>(p_cp->ty0 + q * p_cp->t_height, p_image->y0);
	*ty1 = std::min<uint32_t>(p_cp->ty0 + (q + 1) * p_cp->t_height,
			p_image->y1);

	*max_precincts = 0;
	*max_res = 0;
	*dx_min = UINT_MAX;
	*dy_min = UINT_MAX;

	auto tcp = &p_cp->tcps[tileno];
	for (uint32_t compno = 0; compno < p_image->numcomps; ++compno) {
		auto img_comp = p_image->comps + compno;
		auto tccp = tcp->tccps + compno;

		uint32_t tcx0 = ceildiv<uint32_t>(*tx0, img_comp->dx);
		uint32_t tcy0 = ceildiv<uint32_t>(*ty0, img_comp->dy);
		uint32_t tcx1 = ceildiv<uint32_t>(*tx1, img_comp->dx);
		uint32_t tcy1 = ceildiv<uint32_t>(*ty1, img_comp->dy);

		if (tccp->numresolutions > *max_res)
			*max_res = tccp->numresolutions;

		/* use custom size for precincts */
		for (uint32_t resno = 0; resno < tccp->numresolutions; ++resno) {

			/* precinct width and height */
			uint32_t pdx = tccp->prcw[resno];
			uint32_t pdy = tccp->prch[resno];

			uint64_t dx =
					img_comp->dx
							* ((uint64_t) 1u
									<< (pdx + tccp->numresolutions - 1 - resno));
			uint64_t dy =
					img_comp->dy
							* ((uint64_t) 1u
									<< (pdy + tccp->numresolutions - 1 - resno));

			/* take the minimum size for dx for each comp and resolution */
			if (dx < UINT_MAX)
				*dx_min = std::min<uint32_t>(*dx_min, (uint32_t) dx);
			if (dy < UINT_MAX)
				*dy_min = std::min<uint32_t>(*dy_min, (uint32_t) dy);

			uint32_t level_no = tccp->numresolutions - 1 - resno;
			uint32_t rx0 = ceildivpow2<uint32_t>(tcx0, level_no);
			uint32_t ry0 = ceildivpow2<uint32_t>(tcy0, level_no);
			uint32_t rx1 = ceildivpow2<uint32_t>(tcx1, level_no);
			uint32_t ry1 = ceildivpow2<uint32_t>(tcy1, level_no);
			uint32_t px0 = uint_floordivpow2(rx0, pdx) << pdx;
			uint32_t py0 = uint_floordivpow2(ry0, pdy) << pdy;
			uint32_t px1 = ceildivpow2<uint32_t>(rx1, pdx) << pdx;
			uint32_t py1 = ceildivpow2<uint32_t>(ry1, pdy) << pdy;
			uint32_t pw = (rx0 == rx1) ? 0 : ((px1 - px0) >> pdx);
			uint32_t ph = (ry0 == ry1) ? 0 : ((py1 - py0) >> pdy);
			uint64_t product = (uint64_t)pw * ph;
			if (product > *max_precincts)
				*max_precincts = product;
		}
	}
}

static void grk_get_all_encoding_parameters(const grk_image *p_image,
		const CodingParams *p_cp, uint16_t tileno, uint32_t *tx0,
		uint32_t *tx1, uint32_t *ty0, uint32_t *ty1, uint32_t *dx_min,
		uint32_t *dy_min, uint64_t *max_precincts, uint8_t *max_res,
		uint32_t **p_resolutions) {
	assert(p_cp != nullptr);
	assert(p_image != nullptr);
	assert(tileno < p_cp->t_grid_width * p_cp->t_grid_height);

	/* position in x and y of tile*/
	uint32_t p = tileno % p_cp->t_grid_width;
	uint32_t q = tileno / p_cp->t_grid_width;

	/* non-corrected (in regard to image offset) tile offset */

	/* can't be greater than p_image->x1 so won't overflow */
	uint32_t uncorrected_tx0 = p_cp->tx0 + p * p_cp->t_width;
	*tx0 = std::max<uint32_t>(uncorrected_tx0, p_image->x0);
	*tx1 = std::min<uint32_t>(uint_adds(uncorrected_tx0, p_cp->t_width),
			p_image->x1);
	/* can't be greater than p_image->y1 so won't overflow */
	uint32_t uncorrected_ty0 = p_cp->ty0 + q * p_cp->t_height;
	*ty0 = std::max<uint32_t>(uncorrected_ty0, p_image->y0);
	*ty1 = std::min<uint32_t>(uint_adds(uncorrected_ty0, p_cp->t_height),
			p_image->y1);

	*max_precincts = 0;
	*max_res = 0;
	*dx_min = UINT_MAX;
	*dy_min = UINT_MAX;

	auto tcp = &p_cp->tcps[tileno];
	for (uint32_t compno = 0; compno < p_image->numcomps; ++compno) {
		uint32_t level_no;

		auto lResolutionPtr = p_resolutions[compno];
		auto tccp = tcp->tccps + compno;
		auto img_comp = p_image->comps + compno;

		uint32_t tcx0 = ceildiv<uint32_t>(*tx0, img_comp->dx);
		uint32_t tcy0 = ceildiv<uint32_t>(*ty0, img_comp->dy);
		uint32_t tcx1 = ceildiv<uint32_t>(*tx1, img_comp->dx);
		uint32_t tcy1 = ceildiv<uint32_t>(*ty1, img_comp->dy);

		if (tccp->numresolutions > *max_res)
			*max_res = tccp->numresolutions;

		/* use custom size for precincts*/
		level_no = tccp->numresolutions - 1;
		for (uint32_t resno = 0; resno < tccp->numresolutions; ++resno) {
			uint32_t pdx = tccp->prcw[resno];
			uint32_t pdy = tccp->prch[resno];
			*lResolutionPtr++ = pdx;
			*lResolutionPtr++ = pdy;

			uint64_t dx = img_comp->dx * ((uint64_t) 1u << (pdx + level_no));
			uint64_t dy = img_comp->dy * ((uint64_t) 1u << (pdy + level_no));
			if (dx < UINT_MAX)
				*dx_min = std::min<uint32_t>(*dx_min, (uint32_t) dx);
			if (dy < UINT_MAX)
				*dy_min = std::min<uint32_t>(*dy_min, (uint32_t) dy);
			uint32_t rx0 = ceildivpow2<uint32_t>(tcx0, level_no);
			uint32_t ry0 = ceildivpow2<uint32_t>(tcy0, level_no);
			uint32_t rx1 = ceildivpow2<uint32_t>(tcx1, level_no);
			uint32_t ry1 = ceildivpow2<uint32_t>(tcy1, level_no);
			uint32_t px0 = uint_floordivpow2(rx0, pdx) << pdx;
			uint32_t py0 = uint_floordivpow2(ry0, pdy) << pdy;
			uint32_t px1 = ceildivpow2<uint32_t>(rx1, pdx) << pdx;
			uint32_t py1 = ceildivpow2<uint32_t>(ry1, pdy) << pdy;
			uint32_t pw = (rx0 == rx1) ? 0 : ((px1 - px0) >> pdx);
			uint32_t ph = (ry0 == ry1) ? 0 : ((py1 - py0) >> pdy);
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
		const CodingParams *cp, uint16_t tileno) {
	assert(cp != nullptr);
	assert(image != nullptr);
	assert(tileno < cp->t_grid_width * cp->t_grid_height);
	auto tcp = &cp->tcps[tileno];
	uint32_t poc_bound = tcp->numpocs + 1;

	auto pi = new PacketIter[poc_bound];
	for (uint32_t pino = 0; pino < poc_bound; ++pino) {
		auto current_pi = pi + pino;

		current_pi->comps = (grk_pi_comp*) grk_calloc(image->numcomps,
				sizeof(grk_pi_comp));
		if (!current_pi->comps) {
			pi_destroy(pi, poc_bound);
			return nullptr;
		}
		current_pi->numcomps = image->numcomps;
		for (uint32_t compno = 0; compno < image->numcomps; ++compno) {
			grk_pi_comp *comp = current_pi->comps + compno;
			auto tccp = &tcp->tccps[compno];
			comp->resolutions = (grk_pi_resolution*) grk_calloc(
					tccp->numresolutions, sizeof(grk_pi_resolution));

			if (!comp->resolutions) {
				pi_destroy(pi, poc_bound);
				return nullptr;
			}
			comp->numresolutions = tccp->numresolutions;
		}
	}
	return pi;
}

static void pi_update_encode_poc_and_final(CodingParams *p_cp,
		uint16_t tileno, uint32_t tx0, uint32_t tx1, uint32_t ty0, uint32_t ty1,
		uint64_t max_precincts, uint32_t dx_min, uint32_t dy_min) {
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
		current_poc->txS = tx0;
		current_poc->txE = tx1;
		current_poc->tyS = ty0;
		current_poc->tyE = ty1;
		current_poc->dx = dx_min;
		current_poc->dy = dy_min;
	}
}

static void pi_update_encode_no_poc(CodingParams *p_cp,
		uint16_t num_comps, uint16_t tileno, uint32_t tx0, uint32_t tx1,
		uint32_t ty0, uint32_t ty1, uint64_t max_precincts, uint8_t max_res,
		uint32_t dx_min, uint32_t dy_min) {
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
		current_poc->txS = tx0;
		current_poc->txE = tx1;
		current_poc->tyS = ty0;
		current_poc->tyE = ty1;
		current_poc->dx = dx_min;
		current_poc->dy = dy_min;
	}
}

static void pi_update_decode_poc(PacketIter *p_pi, TileCodingParams *p_tcp,
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

static void pi_update_decode_no_poc(PacketIter *p_pi, TileCodingParams *p_tcp,
		uint64_t max_precincts, uint8_t max_res) {
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

static bool pi_check_next_level(int32_t pos, CodingParams *cp,
		uint16_t tileno, uint32_t pino, const char *prog) {
	auto tcps = cp->tcps + tileno;
	auto poc = tcps->pocs + pino;

	if (pos >= 0) {
		for (int32_t i = pos; pos >= 0; i--) {
			switch (prog[i]) {
			case 'R':
				if (poc->res_t == poc->resE)
					return pi_check_next_level(pos - 1, cp, tileno, pino, prog);
				else
					return true;
				break;
			case 'C':
				if (poc->comp_t == poc->compE)
					return pi_check_next_level(pos - 1, cp, tileno, pino, prog);
				else
					return true;
				break;
			case 'L':
				if (poc->lay_t == poc->layE)
					return pi_check_next_level(pos - 1, cp, tileno, pino, prog);
				else
					return true;
				break;
			case 'P':
				switch (poc->prg) {
				case GRK_LRCP: /* fall through */
				case GRK_RLCP:
					if (poc->prc_t == poc->prcE)
						return pi_check_next_level(i - 1, cp, tileno, pino,	prog);
					else
						return true;
					break;
				default:
					if (poc->tx0_t == poc->txE) {
						/*TY*/
						if (poc->ty0_t == poc->tyE)
							return pi_check_next_level(i - 1, cp, tileno, pino,	prog);
						else
							return true;
						/*TY*/
					} else {
						return true;
					}
					break;
				}/*end case P*/
			}/*end switch*/
		}/*end for*/
	}/*end if*/
	return false;
}

/*
 ==========================================================
 Packet iterator interface
 ==========================================================
 */
PacketIter* pi_create_decompress(grk_image *p_image, CodingParams *p_cp,
		uint16_t tile_no) {
	assert(p_cp != nullptr);
	assert(p_image != nullptr);
	assert(tile_no < p_cp->t_grid_width * p_cp->t_grid_height);

	auto tcp = &p_cp->tcps[tile_no];
	uint32_t bound = tcp->numpocs + 1;

	uint32_t data_stride = 4 * GRK_J2K_MAXRLVLS;
	auto tmp_data = (uint32_t*) grk_malloc(
			data_stride * p_image->numcomps * sizeof(uint32_t));
	if (!tmp_data)
		return nullptr;
	auto tmp_ptr = (uint32_t**) grk_malloc(
			p_image->numcomps * sizeof(uint32_t*));
	if (!tmp_ptr) {
		grk_free(tmp_data);
		return nullptr;
	}

	/* memory allocation for pi */
	auto pi = pi_create(p_image, p_cp, tile_no);
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
	uint32_t tx0, tx1, ty0, ty1;
	uint32_t dx_min, dy_min;
	grk_get_all_encoding_parameters(p_image, p_cp, tile_no, &tx0, &tx1, &ty0,
			&ty1, &dx_min, &dy_min, &max_precincts, &max_res, tmp_ptr);

	/* step calculations */
	uint32_t step_p = 1;
	uint64_t step_c = max_precincts * step_p;
	uint64_t step_r = p_image->numcomps * step_c;
	uint64_t step_l = max_res * step_r;

	/* set values for first packet iterator */
	auto current_pi = pi;

	/* memory allocation for include */
	current_pi->include = nullptr;
	if (step_l && (tcp->numlayers < (SIZE_MAX / step_l) - 1)) {
		current_pi->include = new bool[((size_t) tcp->numlayers + 1) * step_l];
		for (size_t i = 0; i < ((size_t) tcp->numlayers + 1) * step_l; ++i)
			current_pi->include[i] = false;
	}

	/* special treatment for the first packet iterator */
	current_pi->tx0 = tx0;
	current_pi->ty0 = ty0;
	current_pi->tx1 = tx1;
	current_pi->ty1 = ty1;

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
		for (uint32_t resno = 0; resno < current_comp->numresolutions;
				resno++) {
			grk_pi_resolution *res = current_comp->resolutions + resno;

			res->pdx = *(encoding_value_ptr++);
			res->pdy = *(encoding_value_ptr++);
			res->pw = *(encoding_value_ptr++);
			res->ph = *(encoding_value_ptr++);
		}
	}
	for (uint32_t pino = 1; pino < bound; ++pino) {
		current_pi = pi + pino;
		current_pi->tx0 = tx0;
		current_pi->ty0 = ty0;
		current_pi->tx1 = tx1;
		current_pi->ty1 = ty1;
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
			for (uint32_t resno = 0; resno < current_comp->numresolutions;
					resno++) {
				grk_pi_resolution *res = current_comp->resolutions + resno;

				res->pdx = *(encoding_value_ptr++);
				res->pdy = *(encoding_value_ptr++);
				res->pw = *(encoding_value_ptr++);
				res->ph = *(encoding_value_ptr++);
			}
		}
		/* special treatment*/
		current_pi->include = (current_pi - 1)->include;
	}
	grk_free(tmp_data);
	grk_free(tmp_ptr);
	if (tcp->POC)
		pi_update_decode_poc(pi, tcp, max_precincts);
	else
		pi_update_decode_no_poc(pi, tcp, max_precincts, max_res);

	return pi;
}

PacketIter* pi_initialise_encode(const grk_image *p_image,
		CodingParams *p_cp, uint16_t tile_no, J2K_T2_MODE p_t2_mode) {
	assert(p_cp != nullptr);
	assert(p_image != nullptr);
	assert(tile_no < p_cp->t_grid_width * p_cp->t_grid_height);

	auto tcp = &p_cp->tcps[tile_no];
	uint32_t bound = tcp->numpocs + 1;
	uint32_t data_stride = 4 * GRK_J2K_MAXRLVLS;
	auto tmp_data = (uint32_t*) grk_malloc(
			data_stride * p_image->numcomps * sizeof(uint32_t));
	if (!tmp_data) {
		return nullptr;
	}

	auto tmp_ptr = (uint32_t**) grk_malloc(
			p_image->numcomps * sizeof(uint32_t*));
	if (!tmp_ptr) {
		grk_free(tmp_data);
		return nullptr;
	}

	auto pi = pi_create(p_image, p_cp, tile_no);
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
	uint32_t tx0, tx1, ty0, ty1;
	uint32_t dx_min, dy_min;
	grk_get_all_encoding_parameters(p_image, p_cp, tile_no, &tx0, &tx1, &ty0,
			&ty1, &dx_min, &dy_min, &max_precincts, &max_res, tmp_ptr);

	uint32_t step_p = 1;
	uint64_t step_c = max_precincts * step_p;
	uint64_t step_r = p_image->numcomps * step_c;
	uint64_t step_l = max_res * step_r;

	/* set values for first packet iterator*/
	pi->tp_on = p_cp->m_coding_params.m_enc.m_tp_on;
	auto current_pi = pi;
	current_pi->include = nullptr;
	if (step_l && (tcp->numlayers < (SIZE_MAX / step_l))) {
		current_pi->include = new bool[(size_t) tcp->numlayers * step_l];
		for (size_t i = 0; i < (size_t) tcp->numlayers * step_l; ++i)
			current_pi->include[i] = false;
	}

	/* special treatment for the first packet iterator*/
	current_pi->tx0 = tx0;
	current_pi->ty0 = ty0;
	current_pi->tx1 = tx1;
	current_pi->ty1 = ty1;
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

		current_pi->tx0 = tx0;
		current_pi->ty0 = ty0;
		current_pi->tx1 = tx1;
		current_pi->ty1 = ty1;
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
			for (uint32_t resno = 0; resno < current_comp->numresolutions;
					resno++) {
				auto res = current_comp->resolutions + resno;

				res->pdx = *(encoding_value_ptr++);
				res->pdy = *(encoding_value_ptr++);
				res->pw = *(encoding_value_ptr++);
				res->ph = *(encoding_value_ptr++);
			}
		}
		/* special treatment*/
		current_pi->include = (current_pi - 1)->include;
	}

	grk_free(tmp_data);
	tmp_data = nullptr;
	grk_free(tmp_ptr);
	tmp_ptr = nullptr;

	if (tcp->POC && (GRK_IS_CINEMA(p_cp->rsiz) || p_t2_mode == FINAL_PASS))
		pi_update_encode_poc_and_final(p_cp, tile_no, tx0, tx1, ty0, ty1,
				max_precincts, dx_min, dy_min);
	else
		pi_update_encode_no_poc(p_cp, p_image->numcomps, tile_no, tx0, tx1,
				ty0, ty1, max_precincts, max_res, dx_min, dy_min);

	return pi;
}

void pi_enable_tile_part_generation(PacketIter *pi, CodingParams *cp, uint16_t tileno,
		uint32_t pino, bool first_poc_tile_part, uint32_t tppos, J2K_T2_MODE t2_mode) {
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
							if (pi_check_next_level(i - 1, cp, tileno, pino,
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
							if (pi_check_next_level(i - 1, cp, tileno, pino,
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
							if (pi_check_next_level(i - 1, cp, tileno, pino,
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
								if (pi_check_next_level(i - 1, cp, tileno, pino,
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
									if (pi_check_next_level(i - 1, cp, tileno,
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

void pi_destroy(PacketIter *p_pi, uint32_t nb_elements) {
	if (p_pi) {
		delete[] p_pi->include;
		for (uint32_t pino = 0; pino < nb_elements; ++pino) {
			auto current_pi = p_pi + pino;

			if (current_pi->comps) {
				for (uint32_t compno = 0; compno < current_pi->numcomps;
						compno++) {
					auto current_component = current_pi->comps + compno;

					grk_free(current_component->resolutions);
				}
				grk_free(current_pi->comps);
			}
		}
		delete[] p_pi;
	}
}

void pi_update_encoding_parameters(const grk_image *p_image,
		CodingParams *p_cp, uint16_t tile_no) {
	assert(p_cp != nullptr);
	assert(p_image != nullptr);
	assert(tile_no < p_cp->t_grid_width * p_cp->t_grid_height);

	auto tcp = &(p_cp->tcps[tile_no]);
	uint8_t max_res;
	uint64_t max_precincts;
	uint32_t tx0, tx1, ty0, ty1;
	uint32_t dx_min, dy_min;
	grk_get_encoding_parameters(p_image, p_cp, tile_no, &tx0, &tx1, &ty0, &ty1,
			&dx_min, &dy_min, &max_precincts, &max_res);

	if (tcp->POC)
		pi_update_encode_poc_and_final(p_cp, tile_no, tx0, tx1, ty0, ty1,
				max_precincts, dx_min, dy_min);
	else
		pi_update_encode_no_poc(p_cp, p_image->numcomps, tile_no, tx0, tx1,
				ty0, ty1, max_precincts, max_res, dx_min, dy_min);
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

PacketIter::PacketIter() : tp_on(false), include(nullptr), step_l(0), step_r(0), step_c(0), step_p(0), compno(0),
							resno(0), precno(0), layno(0), first(true), numcomps(0),comps(nullptr), tx0(0), ty0(0),
							tx1(0), ty1(0), x(0), y(0), dx(0), dy(0)
{
	memset(&poc, 0, sizeof(poc));
}

}
