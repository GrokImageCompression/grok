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
 * Copyright (c) 2006-2007, Parvatha Elangovan
 * Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
 * Copyright (c) 2011-2012, Centre National d'Etudes Spatiales (CNES), France
 * Copyright (c) 2012, CS Systemes d'Information, France
 *
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

bool SIZMarker::read(CodeStream *codeStream, uint8_t *p_header_data,
		uint16_t header_size){
	uint32_t i;
	uint32_t nb_comp;
	uint32_t nb_comp_remain;
	uint32_t remaining_size;
	uint16_t nb_tiles;
	uint32_t tmp, tx1, ty1;
	grk_image_comp *img_comp = nullptr;
	TileCodingParams *current_tile_param = nullptr;
	DecoderState  *decoder = &codeStream->m_specific_param.m_decoder;

	assert(codeStream != nullptr);
	assert(p_header_data != nullptr);

	auto image = codeStream->m_private_image;
	auto cp = &(codeStream->m_cp);

	/* minimum size == 39 - 3 (= minimum component parameter) */
	if (header_size < 36) {
		GROK_ERROR("Error with SIZ marker size");
		return false;
	}

	remaining_size = header_size - 36;
	nb_comp = remaining_size / 3;
	nb_comp_remain = remaining_size % 3;
	if (nb_comp_remain != 0) {
		GROK_ERROR("Error with SIZ marker size");
		return false;
	}

	grk_read<uint32_t>(p_header_data, &tmp, 2); /* Rsiz (capabilities) */
	p_header_data += 2;

	// sanity check on RSIZ
	uint16_t profile = 0;
	uint16_t part2_extensions = 0;
	// check for Part 2
	if (tmp & GRK_PROFILE_PART2) {
		profile = GRK_PROFILE_PART2;
		part2_extensions = tmp & GRK_PROFILE_PART2_EXTENSIONS_MASK;
		(void) part2_extensions;
	} else {
		profile = tmp & GRK_PROFILE_MASK;
		if ((profile > GRK_PROFILE_CINEMA_LTS)
				&& !GRK_IS_BROADCAST(profile) && !GRK_IS_IMF(profile)) {
			GROK_ERROR("Non-compliant Rsiz value 0x%x in SIZ marker", tmp);
			return false;
		}
	}

	cp->rsiz = (uint16_t) tmp;
	grk_read<uint32_t>(p_header_data, &image->x1, 4); /* Xsiz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &image->y1, 4); /* Ysiz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &image->x0, 4); /* X0siz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &image->y0, 4); /* Y0siz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &cp->t_width, 4); /* XTsiz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &cp->t_height, 4); /* YTsiz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &cp->tx0, 4); /* XT0siz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &cp->ty0, 4); /* YT0siz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &tmp, 2); /* Csiz */
	p_header_data += 2;
	if (tmp <= max_num_components)
		image->numcomps = (uint16_t) tmp;
	else {
		GROK_ERROR("Error in SIZ marker: number of component is illegal -> %d",
				tmp);
		return false;
	}

	if (image->numcomps != nb_comp) {
		GROK_ERROR(
				"Error in SIZ marker: number of component is not compatible with the remaining number of parameters ( %d vs %d)",
				image->numcomps, nb_comp);
		return false;
	}

	/* testcase 4035.pdf.SIGSEGV.d8b.3375 */
	/* testcase issue427-null-image-size.jp2 */
	if ((image->x0 >= image->x1) || (image->y0 >= image->y1)) {
		std::stringstream ss;
		ss << "Error in SIZ marker: negative or zero image dimensions ("
				<< (int64_t) image->x1 - image->x0 << " x "
				<< (int64_t) image->y1 - image->y0 << ")" << std::endl;
		GROK_ERROR("%s", ss.str().c_str());
		return false;
	}
	/* testcase 2539.pdf.SIGFPE.706.1712 (also 3622.pdf.SIGFPE.706.2916 and 4008.pdf.SIGFPE.706.3345 and maybe more) */
	if ((cp->t_width == 0U) || (cp->t_height == 0U)) {
		GROK_ERROR("Error in SIZ marker: invalid tile size (%d, %d)",
				cp->t_width, cp->t_height);
		return false;
	}

	/* testcase issue427-illegal-tile-offset.jp2 */
	if (cp->tx0 > image->x0 || cp->ty0 > image->y0) {
		GROK_ERROR(
				"Error in SIZ marker: tile origin (%d,%d) cannot lie in the region"
						" to the right and bottom of image origin (%d,%d)",
				cp->tx0, cp->ty0, image->x0, image->y0);
		return false;
	}
	tx1 = uint_adds(cp->tx0, cp->t_width); /* manage overflow */
	ty1 = uint_adds(cp->ty0, cp->t_height); /* manage overflow */
	if (tx1 <= image->x0 || ty1 <= image->y0) {
		GROK_ERROR("Error in SIZ marker: first tile (%d,%d,%d,%d) must overlap"
				" image (%d,%d,%d,%d)", cp->tx0, cp->ty0, tx1, ty1, image->x0,
				image->y0, image->x1, image->y1);
		return false;
	}

	uint64_t tileArea = (uint64_t) cp->t_width * cp->t_height;
	if (tileArea > max_tile_area) {
		GROK_ERROR(
				"Error in SIZ marker: tile area = %llu greater than max tile area = %llu",
				tileArea, max_tile_area);
		return false;

	}

	/* Allocate the resulting image components */
	image->comps = (grk_image_comp*) grk_calloc(image->numcomps,
			sizeof(grk_image_comp));
	if (image->comps == nullptr) {
		image->numcomps = 0;
		GROK_ERROR("Not enough memory to take in charge SIZ marker");
		return false;
	}

	img_comp = image->comps;

	/* Read the component information */
	for (i = 0; i < image->numcomps; ++i) {
		uint32_t tmp;
		grk_read<uint32_t>(p_header_data, &tmp, 1); /* Ssiz_i */
		++p_header_data;
		img_comp->prec = (tmp & 0x7f) + 1;
		img_comp->sgnd = tmp >> 7;
		grk_read<uint32_t>(p_header_data, &tmp, 1); /* XRsiz_i */
		++p_header_data;
		img_comp->dx = tmp; /* should be between 1 and 255 */
		grk_read<uint32_t>(p_header_data, &tmp, 1); /* YRsiz_i */
		++p_header_data;
		img_comp->dy = tmp; /* should be between 1 and 255 */
		if (img_comp->dx < 1 || img_comp->dx > 255 || img_comp->dy < 1
				|| img_comp->dy > 255) {
			GROK_ERROR(
					"Invalid values for comp = %d : dx=%u dy=%u\n (should be between 1 and 255 according to the JPEG2000 standard)",
					i, img_comp->dx, img_comp->dy);
			return false;
		}

		if (img_comp->prec == 0 || img_comp->prec > max_supported_precision) {
			GROK_ERROR(
					"Unsupported precision for comp = %d : prec=%u (Grok only supportes precision between 1 and %d)",
					i, img_comp->prec, max_supported_precision);
			return false;
		}
		img_comp->resno_decoded = 0; /* number of resolution decoded */
		++img_comp;
	}

	/* Compute the number of tiles */
	cp->t_grid_width = ceildiv<uint32_t>(image->x1 - cp->tx0, cp->t_width);
	cp->t_grid_height = ceildiv<uint32_t>(image->y1 - cp->ty0, cp->t_height);

	/* Check that the number of tiles is valid */
	if (cp->t_grid_width == 0 || cp->t_grid_height == 0) {
		GROK_ERROR(
				"Invalid grid of tiles: %u x %u. JPEG 2000 standard requires at least one tile in grid. ",
				cp->t_grid_width, cp->t_grid_height);
		return false;
	}
	if (cp->t_grid_width * cp->t_grid_height > max_num_tiles) {
		GROK_ERROR(
				"Invalid grid of tiles : %u x %u.  JPEG 2000 standard specifies maximum of %d tiles",
				max_num_tiles, cp->t_grid_width, cp->t_grid_height);
		return false;
	}
	nb_tiles = (uint16_t)(cp->t_grid_width * cp->t_grid_height);

	/* Define the tiles which will be decoded */
	if (decoder->m_discard_tiles) {
		decoder->m_start_tile_x_index =
				(decoder->m_start_tile_x_index
						- cp->tx0) / cp->t_width;
		decoder->m_start_tile_y_index =
				(decoder->m_start_tile_y_index
						- cp->ty0) / cp->t_height;
		decoder->m_end_tile_x_index =
				ceildiv<uint32_t>(
						(decoder->m_end_tile_x_index
								- cp->tx0), cp->t_width);
		decoder->m_end_tile_y_index =
				ceildiv<uint32_t>(
						(decoder->m_end_tile_y_index
								- cp->ty0), cp->t_height);
	} else {
		decoder->m_start_tile_x_index = 0;
		decoder->m_start_tile_y_index = 0;
		decoder->m_end_tile_x_index = cp->t_grid_width;
		decoder->m_end_tile_y_index =
				cp->t_grid_height;
	}

	/* memory allocations */
	cp->tcps = new TileCodingParams[nb_tiles];
	decoder->m_default_tcp->tccps =
			(TileComponentCodingParams*) grk_calloc(image->numcomps, sizeof(TileComponentCodingParams));
	if (decoder->m_default_tcp->tccps == nullptr) {
		GROK_ERROR("Not enough memory to take in charge SIZ marker");
		return false;
	}

	decoder->m_default_tcp->m_mct_records =
			(grk_mct_data*) grk_calloc(default_number_mct_records,
					sizeof(grk_mct_data));

	if (!decoder->m_default_tcp->m_mct_records) {
		GROK_ERROR("Not enough memory to take in charge SIZ marker");
		return false;
	}
	decoder->m_default_tcp->m_nb_max_mct_records =
			default_number_mct_records;

	decoder->m_default_tcp->m_mcc_records =
			(grk_simple_mcc_decorrelation_data*) grk_calloc(
					default_number_mcc_records,
					sizeof(grk_simple_mcc_decorrelation_data));

	if (!decoder->m_default_tcp->m_mcc_records) {
		GROK_ERROR("Not enough memory to take in charge SIZ marker");
		return false;
	}
	decoder->m_default_tcp->m_nb_max_mcc_records =
			default_number_mcc_records;

	/* set up default dc level shift */
	for (i = 0; i < image->numcomps; ++i) {
		if (!image->comps[i].sgnd) {
			decoder->m_default_tcp->tccps[i].m_dc_level_shift =
					1 << (image->comps[i].prec - 1);
		}
	}

	current_tile_param = cp->tcps;
	for (i = 0; i < nb_tiles; ++i) {
		current_tile_param->tccps = (TileComponentCodingParams*) grk_calloc(image->numcomps,
				sizeof(TileComponentCodingParams));
		if (current_tile_param->tccps == nullptr) {
			GROK_ERROR("Not enough memory to take in charge SIZ marker");
			return false;
		}

		++current_tile_param;
	}

	decoder->m_state = J2K_DEC_STATE_MH;
	grk_image_comp_header_update(image, cp);

	return true;

}

