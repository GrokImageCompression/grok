/*
*    Copyright (C) 2016-2018 Grok Image Compression Inc.
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
#include "opj_apps_config.h"
#include "openjpeg.h"
#include "TGAFormat.h"
#include "convert.h"
#include <cstring>



#ifdef INFORMATION_ONLY
 /* TGA header definition. */
struct tga_header {
	unsigned char   id_length;              /* Image id field length    */
	unsigned char   colour_map_type;        /* Colour map type          */
	unsigned char   image_type;             /* Image type               */
											/*
											** Colour map specification
											*/
	unsigned short  colour_map_index;       /* First entry index        */
	unsigned short  colour_map_length;      /* Colour map length        */
	unsigned char   colour_map_entry_size;  /* Colour map entry size    */
											/*
											** Image specification
											*/
	unsigned short  x_origin;               /* x origin of image        */
	unsigned short  y_origin;               /* u origin of image        */
	unsigned short  image_width;            /* Image width              */
	unsigned short  image_height;           /* Image height             */
	unsigned char   pixel_depth;            /* Pixel depth              */
	unsigned char   image_desc;             /* Image descriptor         */
};
#endif /* INFORMATION_ONLY */

static unsigned short get_ushort(const unsigned char *data)
{
	unsigned short val = *(const unsigned short *)data;
#ifdef GROK_BIG_ENDIAN
	val = ((val & 0xffU) << 8) | (val >> 8);
#endif
	return val;
}

#define TGA_HEADER_SIZE 18

