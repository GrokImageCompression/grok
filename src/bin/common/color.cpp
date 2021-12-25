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
#include <lcms2.h>

namespace grk
{
//#define DEBUG_PROFILE


void allocPalette(grk_color* color, uint8_t num_channels, uint16_t num_entries)
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

	return rc;
}

} // namespace grk
