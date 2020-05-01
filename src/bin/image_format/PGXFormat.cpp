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

/**
 Load a single image component encoded in PGX file format
 @param filename Name of the PGX file to load
 @param parameters *List ?*
 @return a greyscale image if successful, returns nullptr otherwise
 */
#include "common.h"
#include <cstdio>
#include <cstdlib>
#include "grk_apps_config.h"
#include "grok.h"
#include "PGXFormat.h"
#include "convert.h"
#include <cstring>
#include <cassert>

static uint8_t readuchar(FILE *f) {
	uint8_t c1;
	if (!fread(&c1, 1, 1, f)) {
		spdlog::error(
				" fread return a number of element different from the expected.");
		return 0;
	}
	return c1;
}

static unsigned short readushort(FILE *f, int bigendian) {
	uint8_t c1, c2;
	if (!fread(&c1, 1, 1, f)) {
		spdlog::error(
				"  fread return a number of element different from the expected.");
		return 0;
	}
	if (!fread(&c2, 1, 1, f)) {
		spdlog::error(
				"  fread return a number of element different from the expected.");
		return 0;
	}
	if (bigendian)
		return (unsigned short) ((c1 << 8) + c2);
	else
		return (unsigned short) ((c2 << 8) + c1);
}

static grk_image* pgxtoimage(const char *filename,
		grk_cparameters *parameters) {
	FILE *f = nullptr;
	uint32_t w, h, prec, numcomps;
	int32_t max;
	uint64_t i, area;
	GRK_COLOR_SPACE color_space;
	grk_image_cmptparm cmptparm; /* maximum of 1 component  */
	grk_image *image = nullptr;
	uint32_t adjustS, ushift, dshift;
	bool force8 = false;;
	int c;
	char endian1, endian2, sign;
	char signtmp[32];

	char temp[32];
	uint32_t bigendian;
	grk_image_comp *comp = nullptr;

	numcomps = 1;
	color_space = GRK_CLRSPC_GRAY;

	memset(&cmptparm, 0, sizeof(grk_image_cmptparm));
	max = 0;
	f = fopen(filename, "rb");
	if (!f) {
		spdlog::error("Failed to open {} for reading !", filename);
		return nullptr;
	}

	if (fseek(f, 0, SEEK_SET))
		goto cleanup;
	if (fscanf(f, "PG%31[ \t]%c%c%31[ \t+-]%d%31[ \t]%d%31[ \t]%d", temp,
			&endian1, &endian2, signtmp, &prec, temp, &w, temp, &h) != 9) {
		spdlog::error(
				" Failed to read the right number of element from the fscanf() function!");
		goto cleanup;
	}

	if (prec < 4){
		spdlog::error("Precision must be >= 4");
		goto cleanup;
	}

	i = 0;
	sign = '+';
	while (signtmp[i] != '\0') {
		if (signtmp[i] == '-')
			sign = '-';
		i++;
	}

	c = fgetc(f);
	if (c == EOF)
		goto cleanup;
	if (endian1 == 'M' && endian2 == 'L') {
		bigendian = 1;
	} else if (endian2 == 'M' && endian1 == 'L') {
		bigendian = 0;
	} else {
		spdlog::error("Bad pgx header, please check input file");
		goto cleanup;
	}

	/* initialize image component */

	cmptparm.x0 = parameters->image_offset_x0;
	cmptparm.y0 = parameters->image_offset_y0;
	cmptparm.w =
			!cmptparm.x0 ?
					((w - 1) * parameters->subsampling_dx + 1) :
					cmptparm.x0
							+ (uint32_t) (w - 1) * parameters->subsampling_dx
							+ 1;
	cmptparm.h =
			!cmptparm.y0 ?
					((h - 1) * parameters->subsampling_dy + 1) :
					cmptparm.y0
							+ (uint32_t) (h - 1) * parameters->subsampling_dy
							+ 1;

	if (sign == '-') {
		cmptparm.sgnd = 1;
	} else {
		cmptparm.sgnd = 0;
	}
	if (prec < 8) {
		force8 = true;
		ushift = (uint32_t)(8 - prec);
		dshift = (uint32_t)(prec - ushift);
		if (cmptparm.sgnd)
			adjustS = (1 << (prec - 1));
		else
			adjustS = 0;
		cmptparm.sgnd = 0;
		prec = 8;
	} else{
		ushift = dshift =  adjustS = 0;
		force8 = false;
	}

	cmptparm.prec = prec;
	cmptparm.dx = parameters->subsampling_dx;
	cmptparm.dy = parameters->subsampling_dy;

	/* create the image */
	image = grk_image_create(numcomps, &cmptparm, color_space);
	if (!image) {
		goto cleanup;
	}
	/* set image offset and reference grid */
	image->x0 = cmptparm.x0;
	image->y0 = cmptparm.x0;
	image->x1 = cmptparm.w;
	image->y1 = cmptparm.h;

	/* set image data */
	comp = &image->comps[0];
	area = (uint64_t) w * h;
	for (i = 0; i < area; i++) {
		int32_t v = 0;
		if (force8) {
			v = readuchar(f) + adjustS;
			v = (v << ushift) + (v >> dshift);
		} else  {
			if (comp->prec == 8) {
				if (!comp->sgnd) {
					v = readuchar(f);
				} else {
					v = readuchar(f);
				}
			} else {
				if (!comp->sgnd) {
					v = readushort(f, bigendian);
				} else {
					v = readushort(f, bigendian);
				}
			}
		}
		if (v > max)
			max = v;
		comp->data[i] = v;
	}
	cleanup: if (!grk::safe_fclose(f)) {
		grk_image_destroy(image);
		image = nullptr;
	}
	return image;
}

