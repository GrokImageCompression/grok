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
#include "grk_apps_config.h"
#include "grok.h"
#include "TGAFormat.h"
#include "convert.h"
#include <cstring>
#include "common.h"


#ifdef INFORMATION_ONLY
 /* TGA header definition. */
struct tga_header {
	uint8_t   id_length;              /* Image id field length    */
	uint8_t   colour_map_type;        /* Colour map type          */
	uint8_t   image_type;             /* Image type               */
											/*
											** Colour map specification
											*/
	unsigned short  colour_map_index;       /* First entry index        */
	unsigned short  colour_map_length;      /* Colour map length        */
	uint8_t   colour_map_entry_size;  /* Colour map entry size    */
											/*
											** Image specification
											*/
	unsigned short  x_origin;               /* x origin of image        */
	unsigned short  y_origin;               /* u origin of image        */
	unsigned short  image_width;            /* Image width              */
	unsigned short  image_height;           /* Image height             */
	uint8_t   pixel_depth;            /* Pixel depth              */
	uint8_t   image_desc;             /* Image descriptor         */
};
#endif /* INFORMATION_ONLY */

static unsigned short get_ushort(const uint8_t *data) {
	unsigned short val = *(const unsigned short*) data;
#ifdef GROK_BIG_ENDIAN
	val = ((val & 0xffU) << 8) | (val >> 8);
#endif
	return val;
}

#define TGA_HEADER_SIZE 18

static bool tga_readheader(FILE *fp, unsigned int *bits_per_pixel,
		unsigned int *width, unsigned int *height, int *flip_image) {
	int palette_size;
	uint8_t tga[TGA_HEADER_SIZE];
	uint8_t id_len, /*cmap_type,*/image_type;
	uint8_t pixel_depth, image_desc;
	unsigned short /*cmap_index,*/cmap_len, cmap_entry_size;
	unsigned short /*x_origin, y_origin,*/image_w, image_h;

	if (!bits_per_pixel || !width || !height || !flip_image)
		return false;

	if (fread(tga, TGA_HEADER_SIZE, 1, fp) != 1) {
		spdlog::error(" fread return a number of element different from the expected.");
		return false;
	}
	id_len = tga[0];
	/*cmap_type = tga[1];*/
	image_type = tga[2];
	/*cmap_index = get_ushort(&tga[3]);*/
	cmap_len = get_ushort(&tga[5]);
	cmap_entry_size = tga[7];

#if 0
	x_origin = get_ushort(&tga[8]);
	y_origin = get_ushort(&tga[10]);
#endif
	image_w = get_ushort(&tga[12]);
	image_h = get_ushort(&tga[14]);
	pixel_depth = tga[16];
	image_desc = tga[17];

	*bits_per_pixel = (unsigned int) pixel_depth;
	*width = (unsigned int) image_w;
	*height = (unsigned int) image_h;

	/* Ignore tga identifier, if present ... */
	if (id_len) {
		uint8_t *id = (uint8_t*) malloc(id_len);
		if (!id) {
			spdlog::error("tga_readheader: out of memory out");
			return false;
		}
		if (!fread(id, id_len, 1, fp)) {
			spdlog::error(" fread return a number of element different from the expected.");
			free(id);
			return false;
		}
		free(id);
	}

	/* Test for compressed formats ... not yet supported ...
	 // Note :-  9 - RLE encoded palettized.
	 //	  	   10 - RLE encoded RGB. */
	if (image_type > 8) {
		spdlog::error(" Sorry, compressed tga files are not currently supported.");
		return false;
	}

	*flip_image = !(image_desc & 32);

	/* Palettized formats are not yet supported, skip over the palette, if present ... */
	palette_size = cmap_len * (cmap_entry_size / 8);

	if (palette_size > 0) {
		spdlog::error("File contains a palette - not yet supported.");
		if (fseek(fp, palette_size, SEEK_CUR))
			return false;
	}
	return false;
}

#ifdef GROK_BIG_ENDIAN

static inline uint16_t swap16(uint16_t x)
{
	return (uint16_t)(((x & 0x00ffU) << 8) | ((x & 0xff00U) >> 8));
}

#endif

