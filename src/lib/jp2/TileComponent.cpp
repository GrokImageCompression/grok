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
 *    This source code incorporates work covered by the following copyright and
 *    permission notice:
 *
 * The copyright in this software is being made available under the 2-clauses
 * BSD License, included below. This software may be subject to other third
 * party and contributor rights, including patent rights, and no such rights
 * are granted under this license.
 *
 * Copyright (c) 2002-2014, Universite catholique de Louvain (UCL), Belgium
 * Copyright (c) 2002-2014, Professor Benoit Macq
 * Copyright (c) 2001-2003, David Janssens
 * Copyright (c) 2002-2003, Yannick Verschueren
 * Copyright (c) 2003-2007, Francois-Olivier Devaux
 * Copyright (c) 2003-2014, Antonin Descampe
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
 * Copyright (c) 2008, 2011-2012, Centre National d'Etudes Spatiales (CNES), FR
 * Copyright (c) 2012, CS Systemes d'Information, France
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS `AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "grok_includes.h"

namespace grk {

TileComponent::TileComponent() :numresolutions(0),
								numAllocatedResolutions(0),
								minimum_num_resolutions(0),
								resolutions(nullptr),
						#ifdef DEBUG_LOSSLESS_T2
								round_trip_resolutions(nullptr),
						#endif
							   numpix(0),
							   buf(nullptr),
							   whole_tile_decoding(true),
							   x0(0),
							   y0(0),
							   x1(0),
							   y1(0),
							   m_is_encoder(false),
							   m_sa(nullptr)
{}

TileComponent::~TileComponent(){
	release_mem();
	delete buf;
}
void TileComponent::release_mem(){
	auto res = resolutions;
	if (res) {
		auto nb_resolutions = numAllocatedResolutions;
		for (uint32_t resno = 0; resno < nb_resolutions; ++resno) {
			auto band = res->bands;
			for (uint32_t bandno = 0; bandno < 3; ++bandno) {
				auto precinct = band->precincts;
				if (precinct) {
					for (uint32_t precno = 0; precno < band->numAllocatedPrecincts;
							++precno) {
						precinct->deleteTagTrees();
						if (m_is_encoder)
							code_block_enc_deallocate(precinct);
						else
							code_block_dec_deallocate(precinct);
						++precinct;
					}
					delete[] band->precincts;
					band->precincts = nullptr;
				}
				++band;
			} /* for (resno */
			++res;
		}
		delete[] resolutions;
		resolutions = nullptr;
	}
	delete m_sa;
	m_sa = nullptr;
}

uint32_t TileComponent::width(){
	return (uint32_t) (x1 - x0);
}
uint32_t TileComponent::height(){
	return (uint32_t) (y1 - y0);
}
uint64_t TileComponent::size(){
	return  area() * sizeof(int32_t);
}
uint64_t TileComponent::area(){
	return (uint64_t)width() * height() ;
}
void TileComponent::finalizeCoordinates(){
	auto highestRes =
			(!m_is_encoder) ? minimum_num_resolutions : numresolutions;
	auto res =  resolutions + highestRes - 1;
	x0 = res->x0;
	x1 = res->x1;
	y0 = res->y0;
	y1 = res->y1;

	res = resolutions + numresolutions - 1;
	unreduced_tile_dim = grk_rect(res->x0, res->y0, res->x1, res->y1);

}


void TileComponent::get_dimensions(grk_image *image,
		 	 	 	 	 	 	 grk_image_comp  *img_comp,
								 uint32_t *size_comp,
								 uint32_t *w,
								 uint32_t *h,
								 uint32_t *offset_x,
								 uint32_t *offset_y,
								 uint32_t *image_width,
								 uint32_t *stride,
								 uint64_t *tile_offset) {
	*size_comp = (img_comp->prec + 7) >> 3; /* (/8) */
	*w = width();
	*h = height();
	*offset_x = ceildiv<uint32_t>(image->x0, img_comp->dx);
	*offset_y = ceildiv<uint32_t>(image->y0, img_comp->dy);
	*image_width = ceildiv<uint32_t>(image->x1 - image->x0,
			img_comp->dx);
	*stride = *image_width - *w;
	*tile_offset = (x0 - *offset_x)
			+ (uint64_t) (y0 - *offset_y) * *image_width;
}

bool TileComponent::init(bool isEncoder,
						bool whole_tile,
						grk_image *output_image,
						grk_coding_parameters *cp,
						grk_tcp *tcp,
						grk_tcd_tile* tile,
						grk_image_comp* image_comp,
						grk_tccp* tccp,
						grk_plugin_tile *current_plugin_tile){
	uint32_t state = grk_plugin_get_debug_state();
	m_is_encoder = isEncoder;
	whole_tile_decoding = whole_tile;

	size_t sizeof_block = m_is_encoder ? sizeof(grk_tcd_cblk_enc) : sizeof(grk_tcd_cblk_dec);
	uint32_t bandno, resno, precno;
	/* extent of precincts , top left, bottom right**/
	uint32_t tprc_x_start, tprc_y_start, br_prc_x_end, br_prc_y_end;
	/* number of precinct for a resolution */
	uint32_t nb_precincts;
	/* number of code blocks for a precinct*/
	uint64_t nb_code_blocks, cblkno;
	/* room needed to store nb_code_blocks code blocks for a precinct*/
	uint64_t nb_code_blocks_size;
	uint32_t leveno;
	uint32_t pdx, pdy;
	uint32_t x0b, y0b;

	/* border of each tile component in tile coordinates */
	auto x0 = ceildiv<uint32_t>(tile->x0, image_comp->dx);
	auto y0 = ceildiv<uint32_t>(tile->y0, image_comp->dy);
	auto x1 = ceildiv<uint32_t>(tile->x1, image_comp->dx);
	auto y1 = ceildiv<uint32_t>(tile->y1, image_comp->dy);
	/*fprintf(stderr, "\tTile compo border = %d,%d,%d,%d\n", X0(), Y0(),x1,y1);*/

	numresolutions = tccp->numresolutions;
	if (numresolutions < cp->m_coding_param.m_dec.m_reduce) {
		minimum_num_resolutions = 1;
	} else {
		minimum_num_resolutions = numresolutions
				- cp->m_coding_param.m_dec.m_reduce;
	}
	if (!resolutions) {
		resolutions = new grk_tcd_resolution[numresolutions];
		numAllocatedResolutions = numresolutions;
	} else if (numresolutions > numAllocatedResolutions) {
		grk_tcd_resolution *new_resolutions =
				new grk_tcd_resolution[numresolutions];
		for (uint32_t i = 0; i < numresolutions; ++i) {
			new_resolutions[i] = resolutions[i];
		}
		delete[] resolutions;
		resolutions = new_resolutions;
		numAllocatedResolutions = numresolutions;
	}
	leveno = numresolutions;
	auto res = resolutions;
	/*fprintf(stderr, "\tleveno=%d\n",leveno);*/

	for (resno = 0; resno < numresolutions; ++resno) {
		/*fprintf(stderr, "\t\tresno = %d/%d\n", resno, numresolutions);*/
		uint32_t tlcbgxstart, tlcbgystart;
		uint32_t cbgwidthexpn, cbgheightexpn;
		uint32_t cblkwidthexpn, cblkheightexpn;

		--leveno;

		/* border for each resolution level (global) */
		res->x0 = uint_ceildivpow2(x0, leveno);
		res->y0 = uint_ceildivpow2(y0, leveno);
		res->x1 = uint_ceildivpow2(x1, leveno);
		res->y1 = uint_ceildivpow2(y1, leveno);
		/*fprintf(stderr, "\t\t\tres_x0= %d, res_y0 =%d, res_x1=%d, res_y1=%d\n", res->x0, res->y0, res->x1, res->y1);*/
		/* p. 35, table A-23, ISO/IEC FDIS154444-1 : 2000 (18 august 2000) */
		pdx = tccp->prcw[resno];
		pdy = tccp->prch[resno];
		/*fprintf(stderr, "\t\t\tpdx=%d, pdy=%d\n", pdx, pdy);*/
		/* p. 64, B.6, ISO/IEC FDIS15444-1 : 2000 (18 august 2000)  */
		tprc_x_start = uint_floordivpow2(res->x0, pdx) << pdx;
		tprc_y_start = uint_floordivpow2(res->y0, pdy) << pdy;
		uint64_t temp = (uint64_t)uint_ceildivpow2(res->x1, pdx) << pdx;
		if (temp > UINT_MAX){
			GROK_ERROR("Resolution x1 value %d must be less than 2^32", temp);
			return false;
		}
		br_prc_x_end = (uint32_t)temp;
		temp = (uint64_t)uint_ceildivpow2(res->y1, pdy) << pdy;
		if (temp > UINT_MAX){
			GROK_ERROR("Resolution y1 value %d must be less than 2^32", temp);
			return false;
		}
		br_prc_y_end = (uint32_t)temp;

		/*fprintf(stderr, "\t\t\tprc_x_start=%d, prc_y_start=%d, br_prc_x_end=%d, br_prc_y_end=%d \n", tprc_x_start, tprc_y_start, br_prc_x_end ,br_prc_y_end );*/

		res->pw =
				(res->x0 == res->x1) ?
						0 : ((br_prc_x_end - tprc_x_start) >> pdx);
		res->ph =
				(res->y0 == res->y1) ?
						0 : ((br_prc_y_end - tprc_y_start) >> pdy);
		/*fprintf(stderr, "\t\t\tres_pw=%d, res_ph=%d\n", res->pw, res->ph );*/

		if (mult_will_overflow(res->pw, res->ph)) {
			GROK_ERROR(
					"nb_precincts calculation would overflow ");
			return false;
		}
		nb_precincts = res->pw * res->ph;

		if (mult_will_overflow(nb_precincts,
				(uint32_t) sizeof(grk_tcd_precinct))) {
			GROK_ERROR(
					"nb_precinct_size calculation would overflow ");
			return false;
		}
		if (resno == 0) {
			tlcbgxstart = tprc_x_start;
			tlcbgystart = tprc_y_start;
			cbgwidthexpn = pdx;
			cbgheightexpn = pdy;
			res->numbands = 1;
		} else {
			tlcbgxstart = uint_ceildivpow2(tprc_x_start, 1);
			tlcbgystart = uint_ceildivpow2(tprc_y_start, 1);
			cbgwidthexpn = pdx - 1;
			cbgheightexpn = pdy - 1;
			res->numbands = 3;
		}

		cblkwidthexpn = std::min<uint32_t>(tccp->cblkw, cbgwidthexpn);
		cblkheightexpn = std::min<uint32_t>(tccp->cblkh, cbgheightexpn);
		size_t nominalBlockSize = (1 << cblkwidthexpn)
				* (1 << cblkheightexpn);
		auto band = res->bands;

		for (bandno = 0; bandno < res->numbands; ++bandno) {
			/*fprintf(stderr, "\t\t\tband_no=%d/%d\n", bandno, res->numbands );*/

			if (resno == 0) {
				band->bandno = 0;
				band->x0 = uint_ceildivpow2(x0, leveno);
				band->y0 = uint_ceildivpow2(y0, leveno);
				band->x1 = uint_ceildivpow2(x1, leveno);
				band->y1 = uint_ceildivpow2(y1, leveno);
			} else {
				band->bandno = (uint8_t)(bandno + 1);
				/* x0b = 1 if bandno = 1 or 3 */
				x0b = band->bandno & 1;
				/* y0b = 1 if bandno = 2 or 3 */
				y0b = (uint32_t) ((band->bandno) >> 1);
				/* band border (global) */
				band->x0 = uint64_ceildivpow2(
						x0 - ((uint64_t) x0b << leveno),
						leveno + 1);
				band->y0 = uint64_ceildivpow2(
						y0 - ((uint64_t) y0b << leveno),
						leveno + 1);
				band->x1 = uint64_ceildivpow2(
						x1 - ((uint64_t) x0b << leveno),
						leveno + 1);
				band->y1 = uint64_ceildivpow2(
						y1 - ((uint64_t) y0b << leveno),
						leveno + 1);
			}

			tccp->quant.setBandStepSizeAndBps(tcp,
												band,
												resno,
												(uint8_t)bandno,
												tccp,
												image_comp->prec,
												m_is_encoder ? 1.0f : 0.5f);

			if (!band->precincts && (nb_precincts > 0U)) {
				band->precincts = new grk_tcd_precinct[nb_precincts];
				band->numAllocatedPrecincts = nb_precincts;
			} else if (band->numAllocatedPrecincts < nb_precincts) {
				grk_tcd_precinct *new_precincts =
						new grk_tcd_precinct[nb_precincts];
				for (size_t i = 0; i < band->numAllocatedPrecincts; ++i) {
					new_precincts[i] = band->precincts[i];
				}
				if (band->precincts)
					delete[] band->precincts;
				band->precincts = new_precincts;
				band->numAllocatedPrecincts = nb_precincts;
			}
			band->numPrecincts = nb_precincts;
			auto current_precinct = band->precincts;
			for (precno = 0; precno < nb_precincts; ++precno) {
				uint32_t tlcblkxstart, tlcblkystart, brcblkxend, brcblkyend;
				uint32_t cbgxstart = tlcbgxstart
						+ (precno % res->pw) * (1 << cbgwidthexpn);
				uint32_t cbgystart = tlcbgystart
						+ (precno / res->pw) * (1 << cbgheightexpn);
				uint32_t cbgxend = cbgxstart + (1 << cbgwidthexpn);
				uint32_t cbgyend = cbgystart + (1 << cbgheightexpn);
				/*fprintf(stderr, "\t precno=%d; bandno=%d, resno=%d; compno=%d\n", precno, bandno , resno, compno);*/
				/*fprintf(stderr, "\t tlcbgxstart(=%d) + (precno(=%d) percent res->pw(=%d)) * (1 << cbgwidthexpn(=%d)) \n",tlcbgxstart,precno,res->pw,cbgwidthexpn);*/

				/* precinct size (global) */
				/*fprintf(stderr, "\t cbgxstart=%d, band->x0 = %d \n",cbgxstart, band->x0);*/

				current_precinct->x0 = std::max<uint32_t>(cbgxstart,
						band->x0);
				current_precinct->y0 = std::max<uint32_t>(cbgystart,
						band->y0);
				current_precinct->x1 = std::min<uint32_t>(cbgxend,
						band->x1);
				current_precinct->y1 = std::min<uint32_t>(cbgyend,
						band->y1);
				/*fprintf(stderr, "\t prc_x0=%d; prc_y0=%d, prc_x1=%d; prc_y1=%d\n",current_precinct->x0, current_precinct->y0 ,current_precinct->x1, current_precinct->y1);*/

				tlcblkxstart = uint_floordivpow2(current_precinct->x0,
						cblkwidthexpn) << cblkwidthexpn;
				/*fprintf(stderr, "\t tlcblkxstart =%d\n",tlcblkxstart );*/
				tlcblkystart = uint_floordivpow2(current_precinct->y0,
						cblkheightexpn) << cblkheightexpn;
				/*fprintf(stderr, "\t tlcblkystart =%d\n",tlcblkystart );*/
				brcblkxend = uint_ceildivpow2(current_precinct->x1,
						cblkwidthexpn) << cblkwidthexpn;
				/*fprintf(stderr, "\t brcblkxend =%d\n",brcblkxend );*/
				brcblkyend = uint_ceildivpow2(current_precinct->y1,
						cblkheightexpn) << cblkheightexpn;
				/*fprintf(stderr, "\t brcblkyend =%d\n",brcblkyend );*/
				current_precinct->cw = ((brcblkxend - tlcblkxstart)
						>> cblkwidthexpn);
				current_precinct->ch = ((brcblkyend - tlcblkystart)
						>> cblkheightexpn);

				nb_code_blocks = (uint64_t) current_precinct->cw
						* current_precinct->ch;
				/*fprintf(stderr, "\t\t\t\t precinct_cw = %d x recinct_ch = %d\n",current_precinct->cw, current_precinct->ch);      */
				nb_code_blocks_size = nb_code_blocks * sizeof_block;

				if (!current_precinct->cblks.blocks
						&& (nb_code_blocks > 0U)) {
					current_precinct->cblks.blocks = grk_malloc(
							nb_code_blocks_size);
					if (!current_precinct->cblks.blocks) {
						return false;
					}
					/*fprintf(stderr, "\t\t\t\tAllocate cblks of a precinct (grk_tcd_cblk_dec): %d\n",nb_code_blocks_size);*/
					memset(current_precinct->cblks.blocks, 0,
							nb_code_blocks_size);

					current_precinct->block_size = nb_code_blocks_size;
				} else if (nb_code_blocks_size
						> current_precinct->block_size) {
					void *new_blocks = grk_realloc(
							current_precinct->cblks.blocks,
							nb_code_blocks_size);
					if (!new_blocks) {
						grok_free(current_precinct->cblks.blocks);
						current_precinct->cblks.blocks = nullptr;
						current_precinct->block_size = 0;
						GROK_ERROR(
								"Not enough memory for current precinct codeblock element");
						return false;
					}
					current_precinct->cblks.blocks = new_blocks;
					/*fprintf(stderr, "\t\t\t\tReallocate cblks of a precinct (grk_tcd_cblk_dec): from %d to %d\n",current_precinct->block_size, nb_code_blocks_size);     */

					memset(	((uint8_t*) current_precinct->cblks.blocks)
									+ current_precinct->block_size, 0,
							nb_code_blocks_size
									- current_precinct->block_size);
					current_precinct->block_size = nb_code_blocks_size;
				}

				current_precinct->initTagTrees();

				for (cblkno = 0; cblkno < nb_code_blocks; ++cblkno) {
					uint32_t cblkxstart = tlcblkxstart
							+ (uint32_t) (cblkno % current_precinct->cw)
									* (1 << cblkwidthexpn);
					uint32_t cblkystart = tlcblkystart
							+ (uint32_t) (cblkno / current_precinct->cw)
									* (1 << cblkheightexpn);
					uint32_t cblkxend = cblkxstart + (1 << cblkwidthexpn);
					uint32_t cblkyend = cblkystart + (1 << cblkheightexpn);

					if (m_is_encoder) {
						grk_tcd_cblk_enc *code_block =
								current_precinct->cblks.enc + cblkno;

						if (!code_block->alloc()) {
							return false;
						}
						/* code-block size (global) */
						code_block->x0 = std::max<uint32_t>(cblkxstart,
								current_precinct->x0);
						code_block->y0 = std::max<uint32_t>(cblkystart,
								current_precinct->y0);
						code_block->x1 = std::min<uint32_t>(cblkxend,
								current_precinct->x1);
						code_block->y1 = std::min<uint32_t>(cblkyend,
								current_precinct->y1);

						if (!current_plugin_tile
								|| (state & GRK_PLUGIN_STATE_DEBUG)) {
							if (!code_block->alloc_data(
									nominalBlockSize)) {
								return false;
							}
						}
					} else {
						grk_tcd_cblk_dec *code_block =
								current_precinct->cblks.dec + cblkno;
						if (!current_plugin_tile
								|| (state & GRK_PLUGIN_STATE_DEBUG)) {
							if (!code_block->alloc()) {
								return false;
							}
						}

						/* code-block size (global) */
						code_block->x0 = std::max<uint32_t>(cblkxstart,
								current_precinct->x0);
						code_block->y0 = std::max<uint32_t>(cblkystart,
								current_precinct->y0);
						code_block->x1 = std::min<uint32_t>(cblkxend,
								current_precinct->x1);
						code_block->y1 = std::min<uint32_t>(cblkyend,
								current_precinct->y1);
					}
				}
				++current_precinct;
			} /* precno */
			++band;
		} /* bandno */
		++res;
	} /* resno */
	finalizeCoordinates();
	if (!create_buffer(output_image,
								image_comp->dx,
								image_comp->dy)) {
		return false;
	}
	buf->data_size_needed = size();

	return true;
}


void TileComponent::alloc_sparse_array(uint32_t numres){
    auto tr_max = &(resolutions[numres - 1]);
	uint32_t w = (uint32_t)(tr_max->x1 - tr_max->x0);
	uint32_t h = (uint32_t)(tr_max->y1 - tr_max->y0);
	auto sa = new sparse_array(w, h, min<uint32_t>(w, 64), min<uint32_t>(h, 64));
    for (uint32_t resno = 0; resno < numres; ++resno) {
        auto res = &resolutions[resno];

        for (uint32_t bandno = 0; bandno < res->numbands; ++bandno) {
            auto band = &res->bands[bandno];

            for (uint32_t precno = 0; precno < res->pw * res->ph; ++precno) {
                auto precinct = &band->precincts[precno];

                for (uint32_t cblkno = 0; cblkno < precinct->cw * precinct->ch; ++cblkno) {
                    auto cblk = &precinct->cblks.dec[cblkno];
					uint32_t x = (uint32_t)(cblk->x0 - band->x0);
					uint32_t y = (uint32_t)(cblk->y0 - band->y0);
					uint32_t cblk_w = (uint32_t)(cblk->x1 - cblk->x0);
					uint32_t cblk_h = (uint32_t)(cblk->y1 - cblk->y0);

					if (band->bandno & 1) {
						grk_tcd_resolution* pres = &resolutions[resno - 1];
						x += (uint32_t)(pres->x1 - pres->x0);
					}
					if (band->bandno & 2) {
						grk_tcd_resolution* pres = &resolutions[resno - 1];
						y += (uint32_t)(pres->y1 - pres->y0);
					}

					if (!sa->alloc(x,
								  y,
								  x + cblk_w,
								  y + cblk_h)) {
						delete sa;
						throw runtime_error("unable to allocate sparse array");
					}
                }
            }
        }
    }
    if (m_sa)
    	delete m_sa;
    m_sa = sa;
}


bool TileComponent::create_buffer(grk_image *output_image,
									uint32_t dx,
									uint32_t dy) {
	auto new_buffer = new TileBuffer();
	new_buffer->data = nullptr;
	new_buffer->reduced_tile_dim = grk_rect(x0, y0, x1, y1);
	new_buffer->reduced_image_dim = new_buffer->reduced_tile_dim;
	new_buffer->unreduced_tile_dim = unreduced_tile_dim;
	grk_rect max_image_dim = unreduced_tile_dim ;

	if (output_image) {
		// tile coordinates
		new_buffer->unreduced_image_dim = grk_rect(ceildiv<uint32_t>(output_image->x0, dx),
									ceildiv<uint32_t>(output_image->y0, dy),
									ceildiv<uint32_t>(output_image->x1, dx),
									ceildiv<uint32_t>(output_image->y1, dy));

		new_buffer->reduced_image_dim = new_buffer->unreduced_image_dim;

		max_image_dim = new_buffer->reduced_image_dim;

		new_buffer->reduced_image_dim.ceildivpow2(numresolutions - minimum_num_resolutions);

		/* clip output image to tile */
		new_buffer->reduced_tile_dim.clip(new_buffer->reduced_image_dim, &new_buffer->reduced_image_dim);
		new_buffer->unreduced_tile_dim.clip(new_buffer->unreduced_image_dim, &new_buffer->unreduced_image_dim);
	}

	/* for compress, we don't need to allocate resolutions */
	if (!m_is_encoder) {
		/* fill resolutions vector */
        assert(numresolutions>0);
		TileBufferResolution *prev_res = nullptr;
		for (int32_t resno = (int32_t) (numresolutions - 1); resno >= 0; --resno) {
			uint32_t bandno;
			grk_tcd_resolution *tcd_res = resolutions + resno;
			TileBufferResolution *res = (TileBufferResolution*) grk_calloc(1,
					sizeof(TileBufferResolution));
			if (!res) {
				delete new_buffer;
				return false;
			}

			res->bounds.x = tcd_res->x1 - tcd_res->x0;
			res->bounds.y = tcd_res->y1 - tcd_res->y0;
			res->origin.x = tcd_res->x0;
			res->origin.y = tcd_res->y0;

			for (bandno = 0; bandno < tcd_res->numbands; ++bandno) {
				grk_tcd_band *band = tcd_res->bands + bandno;
				grk_rect band_rect;
				band_rect = grk_rect(band->x0, band->y0, band->x1, band->y1);

				res->band_region[bandno] =
						prev_res ? prev_res->band_region[bandno] : max_image_dim;
				if (resno > 0) {

					/*For next level down, E' = ceil((E-b)/2) where b in {0,1} identifies band
					 * see Chapter 11 of Taubman and Marcellin for more details
					 * */
					grk_pt shift;
					shift.x = -(int64_t)(band->bandno & 1);
					shift.y = -(int64_t)(band->bandno >> 1);

					res->band_region[bandno].pan(&shift);
					res->band_region[bandno].ceildivpow2(1);

					// boundary padding. This number is slightly larger than it should be theoretically,
					// but we want to make sure that we don't have bugs at the region boundaries
					res->band_region[bandno].grow(4);

				}
			}
			res->num_bands = tcd_res->numbands;
			new_buffer->resolutions.push_back(res);
			prev_res = res;
		}
	}
	delete buf;
	buf = new_buffer;

	return true;
}

/**
 * Deallocates the encoding data of the given precinct.
 */
void TileComponent::code_block_dec_deallocate(grk_tcd_precinct *p_precinct) {
	uint64_t cblkno, nb_code_blocks;
	auto code_block = p_precinct->cblks.dec;
	if (code_block) {
		/*fprintf(stderr,"deallocate codeblock:{\n");*/
		/*fprintf(stderr,"\t x0=%d, y0=%d, x1=%d, y1=%d\n",code_block->x0, code_block->y0, code_block->x1, code_block->y1);*/
		/*fprintf(stderr,"\t numbps=%d, numlenbits=%d, len=%d, numPassesInPacket=%d, reanum_segs=%d, numSegmentsAllocated=%d\n ",
		 code_block->numbps, code_block->numlenbits, code_block->len, code_block->numPassesInPacket, code_block->numSegments, code_block->numSegmentsAllocated );*/

		nb_code_blocks = p_precinct->block_size / sizeof(grk_tcd_cblk_dec);
		/*fprintf(stderr,"nb_code_blocks =%d\t}\n", nb_code_blocks);*/

		for (cblkno = 0; cblkno < nb_code_blocks; ++cblkno) {
			code_block->cleanup();
			++code_block;
		}
		grok_free(p_precinct->cblks.dec);
		p_precinct->cblks.dec = nullptr;
	}
}

/**
 * Deallocates the encoding data of the given precinct.
 */
void TileComponent::code_block_enc_deallocate(grk_tcd_precinct *p_precinct) {
	uint64_t cblkno, nb_code_blocks;
	auto code_block = p_precinct->cblks.enc;
	if (code_block) {
		nb_code_blocks = p_precinct->block_size / sizeof(grk_tcd_cblk_enc);
		for (cblkno = 0; cblkno < nb_code_blocks; ++cblkno) {
			code_block->cleanup();
			++code_block;
		}
		grok_free(p_precinct->cblks.enc);
		p_precinct->cblks.enc = nullptr;
	}
}


}

