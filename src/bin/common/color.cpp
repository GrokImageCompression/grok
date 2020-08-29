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

static grk_image* create_rgb_no_subsample_image(uint32_t numcmpts, uint32_t w, uint32_t h,
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
			GRK_CLRSPC_SRGB,true);
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

static bool sycc444_to_rgb(grk_image *src_img) {
	int32_t *d0, *d1, *d2, *r, *g, *b;
	auto dest_img = create_rgb_no_subsample_image(3, src_img->comps[0].w, src_img->comps[0].h,
			src_img->comps[0].prec);
	if (!dest_img)
		return false;

	int32_t upb = (int32_t) src_img->comps[0].prec;
	int32_t offset = 1 << (upb - 1);
	upb = (1 << upb) - 1;

	uint32_t w = src_img->comps[0].w;
	uint32_t stride_diff = src_img->comps[0].stride - w;
	uint32_t h =  src_img->comps[0].h;

	auto y = src_img->comps[0].data;
	auto cb = src_img->comps[1].data;
	auto cr = src_img->comps[2].data;

	d0 = r = dest_img->comps[0].data;
	d1 = g = dest_img->comps[1].data;
	d2 = b = dest_img->comps[2].data;

	dest_img->comps[0].data = nullptr;
	dest_img->comps[1].data = nullptr;
	dest_img->comps[2].data = nullptr;

	for (uint32_t j = 0; j < h; ++j){
		for (uint32_t i = 0; i < w; ++i)
			sycc_to_rgb(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);
		y  += stride_diff;
		cb += stride_diff;
		cr += stride_diff;
		r  += stride_diff;
		g  += stride_diff;
		b  += stride_diff;
	}

	grk_image_all_components_data_free(src_img);
	src_img->comps[0].data = d0;
	src_img->comps[0].owns_data = true;
	src_img->comps[1].data = d1;
	src_img->comps[1].owns_data = true;
	src_img->comps[2].data = d2;
	src_img->comps[2].owns_data = true;
	src_img->color_space = GRK_CLRSPC_SRGB;

	for (uint32_t i = 0; i < src_img->numcomps; ++i)
		src_img->comps[i].stride = dest_img->comps[i].stride;
	grk_image_destroy(dest_img);
	dest_img = nullptr;

	return true;
}/* sycc444_to_rgb() */

static bool sycc422_to_rgb(grk_image *src_img, bool oddFirstX) {
	auto dest_img = create_rgb_no_subsample_image(3, src_img->comps[0].w, src_img->comps[0].h,
			src_img->comps[0].prec);
	if (!dest_img)
		return false;

	uint32_t upb =  src_img->comps[0].prec;
	uint32_t offset = 1 << (upb - 1);
	upb = (1 << upb) - 1;

	uint32_t w = src_img->comps[0].w;
	uint32_t stride_diff = src_img->comps[0].stride - w;
	uint32_t stride_diff_chroma = src_img->comps[1].stride - src_img->comps[1].w;
	uint32_t h = src_img->comps[0].h;

	int32_t *d0, *d1, *d2, *r, *g, *b;

	auto y = src_img->comps[0].data;
	auto cb = src_img->comps[1].data;
	auto cr = src_img->comps[2].data;

	d0 = r = dest_img->comps[0].data;
	d1 = g = dest_img->comps[1].data;
	d2 = b = dest_img->comps[2].data;

	dest_img->comps[0].data = nullptr;
	dest_img->comps[1].data = nullptr;
	dest_img->comps[2].data = nullptr;

	/* if img->x0 is odd, then first column shall use Cb/Cr = 0 */
	uint32_t loopmaxw = w;
	if (oddFirstX)
		loopmaxw--;

	for (uint32_t i = 0U; i < h; ++i) {
		if (oddFirstX)
			sycc_to_rgb(offset, upb, *y++, 0, 0, r++, g++, b++);

		uint32_t j;
		for (j = 0U; j < (loopmaxw & ~(size_t) 1U); j += 2U) {
			sycc_to_rgb(offset, upb, *y++, *cb, *cr, r++, g++, b++);
			sycc_to_rgb(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);
		}
		if (j < loopmaxw)
			sycc_to_rgb(offset, upb, *y++, *cb++, *cr++, r++, g++, b++);

		y  += stride_diff;
		cb += stride_diff_chroma;
		cr += stride_diff_chroma;
		r  += stride_diff;
		g  += stride_diff;
		b  += stride_diff;
	}
	grk_image_all_components_data_free(src_img);

	src_img->comps[0].data = d0;
	src_img->comps[0].owns_data = true;
	src_img->comps[1].data = d1;
	src_img->comps[1].owns_data = true;
	src_img->comps[2].data = d2;
	src_img->comps[2].owns_data = true;

	src_img->comps[1].w = src_img->comps[2].w = src_img->comps[0].w;
	src_img->comps[1].h = src_img->comps[2].h = src_img->comps[0].h;
	src_img->comps[1].dx = src_img->comps[2].dx = src_img->comps[0].dx;
	src_img->comps[1].dy = src_img->comps[2].dy = src_img->comps[0].dy;
	src_img->color_space = GRK_CLRSPC_SRGB;

	for (uint32_t i = 0; i < src_img->numcomps; ++i)
		src_img->comps[i].stride = dest_img->comps[i].stride;
	grk_image_destroy(dest_img);
	dest_img = nullptr;

	return true;

}/* sycc422_to_rgb() */

