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
 *
 * Copyright (c) 2011-2012, Centre National d'Etudes Spatiales (CNES), France
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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "grk_apps_config.h"
#include "grok.h"
#include "PNMFormat.h"
#include "PGXFormat.h"
#include "convert.h"
#include "common.h"
#include "spdlog/spdlog.h"

#ifdef GROK_HAVE_LIBPNG
#include "PNGFormat.h"
#endif

#ifdef GROK_HAVE_LIBTIFF
#include "TIFFFormat.h"
#include <tiffio.h> /* TIFFSetWarningHandler */
#endif /* GROK_HAVE_LIBTIFF */

#include <string>
#define TCLAP_NAMESTARTSTRING "-"
#include "tclap/CmdLine.h"

using namespace TCLAP;
using namespace std;

/*******************************************************************************
 * Parse MSE and PEAK input values (
 * separator = ":"
 *******************************************************************************/
static double* parseToleranceValues(char *inArg, const size_t nbcomp) {
	if (!nbcomp || !inArg)
		return nullptr;
	double *outArgs = (double*) malloc((size_t) nbcomp * sizeof(double));
	if (!outArgs)
		return nullptr;
	size_t it_comp = 0;
	const char delims[] = ":";
	char *result = strtok(inArg, delims);

	while ((result != nullptr) && (it_comp < nbcomp)) {
		outArgs[it_comp] = atof(result);
		it_comp++;
		result = strtok(nullptr, delims);
	}

	if (it_comp != nbcomp) {
		free(outArgs);
		return nullptr;
	}
	/* else */
	return outArgs;
}

/*******************************************************************************
 * Command line help function
 *******************************************************************************/
static void compare_images_help_display(void) {
	fprintf(stdout, "\nList of parameters for the compare_images utility  \n");
	fprintf(stdout, "\n");
	fprintf(stdout,
			"  -b \t REQUIRED \t file to be used as reference/baseline PGX/TIF/PNM image \n");
	fprintf(stdout, "  -t \t REQUIRED \t file to test PGX/TIF/PNM image\n");
	fprintf(stdout,
			"  -n \t REQUIRED \t number of components in the image (used to generate correct filename; not used when both input files are TIF)\n");
	fprintf(stdout,
			" -d \t OPTIONAL \t indicates that utility will run as non-regression test (otherwise it will run as conformance test)\n");
	fprintf(stdout,
			"  -m \t OPTIONAL \t list of MSE tolerances, separated by : (size must correspond to the number of component) of \n");
	fprintf(stdout,
			"  -p \t OPTIONAL \t list of PEAK tolerances, separated by : (size must correspond to the number of component) \n");
	fprintf(stdout,
			"  -s \t OPTIONAL \t 1 or 2 filename separator to take into account PGX/PNM image with different components, "
					"please indicate b or t before separator to indicate respectively the separator "
					"for ref/base file and for test file.  \n");
	fprintf(stdout,
			"  -R \t OPTIONAL \t Sub-region of base image to compare with test image; comma separated list of four integers: x0,y0,x1,y1 \n");
	fprintf(stdout,
			"  If sub-region is set, then test images dimensions must match sub-region exactly\n");
	fprintf(stdout, "\n");
}

static int get_decod_format_from_string(const char *filename) {
	const int dot = '.';
	char *ext = (char*) strrchr(filename, dot);
	if (strcmp(ext, ".pgx") == 0)
		return GRK_PGX_FMT;
	if (strcmp(ext, ".tif") == 0)
		return GRK_TIF_FMT;
	if (strcmp(ext, ".ppm") == 0)
		return GRK_PXM_FMT;
	if (strcmp(ext, ".png") == 0)
		return GRK_PNG_FMT;
	return -1;
}

/*******************************************************************************
 * Create filenames from a filename using separator and nb components
 * (begin from 0)
 *******************************************************************************/
static char* createMultiComponentsFilename(const char *inFilename,
		const size_t indexF, const char *separator) {
	char s[255];
	char *outFilename, *ptr;
	const char token = '.';
	size_t posToken = 0;
	int decod_format;

	/*spdlog::info("inFilename = {}", inFilename);*/
	if ((ptr = (char*) strrchr(inFilename, token)) != nullptr) {
		posToken = strlen(inFilename) - strlen(ptr);
		/*spdlog::info("Position of {} character inside inFilename = {}\n", token, posToken);*/
	} else {
		/*spdlog::info("Token {} not found", token);*/
		outFilename = (char*) malloc(1);
		if (!outFilename)
			return nullptr;
		outFilename[0] = '\0';
		return outFilename;
	}

	outFilename = (char*) malloc((posToken + 7) * sizeof(char)); /*6*/
	if (!outFilename)
		return nullptr;

	strncpy(outFilename, inFilename, posToken);
	outFilename[posToken] = '\0';
	strcat(outFilename, separator);
	sprintf(s, "%u", (uint32_t)indexF);
	strcat(outFilename, s);

	decod_format = get_decod_format_from_string(inFilename);
	if (decod_format == GRK_PGX_FMT) {
		strcat(outFilename, ".pgx");
	} else if (decod_format == GRK_PXM_FMT) {
		strcat(outFilename, ".pgm");
	}

	/*spdlog::info("outfilename: {}", outFilename);*/
	return outFilename;
}

