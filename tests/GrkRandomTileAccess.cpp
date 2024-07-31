/*
 *    Copyright (C) 2016-2024 Grok Image Compression Inc.
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
#endif /* _WIN32 */

#include "grok.h"
#include "grk_config.h"
#include "GrkRandomTileAccess.h"

namespace grk
{

static void errorCallback(const char* msg, [[maybe_unused]] void* client_data)
{
   fprintf(stderr, "Error: %s\n", msg);
}
static void warningCallback(const char* msg, [[maybe_unused]] void* client_data)
{
   fprintf(stdin, "Warning: %s\n", msg);
}
static void infoCallback(const char* msg, [[maybe_unused]] void* client_data)
{
   fprintf(stdin, "Info: %s\n", msg);
}

static int32_t test_tile(uint16_t tile_index, grk_image* image, grk_codec* codec)
{
   fprintf(stdout, "Decompressing tile %d ...", tile_index);
   if(!grk_decompress_tile(codec, tile_index))
   {
	  fprintf(stderr, "random tile processor: failed to decompress tile %d", tile_index);
	  return EXIT_FAILURE;
   }
   for(uint32_t index = 0; index < image->numcomps; ++index)
   {
	  if(image->comps[index].data == nullptr)
	  {
		 fprintf(stderr, "random tile processor: failed to decompress tile %d", tile_index);
		 return EXIT_FAILURE;
	  }
   }
   fprintf(stdin, "Tile %d decoded successfully", tile_index);
   return EXIT_SUCCESS;
}

int GrkRandomTileAccess::main(int argc, char** argv)
{
   grk_decompress_parameters parameters; /* decompression parameters */
   int32_t ret = EXIT_FAILURE, rc;

   if(argc != 2)
   {
	  fprintf(stderr, "Usage: %s <input_file>", argv[0]);
	  return EXIT_FAILURE;
   }

   grk_initialize(nullptr, 0, true);
   grk_set_msg_handlers(infoCallback, nullptr, warningCallback, nullptr, errorCallback, nullptr);

   for(uint32_t i = 0; i < 4; ++i)
   {
	  grk_codec* codec = nullptr; /* Handle to a decompressor */
	  grk_image* image = nullptr;

	  grk_decompress_set_default_params(&parameters);
	  strncpy(parameters.infile, argv[1], GRK_PATH_LEN - 1);

	  /* Index of corner tiles */
	  uint16_t tile[4];

	  grk_stream_params stream_params;
	  memset(&stream_params, 0, sizeof(stream_params));
	  stream_params.file = parameters.infile;
	  codec = grk_decompress_init(&stream_params, &parameters);
	  if(!codec)
	  {
		 fprintf(stderr, "random tile processor: failed to set up decompressor");
		 goto cleanup;
	  }

	  /* Read the main header of the codestream and if necessary the JP2 boxes*/
	  grk_header_info headerInfo;
	  memset(&headerInfo, 0, sizeof(grk_header_info));
	  if(!grk_decompress_read_header(codec, &headerInfo))
	  {
		 fprintf(stderr, "random tile processor : failed to read header");
		 goto cleanup;
	  }

	  fprintf(stdin, "The file contains %dx%d tiles", headerInfo.t_grid_width,
			  headerInfo.t_grid_height);

	  tile[0] = 0;
	  tile[1] = (uint16_t)(headerInfo.t_grid_width - 1);
	  tile[2] = (uint16_t)(headerInfo.t_grid_width * headerInfo.t_grid_height - 1);
	  tile[3] = (uint16_t)(tile[2] - headerInfo.t_grid_width);

	  image = grk_decompress_get_composited_image(codec);
	  rc = test_tile(tile[i], image, codec);

	  grk_object_unref(codec);
	  if(rc)
		 goto cleanup;
   }
   ret = EXIT_SUCCESS;
cleanup:
   grk_deinitialize();

   return ret;
}

} // namespace grk
