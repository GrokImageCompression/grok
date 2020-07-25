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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "grk_apps_config.h"
#include "grok.h"
#include "color.h"
#include "common.h"

#ifdef GROK_HAVE_LIBLCMS
#include <lcms2.h>
#endif

//#define DEBUG_PROFILE

static grk_image* image_create(uint32_t numcmpts, uint32_t w, uint32_t h,
		uint32_t prec) {
	if (!numcmpts)
		return nullptr;

	auto cmptparms = (grk_image_cmptparm*) calloc(numcmpts,
			sizeof(grk_image_cmptparm));
	if (!cmptparms)
		return nullptr;
	uint32_t compno = 0U;
	for (compno = 0U; compno < numcmpts; ++compno) {
		memset(cmptparms + compno, 0, sizeof(grk_image_cmptparm));
		cmptparms[compno].dx = 1;
		cmptparms[compno].dy = 1;
		cmptparms[compno].w = w;
		cmptparms[compno].h = h;
		cmptparms[compno].x0 = 0U;
		cmptparms[compno].y0 = 0U;
		cmptparms[compno].prec = prec;
		cmptparms[compno].sgnd = 0U;
	}
	auto img = grk_image_create(numcmpts, (grk_image_cmptparm*) cmptparms,
			GRK_CLRSPC_SRGB);
	free(cmptparms);
	return img;

}

/*--------------------------------------------------------
 Matrix for sYCC, Amendment 1 to IEC 61966-2-1

 Y  |  0.299   0.587    0.114  |    R
 Cb | -0.1687 -0.3312   0.5    | x  G
 Cr |  0.5    -0.4187  -0.0812 |    B

 Inverse:

 R   |1        -3.68213e-05    1.40199     |    Y
 G = |1.00003  -0.344125      -0.714128    | x  Cb - 2^(prec - 1)
 B   |0.999823  1.77204       -8.04142e-06 |    Cr - 2^(prec - 1)

 -----------------------------------------------------------*/
static void sycc_to_rgb(int32_t offset, int32_t upb, int32_t y, int32_t cb, int32_t cr, int32_t *out_r,
		int32_t *out_g, int32_t *out_b) {
	int32_t r, g, b;

	cb -= offset;
	cr -= offset;
	r = y + (int32_t) (1.402 * cr);
	if (r < 0)
		r = 0;
	else if (r > upb)
		r = upb;
	*out_r = r;

	g = y - (int32_t) (0.344 * cb + 0.714 *  cr);
	if (g < 0)
		g = 0;
	else if (g > upb)
		g = upb;
	*out_g = g;

	b = y + (int32_t) (1.772 * cb);
	if (b < 0)
		b = 0;
	else if (b > upb)
		b = upb;
	*out_b = b;
}

static bool sycc444_to_rgb(grk_image *img) {
	int32_t *d0, *d1, *d2, *r, *g, *b;
	const int32_t *y, *cb, *cr;
	size_t maxw, maxh, max, i;
	int32_t offset, upb;
	auto new_image = image_create(3, img->comps[0].w, img->comps[0].h,
			img->comps[0].prec);
	if (!new_image)
		return false;

	upb = (int32_t) img->comps[0].prec;
	offset = 1 << (upb - 1);
	upb = (1 << upb) - 1;

	maxw = (size_t) img->comps[0].w;
	maxh = (size_t) img->comps[0].h;
	max = maxw * maxh;

	y = img->comps[0].data;
	cb = img->comps[1].data;
	cr = img->comps[2].data;

	d0 = r = new_image->comps[0].data;
	d1 = g = new_image->comps[1].data;
	d2 = b = new_image->comps[2].data;

	new_image->comps[0].data = nullptr;
	new_image->comps[1].data = nullptr;
	new_image->comps[2].data = nullptr;

	grk_image_destroy(new_image);
	new_image = nullptr;

	for (i = 0U; i < max; ++i)
		sycc_to_rgb(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);

	grk_image_all_components_data_free(img);
	img->comps[0].data = d0;
	img->comps[0].owns_data = true;
	img->comps[1].data = d1;
	img->comps[1].owns_data = true;
	img->comps[2].data = d2;
	img->comps[2].owns_data = true;
	img->color_space = GRK_CLRSPC_SRGB;

	return true;
}/* sycc444_to_rgb() */