/*******************************************************************************
 *
 *******************************************************************************/
static grk_image* readImageFromFilePPM(const char *filename, size_t nbFilenamePGX,
		const char *separator) {
	size_t fileno = 0;
	grk_image *src = nullptr;
	grk_image *dest = nullptr;
	grk_cparameters parameters;
	grk_image_cmptparm *src_param = nullptr;
	int **dest_data = nullptr;

	/* If separator is empty => nb file to read is equal to one*/
	if (strlen(separator) == 0)
		nbFilenamePGX = 1;

	if (!nbFilenamePGX)
		return nullptr;

	/* set encoding parameters to default values */
	grk_set_default_compress_params(&parameters);
	parameters.decod_format = GRK_PXM_FMT;
	strcpy(parameters.infile, filename);

	/* Allocate memory*/
	src_param = (grk_image_cmptparm*) malloc(
			(size_t) nbFilenamePGX * sizeof(grk_image_cmptparm));
	if (!src_param)
		goto cleanup;
	dest_data = (int**) calloc((size_t) nbFilenamePGX, sizeof(*dest_data));
	if (!dest_data)
		goto cleanup;

	for (fileno = 0; fileno < nbFilenamePGX; fileno++) {
		/* Create the right filename*/
		char *filenameComponentPGX = nullptr;
		if (strlen(separator) == 0) {
			filenameComponentPGX = (char*) malloc(
					(strlen(filename) + 1) * sizeof(*filenameComponentPGX));
			if (!filenameComponentPGX)
				goto cleanup;
			strcpy(filenameComponentPGX, filename);
		} else
			filenameComponentPGX = createMultiComponentsFilename(filename,
					fileno, separator);

		/* Read the file corresponding to the component */
		PNMFormat pnm(false);
		src = pnm.decode(filenameComponentPGX, &parameters);
		if (!src || !src->comps || !src->comps->h
				|| !src->comps->w) {
			spdlog::error("Unable to load ppm file: {}",
					filenameComponentPGX);
			free(filenameComponentPGX);
			goto cleanup;
		}

		/* Set the image_read parameters*/
		src_param[fileno].x0 = 0;
		src_param[fileno].y0 = 0;
		src_param[fileno].dx = 1;
		src_param[fileno].dy = 1;
		src_param[fileno].h = src->comps->h;
		src_param[fileno].w = src->comps->w;
		src_param[fileno].stride = src->comps->stride;
		src_param[fileno].prec = src->comps->prec;
		src_param[fileno].sgnd = src->comps->sgnd;

		/* Copy data*/
		dest_data[fileno] = (int32_t*) malloc(src_param[fileno].h * src_param[fileno].stride
												* sizeof(int32_t));
		if (!dest_data[fileno]) {
			grk_image_destroy(src);
			free(filenameComponentPGX);
			goto cleanup;
		}
		memcpy(dest_data[fileno], src->comps->data,
				src->comps->h * src->comps->stride * sizeof(int));

		/* Free memory*/
		grk_image_destroy(src);
		free(filenameComponentPGX);
	}

	dest = grk_image_create((uint32_t) nbFilenamePGX, src_param,
			GRK_CLRSPC_UNKNOWN,true);
	if (!dest || !dest->comps)
		goto cleanup;
	for (fileno = 0; fileno < nbFilenamePGX; fileno++) {
		auto dest_comp = dest->comps + fileno;
		if (dest_comp && dest_data[fileno]) {
			memcpy(dest_comp->data, dest_data[fileno], 	dest_comp->h * dest_comp->stride
							* sizeof(int32_t));
			free(dest_data[fileno]);
			dest_data[fileno] = nullptr;
		}
	}

	cleanup:
	free(src_param);
	if (dest_data) {
		for (size_t it_free_data = 0; it_free_data < fileno; it_free_data++)
			free(dest_data[it_free_data]);
		free(dest_data);
	}

	return dest;
}

static grk_image* readImageFromFilePNG(const char *filename, size_t nbFilenamePGX,
		const char *separator) {
	grk_image *image_read = nullptr;
	grk_cparameters parameters;
	(void) nbFilenamePGX;
	(void) separator;

	if (strlen(separator) != 0)
		return nullptr;

	/* set encoding parameters to default values */
	grk_set_default_compress_params(&parameters);
	parameters.decod_format = GRK_TIF_FMT;
	strcpy(parameters.infile, filename);

#ifdef GROK_HAVE_LIBPNG
	PNGFormat png;
	image_read = png.decode(filename, &parameters);
#endif
	if (!image_read) {
		spdlog::error("Unable to load PNG file");
		return nullptr;
	}

	return image_read;
}

