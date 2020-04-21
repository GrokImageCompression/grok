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
#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <vector>
#include <iostream>
using namespace std;

enum PNM_COLOUR_SPACE {
	PNM_UNKNOWN, PNM_BW, PNM_GRAY, PNM_GRAYA, PNM_RGB, PNM_RGBA
};

struct pnm_header {
	uint32_t width, height, maxval, depth, format;
	PNM_COLOUR_SPACE colour_space;
};

static char* skip_white(char *s) {
	if (!s)
		return nullptr;
	while (*s) {
		if (*s == '\n' || *s == '\r' || *s == '\t')
			return nullptr;
		if (isspace(*s)) {
			++s;
			continue;
		}
		return s;
	}
	return nullptr;
}

static char* skip_int(char *start, uint32_t *out_n) {
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
	*out_n = (uint32_t) atoi(start);
	*s = c;
	return s;
}

int convert(std::string s) {
	try {
		return stoi(s);
	} catch (std::invalid_argument const &e) {
		std::cout << "Bad input: std::invalid_argument thrown" << '\n';
	} catch (std::out_of_range const &e) {
		std::cout << "Integer overflow: std::out_of_range thrown" << '\n';
	}
	return -1;
}

bool header_rewind(char *s, char *line, size_t lineLen,  FILE *reader){
    // if s points to ' ', then rewind file
	// to two past current position of s
	if (*s == ' ') {
		ptrdiff_t len = (ptrdiff_t)s - (ptrdiff_t)line;
		if (fseek(reader, (ptrdiff_t)(-lineLen) +len + 2, SEEK_CUR))
			return false;
	}
	return true;
}

static bool read_pnm_header(FILE *reader, struct pnm_header *ph, bool verbose) {
	uint32_t format;
	const size_t lineSize = 256;
	const size_t lineSearch = 250;
	char line[lineSize];
	char c;

	if (fread(&c, 1, 1, reader) != 1){
		spdlog::error(" fread error");
		return false;
	}
	if (c != 'P') {
		spdlog::error("read_pnm_header:PNM:magic P missing");
		return false;
	}
	if (fread(&c, 1, 1, reader) != 1){
		spdlog::error(" fread error");
		return false;
	}
	format = (uint32_t) (c- 48);
	if (format < 1 || format > 7) {
		spdlog::error("read_pnm_header:magic format {} invalid", format);
		return false;
	}
	ph->format = format;
	if (format == 7) {
		uint32_t end = 0;
		while (fgets(line, lineSearch, reader)) {
			if (*line == '#' || *line == '\n')
				continue;

			istringstream iss(line);
			vector<string> tokens { istream_iterator<string> { iss },
					istream_iterator<string> { } };
			if (tokens.size() == 0)
				continue;
			string idf = tokens[0];
			if (idf == "ENDHDR") {
				end = 1;
				break;
			}
			if (tokens.size() == 2) {
				int temp;
				if (idf == "WIDTH") {
					temp = convert(tokens[1]);
					if (temp < 1) {
						spdlog::error("Invalid width");
						return false;
					}
					ph->width = (uint32_t) temp;

				} else if (idf == "HEIGHT") {
					temp = convert(tokens[1]);
					if (temp < 1) {
						spdlog::error("Invalid height");
						return false;
					}
					ph->height = (uint32_t) temp;
				} else if (idf == "DEPTH") {
					temp = convert(tokens[1]);
					if (temp < 1 || temp > 4) {
						spdlog::error("Invalid depth {}", temp);
						return false;
					}
					ph->depth = (uint32_t) temp;

				} else if (idf == "MAXVAL") {
					temp = convert(tokens[1]);
					if (temp < 1 || temp > 65535) {
						spdlog::error("Invalid maximum value {}", temp);
						return false;
					}
					ph->maxval = (uint32_t) temp;

				} else if (idf == "TUPLTYPE") {
					string type = tokens[1];
					if (type == "BLACKANDWHITE") {
						ph->colour_space = PNM_BW;
					} else if (type == "GRAYSCALE") {
						ph->colour_space = PNM_GRAY;
					} else if (type == "GRAYSCALE_ALPHA") {
						ph->colour_space = PNM_GRAYA;
					} else if (type == "RGB") {
						ph->colour_space = PNM_RGB;
					} else if (type == "RGB_ALPHA") {
						ph->colour_space = PNM_RGBA;
					} else {
						spdlog::error(" read_pnm_header:unknown P7 TUPLTYPE {}",
								type);
					}
				}
			} else {
				continue;
			}
		}/* while(fgets( ) */
		if (!end) {
			spdlog::error("read_pnm_header:P7 without ENDHDR");
			return false;
		}
		if (ph->depth == 0) {
			spdlog::error("Depth is missing");
			return false;
		}
		if (ph->maxval == 0) {
			spdlog::error("Maximum value is missing");
			return false;
		}
		PNM_COLOUR_SPACE depth_colour_space = PNM_UNKNOWN;
		switch (ph->depth) {
		case 1:
			depth_colour_space = (ph->maxval == 1) ? PNM_BW : PNM_GRAY;
			break;
		case 2:
			depth_colour_space = PNM_GRAYA;
			break;
		case 3:
			depth_colour_space = PNM_RGB;
			break;
		case 4:
			depth_colour_space = PNM_RGBA;
			break;
		}
		if (ph->colour_space != PNM_UNKNOWN
				&& ph->colour_space != depth_colour_space) {
			if (verbose)
				spdlog::warn("Tuple colour space {} does not match depth {}. "
						"Will use depth colour space", ph->colour_space,
						depth_colour_space);
		}
		ph->colour_space = depth_colour_space;

	} else {
		while (fgets(line, lineSearch, reader)) {
			int allow_null = 0;
			if (*line == '#' || *line == '\n' || *line == '\r')
				continue;

			char *s = line;
			/* Here format is in range [1,6] */
			if (ph->width == 0) {
				s = skip_int(s, &ph->width);
				if ((!s) || (*s == 0) || (ph->width < 1)) {
					spdlog::error("Invalid width {}",
							(s && *s != 0) ? ph->width : 0U);
					return false;
				}
				allow_null = 1;
			}
			if (ph->height == 0) {
				s = skip_int(s, &ph->height);
				if ((s == nullptr) && allow_null)
					continue;
				if (!s || (*s == 0) || (ph->height < 1)) {
					spdlog::error("Invalid height {}",
							(s && *s != 0) ? ph->height : 0U);
					return false;
				}
				if (format == 1 || format == 4){
					if (!header_rewind(s,line,lineSearch,reader))
						return false;
					break;
				}
				allow_null = 1;
			}
			/* here, format is in P2, P3, P5, P6 */
			s = skip_int(s, &ph->maxval);
			if (!s && allow_null)
				continue;
			if (!s || (*s == 0))
				return false;

			if (!header_rewind(s,line,lineSearch,reader))
				return false;

		 	break;
		}/* while(fgets( ) */

		if (format == 2 || format == 3 || format > 4) {
			if (ph->maxval < 1 || ph->maxval > 65535) {
				spdlog::error("Invalid max value {}", ph->maxval);
				return false;
			}
		}
		if (ph->width < 1 || ph->height < 1) {
			spdlog::error("Invalid width or height");
			return false;
		}
		// bitmap (ascii or binary)
		if (format == 1 || format == 4)
			ph->maxval = 1;
	}
	return true;

}

