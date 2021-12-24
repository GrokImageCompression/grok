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

namespace grk
{
//#define DEBUG_PROFILE

static grk_image* create_rgb_no_subsample_image(uint16_t numcmpts, uint32_t w, uint32_t h,
												uint8_t prec)
{
	if(!numcmpts)
	{
		spdlog::warn("create_rgb_no_subsample_image: number of components cannot be zero.");
		return nullptr;
	}

	auto cmptparms = new grk_image_cmptparm[numcmpts];
	if(!cmptparms)
	{
		spdlog::warn("create_rgb_no_subsample_image: out of memory.");
		return nullptr;
	}
	uint32_t compno = 0U;
	for(compno = 0U; compno < numcmpts; ++compno)
	{
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
	auto img = grk_image_new(numcmpts, (grk_image_cmptparm*)cmptparms, GRK_CLRSPC_SRGB, true);
	delete[] cmptparms;

	return img;
}

#if defined(GROK_HAVE_LIBLCMS)

/*#define DEBUG_PROFILE*/
void color_apply_icc_profile(grk_image* image, bool forceRGB)
{
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
	grk_image* new_image = nullptr;
	if(image->numcomps == 0 || !grk::allComponentsSanityCheck(image, true))
		return;
	if(!image->meta)
		return;
	in_prof = cmsOpenProfileFromMem(image->meta->color.icc_profile_buf,
									image->meta->color.icc_profile_len);
#ifdef DEBUG_PROFILE
	FILE* icm = fopen("debug.icm", "wb");
	fwrite(image->color.icc_profile_buf, 1, image->color.icc_profile_len, icm);
	fclose(icm);
#endif

	if(in_prof == nullptr)
		return;

	in_space = cmsGetPCS(in_prof);
	out_space = cmsGetColorSpace(in_prof);
	intent = cmsGetHeaderRenderingIntent(in_prof);

	w = image->comps[0].w;
	stride_diff = image->comps[0].stride - w;
	h = image->comps[0].h;

	if(!w || !h)
		goto cleanup;

	prec = image->comps[0].prec;
	oldspace = image->color_space;

	if(out_space == cmsSigRgbData)
	{ /* enumCS 16 */
		uint32_t i, nr_comp = image->numcomps;

		if(nr_comp > 4)
		{
			nr_comp = 4;
		}
		for(i = 1; i < nr_comp; ++i)
		{
			if(image->comps[0].dx != image->comps[i].dx)
				break;
			if(image->comps[0].dy != image->comps[i].dy)
				break;
			if(image->comps[0].prec != image->comps[i].prec)
				break;
			if(image->comps[0].sgnd != image->comps[i].sgnd)
				break;
		}
		if(i != nr_comp)
			goto cleanup;

		if(prec <= 8)
		{
			in_type = TYPE_RGB_8;
			out_type = TYPE_RGB_8;
		}
		else
		{
			in_type = TYPE_RGB_16;
			out_type = TYPE_RGB_16;
		}
		out_prof = cmsCreate_sRGBProfile();
		image->color_space = GRK_CLRSPC_SRGB;
	}
	else if(out_space == cmsSigGrayData)
	{ /* enumCS 17 */
		in_type = TYPE_GRAY_8;
		out_type = TYPE_RGB_8;
		out_prof = cmsCreate_sRGBProfile();
		if(forceRGB)
			image->color_space = GRK_CLRSPC_SRGB;
		else
			image->color_space = GRK_CLRSPC_GRAY;
	}
	else if(out_space == cmsSigYCbCrData)
	{ /* enumCS 18 */
		in_type = TYPE_YCbCr_16;
		out_type = TYPE_RGB_16;
		out_prof = cmsCreate_sRGBProfile();
		image->color_space = GRK_CLRSPC_SRGB;
	}
	else
	{
#ifdef DEBUG_PROFILE
		spdlog::error("{}:{}: color_apply_icc_profile\n\tICC Profile has unknown "
					  "output colorspace(%#x)({}{}{}{})\n\tICC Profile ignored.",
					  __FILE__, __LINE__, out_space, (out_space >> 24) & 0xff,
					  (out_space >> 16) & 0xff, (out_space >> 8) & 0xff, out_space & 0xff);
#endif
		return;
	}

#ifdef DEBUG_PROFILE
	spdlog::error("{}:{}:color_apply_icc_profile\n\tchannels({}) prec({}) w({}) h({})"
				  "\n\tprofile: in({}) out({})",
				  __FILE__, __LINE__, image->numcomps, prec, max_w, max_h, (void*)in_prof,
				  (void*)out_prof);

	spdlog::error("\trender_intent ({})\n\t"
				  "color_space: in({})({}{}{}{})   out:({})({}{}{}{})\n\t"
				  "       type: in({})              out:({})",
				  intent, in_space, (in_space >> 24) & 0xff, (in_space >> 16) & 0xff,
				  (in_space >> 8) & 0xff, in_space & 0xff,

				  out_space, (out_space >> 24) & 0xff, (out_space >> 16) & 0xff,
				  (out_space >> 8) & 0xff, out_space & 0xff,

				  in_type, out_type);
#else
	(void)prec;
	(void)in_space;
#endif /* DEBUG_PROFILE */

	transform = cmsCreateTransform(in_prof, in_type, out_prof, out_type, intent, 0);

	cmsCloseProfile(in_prof);
	in_prof = nullptr;
	cmsCloseProfile(out_prof);
	out_prof = nullptr;

	if(transform == nullptr)
	{
#ifdef DEBUG_PROFILE
		spdlog::error("{}:{}:color_apply_icc_profile\n\tcmsCreateTransform failed. "
					  "ICC Profile ignored.",
					  __FILE__, __LINE__);
#endif
		image->color_space = oldspace;
		return;
	}
	if(image->numcomps > 2)
	{ /* RGB, RGBA */
		if(prec <= 8)
		{
			uint8_t *in = nullptr, *inbuf = nullptr, *out = nullptr, *outbuf = nullptr;
			max = (size_t)w * h;
			nr_samples = max * 3 * (cmsUInt32Number)sizeof(uint8_t);
			in = inbuf = (uint8_t*)malloc(nr_samples);
			if(!in)
			{
				goto cleanup;
			}
			out = outbuf = (uint8_t*)malloc(nr_samples);
			if(!out)
			{
				free(inbuf);
				goto cleanup;
			}

			auto r = image->comps[0].data;
			auto g = image->comps[1].data;
			auto b = image->comps[2].data;

			size_t src_index = 0;
			size_t dest_index = 0;
			for(uint32_t j = 0; j < h; ++j)
			{
				for(uint32_t i = 0; i < w; ++i)
				{
					in[dest_index++] = (uint8_t)r[src_index];
					in[dest_index++] = (uint8_t)g[src_index];
					in[dest_index++] = (uint8_t)b[src_index];
					src_index++;
				}
				src_index += stride_diff;
			}

			cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number)max);

			src_index = 0;
			dest_index = 0;
			for(uint32_t j = 0; j < h; ++j)
			{
				for(uint32_t i = 0; i < w; ++i)
				{
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
		else
		{
			uint16_t *in = nullptr, *inbuf = nullptr, *out = nullptr, *outbuf = nullptr;
			max = (size_t)w * h;
			nr_samples = max * 3 * (cmsUInt32Number)sizeof(uint16_t);
			in = inbuf = (uint16_t*)malloc(nr_samples);
			if(!in)
				goto cleanup;
			out = outbuf = (uint16_t*)malloc(nr_samples);
			if(!out)
			{
				free(inbuf);
				goto cleanup;
			}
			auto r = image->comps[0].data;
			auto g = image->comps[1].data;
			auto b = image->comps[2].data;

			size_t src_index = 0;
			size_t dest_index = 0;
			for(uint32_t j = 0; j < h; ++j)
			{
				for(uint32_t i = 0; i < w; ++i)
				{
					in[dest_index++] = (uint16_t)r[src_index];
					in[dest_index++] = (uint16_t)g[src_index];
					in[dest_index++] = (uint16_t)b[src_index];
					src_index++;
				}
				src_index += stride_diff;
			}

			cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number)max);

			src_index = 0;
			dest_index = 0;
			for(uint32_t j = 0; j < h; ++j)
			{
				for(uint32_t i = 0; i < w; ++i)
				{
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
	}
	else
	{ /* GRAY, GRAYA */
		uint8_t *in = nullptr, *inbuf = nullptr, *out = nullptr, *outbuf = nullptr;

		max = (size_t)w * h;
		nr_samples = max * 3 * (cmsUInt32Number)sizeof(uint8_t);
		auto newComps = new grk_image_comp[image->numcomps + 2U];
		for(uint32_t i = 0; i < image->numcomps + 2U; ++i)
		{
			if(i < image->numcomps)
				newComps[i] = image->comps[i];
			else
				memset(newComps + 1, 0, sizeof(grk_image_comp));
		}
		delete[] image->comps;
		image->comps = newComps;

		in = inbuf = (uint8_t*)malloc(nr_samples);
		if(!in)
			goto cleanup;
		out = outbuf = (uint8_t*)malloc(nr_samples);
		if(!out)
		{
			free(inbuf);
			goto cleanup;
		}

		new_image = create_rgb_no_subsample_image(2, image->comps[0].w, image->comps[0].h,
												  image->comps[0].prec);
		if(!new_image)
		{
			free(inbuf);
			free(outbuf);
			goto cleanup;
		}

		if(image->numcomps == 2)
			image->comps[3] = image->comps[1];

		image->comps[1] = image->comps[0];
		image->comps[2] = image->comps[0];

		image->comps[1].data = new_image->comps[0].data;
		image->comps[2].data = new_image->comps[1].data;

		new_image->comps[0].data = nullptr;
		new_image->comps[1].data = nullptr;

		grk_object_unref(&new_image->obj);
		new_image = nullptr;

		if(forceRGB)
			image->numcomps = (uint16_t)(2 + image->numcomps);

		auto r = image->comps[0].data;

		size_t src_index = 0;
		size_t dest_index = 0;
		for(uint32_t j = 0; j < h; ++j)
		{
			for(uint32_t i = 0; i < w; ++i)
			{
				in[dest_index++] = (uint8_t)r[src_index];
				src_index++;
			}
			src_index += stride_diff;
		}

		cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number)max);

		auto g = image->comps[1].data;
		auto b = image->comps[2].data;

		src_index = 0;
		dest_index = 0;
		for(uint32_t j = 0; j < h; ++j)
		{
			for(uint32_t i = 0; i < w; ++i)
			{
				r[dest_index] = (int32_t)out[src_index++];
				if(forceRGB)
				{
					g[dest_index] = (int32_t)out[src_index++];
					b[dest_index] = (int32_t)out[src_index++];
				}
				else
				{
					src_index += 2;
				}
				dest_index++;
			}
			dest_index += stride_diff;
		}

		free(inbuf);
		free(outbuf);
	} /* if(image->numcomps */
cleanup:
	if(in_prof)
		cmsCloseProfile(in_prof);
	if(out_prof)
		cmsCloseProfile(out_prof);
	if(transform)
		cmsDeleteTransform(transform);
} /* color_apply_icc_profile() */

// transform LAB colour space to sRGB @ 16 bit precision
bool color_cielab_to_rgb(grk_image* src_img)
{
	// sanity checks
	if(src_img->numcomps == 0 || !grk::allComponentsSanityCheck(src_img, true))
		return false;
	if(!src_img->meta)
		return false;
	size_t i;
	for(i = 1U; i < src_img->numcomps; ++i)
	{
		auto comp0 = src_img->comps;
		auto compi = src_img->comps + i;

		if(comp0->prec != compi->prec)
			break;
		if(comp0->sgnd != compi->sgnd)
			break;
		if(comp0->stride != compi->stride)
			break;
	}
	if(i != src_img->numcomps)
	{
		spdlog::warn("All components must have same precision, sign and stride");
		return false;
	}

	auto row = (uint32_t*)src_img->meta->color.icc_profile_buf;
	auto enumcs = (GRK_ENUM_COLOUR_SPACE)row[0];
	if(enumcs != GRK_ENUM_CLRSPC_CIE)
	{ /* CIELab */
		spdlog::warn("{}:{}:\n\tenumCS {} not handled. Ignoring.", __FILE__, __LINE__, enumcs);
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
	if(!dest_img)
		return false;

	prec_L = (double)src_img->comps[0].prec;
	prec_a = (double)src_img->comps[1].prec;
	prec_b = (double)src_img->comps[2].prec;

	if(defaultType)
	{ // default Lab space
		r_L = 100;
		r_a = 170;
		r_b = 200;
		o_L = 0;
		o_a = pow(2, prec_a - 1); // 2 ^ (prec_b - 1)
		o_b = 3 * pow(2, prec_b - 3); // 0.75 * 2 ^ (prec_b - 1)
	}
	else
	{
		r_L = row[2];
		r_a = row[4];
		r_b = row[6];
		o_L = row[3];
		o_a = row[5];
		o_b = row[7];
		illuminant = row[8];
	}
	switch(illuminant)
	{
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
	auto in = cmsCreateLab4Profile(illuminant == GRK_CIE_D50 ? nullptr : &WhitePoint);
	// sRGB output profile
	auto out = cmsCreate_sRGBProfile();
	auto transform =
		cmsCreateTransform(in, TYPE_Lab_DBL, out, TYPE_RGB_16, INTENT_PERCEPTUAL, 0);

	cmsCloseProfile(in);
	cmsCloseProfile(out);
	if(transform == nullptr)
	{
		grk_object_unref(&dest_img->obj);
		return false;
	}

	L = src[0] = src_img->comps[0].data;
	a = src[1] = src_img->comps[1].data;
	b = src[2] = src_img->comps[2].data;

	if(!L || !a || !b)
	{
		spdlog::warn("color_cielab_to_rgb: null L*a*b component");
		return false;
	}

	red = dst[0] = dest_img->comps[0].data;
	green = dst[1] = dest_img->comps[1].data;
	blue = dst[2] = dest_img->comps[2].data;

	dest_img->comps[0].data = nullptr;
	dest_img->comps[1].data = nullptr;
	dest_img->comps[2].data = nullptr;

	grk_object_unref(&dest_img->obj);
	dest_img = nullptr;

	minL = -(r_L * o_L) / (pow(2, prec_L) - 1);
	maxL = minL + r_L;

	mina = -(r_a * o_a) / (pow(2, prec_a) - 1);
	maxa = mina + r_a;

	minb = -(r_b * o_b) / (pow(2, prec_b) - 1);
	maxb = minb + r_b;

	uint32_t stride_diff = src_img->comps[0].stride - src_img->comps[0].w;
	size_t dest_index = 0;
	for(uint32_t j = 0; j < src_img->comps[0].h; ++j)
	{
		for(uint32_t k = 0; k < src_img->comps[0].w; ++k)
		{
			cmsCIELab Lab;
			Lab.L = minL + (double)(*L) * (maxL - minL) / (pow(2, prec_L) - 1);
			++L;
			Lab.a = mina + (double)(*a) * (maxa - mina) / (pow(2, prec_a) - 1);
			++a;
			Lab.b = minb + (double)(*b) * (maxb - minb) / (pow(2, prec_b) - 1);
			++b;

			cmsDoTransform(transform, &Lab, RGB, 1);

			red[dest_index] = RGB[0];
			green[dest_index] = RGB[1];
			blue[dest_index] = RGB[2];
			dest_index++;
		}
		dest_index += stride_diff;
		L += stride_diff;
		a += stride_diff;
		b += stride_diff;
	}
	cmsDeleteTransform(transform);
	for(i = 0; i < 3; ++i)
	{
		auto comp = src_img->comps + i;
		grk_image_single_component_data_free(comp);
		comp->data = dst[i];
		comp->prec = 16;
	}
	src_img->color_space = GRK_CLRSPC_SRGB;

	return true;
} /* color_cielab_to_rgb() */

#endif /* GROK_HAVE_LIBLCMS */

void alloc_palette(grk_color* color, uint8_t num_channels, uint16_t num_entries)
{
	assert(color);
	assert(num_channels);
	assert(num_entries);

	auto jp2_pclr = new grk_palette_data();
	jp2_pclr->channel_sign = new bool[num_channels];
	jp2_pclr->channel_prec = new uint8_t[num_channels];
	jp2_pclr->lut = new int32_t[num_channels * num_entries];
	jp2_pclr->num_entries = num_entries;
	jp2_pclr->num_channels = num_channels;
	jp2_pclr->component_mapping = nullptr;
	color->palette = jp2_pclr;
}

void copy_icc(grk_image* dest, uint8_t* iccbuf, uint32_t icclen)
{
	create_meta(dest);
	dest->meta->color.icc_profile_buf = new uint8_t[icclen];
	memcpy(dest->meta->color.icc_profile_buf, iccbuf, icclen);
	dest->meta->color.icc_profile_len = icclen;
	dest->color_space = GRK_CLRSPC_ICC;
}
void create_meta(grk_image* img)
{
	if(img && !img->meta)
		img->meta = grk_image_meta_new();
}

bool validate_icc(GRK_COLOR_SPACE colourSpace, uint8_t* iccbuf, uint32_t icclen)
{
	bool rc = true;
#ifdef GROK_HAVE_LIBLCMS
	auto in_prof = cmsOpenProfileFromMem(iccbuf, icclen);
	if(in_prof)
	{
		auto cmsColorSpaceSignature = cmsGetColorSpace(in_prof);
		switch(cmsColorSpaceSignature)
		{
			case cmsSigLabData:
				rc =
					(colourSpace == GRK_CLRSPC_DEFAULT_CIE || colourSpace == GRK_CLRSPC_CUSTOM_CIE);
				break;
			case cmsSigYCbCrData:
				rc = (colourSpace == GRK_CLRSPC_SYCC || colourSpace == GRK_CLRSPC_EYCC);
				break;
			case cmsSigRgbData:
				rc = colourSpace == GRK_CLRSPC_SRGB;
				break;
			case cmsSigGrayData:
				rc = colourSpace == GRK_CLRSPC_GRAY;
				break;
			case cmsSigCmykData:
				rc = colourSpace == GRK_CLRSPC_CMYK;
				break;
			default:
				rc = false;
				break;
		}
		cmsCloseProfile(in_prof);
	}
#else
	GRK_UNUSED(colourSpace);
	GRK_UNUSED(iccbuf);
	GRK_UNUSED(icclen);
#endif
	return rc;
}

} // namespace grk