static bool sycc422_to_rgb(grk_image *img) {
	int32_t *d0, *d1, *d2, *r, *g, *b;
	const int32_t *y, *cb, *cr;
	size_t maxw, maxh, offx, loopmaxw;
	int32_t offset, upb;
	size_t i;

	auto new_image = image_create(3, img->comps[0].w, img->comps[0].h,
			img->comps[0].prec);
	if (!new_image)
		return false;

	upb = (int32_t) img->comps[0].prec;
	offset = 1 << (upb - 1);
	upb = (1 << upb) - 1;

	maxw = (size_t) img->comps[0].w;
	maxh = (size_t) img->comps[0].h;

	y = img->comps[0].data;
	cb = img->comps[1].data;
	cr = img->comps[2].data;

	d0 = r = new_image->comps[0].data;
	d1 = g = new_image->comps[1].data;
	d2 = b = new_image->comps[2].data;

	new_image->comps[0].data = nullptr;
	new_image->comps[1].data = nullptr;
	new_image->comps[2].data = nullptr;

	grk_image_destroy(new_image);
	new_image = nullptr;

	/* if img->x0 is odd, then first column shall use Cb/Cr = 0 */
	offx = img->x0 & 1U;
	loopmaxw = maxw - offx;

	for (i = 0U; i < maxh; ++i) {
		size_t j;

		if (offx > 0U)
			sycc_to_rgb(offset, upb, *y++, 0, 0, r++, g++, b++);

		for (j = 0U; j < (loopmaxw & ~(size_t) 1U); j += 2U) {
			sycc_to_rgb(offset, upb, *y++, *cb, *cr, r++, g++, b++);
			sycc_to_rgb(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);
		}
		if (j < loopmaxw)
			sycc_to_rgb(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);
	}
	grk_image_all_components_data_free(img);

	img->comps[0].data = d0;
	img->comps[0].owns_data = true;
	img->comps[1].data = d1;
	img->comps[1].owns_data = true;
	img->comps[2].data = d2;
	img->comps[2].owns_data = true;

	img->comps[1].w = img->comps[2].w = img->comps[0].w;
	img->comps[1].h = img->comps[2].h = img->comps[0].h;
	img->comps[1].dx = img->comps[2].dx = img->comps[0].dx;
	img->comps[1].dy = img->comps[2].dy = img->comps[0].dy;
	img->color_space = GRK_CLRSPC_SRGB;

	return true;

}/* sycc422_to_rgb() */