bool SIZMarker::write(CodeStream *codeStream, BufferedStream *stream){
	uint32_t i;
	uint32_t size_len;

	assert(stream != nullptr);
	assert(codeStream != nullptr);

	auto image = codeStream->m_private_image;
	auto cp = &(codeStream->m_cp);
	size_len = 40 + 3 * image->numcomps;
	/* write SOC identifier */

	/* SIZ */
	if (!stream->write_short(J2K_MS_SIZ))
		return false;

	/* L_SIZ */
	if (!stream->write_short((uint16_t) (size_len - 2)))
		return false;
	/* Rsiz (capabilities) */
	if (!stream->write_short(cp->rsiz))
		return false;
	/* Xsiz */
	if (!stream->write_int(image->x1))
		return false;
	/* Ysiz */
	if (!stream->write_int(image->y1))
		return false;
	/* X0siz */
	if (!stream->write_int(image->x0))
		return false;
	/* Y0siz */
	if (!stream->write_int(image->y0))
		return false;
	/* XTsiz */
	if (!stream->write_int(cp->t_width))
		return false;
	/* YTsiz */
	if (!stream->write_int(cp->t_height))
		return false;
	/* XT0siz */
	if (!stream->write_int(cp->tx0))
		return false;
	/* YT0siz */
	if (!stream->write_int(cp->ty0))
		return false;
	/* Csiz */
	if (!stream->write_short((uint16_t) image->numcomps))
		return false;
	for (i = 0; i < image->numcomps; ++i) {
		auto comp = image->comps + i;
		/* TODO here with MCT ? */
		uint8_t bpcc = (uint8_t) (comp->prec - 1);
		if (comp->sgnd)
			bpcc += (uint8_t)(1 << 7);
		if (!stream->write_byte(bpcc))
			return false;
		/* XRsiz_i */
		if (!stream->write_byte((uint8_t) comp->dx))
			return false;
		/* YRsiz_i */
		if (!stream->write_byte((uint8_t) comp->dy))
			return false;
	}

	return true;
}

}
