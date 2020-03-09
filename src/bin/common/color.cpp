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

static grk_image *   image_create(uint32_t numcmpts, uint32_t w, uint32_t h, uint32_t prec)
{
	if (!numcmpts)
		return nullptr;

     grk_image_cmptparm  *  cmptparms = ( grk_image_cmptparm  * )calloc(numcmpts, sizeof( grk_image_cmptparm) );
	if (!cmptparms)
		return nullptr;
    grk_image *  img = nullptr;
    uint32_t compno=0U;
    for (compno = 0U; compno < numcmpts; ++compno) {
        memset(cmptparms + compno, 0, sizeof( grk_image_cmptparm) );
        cmptparms[compno].dx = 1;
        cmptparms[compno].dy = 1;
        cmptparms[compno].w = w;
        cmptparms[compno].h = h;
        cmptparms[compno].x0 = 0U;
        cmptparms[compno].y0 = 0U;
        cmptparms[compno].prec = prec;
        cmptparms[compno].sgnd = 0U;
    }
    img = grk_image_create(numcmpts, ( grk_image_cmptparm  *)cmptparms, GRK_CLRSPC_SRGB);
    free(cmptparms);
    return img;

}


static bool all_components_equal_subsampling(grk_image *image) {
	if (image->numcomps == 0)
		return true;

	uint16_t i;
	for (i = 1U; i < image->numcomps; ++i) {
		if (image->comps[0].dx != image->comps[i].dx) {
			break;
		}
		if (image->comps[0].dy != image->comps[i].dy) {
			break;
		}
	}
	if (i != image->numcomps) {
		spdlog::error("Color conversion: all components must have the same subsampling.");
		return false;
	}
	return true;
}
/*--------------------------------------------------------
Matrix for sYCC, Amendment 1 to IEC 61966-2-1

Y :   0.299   0.587    0.114   :R
Cb:  -0.1687 -0.3312   0.5     :G
Cr:   0.5    -0.4187  -0.0812  :B

Inverse:

R: 1        -3.68213e-05    1.40199      :Y
G: 1.00003  -0.344125      -0.714128     :Cb - 2^(prec - 1)
B: 0.999823  1.77204       -8.04142e-06  :Cr - 2^(prec - 1)

-----------------------------------------------------------*/
static void sycc_to_rgb(int offset, int upb, int y, int cb, int cr,
                        int *out_r, int *out_g, int *out_b)
{
    int r, g, b;

    cb -= offset;
    cr -= offset;
    r = y + (int)(1.402 * (float)cr);
    if(r < 0) r = 0;
    else if(r > upb) r = upb;
    *out_r = r;

    g = y - (int)(0.344 * (float)cb + 0.714 * (float)cr);
    if(g < 0) g = 0;
    else if(g > upb) g = upb;
    *out_g = g;

    b = y + (int)(1.772 * (float)cb);
    if(b < 0) b = 0;
    else if(b > upb) b = upb;
    *out_b = b;
}

static void sycc444_to_rgb(grk_image *img)
{
	int *d0, *d1, *d2, *r, *g, *b;
	const int *y, *cb, *cr;
	size_t maxw, maxh, max, i;
	int offset, upb;
	grk_image *  new_image = image_create(3, img->comps[0].w, img->comps[0].h, img->comps[0].prec);
	if (!new_image)
		return;

	upb = (int)img->comps[0].prec;
	offset = 1 << (upb - 1); upb = (1 << upb) - 1;

	maxw = (size_t)img->comps[0].w;
	maxh = (size_t)img->comps[0].h;
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
	{
		sycc_to_rgb(offset, upb, *y, *cb, *cr, r, g, b);
		++y; ++cb; ++cr; ++r; ++g; ++b;
	}
	grk_image_all_components_data_free(img);
	img->comps[0].data = d0;
	img->comps[0].owns_data = true;
	img->comps[1].data = d1;
	img->comps[1].owns_data = true;
	img->comps[2].data = d2;
	img->comps[2].owns_data = true;
	img->color_space = GRK_CLRSPC_SRGB;
	return;
}/* sycc444_to_rgb() */