static bool sycc420_to_rgb(grk_image *img) {
	size_t maxw, maxh, offx, loopmaxw, offy, loopmaxh;
	int32_t offset, upb;
	auto new_image = image_create(3, img->comps[0].w, img->comps[0].h,
			img->comps[0].prec);
	if (!new_image)
		return false;

	upb = (int32_t) img->comps[0].prec;
	offset = 1 << (upb - 1);
	upb = (1 << upb) - 1;

	maxw = (size_t) img->comps[0].w;
	maxh = (size_t) img->comps[0].h;

	auto y = img->comps[0].data;
	auto cb = img->comps[1].data;
	auto cr = img->comps[2].data;

	auto d0 = new_image->comps[0].data;
	auto d1 = new_image->comps[1].data;
	auto d2 = new_image->comps[2].data;
	auto r  = new_image->comps[0].data;
	auto g  = new_image->comps[1].data;
	auto b  = new_image->comps[2].data;


	new_image->comps[0].data = nullptr;
	new_image->comps[1].data = nullptr;
	new_image->comps[2].data = nullptr;

	grk_image_destroy(new_image);
	new_image = nullptr;

	/* if img->x0 is odd, then first column shall use Cb/Cr = 0 */
	offx = img->x0 & 1U;
	loopmaxw = maxw - offx;
	/* if img->y0 is odd, then first line shall use Cb/Cr = 0 */
	offy = img->y0 & 1U;
	loopmaxh = maxh - offy;

	if (offy > 0U) {
		for (size_t j = 0U; j < maxw; ++j)
			sycc_to_rgb(offset, upb, *y++, 0, 0, r++, g++, b++);
	}
	size_t i;
	for (i = 0U; i < (loopmaxh & ~(size_t) 1U); i += 2U) {
		size_t j;

		auto ny = y + maxw;
		auto nr = r + maxw;
		auto ng = g + maxw;
		auto nb = b + maxw;

		if (offx > 0U) {
			sycc_to_rgb(offset, upb, *y++, 0, 0, r++, g++, b++);
			sycc_to_rgb(offset, upb, *ny++, *cb, *cr, nr++, ng++, nb++);
		}

		for (j = 0U; j < (loopmaxw & ~(size_t) 1U); j += 2U) {
			sycc_to_rgb(offset, upb, *y++, *cb, *cr, r++, g++, b++);
			sycc_to_rgb(offset, upb, *y++, *cb, *cr, r++, g++, b++);
			sycc_to_rgb(offset, upb, *ny++, *cb, *cr, nr++, ng++, nb++);
			sycc_to_rgb(offset, upb, *ny++, *cb++, *cr++, nr++, ng++, nb++);
		}
		if (j < loopmaxw) {
			sycc_to_rgb(offset, upb, *y++, *cb, *cr, r++, g++, b++);
			sycc_to_rgb(offset, upb, *ny++, *cb++, *cr++, nr++, ng++, nb++);
		}
		y += maxw;
		r += maxw;
		g += maxw;
		b += maxw;
	}
	if (i < loopmaxh) {
		size_t j;

		for (j = 0U; j < (maxw & ~(size_t) 1U); j += 2U) {
			sycc_to_rgb(offset, upb, *y++, *cb, *cr, r++, g++, b++);
			sycc_to_rgb(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);
		}
		if (j < maxw)
			sycc_to_rgb(offset, upb, *y, *cb, *cr, r, g, b);
	}

	grk_image_all_components_data_free(img);
	img->comps[0].data = d0;
	img->comps[0].owns_data = true;
	img->comps[1].data = d1;
	img->comps[1].owns_data = true;
	img->comps[2].data = d2;
	img->comps[2].owns_data = true;

	img->comps[1].w = img->comps[2].w = img->comps[0].w;
	img->comps[1].h = img->comps[2].h = img->comps[0].h;
	img->comps[1].dx = img->comps[2].dx = img->comps[0].dx;
	img->comps[1].dy = img->comps[2].dy = img->comps[0].dy;
	img->color_space = GRK_CLRSPC_SRGB;

	return true;

}/* sycc420_to_rgb() */

bool color_sycc_to_rgb(grk_image *img) {
	if (img->numcomps < 3) {
		spdlog::warn(
				"color_sycc_to_rgb: number of components {} is less than 3."
						" Unable to convert", img->numcomps);
		return false;
	}
	bool rc;

	if ((img->comps[0].dx == 1) && (img->comps[1].dx == 2)
			&& (img->comps[2].dx == 2) && (img->comps[0].dy == 1)
			&& (img->comps[1].dy == 2) && (img->comps[2].dy == 2)) { /* horizontal and vertical sub-sample */
		rc = sycc420_to_rgb(img);
	} else if ((img->comps[0].dx == 1) && (img->comps[1].dx == 2)
			&& (img->comps[2].dx == 2) && (img->comps[0].dy == 1)
			&& (img->comps[1].dy == 1) && (img->comps[2].dy == 1)) { /* horizontal sub-sample only */
		rc = sycc422_to_rgb(img);
	} else if ((img->comps[0].dx == 1) && (img->comps[1].dx == 1)
			&& (img->comps[2].dx == 1) && (img->comps[0].dy == 1)
			&& (img->comps[1].dy == 1) && (img->comps[2].dy == 1)) { /* no sub-sample */
		rc = sycc444_to_rgb(img);
	} else {
		spdlog::warn(
				"color_sycc_to_rgb:  Invalid sub-sampling: ({},{}), ({},{}), ({},{})."
						" Unable to convert.", img->comps[0].dx,
				img->comps[0].dy, img->comps[1].dx, img->comps[1].dy,
				img->comps[2].dx, img->comps[2].dy);
		rc = false;
	}
	if (rc)
		img->color_space = GRK_CLRSPC_SRGB;

	return rc;

}/* color_sycc_to_rgb() */