static int tga_writeheader(FILE *fp, int bits_per_pixel, int width, int height,
		bool flip_image) {
	uint16_t image_w, image_h, us0;
	uint8_t uc0, image_type;
	uint8_t pixel_depth, image_desc;

	if (!bits_per_pixel || !width || !height)
		return 0;

	pixel_depth = 0;

	if (bits_per_pixel < 256)
		pixel_depth = (uint8_t) bits_per_pixel;
	else {
		spdlog::error("Wrong bits per pixel inside tga_header");
		return 0;
	}
	uc0 = 0;

	if (fwrite(&uc0, 1, 1, fp) != 1)
		goto fails;
	/* id_length */
	if (fwrite(&uc0, 1, 1, fp) != 1)
		goto fails;
	/* colour_map_type */

	image_type = 2; /* Uncompressed. */
	if (fwrite(&image_type, 1, 1, fp) != 1)
		goto fails;

	us0 = 0;
	if (fwrite(&us0, 2, 1, fp) != 1)
		goto fails;
	/* colour_map_index */
	if (fwrite(&us0, 2, 1, fp) != 1)
		goto fails;
	/* colour_map_length */
	if (fwrite(&uc0, 1, 1, fp) != 1)
		goto fails;
	/* colour_map_entry_size */

	if (fwrite(&us0, 2, 1, fp) != 1)
		goto fails;
	/* x_origin */
	if (fwrite(&us0, 2, 1, fp) != 1)
		goto fails;
	/* y_origin */

	image_w = (unsigned short) width;
	image_h = (unsigned short) height;

#ifndef GROK_BIG_ENDIAN
	if (fwrite(&image_w, 2, 1, fp) != 1)
		goto fails;
	if (fwrite(&image_h, 2, 1, fp) != 1)
		goto fails;
#else
	image_w = swap16(image_w);
	image_h = swap16(image_h);
	if (fwrite(&image_w, 2, 1, fp) != 1) goto fails;
	if (fwrite(&image_h, 2, 1, fp) != 1) goto fails;
#endif

	if (fwrite(&pixel_depth, 1, 1, fp) != 1)
		goto fails;

	image_desc = 8; /* 8 bits per component. */

	if (flip_image)
		image_desc |= 32;
	if (fwrite(&image_desc, 1, 1, fp) != 1)
		goto fails;

	return 1;

	fails:
		spdlog::error("\nwrite_tgaheader: write ERROR");
	return 0;
}

