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
 * Copyright (c) 2011-2012, Centre National d'Etudes Spatiales (CNES), France
 * Copyright (c) 2012, CS Systemes d'Information, France
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

#include "grk_config.h"
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif /* _WIN32 */


/**
sample error callback expecting a FILE* client object
*/
static void error_callback(const char *msg, void *client_data)
{
    (void)client_data;
    spdlog::error("%s", msg);
}
/**
sample warning callback expecting a FILE* client object
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

int main(int argc, char **argv)
{
    uint32_t index;
     grk_dparameters  parameters;			/* decompression parameters */
    grk_image *  image = nullptr;
     grk_stream  *l_stream = nullptr;				/* Stream */
     grk_codec  *  l_codec = nullptr;				/* Handle to a decompressor */
     grk_codestream_info_v2  *  cstr_info = nullptr;

    /* Index of corner tiles */
    uint32_t tile_ul = 0;
    uint32_t tile_ur = 0;
    uint32_t tile_lr = 0;
    uint32_t tile_ll = 0;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Set decoding parameters to default values */
    grk_set_default_decoder_parameters(&parameters);

    strncpy(parameters.infile, argv[1], GRK_PATH_LEN - 1);


    /* decode the JPEG2000 stream */
    /* -------------------------- */
	if (!grk::jpeg2000_file_format(parameters.infile, &parameters.decod_format)){
        spdlog::error("Failed to detect JPEG 2000 file format for file %s\n", parameters.infile);
        return EXIT_FAILURE;
	}

    l_stream = grk_stream_create_file_stream(parameters.infile,1024*1024, 1);
    if (!l_stream) {
        spdlog::error("failed to create the stream from the file %s\n", parameters.infile);
        return EXIT_FAILURE;
    }


    switch(parameters.decod_format) {
    case GRK_J2K_CFMT: {	/* JPEG-2000 codestream */
        /* Get a decoder handle */
        l_codec = grk_create_decompress(GRK_CODEC_J2K, l_stream);
        break;
    }
    case GRK_JP2_CFMT: {	/* JPEG 2000 compressed image data */
        /* Get a decoder handle */
        l_codec = grk_create_decompress(GRK_CODEC_JP2, l_stream);
        break;
    }
    default:
        fprintf(stderr,
                "Unrecognized format for input %s [accept only *.j2k, *.jp2 or *.jpc]\n\n",
                parameters.infile);
        return EXIT_FAILURE;
    }
    grk_initialize(nullptr,0);

    /* catch events using our callbacks and give a local context */
    grk_set_info_handler(info_callback,nullptr);
    grk_set_warning_handler(warning_callback,nullptr);
    grk_set_error_handler(error_callback,nullptr);

    /* Setup the decoder decoding parameters using user parameters */
    if ( !grk_setup_decoder(l_codec, &parameters) ) {
        spdlog::error("j2k_dump: failed to setup the decoder");
        grk_stream_destroy(l_stream);
        grk_destroy_codec(l_codec);
        return EXIT_FAILURE;
    }

    /* Read the main header of the codestream and if necessary the JP2 boxes*/
    if(! grk_read_header(l_codec,nullptr, &image)) {
        spdlog::error("j2k_to_image: failed to read the header");
        grk_stream_destroy(l_stream);
        grk_destroy_codec(l_codec);
        grk_image_destroy(image);
        return EXIT_FAILURE;
    }

    /* Extract some info from the code stream */
    cstr_info = grk_get_cstr_info(l_codec);

    fprintf(stdout, "The file contains %dx%d tiles\n", cstr_info->tw, cstr_info->th);

    tile_ul = 0;
    tile_ur = cstr_info->tw - 1;
    tile_lr = cstr_info->tw * cstr_info->th - 1;
    tile_ll = tile_lr - cstr_info->tw;

#define TEST_TILE( tile_index ) \
	fprintf(stdout, "Decoding tile %d ...\n", tile_index); \
	if(!grk_get_decoded_tile(l_codec, image, tile_index )){ \
		spdlog::error("j2k_to_image: failed to decode tile %d\n", tile_index); \
		grk_stream_destroy(l_stream); \
		grk_destroy_cstr_info(&cstr_info); \
		grk_destroy_codec(l_codec); \
		grk_image_destroy(image); \
		return EXIT_FAILURE; \
	} \
  for(index = 0; index < image->numcomps; ++index) { \
    if( image->comps[index].data == nullptr ){ \
    	spdlog::error("j2k_to_image: failed to decode tile %d\n", tile_index); \
		grk_stream_destroy(l_stream); \
		grk_destroy_cstr_info(&cstr_info); \
		grk_destroy_codec(l_codec); \
		grk_image_destroy(image); \
        return EXIT_FAILURE; \
        } \
  } \
	fprintf(stdout, "Tile %d is decoded successfully\n", tile_index);

    TEST_TILE(tile_ul)
    TEST_TILE(tile_lr)
    TEST_TILE(tile_ul)
    TEST_TILE(tile_ll)
    TEST_TILE(tile_ur)
    TEST_TILE(tile_lr)

    /* Close the byte stream */
    grk_stream_destroy(l_stream);

    /* Destroy code stream info */
    grk_destroy_cstr_info(&cstr_info);

    /* Free remaining structures */
    grk_destroy_codec(l_codec);

    /* Free image data structure */
    grk_image_destroy(image);

    grk_deinitialize();

    return EXIT_SUCCESS;
}
/*end main*/