#if defined(GROK_HAVE_LIBLCMS)

/*#define DEBUG_PROFILE*/
void color_apply_icc_profile(grk_image *image, bool forceRGB) {
	cmsColorSpaceSignature in_space;
	cmsColorSpaceSignature out_space;
	cmsUInt32Number intent = 0;
	cmsHTRANSFORM transform = nullptr;
	cmsHPROFILE in_prof = nullptr;
	cmsHPROFILE out_prof = nullptr;
	cmsUInt32Number in_type, out_type;
	size_t nr_samples, max;
	uint32_t prec, max_w, max_h;
	GRK_COLOR_SPACE oldspace;
	grk_image *new_image = nullptr;
	if (image->numcomps == 0 || !grk::all_components_sanity_check(image))
		return;
	in_prof = cmsOpenProfileFromMem(image->icc_profile_buf,
			image->icc_profile_len);
#ifdef DEBUG_PROFILE
    FILE *icm = fopen("debug.icm","wb");
    fwrite( image->icc_profile_buf,1, image->icc_profile_len,icm);
    fclose(icm);
#endif

	if (in_prof == nullptr)
		return;

	in_space = cmsGetPCS(in_prof);
	out_space = cmsGetColorSpace(in_prof);
	intent = cmsGetHeaderRenderingIntent(in_prof);

	max_w = image->comps[0].w;
	max_h = image->comps[0].h;

	if (!max_w || !max_h)
		goto cleanup;

	prec = image->comps[0].prec;
	oldspace = image->color_space;

	if (out_space == cmsSigRgbData) { /* enumCS 16 */
		uint32_t i, nr_comp = image->numcomps;

		if (nr_comp > 4) {
			nr_comp = 4;
		}
		for (i = 1; i < nr_comp; ++i) {
			if (image->comps[0].dx != image->comps[i].dx)
				break;
			if (image->comps[0].dy != image->comps[i].dy)
				break;
			if (image->comps[0].prec != image->comps[i].prec)
				break;
			if (image->comps[0].sgnd != image->comps[i].sgnd)
				break;
		}
		if (i != nr_comp)
			goto cleanup;

		if (prec <= 8) {
			in_type = TYPE_RGB_8;
			out_type = TYPE_RGB_8;
		} else {
			in_type = TYPE_RGB_16;
			out_type = TYPE_RGB_16;
		}
		out_prof = cmsCreate_sRGBProfile();
		image->color_space = GRK_CLRSPC_SRGB;
	} else if (out_space == cmsSigGrayData) { /* enumCS 17 */
		in_type = TYPE_GRAY_8;
		out_type = TYPE_RGB_8;
		out_prof = cmsCreate_sRGBProfile();
		if (forceRGB)
			image->color_space = GRK_CLRSPC_SRGB;
		else
			image->color_space = GRK_CLRSPC_GRAY;
	} else if (out_space == cmsSigYCbCrData) { /* enumCS 18 */
		in_type = TYPE_YCbCr_16;
		out_type = TYPE_RGB_16;
		out_prof = cmsCreate_sRGBProfile();
		image->color_space = GRK_CLRSPC_SRGB;
	} else {
#ifdef DEBUG_PROFILE
        spdlog::error("{}:{}: color_apply_icc_profile\n\tICC Profile has unknown "
                "output colorspace(%#x)({}{}{}{})\n\tICC Profile ignored.",
                __FILE__,__LINE__,out_space,
                (out_space>>24) & 0xff,(out_space>>16) & 0xff,
                (out_space>>8) & 0xff, out_space & 0xff);
#endif
		return;
	}

#ifdef DEBUG_PROFILE
    spdlog::error("{}:{}:color_apply_icc_profile\n\tchannels({}) prec({}) w({}) h({})"
            "\n\tprofile: in({}) out({})",__FILE__,__LINE__,image->numcomps,prec,
            max_w,max_h, (void*)in_prof,(void*)out_prof);

    spdlog::error("\trender_intent ({})\n\t"
            "color_space: in({})({}{}{}{})   out:({})({}{}{}{})\n\t"
            "       type: in({})              out:({})",
            intent,
            in_space,
            (in_space>>24) & 0xff,(in_space>>16) & 0xff,
            (in_space>>8) & 0xff, in_space & 0xff,

            out_space,
            (out_space>>24) & 0xff,(out_space>>16) & 0xff,
            (out_space>>8) & 0xff, out_space & 0xff,

            in_type,out_type
           );
#else
	(void) prec;
	(void) in_space;
#endif /* DEBUG_PROFILE */

	transform = cmsCreateTransform(in_prof, in_type, out_prof, out_type, intent,
			0);

	cmsCloseProfile(in_prof);
	in_prof = nullptr;
	cmsCloseProfile(out_prof);
	out_prof = nullptr;

	if (transform == nullptr) {
#ifdef DEBUG_PROFILE
        spdlog::error("{}:{}:color_apply_icc_profile\n\tcmsCreateTransform failed. "
                "ICC Profile ignored.",__FILE__,__LINE__);
#endif
		image->color_space = oldspace;
		return;
	}
	if (image->numcomps > 2) { /* RGB, RGBA */
		if (prec <= 8) {
			uint8_t *in = nullptr, *inbuf = nullptr, *out = nullptr, *outbuf =
					nullptr;
			max = (size_t) max_w * max_h;
			nr_samples = max * 3 * (cmsUInt32Number) sizeof(uint8_t);
			in = inbuf = (uint8_t*) malloc(nr_samples);
			if (!in) {
				goto cleanup;
			}
			out = outbuf = (uint8_t*) malloc(nr_samples);
			if (!out) {
				free(inbuf);
				goto cleanup;
			}

			auto r = image->comps[0].data;
			auto g = image->comps[1].data;
			auto b = image->comps[2].data;

			for (uint32_t i = 0; i < max; ++i) {
				*in++ = (uint8_t) *r++;
				*in++ = (uint8_t) *g++;
				*in++ = (uint8_t) *b++;
			}

			cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number) max);

			r = image->comps[0].data;
			g = image->comps[1].data;
			b = image->comps[2].data;

			for (uint32_t i = 0; i < max; ++i) {
				*r++ = (int32_t) *out++;
				*g++ = (int32_t) *out++;
				*b++ = (int32_t) *out++;
			}
			free(inbuf);
			free(outbuf);
		} else {
			uint16_t *in = nullptr, *inbuf = nullptr, *out = nullptr,
					*outbuf = nullptr;
			max = max_w * max_h;
			nr_samples = max * 3 * (cmsUInt32Number) sizeof(uint16_t);
			in = inbuf = (uint16_t*) malloc(nr_samples);
			if (!in)
				goto cleanup;
			out = outbuf = (uint16_t*) malloc(nr_samples);
			if (!out) {
				free(inbuf);
				goto cleanup;
			}

			auto r = image->comps[0].data;
			auto g = image->comps[1].data;
			auto b = image->comps[2].data;

			for (uint32_t i = 0; i < max; ++i) {
				*in++ = (uint16_t) *r++;
				*in++ = (uint16_t) *g++;
				*in++ = (uint16_t) *b++;
			}

			cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number) max);

			r = image->comps[0].data;
			g = image->comps[1].data;
			b = image->comps[2].data;

			for (uint32_t i = 0; i < max; ++i) {
				*r++ = (int32_t) *out++;
				*g++ = (int32_t) *out++;
				*b++ = (int32_t) *out++;
			}
			free(inbuf);
			free(outbuf);
		}
	} else { /* GRAY, GRAYA */
		uint8_t *in = nullptr, *inbuf = nullptr, *out = nullptr, *outbuf =
				nullptr;

		max = max_w * max_h;
		nr_samples = max * 3 * (cmsUInt32Number) sizeof(uint8_t);
		grk_image_comp *comps = (grk_image_comp*) realloc(image->comps,
				(image->numcomps + 2) * sizeof(grk_image_comp));
		if (!comps)
			goto cleanup;
		image->comps = comps;

		in = inbuf = (uint8_t*) malloc(nr_samples);
		if (!in)
			goto cleanup;
		out = outbuf = (uint8_t*) malloc(nr_samples);
		if (!out) {
			free(inbuf);
			goto cleanup;
		}

		new_image = image_create(2, image->comps[0].w, image->comps[0].h,
				image->comps[0].prec);
		if (!new_image) {
			free(inbuf);
			free(outbuf);
			goto cleanup;
		}

		if (image->numcomps == 2)
			image->comps[3] = image->comps[1];

		image->comps[1] = image->comps[0];
		image->comps[2] = image->comps[0];

		image->comps[1].data = new_image->comps[0].data;
		image->comps[1].owns_data = true;
		image->comps[2].data = new_image->comps[1].data;
		image->comps[2].owns_data = true;

		new_image->comps[0].data = nullptr;
		new_image->comps[1].data = nullptr;

		grk_image_destroy(new_image);
		new_image = nullptr;

		if (forceRGB)
			image->numcomps += 2;

		auto r = image->comps[0].data;
		for (uint32_t i = 0; i < max; ++i) {
			*in++ = (uint8_t) *r++;
		}
		cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number) max);

		r = image->comps[0].data;
		auto g = image->comps[1].data;
		auto b = image->comps[2].data;

		for (uint32_t i = 0; i < max; ++i) {
			*r++ = (int32_t) *out++;
			if (forceRGB) {
				*g++ = (int32_t) *out++;
				*b++ = (int32_t) *out++;
			} else { //just skip green and blue channels
				out += 2;
			}
		}
		free(inbuf);
		free(outbuf);
	}/* if(image->numcomps */
	cleanup: if (in_prof)
		cmsCloseProfile(in_prof);
	if (out_prof)
		cmsCloseProfile(out_prof);
	if (transform)
		cmsDeleteTransform(transform);
}/* color_apply_icc_profile() */