static grk_image* readImageFromFileTIF(const char *filename, size_t nbFilenamePGX,
		const char *separator) {
	grk_image *image_read = nullptr;
	grk_cparameters parameters;
	(void) nbFilenamePGX;
	(void) separator;

#ifdef GROK_HAVE_LIBTIFF
	TIFFSetWarningHandler(nullptr);
	TIFFSetErrorHandler(nullptr);
#endif

	if (strlen(separator) != 0)
		return nullptr;

	/* set encoding parameters to default values */
	grk_set_default_compress_params(&parameters);
	parameters.decod_format = GRK_TIF_FMT;
	strcpy(parameters.infile, filename);

#ifdef GROK_HAVE_LIBTIFF
	TIFFFormat tif;
	image_read = tif.decode(filename, &parameters);
#endif
	if (!image_read) {
		spdlog::error("Unable to load TIF file");
		return nullptr;
	}

	return image_read;
}

static grk_image* readImageFromFilePGX(const char *filename, size_t nbFilenamePGX,
		const char *separator) {
	size_t fileno;
	grk_image *src = nullptr;
	grk_image *dest = nullptr;
	grk_cparameters parameters;
	grk_image_cmptparm *dest_param = nullptr;
	int **dest_data = nullptr;

	/* If separator is empty => nb file to read is equal to one*/
	if (strlen(separator) == 0)
		nbFilenamePGX = 1;

	if (!nbFilenamePGX)
		return nullptr;

	/* set encoding parameters to default values */
	grk_set_default_compress_params(&parameters);
	parameters.decod_format = GRK_PGX_FMT;
	strcpy(parameters.infile, filename);

	/* Allocate memory*/
	dest_param = (grk_image_cmptparm*) malloc(
			nbFilenamePGX * sizeof(grk_image_cmptparm));
	if (!dest_param)
		goto cleanup;
	dest_data = (int**) calloc(nbFilenamePGX, sizeof(*dest_data));
	if (!dest_data)
		goto cleanup;

	for (fileno = 0; fileno < nbFilenamePGX; fileno++) {
		/* Create the right filename*/
		char *filenameComponentPGX = nullptr;
		if (strlen(separator) == 0) {
			filenameComponentPGX = (char*) malloc(
					(strlen(filename) + 1) * sizeof(*filenameComponentPGX));
			if (!filenameComponentPGX)
				goto cleanup;
			strcpy(filenameComponentPGX, filename);
		} else {
			filenameComponentPGX = createMultiComponentsFilename(filename,
					fileno, separator);
			if (!filenameComponentPGX)
				goto cleanup;
		}

		/* Read the pgx file corresponding to the component */
		PGXFormat pgx;
		src = pgx.decode(filenameComponentPGX, &parameters);
		if (!src || !src->comps || !src->comps->h
				|| !src->comps->w) {
			spdlog::error("Unable to load pgx file");
			if (filenameComponentPGX)
				free(filenameComponentPGX);
			goto cleanup;
		}

		/* Set the image_read parameters*/
		dest_param[fileno].x0 = 0;
		dest_param[fileno].y0 = 0;
		dest_param[fileno].dx = 1;
		dest_param[fileno].dy = 1;
		dest_param[fileno].h = src->comps->h;
		dest_param[fileno].w = src->comps->w;
		dest_param[fileno].stride = src->comps->stride;
		dest_param[fileno].prec = src->comps->prec;
		dest_param[fileno].sgnd = src->comps->sgnd;

		/* Copy data*/
		dest_data[fileno] = (int32_t*) malloc(
				dest_param[fileno].h * dest_param[fileno].stride
						* sizeof(int32_t));
		if (!dest_data[fileno])
			goto cleanup;
		memcpy(dest_data[fileno], src->comps->data,
				src->comps->h * src->comps->stride * sizeof(int));

		/* Free memory*/
		grk_image_destroy(src);
		free(filenameComponentPGX);
	}

	dest = grk_image_create((uint32_t) nbFilenamePGX, dest_param,
			GRK_CLRSPC_UNKNOWN,true);
	if (!dest || !dest->comps)
		goto cleanup;
	for (fileno = 0; fileno < nbFilenamePGX; fileno++) {
		if ((dest->comps + fileno) && dest_data[fileno]) {
			auto dest_comp = dest->comps + fileno;
			memcpy(dest_comp->data, dest_data[fileno], dest_comp->h * dest_comp->stride
							* sizeof(int32_t));
			free(dest_data[fileno]);
			dest_data[fileno] = nullptr;
		}
	}

	cleanup:
	free(dest_param);
	if (dest_data) {
		for (size_t i = 0; i < fileno; i++)
			free(dest_data[i]);
		free(dest_data);
	}
	return dest;
}

#if defined(GROK_HAVE_LIBPNG)
/*******************************************************************************
 *
 *******************************************************************************/