static void sycc422_to_rgb(grk_image *img)
{
	int *d0, *d1, *d2, *r, *g, *b;
	const int *y, *cb, *cr;
	size_t maxw, maxh, offx, loopmaxw;
	int offset, upb;
	size_t i;

	grk_image *  new_image = image_create(3, img->comps[0].w, img->comps[0].h, img->comps[0].prec);
	if (!new_image)
		return;


	upb = (int)img->comps[0].prec;
	offset = 1 << (upb - 1); upb = (1 << upb) - 1;

	maxw = (size_t)img->comps[0].w; maxh = (size_t)img->comps[0].h;

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

	for (i = 0U; i < maxh; ++i)
	{
		size_t j;

		if (offx > 0U) {
			sycc_to_rgb(offset, upb, *y, 0, 0, r, g, b);
			++y; ++r; ++g; ++b;
		}

		for (j = 0U; j < (loopmaxw & ~(size_t)1U); j += 2U)
		{
			sycc_to_rgb(offset, upb, *y, *cb, *cr, r, g, b);
			++y; ++r; ++g; ++b;
			sycc_to_rgb(offset, upb, *y, *cb, *cr, r, g, b);
			++y; ++r; ++g; ++b; ++cb; ++cr;
		}
		if (j < loopmaxw) {
			sycc_to_rgb(offset, upb, *y, *cb, *cr, r, g, b);
			++y; ++r; ++g; ++b; ++cb; ++cr;
		}
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
	return;

}/* sycc422_to_rgb() */


static void sycc420_to_rgb(grk_image *img)
{
	int *d0, *d1, *d2, *r, *g, *b, *nr, *ng, *nb;
	const int *y, *cb, *cr, *ny;
	size_t maxw, maxh, offx, loopmaxw, offy, loopmaxh;
	int offset, upb;
	size_t i;
	grk_image *  new_image = image_create(3, img->comps[0].w, img->comps[0].h, img->comps[0].prec);
	if (!new_image)
		return;

	upb = (int)img->comps[0].prec;
	offset = 1 << (upb - 1); upb = (1 << upb) - 1;

	maxw = (size_t)img->comps[0].w;
	maxh = (size_t)img->comps[0].h;

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
	/* if img->y0 is odd, then first line shall use Cb/Cr = 0 */
	offy = img->y0 & 1U;
	loopmaxh = maxh - offy;

	if (offy > 0U) {
		size_t j;

		for (j = 0U; j < maxw; ++j)
		{
			sycc_to_rgb(offset, upb, *y, 0, 0, r, g, b);
			++y; ++r; ++g; ++b;
		}
	}

	for (i = 0U; i < (loopmaxh & ~(size_t)1U); i += 2U)
	{
		size_t j;

		ny = y + maxw;
		nr = r + maxw; ng = g + maxw; nb = b + maxw;

		if (offx > 0U) {
			sycc_to_rgb(offset, upb, *y, 0, 0, r, g, b);
			++y; ++r; ++g; ++b;
			sycc_to_rgb(offset, upb, *ny, *cb, *cr, nr, ng, nb);
			++ny; ++nr; ++ng; ++nb;
		}

		for (j = 0U; j < (loopmaxw & ~(size_t)1U); j += 2U)
		{
			sycc_to_rgb(offset, upb, *y, *cb, *cr, r, g, b);
			++y; ++r; ++g; ++b;
			sycc_to_rgb(offset, upb, *y, *cb, *cr, r, g, b);
			++y; ++r; ++g; ++b;

			sycc_to_rgb(offset, upb, *ny, *cb, *cr, nr, ng, nb);
			++ny; ++nr; ++ng; ++nb;
			sycc_to_rgb(offset, upb, *ny, *cb, *cr, nr, ng, nb);
			++ny; ++nr; ++ng; ++nb; ++cb; ++cr;
		}
		if (j < loopmaxw)
		{
			sycc_to_rgb(offset, upb, *y, *cb, *cr, r, g, b);
			++y; ++r; ++g; ++b;

			sycc_to_rgb(offset, upb, *ny, *cb, *cr, nr, ng, nb);
			++ny; ++nr; ++ng; ++nb; ++cb; ++cr;
		}
		y += maxw; r += maxw; g += maxw; b += maxw;
	}
	if (i < loopmaxh)
	{
		size_t j;

		for (j = 0U; j < (maxw & ~(size_t)1U); j += 2U)
		{
			sycc_to_rgb(offset, upb, *y, *cb, *cr, r, g, b);

			++y; ++r; ++g; ++b;

			sycc_to_rgb(offset, upb, *y, *cb, *cr, r, g, b);

			++y; ++r; ++g; ++b; ++cb; ++cr;
		}
		if (j < maxw)
		{
			sycc_to_rgb(offset, upb, *y, *cb, *cr, r, g, b);
		}
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
	return;

}/* sycc420_to_rgb() */

void color_sycc_to_rgb(grk_image *img)
{
    if(img->numcomps < 3) {
        spdlog::warn("color_sycc_to_rgb: number of components {} is less than 3."
        		" Unable to convert", img->numcomps);
        return;
    }

    if((img->comps[0].dx == 1)
            && (img->comps[1].dx == 2)
            && (img->comps[2].dx == 2)
            && (img->comps[0].dy == 1)
            && (img->comps[1].dy == 2)
            && (img->comps[2].dy == 2)) { /* horizontal and vertical sub-sample */
        sycc420_to_rgb(img);
    } else if((img->comps[0].dx == 1)
              && (img->comps[1].dx == 2)
              && (img->comps[2].dx == 2)
              && (img->comps[0].dy == 1)
              && (img->comps[1].dy == 1)
              && (img->comps[2].dy == 1)) { /* horizontal sub-sample only */
        sycc422_to_rgb(img);
    } else if((img->comps[0].dx == 1)
              && (img->comps[1].dx == 1)
              && (img->comps[2].dx == 1)
              && (img->comps[0].dy == 1)
              && (img->comps[1].dy == 1)
              && (img->comps[2].dy == 1)) { /* no sub-sample */
        sycc444_to_rgb(img);
    } else {
        spdlog::warn("color_sycc_to_rgb:  Invalid sub-sampling: ({},{}), ({},{}), ({},{})."
        		" Unable to convert.",
				img->comps[0].dx, img->comps[0].dy,
				img->comps[1].dx, img->comps[1].dy,
				img->comps[2].dx, img->comps[2].dy );
        return;
    }
    img->color_space = GRK_CLRSPC_SRGB;

}/* color_sycc_to_rgb() */

#if defined(GROK_HAVE_LIBLCMS)

/*#define DEBUG_PROFILE*/
void color_apply_icc_profile(grk_image *image, bool forceRGB, bool verbose)
{
	cmsColorSpaceSignature in_space;
	cmsColorSpaceSignature out_space;
	cmsUInt32Number intent =0;
	cmsHTRANSFORM transform = nullptr;
	cmsHPROFILE in_prof = nullptr;
	cmsHPROFILE out_prof=nullptr;
    cmsUInt32Number in_type, out_type, nr_samples;
    int prec, i, max, max_w, max_h;
    GRK_COLOR_SPACE oldspace;
    grk_image *  new_image = nullptr;
	(void)verbose;
	if (image->numcomps == 0 || !all_components_equal_subsampling(image))
		return;
	in_prof = cmsOpenProfileFromMem(image->icc_profile_buf, image->icc_profile_len);
#ifdef DEBUG_PROFILE
    FILE *icm = fopen("debug.icm","wb");
    fwrite( image->icc_profile_buf,1, image->icc_profile_len,icm);
    fclose(icm);
#endif

    if(in_prof == nullptr)
		return;

	in_space = cmsGetPCS(in_prof);
	out_space = cmsGetColorSpace(in_prof);
	intent = cmsGetHeaderRenderingIntent(in_prof);


    max_w = (int)image->comps[0].w;
    max_h = (int)image->comps[0].h;

	if (!max_w || !max_h)
		goto cleanup;

    prec = (int)image->comps[0].prec;
    oldspace = image->color_space;

    if(out_space == cmsSigRgbData) { /* enumCS 16 */
		unsigned int i, nr_comp = image->numcomps;

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

		if( prec <= 8 ) {
            in_type = TYPE_RGB_8;
            out_type = TYPE_RGB_8;
        } else {
            in_type = TYPE_RGB_16;
            out_type = TYPE_RGB_16;
        }
        out_prof = cmsCreate_sRGBProfile();
        image->color_space = GRK_CLRSPC_SRGB;
    } else if(out_space == cmsSigGrayData) { /* enumCS 17 */
        in_type = TYPE_GRAY_8;
        out_type = TYPE_RGB_8;
        out_prof = cmsCreate_sRGBProfile();
		if (forceRGB)
			image->color_space = GRK_CLRSPC_SRGB;
		else 
			image->color_space = GRK_CLRSPC_GRAY;
    } else if(out_space == cmsSigYCbCrData) { /* enumCS 18 */
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
    (void)prec;
    (void)in_space;
#endif /* DEBUG_PROFILE */

	transform = cmsCreateTransform(in_prof, in_type,
                                   out_prof, out_type, intent, 0);

    cmsCloseProfile(in_prof);
	in_prof = nullptr;
    cmsCloseProfile(out_prof);
	out_prof = nullptr;


    if(transform == nullptr) {
#ifdef DEBUG_PROFILE
        spdlog::error("{}:{}:color_apply_icc_profile\n\tcmsCreateTransform failed. "
                "ICC Profile ignored.",__FILE__,__LINE__);
#endif
        image->color_space = oldspace;
        return;
    }
    if(image->numcomps > 2) { /* RGB, RGBA */
        if( prec <= 8 ) {
			int *r=nullptr, *g=nullptr, *b=nullptr;
            uint8_t *in=nullptr, *inbuf = nullptr, *out=nullptr, *outbuf = nullptr;
            max = max_w * max_h;
            nr_samples = max * 3 * (cmsUInt32Number)sizeof(uint8_t);
            in = inbuf = (uint8_t*)malloc(nr_samples);
			if (!in) {
				goto cleanup;
			}
            out = outbuf = (uint8_t*)malloc(nr_samples);
			if (!out) {
				free(inbuf);
				goto cleanup;
			}

            r = image->comps[0].data;
            g = image->comps[1].data;
            b = image->comps[2].data;

            for(i = 0; i < max; ++i) {
                *in++ = (uint8_t)*r++;
                *in++ = (uint8_t)*g++;
                *in++ = (uint8_t)*b++;
            }

            cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number)max);

            r = image->comps[0].data;
            g = image->comps[1].data;
            b = image->comps[2].data;

            for(i = 0; i < max; ++i) {
                *r++ = (int)*out++;
                *g++ = (int)*out++;
                *b++ = (int)*out++;
            }
    		free(inbuf);
    		free(outbuf);
        } else {
			int *r = nullptr, *g = nullptr, *b = nullptr;
            unsigned short *in=nullptr, *inbuf=nullptr, *out=nullptr, *outbuf = nullptr;
            max = max_w * max_h;
            nr_samples =  max * 3 * (cmsUInt32Number)sizeof(unsigned short);
            in = inbuf = (unsigned short*)malloc(nr_samples);
			if (!in)
				goto cleanup;
            out = outbuf = (unsigned short*)malloc(nr_samples);
			if (!out) {
				free(inbuf);
				goto cleanup;
			}

            r = image->comps[0].data;
            g = image->comps[1].data;
            b = image->comps[2].data;

            for(i = 0; i < max; ++i) {
                *in++ = (unsigned short)*r++;
                *in++ = (unsigned short)*g++;
                *in++ = (unsigned short)*b++;
            }

            cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number)max);

            r = image->comps[0].data;
            g = image->comps[1].data;
            b = image->comps[2].data;

            for(i = 0; i < max; ++i) {
                *r++ = (int)*out++;
                *g++ = (int)*out++;
                *b++ = (int)*out++;
            }
    		free(inbuf);
    		free(outbuf);
        }
    } else { /* GRAY, GRAYA */
		int *r = nullptr;
		int *g = nullptr;
		int *b = nullptr;
        uint8_t *in=nullptr, *inbuf = nullptr, *out=nullptr, *outbuf = nullptr;

        max = max_w * max_h;
        nr_samples = max * 3 * (cmsUInt32Number)sizeof(uint8_t);
		 grk_image_comp  *comps = ( grk_image_comp  * )realloc(image->comps, (image->numcomps + 2) * sizeof( grk_image_comp) );
		if (!comps)
			goto cleanup;
		image->comps = comps;

        in = inbuf = (uint8_t*)malloc(nr_samples);
		if (!in)
			goto cleanup;
        out = outbuf = (uint8_t*)malloc(nr_samples);
		if (!out) {
			free(inbuf);
			goto cleanup;
		}

        new_image = image_create(2, image->comps[0].w, image->comps[0].h, image->comps[0].prec);
		if (!new_image) {
			free(inbuf);
			free(outbuf);
			goto cleanup;
		}

        if(image->numcomps == 2)
            image->comps[3] = image->comps[1];

        image->comps[1] = image->comps[0];
        image->comps[2] = image->comps[0];

		image->comps[1].data = new_image->comps[0].data;
		image->comps[1].owns_data = true;
		image->comps[2].data = new_image->comps[1].data;
		image->comps[2].owns_data = true;

        new_image->comps[0].data= nullptr;
        new_image->comps[1].data = nullptr;

        grk_image_destroy(new_image);
        new_image = nullptr;

		if (forceRGB)
			image->numcomps += 2;

        r = image->comps[0].data;
        for(i = 0; i < max; ++i) {
            *in++ = (uint8_t)*r++;
        }
        cmsDoTransform(transform, inbuf, outbuf, (cmsUInt32Number)max);

        r = image->comps[0].data;
        g = image->comps[1].data;
        b = image->comps[2].data;

        for(i = 0; i < max; ++i) {
            *r++ = (int)*out++;
			if (forceRGB) {
				*g++ = (int)*out++;
				*b++ = (int)*out++;
			}
			else { //just skip green and blue channels
				out += 2;
			}
        }
		free(inbuf);
		free(outbuf);
    }/* if(image->numcomps */