// transform LAB colour space to sRGB @ 16 bit precision
bool color_cielab_to_rgb(grk_image *image) {
	// sanity checks
	if (image->numcomps == 0 || !grk::all_components_sanity_check(image))
		return false;
	uint64_t i;
	for (i = 1U; i < image->numcomps; ++i) {
		auto comp0 = image->comps;
		auto compi = image->comps + i;

		if (comp0->prec != compi->prec)
			break;
		if (comp0->sgnd != compi->sgnd)
			break;
	}
	if(i != image->numcomps){
		spdlog::warn("All components must have same precision and sign");
		return false;
	}

	auto row = (uint32_t*) image->icc_profile_buf;
	GRK_ENUM_COLOUR_SPACE enumcs = (GRK_ENUM_COLOUR_SPACE)row[0];
	if (enumcs != GRK_ENUM_CLRSPC_CIE) { /* CIELab */
		spdlog::warn("{}:{}:\n\tenumCS {} not handled. Ignoring.", __FILE__,
					__LINE__, enumcs);
		return false;
	}

	bool defaultType = true;
	image->color_space = GRK_CLRSPC_SRGB;
	uint32_t illuminant = GRK_CIE_D50;
	cmsCIExyY WhitePoint;
	defaultType = row[1] == GRK_DEFAULT_CIELAB_SPACE;
	int32_t *L, *a, *b, *red, *green, *blue;
	int32_t *src[3], *dst[3];
	// range, offset and precision for L,a and b coordinates
	double r_L, o_L, r_a, o_a, r_b, o_b, prec_L, prec_a, prec_b;
	double minL, maxL, mina, maxa, minb, maxb;
	uint64_t area = 0;;
	cmsUInt16Number RGB[3];
	auto new_image = image_create(3, image->comps[0].w, image->comps[0].h,
			image->comps[0].prec);
	if (!new_image) {
		return false;
	}
	prec_L = (double) image->comps[0].prec;
	prec_a = (double) image->comps[1].prec;
	prec_b = (double) image->comps[2].prec;

	if (defaultType) { // default Lab space
		r_L = 100;
		r_a = 170;
		r_b = 200;
		o_L = 0;
		o_a = pow(2, prec_a - 1);	 // 2 ^ (prec_b - 1)
		o_b = 3 * pow(2, prec_b - 3); // 0.75 * 2 ^ (prec_b - 1)
	} else {
		r_L = row[2];
		r_a = row[4];
		r_b = row[6];
		o_L = row[3];
		o_a = row[5];
		o_b = row[7];
		illuminant = row[8];
	}
	switch (illuminant) {
	case GRK_CIE_D50:
		break;
	case GRK_CIE_D65:
		cmsWhitePointFromTemp(&WhitePoint, 6504);
		break;
	case GRK_CIE_D75:
		cmsWhitePointFromTemp(&WhitePoint, 7500);
		break;
	case GRK_CIE_SA:
		cmsWhitePointFromTemp(&WhitePoint, 2856);
		break;
	case GRK_CIE_SC:
		cmsWhitePointFromTemp(&WhitePoint, 6774);
		break;
	case GRK_CIE_F2:
		cmsWhitePointFromTemp(&WhitePoint, 4100);
		break;
	case GRK_CIE_F7:
		cmsWhitePointFromTemp(&WhitePoint, 6500);
		break;
	case GRK_CIE_F11:
		cmsWhitePointFromTemp(&WhitePoint, 4000);
		break;
	default:
		spdlog::warn("Unrecognized illuminant {} in CIELab colour space. "
				"Setting to default Daylight50",
					illuminant);
		illuminant = GRK_CIE_D50;
		break;
	}

	// Lab input profile
	cmsHPROFILE in = cmsCreateLab4Profile(
			illuminant == GRK_CIE_D50 ? nullptr : &WhitePoint);
	// sRGB output profile
	cmsHPROFILE out = cmsCreate_sRGBProfile();
	cmsHTRANSFORM transform = cmsCreateTransform(in, TYPE_Lab_DBL, out,
			TYPE_RGB_16, INTENT_PERCEPTUAL, 0);

	cmsCloseProfile(in);
	cmsCloseProfile(out);
	if (transform == nullptr) {
		grk_image_destroy(new_image);
		return false;
	}

	L = src[0] = image->comps[0].data;
	a = src[1] = image->comps[1].data;
	b = src[2] = image->comps[2].data;

	area = (uint64_t)image->comps[0].w * image->comps[0].h;

	red = dst[0] = new_image->comps[0].data;
	green = dst[1] = new_image->comps[1].data;
	blue = dst[2] = new_image->comps[2].data;

	new_image->comps[0].data = nullptr;
	new_image->comps[1].data = nullptr;
	new_image->comps[2].data = nullptr;

	grk_image_destroy(new_image);
	new_image = nullptr;

	minL = -(r_L * o_L) / (pow(2, prec_L) - 1);
	maxL = minL + r_L;

	mina = -(r_a * o_a) / (pow(2, prec_a) - 1);
	maxa = mina + r_a;

	minb = -(r_b * o_b) / (pow(2, prec_b) - 1);
	maxb = minb + r_b;

	for (i = 0; i < area; ++i) {
		cmsCIELab Lab;
		Lab.L = minL + (double) (*L) * (maxL - minL) / (pow(2, prec_L) - 1);
		++L;
		Lab.a = mina + (double) (*a) * (maxa - mina) / (pow(2, prec_a) - 1);
		++a;
		Lab.b = minb + (double) (*b) * (maxb - minb) / (pow(2, prec_b) - 1);
		++b;

		cmsDoTransform(transform, &Lab, RGB, 1);

		*red++ = RGB[0];
		*green++ = RGB[1];
		*blue++ = RGB[2];
	}
	cmsDeleteTransform(transform);
	for (i = 0; i < 3; ++i){
		auto comp = image->comps + i;
		grk_image_single_component_data_free(comp);
		comp->data = dst[i];
		comp->owns_data = true;
		comp->prec = 16;
	}
	image->color_space = GRK_CLRSPC_SRGB;

	return true;
}/* color_cielab_to_rgb() */

