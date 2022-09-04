/*
 *    Copyright (C) 2016-2022 Grok Image Compression Inc.
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
 *    This source code incorporates work covered by the BSD 2-clause license.
 *    Please see the LICENSE file in the root directory for details.
 */

#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common.h"
#include "grk_config.h"
#include "GrkTestTileEncoder.h"

namespace grk
{

#define NUM_COMPS_MAX 4
int GrkTestTileEncoder::main(int argc, char* argv[])
{
	grk_cparameters param;
	grk_codec* codec = nullptr;
	grk_stream_params stream_params;
	grk_image* image = nullptr;
	grk_image_comp params[NUM_COMPS_MAX];
	uint32_t nb_tiles = 0;
	uint64_t data_size = 0;
	size_t len = 0;
	int rc = 1;

#ifdef USING_MCT
	const float mct[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

	const int32_t offsets[] = {128, 128, 128};
#endif

	grk_image_comp* current_param_ptr = nullptr;
	uint32_t i;
	uint8_t* data = nullptr;

	uint16_t num_comps;
	uint32_t image_width;
	uint32_t image_height;
	uint32_t tile_width;
	uint32_t tile_height;
	uint8_t comp_prec;
	bool irreversible;
	const char* output_file;

	grk_initialize(nullptr, 0);

	/* should be test_tile_encoder 3 2000 2000 1000 1000 8 tte1.j2k */
	if(argc == 9)
	{
		num_comps = (uint16_t)atoi(argv[1]);
		image_width = (uint32_t)atoi(argv[2]);
		image_height = (uint32_t)atoi(argv[3]);
		tile_width = (uint32_t)atoi(argv[4]);
		tile_height = (uint32_t)atoi(argv[5]);
		comp_prec = (uint8_t)atoi(argv[6]);
		irreversible = atoi(argv[7]) ? true : false;
		output_file = argv[8];
	}
	else
	{
		num_comps = 3U;
		image_width = 2000U;
		image_height = 2000U;
		tile_width = 1000U;
		tile_height = 1000U;
		comp_prec = 8U;
		irreversible = true;
		output_file = "test.j2k";
	}
	if(num_comps > NUM_COMPS_MAX)
		goto cleanup;

	nb_tiles = (image_width / tile_width) * (image_height / tile_height);
	data_size = (uint64_t)tile_width * tile_height * num_comps * (uint8_t)((comp_prec + 7) / 8);
	if(!data_size)
		goto cleanup;
	data = new uint8_t[data_size];
	spdlog::info("Compressing random values -> keep in mind that this is very "
				 "hard to compress");
	for(i = 0; i < data_size; ++i)
		data[i] = (uint8_t)i;

	grk_compress_set_default_params(&param);
	param.numlayers = 1;
	param.allocationByQuality = true;
	param.layer_distortion[0] = 20;
	param.tile_size_on = true;
	param.t_width = tile_width;
	param.t_height = tile_height;
	param.irreversible = irreversible;
	param.numresolution = 6;
	param.prog_order = GRK_LRCP;
#ifdef USING_MCT
	grk_set_MCT(&param, mct, offsets, NUM_COMPS);
#endif
	current_param_ptr = params;
	for(i = 0; i < num_comps; ++i)
	{
		current_param_ptr->dx = 1;
		current_param_ptr->dy = 1;
		current_param_ptr->h = image_height;
		current_param_ptr->w = image_width;
		current_param_ptr->sgnd = false;
		current_param_ptr->prec = comp_prec;
		current_param_ptr->x0 = 0;
		current_param_ptr->y0 = 0;
		++current_param_ptr;
	}

	memset(&stream_params, 0, sizeof(stream_params));
	stream_params.file = output_file;
	len = strlen(output_file);
	if(strcmp(output_file + len - 4, ".jp2") == 0)
		param.cod_format = GRK_FMT_JP2;
	else
		param.cod_format = GRK_FMT_J2K;
	grk_set_msg_handlers(grk::infoCallback, nullptr, grk::warningCallback, nullptr,
						 grk::errorCallback, nullptr);
	image = grk_image_new(num_comps, params, GRK_CLRSPC_SRGB);
	if(!image)
		goto cleanup;

	image->x0 = 0;
	image->y0 = 0;
	image->x1 = image_width;
	image->y1 = image_height;
	image->color_space = GRK_CLRSPC_SRGB;

	codec = grk_compress_init(&stream_params, &param, image);
	if(!codec)
	{
		spdlog::error("test_tile_encoder: failed to setup the codec");
		goto cleanup;
	}
	for(i = 0; i < nb_tiles; ++i)
	{
		if(!grk_compress_tile(codec, (uint16_t)i, data, data_size))
		{
			spdlog::error("test_tile_encoder: failed to write the tile {}", i);
			goto cleanup;
		}
	}

	if(!grk_compress_end(codec))
	{
		spdlog::error("test_tile_encoder: failed to end compress");
		goto cleanup;
	}
	rc = 0;
cleanup:
	grk_object_unref(codec);
	grk_object_unref(&image->obj);

	delete[] data;
	grk_deinitialize();

	return rc;
}

}; // namespace grk
