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
 * Copyright (c) 2005, Herve Drolon, FreeImage Team
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

grk_image *  GRK_CALLCONV grk_image_create(uint32_t numcmpts,
		 grk_image_cmptparm  *cmptparms, GRK_COLOR_SPACE clrspc) {
	auto image = (grk_image * ) grk::grk_calloc(1, sizeof(grk_image));

	if (image) {
		image->color_space = clrspc;
		image->numcomps = numcmpts;
		/* allocate memory for the per-component information */
		image->comps = ( grk_image_comp  * ) grk::grk_calloc(1,
				image->numcomps * sizeof( grk_image_comp) );
		if (!image->comps) {
			grk::GROK_ERROR("Unable to allocate memory for image.");
			grk_image_destroy(image);
			return nullptr;
		}
		/* create the individual image components */
		for (uint32_t compno = 0; compno < numcmpts; compno++) {
			auto comp = &image->comps[compno];

			comp->dx = cmptparms[compno].dx;
			comp->dy = cmptparms[compno].dy;
			comp->w = cmptparms[compno].w;
			comp->h = cmptparms[compno].h;
			comp->x0 = cmptparms[compno].x0;
			comp->y0 = cmptparms[compno].y0;
			comp->prec = cmptparms[compno].prec;
			comp->sgnd = cmptparms[compno].sgnd;
			if (!grk_image_single_component_data_alloc(comp)) {
				grk::GROK_ERROR("Unable to allocate memory for image.");
				grk_image_destroy(image);
				return nullptr;
			}
			comp->type = GRK_COMPONENT_TYPE_COLOUR;
			switch(compno){
			case 0:
				comp->association = GRK_COMPONENT_ASSOC_COLOUR_1;
				break;
			case 1:
				comp->association = GRK_COMPONENT_ASSOC_COLOUR_2;
				break;
			case 2:
				comp->association = GRK_COMPONENT_ASSOC_COLOUR_3;
				break;
			default:
				comp->association = GRK_COMPONENT_ASSOC_UNASSOCIATED;
				comp->type = GRK_COMPONENT_TYPE_UNSPECIFIED;
				break;
			}
		}
	}

	return image;
}

void GRK_CALLCONV grk_image_destroy(grk_image *image) {
	if (image) {
		if (image->comps) {
			grk_image_all_components_data_free(image);
			grk::grok_free(image->comps);
		}
		if (image->icc_profile_buf) {
			grk_buffer_delete(image->icc_profile_buf);
			image->icc_profile_buf = nullptr;
		}
		if (image->iptc_buf) {
			grk_buffer_delete(image->iptc_buf);
			image->iptc_buf = nullptr;
		}
		if (image->xmp_buf) {
			grk_buffer_delete(image->xmp_buf);
			image->xmp_buf = nullptr;
		}
		grk::grok_free(image);
	}
}

