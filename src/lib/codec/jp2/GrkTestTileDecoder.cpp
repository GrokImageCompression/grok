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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <malloc.h>
#else
#include <cstdlib>
#endif

#include "common.h"
#include "grk_config.h"
#include "GrkTestTileDecoder.h"

namespace grk
{

int GrkTestTileDecoder::main(int argc, char* argv[])
{
	grk_decompress_parameters param;
	memset(&param, 0, sizeof(grk_decompress_parameters));
	grk_codec* codec = nullptr;
	grk_stream* stream = nullptr;
	uint16_t tile_index = 0;
	int32_t rc = EXIT_FAILURE;

	uint32_t da_x0 = 0;
	uint32_t da_y0 = 0;
	uint32_t da_x1 = 1000;
	uint32_t da_y1 = 1000;
	const char* input_file;

	/* should be test_tile_decoder 0 0 1000 1000 tte1.j2k */
	if(argc == 6)
	{
		int temp = atoi(argv[1]);
		if(temp < 0)
		{
			spdlog::error("invalid decode region");
			goto beach;
		}
		else
		{
			da_x0 = (uint32_t)temp;
		}

		temp = atoi(argv[2]);
		if(temp < 0)
		{
			spdlog::error("invalid decode region");
			goto beach;
		}
		else
		{
			da_y0 = (uint32_t)temp;
		}

		temp = atoi(argv[3]);
		if(temp < 0)
		{
			spdlog::error("invalid decode region");
			goto beach;
		}
		else
		{
			da_x1 = (uint32_t)temp;
		}

		temp = atoi(argv[4]);
		if(temp < 0)
		{
			spdlog::error("invalid decode region");
			goto beach;
		}
		else
		{
			da_y1 = (uint32_t)temp;
		}
		input_file = argv[5];
	}
	else
	{
		da_x0 = 0;
		da_y0 = 0;
		da_x1 = 1000;
		da_y1 = 1000;
		input_file = "test.j2k";
	}

	grk_initialize(nullptr, 0);
	stream = grk_stream_create_mapped_file_stream(input_file, true);
	if(!stream)
	{
		spdlog::error("failed to create a stream from file {}", input_file);
		goto beach;
	}
	grk_decompress_set_default_params(&param.core);
	if(!grk_decompress_detect_format(input_file, &param.decod_format))
	{
		spdlog::error("failed to parse input file format");
		goto beach;
	}
	param.core.max_layers = 0;
	param.core.reduce = 0;
	codec = grk_decompress_create(stream);
	if(!codec)
	{
		spdlog::error("test tile decoder: failed to create codec\n");
		goto beach;
	}
	grk_set_msg_handlers(grk::infoCallback, nullptr, grk::warningCallback, nullptr,
						 grk::errorCallback, nullptr);
	if(!grk_decompress_init(codec, &param.core))
	{
		spdlog::error("test tile decoder: failed to set up decompressor\n");
		goto beach;
	}
	if(!grk_decompress_read_header(codec, nullptr))
	{
		spdlog::error("test tile decoder: failed to read the header\n");
		goto beach;
	}
	if(!grk_decompress_set_window(codec, da_x0, da_y0, da_x1, da_y1))
	{
		spdlog::error("grk_decompress_set_window: failed to set decompress region\n");
		goto beach;
	}
	if(!grk_decompress_tile(codec, tile_index))
		goto beach;

	rc = EXIT_SUCCESS;
	grk_deinitialize();

beach:
	grk_object_unref(stream);
	grk_object_unref(codec);

	return rc;
}

} // namespace grk
