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
#include "RAWFormat.h"
#include "convert.h"
#include "common.h"

template<typename T> static bool write(FILE *m_fileStream, bool bigEndian,
		int32_t *ptr, uint32_t w, uint32_t stride, uint32_t h, int32_t lower, int32_t upper) {
	const size_t bufSize = 4096;
	T buf[bufSize];
	T *outPtr = buf;
	size_t outCount = 0;
	auto stride_diff = stride - w;
	for (uint32_t j = 0; j < h; ++j) {
		for (uint32_t i = 0; i <  w; ++i) {
			int32_t curr = *ptr++;
			if (curr > upper)
				curr = upper;
			else if (curr < lower)
				curr = lower;
			if (!grk::writeBytes<T>((T) curr, buf, &outPtr, &outCount, bufSize,
					bigEndian, m_fileStream))
				return false;
		}
		ptr += stride_diff;
	}
	//flush
	if (outCount) {
		size_t res = fwrite(buf, sizeof(T), outCount, m_fileStream);
		if (res != outCount)
			return false;
	}

	return true;
}

bool RAWFormat::encodeHeader(grk_image *image, const std::string &filename,
		uint32_t compressionParam) {
	(void) compressionParam;
	m_image = image;
	m_fileName = filename;

	return true;
}
bool RAWFormat::encodeStrip(uint32_t rows){
	(void)rows;

	const char* outfile = m_fileName.c_str();
	m_useStdIO = grk::useStdio(outfile);
	m_fileStream = nullptr;
	unsigned int compno, numcomps;
	bool success = false;

	if ((m_image->numcomps * m_image->x1 * m_image->y1) == 0) {
		spdlog::error("imagetoraw: invalid raw m_image parameters");
		goto beach;
	}

	numcomps = m_image->numcomps;
	if (numcomps > 4) {
		spdlog::warn("imagetoraw: number of components {} is "
					"greater than 4. Truncating to 4", numcomps);
		numcomps = 4;
	}

	for (compno = 1; compno < numcomps; ++compno) {
		if (m_image->comps[0].dx != m_image->comps[compno].dx)
			break;
		if (m_image->comps[0].dy != m_image->comps[compno].dy)
			break;
		if (m_image->comps[0].prec != m_image->comps[compno].prec)
			break;
		if (m_image->comps[0].sgnd != m_image->comps[compno].sgnd)
			break;
	}
	if (compno != numcomps) {
		spdlog::error(
				"imagetoraw: All components shall have the same subsampling, same bit depth, same sign.");
		goto beach;
	}
	if (!grk::grk_open_for_output(&m_fileStream, outfile,m_useStdIO))
		goto beach;

	spdlog::info("imagetoraw: raw m_image characteristics: {} components",
				m_image->numcomps);

	for (compno = 0; compno < m_image->numcomps; compno++) {
		auto comp = m_image->comps + compno;
		spdlog::info("Component {} characteristics: {}x{}x{} {}", compno,
					comp->w, comp->h,
					comp->prec,
					comp->sgnd == 1 ? "signed" : "unsigned");

		if (!comp->data) {
			spdlog::error("imagetotif: component {} is null.", compno);

			goto beach;
		}
		auto w = comp->w;
		auto h = comp->h;
		auto stride = comp->stride;
		bool sgnd = comp->sgnd;
		auto prec = comp->prec;

		int32_t lower = sgnd ? -(1 << (prec - 1)) : 0;
		int32_t upper =
				sgnd ? -lower - 1 : (1 << comp->prec) - 1;
		int32_t *ptr = comp->data;

		bool rc;
		if (prec <= 8) {
			if (sgnd)
				rc = write<int8_t>(m_fileStream, bigEndian, ptr, w, stride, h, lower,
						upper);
			else
				rc = write<uint8_t>(m_fileStream, bigEndian, ptr, w, stride, h, lower,
						upper);
			if (!rc)
				spdlog::error("imagetoraw: failed to write bytes for {}",
						outfile);
		} else if (prec <= 16) {
			if (sgnd)
				rc = write<int16_t>(m_fileStream, bigEndian, ptr, w, stride, h, lower,
						upper);
			else
				rc = write<uint16_t>(m_fileStream, bigEndian, ptr, w, stride, h, lower,
						upper);
			if (!rc)
				spdlog::error("fimagetoraw: ailed to write bytes for {}",
						outfile);
		} else if (comp->prec <= 32) {
			spdlog::error(
					"imagetoraw: more than 16 bits per component no handled yet");
			goto beach;
		} else {
			spdlog::error("imagetoraw: invalid precision: {}",
					comp->prec);
			goto beach;
		}
	}
	success = true;

beach:

	return success;
}
bool RAWFormat::encodeFinish(void){
	bool success = true;

	if (!m_useStdIO && m_fileStream) {
		if (!grk::safe_fclose(m_fileStream))
			success = false;
	}
	return success;
}
grk_image* RAWFormat::decode(const std::string &filename,
		grk_cparameters *parameters) {
	return rawtoimage(filename.c_str(), parameters, bigEndian);
}

template<typename T> static bool read(FILE *m_fileStream, bool bigEndian,
		int32_t *ptr, uint64_t nloop) {
	const size_t bufSize = 4096;
	T buf[bufSize];

	for (uint64_t i = 0; i < nloop; i += bufSize) {
		size_t target = (i + bufSize > nloop) ? (nloop - i) : bufSize;
		size_t ct = fread(buf, sizeof(T), target, m_fileStream);
		if (ct != target)
			return false;
		T *inPtr = buf;
		for (size_t j = 0; j < ct; j++)
			*(ptr++) = grk::endian<T>(*inPtr++, bigEndian);
	}

	return true;
}


