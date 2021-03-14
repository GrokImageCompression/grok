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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
 */

#include "grk_includes.h"

namespace grk {


/**
 * Apply resolution reduction to header image components
 *
 * @param headerImage	header image
 * @param p_cp			the coding parameters from which to update the image.
 */
void SIZMarker::subsampleAndReduceHeaderImageComponents(GrkImage *headerImage,
		const CodingParams *p_cp) {

	//1. calculate canvas coordinates of image
	uint32_t x0 = std::max<uint32_t>(p_cp->tx0, headerImage->x0);
	uint32_t y0 = std::max<uint32_t>(p_cp->ty0, headerImage->y0);

	/* validity of p_cp members used here checked in j2k_read_siz. Can't overflow. */
	uint32_t x1 = p_cp->tx0 + (p_cp->t_grid_width - 1U) * p_cp->t_width;
	uint32_t y1 = p_cp->ty0 + (p_cp->t_grid_height - 1U) * p_cp->t_height;

	 /* (use saturated add to prevent overflow) */
	x1 = std::min<uint32_t>(satAdd<uint32_t>(x1, p_cp->t_width), headerImage->x1);
	y1 = std::min<uint32_t>(satAdd<uint32_t>(y1, p_cp->t_height), headerImage->y1);

	auto imageBounds = grkRectU32(x0,y0,x1,y1);

	// 2. sub-sample and apply resolution reduction
	uint32_t reduce = p_cp->m_coding_params.m_dec.m_reduce;
	for (uint32_t i = 0; i < headerImage->numcomps; ++i) {
		auto comp = headerImage->comps + i;
		auto compBounds = imageBounds.rectceildiv(comp->dx, comp->dy);
		comp->w  = ceildivpow2<uint32_t>(compBounds.width(),reduce);
		comp->h  = ceildivpow2<uint32_t>(compBounds.height(),reduce);
		comp->x0 = ceildivpow2<uint32_t>(compBounds.x0, reduce);
		comp->y0 = ceildivpow2<uint32_t>(compBounds.y0, reduce);
	}
}


bool SIZMarker::read(CodeStreamDecompress *codeStream, uint8_t *p_header_data,
		uint16_t header_size){
	assert(codeStream != nullptr);
	assert(p_header_data != nullptr);

	uint32_t i;
	uint32_t nb_comp;
	uint32_t nb_comp_remain;
	uint32_t remaining_size;
	uint16_t nb_tiles;
	auto decompressor = codeStream->getDecompressorState();

	auto image = codeStream->getHeaderImage();
	auto cp = codeStream->getCodingParams();

	/* minimum size == 39 - 3 (= minimum component parameter) */
	if (header_size < 36) {
		GRK_ERROR("Error with SIZ marker size");
		return false;
	}

	remaining_size = header_size - 36U;
	nb_comp = remaining_size / 3;
	nb_comp_remain = remaining_size % 3;
	if (nb_comp_remain != 0) {
		GRK_ERROR("Error with SIZ marker size");
		return false;
	}

	uint32_t tmp;
	grk_read<uint32_t>(p_header_data, &tmp, 2); /* Rsiz (capabilities) */
	p_header_data += 2;

	// sanity check on RSIZ
	if (tmp & GRK_PROFILE_PART2) {
		// no sanity check on part 2 profile at the moment
		//profile = GRK_PROFILE_PART2;
		//uint16_t part2_extensions = tmp & GRK_PROFILE_PART2_EXTENSIONS_MASK;
	} else {
		if (tmp & 0x3000) {
			GRK_WARN("SIZ marker segment's Rsiz word must have bits 12 and 13 equal to 0");
			GRK_WARN("unless the Part-2 flag (bit-15) is set.");
		}
		uint16_t profile = tmp & GRK_PROFILE_MASK;
		if ((profile > GRK_PROFILE_CINEMA_LTS)
				&& !GRK_IS_BROADCAST(profile) && !GRK_IS_IMF(profile)) {
			GRK_ERROR("Non-compliant Rsiz value 0x%x in SIZ marker", tmp);
			return false;
		}
	}

	cp->rsiz = (uint16_t) tmp;
	grk_read<uint32_t>(p_header_data, &image->x1); /* Xsiz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &image->y1); /* Ysiz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &image->x0); /* X0siz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &image->y0); /* Y0siz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &cp->t_width); /* XTsiz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &cp->t_height); /* YTsiz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &cp->tx0); /* XT0siz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &cp->ty0); /* YT0siz */
	p_header_data += 4;
	grk_read<uint32_t>(p_header_data, &tmp, 2); /* Csiz */
	p_header_data += 2;
	if (tmp ==0) {
		GRK_ERROR("SIZ marker: number of components cannot be zero");
		return false;
	}
	if (tmp <= max_num_components)
		image->numcomps = (uint16_t) tmp;
	else {
		GRK_ERROR("SIZ marker: number of components %u is greater than maximum allowed number of components %d",
				tmp,max_num_components);
		return false;
	}

	if (image->numcomps != nb_comp) {
		GRK_ERROR("SIZ marker: signalled number of components is not compatible with remaining number of components ( %u vs %u)",
				image->numcomps, nb_comp);
		return false;
	}

	/* testcase 4035.pdf.SIGSEGV.d8b.3375 */
	/* testcase issue427-null-image-size.jp2 */
	if ((image->x0 >= image->x1) || (image->y0 >= image->y1)) {
		std::stringstream ss;
		ss << "SIZ marker: negative or zero image dimensions ("
				<< (int64_t) image->x1 - image->x0 << " x "
				<< (int64_t) image->y1 - image->y0 << ")";
		GRK_ERROR("%s", ss.str().c_str());
		return false;
	}
	/* testcase 2539.pdf.SIGFPE.706.1712 (also 3622.pdf.SIGFPE.706.2916 and 4008.pdf.SIGFPE.706.3345 and maybe more) */
	if ((cp->t_width == 0U) || (cp->t_height == 0U)) {
		GRK_ERROR("SIZ marker: invalid tile size (%u, %u)",
				cp->t_width, cp->t_height);
		return false;
	}

	/* testcase issue427-illegal-tile-offset.jp2 */
	if (cp->tx0 > image->x0 || cp->ty0 > image->y0) {
		GRK_ERROR("SIZ marker: tile origin (%u,%u) cannot lie in the region"
						" to the right and bottom of image origin (%u,%u)",
				cp->tx0, cp->ty0, image->x0, image->y0);
		return false;
	}
	uint32_t tx1 = satAdd<uint32_t>(cp->tx0, cp->t_width); /* manage overflow */
	uint32_t ty1 = satAdd<uint32_t>(cp->ty0, cp->t_height); /* manage overflow */
	if (tx1 <= image->x0 || ty1 <= image->y0) {
		GRK_ERROR("SIZ marker: first tile (%u,%u,%u,%u) must overlap"
				" image (%u,%u,%u,%u)", cp->tx0, cp->ty0, tx1, ty1, image->x0,
				image->y0, image->x1, image->y1);
		return false;
	}

	/* Allocate the resulting image components */
	image->comps = new grk_image_comp[image->numcomps];
	memset(image->comps,0,image->numcomps * sizeof(grk_image_comp));
	auto img_comp = image->comps;

	/* Read the component information */
	for (i = 0; i < image->numcomps; ++i) {
		grk_read<uint32_t>(p_header_data++, &tmp, 1); /* Ssiz_i */
		img_comp->prec = (uint8_t)((tmp & 0x7f) + 1);
		img_comp->sgnd = tmp >> 7;
		grk_read<uint32_t>(p_header_data++, &tmp, 1); /* XRsiz_i */
		img_comp->dx = tmp; /* should be between 1 and 255 */
		grk_read<uint32_t>(p_header_data++, &tmp, 1); /* YRsiz_i */
		img_comp->dy = tmp; /* should be between 1 and 255 */
		if (img_comp->dx < 1 || img_comp->dx > 255 || img_comp->dy < 1
				|| img_comp->dy > 255) {
			GRK_ERROR("Invalid values for comp = %u : dx=%u dy=%u\n (should be between 1 and 255 according to the JPEG2000 standard)",
					i, img_comp->dx, img_comp->dy);
			return false;
		}

		if (img_comp->prec == 0 || img_comp->prec > max_supported_precision) {
			GRK_ERROR("Unsupported precision for comp = %u : prec=%u (this library only supports precisions between 1 and %u)",
					i, img_comp->prec, max_supported_precision);
			return false;
		}
		++img_comp;
	}

	/* Compute the number of tiles */
	cp->t_grid_width = ceildiv<uint32_t>(image->x1 - cp->tx0, cp->t_width);
	cp->t_grid_height = ceildiv<uint32_t>(image->y1 - cp->ty0, cp->t_height);

	/* Check that the number of tiles is valid */
	if (cp->t_grid_width == 0 || cp->t_grid_height == 0) {
		GRK_ERROR("Invalid grid of tiles: %u x %u. JPEG 2000 standard requires at least one tile in grid. ",
				cp->t_grid_width, cp->t_grid_height);
		return false;
	}
	if ((uint64_t)cp->t_grid_width * cp->t_grid_height > (uint64_t)max_num_tiles) {
		GRK_ERROR("Invalid grid of tiles : %u x %u.  JPEG 2000 standard specifies maximum of %u tiles",
				cp->t_grid_width, cp->t_grid_height, max_num_tiles);
		return false;
	}
	nb_tiles = (uint16_t)(cp->t_grid_width * cp->t_grid_height);

	/* Define the tiles which will be decompressed */
	if (!codeStream->isWholeTileDecompress()) {
		decompressor->m_start_tile_x_index = (decompressor->m_start_tile_x_index - cp->tx0) / cp->t_width;
		decompressor->m_start_tile_y_index = (decompressor->m_start_tile_y_index - cp->ty0) / cp->t_height;
		decompressor->m_end_tile_x_index   = ceildiv<uint32_t>((decompressor->m_end_tile_x_index	- cp->tx0), cp->t_width);
		decompressor->m_end_tile_y_index   = ceildiv<uint32_t>((decompressor->m_end_tile_y_index	- cp->ty0), cp->t_height);
	} else {
		decompressor->m_start_tile_x_index = 0;
		decompressor->m_start_tile_y_index = 0;
		decompressor->m_end_tile_x_index = cp->t_grid_width;
		decompressor->m_end_tile_y_index =	cp->t_grid_height;
	}

	/* memory allocations */
	cp->tcps = new TileCodingParams[nb_tiles];
	decompressor->m_default_tcp->tccps = new  TileComponentCodingParams[image->numcomps];
	decompressor->m_default_tcp->m_mct_records =
			(grk_mct_data*) grkCalloc(default_number_mct_records,
					sizeof(grk_mct_data));

	if (!decompressor->m_default_tcp->m_mct_records) {
		GRK_ERROR("Not enough memory to take in charge SIZ marker");
		return false;
	}
	decompressor->m_default_tcp->m_nb_max_mct_records =
			default_number_mct_records;

	decompressor->m_default_tcp->m_mcc_records =
			(grk_simple_mcc_decorrelation_data*) grkCalloc(
					default_number_mcc_records,
					sizeof(grk_simple_mcc_decorrelation_data));

	if (!decompressor->m_default_tcp->m_mcc_records) {
		GRK_ERROR("Not enough memory to take in charge SIZ marker");
		return false;
	}
	decompressor->m_default_tcp->m_nb_max_mcc_records =
			default_number_mcc_records;

	/* set up default dc level shift */
	for (i = 0; i < image->numcomps; ++i) {
		if (!image->comps[i].sgnd)
			decompressor->m_default_tcp->tccps[i].m_dc_level_shift =
					1 << (image->comps[i].prec - 1);
	}

	for (i = 0; i < nb_tiles; ++i) {
		auto current_tile_param = cp->tcps + i;
		current_tile_param->tccps = new TileComponentCodingParams[image->numcomps];
	}
	decompressor->setState(J2K_DEC_STATE_MH);
	subsampleAndReduceHeaderImageComponents(image, cp);

	return true;
}

bool SIZMarker::write(CodeStreamCompress *codeStream, BufferedStream *stream){
	uint32_t i;
	uint32_t size_len;

	assert(stream != nullptr);
	assert(codeStream != nullptr);

	auto image = codeStream->getHeaderImage();
	auto cp = codeStream->getCodingParams();
	size_len = 40 + 3U * image->numcomps;
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
		uint8_t bpc = (uint8_t) (comp->prec - 1);
		if (comp->sgnd)
			bpc = (uint8_t)(bpc + (1 << 7));
		if (!stream->write_byte(bpc))
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
