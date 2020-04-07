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
 * Copyright (c) 2008, Jerome Fimes, Communications & Systemes <jerome.fimes@c-s.fr>
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
#define USE_GRK_DEPRECATED
/* set this macro to enable profiling for the given test */
/* warning : in order to be effective, Grok must have been built with profiling enabled !! */
/*#define _PROFILE*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef _WIN32
#include <malloc.h>
#else
#include <stdlib.h>
#endif

#include "grk_config.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif /* _WIN32 */

#include "grok.h"
#include "format_defs.h"
#include "spdlog/spdlog.h"


/* -------------------------------------------------------------------------- */
/* Declarations                                                               */
int get_file_format(const char *filename);
static int infile_format(const char *fname);

/* -------------------------------------------------------------------------- */
int get_file_format(const char *filename)
{
    unsigned int i;
    static const char *extension[] = {"pgx", "pnm", "pgm", "ppm", "bmp","tif", "raw", "rawl", "tga", "png", "j2k", "jp2", "j2c", "jpc" };
    static const int format[] = { PGX_DFMT, PXM_DFMT, PXM_DFMT, PXM_DFMT, BMP_DFMT, TIF_DFMT, RAW_DFMT, RAWL_DFMT, TGA_DFMT, PNG_DFMT, J2K_CFMT, JP2_CFMT, J2K_CFMT, J2K_CFMT };
    char * ext = (char*)strrchr(filename, '.');
    if (ext == nullptr)
        return -1;
    ext++;
    if(ext) {
        for(i = 0; i < sizeof(format)/sizeof(*format); i++) {
            if(strcasecmp(ext, extension[i]) == 0) {
                return format[i];
            }
        }
    }

    return -1;
}

/* -------------------------------------------------------------------------- */
#define JP2_RFC3745_MAGIC "\x00\x00\x00\x0c\x6a\x50\x20\x20\x0d\x0a\x87\x0a"
/* position 45: "\xff\x52" */
#define J2K_CODESTREAM_MAGIC "\xff\x4f\xff\x51"

static int infile_format(const char *fname)
{
    FILE *reader;
    const char *s, *magic_s;
    int ext_format, magic_format;
    unsigned char buf[12];
    unsigned int l_nb_read;

    reader = fopen(fname, "rb");

    if (reader == nullptr)
        return -1;

    memset(buf, 0, 12);
    l_nb_read = (unsigned int)fread(buf, 1, 12, reader);
    fclose(reader);
    if (l_nb_read != 12)
        return -1;

    ext_format = get_file_format(fname);
    if (memcmp(buf, JP2_RFC3745_MAGIC, 12) == 0 ) {
        magic_format = JP2_CFMT;
        magic_s = ".jp2";
    } else if (memcmp(buf, J2K_CODESTREAM_MAGIC, 4) == 0) {
        magic_format = J2K_CFMT;
        magic_s = ".j2k or .jpc or .j2c";
    } else
        return -1;

    if (magic_format == ext_format)
        return ext_format;

    s = fname + strlen(fname) - 4;

    fputs("\n===========================================\n", stderr);
    fprintf(stderr, "The extension of this file is incorrect.\n"
            "FOUND %s. SHOULD BE %s\n", s, magic_s);
    fputs("===========================================\n", stderr);

    return magic_format;
}


/* -------------------------------------------------------------------------- */

/**
  sample error debug callback expecting no client object
 */
static void error_callback(const char *msg, void *client_data)
{
    (void)client_data;
    spdlog::error("%s", msg);
}
/**
  sample warning debug callback expecting no client object
 */
static void warning_callback(const char *msg, void *client_data)
{
    (void)client_data;
    spdlog::warn("%s", msg);
}
/**
  sample debug callback expecting no client object
 */
static void info_callback(const char *msg, void *client_data)
{
    (void)client_data;
    spdlog::info("%s", msg);
}

/* -------------------------------------------------------------------------- */

