/*
*    Copyright (C) 2016-2019 Grok Image Compression Inc.
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
 * Copyright (c) 2015, Matthieu Darbois
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

#include <cstdio>
#include <cstdlib>
#include "opj_apps_config.h"
#include "grok.h"
#include "PNGFormat.h"
#include "convert.h"
#include <cstring>


#ifdef GROK_HAVE_LIBLCMS
#include <lcms2.h>
#endif
#include <zlib.h>
#include <png.h>
#include <memory>
#include <iostream>
#include <string>
#include <cassert>
#include <locale>
#include "common.h"

#define PNG_MAGIC "\x89PNG\x0d\x0a\x1a\x0a"
#define MAGIC_SIZE 8
 /* PNG allows bits per sample: 1, 2, 4, 8, 16 */

static bool pngWarningHandlerVerbose = true;

static void png_warning_fn(png_structp png_ptr,
						 png_const_charp warning_message) {
	(void)png_ptr;
	if (pngWarningHandlerVerbose) {
		spdlog::error("libpng warning: {}", warning_message);
		spdlog::error("");
	}
}

void pngSetVerboseFlag(bool verbose) {
	pngWarningHandlerVerbose = verbose;
}

static void convert_16u32s_C1R(const uint8_t* pSrc, int32_t* pDst, size_t length, bool invert)
{
	size_t i;
	for (i = 0; i < length; i++) {
		int32_t val0 = *pSrc++;
		int32_t val1 = *pSrc++;
		pDst[i] = INV(val0 << 8 | val1, 0xFFFF, invert);
	}
}

struct pngToImageInfo {
	pngToImageInfo() : png(nullptr),
		reader(nullptr),
		rows(nullptr),
		row32s(nullptr),
		image(nullptr),
		readFromStdin(false),
		colorSpace(OPJ_CLRSPC_UNKNOWN)
	{}

	png_structp  png;
	FILE *reader;
	uint8_t** rows;
	int32_t* row32s;
	opj_image_t *image;
	bool readFromStdin;
	OPJ_COLOR_SPACE colorSpace;
};


