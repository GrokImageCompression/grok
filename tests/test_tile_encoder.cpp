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
#include "grk_config.h"
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "stdlib.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------- */

/**
 sample error debug callback expecting no client object
 */
static void error_callback(const char *msg, void *client_data) {
	(void) client_data;
	spdlog::error("%s", msg);
}
/**
 sample warning debug callback expecting no client object
 */
static void warning_callback(const char *msg, void *client_data) {
	(void) client_data;
	spdlog::warn("%s", msg);
}
/**
 sample debug callback expecting no client object
 */
static void info_callback(const char *msg, void *client_data) {
	(void) client_data;
	spdlog::info("%s", msg);
}

/* -------------------------------------------------------------------------- */

#define NUM_COMPS_MAX 4
int main(int argc, char *argv[]) {
	grk_cparameters param;
	grk_codec *codec = nullptr;
	grk_image *image = nullptr;
	grk_image_cmptparm params[NUM_COMPS_MAX];
	grk_stream *stream = nullptr;
	uint32_t nb_tiles = 0;
	uint64_t data_size = 0;
	size_t len = 0;
	int rc = 0;

#ifdef USING_MCT
    const float mct [] = {
        1 , 0 , 0 ,
        0 , 1 , 0 ,
        0 , 0 , 1
    };

    const int32_t offsets [] = {
        128 , 128 , 128
    };
#endif

	grk_image_cmptparm *current_param_ptr = nullptr;
	uint32_t i;
	uint8_t *data = nullptr;

	uint32_t num_comps;
	uint32_t image_width;
	uint32_t image_height;
	uint32_t tile_width;
	uint32_t tile_height;
	uint32_t comp_prec;
	bool irreversible;
	const char *output_file;

	grk_initialize(nullptr, 0);

	/* should be test_tile_encoder 3 2000 2000 1000 1000 8 tte1.j2k */
	if (argc == 9) {
		num_comps = (uint32_t)atoi(argv[1]);
		image_width = (uint32_t)atoi(argv[2]);
		image_height = (uint32_t)atoi(argv[3]);
		tile_width = (uint32_t)atoi(argv[4]);
		tile_height = (uint32_t)atoi(argv[5]);
		comp_prec = (uint32_t)atoi(argv[6]);
		irreversible = atoi(argv[7]) ? true : false;
		output_file = argv[8];
	} else {
		num_comps = 3U;
		image_width = 2000U;
		image_height = 2000U;
		tile_width = 1000U;
		tile_height = 1000U;
		comp_prec = 8U;
		irreversible = true;
		output_file = "test.j2k";
	}
	if (num_comps > NUM_COMPS_MAX) {
		rc = 1;
		goto cleanup;
	}
	nb_tiles = (image_width / tile_width) * (image_height / tile_height);
	data_size = (uint64_t) tile_width * tile_height * num_comps
			* (comp_prec / 8);
	if (!data_size) {
		rc = 1;
		goto cleanup;
	}
	data = (uint8_t*) malloc(data_size * sizeof(uint8_t));
	if (!data) {
		rc = 1;
		goto cleanup;
	}

	fprintf(stdout,
			"Encoding random values -> keep in mind that this is very hard to compress\n");
	for (i = 0; i < data_size; ++i) {
		data[i] = (uint8_t) i; /*rand();*/
	}

	grk_set_default_encoder_parameters(&param);
	/** you may here add custom encoding parameters */
	/* rate specifications */
	/** number of quality layers in the stream */
	param.tcp_numlayers = 1;
	param.cp_fixed_quality = 1;
	param.tcp_distoratio[0] = 20;
	/* is using others way of calculation */
	/* param.cp_disto_alloc = 1 or param.cp_fixed_alloc = 1 */
	/* param.tcp_rates[0] = ... */

	/* tile definitions parameters */
	/* position of the tile grid aligned with the image */
	param.tx0 = 0;
	param.ty0 = 0;
	/* tile size, we are using tile based encoding */
	param.tile_size_on = true;
	param.t_width = tile_width;
	param.t_height = tile_height;

	/* use irreversible encoding ?*/
	param.irreversible = irreversible;

	/* do not bother with mct, the rsiz is set when calling grk_set_MCT*/
	/*param.cp_rsiz = GRK_STD_RSIZ;*/

	/* no cinema */
	/*param.cp_cinema = 0;*/

	/* do not bother using SOP or EPH markers, do not use custom size precinct */
	/* number of precincts to specify */
	/* param.csty = 0;*/
	/* param.res_spec = ... */
	/* param.prch_init[i] = .. */
	/* param.prcw_init[i] = .. */

	/* do not use progression order changes */
	/*param.numpocs = 0;*/
	/* param.POC[i].... */

	/* do not restrain the size for a component.*/
	/* param.max_comp_size = 0; */

	/** block encoding style for each component, do not use at the moment */
	/** J2K_CCP_CBLKSTY_TERMALL, J2K_CCP_CBLKSTY_LAZY, J2K_CCP_CBLKSTY_VSC, J2K_CCP_CBLKSTY_SEGSYM, J2K_CCP_CBLKSTY_RESET */
	/* param.mode = 0;*/

	/** number of resolutions */
	param.numresolution = 6;

	/** progression order to use*/
	/** GRK_LRCP, GRK_RLCP, GRK_RPCL, PCRL, CPRL */
	param.prog_order = GRK_LRCP;

	/** no "region" of interest, more precisely component */
	/* param.roi_compno = -1; */
	/* param.roi_shift = 0; */

	/* we are not using multiple tile parts for a tile. */
	/* param.tp_on = 0; */
	/* param.tp_flag = 0; */

	/* if we are using mct */
#ifdef USING_MCT
    grk_set_MCT(&param,mct,offsets,NUM_COMPS);
#endif

	/* image definition */
	current_param_ptr = params;
	for (i = 0; i < num_comps; ++i) {
		/* do not bother bpp useless */
		/*current_param_ptr->bpp = COMP_PREC;*/
		current_param_ptr->dx = 1;
		current_param_ptr->dy = 1;

		current_param_ptr->h = image_height;
		current_param_ptr->w = image_width;

		current_param_ptr->sgnd = 0;
		current_param_ptr->prec = comp_prec;

		current_param_ptr->x0 = 0;
		current_param_ptr->y0 = 0;

		++current_param_ptr;
	}

	stream = grk_stream_create_file_stream(output_file, 1024 * 1024, false);
	if (!stream) {
		spdlog::error(
				"test_tile_encoder: failed to create the stream from the output file %s !\n",
				output_file);
		rc = 1;
		goto cleanup;
	}

	/* should we do j2k or jp2 ?*/
	len = strlen(output_file);
	if (strcmp(output_file + len - 4, ".jp2") == 0) {
		codec = grk_create_compress(GRK_CODEC_JP2, stream);
	} else {
		codec = grk_create_compress(GRK_CODEC_J2K, stream);
	}
	if (!codec) {
		rc = 1;
		goto cleanup;
	}

	/* catch events using our callbacks and give a local context */
	grk_set_info_handler(info_callback, nullptr);
	grk_set_warning_handler(warning_callback, nullptr);
	grk_set_error_handler(error_callback, nullptr);

	image = grk_image_create(num_comps, params, GRK_CLRSPC_SRGB);
	if (!image) {
		rc = 1;
		goto cleanup;
	}

	image->x0 = 0;
	image->y0 = 0;
	image->x1 = image_width;
	image->y1 = image_height;
	image->color_space = GRK_CLRSPC_SRGB;

	if (!grk_setup_encoder(codec, &param, image)) {
		spdlog::error("test_tile_encoder: failed to setup the codec!\n");
		rc = 1;
		goto cleanup;
	}
	if (!grk_start_compress(codec, image)) {
		spdlog::error("test_tile_encoder: failed to start compress!\n");
		rc = 1;
		goto cleanup;
	}

	for (i = 0; i < nb_tiles; ++i) {
		if (!grk_encode_tile(codec, (uint16_t) i, data, data_size)) {
			spdlog::error("test_tile_encoder: failed to write the tile %d!\n",
					i);
			rc = 1;
			goto cleanup;
		}
	}

	if (!grk_end_compress(codec)) {
		spdlog::error("test_tile_encoder: failed to end compress!\n");
		rc = 1;
		goto cleanup;
	}

	cleanup: if (stream)
		grk_stream_destroy(stream);
	if (codec)
		grk_destroy_codec(codec);
	if (image)
		grk_image_destroy(image);

	free(data);

	/* Print profiling*/
	/*PROFPRINT();*/
	grk_deinitialize();

	return rc;
}

