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

 /**
 Load a single image component encoded in PGX file format
 @param filename Name of the PGX file to load
 @param parameters *List ?*
 @return a greyscale image if successful, returns nullptr otherwise
 */



#include <cstdio>
#include <cstdlib>
#include "opj_apps_config.h"
#include "openjpeg.h"
#include "PGXFormat.h"
#include "convert.h"
#include <cstring>


static unsigned char readuchar(FILE * f)
{
	unsigned char c1;
	if (!fread(&c1, 1, 1, f)) {
		fprintf(stderr, "[ERROR] fread return a number of element different from the expected.\n");
		return 0;
	}
	return c1;
}

static unsigned short readushort(FILE * f, int bigendian)
{
	unsigned char c1, c2;
	if (!fread(&c1, 1, 1, f)) {
		fprintf(stderr, "[ERROR]  fread return a number of element different from the expected.\n");
		return 0;
	}
	if (!fread(&c2, 1, 1, f)) {
		fprintf(stderr, "[ERROR]  fread return a number of element different from the expected.\n");
		return 0;
	}
	if (bigendian)
		return (unsigned short)((c1 << 8) + c2);
	else
		return (unsigned short)((c2 << 8) + c1);
}

static unsigned int readuint(FILE * f, int bigendian)
{
	unsigned char c1, c2, c3, c4;
	if (!fread(&c1, 1, 1, f)) {
		fprintf(stderr, "[ERROR] fread return a number of element different from the expected.\n");
		return 0;
	}
	if (!fread(&c2, 1, 1, f)) {
		fprintf(stderr, "[ERROR] fread return a number of element different from the expected.\n");
		return 0;
	}
	if (!fread(&c3, 1, 1, f)) {
		fprintf(stderr, "[ERROR]  fread return a number of element different from the expected.\n");
		return 0;
	}
	if (!fread(&c4, 1, 1, f)) {
		fprintf(stderr, "[ERROR] fread return a number of element different from the expected.\n");
		return 0;
	}
	if (bigendian)
		return (unsigned int)(c1 << 24) + (unsigned int)(c2 << 16) + (unsigned int)(c3 << 8) + c4;
	else
		return (unsigned int)(c4 << 24) + (unsigned int)(c3 << 16) + (unsigned int)(c2 << 8) + c1;
}

static opj_image_t* pgxtoimage(const char *filename, opj_cparameters_t *parameters)
{
	FILE *f = nullptr;
	uint32_t w, h, prec, numcomps, max;
	uint64_t i, area;
	OPJ_COLOR_SPACE color_space;
	opj_image_cmptparm_t cmptparm;	/* maximum of 1 component  */
	opj_image_t * image = nullptr;
	int adjustS, ushift, dshift, force8;

	char endian1, endian2, sign;
	char signtmp[32];

	char temp[32];
	uint32_t bigendian;
	opj_image_comp_t *comp = nullptr;

	numcomps = 1;
	color_space = OPJ_CLRSPC_GRAY;

	memset(&cmptparm, 0, sizeof(opj_image_cmptparm_t));

	max = 0;

	f = fopen(filename, "rb");
	if (!f) {
		fprintf(stderr, "[ERROR] Failed to open %s for reading !\n", filename);
		return nullptr;
	}

	fseek(f, 0, SEEK_SET);
	if (fscanf(f, "PG%31[ \t]%c%c%31[ \t+-]%d%31[ \t]%d%31[ \t]%d", temp, &endian1, &endian2, signtmp, &prec, temp, &w, temp, &h) != 9) {
		fclose(f);
		fprintf(stderr, "[ERROR] Failed to read the right number of element from the fscanf() function!\n");
		return nullptr;
	}

	i = 0;
	sign = '+';
	while (signtmp[i] != '\0') {
		if (signtmp[i] == '-') sign = '-';
		i++;
	}

	fgetc(f);
	if (endian1 == 'M' && endian2 == 'L') {
		bigendian = 1;
	}
	else if (endian2 == 'M' && endian1 == 'L') {
		bigendian = 0;
	}
	else {
		fclose(f);
		fprintf(stderr, "[ERROR] Bad pgx header, please check input file\n");
		return nullptr;
	}

	/* initialize image component */

	cmptparm.x0 = parameters->image_offset_x0;
	cmptparm.y0 = parameters->image_offset_y0;
	cmptparm.w = !cmptparm.x0 ? ((w - 1) * parameters->subsampling_dx + 1) : cmptparm.x0 + (uint32_t)(w - 1) * parameters->subsampling_dx + 1;
	cmptparm.h = !cmptparm.y0 ? ((h - 1) * parameters->subsampling_dy + 1) : cmptparm.y0 + (uint32_t)(h - 1) * parameters->subsampling_dy + 1;

	if (sign == '-') {
		cmptparm.sgnd = 1;
	}
	else {
		cmptparm.sgnd = 0;
	}
	if (prec < 8) {
		force8 = 1;
		ushift = 8 - prec;
		dshift = prec - ushift;
		if (cmptparm.sgnd) adjustS = (1 << (prec - 1));
		else adjustS = 0;
		cmptparm.sgnd = 0;
		prec = 8;
	}
	else ushift = dshift = force8 = adjustS = 0;

	cmptparm.prec = prec;
	cmptparm.dx = parameters->subsampling_dx;
	cmptparm.dy = parameters->subsampling_dy;

	/* create the image */
	image = opj_image_create(numcomps, &cmptparm, color_space);
	if (!image) {
		fclose(f);
		return nullptr;
	}
	/* set image offset and reference grid */
	image->x0 = cmptparm.x0;
	image->y0 = cmptparm.x0;
	image->x1 = cmptparm.w;
	image->y1 = cmptparm.h;

	/* set image data */

	comp = &image->comps[0];
	area = (uint64_t)w * h;
	for (i = 0; i < area; i++) {
		uint32_t v;
		if (force8) {
			v = readuchar(f) + adjustS;
			v = (v << ushift) + (v >> dshift);
			comp->data[i] = (unsigned char)v;

			if (v > max)
				max = v;

			continue;
		}
		if (comp->prec == 8) {
			if (!comp->sgnd) {
				v = readuchar(f);
			}
			else {
				v = (char)readuchar(f);
			}
		}
		else if (comp->prec <= 16) {
			if (!comp->sgnd) {
				v = readushort(f, bigendian);
			}
			else {
				v = (short)readushort(f, bigendian);
			}
		}
		else {
			if (!comp->sgnd) {
				v = readuint(f, bigendian);
			}
			else {
				v = (int)readuint(f, bigendian);
			}
		}
		if (v > max)
			max = v;
		comp->data[i] = v;
	}
	fclose(f);
	return image;
}