static int imageToPNG(const grk_image *src, const char *filename, size_t compno) {
	grk_image_cmptparm dest_param;
	auto src_comp = src->comps + compno;
	dest_param.x0 = 0;
	dest_param.y0 = 0;
	dest_param.dx = 1;
	dest_param.dy = 1;
	dest_param.h = src_comp->h;
	dest_param.w = src_comp->w;
	dest_param.prec = src_comp->prec;
	dest_param.sgnd = src_comp->sgnd;

	auto dest = grk_image_create(1u, &dest_param, GRK_CLRSPC_GRAY,true);
	auto dest_comp = dest->comps;
	uint32_t src_diff = src_comp->stride - src_comp->w;
	uint32_t dest_diff = dest_comp->stride - dest_comp->w;
	size_t src_ind = 0, dest_ind=0;
	for (uint32_t j = 0; j < dest_param.h; ++j) {
		memcpy(dest_comp->data + dest_ind, src_comp->data + src_ind, dest_param.w);
		src_ind += src_diff;
		dest_ind += dest_diff;
	}
	PNGFormat png;
	if (!png.encodeHeader(dest, filename, GRK_DECOMPRESS_COMPRESSION_LEVEL_DEFAULT))
		return EXIT_FAILURE;
	if (!png.encodeStrip(dest->comps[0].h))
		return EXIT_FAILURE;
	if (!png.encodeFinish())
		return EXIT_FAILURE;

	grk_image_destroy(dest);

	return EXIT_SUCCESS;
}
#endif

struct test_cmp_parameters {
	/**  */
	char *base_filename;
	/**  */
	char *test_filename;
	/** Number of components */
	uint32_t nbcomp;
	/**  */
	double *tabMSEvalues;
	/**  */
	double *tabPEAKvalues;
	/**  */
	int nr_flag;
	/**  */
	char separator_base[2];
	/**  */
	char separator_test[2];

	uint32_t region[4];
	bool regionSet;

};

/* return decode format PGX / TIF / PPM , return -1 on error */
static int get_decod_format(test_cmp_parameters *param) {
	int base_format = get_decod_format_from_string(param->base_filename);
	int test_format = get_decod_format_from_string(param->test_filename);
	if (base_format != test_format)
		return -1;
	/* handle case -1: */
	return base_format;
}

/*******************************************************************************
 * Parse command line
 *******************************************************************************/

class GrokOutput: public StdOutput {
public:
	virtual void usage(CmdLineInterface &c) {
		(void) c;
		compare_images_help_display();
	}
};