grk_image* RAWFormat::rawtoimage(const char *filename,
		grk_cparameters *parameters, bool bigEndian) {
	m_useStdIO = grk::useStdio(filename);
	grk_raw_cparameters *raw_cp = &parameters->raw_cp;
	uint32_t subsampling_dx = parameters->subsampling_dx;
	uint32_t subsampling_dy = parameters->subsampling_dy;

	uint32_t i, compno, numcomps, w, h;
	GRK_COLOR_SPACE color_space;
	grk_image_cmptparm *cmptparm;
	grk_image *image = nullptr;
	unsigned short ch;
	bool success = true;

	if (!(raw_cp->width && raw_cp->height && raw_cp->numcomps && raw_cp->prec)) {
		spdlog::error("invalid raw image parameters");
		spdlog::error("Please use the Format option -F:");
		spdlog::error(
				"-F <width>,<height>,<ncomp>,<bitdepth>,{s,u}@<dx1>x<dy1>:...:<dxn>x<dyn>");
		spdlog::error(
				"If subsampling is omitted, 1x1 is assumed for all components");
		spdlog::error(
				"Example: -i image.raw -o image.j2k -F 512,512,3,8,u@1x1:2x2:2x2");
		spdlog::error("         for raw 512x512 image with 4:2:0 subsampling");
		return nullptr;
	}

	if (m_useStdIO) {
		if (!grk::grk_set_binary_mode(stdin))
			return nullptr;
		m_fileStream = stdin;
	} else {
		m_fileStream = fopen(filename, "rb");
		if (!m_fileStream) {
			spdlog::error("Failed to open {} for reading", filename);
			success = false;
			goto cleanup;
		}
	}
	numcomps = raw_cp->numcomps;
	if (numcomps == 1) {
		color_space = GRK_CLRSPC_GRAY;
	} else if ((numcomps >= 3) && (parameters->tcp_mct == 0)) {
		color_space = GRK_CLRSPC_SYCC;
	} else if ((numcomps >= 3) && (parameters->tcp_mct != 2)) {
		color_space = GRK_CLRSPC_SRGB;
	} else {
		color_space = GRK_CLRSPC_UNKNOWN;
	}
	w = raw_cp->width;
	h = raw_cp->height;
	cmptparm = (grk_image_cmptparm*) calloc(numcomps,
			sizeof(grk_image_cmptparm));
	if (!cmptparm) {
		spdlog::error("Failed to allocate image components parameters");
		success = false;
		goto cleanup;
	}
	/* initialize image components */
	for (i = 0; i < numcomps; i++) {
		cmptparm[i].prec = raw_cp->prec;
		cmptparm[i].sgnd = raw_cp->sgnd;
		cmptparm[i].dx = subsampling_dx * raw_cp->comps[i].dx;
		cmptparm[i].dy = subsampling_dy * raw_cp->comps[i].dy;
		cmptparm[i].w = w;
		cmptparm[i].h = h;

		if (raw_cp->comps[i].dx * raw_cp->comps[i].dy != 1){
			spdlog::error("Subsampled raw images are not currently supported");
			success = false;
			goto cleanup;
		}
	}
	/* create the image */
	image = grk_image_create(numcomps, &cmptparm[0], color_space,true);
	free(cmptparm);
	if (!image) {
		success = false;
		goto cleanup;
	}
	/* set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->y0 = parameters->image_offset_y0;
	image->x1 = parameters->image_offset_x0 + (w - 1) * subsampling_dx + 1;
	image->y1 = parameters->image_offset_y0 + (h - 1) * subsampling_dy + 1;

	if (raw_cp->prec <= 8) {
		for (compno = 0; compno < numcomps; compno++) {
			int32_t *ptr = image->comps[compno].data;
			for (uint32_t j = 0; j < h; ++j){
				bool rc;
				if (raw_cp->sgnd)
					rc = read<int8_t>(m_fileStream, bigEndian, ptr, w);
				else
					rc = read<uint8_t>(m_fileStream, bigEndian, ptr, w);
				if (!rc) {
					spdlog::error(
							"Error reading raw file. End of file probably reached.");
					success = false;
					goto cleanup;
				}
				ptr += image->comps[compno].stride ;

			}
		}
	} else if (raw_cp->prec <= 16) {
		for (compno = 0; compno < numcomps; compno++) {
			auto ptr = image->comps[compno].data;
			for (uint32_t j = 0; j < h; ++j){
				bool rc;
				if (raw_cp->sgnd)
					rc = read<int16_t>(m_fileStream, bigEndian, ptr, w);
				else
					rc = read<uint16_t>(m_fileStream, bigEndian, ptr, w);
				if (!rc) {
					spdlog::error(
							"Error reading raw file. End of file probably reached.");
					success = false;
					goto cleanup;
				}
				ptr += image->comps[compno].stride ;
			}
		}
	} else {
		spdlog::error(
				"Grok cannot encode raw components with bit depth higher than 16 bits.");
		success = false;
		goto cleanup;
	}

	if (fread(&ch, 1, 1, m_fileStream)) {
		spdlog::warn("End of raw file not reached... processing anyway");
	}
	cleanup: if (m_fileStream && !m_useStdIO) {
		if (!grk::safe_fclose(m_fileStream)) {
			grk_image_destroy(image);
			image = nullptr;
		}
	}
	if (!success) {
		grk_image_destroy(image);
		image = nullptr;
	}
	return image;
}
