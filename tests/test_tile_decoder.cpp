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
#include "common.h"
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
     grk_dparameters  param;
     grk_codec  * codec = nullptr;
    grk_image * image = nullptr;
     grk_stream  * stream = nullptr;
    uint64_t data_size=0;
    uint64_t max_data_size = 1000;
    uint16_t tile_index;
    uint8_t * data  = nullptr;
    bool go_on = true;
    uint32_t nb_comps=0 ;
    uint32_t current_tile_x0,current_tile_y0,current_tile_x1,current_tile_y1;
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

    data = (uint8_t *) malloc(1000);
    if (! data)
        goto beach;

    grk_initialize(nullptr,0);
    stream = grk_stream_create_file_stream(input_file, 1024*1024,true);
    if (!stream) {
        spdlog::error("failed to create the stream from the file");
        goto beach;
    }

    /* Set the default decoding parameters */
    grk_set_default_decoder_parameters(&param);

    /* */
	if (!grk::jpeg2000_file_format(input_file, &param.decod_format)){
        spdlog::error("failed to parse input file format");
    	goto beach;
    }

    /** you may here add custom decoding parameters */
    /* do not use layer decoding limitations */
    param.cp_layer = 0;

    /* do not use resolutions reductions */
    param.cp_reduce = 0;

    /* to decode only a part of the image data */
    /*grk_restrict_decoding(&param,0,0,1000,1000);*/


    switch(param.decod_format) {
    case GRK_J2K_CFMT: {	/* JPEG-2000 codestream */
        /* Get a decoder handle */
        codec = grk_create_decompress(GRK_CODEC_J2K,stream);
        break;
    }
    case GRK_JP2_CFMT: {	/* JPEG 2000 compressed image data */
        /* Get a decoder handle */
        codec = grk_create_decompress(GRK_CODEC_JP2, stream);
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
    if (! grk_setup_decoder(codec, &param)) {
        spdlog::error("j2k_dump: failed to setup the decoder\n");
        goto beach;
    }

    /* Read the main header of the codestream and if necessary the JP2 boxes*/
    if (! grk_read_header(codec,nullptr,&image)) {
        spdlog::error("j2k_to_image: failed to read the header\n");
        goto beach;
    }

    if (!grk_set_decode_area(codec, image, da_x0, da_y0,da_x1, da_y1)) {
        fprintf(stderr,	"[ERROR] j2k_to_image: failed to set the decoded area\n");
        goto beach;
    }


    while (go_on) {
        if (! grk_read_tile_header( codec,
                                    &tile_index,
                                    &data_size,
                                    &current_tile_x0,
                                    &current_tile_y0,
                                    &current_tile_x1,
                                    &current_tile_y1,
                                    &nb_comps,
                                    &go_on))
        	goto beach;

        if (go_on) {
            if (data_size > max_data_size) {
                uint8_t *new_data = (uint8_t *) realloc(data, data_size);
                if (! new_data)
                	goto beach;
                data = new_data;
                max_data_size = data_size;
            }

            if (! grk_decode_tile_data(codec,tile_index,data,data_size))
            	goto beach;
            /** now should inspect image to know the reduction factor and then how to behave with data */
        }
    }

    if (! grk_end_decompress(codec))
        goto beach;

    rc = EXIT_SUCCESS;
    grk_deinitialize();

beach:
	free(data);
	if (stream)
		grk_stream_destroy(stream);
	if (codec)
		grk_destroy_codec(codec);
	if (image)
		grk_image_destroy(image);

    return rc;
}