cleanup:
	if (in_prof)
		cmsCloseProfile(in_prof);
	if (out_prof)
		cmsCloseProfile(out_prof);
	if (transform)
		cmsDeleteTransform(transform);
}/* color_apply_icc_profile() */

// transform LAB colour space to sRGB @ 16 bit precision
void color_cielab_to_rgb(grk_image *image,bool verbose){
    uint32_t *row;
    int enumcs, numcomps;
    numcomps = (int)image->numcomps;
    // sanity checks
    if(numcomps != 3) {
		if (verbose)
			spdlog::warn("{}:{}:\n\tnumcomps {} not handled. Quitting.",
                __FILE__,__LINE__,numcomps);
        return;
    }
	if (image->numcomps == 0 || !all_components_equal_subsampling(image))
		return;
    row = (uint32_t*)image->icc_profile_buf;
    enumcs = row[0];
	if (enumcs != 14) { /* CIELab */
		if (verbose)
			spdlog::warn("{}:{}:\n\tenumCS {} not handled. Ignoring.", __FILE__, __LINE__, enumcs);
		return;
	}

	bool defaultType = true;
    image->color_space = GRK_CLRSPC_SRGB;
	uint32_t illuminant = GRK_CIE_D50;
	cmsCIExyY WhitePoint;
	defaultType = row[1] == GRK_DEFAULT_CIELAB_SPACE;
    int *L, *a, *b, *red, *green, *blue;
    int *src0, *src1, *src2, *dst0, *dst1, *dst2;
	// range, offset and precision for L,a and b coordinates
    double r_L, o_L, r_a, o_a, r_b, o_b, prec_L, prec_a, prec_b;
    double minL, maxL, mina, maxa, minb, maxb;
    unsigned int i, max;
    cmsUInt16Number RGB[3];
    grk_image *  new_image = image_create(3, image->comps[0].w, image->comps[0].h, image->comps[0].prec);
	if (!new_image)
		return;
    prec_L = (double)image->comps[0].prec;
    prec_a = (double)image->comps[1].prec;
    prec_b = (double)image->comps[2].prec;

    if(defaultType) { // default Lab space
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
		if (verbose)
			spdlog::warn("Unrecognized illuminant {} in CIELab colour space. Setting to default Daylight50", illuminant);
		illuminant = GRK_CIE_D50;
		break;
	}

	// Lab input profile
	cmsHPROFILE in = cmsCreateLab4Profile(illuminant == GRK_CIE_D50 ? nullptr : &WhitePoint);
	// sRGB output profile
	cmsHPROFILE out = cmsCreate_sRGBProfile();
	cmsHTRANSFORM transform =
		cmsCreateTransform(in, TYPE_Lab_DBL, out, TYPE_RGB_16, INTENT_PERCEPTUAL, 0);

	cmsCloseProfile(in);
	cmsCloseProfile(out);
	if (transform == nullptr) {
		grk_image_destroy(new_image);
		return;
	}

    L = src0 = image->comps[0].data;
    a = src1 = image->comps[1].data;
    b = src2 = image->comps[2].data;

    max = image->comps[0].w * image->comps[0].h;

    red = dst0	= new_image->comps[0].data;
    green = dst1 = new_image->comps[1].data;
    blue = dst2	 = new_image->comps[2].data;

    new_image->comps[0].data=nullptr;
    new_image->comps[1].data=nullptr;
    new_image->comps[2].data=nullptr;

    grk_image_destroy(new_image);
    new_image = nullptr;

    minL = -(r_L * o_L) / (pow(2, prec_L) - 1);
    maxL = minL + r_L;

    mina = -(r_a * o_a)/(pow(2, prec_a)-1);
    maxa = mina + r_a;

    minb = -(r_b * o_b)/(pow(2, prec_b)-1);
    maxb = minb + r_b;

    for(i = 0; i < max; ++i) {
		cmsCIELab Lab;
        Lab.L = minL + (double)(*L) * (maxL - minL)/(pow(2, prec_L)-1);
        ++L;
        Lab.a = mina + (double)(*a) * (maxa - mina)/(pow(2, prec_a)-1);
        ++a;
        Lab.b = minb + (double)(*b) * (maxb - minb)/(pow(2, prec_b)-1);
        ++b;

        cmsDoTransform(transform, &Lab, RGB, 1);

        *red++		= RGB[0];
        *green++	= RGB[1];
        *blue++		= RGB[2];
    }
    cmsDeleteTransform(transform);
    grk_image_all_components_data_free(image);
	image->comps[0].data = dst0;
	image->comps[0].owns_data = true;
	image->comps[1].data = dst1;
	image->comps[1].owns_data = true;
	image->comps[2].data = dst2;
	image->comps[2].owns_data = true;

    image->color_space = GRK_CLRSPC_SRGB;
    image->comps[0].prec = 16;
    image->comps[1].prec = 16;
    image->comps[2].prec = 16;

    
}/* color_cielab_to_rgb() */