static opj_image_t *pngtoimage(const char *read_idf, 
								opj_cparameters_t * params)
{
	pngToImageInfo local_info;
	png_infop    info = nullptr;
	int bit_depth, interlace_type, compression_type, filter_type;
	uint32_t i;
	png_uint_32  width = 0U, height = 0U;
	int color_type;
	local_info.readFromStdin = grk::useStdio(read_idf);
	opj_image_cmptparm_t cmptparm[4];
	uint32_t nr_comp;
	uint8_t sigbuf[8];
	convert_XXx32s_C1R cvtXXTo32s = nullptr;
	convert_32s_CXPX cvtCxToPx = nullptr;
	int32_t* planes[4];
	int srgbIntent = -1;
	png_textp text_ptr;
	int num_comments = 0;
	int unit;
	png_uint_32 resx, resy;

	if (local_info.readFromStdin) {
		if (!grok_set_binary_mode(stdin))
			return nullptr;
		local_info.reader = stdin;
	}
	else {
		if ((local_info.reader = fopen(read_idf, "rb")) == nullptr) {
			spdlog::error("pngtoimage: can not open {}\n", read_idf);
			return nullptr;
		}
	}

	if (fread(sigbuf, 1, MAGIC_SIZE, local_info.reader) != MAGIC_SIZE
		|| memcmp(sigbuf, PNG_MAGIC, MAGIC_SIZE) != 0) {
		spdlog::error("pngtoimage: {} is no valid PNG file\n", read_idf);
		goto beach;
	}

	if ((local_info.png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
		nullptr, nullptr, png_warning_fn)) == nullptr)
		goto beach;

	// allow Microsoft/HP 3144-byte sRGB profile, normally skipped by library 
	// because it deems it broken. (a controversial decision)
	png_set_option(local_info.png, PNG_SKIP_sRGB_CHECK_PROFILE, PNG_OPTION_ON);
	
	// treat some errors as warnings
	png_set_benign_errors(local_info.png, 1);

	if ((info = png_create_info_struct(local_info.png)) == nullptr)
		goto beach;

	if (setjmp(png_jmpbuf(local_info.png)))
		goto beach;



	png_init_io(local_info.png, local_info.reader);
	png_set_sig_bytes(local_info.png, MAGIC_SIZE);

	png_read_info(local_info.png, info);

	if (png_get_IHDR(local_info.png, info, &width, &height,
		&bit_depth, &color_type, &interlace_type,
		&compression_type, &filter_type) == 0)
		goto beach;

	if (!width || !height)
		goto beach;

	/* png_set_expand():
	* expand paletted images to RGB, expand grayscale images of
	* less than 8-bit depth to 8-bit depth, and expand tRNS chunks
	* to alpha channels.
	*/
	if (color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_expand(local_info.png);
	}

	if (png_get_valid(local_info.png, info, PNG_INFO_tRNS)) {
		png_set_expand(local_info.png);
	}
	/* We might want to expand background */
	/*
	if(png_get_valid(local_info.png, info, PNG_INFO_bKGD)) {
	png_color_16p bgnd;
	png_get_bKGD(local_info.png, info, &bgnd);
	png_set_background(local_info.png, bgnd, PNG_BACKGROUND_GAMMA_FILE, 1, 1.0);
	}
	*/


	if (png_get_sRGB(local_info.png, info, &srgbIntent)) {
		if (srgbIntent >= 0 && srgbIntent <= 3)
			local_info.colorSpace = OPJ_CLRSPC_SRGB;
	}


	png_read_update_info(local_info.png, info);
	color_type = png_get_color_type(local_info.png, info);

	switch (color_type) {
	case PNG_COLOR_TYPE_GRAY:
		nr_comp = 1;
		break;
	case PNG_COLOR_TYPE_GRAY_ALPHA:
		nr_comp = 2;
		break;
	case PNG_COLOR_TYPE_RGB:
		nr_comp = 3;
		break;
	case PNG_COLOR_TYPE_RGB_ALPHA:
		nr_comp = 4;
		break;
	default:
		spdlog::error("pngtoimage: colortype {} is not supported\n", color_type);
		goto beach;
	}

	if (local_info.colorSpace == OPJ_CLRSPC_UNKNOWN)
		local_info.colorSpace = (nr_comp > 2U) ? OPJ_CLRSPC_SRGB : OPJ_CLRSPC_GRAY;

	cvtCxToPx = convert_32s_CXPX_LUT[nr_comp];
	bit_depth = png_get_bit_depth(local_info.png, info);

	switch (bit_depth) {
	case 1:
	case 2:
	case 4:
	case 8:
		cvtXXTo32s = convert_XXu32s_C1R_LUT[bit_depth];
		break;
	case 16: /* 16 bpp is specific to PNG */
		cvtXXTo32s = convert_16u32s_C1R;
		break;
	default:
		spdlog::error("pngtoimage: bit depth {} is not supported\n", bit_depth);
		goto beach;
	}


	local_info.rows = (uint8_t**)calloc(height, sizeof(uint8_t*));
	if (local_info.rows == nullptr) {
		spdlog::error("pngtoimage: out of memory");
		goto beach;
	}
	for (i = 0; i < height; ++i) {
		local_info.rows[i] = (uint8_t*)malloc(png_get_rowbytes(local_info.png, info));
		if (!local_info.rows[i]) {
			spdlog::error("pngtoimage: out of memory");
			goto beach;
		}
	}

	png_read_image(local_info.png, local_info.rows);

	/* Create image */
	memset(cmptparm, 0, sizeof(cmptparm));
	for (i = 0; i < nr_comp; ++i) {
		cmptparm[i].prec = bit_depth;
		cmptparm[i].sgnd = 0;
		cmptparm[i].dx = params->subsampling_dx;
		cmptparm[i].dy = params->subsampling_dy;
		cmptparm[i].w = width;
		cmptparm[i].h = height;
	}

	local_info.image = opj_image_create(nr_comp, &cmptparm[0], local_info.colorSpace);
	if (local_info.image == nullptr)
		goto beach;
	local_info.image->x0 = params->image_offset_x0;
	local_info.image->y0 = params->image_offset_y0;
	local_info.image->x1 = (local_info.image->x0 + (width - 1) * params->subsampling_dx + 1 + local_info.image->x0);
	local_info.image->y1 = (local_info.image->y0 + (height - 1) * params->subsampling_dy + 1 + local_info.image->y0);

	/* Set alpha channel. Only non-premultiplied alpha is supported */
	local_info.image->comps[nr_comp - 1U].alpha = (uint16_t)(1U - (nr_comp & 1U));

	for (i = 0; i < nr_comp; i++) {
		planes[i] = local_info.image->comps[i].data;
	}

	// See if iCCP chunk is present
	if (png_get_valid(local_info.png, info, PNG_INFO_iCCP))
	{
		uint32_t ProfileLen = 0;
		png_bytep ProfileData = nullptr;
		int  Compression = 0;
		png_charp ProfileName = nullptr;
		if (png_get_iCCP(local_info.png,
			info,
			&ProfileName,
			&Compression,
			&ProfileData,
			&ProfileLen) == PNG_INFO_iCCP) {
			local_info.image->icc_profile_buf = opj_buffer_new(ProfileLen);
			memcpy(local_info.image->icc_profile_buf, ProfileData, ProfileLen);
			local_info.image->icc_profile_len = ProfileLen;
			local_info.image->color_space = OPJ_CLRSPC_ICC;
		}
	}

	if (params->verbose && png_get_valid(local_info.png, info, PNG_INFO_gAMA)) {
		spdlog::warn("input PNG contains gamma value; this will not be stored in compressed image.");
	}

	if (params->verbose &&  png_get_valid(local_info.png, info, PNG_INFO_cHRM)) {
		spdlog::warn("input PNG contains chroma information which will not be stored in compressed image.");
	}

	/*
	double fileGamma = 0.0;
	if (png_get_gAMA(png, info, &fileGamma)) {
	spdlog::warn("input PNG contains gamma value of %f; this will not be stored in compressed image.\n", fileGamma);
	}
	double wpx, wpy, rx, ry, gx, gy, bx, by; // white point and primaries
	if (png_get_cHRM(png, info, &wpx, &wpy, &rx, &ry, &gx, &gy, &bx, &by)) 	{
	spdlog::warn("input PNG contains chroma information which will not be stored in compressed image.");
	}
	*/
		
	num_comments = png_get_text(local_info.png, info, &text_ptr, NULL);
	if (num_comments) {
		for (int i = 0; i < num_comments; ++i) {
			const char* key = text_ptr[i].key;
			if (!strcmp(key, "Description")) {

			}
			else if (!strcmp(key, "Author")) {

			}
			else if (!strcmp(key, "Title")) {

			}
			else if (!strcmp(key, "XML:com.adobe.xmp")) {
				if (text_ptr[i].text_length) {
					local_info.image->xmp_len = text_ptr[i].text_length;
					local_info.image->xmp_buf = opj_buffer_new(local_info.image->xmp_len);
					memcpy(local_info.image->xmp_buf, text_ptr[i].text, local_info.image->xmp_len);
				}
			}
			// other comments
			else {

			}
		}
	}

	if (png_get_pHYs(local_info.png, info, &resx, &resy, &unit)) {
		if (unit == PNG_RESOLUTION_METER) {
			local_info.image->capture_resolution[0] = resx;
			local_info.image->capture_resolution[1] = resy;
		}
		else {
			if (params->verbose) {
				spdlog::warn("input PNG contains resolution information in unknown units. Ignoring");
			}
		}
	}

	local_info.row32s = (int32_t *)malloc((size_t)width * nr_comp * sizeof(int32_t));
	if (local_info.row32s == nullptr)
		goto beach;

	for (i = 0; i < height; ++i) {
		cvtXXTo32s(local_info.rows[i], local_info.row32s, (size_t)width * nr_comp, false);
		cvtCxToPx(local_info.row32s, planes, width);
		planes[0] += width;
		planes[1] += width;
		planes[2] += width;
		planes[3] += width;
	}