static inline uint32_t uint_floorlog2(uint32_t a) {
	uint32_t l;
	for (l = 0; a > 1; l++) {
		a >>= 1;
	}
	return l;
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
	bool success = false;

	if ((fp = fopen(filename, "rb")) == nullptr) {
		spdlog::error("pnmtoimage:Failed to open {} for reading!", filename);
		goto cleanup;
	}
	memset(&header_info, 0, sizeof(struct pnm_header));
	if (!read_pnm_header(fp, &header_info, parameters->verbose)) {
		spdlog::error("Invalid PNM header");
		goto cleanup;
	}

	format = header_info.format;
	switch (format) {
	case 1: /* ascii bitmap */
	case 4: /* binary bitmap */
		numcomps = 1;
		break;
	case 2: /* ascii greymap */
	case 5: /* binary greymap */
		numcomps = 1;
		break;
	case 3: /* ascii pixmap */
	case 6: /* binary pixmap */
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

	prec = uint_floorlog2(header_info.maxval) + 1;
	if (prec > 16) {
		spdlog::error(
				"Precision {} is greater than max supported precision (16)",
				prec);
		goto cleanup;
	}
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
		spdlog::error("pnmtoimage: Failed to create image");
		goto cleanup;
	}

	/* set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->y0 = parameters->image_offset_y0;
	image->x1 = (parameters->image_offset_x0 + (w - 1) * subsampling_dx + 1);
	image->y1 = (parameters->image_offset_y0 + (h - 1) * subsampling_dy + 1);

	if (format == 1) { /* ascii bitmap */
		const size_t chunkSize = 4096;
		uint8_t chunk[chunkSize];
		uint64_t i = 0;
		while (i < area) {
			size_t bytesRead = fread(chunk, 1, chunkSize, fp);
			if (!bytesRead)
				break;
			uint8_t *chunkPtr = (uint8_t*) chunk;
			for (size_t ct = 0; ct < bytesRead; ++ct) {
				uint8_t c = *chunkPtr++;
				if (c != '\n' && c != ' ')
					image->comps[0].data[i++] = (c & 1) ^ 1;
			}
		}
		if (i != area) {
			spdlog::error("pixels read ({}) less than image area ({})", i, area);
			goto cleanup;
		}
	} else if (format == 2 || format == 3) { /* ascii pixmap */
		uint32_t index;
		for (uint64_t i = 0; i < area; i++) {
			for (compno = 0; compno < numcomps; compno++) {
				index = 0;
				if (fscanf(fp, "%u", &index) != 1) {
					if (parameters->verbose)
						spdlog::warn(
								"fscanf return a number of element different from the expected.");
				}
				image->comps[compno].data[i] = (int32_t)index;
			}
		}
	} else if (format == 5 || format == 6
			|| ((format == 7)
					&& (header_info.colour_space == PNM_GRAY
							|| header_info.colour_space == PNM_GRAYA
							|| header_info.colour_space == PNM_RGB
							|| header_info.colour_space == PNM_RGBA))) {

		bool rc = false;
		if (prec <= 8)
			rc = grk::readBytes<uint8_t>(fp, image, area);
		else
			rc = grk::readBytes<uint16_t>(fp, image, area);
		if (!rc)
			goto cleanup;
	} else if (format == 4 || (format == 7 && header_info.colour_space == PNM_BW) ) { /* binary bitmap */
		bool packed = false;
		uint64_t packed_area = (uint64_t)((w + 7)/8) * h;
		if (format == 4) {
			packed = true;
		} else {
			/* let's see if bits are packed into bytes or not */
			int64_t currentPos = ftell(fp);
			if (currentPos == -1)
				goto cleanup;
			if (fseek(fp, 0L, SEEK_END))
				goto cleanup;
			int64_t endPos = ftell(fp);
			if (endPos == -1)
				goto cleanup;
			if (fseek(fp, currentPos, SEEK_SET))
				goto cleanup;
			uint64_t pixels = (uint64_t)(endPos - currentPos);
			if (pixels == packed_area)
				packed = true;
		}
		if (packed)
			area = packed_area;

		uint64_t index = 0;
		const size_t chunkSize = 4096;
		uint8_t chunk[chunkSize];
		uint64_t i = 0;
		while (i < area) {
			size_t bytesRead = fread(chunk, 1, min((uint64_t)chunkSize, (uint64_t)(area - i)), fp);
			if (!bytesRead)
				break;
			auto chunkPtr = (uint8_t*) chunk;
			for (size_t ct = 0; ct < bytesRead; ++ct) {
				uint8_t c = *chunkPtr++;
				if (packed) {
					for (int32_t j = 7; j >= 0; --j){
						image->comps[0].data[index++] = ((c >> j) & 1) ^ 1;
						if ((index % w) == 0)
							break;
					}
				} else {
					image->comps[0].data[index++] = c & 1;
				}
				i++;
			}
		}
		if (i != area) {
			spdlog::error("pixels read ({}) differs from image area ({})", i, area);
			goto cleanup;
		}
	}
	success = true;
	cleanup: if (!grk::safe_fclose(fp) || !success) {
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
	uint32_t compno, ncomp, prec;
	int adjustR, adjustG, adjustB, adjustA;
	int two, want_gray, has_alpha, triple;
	int v;
	FILE *fdest = nullptr;
	const char *tmp = outfile;
	char *destname = nullptr;
	int rc = 0;

	alpha = nullptr;

	if ((prec = image->comps[0].prec) > 16) {
		spdlog::error("{}:{}:imagetopnm\n\tprecision {} is larger than 16"
				"\n\t: refused.", __FILE__, __LINE__, prec);
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
			spdlog::error("failed to open {} for writing", outfile);
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
				v = *red++ + adjustR;
				if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
						&outCount, bufSize, true, fdest)) {
					rc = 1;
					goto cleanup;
				}
				if (triple) {
					v = *green++ + adjustG;
					if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
							&outCount, bufSize, true, fdest)) {
						rc = 1;
						goto cleanup;
					}
					v = *blue++ + adjustB;
					if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
							&outCount, bufSize, true, fdest)) {
						rc = 1;
						goto cleanup;
					}
				}/* if(triple) */

				if (has_alpha) {
					v = *alpha++ + adjustA;
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
			for (i = 0; i < (uint64_t) wr * hr; i++) {
				v = *red++;
				if (!grk::writeBytes<uint8_t>((uint8_t) v, buf, &outPtr,
						&outCount, bufSize, true, fdest)) {
					rc = 1;
					goto cleanup;
				}
				if (triple) {
					v = *green++;
					if (!grk::writeBytes<uint8_t>((uint8_t) v, buf, &outPtr,
							&outCount, bufSize, true, fdest)) {
						rc = 1;
						goto cleanup;
					}
					v = *blue++;
					if (!grk::writeBytes<uint8_t>((uint8_t) v, buf, &outPtr,
							&outCount, bufSize, true, fdest)) {
						rc = 1;
						goto cleanup;
					}
				}
				if (has_alpha) {
					v = *alpha++;
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
		if (verbose)
			spdlog::warn("-> [PGM file] Only the first component"
					" is written to the file");
	}
	destname = (char*) malloc(strlen(outfile) + 8);
	if (!destname) {
		spdlog::error("imagetopnm: out of memory");
		rc = 1;
		goto cleanup;
	}

	for (compno = 0; compno < ncomp; compno++) {
		if (ncomp > 1) {
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
			spdlog::error("failed to open {} for writing", destname);
			rc = 1;
			goto cleanup;
		}
		wr = image->comps[compno].w;
		hr = image->comps[compno].h;
		prec = image->comps[compno].prec;
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
				v = *red++ + adjustR;
				if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
						&outCount, bufSize, true, fdest)) {
					rc = 1;
					goto cleanup;
				}
				if (has_alpha) {
					v = *alpha++;
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
				v = *red++ + adjustR;
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