#endif /* GROK_HAVE_LIBLCMS */


int color_cmyk_to_rgb(grk_image *image)
{
    float C, M, Y, K;
    float sC, sM, sY, sK;
    uint32_t w, h;
	uint64_t i, area;

    w = image->comps[0].w;
    h = image->comps[0].h;

    if( (image->numcomps < 4)  || !all_components_equal_subsampling(image))
		return 1;


	area = (uint64_t)w * h;

    sC = 1.0F / (float)((1 << image->comps[0].prec) - 1);
    sM = 1.0F / (float)((1 << image->comps[1].prec) - 1);
    sY = 1.0F / (float)((1 << image->comps[2].prec) - 1);
    sK = 1.0F / (float)((1 << image->comps[3].prec) - 1);

    for(i = 0; i < area; ++i) {
        /* CMYK values from 0 to 1 */
        C = (float)(image->comps[0].data[i]) * sC;
        M = (float)(image->comps[1].data[i]) * sM;
        Y = (float)(image->comps[2].data[i]) * sY;
        K = (float)(image->comps[3].data[i]) * sK;

        /* Invert all CMYK values */
        C = 1.0F - C;
        M = 1.0F - M;
        Y = 1.0F - Y;
        K = 1.0F - K;

        /* CMYK -> RGB : RGB results from 0 to 255 */
        image->comps[0].data[i] = (int)(255.0F * C * K); /* R */
        image->comps[1].data[i] = (int)(255.0F * M * K); /* G */
        image->comps[2].data[i] = (int)(255.0F * Y * K); /* B */
    }

    grk_image_single_component_data_free(image->comps + 3);
    image->comps[0].prec = 8;
    image->comps[1].prec = 8;
    image->comps[2].prec = 8;
    image->numcomps -= 1;
    image->color_space = GRK_CLRSPC_SRGB;

    for (i = 3; i < image->numcomps; ++i) {
        memcpy(&(image->comps[i]), &(image->comps[i+1]), sizeof(image->comps[i]));
    }

	return 0;

}/* color_cmyk_to_rgb() */