static grk_image *  tgatoimage(const char *filename,
		 grk_cparameters  *parameters) {
	FILE *f;
	grk_image *image;
	unsigned int image_width, image_height, pixel_bit_depth;
	unsigned int x, y;
	int flip_image = 0;
	 grk_image_cmptparm  cmptparm[4]; /* maximum 4 components */
	uint32_t numcomps;
	GRK_COLOR_SPACE color_space;
	uint32_t subsampling_dx, subsampling_dy;
	uint32_t i;

	f = fopen(filename, "rb");
	if (!f) {
		spdlog::error("Failed to open {} for reading !!\n", filename);
		return 0;
	}

	if (!tga_readheader(f, &pixel_bit_depth, &image_width, &image_height,
			&flip_image)) {
		grk::safe_fclose(f);
		return nullptr;
	}

	/* We currently only support 24 & 32 bit tga's ... */
	if (!((pixel_bit_depth == 24) || (pixel_bit_depth == 32))) {
		grk::safe_fclose(f);
		return nullptr;
	}

	/* initialize image components */
	memset(&cmptparm[0], 0, 4 * sizeof( grk_image_cmptparm) );
	//bool mono = (pixel_bit_depth == 8) || (pixel_bit_depth == 16);  /* Mono with & without alpha. */
	bool save_alpha = (pixel_bit_depth == 16) || (pixel_bit_depth == 32); // Mono with alpha, or RGB with alpha
	/*if (mono) {
	 color_space = GRK_CLRSPC_GRAY;
	 numcomps = save_alpha ? 2 : 1;
	 }
	 else*/{
		numcomps = save_alpha ? 4 : 3;
		color_space = GRK_CLRSPC_SRGB;
	}

	subsampling_dx = parameters->subsampling_dx;
	subsampling_dy = parameters->subsampling_dy;

	for (i = 0; i < numcomps; i++) {
		cmptparm[i].prec = 8;
		cmptparm[i].sgnd = 0;
		cmptparm[i].dx = subsampling_dx;
		cmptparm[i].dy = subsampling_dy;
		cmptparm[i].w = image_width;
		cmptparm[i].h = image_height;
	}

	/* create the image */
	image = grk_image_create(numcomps, &cmptparm[0], color_space);
	if (!image) {
		grk::safe_fclose(f);
		return nullptr;
	}
	if (!sanityCheckOnImage(image, numcomps)) {
		grk_image_destroy(image);
		image = nullptr;
		goto cleanup;
	}

	/* set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->y0 = parameters->image_offset_y0;
	image->x1 =
			!image->x0 ?
					(image_width - 1) * subsampling_dx + 1 :
					image->x0 + (image_width - 1) * subsampling_dx + 1;
	image->y1 =
			!image->y0 ?
					(image_height - 1) * subsampling_dy + 1 :
					image->y0 + (image_height - 1) * subsampling_dy + 1;

	/* set image data */
	for (y = 0; y < image_height; y++) {
		int index;

		if (flip_image)
			index = (int) ((image_height - y - 1) * image_width);
		else
			index = (int) (y * image_width);

		if (numcomps == 3) {
			for (x = 0; x < image_width; x++) {
				uint8_t r, g, b;

				if (!fread(&b, 1, 1, f)) {
					spdlog::error(" fread return a number of element different from the expected.");
					grk_image_destroy(image);
					image = nullptr;
					goto cleanup;
				}
				if (!fread(&g, 1, 1, f)) {
					spdlog::error(" fread return a number of element different from the expected.");
					grk_image_destroy(image);
					image = nullptr;
					goto cleanup;
				}
				if (!fread(&r, 1, 1, f)) {
					spdlog::error(" fread return a number of element different from the expected.");
					grk_image_destroy(image);
					image = nullptr;
					goto cleanup;
				}

				image->comps[0].data[index] = r;
				image->comps[1].data[index] = g;
				image->comps[2].data[index] = b;
				index++;
			}
		} else if (numcomps == 4) {
			for (x = 0; x < image_width; x++) {
				uint8_t r, g, b, a;
				if (!fread(&b, 1, 1, f)) {
					spdlog::error(" fread return a number of element different from the expected.");
					grk_image_destroy(image);
					image = nullptr;
					goto cleanup;
				}
				if (!fread(&g, 1, 1, f)) {
					spdlog::error(" fread return a number of element different from the expected.");
					grk_image_destroy(image);
					image = nullptr;
					goto cleanup;
				}
				if (!fread(&r, 1, 1, f)) {
					spdlog::error(" fread return a number of element different from the expected.");
					grk_image_destroy(image);
					image = nullptr;
					goto cleanup;
				}
				if (!fread(&a, 1, 1, f)) {
					spdlog::error(" fread return a number of element different from the expected.");
					grk_image_destroy(image);
					grk::safe_fclose(f);
					return nullptr;
				}

				image->comps[0].data[index] = r;
				image->comps[1].data[index] = g;
				image->comps[2].data[index] = b;
				image->comps[3].data[index] = a;
				index++;
			}
		} else {
			spdlog::error("Currently unsupported bit depth : {}\n", filename);
		}
	}
	cleanup: if (!grk::safe_fclose(f)) {
		grk_image_destroy(image);
		image = nullptr;
	}
	return image;
}