static int parse_cmdline_cmp(int argc, char **argv,
		test_cmp_parameters *param) {
	char *MSElistvalues = nullptr;
	char *PEAKlistvalues = nullptr;
	char *separatorList = nullptr;
	int flagM = 0, flagP = 0;

	/* Init parameters*/
	param->base_filename = nullptr;
	param->test_filename = nullptr;
	param->nbcomp = 0;
	param->tabMSEvalues = nullptr;
	param->tabPEAKvalues = nullptr;
	param->nr_flag = 0;
	param->separator_base[0] = 0;
	param->separator_test[0] = 0;
	param->regionSet = false;

	try {

		// Define the command line object.
		CmdLine cmd("compare_images command line", ' ', "0.9");

		// set the output
		GrokOutput output;
		cmd.setOutput(&output);

		ValueArg<string> baseImageArg("b", "Base", "Base Image", true, "",
				"string", cmd);
		ValueArg<string> testImageArg("t", "Test", "Test Image", true, "",
				"string", cmd);
		ValueArg<uint32_t> numComponentsArg("n", "NumComponents",
				"Number of components", true, 1, "uint32_t", cmd);

		ValueArg<string> mseArg("m", "MSE", "Mean Square Energy", false, "",
				"string", cmd);
		ValueArg<string> psnrArg("p", "PSNR", "Peak Signal To Noise Ratio",
				false, "", "string", cmd);

		SwitchArg nonRegressionArg("d", "NonRegression", "Non regression", cmd);
		ValueArg<string> separatorArg("s", "Separator", "Separator", false, "",
				"string", cmd);

		ValueArg<string> regionArg("R", "SubRegion", "Sub region to compare",
				false, "", "string", cmd);

		cmd.parse(argc, argv);

		if (baseImageArg.isSet()) {
			param->base_filename = (char*) malloc(
					baseImageArg.getValue().size() + 1);
			if (!param->base_filename)
				return 1;
			strcpy(param->base_filename, baseImageArg.getValue().c_str());
			/*spdlog::info("param->base_filename = %s [%u / %u]", param->base_filename, strlen(param->base_filename), sizemembasefile );*/
		}
		if (testImageArg.isSet()) {
			param->test_filename = (char*) malloc(
					testImageArg.getValue().size() + 1);
			if (!param->test_filename)
				return 1;
			strcpy(param->test_filename, testImageArg.getValue().c_str());
			/*spdlog::info("param->test_filename = %s [%u / %u]", param->test_filename, strlen(param->test_filename), sizememtestfile);*/
		}
		if (numComponentsArg.isSet()) {
			param->nbcomp = numComponentsArg.getValue();
		}
		if (mseArg.isSet()) {
			MSElistvalues = (char*) mseArg.getValue().c_str();
			flagM = 1;
		}
		if (psnrArg.isSet()) {
			PEAKlistvalues = (char*) psnrArg.getValue().c_str();
			flagP = 1;
		}
		if (nonRegressionArg.isSet()) {
			param->nr_flag = 1;
		}
		if (separatorArg.isSet()) {
			separatorList = (char*) separatorArg.getValue().c_str();
		}
		if (regionArg.isSet()) {
			uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			if (grk::parse_DA_values((char*) regionArg.getValue().c_str(),
					&x0, &y0, &x1, &y1) == EXIT_SUCCESS) {
				param->region[0] = x0;
				param->region[1] = y0;
				param->region[2] = x1;
				param->region[3] = y1;
				param->regionSet = true;
			}

		}

		if (param->nbcomp == 0) {
			spdlog::error("Need to indicate the number of components !");
			return 1;
		}
		/* else */
		if (flagM && flagP) {
			param->tabMSEvalues = parseToleranceValues(MSElistvalues,
					param->nbcomp);
			param->tabPEAKvalues = parseToleranceValues(PEAKlistvalues,
					param->nbcomp);
			if ((param->tabMSEvalues == nullptr)
					|| (param->tabPEAKvalues == nullptr)) {
				spdlog::error(
						"MSE and PEAK values are not correct (respectively need {} values)",
						param->nbcomp);
				return 1;
			}
		}

		/* Get separators after corresponding letter (b or t)*/
		if (separatorList != nullptr) {
			if ((strlen(separatorList) == 2) || (strlen(separatorList) == 4)) {
				/* keep original string*/
				size_t sizeseplist = strlen(separatorList) + 1;
				char *separatorList2 = (char*) malloc(sizeseplist);
				strcpy(separatorList2, separatorList);
				/*spdlog::info("separatorList2 = %s [%u / %u]", separatorList2, strlen(separatorList2), sizeseplist);*/

				if (strlen(separatorList) == 2) { /* one separator behind b or t*/
					char *resultT = nullptr;
					resultT = strtok(separatorList2, "t");
					if (strlen(resultT) == strlen(separatorList)) { /* didn't find t character, try to find b*/
						char *resultB = nullptr;
						resultB = strtok(resultT, "b");
						if (strlen(resultB) == 1) {
							param->separator_base[0] = separatorList[1];
							param->separator_base[1] = 0;
							param->separator_test[0] = 0;
						} else { /* not found b*/
							free(separatorList2);
							return 1;
						}
					} else { /* found t*/
						param->separator_base[0] = 0;
						param->separator_test[0] = separatorList[1];
						param->separator_test[1] = 0;
					}
					/*spdlog::info("sep b = %s [%u] and sep t = %s [%u]",param->separator_base, strlen(param->separator_base), param->separator_test, strlen(param->separator_test) );*/
				} else { /* == 4 characters we must found t and b*/
					char *resultT = nullptr;
					resultT = strtok(separatorList2, "t");
					if (strlen(resultT) == 3) { /* found t in first place*/
						char *resultB = nullptr;
						resultB = strtok(resultT, "b");
						if (strlen(resultB) == 1) { /* found b after t*/
							param->separator_test[0] = separatorList[1];
							param->separator_test[1] = 0;
							param->separator_base[0] = separatorList[3];
							param->separator_base[1] = 0;
						} else { /* didn't find b after t*/
							free(separatorList2);
							return 1;
						}
					} else { /* == 2, didn't find t in first place*/
						char *resultB = nullptr;
						resultB = strtok(resultT, "b");
						if (strlen(resultB) == 1) { /* found b in first place*/
							param->separator_base[0] = separatorList[1];
							param->separator_base[1] = 0;
							param->separator_test[0] = separatorList[3];
							param->separator_test[1] = 0;
						} else { /* didn't found b in first place => problem*/
							free(separatorList2);
							return 1;
						}
					}
				}
				free(separatorList2);
			} else { /* wrong number of argument after -s*/
				return 1;
			}
		} else {
			if (param->nbcomp == 1) {
				assert(param->separator_base[0] == 0);
				assert(param->separator_test[0] == 0);
			} else {
				spdlog::error("If number of components is > 1, we need separator");
				return 1;
			}
		}
		if ((param->nr_flag) && (flagP || flagM)) {
			spdlog::error("Non-regression flag cannot be used if PEAK or MSE tolerance is specified.");
			return 1;
		}
		if ((!param->nr_flag) && (!flagP || !flagM)) {
			spdlog::info("Non-regression flag must be set if"
					" PEAK or MSE tolerance are not specified. Flag has now been set.");
			param->nr_flag = 1;
		}
	} catch (ArgException &e)  // catch any exceptions
	{
		cerr << "error: " << e.error() << " for arg " << e.argId() << endl;
	}

	return 0;
}

/*******************************************************************************
 * MAIN
 *******************************************************************************/
