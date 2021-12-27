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

#include "common.h"
#include "grk_config.h"
#include "stdlib.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_COMPS_MAX 4
int main(int argc, char *argv[]) {
  grk_cparameters param;
  grk_codec *codec = nullptr;
  grk_image *image = nullptr;
  grk_stream *stream = nullptr;
  grk_image_cmptparm params[NUM_COMPS_MAX];
  uint32_t nb_tiles = 0;
  uint64_t data_size = 0;
  size_t len = 0;
  int rc = 1;

#ifdef USING_MCT
  const float mct[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

  const int32_t offsets[] = {128, 128, 128};
#endif

  grk_image_cmptparm *current_param_ptr = nullptr;
  uint32_t i;
  uint8_t *data = nullptr;

  uint16_t num_comps;
  uint32_t image_width;
  uint32_t image_height;
  uint32_t tile_width;
  uint32_t tile_height;
  uint8_t comp_prec;
  bool irreversible;
  const char *output_file;

  grk_initialize(nullptr, 0);

  /* should be test_tile_encoder 3 2000 2000 1000 1000 8 tte1.j2k */
  if (argc == 9) {
    num_comps = (uint16_t)atoi(argv[1]);
    image_width = (uint32_t)atoi(argv[2]);
    image_height = (uint32_t)atoi(argv[3]);
    tile_width = (uint32_t)atoi(argv[4]);
    tile_height = (uint32_t)atoi(argv[5]);
    comp_prec = (uint8_t)atoi(argv[6]);
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
  if (num_comps > NUM_COMPS_MAX)
    goto cleanup;

  nb_tiles = (image_width / tile_width) * (image_height / tile_height);
  data_size =
      (uint64_t)tile_width * tile_height * num_comps * ((comp_prec + 7) / 8);
  if (!data_size)
    goto cleanup;
  data = (uint8_t *)malloc(data_size * sizeof(uint8_t));
  if (!data)
    goto cleanup;
  spdlog::info("Compressing random values -> keep in mind that this is very "
               "hard to compress");
  for (i = 0; i < data_size; ++i)
    data[i] = (uint8_t)i;

  grk_compress_set_default_params(&param);
  /** you may here add custom encoding parameters */
  /* rate specifications */
  /** number of quality layers in the stream */
  param.numlayers = 1;
  param.allocationByQuality = true;
  param.layer_distortion[0] = 20;
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
  /** J2K_CCP_CBLKSTY_TERMALL, J2K_CCP_CBLKSTY_LAZY, J2K_CCP_CBLKSTY_VSC,
   * J2K_CCP_CBLKSTY_SEGSYM, J2K_CCP_CBLKSTY_RESET */
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
  grk_set_MCT(&param, mct, offsets, NUM_COMPS);
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

    current_param_ptr->sgnd = false;
    current_param_ptr->prec = comp_prec;

    current_param_ptr->x0 = 0;
    current_param_ptr->y0 = 0;

    ++current_param_ptr;
  }

  stream = grk_stream_create_file_stream(output_file, 1024 * 1024, false);
  if (!stream) {
    spdlog::error("test_tile_encoder: failed to create a stream from file {}",
                  output_file);
    goto cleanup;
  }

  /* should we do j2k or jp2 ?*/
  len = strlen(output_file);
  if (strcmp(output_file + len - 4, ".jp2") == 0) {
    codec = grk_compress_create(GRK_CODEC_JP2, stream);
  } else {
    codec = grk_compress_create(GRK_CODEC_J2K, stream);
  }
  if (!codec) {
    goto cleanup;
  }

  /* catch events using our callbacks and give a local context */
  grk_set_info_handler(grk::infoCallback, nullptr);
  grk_set_warning_handler(grk::warningCallback, nullptr);
  grk_set_error_handler(grk::errorCallback, nullptr);

  image = grk_image_new(nullptr,num_comps, params, GRK_CLRSPC_SRGB, true);
  if (!image)
    goto cleanup;

  image->x0 = 0;
  image->y0 = 0;
  image->x1 = image_width;
  image->y1 = image_height;
  image->color_space = GRK_CLRSPC_SRGB;

  if (!grk_compress_init(codec, &param, image)) {
    spdlog::error("test_tile_encoder: failed to setup the codec");
    goto cleanup;
  }
  if (!grk_compress_start(codec)) {
    spdlog::error("test_tile_encoder: failed to start compress");
    goto cleanup;
  }

  for (i = 0; i < nb_tiles; ++i) {
    if (!grk_compress_tile(codec, (uint16_t)i, data, data_size)) {
      spdlog::error("test_tile_encoder: failed to write the tile {}", i);
      goto cleanup;
    }
  }

  if (!grk_compress_end(codec)) {
    spdlog::error("test_tile_encoder: failed to end compress");
    goto cleanup;
  }
  rc = 0;
cleanup:
  grk_object_unref(stream);
  grk_object_unref(codec);
  grk_object_unref(&image->obj);

  free(data);
  grk_deinitialize();

  return rc;
}