static bool sycc420_to_rgb(grk_image *src_img, bool oddFirstX, bool oddFirstY) {
	auto dest_img = create_rgb_no_subsample_image(3, src_img->comps[0].w, src_img->comps[0].h,
			src_img->comps[0].prec);
	if (!dest_img)
		return false;

	uint32_t upb = src_img->comps[0].prec;
	uint32_t offset = 1 << (upb - 1);
	upb = (1 << upb) - 1;

	uint32_t w = src_img->comps[0].w;
	uint32_t h = src_img->comps[0].h;


	int32_t *src[3];
	int32_t *dest[3];
	int32_t *dest_ptr[3];
	uint32_t stride_src[3];
	uint32_t stride_src_diff[3];

	uint32_t stride_dest = dest_img->comps[0].stride;
	uint32_t stride_dest_diff = dest_img->comps[0].stride -  dest_img->comps[0].w;


	for (uint32_t i = 0; i < 3; ++i){
		auto src_comp = src_img->comps + i;
		src[i] = src_comp->data;
		stride_src[i] = src_comp->stride;
		stride_src_diff[i] = src_comp->stride -  src_comp->w;

		dest[i] = dest_ptr[i] = dest_img->comps[i].data;
		dest_img->comps[i].data = nullptr;
	}

	uint32_t loopmaxw = w;
	uint32_t loopmaxh = h;

	/* if img->x0 is odd, then first column shall use Cb/Cr = 0 */
	if (oddFirstX)
		loopmaxw--;
	/* if img->y0 is odd, then first line shall use Cb/Cr = 0 */
	if (oddFirstY)
		loopmaxh--;

	if (oddFirstX) {
		for (size_t j = 0U; j < w; ++j)
			sycc_to_rgb(offset, upb, *src[0]++, 0, 0, dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
		src[0]  += stride_src_diff[0];
		for (uint32_t i = 0; i < 3; ++i)
			dest_ptr[i]  += stride_dest_diff;
	}
	size_t i;
	for (i = 0U; i < (loopmaxh & ~(size_t) 1U); i += 2U) {
		auto ny = src[0] +  stride_src[0];
		auto nr = dest_ptr[0] + stride_dest;
		auto ng = dest_ptr[1] + stride_dest;
		auto nb = dest_ptr[2] + stride_dest;

		if (oddFirstY) {
			sycc_to_rgb(offset, upb, *src[0]++, 0, 0, dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
			sycc_to_rgb(offset, upb, *ny++, *src[1], *src[2], nr++, ng++, nb++);
		}
		uint32_t j;
		for (j = 0U; j < (loopmaxw & ~(size_t) 1U); j += 2U) {
			sycc_to_rgb(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
			sycc_to_rgb(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
			sycc_to_rgb(offset, upb, *ny++, *src[1], *src[2], nr++, ng++, nb++);
			sycc_to_rgb(offset, upb, *ny++, *src[1]++, *src[2]++, nr++, ng++, nb++);
		}
		if (j < loopmaxw) {
			sycc_to_rgb(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
			sycc_to_rgb(offset, upb, *ny++, *src[1]++, *src[2]++, nr++, ng++, nb++);
		}
		src[0] += stride_src_diff[0] + stride_src[0];
		src[1] += stride_src_diff[1];
		src[2] += stride_src_diff[2];
		for (uint32_t k = 0; k < 3; ++k)
			dest_ptr[k]  += stride_dest_diff + stride_dest;
	}
	// last row has no sub-sampling
	if (i < loopmaxh) {
		uint32_t j;
		for (j = 0U; j < (w & ~(size_t) 1U); j += 2U) {
			sycc_to_rgb(offset, upb, *src[0]++, *src[1], *src[2], dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
			sycc_to_rgb(offset, upb, *src[0]++, *src[1]++, *src[2]++, dest_ptr[0]++, dest_ptr[1]++, dest_ptr[2]++);
		}
		if (j < w)
			sycc_to_rgb(offset, upb, *src[0], *src[1], *src[2], dest_ptr[0], dest_ptr[1], dest_ptr[2]);
	}

	grk_image_all_components_data_free(src_img);
	for (uint32_t k = 0; k < 3; ++k){
		src_img->comps[k].data = dest[k];
		src_img->comps[k].owns_data = true;
		src_img->comps[k].stride = dest_img->comps[k].stride;
	}

	src_img->comps[1].w = src_img->comps[2].w = src_img->comps[0].w;
	src_img->comps[1].h = src_img->comps[2].h = src_img->comps[0].h;
	src_img->comps[1].dx = src_img->comps[2].dx = src_img->comps[0].dx;
	src_img->comps[1].dy = src_img->comps[2].dy = src_img->comps[0].dy;
	src_img->color_space = GRK_CLRSPC_SRGB;

	grk_image_destroy(dest_img);
	dest_img = nullptr;

	return true;

}/* sycc420_to_rgb() */

bool color_sycc_to_rgb(grk_image *img, bool oddFirstX, bool oddFirstY) {
	if (img->numcomps < 3) {
		spdlog::warn("color_sycc_to_rgb: number of components {} is less than 3."
						" Unable to convert", img->numcomps);
		return false;
	}
	bool rc;

	if ((img->comps[0].dx == 1) && (img->comps[1].dx == 2)
			&& (img->comps[2].dx == 2) && (img->comps[0].dy == 1)
			&& (img->comps[1].dy == 2) && (img->comps[2].dy == 2)) { /* horizontal and vertical sub-sample */
		rc = sycc420_to_rgb(img,oddFirstX, oddFirstY);
	} else if ((img->comps[0].dx == 1) && (img->comps[1].dx == 2)
			&& (img->comps[2].dx == 2) && (img->comps[0].dy == 1)
			&& (img->comps[1].dy == 1) && (img->comps[2].dy == 1)) { /* horizontal sub-sample only */
		rc = sycc422_to_rgb(img, oddFirstX);
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
	uint32_t prec, w, stride_diff, h;
	GRK_COLOR_SPACE oldspace;
	grk_image *new_image = nullptr;
	if (image->numcomps == 0 || !grk::all_components_sanity_check(image,true))
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

	w = image->comps[0].w;
	stride_diff = image->comps[0].stride - w;
	h = image->comps[0].h;

	if (!w || !h)
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
			max = (size_t) w * h;
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

			size_t src_index = 0;
			size_t dest_index = 0;
			for (uint32_t j = 0; j < h; ++j){
				for (uint32_t i = 0; i < w; ++i){
					in[dest_index++] = (uint8_t)r[src_index];
					in[dest_index++] = (uint8_t)g[src_index];
					in[dest_index++] = (uint8_t)b[src_index];
					src_index++;
				}
				src_index += stride_diff;
			}

			cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number) max);

			src_index = 0;
			dest_index = 0;
			for (uint32_t j = 0; j < h; ++j){
				for (uint32_t i = 0; i < w; ++i){
					r[dest_index] = (int32_t)out[src_index++];
					g[dest_index] = (int32_t)out[src_index++];
					b[dest_index] = (int32_t)out[src_index++];
					dest_index++;
				}
				dest_index += stride_diff;
			}
			free(inbuf);
			free(outbuf);
		} else {
			uint16_t *in = nullptr, *inbuf = nullptr, *out = nullptr,
					*outbuf = nullptr;
			max = (size_t)w * h;
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

			size_t src_index = 0;
			size_t dest_index = 0;
			for (uint32_t j = 0; j < h; ++j){
				for (uint32_t i = 0; i < w; ++i){
					in[dest_index++] = (uint16_t)r[src_index];
					in[dest_index++] = (uint16_t)g[src_index];
					in[dest_index++] = (uint16_t)b[src_index];
					src_index++;
				}
				src_index += stride_diff;
			}

			cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number) max);

			src_index = 0;
			dest_index = 0;
			for (uint32_t j = 0; j < h; ++j){
				for (uint32_t i = 0; i < w; ++i){
					r[dest_index] = (int32_t)out[src_index++];
					g[dest_index] = (int32_t)out[src_index++];
					b[dest_index] = (int32_t)out[src_index++];
					dest_index++;
				}
				dest_index += stride_diff;
			}
			free(inbuf);
			free(outbuf);
		}
	} else { /* GRAY, GRAYA */
		uint8_t *in = nullptr, *inbuf = nullptr, *out = nullptr, *outbuf =
				nullptr;

		max = (size_t)w * h;
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

		new_image = create_rgb_no_subsample_image(2, image->comps[0].w, image->comps[0].h,
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

		size_t src_index = 0;
		size_t dest_index = 0;
		for (uint32_t j = 0; j < h; ++j){
			for (uint32_t i = 0; i < w; ++i){
				in[dest_index++] = (uint8_t)r[src_index];
				src_index++;
			}
			src_index += stride_diff;
		}

		cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number) max);

		auto g = image->comps[1].data;
		auto b = image->comps[2].data;

		src_index = 0;
		dest_index = 0;
		for (uint32_t j = 0; j < h; ++j){
			for (uint32_t i = 0; i < w; ++i){
				r[dest_index] = (int32_t)out[src_index++];
				if (forceRGB) {
					g[dest_index] = (int32_t)out[src_index++];
					b[dest_index] = (int32_t)out[src_index++];
				} else {
					src_index+=2;
				}
				dest_index++;
			}
			dest_index += stride_diff;
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
bool color_cielab_to_rgb(grk_image *src_img) {
	// sanity checks
	if (src_img->numcomps == 0 || !grk::all_components_sanity_check(src_img,true))
		return false;
	size_t i;
	for (i = 1U; i < src_img->numcomps; ++i) {
		auto comp0 = src_img->comps;
		auto compi = src_img->comps + i;

		if (comp0->prec != compi->prec)
			break;
		if (comp0->sgnd != compi->sgnd)
			break;
		if (comp0->stride != compi->stride)
			break;
	}
	if(i != src_img->numcomps){
		spdlog::warn("All components must have same precision, sign and stride");
		return false;
	}

	auto row = (uint32_t*) src_img->icc_profile_buf;
	GRK_ENUM_COLOUR_SPACE enumcs = (GRK_ENUM_COLOUR_SPACE)row[0];
	if (enumcs != GRK_ENUM_CLRSPC_CIE) { /* CIELab */
		spdlog::warn("{}:{}:\n\tenumCS {} not handled. Ignoring.", __FILE__,
					__LINE__, enumcs);
		return false;
	}

	bool defaultType = true;
	src_img->color_space = GRK_CLRSPC_SRGB;
	uint32_t illuminant = GRK_CIE_D50;
	cmsCIExyY WhitePoint;
	defaultType = row[1] == GRK_DEFAULT_CIELAB_SPACE;
	int32_t *L, *a, *b, *red, *green, *blue;
	int32_t *src[3], *dst[3];
	// range, offset and precision for L,a and b coordinates
	double r_L, o_L, r_a, o_a, r_b, o_b, prec_L, prec_a, prec_b;
	double minL, maxL, mina, maxa, minb, maxb;
	cmsUInt16Number RGB[3];
	auto dest_img = create_rgb_no_subsample_image(3, src_img->comps[0].w, src_img->comps[0].h,
			src_img->comps[0].prec);
	if (!dest_img)
		return false;

	prec_L = (double) src_img->comps[0].prec;
	prec_a = (double) src_img->comps[1].prec;
	prec_b = (double) src_img->comps[2].prec;

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
		grk_image_destroy(dest_img);
		return false;
	}

	L = src[0] = src_img->comps[0].data;
	a = src[1] = src_img->comps[1].data;
	b = src[2] = src_img->comps[2].data;

	red = dst[0] = dest_img->comps[0].data;
	green = dst[1] = dest_img->comps[1].data;
	blue = dst[2] = dest_img->comps[2].data;

	dest_img->comps[0].data = nullptr;
	dest_img->comps[1].data = nullptr;
	dest_img->comps[2].data = nullptr;

	grk_image_destroy(dest_img);
	dest_img = nullptr;

	minL = -(r_L * o_L) / (pow(2, prec_L) - 1);
	maxL = minL + r_L;

	mina = -(r_a * o_a) / (pow(2, prec_a) - 1);
	maxa = mina + r_a;

	minb = -(r_b * o_b) / (pow(2, prec_b) - 1);
	maxb = minb + r_b;


	uint32_t stride_diff = src_img->comps[0].stride - src_img->comps[0].w;
	size_t dest_index = 0;
	for (uint32_t j = 0; j < src_img->comps[0].h; ++j){
		for (uint32_t k = 0; k < src_img->comps[0].w; ++k){
			cmsCIELab Lab;
			Lab.L = minL + (double) (*L) * (maxL - minL) / (pow(2, prec_L) - 1);
			++L;
			Lab.a = mina + (double) (*a) * (maxa - mina) / (pow(2, prec_a) - 1);
			++a;
			Lab.b = minb + (double) (*b) * (maxb - minb) / (pow(2, prec_b) - 1);
			++b;

			cmsDoTransform(transform, &Lab, RGB, 1);

			red[dest_index] 	= RGB[0];
			green[dest_index] 	= RGB[1];
			blue[dest_index] 	= RGB[2];
			dest_index++;
		}
		dest_index += stride_diff;
		L += stride_diff;
		a += stride_diff;
		b += stride_diff;
	}
	cmsDeleteTransform(transform);
	for (i = 0; i < 3; ++i){
		auto comp = src_img->comps + i;
		grk_image_single_component_data_free(comp);
		comp->data = dst[i];
		comp->owns_data = true;
		comp->prec = 16;
	}
	src_img->color_space = GRK_CLRSPC_SRGB;

	return true;
}/* color_cielab_to_rgb() */

#endif /* GROK_HAVE_LIBLCMS */

bool color_cmyk_to_rgb(grk_image *image) {
	uint32_t w = image->comps[0].w;
	uint32_t h = image->comps[0].h;

	if ((image->numcomps < 4) || !grk::all_components_sanity_check(image,true))
		return false;

	float sC = 1.0F / (float) ((1 << image->comps[0].prec) - 1);
	float sM = 1.0F / (float) ((1 << image->comps[1].prec) - 1);
	float sY = 1.0F / (float) ((1 << image->comps[2].prec) - 1);
	float sK = 1.0F / (float) ((1 << image->comps[3].prec) - 1);

	uint32_t stride_diff = image->comps[0].stride - w;
	size_t dest_index = 0;
	for (uint32_t j = 0; j < h; ++j){
		for (uint32_t i = 0; i < w; ++i){
			/* CMYK values from 0 to 1 */
			float C = (float) (image->comps[0].data[dest_index]) * sC;
			float M = (float) (image->comps[1].data[dest_index]) * sM;
			float Y = (float) (image->comps[2].data[dest_index]) * sY;
			float K = (float) (image->comps[3].data[dest_index]) * sK;

			/* Invert all CMYK values */
			C = 1.0F - C;
			M = 1.0F - M;
			Y = 1.0F - Y;
			K = 1.0F - K;

			/* CMYK -> RGB : RGB results from 0 to 255 */
			image->comps[0].data[dest_index] = (int32_t) (255.0F * C * K); /* R */
			image->comps[1].data[dest_index] = (int32_t) (255.0F * M * K); /* G */
			image->comps[2].data[dest_index] = (int32_t) (255.0F * Y * K); /* B */
			dest_index++;
		}
		dest_index += stride_diff;
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

	if ((image->numcomps < 3) || !grk::all_components_sanity_check(image,true))
		return false;

	uint32_t w = image->comps[0].w;
	uint32_t h = image->comps[0].h;

	bool sign1 = image->comps[1].sgnd;
	bool sign2 = image->comps[2].sgnd;

	uint32_t stride_diff = image->comps[0].stride - w;
	size_t dest_index = 0;
	for (uint32_t j = 0; j < h; ++j){
		for (uint32_t i = 0; i < w; ++i){

			int32_t y = image->comps[0].data[dest_index];
			int32_t cb = image->comps[1].data[dest_index];
			int32_t cr = image->comps[2].data[dest_index];

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
			image->comps[0].data[dest_index] = val;

			val = (int32_t) (1.0003 * y - 0.344125 * cb
					- 0.7141128 * cr + 0.5);

			if (val > max_value)
				val = max_value;
			else if (val < 0)
				val = 0;
			image->comps[1].data[dest_index] = val;

			val = (int32_t) (0.999823 * y + 1.77204 * cb
					- 0.000008 * cr + 0.5);

			if (val > max_value)
				val = max_value;
			else if (val < 0)
				val = 0;
			image->comps[2].data[dest_index] = val;
			dest_index++;
		}
		dest_index += stride_diff;
	}
	image->color_space = GRK_CLRSPC_SRGB;
	return true;

}/* color_esycc_to_rgb() */
