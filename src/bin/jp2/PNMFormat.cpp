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
#include "PNMFormat.h"
#include "convert.h"
#include <cstring>
#include "common.h"

struct pnm_header {
	int width, height, maxval, depth, format;
	char rgb, rgba, gray, graya, bw;
	char ok;
};

static char* skip_white(char *s) {
	if (!s)
		return nullptr;
	while (*s) {
		if (*s == '\n' || *s == '\r')
			return nullptr;
		if (isspace(*s)) {
			++s;
			continue;
		}
		return s;
	}
	return nullptr;
}

static char* skip_int(char *start, int *out_n) {
	char *s;
	char c;

	*out_n = 0;

	s = skip_white(start);
	if (s == nullptr)
		return nullptr;
	start = s;

	while (*s) {
		if (!isdigit(*s))
			break;
		++s;
	}
	c = *s;
	*s = 0;
	*out_n = atoi(start);
	*s = c;
	return s;
}

static char* skip_idf(char *start, char out_idf[256]) {
	char *s;
	char c;

	s = skip_white(start);
	if (s == nullptr)
		return nullptr;
	start = s;

	while (*s) {
		if (isalpha(*s) || *s == '_') {
			++s;
			continue;
		}
		break;
	}
	c = *s;
	*s = 0;
	strncpy(out_idf, start, 255);
	*s = c;
	return s;
}

static void read_pnm_header(FILE *reader, struct pnm_header *ph) {
	int format, end, ttype;
	char idf[256], type[256];
	char line[256];

	if (fgets(line, 250, reader) == nullptr) {
		spdlog::error(" fgets returned nullptr");
		return;
	}

	if (line[0] != 'P') {
		spdlog::error("read_pnm_header:PNM:magic P missing");
		return;
	}
	format = atoi(line + 1);
	if (format < 1 || format > 7) {
		spdlog::error("read_pnm_header:magic format {} invalid", format);
		return;
	}
	ph->format = format;
	ttype = end = 0;

	while (fgets(line, 250, reader)) {
		int allow_null = 0;
		if (*line == '#' || *line == '\n')
			continue;

		char *s = line;
		if (format == 7) {
			s = skip_idf(s, idf);
			if (!s || *s == 0) {
				spdlog::error("Skip idf returned null");
				return;
			}

			if (strcmp(idf, "ENDHDR") == 0) {
				end = 1;
				break;
			}
			if (strcmp(idf, "WIDTH") == 0) {
				s = skip_int(s, &ph->width);
				if (!s || *s == 0 || ph->width < 0) {
					spdlog::error("Invalid width");
					return;
				}

				continue;
			}
			if (strcmp(idf, "HEIGHT") == 0) {
				s = skip_int(s, &ph->height);
				if (!s || *s == 0 || ph->height < 0) {
					spdlog::error("Invalid height");
					return;
				}

				continue;
			}
			if (strcmp(idf, "DEPTH") == 0) {
				s = skip_int(s, &ph->depth);
				if (!s || *s == 0 || ph->depth < 0) {
					spdlog::error("Invalid depth");
					return;
				}

				continue;
			}
			if (strcmp(idf, "MAXVAL") == 0) {
				s = skip_int(s, &ph->maxval);
				if (!s || *s == 0 || ph->maxval < 0) {
					spdlog::error("Invalid max val");
					return;
				}
				continue;
			}
			if (strcmp(idf, "TUPLTYPE") == 0) {
				s = skip_idf(s, type);
				if (!s || *s == 0) {
					spdlog::error("Skip idf returned null");
					return;
				}

				if (strcmp(type, "BLACKANDWHITE") == 0) {
					ph->bw = 1;
					ttype = 1;
					continue;
				}
				if (strcmp(type, "GRAYSCALE") == 0) {
					ph->gray = 1;
					ttype = 1;
					continue;
				}
				if (strcmp(type, "GRAYSCALE_ALPHA") == 0) {
					ph->graya = 1;
					ttype = 1;
					continue;
				}
				if (strcmp(type, "RGB") == 0) {
					ph->rgb = 1;
					ttype = 1;
					continue;
				}
				if (strcmp(type, "RGB_ALPHA") == 0) {
					ph->rgba = 1;
					ttype = 1;
					continue;
				}
				spdlog::error(" read_pnm_header:unknown P7 TUPLTYPE %s\n",
						type);
				return;
			}
			spdlog::error("read_pnm_header:unknown P7 idf {}\n", idf);
			return;
		} /* if(format == 7) */

		/* Here format is in range [1,6] */
		if (ph->width == 0) {
			s = skip_int(s, &ph->width);
			if ((!s) || (*s == 0) || (ph->width < 1)) {
				spdlog::error("Invalid width {}",
						(s && *s != 0) ? ph->width : -1);
				return;
			}
			allow_null = 1;
		}
		if (ph->height == 0) {
			s = skip_int(s, &ph->height);
			if ((s == nullptr) && allow_null)
				continue;
			if (!s || (*s == 0) || (ph->height < 1)) {
				spdlog::error("Invalid height {}",
						(s && *s != 0) ? ph->height : -1);
				return;
			}
			if (format == 1 || format == 4) {
				break;
			}
			allow_null = 1;
		}
		/* here, format is in P2, P3, P5, P6 */
		s = skip_int(s, &ph->maxval);
		if (!s && allow_null)
			continue;
		if (!s || (*s == 0))
			return;
		break;
	}/* while(fgets( ) */
	if (format == 2 || format == 3 || format > 4) {
		if (ph->maxval < 1 || ph->maxval > 65535) {
			spdlog::error("Invalid max value {}", ph->maxval);
			return;
		}
	}
	if (ph->width < 1 || ph->height < 1) {
		spdlog::error("Invalid width or height");
		return;
	}

	if (format == 7) {
		if (!end) {
			spdlog::error("read_pnm_header:P7 without ENDHDR");
			return;
		}
		if (ph->depth < 1 || ph->depth > 4) {
			spdlog::error("Invalid depth {}", ph->depth);
			return;
		}

		if (ttype)
			ph->ok = 1;
	} else {
		ph->ok = 1;
		if (format == 1 || format == 4) {
			ph->maxval = 255;
		}
	}
}