#endif /* GROK_HAVE_LIBLCMS */

bool color_cmyk_to_rgb(grk_image *image) {
	uint32_t w = image->comps[0].w;
	uint32_t h = image->comps[0].h;

	if ((image->numcomps < 4) || !grk::all_components_sanity_check(image))
		return false;

	uint64_t area = (uint64_t) w * h;

	float sC = 1.0F / (float) ((1 << image->comps[0].prec) - 1);
	float sM = 1.0F / (float) ((1 << image->comps[1].prec) - 1);
	float sY = 1.0F / (float) ((1 << image->comps[2].prec) - 1);
	float sK = 1.0F / (float) ((1 << image->comps[3].prec) - 1);

	for (uint64_t i = 0; i < area; ++i) {
		/* CMYK values from 0 to 1 */
		float C = (float) (image->comps[0].data[i]) * sC;
		float M = (float) (image->comps[1].data[i]) * sM;
		float Y = (float) (image->comps[2].data[i]) * sY;
		float K = (float) (image->comps[3].data[i]) * sK;

		/* Invert all CMYK values */
		C = 1.0F - C;
		M = 1.0F - M;
		Y = 1.0F - Y;
		K = 1.0F - K;

		/* CMYK -> RGB : RGB results from 0 to 255 */
		image->comps[0].data[i] = (int32_t) (255.0F * C * K); /* R */
		image->comps[1].data[i] = (int32_t) (255.0F * M * K); /* G */
		image->comps[2].data[i] = (int32_t) (255.0F * Y * K); /* B */
	}

	grk_image_single_component_data_free(image->comps + 3);
	image->comps[0].prec = 8;
	image->comps[1].prec = 8;
	image->comps[2].prec = 8;
	image->numcomps -= 1;
	image->color_space = GRK_CLRSPC_SRGB;

	for (uint32_t i = 3; i < image->numcomps; ++i) {
		memcpy(&(image->comps[i]), &(image->comps[i + 1]),
				sizeof(image->comps[i]));
	}

	return true;

}/* color_cmyk_to_rgb() */