int main(int argc, char **argv) {

#ifndef NDEBUG
	std::string out;
	for (int i = 0; i < argc; ++i)
		out += std::string(" ") + argv[i];
	spdlog::info("{}", out.c_str());
#endif

	test_cmp_parameters inParam;
	size_t compno;
	int failed = 1;
	uint32_t nbFilenamePGXbase = 0, nbFilenamePGXtest = 0;
	char *filenamePNGtest = nullptr, *filenamePNGbase = nullptr,
			*filenamePNGdiff = nullptr;
	size_t memsizebasefilename, memsizetestfilename;
	size_t memsizedifffilename;
	uint32_t nbPixelDiff = 0;
	double sumDiff = 0.0;
	/* Structures to store image parameters and data*/
	grk_image *imageBase = nullptr, *imageTest = nullptr, *imageDiff = nullptr;
	grk_image_cmptparm *param_image_diff = nullptr;
	int decod_format;

	/* Get parameters from command line*/
	if (parse_cmdline_cmp(argc, argv, &inParam)) {
		compare_images_help_display();
		goto cleanup;
	}

	/* Display Parameters*/
	spdlog::info("******Parameters*********");
	spdlog::info("Base_filename = {}",       inParam.base_filename);
	spdlog::info("Test_filename = {}",       inParam.test_filename);
	spdlog::info("Number of components = {}",inParam.nbcomp);
	spdlog::info("Non-regression test = {}", inParam.nr_flag);
	spdlog::info("Separator Base = {}",      inParam.separator_base);
	spdlog::info("Separator Test = {}",      inParam.separator_test);

	if ((inParam.tabMSEvalues != nullptr)
			&& (inParam.tabPEAKvalues != nullptr)) {
		uint32_t it_comp2;
		spdlog::info(" MSE values = [");
		for (it_comp2 = 0; it_comp2 < inParam.nbcomp; it_comp2++)
			spdlog::info(" {} ", inParam.tabMSEvalues[it_comp2]);
		spdlog::info(" PEAK values = [");
		for (it_comp2 = 0; it_comp2 < inParam.nbcomp; it_comp2++)
			spdlog::info(" {} ", inParam.tabPEAKvalues[it_comp2]);
		spdlog::info(" Non-regression test = {}", inParam.nr_flag);
	}

	if (strlen(inParam.separator_base) != 0)
		nbFilenamePGXbase = inParam.nbcomp;

	if (strlen(inParam.separator_test) != 0)
		nbFilenamePGXtest = inParam.nbcomp;

	spdlog::info("NbFilename to generate from base filename = {}",
			nbFilenamePGXbase);
	spdlog::info("NbFilename to generate from test filename = {}",
			nbFilenamePGXtest);
	spdlog::info("*************************");

	/*----------BASELINE IMAGE--------*/
	memsizebasefilename = strlen(inParam.test_filename) + 1 + 5 + 2 + 4;
	memsizetestfilename = strlen(inParam.test_filename) + 1 + 5 + 2 + 4;

	decod_format = get_decod_format(&inParam);
	if (decod_format == -1) {
		spdlog::error("Unhandled file format");
		goto cleanup;
	}
	assert(
			decod_format == GRK_PGX_FMT || decod_format == GRK_TIF_FMT
					|| decod_format == GRK_PXM_FMT
					|| decod_format == GRK_PNG_FMT);

	if (decod_format == GRK_PGX_FMT) {
		imageBase = readImageFromFilePGX(inParam.base_filename,
				nbFilenamePGXbase, inParam.separator_base);
	} else if (decod_format == GRK_TIF_FMT) {
		imageBase = readImageFromFileTIF(inParam.base_filename,
				nbFilenamePGXbase, "");
	} else if (decod_format == GRK_PXM_FMT) {
		imageBase = readImageFromFilePPM(inParam.base_filename,
				nbFilenamePGXbase, inParam.separator_base);
	} else if (decod_format == GRK_PNG_FMT) {
		imageBase = readImageFromFilePNG(inParam.base_filename,
				nbFilenamePGXbase, inParam.separator_base);
	}

	if (!imageBase)
		goto cleanup;

	filenamePNGbase = (char*) malloc(memsizebasefilename);
	strcpy(filenamePNGbase, inParam.test_filename);
	strcat(filenamePNGbase, ".base");
	/*spdlog::info("filenamePNGbase = %s [%u / %u octets]",filenamePNGbase, strlen(filenamePNGbase),memsizebasefilename );*/

	/*----------TEST IMAGE--------*/

	if (decod_format == GRK_PGX_FMT) {
		imageTest = readImageFromFilePGX(inParam.test_filename,
				nbFilenamePGXtest, inParam.separator_test);
	} else if (decod_format == GRK_TIF_FMT) {
		imageTest = readImageFromFileTIF(inParam.test_filename,
				nbFilenamePGXtest, "");
	} else if (decod_format == GRK_PXM_FMT) {
		imageTest = readImageFromFilePPM(inParam.test_filename,
				nbFilenamePGXtest, inParam.separator_test);
	} else if (decod_format == GRK_PNG_FMT) {
		imageTest = readImageFromFilePNG(inParam.test_filename,
				nbFilenamePGXtest, inParam.separator_test);
	}

	if (!imageTest)
		goto cleanup;

	filenamePNGtest = (char*) malloc(memsizetestfilename);
	strcpy(filenamePNGtest, inParam.test_filename);
	strcat(filenamePNGtest, ".test");
	/*spdlog::info("filenamePNGtest = %s [%u / %u octets]",filenamePNGtest, strlen(filenamePNGtest),memsizetestfilename );*/

	/*----------DIFF IMAGE--------*/

	/* Allocate memory*/
	param_image_diff = (grk_image_cmptparm*) malloc(
			imageBase->numcomps * sizeof(grk_image_cmptparm));

	/* Comparison of header parameters*/
	spdlog::info("Step 1 -> Header comparison");

	/* check dimensions (issue 286)*/
	if (imageBase->numcomps != imageTest->numcomps) {
		spdlog::error("dimension mismatch ({}><{})",
				imageBase->numcomps, imageTest->numcomps);
		goto cleanup;
	}

	for (compno = 0; compno < imageBase->numcomps; compno++) {

		auto baseComp = imageBase->comps + compno;
		auto testComp = imageTest->comps + compno;
		if (baseComp->sgnd != testComp->sgnd) {
			spdlog::error("sign mismatch [comp {}] ({}><{})",
					(uint32_t)compno, baseComp->sgnd, testComp->sgnd);
			goto cleanup;
		}

		if (inParam.regionSet) {
			if (testComp->w != inParam.region[2] - inParam.region[0]) {
				spdlog::error("test image component {} width doesn't match region width {}",
						testComp->w, inParam.region[2] - inParam.region[0]);
				goto cleanup;
			}
			if (testComp->h != inParam.region[3] - inParam.region[1]) {
				spdlog::error("test image component {} height doesn't match region height {}",
						testComp->h, inParam.region[3] - inParam.region[1]);
				goto cleanup;
			}
		} else {

			if (baseComp->h != testComp->h) {
				spdlog::error("height mismatch [comp {}] ({}><{})",
						(uint32_t)compno, baseComp->h, testComp->h);
				goto cleanup;
			}

			if (baseComp->w != testComp->w) {
				spdlog::error("width mismatch [comp {}] ({}><{})",
						(uint32_t)compno, baseComp->w, testComp->w);
				goto cleanup;
			}
		}

		if (baseComp->prec != testComp->prec) {
			spdlog::error("precision mismatch [comp {}] ({}><{})",
					(uint32_t)compno, baseComp->prec, testComp->prec);
			goto cleanup;
		}

		param_image_diff[compno].x0 = 0;
		param_image_diff[compno].y0 = 0;
		param_image_diff[compno].dx = 1;
		param_image_diff[compno].dy = 1;
		param_image_diff[compno].sgnd = testComp->sgnd;
		param_image_diff[compno].prec = testComp->prec;
		param_image_diff[compno].h = testComp->h;
		param_image_diff[compno].w = testComp->w;

	}

	imageDiff = grk_image_create(imageBase->numcomps, param_image_diff,
			GRK_CLRSPC_UNKNOWN,true);
	/* Free memory*/
	free(param_image_diff);
	param_image_diff = nullptr;

	/* Measurement computation*/
	spdlog::info("Step 2 -> measurement comparison");

	memsizedifffilename = strlen(inParam.test_filename) + 1 + 5 + 2 + 4;
	filenamePNGdiff = (char*) malloc(memsizedifffilename);
	strcpy(filenamePNGdiff, inParam.test_filename);
	strcat(filenamePNGdiff, ".diff");
	/*spdlog::info("filenamePNGdiff = %s [%u / %u octets]",filenamePNGdiff, strlen(filenamePNGdiff),memsizedifffilename );*/

	/* Compute pixel diff*/
	for (compno = 0; compno < imageDiff->numcomps; compno++) {
		double SE = 0, PEAK = 0;
		double MSE = 0;
		auto diffComp = imageDiff->comps + compno;
		auto baseComp = imageBase->comps + compno;
		auto testComp = imageTest->comps + compno;
		uint32_t x0 = 0, y0 = 0, x1 = diffComp->w, y1 = diffComp->h;
		// one region for all components
		if (inParam.regionSet) {
			x0 = inParam.region[0];
			y0 = inParam.region[1];
			x1 = inParam.region[2];
			y1 = inParam.region[3];
		}
		for (uint32_t j = y0; j < y1; ++j) {
			for (uint32_t i = x0; i < x1; ++i) {
				auto baseIndex = i + j * baseComp->stride;
				auto testIndex = (i - x0) + (j - y0) * testComp->stride;
				auto basePixel = baseComp->data[baseIndex];
				auto testPixel = testComp->data[testIndex];
				int64_t diff = basePixel - testPixel;
				auto absDiff = llabs(diff);
				if (absDiff > 0) {
					diffComp->data[testIndex] = (int32_t) absDiff;
					sumDiff += (double)diff;
					nbPixelDiff++;

					SE += (double) diff * (double) diff;
					PEAK = (PEAK > (double)absDiff) ? PEAK : (double) absDiff;
				} else
					diffComp->data[testIndex] = 0;

			}
		}
		MSE = SE / ((uint64_t) diffComp->w * diffComp->h);

		if (!inParam.nr_flag && (inParam.tabMSEvalues != nullptr)
				&& (inParam.tabPEAKvalues != nullptr)) {
			/* Conformance test*/
			spdlog::info(
					"<DartMeasurement name=\"PEAK_{}\" type=\"numeric/double\"> {} </DartMeasurement>",
					(uint32_t)compno, PEAK);
			spdlog::info(
					"<DartMeasurement name=\"MSE_{}\" type=\"numeric/double\"> {} </DartMeasurement>",
					(uint32_t)compno, MSE);

			if ((MSE > inParam.tabMSEvalues[compno])
					|| (PEAK > inParam.tabPEAKvalues[compno])) {
				spdlog::error("MSE ({}) or PEAK ({}) values produced by the decoded file are greater "
								"than the allowable error (respectively {} and {})",
						MSE, PEAK, inParam.tabMSEvalues[compno],
						inParam.tabPEAKvalues[compno]);
				goto cleanup;
			}
		} else { /* Non regression-test */
			if (nbPixelDiff != 0) {
				char it_compc[255];
				it_compc[0] = 0;

				spdlog::info(
						"<DartMeasurement name=\"NumberOfPixelsWithDifferences_{}\" type=\"numeric/int\"> {} </DartMeasurement>",
						(uint32_t)compno, nbPixelDiff);
				spdlog::info(
						"<DartMeasurement name=\"ComponentError_{}\" type=\"numeric/double\"> {} </DartMeasurement>",
						(uint32_t)compno, sumDiff);
				spdlog::info(
						"<DartMeasurement name=\"PEAK_{}\" type=\"numeric/double\"> {} </DartMeasurement>",
						(uint32_t)compno, PEAK);
				spdlog::info(
						"<DartMeasurement name=\"MSE_{}\" type=\"numeric/double\"> {} </DartMeasurement>",
						(uint32_t)compno, MSE);

#ifdef GROK_HAVE_LIBPNG
				{
					char *filenamePNGbase_it_comp = nullptr;
					char *filenamePNGtest_it_comp = nullptr;
					char *filenamePNGdiff_it_comp = nullptr;

					filenamePNGbase_it_comp = (char*) malloc(
							memsizebasefilename);
					if (!filenamePNGbase_it_comp) {
						goto cleanup;
					}
					strcpy(filenamePNGbase_it_comp, filenamePNGbase);

					filenamePNGtest_it_comp = (char*) malloc(
							memsizetestfilename);
					if (!filenamePNGtest_it_comp) {
						free(filenamePNGbase_it_comp);
						goto cleanup;
					}
					strcpy(filenamePNGtest_it_comp, filenamePNGtest);

					filenamePNGdiff_it_comp = (char*) malloc(
							memsizedifffilename);
					if (!filenamePNGdiff_it_comp) {
						free(filenamePNGbase_it_comp);
						free(filenamePNGtest_it_comp);
						goto cleanup;
					}
					strcpy(filenamePNGdiff_it_comp, filenamePNGdiff);

					sprintf(it_compc, "_%i", (uint32_t)compno);
					strcat(it_compc, ".png");
					strcat(filenamePNGbase_it_comp, it_compc);
					/*spdlog::info("filenamePNGbase_it = %s [%u / %u octets]",filenamePNGbase_it_comp, strlen(filenamePNGbase_it_comp),memsizebasefilename );*/
					strcat(filenamePNGtest_it_comp, it_compc);
					/*spdlog::info("filenamePNGtest_it = %s [%u / %u octets]",filenamePNGtest_it_comp, strlen(filenamePNGtest_it_comp),memsizetestfilename );*/
					strcat(filenamePNGdiff_it_comp, it_compc);
					/*spdlog::info("filenamePNGdiff_it = %s [%u / %u octets]",filenamePNGdiff_it_comp, strlen(filenamePNGdiff_it_comp),memsizedifffilename );*/

					if (imageToPNG(imageBase, filenamePNGbase_it_comp,
							compno) == EXIT_SUCCESS) {
						spdlog::info(
								"<DartMeasurementFile name=\"BaselineImage_{}\" type=\"image/png\"> {} </DartMeasurementFile>",
								(uint32_t)compno, filenamePNGbase_it_comp);
					}

					if (imageToPNG(imageTest, filenamePNGtest_it_comp,
							compno) == EXIT_SUCCESS) {
						spdlog::info(
								"<DartMeasurementFile name=\"TestImage_{}\" type=\"image/png\"> {} </DartMeasurementFile>",
								(uint32_t)compno, filenamePNGtest_it_comp);
					}

					if (imageToPNG(imageDiff, filenamePNGdiff_it_comp,
							compno) == EXIT_SUCCESS) {
						spdlog::info(
								"<DartMeasurementFile name=\"DiffferenceImage_{}\" type=\"image/png\"> {} </DartMeasurementFile>",
								(uint32_t)compno, filenamePNGdiff_it_comp);
					}

					free(filenamePNGbase_it_comp);
					free(filenamePNGtest_it_comp);
					free(filenamePNGdiff_it_comp);
				}
#endif
				goto cleanup;
			}
		}
	} /* it_comp loop */

	spdlog::info("---- TEST SUCCEEDED ----");
	failed = 0;
	cleanup:
	free(param_image_diff);
	grk_image_destroy(imageBase);
	grk_image_destroy(imageTest);
	grk_image_destroy(imageDiff);

	free(filenamePNGbase);
	free(filenamePNGtest);
	free(filenamePNGdiff);
	free(inParam.tabMSEvalues);
	free(inParam.tabPEAKvalues);
	free(inParam.base_filename);
	free(inParam.test_filename);

	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