int main (int argc, char *argv[])
{
     grk_dparameters  l_param;
     grk_codec  * l_codec = nullptr;
    grk_image * l_image = nullptr;
     grk_stream  * l_stream = nullptr;
    uint64_t l_data_size=0;
    uint64_t l_max_data_size = 1000;
    uint16_t l_tile_index;
    uint8_t * l_data  = nullptr;
    bool l_go_on = true;
    uint32_t l_nb_comps=0 ;
    uint32_t l_current_tile_x0,l_current_tile_y0,l_current_tile_x1,l_current_tile_y1;
    int32_t rc = EXIT_FAILURE;
    int temp;

    uint32_t da_x0=0;
	uint32_t da_y0=0;
	uint32_t da_x1=1000;
	uint32_t da_y1=1000;
	const char *input_file;

    /* should be test_tile_decoder 0 0 1000 1000 tte1.j2k */
    if( argc == 6 ) {
		int temp = atoi(argv[1]);
		if (temp < 0) {
			fprintf(stderr, "Error -> invalid decode region\n");
			goto beach;
		}
		else {
			da_x0 = (uint32_t)temp;
		}

		temp = atoi(argv[2]);
		if (temp < 0) {
			fprintf(stderr, "Error -> invalid decode region\n");
			goto beach;
		}
		else {
			da_y0 = (uint32_t)temp;
		}

		temp = atoi(argv[3]);
		if (temp < 0) {
			fprintf(stderr, "Error -> invalid decode region\n");
			goto beach;
		}
		else {
			da_x1 = (uint32_t)temp;
		}

		temp = atoi(argv[4]);
		if (temp < 0) {
			fprintf(stderr, "Error -> invalid decode region\n");
			goto beach;
		}
		else {
			da_y1 = (uint32_t)temp;
		}
		input_file = argv[5];

    } else {
        da_x0=0;
        da_y0=0;
        da_x1=1000;
        da_y1=1000;
        input_file = "test.j2k";
    }

    l_data = (uint8_t *) malloc(1000);
    if (! l_data)
        goto beach;

    grk_initialize(nullptr,0);
    l_stream = grk_stream_create_file_stream(input_file, 1024*1024,true);
    if (!l_stream) {
        spdlog::error("failed to create the stream from the file");
        goto beach;
    }

    /* Set the default decoding parameters */
    grk_set_default_decoder_parameters(&l_param);

    /* */
    temp = infile_format(input_file);
    if (temp == -1){
        spdlog::error("failed to parse input file format");
    	goto beach;
    }
    l_param.decod_format = (uint32_t)temp;

    /** you may here add custom decoding parameters */
    /* do not use layer decoding limitations */
    l_param.cp_layer = 0;

    /* do not use resolutions reductions */
    l_param.cp_reduce = 0;

    /* to decode only a part of the image data */
    /*grk_restrict_decoding(&l_param,0,0,1000,1000);*/


    switch(l_param.decod_format) {
    case J2K_CFMT: {	/* JPEG-2000 codestream */
        /* Get a decoder handle */
        l_codec = grk_create_decompress(GRK_CODEC_J2K,l_stream);
        break;
    }
    case JP2_CFMT: {	/* JPEG 2000 compressed image data */
        /* Get a decoder handle */
        l_codec = grk_create_decompress(GRK_CODEC_JP2, l_stream);
        break;
    }
    default: {
        spdlog::error("Not a valid JPEG2000 file!\n");
        goto beach;
        break;
    }
    }

    /* catch events using our callbacks and give a local context */
    grk_set_info_handler(info_callback,nullptr);
    grk_set_warning_handler(warning_callback,nullptr);
    grk_set_error_handler(error_callback,nullptr);

    /* Setup the decoder decoding parameters using user parameters */
    if (! grk_setup_decoder(l_codec, &l_param)) {
        spdlog::error("j2k_dump: failed to setup the decoder\n");
        goto beach;
    }

    /* Read the main header of the codestream and if necessary the JP2 boxes*/
    if (! grk_read_header(l_codec,nullptr,&l_image)) {
        spdlog::error("j2k_to_image: failed to read the header\n");
        goto beach;
    }

    if (!grk_set_decode_area(l_codec, l_image, da_x0, da_y0,da_x1, da_y1)) {
        fprintf(stderr,	"[ERROR] j2k_to_image: failed to set the decoded area\n");
        goto beach;
    }


    while (l_go_on) {
        if (! grk_read_tile_header( l_codec,
                                    &l_tile_index,
                                    &l_data_size,
                                    &l_current_tile_x0,
                                    &l_current_tile_y0,
                                    &l_current_tile_x1,
                                    &l_current_tile_y1,
                                    &l_nb_comps,
                                    &l_go_on))
        	goto beach;

        if (l_go_on) {
            if (l_data_size > l_max_data_size) {
                uint8_t *l_new_data = (uint8_t *) realloc(l_data, l_data_size);
                if (! l_new_data)
                	goto beach;
                l_data = l_new_data;
                l_max_data_size = l_data_size;
            }

            if (! grk_decode_tile_data(l_codec,l_tile_index,l_data,l_data_size))
            	goto beach;
            /** now should inspect image to know the reduction factor and then how to behave with data */
        }
    }

    if (! grk_end_decompress(l_codec))
        goto beach;

    rc = EXIT_SUCCESS;
    grk_deinitialize();

beach:
	free(l_data);
	if (l_stream)
		grk_stream_destroy(l_stream);
	if (l_codec)
		grk_destroy_codec(l_codec);
	if (l_image)
		grk_image_destroy(l_image);

    return rc;
}

