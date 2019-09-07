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
#include "common.h"
#include "spdlog/spdlog.h"

bool RAWFormat::encode(opj_image_t* image, const char* filename, int compressionParam, bool verbose) {
	(void)compressionParam;
	return imagetoraw(image, filename, bigEndian,verbose) ? true : false;
}
opj_image_t* RAWFormat::decode(const char* filename, opj_cparameters_t *parameters) {
	return rawtoimage(filename, parameters, bigEndian);
}

template<typename T> static bool read(FILE *rawFile, bool big_endian,
										int32_t* ptr,
										uint64_t nloop){
	const size_t bufSize = 4096;
	T buf[bufSize];

	for (uint64_t i = 0; i < nloop; i+=bufSize) {
		size_t target = (i + bufSize > nloop) ? (nloop - i) : bufSize;
		size_t ct = fread(buf, sizeof(T), target, rawFile);
		if (ct != target)
			return false;
		T *inPtr = buf;
		for (size_t j = 0; j < ct; j++)
		    *(ptr++) = grk::endian<T>(*inPtr++, big_endian);
	}

	return true;
}

opj_image_t* RAWFormat::rawtoimage(const char *filename,
										opj_cparameters_t *parameters,
										bool big_endian)
{
	bool readFromStdin = grk::useStdio(filename);
	raw_cparameters_t *raw_cp = &parameters->raw_cp;
	uint32_t subsampling_dx = parameters->subsampling_dx;
	uint32_t subsampling_dy = parameters->subsampling_dy;

	FILE *f = nullptr;
	uint32_t i, compno, numcomps, w, h;
	OPJ_COLOR_SPACE color_space;
	opj_image_cmptparm_t *cmptparm;
	opj_image_t * image = nullptr;
	unsigned short ch;
	bool success = true;

	if (!(raw_cp->width && raw_cp->height && raw_cp->numcomps && raw_cp->prec)) {
		spdlog::error("invalid raw image parameters");
		spdlog::error("Please use the Format option -F:");
		spdlog::error("-F <width>,<height>,<ncomp>,<bitdepth>,{s,u}@<dx1>x<dy1>:...:<dxn>x<dyn>");
		spdlog::error("If subsampling is omitted, 1x1 is assumed for all components");
		spdlog::error("Example: -i image.raw -o image.j2k -F 512,512,3,8,u@1x1:2x2:2x2");
		spdlog::error("         for raw 512x512 image with 4:2:0 subsampling");
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
			spdlog::error("Failed to open {} for reading !!\n", filename);
			success = false;
			goto cleanup;
		}
	}
	numcomps = raw_cp->numcomps;
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
	w = raw_cp->width;
	h = raw_cp->height;
	cmptparm = (opj_image_cmptparm_t*)calloc(numcomps, sizeof(opj_image_cmptparm_t));
	if (!cmptparm) {
		spdlog::error("Failed to allocate image components parameters !!");
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
	}
	/* create the image */
	image = opj_image_create(numcomps, &cmptparm[0], color_space);
	free(cmptparm);
	if (!image) {
		success = false;
		goto cleanup;
	}
	/* set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->y0 = parameters->image_offset_y0;
	image->x1 = parameters->image_offset_x0 + (w - 1) *	subsampling_dx + 1;
	image->y1 = parameters->image_offset_y0 + (h - 1) * subsampling_dy + 1;

	if (raw_cp->prec <= 8) {
		for (compno = 0; compno < numcomps; compno++) {
			int32_t *ptr = image->comps[compno].data;
			uint64_t nloop = ((uint64_t)w*h) / (raw_cp->comps[compno].dx*raw_cp->comps[compno].dy);
			bool rc;
			if (raw_cp->sgnd)
				rc = read<int8_t>(f, big_endian, ptr,nloop);
			else
				rc = read<uint8_t>(f, big_endian, ptr,nloop);
			if (!rc){
				spdlog::error("Error reading raw file. End of file probably reached.");
				success = false;
				goto cleanup;
			}
		}
	}
	else if (raw_cp->prec <= 16) {
		for (compno = 0; compno < numcomps; compno++) {
			auto ptr = image->comps[compno].data;
			uint64_t nloop = ((uint64_t)w*h) / (raw_cp->comps[compno].dx*raw_cp->comps[compno].dy);
			bool rc;
			if (raw_cp->sgnd)
				rc = read<int16_t>(f, big_endian, ptr,nloop);
			else
				rc = read<uint16_t>(f, big_endian, ptr,nloop);
			if (!rc){
				spdlog::error("Error reading raw file. End of file probably reached.");
				success = false;
				goto cleanup;
			}
		}
	}
	else {
		spdlog::error("Grok cannot encode raw components with bit depth higher than 16 bits.");
		success = false;
		goto cleanup;
	}

	if (fread(&ch, 1, 1, f)) {
		if (parameters->verbose)
			spdlog::warn("End of raw file not reached... processing anyway");
	}
cleanup:
	if (f && !readFromStdin){
		if (!grk::safe_fclose(f)){
			opj_image_destroy(image);
			image = nullptr;
		}
	}
	if (!success){
		opj_image_destroy(image);
		image = nullptr;
	}
	return image;
}

template<typename T> static bool write(FILE *rawFile, bool big_endian,
										int32_t* ptr,
										uint32_t w, uint32_t h,
										int32_t lower, int32_t upper){
	const size_t bufSize = 4096;
	T buf[bufSize];
	T *outPtr = buf;
	size_t outCount = 0;

	for (uint64_t i = 0; i < (uint64_t)w*h; ++i) {
		int32_t curr = *ptr++;
		if (curr > upper)
			curr = upper;
		else if (curr < lower)
			curr = lower;
		if (!grk::writeBytes<T>((T)curr,
							buf,
							&outPtr,
							&outCount,
							bufSize,
							big_endian,
							rawFile))
			return false;
	}
	//flush
	if (outCount) {
		size_t res = fwrite(buf, sizeof(T), outCount, rawFile);
		if (res != outCount)
			return false;
	}

	return true;
}

int RAWFormat::imagetoraw(opj_image_t * image, 
							const char *outfile,
							bool big_endian,
							bool verbose)
{
	bool writeToStdout = grk::useStdio(outfile);
	FILE *rawFile = nullptr;
	unsigned int compno, numcomps;
	int fails;
	if ((image->numcomps * image->x1 * image->y1) == 0) {
		spdlog::error("invalid raw image parameters");
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
		spdlog::error("imagetoraw_common: All components shall have the same subsampling, same bit depth, same sign.");
		return 1;
	}

	if (writeToStdout) {
		if (!grok_set_binary_mode(stdout))
			return 1;
		rawFile = stdout;
	}
	else {
		rawFile = fopen(outfile, "wb");
		if (!rawFile) {
			spdlog::error("Failed to open {} for writing !!\n", outfile);
			return 1;
		}
	}

	fails = 1;
	if (verbose)
		spdlog::info("Raw image characteristics: {} components\n", image->numcomps);

	for (compno = 0; compno < image->numcomps; compno++) {
		if (verbose)
			spdlog::info("Component %u characteristics: {}x{}x{} {}\n", compno, image->comps[compno].w,
				image->comps[compno].h, image->comps[compno].prec, image->comps[compno].sgnd == 1 ? "signed" : "unsigned");

		auto w = image->comps[compno].w;
		auto h = image->comps[compno].h;
		bool sgnd = image->comps[compno].sgnd ;
		auto prec = image->comps[compno].prec;

		int32_t lower = sgnd ? -(1 << (prec-1)) : 0;
		int32_t upper = sgnd? -lower -1 : (1 << image->comps[compno].prec) - 1;
		int32_t *ptr = image->comps[compno].data;

		bool rc;
		if (prec <= 8) {
			if (sgnd)
				rc = write<int8_t>(rawFile, big_endian, ptr, w,h,lower,upper);
			else
				rc = write<uint8_t>(rawFile, big_endian, ptr, w,h,lower,upper);
			if (!rc)
				spdlog::error("failed to write bytes for {}\n", outfile);
		}
		else if (prec <= 16) {
			if (sgnd)
				rc = write<int16_t>(rawFile, big_endian,ptr, w,h,lower,upper);
			else
				rc = write<uint16_t>(rawFile, big_endian,ptr, w,h,lower,upper);
			if (!rc)
				spdlog::error("failed to write bytes for {}\n", outfile);
		}
		else if (image->comps[compno].prec <= 32) {
			spdlog::error("More than 16 bits per component no handled yet");
			goto beach;
		}
		else {
			spdlog::error("invalid precision: {}\n", image->comps[compno].prec);
			goto beach;
		}
	}
	fails = 0;
beach:
	if (!writeToStdout && rawFile){
		if (!grk::safe_fclose(rawFile))
			fails = 1;
	}
	return fails;
}