beach:
	if (local_info.rows) {
		for (i = 0; i < height; ++i) {
			if (local_info.rows[i])
				free(local_info.rows[i]);
		}
		free(local_info.rows);
	}
	if (local_info.row32s) {
		free(local_info.row32s);
	}
	if (local_info.png)
		png_destroy_read_struct(&local_info.png, &info, nullptr);
	if (!local_info.readFromStdin &&  local_info.reader){
		if (!grk::safe_fclose(local_info.reader)){
			opj_image_destroy(local_info.image);
			local_info.image = nullptr;
		}
	}
	return local_info.image;
}/* pngtoimage() */


static void convert_32s16u_C1R(const int32_t* pSrc, uint8_t* pDst, size_t length)
{
	size_t i;
	for (i = 0; i < length; i++) {
		uint32_t val = (uint32_t)pSrc[i];
		*pDst++ = (uint8_t)(val >> 8);
		*pDst++ = (uint8_t)val;
	}
}

struct imageToPngInfo {
	imageToPngInfo() : png(nullptr),
		writer(nullptr),
		row_buf(nullptr),
		buffer32s(nullptr),
		image(nullptr),
		writeToStdout(false),
		fails(1)
	{}
	png_structp png;
	FILE * volatile writer;
	png_bytep volatile row_buf;
	int32_t* volatile buffer32s;
	opj_image_t *image;
	bool writeToStdout;
	volatile int fails;
};