int color_esycc_to_rgb(grk_image *image)
{
    int y, cb, cr, sign1, sign2, val;
	uint32_t w, h;
	uint64_t area, i;
    int flip_value = (1 << (image->comps[0].prec-1));
    int max_value = (1 << image->comps[0].prec) - 1;

    if( (image->numcomps < 3)  || !all_components_equal_subsampling(image))
		return 1;
	
	w = image->comps[0].w;
    h = image->comps[0].h;

    sign1 = (int)image->comps[1].sgnd;
    sign2 = (int)image->comps[2].sgnd;

	area = (uint64_t)w * h;

    for(i = 0; i < area; ++i) {

        y = image->comps[0].data[i];
        cb = image->comps[1].data[i];
        cr = image->comps[2].data[i];

        if( !sign1) cb -= flip_value;
        if( !sign2) cr -= flip_value;

        val = (int)
              ((float)y - (float)0.0000368 * (float)cb
               + (float)1.40199 * (float)cr + (float)0.5);

        if(val > max_value) val = max_value;
        else if(val < 0) val = 0;
        image->comps[0].data[i] = val;

        val = (int)
              ((float)1.0003 * (float)y - (float)0.344125 * (float)cb
               - (float)0.7141128 * (float)cr + (float)0.5);

        if(val > max_value) val = max_value;
        else if(val < 0) val = 0;
        image->comps[1].data[i] = val;

        val = (int)
              ((float)0.999823 * (float)y + (float)1.77204 * (float)cb
               - (float)0.000008 *(float)cr + (float)0.5);

        if(val > max_value) val = max_value;
        else if(val < 0) val = 0;
        image->comps[2].data[i] = val;
    }
    image->color_space = GRK_CLRSPC_SRGB;
	return 0;

}/* color_esycc_to_rgb() */