static int tga_readheader(FILE *fp, unsigned int *bits_per_pixel,
	unsigned int *width, unsigned int *height, int *flip_image)
{
	int palette_size;
	unsigned char tga[TGA_HEADER_SIZE];
	unsigned char id_len, /*cmap_type,*/ image_type;
	unsigned char pixel_depth, image_desc;
	unsigned short /*cmap_index,*/ cmap_len, cmap_entry_size;
	unsigned short /*x_origin, y_origin,*/ image_w, image_h;

	if (!bits_per_pixel || !width || !height || !flip_image)
		return 0;

	if (fread(tga, TGA_HEADER_SIZE, 1, fp) != 1) {
		fprintf(stderr, "[ERROR] fread return a number of element different from the expected.\n");
		return 0;
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

	*bits_per_pixel = (unsigned int)pixel_depth;
	*width = (unsigned int)image_w;
	*height = (unsigned int)image_h;

	/* Ignore tga identifier, if present ... */
	if (id_len) {
		unsigned char *id = (unsigned char *)malloc(id_len);
		if (!id) {
			fprintf(stderr, "[ERROR] tga_readheader: out of memory out\n");
			return 0;
		}
		if (!fread(id, id_len, 1, fp)) {
			fprintf(stderr, "[ERROR] fread return a number of element different from the expected.\n");
			free(id);
			return 0;
		}
		free(id);
	}

	/* Test for compressed formats ... not yet supported ...
	// Note :-  9 - RLE encoded palettized.
	//	  	   10 - RLE encoded RGB. */
	if (image_type > 8) {
		fprintf(stderr, "[ERROR] Sorry, compressed tga files are not currently supported.\n");
		return 0;
	}

	*flip_image = !(image_desc & 32);

	/* Palettized formats are not yet supported, skip over the palette, if present ... */
	palette_size = cmap_len * (cmap_entry_size / 8);

	if (palette_size>0) {
		fprintf(stderr, "[ERROR] File contains a palette - not yet supported.");
		fseek(fp, palette_size, SEEK_CUR);
	}
	return 1;
}

#ifdef GROK_BIG_ENDIAN

static inline uint16_t swap16(uint16_t x)
{
	return (uint16_t)(((x & 0x00ffU) << 8) | ((x & 0xff00U) >> 8));
}

#endif

static int tga_writeheader(FILE *fp, int bits_per_pixel, int width, int height,
	bool flip_image)
{
	uint16_t image_w, image_h, us0;
	unsigned char uc0, image_type;
	unsigned char pixel_depth, image_desc;

	if (!bits_per_pixel || !width || !height)
		return 0;

	pixel_depth = 0;

	if (bits_per_pixel < 256)
		pixel_depth = (unsigned char)bits_per_pixel;
	else {
		fprintf(stderr, "[ERROR] Wrong bits per pixel inside tga_header");
		return 0;
	}
	uc0 = 0;

	if (fwrite(&uc0, 1, 1, fp) != 1) goto fails; /* id_length */
	if (fwrite(&uc0, 1, 1, fp) != 1) goto fails; /* colour_map_type */

	image_type = 2; /* Uncompressed. */
	if (fwrite(&image_type, 1, 1, fp) != 1) goto fails;

	us0 = 0;
	if (fwrite(&us0, 2, 1, fp) != 1) goto fails; /* colour_map_index */
	if (fwrite(&us0, 2, 1, fp) != 1) goto fails; /* colour_map_length */
	if (fwrite(&uc0, 1, 1, fp) != 1) goto fails; /* colour_map_entry_size */

	if (fwrite(&us0, 2, 1, fp) != 1) goto fails; /* x_origin */
	if (fwrite(&us0, 2, 1, fp) != 1) goto fails; /* y_origin */

	image_w = (unsigned short)width;
	image_h = (unsigned short)height;

#ifndef GROK_BIG_ENDIAN
	if (fwrite(&image_w, 2, 1, fp) != 1) goto fails;
	if (fwrite(&image_h, 2, 1, fp) != 1) goto fails;
#else
	image_w = swap16(image_w);
	image_h = swap16(image_h);
	if (fwrite(&image_w, 2, 1, fp) != 1) goto fails;
	if (fwrite(&image_h, 2, 1, fp) != 1) goto fails;
#endif

	if (fwrite(&pixel_depth, 1, 1, fp) != 1) goto fails;

	image_desc = 8; /* 8 bits per component. */

	if (flip_image)
		image_desc |= 32;
	if (fwrite(&image_desc, 1, 1, fp) != 1) goto fails;

	return 1;

fails:
	fputs("\nwrite_tgaheader: write ERROR\n", stderr);
	return 0;
}

static opj_image_t* tgatoimage(const char *filename, opj_cparameters_t *parameters)
{
	FILE *f;
	opj_image_t *image;
	unsigned int image_width, image_height, pixel_bit_depth;
	unsigned int x, y;
	int flip_image = 0;
	opj_image_cmptparm_t cmptparm[4];	/* maximum 4 components */
	uint32_t numcomps;
	OPJ_COLOR_SPACE color_space;
	bool mono;
	bool save_alpha;
	uint32_t subsampling_dx, subsampling_dy;
	uint32_t i;

	f = fopen(filename, "rb");
	if (!f) {
		fprintf(stderr, "[ERROR] Failed to open %s for reading !!\n", filename);
		return 0;
	}

	if (!tga_readheader(f, &pixel_bit_depth, &image_width, &image_height, &flip_image)) {
		fclose(f);
		return nullptr;
	}

	/* We currently only support 24 & 32 bit tga's ... */
	if (!((pixel_bit_depth == 24) || (pixel_bit_depth == 32))) {
		fclose(f);
		return nullptr;
	}

	/* initialize image components */
	memset(&cmptparm[0], 0, 4 * sizeof(opj_image_cmptparm_t));

	mono = (pixel_bit_depth == 8) || (pixel_bit_depth == 16);  /* Mono with & without alpha. */
	save_alpha = (pixel_bit_depth == 16) || (pixel_bit_depth == 32); /* Mono with alpha, or RGB with alpha */

	if (mono) {
		color_space = OPJ_CLRSPC_GRAY;
		numcomps = save_alpha ? 2 : 1;
	}
	else {
		numcomps = save_alpha ? 4 : 3;
		color_space = OPJ_CLRSPC_SRGB;
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
	image = opj_image_create(numcomps, &cmptparm[0], color_space);

	if (!image) {
		fclose(f);
		return nullptr;
	}

	if (!sanityCheckOnImage(image, numcomps)) {
		fclose(f);
		return nullptr;
	}

	/* set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->y0 = parameters->image_offset_y0;
	image->x1 = !image->x0 ? (image_width - 1)  * subsampling_dx + 1 : image->x0 + (image_width - 1)  * subsampling_dx + 1;
	image->y1 = !image->y0 ? (image_height - 1) * subsampling_dy + 1 : image->y0 + (image_height - 1) * subsampling_dy + 1;

	/* set image data */
	for (y = 0; y < image_height; y++) {
		int index;

		if (flip_image)
			index = (int)((image_height - y - 1)*image_width);
		else
			index = (int)(y*image_width);

		if (numcomps == 3) {
			for (x = 0; x<image_width; x++) {
				unsigned char r, g, b;

				if (!fread(&b, 1, 1, f)) {
					fprintf(stderr, "[ERROR] fread return a number of element different from the expected.\n");
					opj_image_destroy(image);
					fclose(f);
					return nullptr;
				}
				if (!fread(&g, 1, 1, f)) {
					fprintf(stderr, "[ERROR] fread return a number of element different from the expected.\n");
					opj_image_destroy(image);
					fclose(f);
					return nullptr;
				}
				if (!fread(&r, 1, 1, f)) {
					fprintf(stderr, "[ERROR] fread return a number of element different from the expected.\n");
					opj_image_destroy(image);
					fclose(f);
					return nullptr;
				}

				image->comps[0].data[index] = r;
				image->comps[1].data[index] = g;
				image->comps[2].data[index] = b;
				index++;
			}
		}
		else if (numcomps == 4) {
			for (x = 0; x<image_width; x++) {
				unsigned char r, g, b, a;
				if (!fread(&b, 1, 1, f)) {
					fprintf(stderr, "[ERROR] fread return a number of element different from the expected.\n");
					opj_image_destroy(image);
					fclose(f);
					return nullptr;
				}
				if (!fread(&g, 1, 1, f)) {
					fprintf(stderr, "[ERROR] fread return a number of element different from the expected.\n");
					opj_image_destroy(image);
					fclose(f);
					return nullptr;
				}
				if (!fread(&r, 1, 1, f)) {
					fprintf(stderr, "[ERROR] fread return a number of element different from the expected.\n");
					opj_image_destroy(image);
					fclose(f);
					return nullptr;
				}
				if (!fread(&a, 1, 1, f)) {
					fprintf(stderr, "[ERROR] fread return a number of element different from the expected.\n");
					opj_image_destroy(image);
					fclose(f);
					return nullptr;
				}

				image->comps[0].data[index] = r;
				image->comps[1].data[index] = g;
				image->comps[2].data[index] = b;
				image->comps[3].data[index] = a;
				index++;
			}
		}
		else {
			fprintf(stderr, "Currently unsupported bit depth : %s\n", filename);
		}
	}
	fclose(f);
	return image;
}

static int imagetotga(opj_image_t * image, const char *outfile)
{
	int width, height, bpp, x, y;
	bool write_alpha;
	unsigned int i;
	int adjustR, adjustG, adjustB, fails;
	unsigned int alpha_channel;
	float r, g, b, a;
	unsigned char value;
	float scale;
	FILE *fdest;
	size_t res;
	fails = 1;

	fdest = fopen(outfile, "wb");
	if (!fdest) {
		fprintf(stderr, "[ERROR] failed to open %s for writing\n", outfile);
		return 1;
	}

	if (!sanityCheckOnImage(image, image->numcomps)) {
		return -1;
	}

	for (i = 0; i < image->numcomps - 1; i++) {
		if ((image->comps[0].dx != image->comps[i + 1].dx)
			|| (image->comps[0].dy != image->comps[i + 1].dy)
			|| (image->comps[0].prec != image->comps[i + 1].prec)
			|| (image->comps[0].sgnd != image->comps[i + 1].sgnd)) {

			fclose(fdest);
			fprintf(stderr, "[ERROR] Unable to create a tga file with such J2K image charateristics.");
			return 1;
		}
	}

	width = (int)image->comps[0].w;
	height = (int)image->comps[0].h;

	/* Mono with alpha, or RGB with alpha. */
	write_alpha = (image->numcomps == 2) || (image->numcomps == 4);

	/* Write TGA header  */
	bpp = write_alpha ? 32 : 24;

	if (!tga_writeheader(fdest, bpp, width, height, true))
		goto beach;

	alpha_channel = image->numcomps - 1;

	scale = 255.0f / (float)((1 << image->comps[0].prec) - 1);

	adjustR = (image->comps[0].sgnd ? 1 << (image->comps[0].prec - 1) : 0);
	adjustG = (image->comps[1].sgnd ? 1 << (image->comps[1].prec - 1) : 0);
	adjustB = (image->comps[2].sgnd ? 1 << (image->comps[2].prec - 1) : 0);

	for (y = 0; y < height; y++) {
		unsigned int index = (unsigned int)(y*width);

		for (x = 0; x < width; x++, index++) {
			r = (float)(image->comps[0].data[index] + adjustR);

			if (image->numcomps > 2) {
				g = (float)(image->comps[1].data[index] + adjustG);
				b = (float)(image->comps[2].data[index] + adjustB);
			}
			else {
				/* Greyscale ... */
				g = r;
				b = r;
			}

			/* TGA format writes BGR ... */
			if (b > 255.) b = 255.;
			else if (b < 0.) b = 0.;
			value = (unsigned char)(b*scale);
			res = fwrite(&value, 1, 1, fdest);

			if (res < 1) {
				fprintf(stderr, "failed to write 1 byte for %s\n", outfile);
				goto beach;
			}
			if (g > 255.) g = 255.;
			else if (g < 0.) g = 0.;
			value = (unsigned char)(g*scale);
			res = fwrite(&value, 1, 1, fdest);

			if (res < 1) {
				fprintf(stderr, "[ERROR] failed to write 1 byte for %s\n", outfile);
				goto beach;
			}
			if (r > 255.) r = 255.;
			else if (r < 0.) r = 0.;
			value = (unsigned char)(r*scale);
			res = fwrite(&value, 1, 1, fdest);

			if (res < 1) {
				fprintf(stderr, "[ERROR] failed to write 1 byte for %s\n", outfile);
				goto beach;
			}

			if (write_alpha) {
				a = (float)(image->comps[alpha_channel].data[index]);
				if (a > 255.) a = 255.;
				else if (a < 0.) a = 0.;
				value = (unsigned char)(a*scale);
				res = fwrite(&value, 1, 1, fdest);

				if (res < 1) {
					fprintf(stderr, "[ERROR] failed to write 1 byte for %s\n", outfile);
					goto beach;
				}
			}
		}
	}
	fails = 0;
beach:
	fclose(fdest);

	return fails;
}


bool TGAFormat::encode(opj_image_t* image, std::string filename, int compressionParam, bool verbose) {
	(void)compressionParam;
	(void)verbose;
	return imagetotga(image, filename.c_str()) ? false : true;
}
opj_image_t*  TGAFormat::decode(std::string filename, opj_cparameters_t *parameters) {
	return tgatoimage(filename.c_str(), parameters);
}
