/*
*    Copyright (C) 2016-2017 Grok Image Compression Inc.
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


extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "opj_apps_config.h"
#include "opj_getopt.h"

#include "openjpeg.h"
#include "format_defs.h"
#include "convert.h"

#ifdef OPJ_HAVE_LIBTIFF
#include <tiffio.h> /* TIFFSetWarningHandler */
#endif /* OPJ_HAVE_LIBTIFF */

}
#include <string>


/*******************************************************************************
 * Parse MSE and PEAK input values (
 * separator = ":"
 *******************************************************************************/
static double* parseToleranceValues( char* inArg, const int nbcomp)
{
	if (!nbcomp || !inArg)
		return nullptr;
    double* outArgs= (double*)malloc((size_t)nbcomp * sizeof(double));
	if (!outArgs)
		return nullptr;
    int it_comp = 0;
    const char delims[] = ":";
    char *result = strtok( inArg, delims );

    while( (result != nullptr) && (it_comp < nbcomp )) {
        outArgs[it_comp] = atof(result);
		it_comp++;
		result = strtok( nullptr, delims );
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
static void compare_images_help_display(void)
{
    fprintf(stdout,"\nList of parameters for the compare_images utility  \n");
    fprintf(stdout,"\n");
    fprintf(stdout,"  -b \t REQUIRED \t file to be used as reference/baseline PGX/TIF/PNM image \n");
    fprintf(stdout,"  -t \t REQUIRED \t file to test PGX/TIF/PNM image\n");
	fprintf(stdout, "  -n \t REQUIRED \t number of components in the image (used to generate correct filename; not used when both input files are TIF)\n");
	fprintf(stdout, " -d \t OPTIONAL \t indicates that utility will run as non-regression test (otherwise it will run as conformance test)\n");
    fprintf(stdout,"  -m \t OPTIONAL \t list of MSE tolerances, separated by : (size must correspond to the number of component) of \n");
    fprintf(stdout,"  -p \t OPTIONAL \t list of PEAK tolerances, separated by : (size must correspond to the number of component) \n");
    fprintf(stdout,"  -s \t OPTIONAL \t 1 or 2 filename separator to take into account PGX/PNM image with different components, "
            "please indicate b or t before separator to indicate respectively the separator "
            "for ref/base file and for test file.  \n");
    fprintf(stdout,"\n");
}

static int get_decod_format_from_string(const char *filename)
{
    const int dot = '.';
    char * ext = (char*)strrchr(filename, dot);
    if( strcmp(ext,".pgx") == 0 )
		return PGX_DFMT;
    if( strcmp(ext,".tif") == 0 )
		return TIF_DFMT;
    if( strcmp(ext,".ppm") == 0 )
		return PXM_DFMT;
	if (strcmp(ext, ".png") == 0)
		return PNG_DFMT;
    return -1;
}


/*******************************************************************************
 * Create filenames from a filename using separator and nb components
 * (begin from 0)
 *******************************************************************************/
static char* createMultiComponentsFilename(const char* inFilename, const int indexF, const char* separator)
{
    char s[255];
    char *outFilename, *ptr;
    const char token = '.';
    size_t posToken = 0;
    int decod_format;

    /*printf("inFilename = %s\n", inFilename);*/
    if ((ptr = (char*)strrchr(inFilename, token)) != nullptr) {
        posToken = strlen(inFilename) - strlen(ptr);
        /*printf("Position of %c character inside inFilename = %d\n", token, posToken);*/
    } else {
        /*printf("Token %c not found\n", token);*/
        outFilename = (char*)malloc(1);
		if (!outFilename)
			return nullptr;
        outFilename[0] = '\0';
        return outFilename;
    }

    outFilename = (char*)malloc((posToken + 7) * sizeof(char)); /*6*/
	if (!outFilename)
		return nullptr;

    strncpy(outFilename, inFilename, posToken);
    outFilename[posToken] = '\0';
    strcat(outFilename, separator);
    sprintf(s, "%i", indexF);
    strcat(outFilename, s);

    decod_format = get_decod_format_from_string(inFilename);
    if( decod_format == PGX_DFMT ) {
        strcat(outFilename, ".pgx");
    } else if( decod_format == PXM_DFMT ) {
        strcat(outFilename, ".pgm");
    }

    /*printf("outfilename: %s\n", outFilename);*/
    return outFilename;
}

/*******************************************************************************
 *
 *******************************************************************************/
static opj_image_t* readImageFromFilePPM(const char* filename, int nbFilenamePGX, const char *separator)
{
    int it_file=0;
    opj_image_t* image_read = nullptr;
    opj_image_t* image = nullptr;
    opj_cparameters_t parameters;
    opj_image_cmptparm_t* param_image_read=nullptr;
    int** data = nullptr;

    /* If separator is empty => nb file to read is equal to one*/
    if ( strlen(separator) == 0 )
        nbFilenamePGX = 1;

	if (!nbFilenamePGX)
		return nullptr;

    /* set encoding parameters to default values */
    opj_set_default_encoder_parameters(&parameters);
    parameters.decod_format = PXM_DFMT;
    strcpy(parameters.infile, filename);

    /* Allocate memory*/
    param_image_read = (opj_image_cmptparm_t*)malloc((size_t)nbFilenamePGX * sizeof(opj_image_cmptparm_t));
	if (!param_image_read)
		goto cleanup;
    data = (int**)calloc((size_t)nbFilenamePGX, sizeof(*data));
	if (!data)
		goto cleanup;

    for (it_file = 0; it_file < nbFilenamePGX; it_file++) {
        /* Create the right filename*/
        char *filenameComponentPGX=nullptr;
        if (strlen(separator) == 0) {
            filenameComponentPGX = (char*)malloc((strlen(filename) + 1) * sizeof(*filenameComponentPGX));
			if (!filenameComponentPGX)
				goto cleanup;
            strcpy(filenameComponentPGX, filename);
        } else
            filenameComponentPGX = createMultiComponentsFilename(filename, it_file, separator);

        /* Read the tif file corresponding to the component */
        image_read = pnmtoimage(filenameComponentPGX, &parameters);
        if (!image_read || !image_read->comps || !image_read->comps->h || !image_read->comps->w) {
            fprintf(stderr, "Unable to load ppm file: %s\n", filenameComponentPGX);
			if (filenameComponentPGX)
				free(filenameComponentPGX);
			goto cleanup;
        }

        /* Set the image_read parameters*/
        param_image_read[it_file].x0 = 0;
        param_image_read[it_file].y0 = 0;
        param_image_read[it_file].dx = 0;
        param_image_read[it_file].dy = 0;
        param_image_read[it_file].h = image_read->comps->h;
        param_image_read[it_file].w = image_read->comps->w;
        param_image_read[it_file].prec = image_read->comps->prec;
        param_image_read[it_file].sgnd = image_read->comps->sgnd;

        /* Copy data*/
        data[it_file] = (int*)malloc(param_image_read[it_file].h * param_image_read[it_file].w * sizeof(int));
		if (!data[it_file]) {
			if (image_read)
				opj_image_destroy(image_read);
			if (filenameComponentPGX)
				free(filenameComponentPGX);
			goto cleanup;
		}
        memcpy(data[it_file], image_read->comps->data, image_read->comps->h * image_read->comps->w * sizeof(int));

        /* Free memory*/
		if (image_read)
			opj_image_destroy(image_read);
		if (filenameComponentPGX)
			free(filenameComponentPGX);
    }

    image = opj_image_create((uint32_t)nbFilenamePGX, param_image_read, OPJ_CLRSPC_UNSPECIFIED);
	if (!image || !image->comps)
		goto cleanup;
    for (it_file = 0; it_file < nbFilenamePGX; it_file++) {
		if ((image->comps + it_file) && data[it_file]) {
			memcpy(image->comps[it_file].data, data[it_file], image->comps[it_file].h * image->comps[it_file].w * sizeof(int));
			free(data[it_file]);
			data[it_file] = nullptr;
		}
    }

cleanup:
    if (param_image_read)
		free(param_image_read);
	if (data) {
		for (int it_free_data = 0; it_free_data < it_file; it_free_data++) {
			if (data[it_free_data])
				free(data[it_free_data]);
		}
		free(data);
	}


    return image;
}

static opj_image_t* readImageFromFilePNG(const char* filename, int nbFilenamePGX, const char *separator)
{
	opj_image_t* image_read = nullptr;
	opj_cparameters_t parameters;
	(void)nbFilenamePGX;
	(void)separator;

	if (strlen(separator) != 0) return nullptr;

	/* set encoding parameters to default values */
	opj_set_default_encoder_parameters(&parameters);
	parameters.decod_format = TIF_DFMT;
	strcpy(parameters.infile, filename);

#ifdef OPJ_HAVE_LIBPNG
	image_read = pngtoimage(filename, &parameters);
#endif
	if (!image_read) {
		fprintf(stderr, "Unable to load PNG file\n");
		return nullptr;
	}

	return image_read;
}

static opj_image_t* readImageFromFileTIF(const char* filename, int nbFilenamePGX, const char *separator)
{
    opj_image_t* image_read = nullptr;
    opj_cparameters_t parameters;
    (void)nbFilenamePGX;
    (void)separator;

    /* conformance test suite produce annoying warning/error:
     * TIFFReadDirectory: Warning, /.../data/baseline/conformance/jp2_1.tif: unknown field with tag 37724 (0x935c) encountered.
     * TIFFOpen: /.../data/baseline/nonregression/opj_jp2_1.tif: Cannot open.
     * On Win32 this open a message box by default, so remove it from the test suite:
     */
#ifdef OPJ_HAVE_LIBTIFF
    TIFFSetWarningHandler(nullptr);
    TIFFSetErrorHandler(nullptr);
#endif

    if ( strlen(separator) != 0 ) return nullptr;

    /* set encoding parameters to default values */
    opj_set_default_encoder_parameters(&parameters);
    parameters.decod_format = TIF_DFMT;
    strcpy(parameters.infile, filename);

#ifdef OPJ_HAVE_LIBTIFF
    image_read = tiftoimage(filename, &parameters, false);
#endif
    if (!image_read) {
        fprintf(stderr, "Unable to load TIF file\n");
        return nullptr;
    }

    return image_read;
}

static opj_image_t* readImageFromFilePGX(const char* filename, int nbFilenamePGX, const char *separator)
{
    int it_file;
    opj_image_t* image_read = nullptr;
    opj_image_t* image = nullptr;
    opj_cparameters_t parameters;
    opj_image_cmptparm_t* param_image_read=nullptr;
    int** data=nullptr;

    /* If separator is empty => nb file to read is equal to one*/
    if ( strlen(separator) == 0 )
        nbFilenamePGX = 1;

	if (!nbFilenamePGX)
		return nullptr;

    /* set encoding parameters to default values */
    opj_set_default_encoder_parameters(&parameters);
    parameters.decod_format = PGX_DFMT;
    strcpy(parameters.infile, filename);

    /* Allocate memory*/
    param_image_read = (opj_image_cmptparm_t*)malloc((size_t)nbFilenamePGX * sizeof(opj_image_cmptparm_t));
	if (!param_image_read)
		goto cleanup;
    data = (int**)calloc((size_t)nbFilenamePGX,sizeof(*data));
	if (!data)
		goto cleanup;

    for (it_file = 0; it_file < nbFilenamePGX; it_file++) {
        /* Create the right filename*/
        char *filenameComponentPGX = nullptr;
        if (strlen(separator) == 0) {
            filenameComponentPGX = (char*)malloc((strlen(filename) + 1) * sizeof(*filenameComponentPGX));
			if (!filenameComponentPGX)
				goto cleanup;
            strcpy(filenameComponentPGX, filename);
		}
		else {
			filenameComponentPGX = createMultiComponentsFilename(filename, it_file, separator);
			if (!filenameComponentPGX)
				goto cleanup;
		}

        /* Read the pgx file corresponding to the component */
        image_read = pgxtoimage(filenameComponentPGX, &parameters);
		if (!image_read || !image_read->comps || !image_read->comps->h || !image_read->comps->w) {
            fprintf(stderr, "Unable to load pgx file\n");
			if (filenameComponentPGX)
				free(filenameComponentPGX);
			goto cleanup;
        }

        /* Set the image_read parameters*/
        param_image_read[it_file].x0 = 0;
        param_image_read[it_file].y0 = 0;
        param_image_read[it_file].dx = 0;
        param_image_read[it_file].dy = 0;
        param_image_read[it_file].h = image_read->comps->h;
        param_image_read[it_file].w = image_read->comps->w;
        param_image_read[it_file].prec = image_read->comps->prec;
        param_image_read[it_file].sgnd = image_read->comps->sgnd;

        /* Copy data*/
        data[it_file] = (int*)malloc(param_image_read[it_file].h * param_image_read[it_file].w * sizeof(int));
		if (!data[it_file])
			goto cleanup;
        memcpy(data[it_file], image_read->comps->data, image_read->comps->h * image_read->comps->w * sizeof(int));

        /* Free memory*/
        opj_image_destroy(image_read);
        free(filenameComponentPGX);
    }

    image = opj_image_create((uint32_t)nbFilenamePGX, param_image_read, OPJ_CLRSPC_UNSPECIFIED);
	if (!image || !image->comps)
		goto cleanup;
    for (it_file = 0; it_file < nbFilenamePGX; it_file++) {
		if ((image->comps + it_file) && data[it_file]) {
			memcpy(image->comps[it_file].data, data[it_file], image->comps[it_file].h * image->comps[it_file].w * sizeof(int));
			free(data[it_file]);
			data[it_file] = nullptr;
		}
    }

cleanup:
	if (param_image_read)
		free(param_image_read);
	if (data) {
		for (int it_free_data = 0; it_free_data < it_file; it_free_data++) {
			if (data[it_free_data])
				free(data[it_free_data]);
		}
		free(data);
	}
    return image;
}

#if defined(OPJ_HAVE_LIBPNG)
/*******************************************************************************
 *
 *******************************************************************************/
static int imageToPNG(const opj_image_t* image, const char* filename, int num_comp_select)
{
    opj_image_cmptparm_t param_image_write;
    opj_image_t* image_write = nullptr;

    param_image_write.x0 = 0;
    param_image_write.y0 = 0;
    param_image_write.dx = 0;
    param_image_write.dy = 0;
    param_image_write.h = image->comps[num_comp_select].h;
    param_image_write.w = image->comps[num_comp_select].w;
    param_image_write.prec = image->comps[num_comp_select].prec;
    param_image_write.sgnd = image->comps[num_comp_select].sgnd;

    image_write = opj_image_create(1u, &param_image_write, OPJ_CLRSPC_GRAY);
    memcpy(image_write->comps->data, image->comps[num_comp_select].data, param_image_write.h * param_image_write.w * sizeof(int));

    imagetopng(image_write, filename, DECOMPRESS_COMPRESSION_LEVEL_DEFAULT);

    opj_image_destroy(image_write);

    return EXIT_SUCCESS;
}
#endif

typedef struct test_cmp_parameters {
    /**  */
    char* base_filename;
    /**  */
    char* test_filename;
    /** Number of components */
    int nbcomp;
    /**  */
    double* tabMSEvalues;
    /**  */
    double* tabPEAKvalues;
    /**  */
    int nr_flag;
    /**  */
    char separator_base[2];
    /**  */
    char separator_test[2];

} test_cmp_parameters;

/* return decode format PGX / TIF / PPM , return -1 on error */
static int get_decod_format(test_cmp_parameters* param)
{
    int base_format = get_decod_format_from_string( param->base_filename );
    int test_format = get_decod_format_from_string( param->test_filename );
    if( base_format != test_format ) return -1;
    /* handle case -1: */
    return base_format;
}

/*******************************************************************************
 * Parse command line
 *******************************************************************************/
static int parse_cmdline_cmp(int argc, char **argv, test_cmp_parameters* param)
{
    char *MSElistvalues = nullptr;
    char *PEAKlistvalues= nullptr;
    char *separatorList = nullptr;
    size_t sizemembasefile, sizememtestfile;
    int index, flagM=0, flagP=0;
    const char optlist[] = "b:t:n:m:p:s:d";
    int c;

    /* Init parameters*/
    param->base_filename = nullptr;
    param->test_filename = nullptr;
    param->nbcomp = 0;
    param->tabMSEvalues = nullptr;
    param->tabPEAKvalues = nullptr;
    param->nr_flag = 0;
    param->separator_base[0] = 0;
    param->separator_test[0] = 0;

    opj_opterr = 0;

    while ((c = opj_getopt(argc, argv, optlist)) != -1)
        switch (c) {
        case 'b':
            sizemembasefile = strlen(opj_optarg) + 1;
            param->base_filename = (char*) malloc(sizemembasefile);
			if (!param->base_filename)
				return 1;
            strcpy(param->base_filename, opj_optarg);
            /*printf("param->base_filename = %s [%d / %d]\n", param->base_filename, strlen(param->base_filename), sizemembasefile );*/
            break;
        case 't':
            sizememtestfile = strlen(opj_optarg) + 1;
            param->test_filename = (char*) malloc(sizememtestfile);
			if (!param->test_filename)
				return 1;
            strcpy(param->test_filename, opj_optarg);
            /*printf("param->test_filename = %s [%d / %d]\n", param->test_filename, strlen(param->test_filename), sizememtestfile);*/
            break;
        case 'n':
            param->nbcomp = atoi(opj_optarg);
            break;
        case 'm':
            MSElistvalues = opj_optarg;
            flagM = 1;
            break;
        case 'p':
            PEAKlistvalues = opj_optarg;
            flagP = 1;
            break;
        case 'd':
            param->nr_flag = 1;
            break;
        case 's':
            separatorList = opj_optarg;
            break;
        case '?':
            if ((opj_optopt == 'b') || (opj_optopt == 't') || (opj_optopt == 'n') || (opj_optopt == 'p') || (opj_optopt == 'm') || (opj_optopt
                    == 's'))
                fprintf(stderr, "Option -%c requires an argument.\n", opj_optopt);
            else if (isprint(opj_optopt)) 
				fprintf(stderr, "Unknown option `-%c'.\n", opj_optopt);
            else 
				fprintf(stderr, "Unknown option character `\\x%x'.\n", opj_optopt);
            return 1;
        default:
            fprintf(stderr, "WARNING -> this option is not valid \"-%c %s\"\n", c, opj_optarg);
            break;
        }

    if (opj_optind != argc) {
        for (index = opj_optind; index < argc; index++)
            fprintf(stderr,"Non-option argument %s\n", argv[index]);
        return 1;
    }

    if (param->nbcomp == 0) {
        fprintf(stderr,"Need to indicate the number of components !\n");
        return 1;
    }
    /* else */
    if ( flagM && flagP ) {
        param->tabMSEvalues = parseToleranceValues( MSElistvalues, param->nbcomp);
        param->tabPEAKvalues = parseToleranceValues( PEAKlistvalues, param->nbcomp);
        if ( (param->tabMSEvalues == nullptr) || (param->tabPEAKvalues == nullptr)) {
            fprintf(stderr,"MSE and PEAK values are not correct (respectively need %d values)\n",param->nbcomp);
            return 1;
        }
    }

    /* Get separators after corresponding letter (b or t)*/
    if (separatorList != nullptr) {
        if( (strlen(separatorList) ==2) || (strlen(separatorList) ==4) ) {
            /* keep original string*/
            size_t sizeseplist = strlen(separatorList)+1;
            char* separatorList2 = (char*)malloc( sizeseplist );
            strcpy(separatorList2, separatorList);
            /*printf("separatorList2 = %s [%d / %d]\n", separatorList2, strlen(separatorList2), sizeseplist);*/

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
                /*printf("sep b = %s [%d] and sep t = %s [%d]\n",param->separator_base, strlen(param->separator_base), param->separator_test, strlen(param->separator_test) );*/
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
            assert( param->separator_base[0] == 0 );
            assert( param->separator_test[0] == 0 );
        } else {
            fprintf(stderr,"If number of components is > 1, we need separator\n");
            return 1;
        }
    }
    if ( (param->nr_flag) && (flagP || flagM) ) {
        fprintf(stderr,"Non-regression flag cannot be used if PEAK or MSE tolerance is specified.\n");
        return 1;
    }
    if ( (!param->nr_flag) && (!flagP || !flagM) ) {
        fprintf(stdout,"Non-regression flag must be set if PEAK or MSE tolerance are not specified. Flag has now been set.\n");
		param->nr_flag = 1;
    }
    return 0;
}

/*******************************************************************************
 * MAIN
 *******************************************************************************/
int main(int argc, char **argv)
{

#ifndef NDEBUG
	std::string out;
	for (int i = 0; i < argc; ++i) {
		out += std::string(" ") + argv[i];
	}
	out += "\n";
	printf(out.c_str());
#endif

    test_cmp_parameters inParam;
    uint32_t it_comp, itpxl;
    int failed = 1;
    int nbFilenamePGXbase = 0, nbFilenamePGXtest = 0;
    char *filenamePNGtest= nullptr, *filenamePNGbase = nullptr, *filenamePNGdiff = nullptr;
    size_t memsizebasefilename, memsizetestfilename;
    size_t memsizedifffilename;
    int32_t nbPixelDiff = 0;
    double sumDiff = 0.0;
    /* Structures to store image parameters and data*/
    opj_image_t *imageBase = nullptr, *imageTest = nullptr, *imageDiff = nullptr;
    opj_image_cmptparm_t* param_image_diff = nullptr;
    int decod_format;

    /* Get parameters from command line*/
    if( parse_cmdline_cmp(argc, argv, &inParam) ) {
        compare_images_help_display();
        goto cleanup;
    }

    /* Display Parameters*/
    printf("******Parameters********* \n");
    printf("Base_filename = %s\n"
           "Test_filename = %s\n"
           "Number of components = %d\n"
           "Non-regression test = %d\n"
           "Separator Base = %s\n"
           "Separator Test = %s\n",
           inParam.base_filename, inParam.test_filename, inParam.nbcomp,
           inParam.nr_flag, inParam.separator_base, inParam.separator_test);

    if ( (inParam.tabMSEvalues != nullptr) && (inParam.tabPEAKvalues != nullptr)) {
        int it_comp2;
        printf(" MSE values = [");
        for (it_comp2 = 0; it_comp2 < inParam.nbcomp; it_comp2++)
            printf(" %f ", inParam.tabMSEvalues[it_comp2]);
        printf("]\n");
        printf(" PEAK values = [");
        for (it_comp2 = 0; it_comp2 < inParam.nbcomp; it_comp2++)
            printf(" %f ", inParam.tabPEAKvalues[it_comp2]);
        printf("]\n");
        printf(" Non-regression test = %d\n", inParam.nr_flag);
    }

    if (strlen(inParam.separator_base) != 0)
        nbFilenamePGXbase = inParam.nbcomp;

    if (strlen(inParam.separator_test) != 0)
        nbFilenamePGXtest = inParam.nbcomp;

    printf("NbFilename to generate from base filename = %d\n", nbFilenamePGXbase);
    printf("NbFilename to generate from test filename = %d\n", nbFilenamePGXtest);
    printf("************************* \n");

    /*----------BASELINE IMAGE--------*/
    memsizebasefilename = strlen(inParam.test_filename) + 1 + 5 + 2 + 4;
    memsizetestfilename = strlen(inParam.test_filename) + 1 + 5 + 2 + 4;

    decod_format = get_decod_format(&inParam);
    if( decod_format == -1 ) {
        fprintf( stderr, "Unhandled file format\n" );
        goto cleanup;
    }
    assert( decod_format == PGX_DFMT || decod_format == TIF_DFMT || decod_format == PXM_DFMT || decod_format == PNG_DFMT );

    if( decod_format == PGX_DFMT ) {
        imageBase = readImageFromFilePGX( inParam.base_filename, nbFilenamePGXbase, inParam.separator_base);
    } else if( decod_format == TIF_DFMT ) {
        imageBase = readImageFromFileTIF( inParam.base_filename, nbFilenamePGXbase, "");
    } else if( decod_format == PXM_DFMT ) {
        imageBase = readImageFromFilePPM( inParam.base_filename, nbFilenamePGXbase, inParam.separator_base);
    }
	else if (decod_format == PNG_DFMT) {
		imageBase = readImageFromFilePNG(inParam.base_filename, nbFilenamePGXbase, inParam.separator_base);
	}

	if (!imageBase)
		goto cleanup;

    filenamePNGbase = (char*) malloc(memsizebasefilename);
    strcpy(filenamePNGbase, inParam.test_filename);
    strcat(filenamePNGbase, ".base");
    /*printf("filenamePNGbase = %s [%d / %d octets]\n",filenamePNGbase, strlen(filenamePNGbase),memsizebasefilename );*/

    /*----------TEST IMAGE--------*/

    if( decod_format == PGX_DFMT ) {
        imageTest = readImageFromFilePGX(inParam.test_filename, nbFilenamePGXtest, inParam.separator_test);
    } else if( decod_format == TIF_DFMT ) {
        imageTest = readImageFromFileTIF(inParam.test_filename, nbFilenamePGXtest, "");
    } else if( decod_format == PXM_DFMT ) {
        imageTest = readImageFromFilePPM(inParam.test_filename, nbFilenamePGXtest, inParam.separator_test);
    }
	else if (decod_format == PNG_DFMT) {
		imageTest = readImageFromFilePNG(inParam.test_filename, nbFilenamePGXtest, inParam.separator_test);
	}

	if (!imageTest)
		goto cleanup;

    filenamePNGtest = (char*) malloc(memsizetestfilename);
    strcpy(filenamePNGtest, inParam.test_filename);
    strcat(filenamePNGtest, ".test");
    /*printf("filenamePNGtest = %s [%d / %d octets]\n",filenamePNGtest, strlen(filenamePNGtest),memsizetestfilename );*/

    /*----------DIFF IMAGE--------*/

    /* Allocate memory*/
    param_image_diff = (opj_image_cmptparm_t*)malloc( imageBase->numcomps * sizeof(opj_image_cmptparm_t));

    /* Comparison of header parameters*/
    printf("Step 1 -> Header comparison\n");

    /* check dimensions (issue 286)*/
    if(imageBase->numcomps != imageTest->numcomps ) {
        printf("ERROR: dimension mismatch (%d><%d)\n", imageBase->numcomps, imageTest->numcomps);
        goto cleanup;
    }

    for (it_comp = 0; it_comp < imageBase->numcomps; it_comp++) {
        param_image_diff[it_comp].x0 = 0;
        param_image_diff[it_comp].y0 = 0;
        param_image_diff[it_comp].dx = 0;
        param_image_diff[it_comp].dy = 0;
        param_image_diff[it_comp].sgnd = 0;
        param_image_diff[it_comp].prec = 8;
        param_image_diff[it_comp].h = imageBase->comps[it_comp].h;
        param_image_diff[it_comp].w = imageBase->comps[it_comp].w;

        if (imageBase->comps[it_comp].sgnd != imageTest->comps[it_comp].sgnd) {
            printf("ERROR: sign mismatch [comp %d] (%d><%d)\n", it_comp, ((imageBase->comps)[it_comp]).sgnd, ((imageTest->comps)[it_comp]).sgnd);
            goto cleanup;
        }

        if (((imageBase->comps)[it_comp]).prec != ((imageTest->comps)[it_comp]).prec) {
            printf("ERROR: precision mismatch [comp %d] (%d><%d)\n", it_comp, ((imageBase->comps)[it_comp]).prec, ((imageTest->comps)[it_comp]).prec);
            goto cleanup;
        }

        if (((imageBase->comps)[it_comp]).h != ((imageTest->comps)[it_comp]).h) {
            printf("ERROR: height mismatch [comp %d] (%d><%d)\n", it_comp, ((imageBase->comps)[it_comp]).h, ((imageTest->comps)[it_comp]).h);
            goto cleanup;
        }

        if (((imageBase->comps)[it_comp]).w != ((imageTest->comps)[it_comp]).w) {
            printf("ERROR: width mismatch [comp %d] (%d><%d)\n", it_comp, ((imageBase->comps)[it_comp]).w, ((imageTest->comps)[it_comp]).w);
            goto cleanup;
        }
    }

    imageDiff = opj_image_create(imageBase->numcomps, param_image_diff, OPJ_CLRSPC_UNSPECIFIED);
    /* Free memory*/
    free(param_image_diff);
    param_image_diff = nullptr;

    /* Measurement computation*/
    printf("Step 2 -> measurement comparison\n");

    memsizedifffilename = strlen(inParam.test_filename) + 1 + 5 + 2 + 4;
    filenamePNGdiff = (char*) malloc(memsizedifffilename);
    strcpy(filenamePNGdiff, inParam.test_filename);
    strcat(filenamePNGdiff, ".diff");
    /*printf("filenamePNGdiff = %s [%d / %d octets]\n",filenamePNGdiff, strlen(filenamePNGdiff),memsizedifffilename );*/

    /* Compute pixel diff*/
    for (it_comp = 0; it_comp < imageDiff->numcomps; it_comp++) {
        double SE=0,PEAK=0;
        double MSE=0;
        for (itpxl = 0; itpxl < ((imageDiff->comps)[it_comp]).w * ((imageDiff->comps)[it_comp]).h; itpxl++) {
			auto basePixel = (imageBase->comps)[it_comp].data[itpxl];
			auto testPixel = (imageTest->comps)[it_comp].data[itpxl];
			int64_t diff = basePixel - testPixel;
            if (llabs(diff) > 0) {
				((imageDiff->comps)[it_comp]).data[itpxl] = (uint32_t)llabs(diff);
                sumDiff += diff;
                nbPixelDiff++;

                SE += (double)diff * diff;
                PEAK = (PEAK > llabs(diff)) ? PEAK : llabs(diff);
            } else
                ((imageDiff->comps)[it_comp]).data[itpxl] = 0;
        }/* h*w loop */

        MSE = SE / ( ((imageDiff->comps)[it_comp]).w * ((imageDiff->comps)[it_comp]).h );

        if (!inParam.nr_flag && (inParam.tabMSEvalues != nullptr) && (inParam.tabPEAKvalues != nullptr)) {
            /* Conformance test*/
            printf("<DartMeasurement name=\"PEAK_%d\" type=\"numeric/double\"> %f </DartMeasurement> \n", it_comp, PEAK);
            printf("<DartMeasurement name=\"MSE_%d\" type=\"numeric/double\"> %f </DartMeasurement> \n", it_comp, MSE);

            if ( (MSE > inParam.tabMSEvalues[it_comp]) || (PEAK > inParam.tabPEAKvalues[it_comp]) ) {
                printf("ERROR: MSE (%f) or PEAK (%f) values produced by the decoded file are greater "
                       "than the allowable error (respectively %f and %f) \n",
                       MSE, PEAK, inParam.tabMSEvalues[it_comp], inParam.tabPEAKvalues[it_comp]);
                goto cleanup;
            }
        } else { /* Non regression-test */
            if ( nbPixelDiff > 0) {
                char it_compc[255];
                it_compc[0] = 0;

                printf("<DartMeasurement name=\"NumberOfPixelsWithDifferences_%d\" type=\"numeric/int\"> %d </DartMeasurement> \n", it_comp, nbPixelDiff);
                printf("<DartMeasurement name=\"ComponentError_%d\" type=\"numeric/double\"> %f </DartMeasurement> \n", it_comp, sumDiff);
         printf("<DartMeasurement name=\"PEAK_%d\" type=\"numeric/double\"> %f </DartMeasurement> \n", it_comp, PEAK);
         printf("<DartMeasurement name=\"MSE_%d\" type=\"numeric/double\"> %f </DartMeasurement> \n", it_comp, MSE);

#ifdef OPJ_HAVE_LIBPNG
                {
					char *filenamePNGbase_it_comp = nullptr;
					char* filenamePNGtest_it_comp = nullptr;
					char* filenamePNGdiff_it_comp = nullptr;

                    filenamePNGbase_it_comp = (char*) malloc(memsizebasefilename);
					if (!filenamePNGbase_it_comp) {
						goto cleanup;
					}
                    strcpy(filenamePNGbase_it_comp,filenamePNGbase);

                    filenamePNGtest_it_comp = (char*) malloc(memsizetestfilename);
					if (!filenamePNGtest_it_comp) {
						if (filenamePNGbase_it_comp)
							free(filenamePNGbase_it_comp);
						goto cleanup;
					}
                    strcpy(filenamePNGtest_it_comp,filenamePNGtest);

                    filenamePNGdiff_it_comp = (char*) malloc(memsizedifffilename);
					if (!filenamePNGdiff_it_comp) {
						if (filenamePNGbase_it_comp)
							free(filenamePNGbase_it_comp);
						if (filenamePNGtest_it_comp)
							free(filenamePNGtest_it_comp);
						goto cleanup;
					}
                    strcpy(filenamePNGdiff_it_comp,filenamePNGdiff);

                    sprintf(it_compc, "_%i", it_comp);
                    strcat(it_compc,".png");
                    strcat(filenamePNGbase_it_comp, it_compc);
                    /*printf("filenamePNGbase_it = %s [%d / %d octets]\n",filenamePNGbase_it_comp, strlen(filenamePNGbase_it_comp),memsizebasefilename );*/
                    strcat(filenamePNGtest_it_comp, it_compc);
                    /*printf("filenamePNGtest_it = %s [%d / %d octets]\n",filenamePNGtest_it_comp, strlen(filenamePNGtest_it_comp),memsizetestfilename );*/
                    strcat(filenamePNGdiff_it_comp, it_compc);
                    /*printf("filenamePNGdiff_it = %s [%d / %d octets]\n",filenamePNGdiff_it_comp, strlen(filenamePNGdiff_it_comp),memsizedifffilename );*/

                    
                    if ( imageToPNG(imageBase, filenamePNGbase_it_comp, it_comp) == EXIT_SUCCESS )
                    {
                    printf("<DartMeasurementFile name=\"BaselineImage_%d\" type=\"image/png\"> %s </DartMeasurementFile> \n", it_comp, filenamePNGbase_it_comp);
                    }

                    if ( imageToPNG(imageTest, filenamePNGtest_it_comp, it_comp) == EXIT_SUCCESS )
                    {
                    printf("<DartMeasurementFile name=\"TestImage_%d\" type=\"image/png\"> %s </DartMeasurementFile> \n", it_comp, filenamePNGtest_it_comp);
                    }

                    if ( imageToPNG(imageDiff, filenamePNGdiff_it_comp, it_comp) == EXIT_SUCCESS )
                    {
                    printf("<DartMeasurementFile name=\"DiffferenceImage_%d\" type=\"image/png\"> %s </DartMeasurementFile> \n", it_comp, filenamePNGdiff_it_comp);
                    }

					if (filenamePNGbase_it_comp)
						free(filenamePNGbase_it_comp);
					if (filenamePNGtest_it_comp)
						free(filenamePNGtest_it_comp);
					if (filenamePNGdiff_it_comp)
						free(filenamePNGdiff_it_comp);
                }
#endif
                goto cleanup;
            }
        }
    } /* it_comp loop */

    printf("---- TEST SUCCEED ----\n");
    failed = 0;
cleanup:
    if (param_image_diff)
		free(param_image_diff);
    if (imageBase)
		opj_image_destroy(imageBase);
	if (imageTest)
		opj_image_destroy(imageTest);
	if (imageDiff)
		opj_image_destroy(imageDiff);

	if (filenamePNGbase)
		free(filenamePNGbase);
	if (filenamePNGtest)
		free(filenamePNGtest);
	if (filenamePNGdiff)
		free(filenamePNGdiff);

	if (inParam.tabMSEvalues)
		free(inParam.tabMSEvalues);
	if (inParam.tabPEAKvalues)
		free(inParam.tabPEAKvalues);
	if (inParam.base_filename)
		free(inParam.base_filename);
	if (inParam.test_filename)
		free(inParam.test_filename);

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