namespace grk {

grk_image *  grk_image_create0(void) {
	return (grk_image * ) grk_calloc(1, sizeof(grk_image));
}

/**
 * Updates the components characteristics of the image from the coding parameters.
 *
 * @param image_header	the image header to update.
 * @param p_cp				the coding parameters from which to update the image.
 */
void grk_image_comp_header_update(grk_image *image_header,
		const grk_coding_parameters *p_cp) {

	//1. calculate canvas coordinates of image
	uint32_t x0 = std::max<uint32_t>(p_cp->tx0, image_header->x0);
	uint32_t y0 = std::max<uint32_t>(p_cp->ty0, image_header->y0);

	/* validity of p_cp members used here checked in j2k_read_siz. Can't overflow. */
	uint32_t x1 = p_cp->tx0 + (p_cp->t_grid_width - 1U) * p_cp->t_width;
	uint32_t y1 = p_cp->ty0 + (p_cp->t_grid_height - 1U) * p_cp->t_height;

	 /* use add saturated to prevent overflow */
	x1 = std::min<uint32_t>(uint_adds(x1, p_cp->t_width), image_header->x1);
	y1 = std::min<uint32_t>(uint_adds(y1, p_cp->t_height), image_header->y1);

	// 2. convert from canvas to tile coordinates, taking into account
	// resolution reduction
	uint32_t reduce = p_cp->m_coding_param.m_dec.m_reduce;
	for (uint32_t i = 0; i < image_header->numcomps; ++i) {
		auto img_comp = image_header->comps + i;
		uint32_t comp_x0 = ceildiv<uint32_t>(x0, img_comp->dx);
		uint32_t comp_y0 = ceildiv<uint32_t>(y0, img_comp->dy);
		uint32_t comp_x1 = ceildiv<uint32_t>(x1, img_comp->dx);
		uint32_t comp_y1 = ceildiv<uint32_t>(y1, img_comp->dy);
		uint32_t width = uint_ceildivpow2(comp_x1 - comp_x0,reduce);
		uint32_t height = uint_ceildivpow2(comp_y1 - comp_y0,reduce);
		img_comp->w = width;
		img_comp->h = height;
		img_comp->x0 = comp_x0;
		img_comp->y0 = comp_y0;
	}
}

/**
 * Copy only header of image and its component header (no data are copied)
 * if dest image have data, they will be freed
 *
 * @param	image_src		the src image
 * @param	image_dest	the dest image
 *
 */
void grk_copy_image_header(const grk_image *image_src,grk_image *image_dest) {
	assert(image_src != nullptr);
	assert(image_dest != nullptr);

	image_dest->x0 = image_src->x0;
	image_dest->y0 = image_src->y0;
	image_dest->x1 = image_src->x1;
	image_dest->y1 = image_src->y1;

	if (image_dest->comps) {
		grk_image_all_components_data_free(image_dest);
		grok_free(image_dest->comps);
		image_dest->comps = nullptr;
	}
	image_dest->numcomps = image_src->numcomps;
	image_dest->comps = ( grk_image_comp  * ) grk_malloc(
			image_dest->numcomps * sizeof( grk_image_comp) );
	if (!image_dest->comps) {
		image_dest->comps = nullptr;
		image_dest->numcomps = 0;
		return;
	}

	for (uint32_t compno = 0; compno < image_dest->numcomps; compno++) {
		memcpy(&(image_dest->comps[compno]), &(image_src->comps[compno]),
				sizeof( grk_image_comp) );
		image_dest->comps[compno].data = nullptr;
	}

	image_dest->color_space = image_src->color_space;
	image_dest->icc_profile_len = image_src->icc_profile_len;
	if (image_dest->icc_profile_len) {
		image_dest->icc_profile_buf = grk_buffer_new(
				image_dest->icc_profile_len);
		memcpy(image_dest->icc_profile_buf, image_src->icc_profile_buf,
				image_src->icc_profile_len);
	} else
		image_dest->icc_profile_buf = nullptr;

	return;
}

bool update_image_dimensions(grk_image* image, uint32_t reduce)
{
    for (uint32_t compno = 0; compno < image->numcomps; ++compno) {
        auto img_comp = image->comps + compno;
        uint32_t temp1,temp2;

        if (image->x0 > (uint32_t)INT_MAX ||
                image->y0 > (uint32_t)INT_MAX ||
                image->x1 > (uint32_t)INT_MAX ||
                image->y1 > (uint32_t)INT_MAX) {
            GROK_ERROR("Image coordinates above INT_MAX are not supported\n");
            return false;
        }

        img_comp->x0 = ceildiv<uint32_t>(image->x0,img_comp->dx);
        img_comp->y0 = ceildiv<uint32_t>(image->y0, img_comp->dy);
        uint32_t comp_x1 = ceildiv<uint32_t>(image->x1, img_comp->dx);
        uint32_t comp_y1 = ceildiv<uint32_t>(image->y1, img_comp->dy);

        temp1 = uint_ceildivpow2(comp_x1, reduce);
        temp2 = uint_ceildivpow2(img_comp->x0, reduce);
        if (temp1 < temp2) {
            GROK_ERROR("Size x of the decoded component image is incorrect (comp[%d].w=%d).",
                          compno, (int32_t)temp1 - (int32_t)temp2);
            return false;
        }
        img_comp->w  = (uint32_t)(temp1 - temp2);

        temp1 = uint_ceildivpow2(comp_y1, reduce);
        temp2 = uint_ceildivpow2(img_comp->y0, reduce);
         if (temp1 < temp2) {
            GROK_ERROR("Size y of the decoded component image is incorrect (comp[%d].h=%d).",
                          compno, (int32_t)temp1 - (int32_t)temp2);
            return false;
        }
        img_comp->h = (uint32_t)(temp1 - temp2);
    }

    return true;
}


/**
 Transfer data from src to dest for each component, and null out src data.
 Assumption:  src and dest have the same number of components
 */
void transfer_image_data(grk_image *src, grk_image *dest) {
	if (!src || !dest || !src->comps || !dest->comps
			|| src->numcomps != dest->numcomps)
		return;

	for (uint32_t compno = 0; compno < src->numcomps; compno++) {
		auto src_comp = src->comps + compno;
		auto dest_comp = dest->comps + compno;

		dest_comp->resno_decoded = src_comp->resno_decoded;
		grk_image_single_component_data_free(dest_comp);
		dest_comp->data = src_comp->data;
		dest_comp->owns_data = src_comp->owns_data;
		src_comp->data = nullptr;
	}
}

}