static int imagetopgx(grk_image *image, const char *outfile) {
	int fails = 1;
	FILE *fdest = nullptr;
	for (uint32_t compno = 0; compno < image->numcomps; compno++) {
		grk_image_comp *comp = &image->comps[compno];
		char bname[4096]; /* buffer for name */
		bname[4095] = '\0';
		int nbytes = 0;
		const size_t olen = strlen(outfile);
		if (olen > 4096) {
			spdlog::error(
					" imagetopgx: output file name size larger than 4096.");
			goto beach;
		}
		if (olen < 4) {
			spdlog::error(" imagetopgx: output file name size less than 4.");
			goto beach;
		}
		const size_t dotpos = olen - 4;
		if (outfile[dotpos] != '.') {
			spdlog::error(
					" pgx was recognized but there was no dot at expected position .");
			goto beach;
		}
		//copy root outfile name to "name"
		memcpy(bname, outfile, dotpos);
		//add new tag
		sprintf(bname + dotpos, "_%u.pgx", compno);
		fdest = fopen(bname, "wb");
		if (!fdest) {

			spdlog::error("failed to open {} for writing", bname);
			goto beach;
		}

		uint32_t w = image->comps[compno].w;
		uint32_t h = image->comps[compno].h;

		fprintf(fdest, "PG ML %c %d %d %d\n", comp->sgnd ? '-' : '+',
				comp->prec, w, h);

		if (comp->prec <= 8)
			nbytes = 1;
		else if (comp->prec <= 16)
			nbytes = 2;

		const size_t bufSize = 4096;
		uint64_t area = (uint64_t) w * h;
		size_t outCount = 0;
		if (nbytes == 1){
			uint8_t buf[bufSize];
			uint8_t *outPtr = buf;
			for (uint64_t i = 0; i < area; i++) {
				const int val = image->comps[compno].data[i];
				if (!grk::writeBytes<uint8_t>((uint8_t) val, buf, &outPtr,
						&outCount, bufSize, true, fdest)) {
					spdlog::error("failed to write bytes for {}", bname);
					goto beach;
				}
			}
			if (outCount) {
				size_t res = fwrite(buf, sizeof(uint8_t), outCount, fdest);
				if (res != outCount) {
					spdlog::error("failed to write bytes for {}", bname);
					goto beach;
				}
			}

		} else {
			uint16_t buf[bufSize];
			uint16_t *outPtr = buf;
			for (uint64_t i = 0; i < area; i++) {
				const int val = image->comps[compno].data[i];
				if (!grk::writeBytes<uint16_t>((uint16_t) val, buf, &outPtr,
						&outCount, bufSize, true, fdest)) {
					spdlog::error("failed to write bytes for {}", bname);
					goto beach;
				}
			}
			if (outCount) {
				size_t res = fwrite(buf, sizeof(uint16_t), outCount, fdest);
				if (res != outCount) {
					spdlog::error("failed to write bytes for {}", bname);
					goto beach;
				}
			}
		}
		if (!grk::safe_fclose(fdest)) {
			fdest = nullptr;
			goto beach;
		}
		fdest = nullptr;
	}
	fails = 0;
	beach: if (!grk::safe_fclose(fdest)) {
		fails = 1;
	}
	return fails;
}

bool PGXFormat::encode(grk_image *image, const char *filename,
		int compressionParam, bool verbose) {
	(void) compressionParam;
	(void) verbose;
	return imagetopgx(image, filename) ? false : true;
}
grk_image* PGXFormat::decode(const char *filename,
		grk_cparameters *parameters) {
	return pgxtoimage(filename, parameters);
}