static int has_prec(int val) {
	if (val < 2)
		return 1;
	if (val < 4)
		return 2;
	if (val < 8)
		return 3;
	if (val < 16)
		return 4;
	if (val < 32)
		return 5;
	if (val < 64)
		return 6;
	if (val < 128)
		return 7;
	if (val < 256)
		return 8;
	if (val < 512)
		return 9;
	if (val < 1024)
		return 10;
	if (val < 2048)
		return 11;
	if (val < 4096)
		return 12;
	if (val < 8192)
		return 13;
	if (val < 16384)
		return 14;
	if (val < 32768)
		return 15;
	return 16;
}

static grk_image* pnmtoimage(const char *filename,
		grk_cparameters *parameters) {
	uint32_t subsampling_dx = parameters->subsampling_dx;
	uint32_t subsampling_dy = parameters->subsampling_dy;

	FILE *fp = nullptr;
	uint32_t compno, numcomps, w, h, prec, format;
	GRK_COLOR_SPACE color_space;
	grk_image_cmptparm cmptparm[4]; /* RGBA: max. 4 components */
	grk_image *image = nullptr;
	struct pnm_header header_info;
	uint64_t area = 0;

	if ((fp = fopen(filename, "rb")) == nullptr) {
		spdlog::error("pnmtoimage:Failed to open {} for reading!\n", filename);
		return nullptr;
	}
	memset(&header_info, 0, sizeof(struct pnm_header));
	read_pnm_header(fp, &header_info);
	if (!header_info.ok) {
		spdlog::error("Invalid PNM header");
		goto cleanup;
	}
	format = header_info.format;
	switch (format) {
	case 1: /* ascii bitmap */
	case 4: /* raw bitmap */
		numcomps = 1;
		break;

	case 2: /* ascii greymap */
	case 5: /* raw greymap */
		numcomps = 1;
		break;

	case 3: /* ascii pixmap */
	case 6: /* raw pixmap */
		numcomps = 3;
		break;

	case 7: /* arbitrary map */
		numcomps = header_info.depth;
		break;

	default:
		goto cleanup;
	}
	if (numcomps < 3)
		color_space = GRK_CLRSPC_GRAY;/* GRAY, GRAYA */
	else
		color_space = GRK_CLRSPC_SRGB;/* RGB, RGBA */

	prec = has_prec(header_info.maxval);

	if (prec < 8)
		prec = 8;

	w = header_info.width;
	h = header_info.height;
	area = (uint64_t) w * h;
	subsampling_dx = parameters->subsampling_dx;
	subsampling_dy = parameters->subsampling_dy;

	memset(&cmptparm[0], 0, (size_t) numcomps * sizeof(grk_image_cmptparm));

	for (uint32_t i = 0; i < numcomps; i++) {
		cmptparm[i].prec = prec;
		cmptparm[i].sgnd = 0;
		cmptparm[i].dx = subsampling_dx;
		cmptparm[i].dy = subsampling_dy;
		cmptparm[i].w = w;
		cmptparm[i].h = h;
	}
	image = grk_image_create(numcomps, &cmptparm[0], color_space);
	if (!image) {
		goto cleanup;
	}

	/* set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->y0 = parameters->image_offset_y0;
	image->x1 = (parameters->image_offset_x0 + (w - 1) * subsampling_dx + 1);
	image->y1 = (parameters->image_offset_y0 + (h - 1) * subsampling_dy + 1);

	if ((format == 2) || (format == 3)) { /* ascii pixmap */
		unsigned int index;

		for (uint64_t i = 0; i < area; i++) {
			for (compno = 0; compno < numcomps; compno++) {
				index = 0;
				if (fscanf(fp, "%u", &index) != 1) {
					if (parameters->verbose)
						spdlog::warn(
								"fscanf return a number of element different from the expected.");
				}

				image->comps[compno].data[i] = (int32_t) (index * 255)
						/ header_info.maxval;
			}
		}
	} else if ((format == 5) || (format == 6)
			|| ((format == 7)
					&& (header_info.gray || header_info.graya || header_info.rgb
							|| header_info.rgba))) { /* binary pixmap */
		uint8_t c0, c1, one;
		one = (prec < 9);
		for (uint64_t i = 0; i < area; i++) {
			for (compno = 0; compno < numcomps; compno++) {
				if (!fread(&c0, 1, 1, fp)) {
					spdlog::error(
							" fread return a number of element different from the expected.");
					grk_image_destroy(image);
					image = nullptr;
					goto cleanup;
				}
				if (one) {
					image->comps[compno].data[i] = c0;
				} else {
					if (!fread(&c1, 1, 1, fp))
						spdlog::error(
								" fread return a number of element different from the expected.");
					/* netpbm: */
					image->comps[compno].data[i] = ((c0 << 8) | c1);
				}
			}
		}
	} else if (format == 1) { /* ascii bitmap */
		for (uint64_t i = 0; i < area; i++) {
			unsigned int index;
			if (fscanf(fp, "%u", &index) != 1) {
				if (parameters->verbose)
					spdlog::warn(
							"fscanf return a number of element different from the expected.");
			}
			image->comps[0].data[i] = (index ? 0 : 255);
		}
	} else if (format == 4) {
		uint32_t x, y;
		int8_t bit;
		uint8_t uc;
		uint64_t i = 0;
		for (y = 0; y < h; ++y) {
			bit = -1;
			uc = 0;
			for (x = 0; x < w; ++x) {
				if (bit == -1) {
					bit = 7;
					int c = getc(fp);
					if (c == EOF) {
						spdlog::error("pnmtoimage reached EOF.");
						grk_image_destroy(image);
						image = nullptr;
						goto cleanup;
					}
					uc = (uint8_t) c;
				}
				image->comps[0].data[i] = (((uc >> bit) & 1) ? 0 : 255);
				--bit;
				++i;
			}
		}
	} else if ((format == 7 && header_info.bw)) { /*MONO*/
		uint8_t uc;
		for (uint64_t i = 0; i < area; ++i) {
			if (!fread(&uc, 1, 1, fp))
				spdlog::error(
						" fread return a number of element different from the expected.");
			image->comps[0].data[i] = (uc & 1) ? 0 : 255;
		}
	}
	cleanup: if (!grk::safe_fclose(fp)) {
		grk_image_destroy(image);
		image = nullptr;
	}
	return image;
}/* pnmtoimage() */