static int imagetotga(grk_image *image, const char *outfile, bool verbose) {
	int width = 0, height = 0, bpp = 0, x = 0, y = 0;
	bool write_alpha = false;
	unsigned int i;
	int adjustR = 0, adjustG = 0, adjustB = 0, fails = 1;
	unsigned int alpha_channel;
	float r, g, b, a;
	uint8_t value;
	float scale = 0;
	FILE *fdest = nullptr;
	size_t res = 0;

	fdest = fopen(outfile, "wb");
	if (!fdest) {
		spdlog::error("failed to open {} for writing\n", outfile);
		goto beach;
	}

	if (!sanityCheckOnImage(image, image->numcomps)) {
		goto beach;
	}
	for (i = 0; i < image->numcomps; i++) {
		if (verbose)
			spdlog::info("Component %u characteristics: {}x{}x{} {}\n", i, image->comps[i].w,
				image->comps[i].h, image->comps[i].prec, image->comps[i].sgnd == 1 ? "signed" : "unsigned");

		if (!image->comps[i].data) {
			spdlog::error("imagetotga: component {} is null.",i);
			spdlog::error("\tAborting");
			goto beach;
		}
	}

	for (i = 0; i < image->numcomps - 1; i++) {
		if ((image->comps[0].dx != image->comps[i + 1].dx)
				|| (image->comps[0].dy != image->comps[i + 1].dy)
				|| (image->comps[0].prec != image->comps[i + 1].prec)
				|| (image->comps[0].sgnd != image->comps[i + 1].sgnd)) {

			spdlog::error("imagetotga: unable to create a tga file with such J2K image charateristics.");
			goto beach;
		}
	}

	width = (int) image->comps[0].w;
	height = (int) image->comps[0].h;

	/* Mono with alpha, or RGB with alpha. */
	write_alpha = (image->numcomps == 2) || (image->numcomps == 4);

	/* Write TGA header  */
	bpp = write_alpha ? 32 : 24;

	if (!tga_writeheader(fdest, bpp, width, height, true))
		goto beach;

	alpha_channel = image->numcomps - 1;

	scale = 255.0f / (float) ((1 << image->comps[0].prec) - 1);

	adjustR = (image->comps[0].sgnd ? 1 << (image->comps[0].prec - 1) : 0);
	if (image->numcomps >= 3) {
		adjustG = (image->comps[1].sgnd ? 1 << (image->comps[1].prec - 1) : 0);
		adjustB = (image->comps[2].sgnd ? 1 << (image->comps[2].prec - 1) : 0);
	}

	for (y = 0; y < height; y++) {
		unsigned int index = (unsigned int) (y * width);

		for (x = 0; x < width; x++, index++) {
			r = (float) (image->comps[0].data[index] + adjustR);

			if (image->numcomps > 2) {
				g = (float) (image->comps[1].data[index] + adjustG);
				b = (float) (image->comps[2].data[index] + adjustB);
			} else {
				/* Greyscale ... */
				g = r;
				b = r;
			}

			/* TGA format writes BGR ... */
			if (b > 255.)
				b = 255.;
			else if (b < 0.)
				b = 0.;
			value = (uint8_t) (b * scale);
			res = fwrite(&value, 1, 1, fdest);

			if (res < 1) {
				spdlog::error("imagetotga: failed to write 1 byte for {}\n", outfile);
				goto beach;
			}
			if (g > 255.)
				g = 255.;
			else if (g < 0.)
				g = 0.;
			value = (uint8_t) (g * scale);
			res = fwrite(&value, 1, 1, fdest);

			if (res < 1) {
				spdlog::error("failed to write 1 byte for {}\n",
						outfile);
				goto beach;
			}
			if (r > 255.)
				r = 255.;
			else if (r < 0.)
				r = 0.;
			value = (uint8_t) (r * scale);
			res = fwrite(&value, 1, 1, fdest);

			if (res < 1) {
				spdlog::error("imagetotga: failed to write 1 byte for {}\n",
						outfile);
				goto beach;
			}

			if (write_alpha) {
				a = (float) (image->comps[alpha_channel].data[index]);
				if (a > 255.)
					a = 255.;
				else if (a < 0.)
					a = 0.;
				value = (uint8_t) (a * scale);
				res = fwrite(&value, 1, 1, fdest);

				if (res < 1) {
					spdlog::error("imagetotga: failed to write 1 byte for {}\n",
							outfile);
					goto beach;
				}
			}
		}
	}
	fails = 0;
	beach:
		if (!grk::safe_fclose(fdest))
			fails = 1;
	return fails;
}

bool TGAFormat::encode(grk_image *image, const char *filename,
		int compressionParam, bool verbose) {
	(void) compressionParam;
	(void) verbose;
	return imagetotga(image, filename, verbose) ? false : true;
}
grk_image *  TGAFormat::decode(const char *filename,
		 grk_cparameters  *parameters) {
	return tgatoimage(filename, parameters);
}
