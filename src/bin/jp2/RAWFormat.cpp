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
#include "RAWFormat.h"
#include "convert.h"

bool RAWFormat::encode(opj_image_t* image, std::string filename, int compressionParam, bool verbose) {
	(void)compressionParam;
	(void)verbose;
	return encode_common(image, filename.c_str(), bigEndian) ? false : true;
}
opj_image_t* RAWFormat::decode(std::string filename, opj_cparameters_t *parameters) {
	return decode_common(filename.c_str(), parameters, bigEndian);
}

opj_image_t* RAWFormat::decode_common(const char *filename, opj_cparameters_t *parameters, bool big_endian)
{
	bool readFromStdin = ((filename == nullptr) || (filename[0] == 0));
	raw_cparameters_t *raw_cp = &parameters->raw_cp;
	uint32_t subsampling_dx = parameters->subsampling_dx;
	uint32_t subsampling_dy = parameters->subsampling_dy;

	FILE *f = nullptr;
	uint32_t i, compno, numcomps, w, h;
	OPJ_COLOR_SPACE color_space;
	opj_image_cmptparm_t *cmptparm;
	opj_image_t * image = nullptr;
	unsigned short ch;

	if (!(raw_cp->rawWidth && raw_cp->rawHeight && raw_cp->rawComp && raw_cp->rawBitDepth)) {
		fprintf(stderr, "[ERROR] invalid raw image parameters\n");
		fprintf(stderr, "Please use the Format option -F:\n");
		fprintf(stderr, "-F <width>,<height>,<ncomp>,<bitdepth>,{s,u}@<dx1>x<dy1>:...:<dxn>x<dyn>\n");
		fprintf(stderr, "If subsampling is omitted, 1x1 is assumed for all components\n");
		fprintf(stderr, "Example: -i image.raw -o image.j2k -F 512,512,3,8,u@1x1:2x2:2x2\n");
		fprintf(stderr, "         for raw 512x512 image with 4:2:0 subsampling\n");
		return nullptr;
	}

	if (readFromStdin) {
		if (!grok_set_binary_mode(stdin))
			return nullptr;
		f = stdin;
	}
	else {
		f = fopen(filename, "rb");
		if (!f) {
			fprintf(stderr, "[ERROR] Failed to open %s for reading !!\n", filename);
			return nullptr;
		}
	}
	numcomps = raw_cp->rawComp;
	if (numcomps == 1) {
		color_space = OPJ_CLRSPC_GRAY;
	}
	else if ((numcomps >= 3) && (parameters->tcp_mct == 0)) {
		color_space = OPJ_CLRSPC_SYCC;
	}
	else if ((numcomps >= 3) && (parameters->tcp_mct != 2)) {
		color_space = OPJ_CLRSPC_SRGB;
	}
	else {
		color_space = OPJ_CLRSPC_UNKNOWN;
	}
	w = raw_cp->rawWidth;
	h = raw_cp->rawHeight;
	cmptparm = (opj_image_cmptparm_t*)calloc(numcomps, sizeof(opj_image_cmptparm_t));
	if (!cmptparm) {
		fprintf(stderr, "[ERROR] Failed to allocate image components parameters !!\n");
		fclose(f);
		return nullptr;
	}
	/* initialize image components */
	for (i = 0; i < numcomps; i++) {
		cmptparm[i].prec = raw_cp->rawBitDepth;
		cmptparm[i].sgnd = raw_cp->rawSigned;
		cmptparm[i].dx = subsampling_dx * raw_cp->rawComps[i].dx;
		cmptparm[i].dy = subsampling_dy * raw_cp->rawComps[i].dy;
		cmptparm[i].w = w;
		cmptparm[i].h = h;
	}
	/* create the image */
	image = opj_image_create(numcomps, &cmptparm[0], color_space);
	free(cmptparm);
	if (!image) {
		fclose(f);
		return nullptr;
	}
	/* set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->y0 = parameters->image_offset_y0;
	image->x1 = parameters->image_offset_x0 + (w - 1) *	subsampling_dx + 1;
	image->y1 = parameters->image_offset_y0 + (h - 1) * subsampling_dy + 1;

	if (raw_cp->rawBitDepth <= 8) {
		unsigned char value = 0;
		for (compno = 0; compno < numcomps; compno++) {
			uint32_t nloop = (w*h) / (raw_cp->rawComps[compno].dx*raw_cp->rawComps[compno].dy);
			for (i = 0; i < nloop; i++) {
				if (!fread(&value, 1, 1, f)) {
					fprintf(stderr, "[ERROR] Error reading raw file. End of file probably reached.\n");
					opj_image_destroy(image);
					fclose(f);
					return nullptr;
				}
				image->comps[compno].data[i] = raw_cp->rawSigned ? (char)value : value;
			}
		}
	}
	else if (raw_cp->rawBitDepth <= 16) {
		unsigned short value;
		for (compno = 0; compno < numcomps; compno++) {
			uint32_t nloop = (w*h) / (raw_cp->rawComps[compno].dx*raw_cp->rawComps[compno].dy);
			for (i = 0; i < nloop; i++) {
				unsigned char temp1;
				unsigned char temp2;
				if (!fread(&temp1, 1, 1, f)) {
					fprintf(stderr, "[ERROR] Error reading raw file. End of file probably reached.\n");
					opj_image_destroy(image);
					fclose(f);
					return nullptr;
				}
				if (!fread(&temp2, 1, 1, f)) {
					fprintf(stderr, "[ERROR] Error reading raw file. End of file probably reached.\n");
					opj_image_destroy(image);
					fclose(f);
					return nullptr;
				}
				if (big_endian) {
					value = (unsigned short)((temp1 << 8) + temp2);
				}
				else {
					value = (unsigned short)((temp2 << 8) + temp1);
				}
				image->comps[compno].data[i] = raw_cp->rawSigned ? (short)value : value;
			}
		}
	}
	else {
		fprintf(stderr, "[ERROR] Grok cannot encode raw components with bit depth higher than 16 bits.\n");
		opj_image_destroy(image);
		fclose(f);
		return nullptr;
	}

	if (fread(&ch, 1, 1, f)) {
		fprintf(stdout, "[WARNING] End of raw file not reached... processing anyway\n");
	}
	fclose(f);

	return image;
}

int RAWFormat::encode_common(opj_image_t * image, const char *outfile, bool big_endian)
{
	bool writeToStdout = ((outfile == nullptr) || (outfile[0] == 0));
	FILE *rawFile = nullptr;
	size_t res;
	unsigned int compno, numcomps;
	int w, h, fails;
	int line, row, curr, mask;
	int *ptr;
	unsigned char uc;
	(void)big_endian;

	if ((image->numcomps * image->x1 * image->y1) == 0) {
		fprintf(stderr, "[ERROR] invalid raw image parameters\n");
		return 1;
	}

	numcomps = image->numcomps;

	if (numcomps > 4) {
		numcomps = 4;
	}

	for (compno = 1; compno < numcomps; ++compno) {
		if (image->comps[0].dx != image->comps[compno].dx) {
			break;
		}
		if (image->comps[0].dy != image->comps[compno].dy) {
			break;
		}
		if (image->comps[0].prec != image->comps[compno].prec) {
			break;
		}
		if (image->comps[0].sgnd != image->comps[compno].sgnd) {
			break;
		}
	}
	if (compno != numcomps) {
		fprintf(stderr, "[ERROR] imagetoraw_common: All components shall have the same subsampling, same bit depth, same sign.\n");
		return 1;
	}

	if (writeToStdout) {
		if (!grok_set_binary_mode(stdin))
			return 1;
		rawFile = stdout;
	}
	else {
		rawFile = fopen(outfile, "wb");
		if (!rawFile) {
			fprintf(stderr, "[ERROR] Failed to open %s for writing !!\n", outfile);
			return 1;
		}
	}

	fails = 1;
	fprintf(stdout, "Raw image characteristics: %d components\n", image->numcomps);

	for (compno = 0; compno < image->numcomps; compno++) {
		fprintf(stdout, "Component %u characteristics: %dx%dx%d %s\n", compno, image->comps[compno].w,
			image->comps[compno].h, image->comps[compno].prec, image->comps[compno].sgnd == 1 ? "signed" : "unsigned");

		w = (int)image->comps[compno].w;
		h = (int)image->comps[compno].h;

		if (image->comps[compno].prec <= 8) {
			if (image->comps[compno].sgnd == 1) {
				mask = (1 << image->comps[compno].prec) - 1;
				ptr = image->comps[compno].data;
				for (line = 0; line < h; line++) {
					for (row = 0; row < w; row++) {
						curr = *ptr;
						if (curr > 127) curr = 127;
						else if (curr < -128) curr = -128;
						uc = (unsigned char)(curr & mask);
						res = fwrite(&uc, 1, 1, rawFile);
						if (res < 1) {
							fprintf(stderr, "[ERROR] failed to write 1 byte for %s\n", outfile);
							goto beach;
						}
						ptr++;
					}
				}
			}
			else if (image->comps[compno].sgnd == 0) {
				mask = (1 << image->comps[compno].prec) - 1;
				ptr = image->comps[compno].data;
				for (line = 0; line < h; line++) {
					for (row = 0; row < w; row++) {
						curr = *ptr;
						if (curr > 255) curr = 255;
						else if (curr < 0) curr = 0;
						uc = (unsigned char)(curr & mask);
						res = fwrite(&uc, 1, 1, rawFile);
						if (res < 1) {
							fprintf(stderr, "[ERROR] failed to write 1 byte for %s\n", outfile);
							goto beach;
						}
						ptr++;
					}
				}
			}
		}
		else if (image->comps[compno].prec <= 16) {
			if (image->comps[compno].sgnd == 1) {
				union {
					signed short val;
					signed char vals[2];
				} uc16;
				mask = (1 << image->comps[compno].prec) - 1;
				ptr = image->comps[compno].data;
				for (line = 0; line < h; line++) {
					for (row = 0; row < w; row++) {
						curr = *ptr;
						if (curr > 32767) curr = 32767;
						else if (curr < -32768) curr = -32768;
						uc16.val = (signed short)(curr & mask);
						res = fwrite(uc16.vals, 1, 2, rawFile);
						if (res < 2) {
							fprintf(stderr, "[ERROR] failed to write 2 byte for %s\n", outfile);
							goto beach;
						}
						ptr++;
					}
				}
			}
			else if (image->comps[compno].sgnd == 0) {
				union {
					unsigned short val;
					unsigned char vals[2];
				} uc16;
				mask = (1 << image->comps[compno].prec) - 1;
				ptr = image->comps[compno].data;
				for (line = 0; line < h; line++) {
					for (row = 0; row < w; row++) {
						curr = *ptr;
						if (curr > 65535) curr = 65535;
						else if (curr < 0) curr = 0;
						uc16.val = (unsigned short)(curr & mask);
						res = fwrite(uc16.vals, 1, 2, rawFile);
						if (res < 2) {
							fprintf(stderr, "[ERROR] failed to write 2 byte for %s\n", outfile);
							goto beach;
						}
						ptr++;
					}
				}
			}
		}
		else if (image->comps[compno].prec <= 32) {
			fprintf(stderr, "[ERROR] More than 16 bits per component no handled yet\n");
			goto beach;
		}
		else {
			fprintf(stderr, "[ERROR] invalid precision: %d\n", image->comps[compno].prec);
			goto beach;
		}
	}
	fails = 0;
beach:
	if (!writeToStdout)
		fclose(rawFile);
	return fails;
}