static int imagetopnm(grk_image *image, const char *outfile, bool force_split,
		bool verbose) {
	int *red = nullptr;
	int *green = nullptr;
	int *blue = nullptr;
	int *alpha = nullptr;
	uint32_t wr, hr, max;
	uint64_t i;
	unsigned int compno, ncomp;
	int adjustR, adjustG, adjustB, adjustA;
	int two, want_gray, has_alpha, triple;
	int prec, v;
	FILE *fdest = nullptr;
	const char *tmp = outfile;
	char *destname = nullptr;
	int rc = 0;

	alpha = nullptr;

	if ((prec = (int) image->comps[0].prec) > 16) {
		spdlog::error("{}:{}:imagetopnm\n\tprecision {} is larger than 16"
				"\n\t: refused.\n", __FILE__, __LINE__, prec);
		rc = 1;
		goto cleanup;
	}
	two = has_alpha = 0;
	ncomp = image->numcomps;

	if (!sanityCheckOnImage(image, ncomp)) {
		rc = 1;
		goto cleanup;
	}

	while (*tmp)
		++tmp;
	tmp -= 2;
	want_gray = (*tmp == 'g' || *tmp == 'G');

	if (want_gray)
		ncomp = 1;

	if ((!force_split)
			&& (ncomp == 2 /* GRAYA */
					|| (ncomp > 2 /* RGB, RGBA */
					&& image->comps[0].dx == image->comps[1].dx
							&& image->comps[1].dx == image->comps[2].dx
							&& image->comps[0].dy == image->comps[1].dy
							&& image->comps[1].dy == image->comps[2].dy
							&& image->comps[0].prec == image->comps[1].prec
							&& image->comps[1].prec == image->comps[2].prec
							&& image->comps[0].sgnd == image->comps[1].sgnd
							&& image->comps[1].sgnd == image->comps[2].sgnd

					))) {
		fdest = fopen(outfile, "wb");
		if (!fdest) {
			spdlog::error("failed to open {} for writing\n", outfile);
			rc = 1;
			goto cleanup;
		}
		two = (prec > 8);
		triple = (ncomp > 2);
		wr = image->comps[0].w;
		hr = image->comps[0].h;
		max = (1 << prec) - 1;
		has_alpha = (ncomp == 4 || ncomp == 2);

		red = image->comps[0].data;

		if (triple) {
			green = image->comps[1].data;
			blue = image->comps[2].data;
		} else
			green = blue = nullptr;

		if (has_alpha) {
			const char *tt = (triple ? "RGB_ALPHA" : "GRAYSCALE_ALPHA");

			fprintf(fdest, "P7\n# Grok-%s\nWIDTH %d\nHEIGHT %d\nDEPTH %u\n"
					"MAXVAL %d\nTUPLTYPE %s\nENDHDR\n", grk_version(), wr, hr,
					ncomp, max, tt);
			alpha = image->comps[ncomp - 1].data;
			adjustA = (
					image->comps[ncomp - 1].sgnd ?
							1 << (image->comps[ncomp - 1].prec - 1) : 0);
		} else {
			fprintf(fdest, "P6\n# Grok-%s\n%d %d\n%d\n", grk_version(), wr, hr,
					max);
			adjustA = 0;
		}
		adjustR = (image->comps[0].sgnd ? 1 << (image->comps[0].prec - 1) : 0);

		if (triple) {
			adjustG = (
					image->comps[1].sgnd ? 1 << (image->comps[1].prec - 1) : 0);
			adjustB = (
					image->comps[2].sgnd ? 1 << (image->comps[2].prec - 1) : 0);
		} else
			adjustG = adjustB = 0;

		if (two) {
			const size_t bufSize = 4096;
			uint16_t buf[bufSize];
			uint16_t *outPtr = buf;
			size_t outCount = 0;

			for (i = 0; i < (uint64_t) wr * hr; i++) {
				v = *red + adjustR;
				++red;
				if (v > INT16_MAX)
					v = INT16_MAX;
				else if (v < 0)
					v = 0;

				/* netpbm: */
				if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
						&outCount, bufSize, true, fdest)) {
					rc = 1;
					goto cleanup;
				}
				if (triple) {
					v = *green + adjustG;
					++green;
					if (v > INT16_MAX)
						v = INT16_MAX;
					else if (v < 0)
						v = 0;

					/* netpbm: */
					if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
							&outCount, bufSize, true, fdest)) {
						rc = 1;
						goto cleanup;
					}
					v = *blue + adjustB;
					++blue;
					if (v > INT16_MAX)
						v = INT16_MAX;
					else if (v < 0)
						v = 0;

					/* netpbm: */
					if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
							&outCount, bufSize, true, fdest)) {
						rc = 1;
						goto cleanup;
					}
				}/* if(triple) */

				if (has_alpha) {
					v = *alpha + adjustA;
					++alpha;
					if (v > INT16_MAX)
						v = INT16_MAX;
					else if (v < 0)
						v = 0;
					/* netpbm: */
					if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
							&outCount, bufSize, true, fdest)) {
						rc = 1;
						goto cleanup;
					}
				}
				if (outCount) {
					size_t res = fwrite(buf, sizeof(uint16_t), outCount, fdest);
					if (res != outCount) {
						rc = 1;
						goto cleanup;
					}
				}
			}
		} else {
			const size_t bufSize = 4096;
			uint8_t buf[bufSize];
			uint8_t *outPtr = buf;
			size_t outCount = 0;
			for (i = 0; i < wr * hr; i++) {
				/* prec <= 8: */
				v = *red++;
				if (v > UINT8_MAX)
					v = UINT8_MAX;
				else if (v < 0)
					v = 0;

				if (!grk::writeBytes<uint8_t>((uint8_t) v, buf, &outPtr,
						&outCount, bufSize, true, fdest)) {
					rc = 1;
					goto cleanup;
				}
				if (triple) {
					v = *green++;
					if (v > UINT8_MAX)
						v = UINT8_MAX;
					else if (v < 0)
						v = 0;

					if (!grk::writeBytes<uint8_t>((uint8_t) v, buf, &outPtr,
							&outCount, bufSize, true, fdest)) {
						rc = 1;
						goto cleanup;
					}
					v = *blue++;
					if (v > UINT8_MAX)
						v = UINT8_MAX;
					else if (v < 0)
						v = 0;

					if (!grk::writeBytes<uint8_t>((uint8_t) v, buf, &outPtr,
							&outCount, bufSize, true, fdest)) {
						rc = 1;
						goto cleanup;
					}
				}
				if (has_alpha) {
					v = *alpha++;
					if (v > UINT8_MAX)
						v = UINT8_MAX;
					else if (v < 0)
						v = 0;
					if (!grk::writeBytes<uint8_t>((uint8_t) v, buf, &outPtr,
							&outCount, bufSize, true, fdest)) {
						rc = 1;
						goto cleanup;
					}
				}
			}
			if (outCount) {
				size_t res = fwrite(buf, sizeof(uint8_t), outCount, fdest);
				if (res != outCount) {
					rc = 1;
					goto cleanup;
				}
			}
		}
	}

	/* YUV or MONO: */
	if (image->numcomps > ncomp) {
		if (verbose) {
			spdlog::warn("-> [PGM file] Only the first component"
					" is written to the file");
		}
	}
	destname = (char*) malloc(strlen(outfile) + 8);
	if (!destname) {
		spdlog::error("imagetopnm: out of memory");
		rc = 1;
		goto cleanup;
	}

	for (compno = 0; compno < ncomp; compno++) {
		if (ncomp > 1) {
			/*sprintf(destname, "%d.%s", compno, outfile);*/
			const size_t olen = strlen(outfile);
			if (olen < 4) {
				spdlog::error(
						" imagetopnm: output file name size less than 4.");
				goto cleanup;
			}
			const size_t dotpos = olen - 4;

			strncpy(destname, outfile, dotpos);
			sprintf(destname + dotpos, "_%u.pgm", compno);
		} else
			sprintf(destname, "%s", outfile);

		if (!fdest)
			fdest = fopen(destname, "wb");
		if (!fdest) {
			spdlog::error("failed to open {} for writing\n", destname);
			rc = 1;
			goto cleanup;
		}
		wr = image->comps[compno].w;
		hr = image->comps[compno].h;
		prec = (int) image->comps[compno].prec;
		max = (1 << prec) - 1;

		fprintf(fdest, "P5\n#Grok-%s\n%d %d\n%d\n", grk_version(), wr, hr, max);

		red = image->comps[compno].data;
		if (!red) {
			rc = 1;
			goto cleanup;
		}
		adjustR = (
				image->comps[compno].sgnd ?
						1 << (image->comps[compno].prec - 1) : 0);

		if (prec > 8) {
			const size_t bufSize = 4096;
			uint16_t buf[bufSize];
			uint16_t *outPtr = buf;
			size_t outCount = 0;

			for (i = 0; i < (uint64_t) wr * hr; i++) {
				v = *red + adjustR;
				++red;
				if (v > UINT16_MAX)
					v = UINT16_MAX;
				else if (v < 0)
					v = 0;
				/* netpbm: */
				if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
						&outCount, bufSize, true, fdest)) {
					rc = 1;
					goto cleanup;
				}
				if (has_alpha) {
					v = *alpha++;
					if (v > UINT16_MAX)
						v = UINT16_MAX;
					else if (v < 0)
						v = 0;
					/* netpbm: */
					if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
							&outCount, bufSize, true, fdest)) {
						rc = 1;
						goto cleanup;
					}
				}
			}/* for(i */
			//flush
			if (outCount) {
				size_t res = fwrite(buf, sizeof(uint16_t), outCount, fdest);
				if (res != outCount)
					rc = 1;
			}
		} else { /* prec <= 8 */
			const size_t bufSize = 4096;
			uint8_t buf[bufSize];
			uint8_t *outPtr = buf;
			size_t outCount = 0;
			for (i = 0; i < (uint64_t) wr * hr; i++) {
				v = *red + adjustR;
				++red;
				if (v > UINT8_MAX)
					v = UINT8_MAX;
				else if (v < 0)
					v = 0;
				if (!grk::writeBytes<uint8_t>((uint8_t) v, buf, &outPtr,
						&outCount, bufSize, true, fdest)) {
					rc = 1;
					goto cleanup;
				}
			}
			if (outCount) {
				size_t res = fwrite(buf, sizeof(uint8_t), outCount, fdest);
				if (res != outCount)
					rc = 1;
			}
		}
		if (!grk::safe_fclose(fdest)) {
			fdest = nullptr;
			rc = 1;
			goto cleanup;
		}
		fdest = nullptr;
	} /* for (compno */
	cleanup: if (destname)
		free(destname);
	if (!grk::safe_fclose(fdest))
		rc = -1;
	return rc;
}/* imagetopnm() */

bool PNMFormat::encode(grk_image *image, const char *filename,
		int compressionParam, bool verbose) {
	(void) compressionParam;
	(void) verbose;
	return imagetopnm(image, filename, forceSplit, verbose) ? false : true;
}
grk_image* PNMFormat::decode(const char *filename,
		grk_cparameters *parameters) {
	return pnmtoimage(filename, parameters);
}

