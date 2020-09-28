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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 *
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
#include <climits>
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

static bool read_pnm_header(FILE *reader, struct pnm_header *ph) {
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
					if (temp < 1 || temp > USHRT_MAX) {
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
			if (ph->maxval < 1 || ph->maxval > USHRT_MAX) {
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
	uint32_t compno, numcomps, w, stride_diff,width,counter, h, prec, format;
	GRK_COLOR_SPACE color_space;
	grk_image_cmptparm cmptparm[4]; /* RGBA: max. 4 components */
	grk_image *image = nullptr;
	struct pnm_header header_info;
	uint64_t area = 0;
	bool success = false;

	if ((fp = fopen(filename, "rb")) == nullptr) {
		spdlog::error("pnmtoimage:Failed to open {} for reading.", filename);
		goto cleanup;
	}
	memset(&header_info, 0, sizeof(struct pnm_header));
	if (!read_pnm_header(fp, &header_info)) {
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
		cmptparm[i].sgnd = false;
		cmptparm[i].dx = subsampling_dx;
		cmptparm[i].dy = subsampling_dy;
		cmptparm[i].w = w;
		cmptparm[i].h = h;
	}
	image = grk_image_create(numcomps, &cmptparm[0], color_space,true);
	if (!image) {
		spdlog::error("pnmtoimage: Failed to create image");
		goto cleanup;
	}

	/* set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->y0 = parameters->image_offset_y0;
	image->x1 = (parameters->image_offset_x0 + (w - 1) * subsampling_dx + 1);
	image->y1 = (parameters->image_offset_y0 + (h - 1) * subsampling_dy + 1);

	width = image->comps[0].w;
	stride_diff =  image->comps[0].stride - width;
	counter = 0;

	if (format == 1) { /* ascii bitmap */
		const size_t chunkSize = 4096;
		uint8_t chunk[chunkSize];
		uint64_t i = 0;
		area = (uint64_t)image->comps[0].stride * h;
		while (i < area) {
			size_t bytesRead = fread(chunk, 1, chunkSize, fp);
			if (bytesRead == 0)
				break;
			uint8_t *chunkPtr = (uint8_t*) chunk;
			for (size_t ct = 0; ct < bytesRead; ++ct) {
				uint8_t c = *chunkPtr++;
				if (c != '\n' && c != ' '){
					image->comps[0].data[i++] = (c & 1) ^ 1;
					counter++;
					if (counter == w){
						counter = 0;
						i += stride_diff;
					}
				}
			}
		}
		if (i != area) {
			spdlog::error("pixels read ({}) less than image area ({})", i, area);
			goto cleanup;
		}
	} else if (format == 2 || format == 3) { /* ascii pixmap */
		area = (uint64_t)image->comps[0].stride * h;
		for (uint64_t i = 0; i < area; i++) {
			for (compno = 0; compno < numcomps; compno++) {
				uint32_t val = 0;
				if (fscanf(fp, "%u", &val) != 1)
					spdlog::warn("fscanf error");
				image->comps[compno].data[i] = (int32_t)val;
			}
			counter++;
			if (counter == w){
				counter = 0;
				i += stride_diff;
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
			auto toRead = min((uint64_t)chunkSize, (uint64_t)(area - i));
			size_t bytesRead = fread(chunk, 1, toRead, fp);
			if (bytesRead == 0)
				break;
			auto chunkPtr = (uint8_t*) chunk;
			for (size_t ct = 0; ct < bytesRead; ++ct) {
				uint8_t c = *chunkPtr++;
				if (packed) {
					for (int32_t j = 7; j >= 0; --j){
						image->comps[0].data[index++] = ((c >> j) & 1) ^ 1;
						counter++;
						if (counter == w){
							counter = 0;
							index += stride_diff;
							break;
						}
					}
				} else {
					image->comps[0].data[index++] = c & 1;
					counter++;
					if (counter == w){
						counter = 0;
						index += stride_diff;
					}
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



bool PNMFormat::encodeHeader(grk_image *image, const std::string &filename,
		uint32_t compressionParam) {
	m_image = image;
	m_fileName = filename;

	(void) compressionParam;

	return true;

}
bool PNMFormat::encodeStrip(uint32_t rows){
	(void)rows;

	int *red = nullptr;
	int *green = nullptr;
	int *blue = nullptr;
	int *alpha = nullptr;
	uint32_t width, height,stride_diff, max;
	uint32_t compno, ncomp, prec;
	int adjustR, adjustG, adjustB, adjustA;
	int two, want_gray, has_alpha, triple;
	int v;
	const char *tmp = m_fileName.c_str();
	char *destname = nullptr;
	bool success = false;
	m_useStdIO = grk::useStdio(m_fileName.c_str());

	alpha = nullptr;

	if ((prec = m_image->comps[0].prec) > 16) {
		spdlog::error("{}:{}:imagetopnm\n\tprecision {} is larger than 16", __FILE__, __LINE__, prec);
		goto cleanup;
	}
	two = has_alpha = 0;
	ncomp = m_image->numcomps;

	if (!grk::all_components_sanity_check(m_image,true)) {
		goto cleanup;
	}

	while (*tmp)
		++tmp;
	tmp -= 2;
	want_gray = (*tmp == 'g' || *tmp == 'G');

	if (want_gray)
		ncomp = 1;

	if (m_useStdIO){
		if (forceSplit) {
			spdlog::error("Unable to write split file to stdout");
			goto cleanup;
		}
	}

	if ((!forceSplit)
			&& (ncomp == 2 /* GRAYA */
					|| (ncomp > 2 /* RGB, RGBA */
					&& m_image->comps[0].dx == m_image->comps[1].dx
							&& m_image->comps[1].dx == m_image->comps[2].dx
							&& m_image->comps[0].dy == m_image->comps[1].dy
							&& m_image->comps[1].dy == m_image->comps[2].dy
					))) {

		if (!grk::grk_open_for_output(&m_fileStream, m_fileName.c_str(),m_useStdIO))
			goto cleanup;

		two = (prec > 8);
		triple = (ncomp > 2);
		width = m_image->comps[0].w;
		stride_diff = m_image->comps[0].stride - width;
		height = m_image->comps[0].h;
		max = (1 << prec) - 1;
		has_alpha = (ncomp == 4 || ncomp == 2);

		red = m_image->comps[0].data;

		if (triple) {
			green = m_image->comps[1].data;
			blue = m_image->comps[2].data;
		} else
			green = blue = nullptr;

		if (has_alpha) {
			const char *tt = (triple ? "RGB_ALPHA" : "GRAYSCALE_ALPHA");

			fprintf(m_fileStream, "P7\n# Grok-%s\nWIDTH %u\nHEIGHT %u\nDEPTH %u\n"
					"MAXVAL %u\nTUPLTYPE %s\nENDHDR\n", grk_version(), width, height,
					ncomp, max, tt);
			alpha = m_image->comps[ncomp - 1].data;
			adjustA = (
					m_image->comps[ncomp - 1].sgnd ?
							1 << (m_image->comps[ncomp - 1].prec - 1) : 0);
		} else {
			fprintf(m_fileStream, "P6\n# Grok-%s\n%u %u\n%u\n", grk_version(), width, height,
					max);
			adjustA = 0;
		}
		adjustR = (m_image->comps[0].sgnd ? 1 << (m_image->comps[0].prec - 1) : 0);

		if (triple) {
			adjustG = (
					m_image->comps[1].sgnd ? 1 << (m_image->comps[1].prec - 1) : 0);
			adjustB = (
					m_image->comps[2].sgnd ? 1 << (m_image->comps[2].prec - 1) : 0);
		} else
			adjustG = adjustB = 0;

		if (two) {
			const size_t bufSize = 4096;
			uint16_t buf[bufSize];
			uint16_t *outPtr = buf;
			size_t outCount = 0;

			for (uint32_t j = 0; j < height; ++j){
				for (uint32_t i = 0; i < width; ++i){
					v = *red++ + adjustR;
					if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
							&outCount, bufSize, true, m_fileStream)) {
						goto cleanup;
					}
					if (triple) {
						v = *green++ + adjustG;
						if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
								&outCount, bufSize, true, m_fileStream)) {
							goto cleanup;
						}
						v = *blue++ + adjustB;
						if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
								&outCount, bufSize, true, m_fileStream)) {
							goto cleanup;
						}
					}/* if(triple) */

					if (has_alpha) {
						v = *alpha++ + adjustA;
						if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
								&outCount, bufSize, true, m_fileStream)) {
							goto cleanup;
						}
					}
				}
				red += stride_diff;
				if (triple) {
					green += stride_diff;
					blue += stride_diff;
				}
				if (has_alpha)
					alpha += stride_diff;
			}
			if (outCount) {
				size_t res = fwrite(buf, sizeof(uint16_t), outCount, m_fileStream);
				if (res != outCount) {
					goto cleanup;
				}
			}
		} else {
			const size_t bufSize = 4096;
			uint8_t buf[bufSize];
			uint8_t *outPtr = buf;
			size_t outCount = 0;
			for (uint32_t j = 0; j < height; ++j){
				for (uint32_t i = 0; i < width; ++i){
					v = *red++;
					if (!grk::writeBytes<uint8_t>((uint8_t) v, buf, &outPtr,
							&outCount, bufSize, true, m_fileStream)) {
						goto cleanup;
					}
					if (triple) {
						v = *green++;
						if (!grk::writeBytes<uint8_t>((uint8_t) v, buf, &outPtr,
								&outCount, bufSize, true, m_fileStream)) {
							goto cleanup;
						}
						v = *blue++;
						if (!grk::writeBytes<uint8_t>((uint8_t) v, buf, &outPtr,
								&outCount, bufSize, true, m_fileStream)) {
							goto cleanup;
						}
					}
					if (has_alpha) {
						v = *alpha++;
						if (!grk::writeBytes<uint8_t>((uint8_t) v, buf, &outPtr,
								&outCount, bufSize, true, m_fileStream)) {
							goto cleanup;
						}
					}
				}
				red += stride_diff;
				if (triple) {
					green += stride_diff;
					blue += stride_diff;
				}
				if (has_alpha)
					alpha += stride_diff;
			}
			if (outCount) {
				size_t res = fwrite(buf, sizeof(uint8_t), outCount, m_fileStream);
				if (res != outCount) {
					goto cleanup;
				}
			}
		}
		// we only write the first PNM file to stdout
		if (m_useStdIO) {
			success = true;
			goto cleanup;
		}
	}

	if (!m_useStdIO && m_fileStream) {
		if (!grk::safe_fclose(m_fileStream))
			goto cleanup;
		m_fileStream = nullptr;
	}

	if (m_useStdIO)
		ncomp = 1;

	/* YUV or MONO: */
	if (m_image->numcomps > ncomp) {
		spdlog::warn("[PGM file] Only the first component"
					" is written out");
	}
	destname = (char*) malloc(strlen(m_fileName.c_str()) + 8);
	if (!destname) {
		spdlog::error("imagetopnm: out of memory");
		goto cleanup;
	}

	for (compno = 0; compno < ncomp; compno++) {
		if (ncomp > 1) {
			const size_t olen = strlen(m_fileName.c_str());
			if (olen < 4) {
				spdlog::error(
						" imagetopnm: output file name size less than 4.");
				goto cleanup;
			}
			const size_t dotpos = olen - 4;

			strncpy(destname, m_fileName.c_str(), dotpos);
			sprintf(destname + dotpos, "_%u.pgm", compno);
		} else
			sprintf(destname, "%s", m_fileName.c_str());

		if (!m_fileStream) {
			if (!grk::grk_open_for_output(&m_fileStream, destname,m_useStdIO))
				goto cleanup;
		}

		width = m_image->comps[compno].w;
		stride_diff = m_image->comps[compno].stride - width;
		height = m_image->comps[compno].h;
		prec = m_image->comps[compno].prec;
		max = (1 << prec) - 1;

		fprintf(m_fileStream, "P5\n#Grok-%s\n%u %u\n%u\n", grk_version(), width, height, max);

		red = m_image->comps[compno].data;
		if (!red) {
			goto cleanup;
		}
		adjustR = (
				m_image->comps[compno].sgnd ?
						1 << (m_image->comps[compno].prec - 1) : 0);

		if (prec > 8) {
			const size_t bufSize = 4096;
			uint16_t buf[bufSize];
			uint16_t *outPtr = buf;
			size_t outCount = 0;

			for (uint32_t j = 0; j < height; ++j){
				for (uint32_t i = 0; i < width; ++i){
					v = *red++ + adjustR;
					if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
							&outCount, bufSize, true, m_fileStream)) {
						goto cleanup;
					}
					if (has_alpha) {
						v = *alpha++;
						if (!grk::writeBytes<uint16_t>((uint16_t) v, buf, &outPtr,
								&outCount, bufSize, true, m_fileStream)) {
							goto cleanup;
						}
					}
				}
				red += stride_diff;
				if (has_alpha)
					alpha += stride_diff;
			}
			//flush
			if (outCount) {
				size_t res = fwrite(buf, sizeof(uint16_t), outCount, m_fileStream);
				if (res != outCount){
					goto cleanup;
				}
			}
		} else { /* prec <= 8 */
			const size_t bufSize = 4096;
			uint8_t buf[bufSize];
			uint8_t *outPtr = buf;
			size_t outCount = 0;
			for (uint32_t j = 0; j < height; ++j){
				for (uint32_t i = 0; i < width; ++i){
					v = *red++ + adjustR;
					if (!grk::writeBytes<uint8_t>((uint8_t) v, buf, &outPtr,
							&outCount, bufSize, true, m_fileStream)) {
						goto cleanup;
					}
				}
				red += stride_diff;
				if (has_alpha)
					alpha += stride_diff;
			}
			if (outCount) {
				size_t res = fwrite(buf, sizeof(uint8_t), outCount, m_fileStream);
				if (res != outCount){
					goto cleanup;
				}
			}
		}
		if (!m_useStdIO && m_fileStream) {
			if (!grk::safe_fclose(m_fileStream)){
				goto cleanup;
			}
		}
		m_fileStream = nullptr;
	} /* for (compno */

	success = true;

cleanup:
	if (destname)
		free(destname);

	return success;
}
bool PNMFormat::encodeFinish(void){
	if (!m_useStdIO && m_fileStream) {
		if (!grk::safe_fclose(m_fileStream))
			return false;
	}

	return true;
}
grk_image* PNMFormat::decode(const std::string &filename,
		grk_cparameters *parameters) {
	return pnmtoimage(filename.c_str(), parameters);
}