static int imagetopng(opj_image_t * image, 
					const char *write_idf,
					int32_t compressionLevel,
					bool verbose)
{
	(void)verbose;
	imageToPngInfo local_info;
	local_info.writeToStdout = grk::useStdio(write_idf);
	png_infop info = nullptr;
	int nr_comp, color_type;
	volatile int prec;
	png_color_8 sig_bit;
	int32_t const* planes[4];
	int i;

	memset(&sig_bit, 0, sizeof(sig_bit));
	prec = (int)image->comps[0].prec;
	planes[0] = image->comps[0].data;
	nr_comp = (int)image->numcomps;

	if (nr_comp > 4) {
		nr_comp = 4;
	}
	for (i = 1; i < nr_comp; ++i) {
		if (image->comps[0].dx != image->comps[i].dx) {
			break;
		}
		if (image->comps[0].dy != image->comps[i].dy) {
			break;
		}
		if (image->comps[0].prec != image->comps[i].prec) {
			break;
		}
		if (image->comps[0].sgnd != image->comps[i].sgnd) {
			break;
		}
		planes[i] = image->comps[i].data;
	}
	if (i != nr_comp) {
		spdlog::error("imagetopng: All components shall have the same subsampling, same bit depth, same sign.");
		spdlog::error("\tAborting");
		return 1;
	}
	if (prec > 8 && prec < 16) {
		for (i = 0; i < nr_comp; ++i) {
			scale_component(&(image->comps[i]), 16);
		}
		prec = 16;
	}
	else if (prec < 8 && nr_comp > 1) { /* GRAY_ALPHA, RGB, RGB_ALPHA */
		for (i = 0; i < nr_comp; ++i) {
			scale_component(&(image->comps[i]), 8);
		}
		prec = 8;
	}
	else if ((prec > 1) && (prec < 8) && ((prec == 6) || ((prec & 1) == 1))) { /* GRAY with non native precision */
		if ((prec == 5) || (prec == 6)) {
			prec = 8;
		}
		else {
			prec++;
		}
		for (i = 0; i < nr_comp; ++i) {
			scale_component(&(image->comps[i]), (uint32_t)prec);
		}
	}

	if (prec != 1 && prec != 2 && prec != 4 && prec != 8 && prec != 16) {
		spdlog::error("imagetopng: can not create {}\n\twrong bit_depth {}\n", write_idf, prec);
		return local_info.fails;
	}
	if (local_info.writeToStdout) {
		if (!grok_set_binary_mode(stdout))
			return 1;
		local_info.writer = stdout;
	}
	else {
		local_info.writer = fopen(write_idf, "wb");
		if (local_info.writer == nullptr)
			return local_info.fails;
	}


	/* Create and initialize the png_struct with the desired error handler
	* functions.  If you want to use the default stderr and longjump method,
	* you can supply nullptr for the last three parameters.  We also check that
	* the library version is compatible with the one used at compile time,
	* in case we are using dynamically linked libraries.  REQUIRED.
	*/
	local_info.png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
		nullptr, nullptr, nullptr);

	// allow Microsoft/HP 3144-byte sRGB profile, normally skipped by library 
	// because it deems it broken. (a controversial decision)
	png_set_option(local_info.png, PNG_SKIP_sRGB_CHECK_PROFILE, PNG_OPTION_ON);

	// treat some errors as warnings
	png_set_benign_errors(local_info.png, 1);

	if (local_info.png == nullptr)
		goto beach;

	/* Allocate/initialize the image information data.  REQUIRED
	*/
	info = png_create_info_struct(local_info.png);

	if (info == nullptr) goto beach;

	/* Set error handling.  REQUIRED if you are not supplying your own
	* error handling functions in the png_create_write_struct() call.
	*/
	if (setjmp(png_jmpbuf(local_info.png))) goto beach;

	/* I/O initialization functions is REQUIRED
	*/
	png_init_io(local_info.png, local_info.writer);

	/* Set the image information here.  Width and height are up to 2^31,
	* bit_depth is one of 1, 2, 4, 8, or 16, but valid values also depend on
	* the color_type selected. color_type is one of PNG_COLOR_TYPE_GRAY,
	* PNG_COLOR_TYPE_GRAY_ALPHA, PNG_COLOR_TYPE_PALETTE, PNG_COLOR_TYPE_RGB,
	* or PNG_COLOR_TYPE_RGB_ALPHA.  interlace is either PNG_INTERLACE_NONE or
	* PNG_INTERLACE_ADAM7, and the compression_type and filter_type MUST
	* currently be PNG_COMPRESSION_TYPE_BASE and PNG_FILTER_TYPE_BASE.
	* REQUIRED
	*
	* ERRORS:
	*
	* color_type == PNG_COLOR_TYPE_PALETTE && bit_depth > 8
	* color_type == PNG_COLOR_TYPE_RGB && bit_depth < 8
	* color_type == PNG_COLOR_TYPE_GRAY_ALPHA && bit_depth < 8
	* color_type == PNG_COLOR_TYPE_RGB_ALPHA) && bit_depth < 8
	*
	*/
	png_set_compression_level(local_info.png,
		(compressionLevel == DECOMPRESS_COMPRESSION_LEVEL_DEFAULT) ? 3 : compressionLevel);

	if (nr_comp >= 3) { /* RGB(A) */
		color_type = PNG_COLOR_TYPE_RGB;
		sig_bit.red = sig_bit.green = sig_bit.blue = (png_byte)prec;
	}
	else { /* GRAY(A) */
		color_type = PNG_COLOR_TYPE_GRAY;
		sig_bit.gray = (png_byte)prec;
	}
	if ((nr_comp & 1) == 0) { /* ALPHA */
		color_type |= PNG_COLOR_MASK_ALPHA;
		sig_bit.alpha = (png_byte)prec;
	}

	png_set_IHDR(local_info.png, info, image->comps[0].w, image->comps[0].h, prec, color_type,
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	png_set_sBIT(local_info.png, info, &sig_bit);
	/* png_set_gamma(png, 2.2, 1./2.2); */
	/* png_set_sRGB(png, info, PNG_sRGB_INTENT_PERCEPTUAL); */

	// Set iCCP chunk
	if (image->icc_profile_buf && image->icc_profile_len)	{
		std::string profileName = "Unknown";
		// if lcms is present, try to extract the description tag from the ICC header,
		// and use this tag as the profile name
#if defined(GROK_HAVE_LIBLCMS)
		auto in_prof = cmsOpenProfileFromMem(image->icc_profile_buf, image->icc_profile_len);
		if (in_prof) {
			cmsUInt32Number bufferSize = cmsGetProfileInfoASCII(in_prof, cmsInfoDescription, cmsNoLanguage, cmsNoCountry, nullptr, 0);
			if (bufferSize) {
				std::unique_ptr<char[]> description(new char[bufferSize]);
				cmsUInt32Number result = cmsGetProfileInfoASCII(in_prof, cmsInfoDescription, cmsNoLanguage, cmsNoCountry, description.get(), bufferSize);
				if (result) {
					profileName = description.get();
				}
			}
			cmsCloseProfile(in_prof);
		}
#endif
		png_set_iCCP(local_info.png,
			info,
			profileName.c_str(),
			PNG_COMPRESSION_TYPE_BASE,
			image->icc_profile_buf,
			image->icc_profile_len);
	}

	if (image->xmp_buf && image->xmp_len) {
		png_text txt;
		txt.key = (png_charp) "XML:com.adobe.xmp";
		txt.compression = PNG_ITXT_COMPRESSION_NONE;
		txt.text_length = image->xmp_len;
		txt.text = (png_charp)image->xmp_buf;
		txt.lang = NULL;
		txt.lang_key = NULL;
		png_set_text(local_info.png,info, &txt, 1);
	}

	if (image->capture_resolution[0] > 0 && image->capture_resolution[1] > 0) {
		png_set_pHYs(local_info.png, info, (png_uint_32)(image->capture_resolution[0]),
			(png_uint_32)(image->capture_resolution[1]), PNG_RESOLUTION_METER);
	}

	// handle libpng errors
	if (setjmp(png_jmpbuf(local_info.png))) {
		goto beach;
	}

	png_write_info(local_info.png, info);

	/* setup conversion */
	{
		size_t rowStride;
		png_size_t png_row_size;

		png_row_size = png_get_rowbytes(local_info.png, info);
		rowStride = ((size_t)image->comps[0].w * (size_t)nr_comp * (size_t)prec + 7U) / 8U;
		if (rowStride != (size_t)png_row_size) {
			spdlog::error("Invalid PNG row size");
			goto beach;
		}
		local_info.row_buf = (png_bytep)malloc(png_row_size);
		if (local_info.row_buf == nullptr) {
			spdlog::error("Can't allocate memory for PNG row");
			goto beach;
		}
		local_info.buffer32s = (int32_t*)malloc((size_t)image->comps[0].w * (size_t)nr_comp * sizeof(int32_t));
		if (local_info.buffer32s == nullptr) {
			spdlog::error("Can't allocate memory for interleaved 32s row");
			goto beach;
		}
	}

	/* convert */
	{
		size_t width = image->comps[0].w;
		uint32_t y;
		convert_32s_PXCX cvtPxToCx = convert_32s_PXCX_LUT[nr_comp];
		convert_32sXXx_C1R cvt32sToPack = nullptr;
		int32_t adjust = image->comps[0].sgnd ? 1 << (prec - 1) : 0;
		png_bytep row_buf_cpy = local_info.row_buf;
		int32_t* buffer32s_cpy = local_info.buffer32s;

		switch (prec) {
		case 1:
		case 2:
		case 4:
		case 8:
			cvt32sToPack = convert_32sXXu_C1R_LUT[prec];
			break;
		case 16:
			cvt32sToPack = convert_32s16u_C1R;
			break;
		default:
			/* never here */
			return 1;
			break;
		}

		for (y = 0; y < image->comps[0].h; ++y) {
			cvtPxToCx(planes, buffer32s_cpy, width, adjust);
			cvt32sToPack(buffer32s_cpy, row_buf_cpy, width * (size_t)nr_comp);
			png_write_row(local_info.png, row_buf_cpy);
			planes[0] += width;
			planes[1] += width;
			planes[2] += width;
			planes[3] += width;
		}
	}

	png_write_end(local_info.png, info);

	local_info.fails = 0;

beach:
	if (local_info.png) {
		png_destroy_write_struct(&local_info.png, &info);
	}
	if (local_info.row_buf) {
		free(local_info.row_buf);
	}
	if (local_info.buffer32s) {
		free(local_info.buffer32s);
	}
	if (!local_info.writeToStdout) {
		if (!grk::safe_fclose(local_info.writer)){
			local_info.fails = 1;
		}
		if (local_info.fails && write_idf)
			(void)remove(write_idf); /* ignore return value */
	}

	return local_info.fails;
}/* imagetopng() */


bool PNGFormat::encode(opj_image_t* image, const char* filename, int compressionParam, bool verbose) {
	(void)verbose;
	return imagetopng(image, filename, compressionParam,verbose) ? false : true;
}
opj_image_t*  PNGFormat::decode(const char* filename, opj_cparameters_t *parameters) {
	return pngtoimage(filename, parameters);
}