// assuming unsigned data !
bool color_esycc_to_rgb(grk_image *image) {
	int32_t flip_value = (1 << (image->comps[0].prec - 1));
	int32_t max_value = (1 << image->comps[0].prec) - 1;

	if ((image->numcomps < 3) || !grk::all_components_sanity_check(image))
		return false;

	uint32_t w = image->comps[0].w;
	uint32_t h = image->comps[0].h;

	int32_t sign1 = (int32_t) image->comps[1].sgnd;
	int32_t sign2 = (int32_t) image->comps[2].sgnd;

	uint64_t area = (uint64_t) w * h;

	for (uint64_t i = 0; i < area; ++i) {

		int32_t y = image->comps[0].data[i];
		int32_t cb = image->comps[1].data[i];
		int32_t cr = image->comps[2].data[i];

		if (!sign1)
			cb -= flip_value;
		if (!sign2)
			cr -= flip_value;

		int32_t val = (int32_t) (y - 0.0000368 * cb
				+ 1.40199 * cr +  0.5);

		if (val > max_value)
			val = max_value;
		else if (val < 0)
			val = 0;
		image->comps[0].data[i] = val;

		val = (int32_t) (1.0003 * y - 0.344125 * cb
				- 0.7141128 * cr + 0.5);

		if (val > max_value)
			val = max_value;
		else if (val < 0)
			val = 0;
		image->comps[1].data[i] = val;

		val = (int32_t) (0.999823 * y + 1.77204 * cb
				- 0.000008 * cr + 0.5);

		if (val > max_value)
			val = max_value;
		else if (val < 0)
			val = 0;
		image->comps[2].data[i] = val;
	}
	image->color_space = GRK_CLRSPC_SRGB;
	return true;

}/* color_esycc_to_rgb() */