#define CLAMP(x,a,b) x < a ? a : (x > b ? b : x)

static inline int clamp(const int value, const int prec, const int sgnd)
{
	if (sgnd) {
		if (prec <= 8)       return CLAMP(value, -128, 127);
		else if (prec <= 16) return CLAMP(value, -32768, 32767);
		else                 return CLAMP(value, -2147483647 - 1, 2147483647);
	}
	else {
		if (prec <= 8)       return CLAMP(value, 0, 255);
		else if (prec <= 16) return CLAMP(value, 0, 65535);
		else                 return value; /*CLAMP(value,0,4294967295);*/
	}
}

static int imagetopgx(opj_image_t * image, const char *outfile)
{
	uint32_t w, h;
	int j, fails = 1;
	unsigned int compno;
	FILE *fdest = nullptr;
	size_t total = 0;
	char *name = nullptr;
	for (compno = 0; compno < image->numcomps; compno++) {
		opj_image_comp_t *comp = &image->comps[compno];
		char bname[256]; /* buffer for name */
		bname[255] = '\0';
		name = bname; /* pointer */
		int nbytes = 0;
		size_t res;
		const size_t olen = strlen(outfile);
		if (olen > 4096) {
			fprintf(stderr, "[ERROR] imagetopgx: output file name larger than 4096.");
			goto beach;
		}
		const size_t dotpos = olen - 4;
		total = dotpos + 1 + 1 + 4; /* '-' + '[1-3]' + '.pgx' */

		if (outfile[dotpos] != '.') {
			/* `pgx` was recognized but there is no dot at expected position */
			fprintf(stderr, "[ERROR] The impossible happened.");
			goto beach;
		}
		if (total > 256) {
			name = (char*)malloc(total + 1);
			if (name == nullptr) {
				fprintf(stderr, "[ERROR] imagetopgx: out of memory\n");
				goto beach;
			}
		}
		//copy root outfile name to "name"
		memcpy(name, outfile, dotpos);
		//add new tag
		sprintf(name + dotpos, "_%u.pgx", compno);

		fdest = fopen(name, "wb");

		if (!fdest) {

			fprintf(stderr, "[ERROR] failed to open %s for writing\n", name);
			goto beach;
		}

		w = image->comps[compno].w;
		h = image->comps[compno].h;

		fprintf(fdest, "PG ML %c %d %d %d\n", comp->sgnd ? '-' : '+', comp->prec,
			w, h);

		if (comp->prec <= 8)
			nbytes = 1;
		else if (comp->prec <= 16)
			nbytes = 2;
		else
			nbytes = 4;

		for (uint64_t i = 0; i < (uint64_t)w * h; i++) {
			/* FIXME: clamp func is being called within a loop */
			const int val = clamp(image->comps[compno].data[i],
				(int)comp->prec, (int)comp->sgnd);

			for (j = nbytes - 1; j >= 0; j--) {
				int v = (int)(val >> (j * 8));
				unsigned char byte = (unsigned char)v;
				res = fwrite(&byte, 1, 1, fdest);

				if (res < 1) {
					fprintf(stderr, "[ERROR] failed to write 1 byte for %s\n", name);
					goto beach;
				}
			}
		}
		if (total > 256)
			free(name);
		fclose(fdest);
		fdest = nullptr;
	}
	fails = 0;
beach:
	if (name && total > 256)
		free(name);
	if (fdest)
		fclose(fdest);

	return fails;
}


bool PGXFormat::encode(opj_image_t* image, std::string filename, int compressionParam, bool verbose) {
	(void)compressionParam;
	(void)verbose;
	return imagetopgx(image, filename.c_str()) ? false : true;
}
opj_image_t*  PGXFormat::decode(std::string filename, opj_cparameters_t *parameters) {
	return pgxtoimage(filename.c_str(), parameters);
}
