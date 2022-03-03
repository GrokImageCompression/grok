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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "grk_config.h"
#include "grok.h"

void error_callback(const char *msg, void *v);
void warning_callback(const char *msg, void *v);
void info_callback(const char *msg, void *v);

void error_callback(const char *msg, void *v) {
  (void)msg;
  (void)v;
  puts(msg);
}
void warning_callback(const char *msg, void *v) {
  (void)msg;
  (void)v;
  puts(msg);
}
void info_callback(const char *msg, void *v) {
  (void)msg;
  (void)v;
  puts(msg);
}

int main(int argc, char *argv[]) {
  const char *v = grk_version();

  const GRK_COLOR_SPACE color_space = GRK_CLRSPC_GRAY;
  uint16_t numcomps = 1;
  unsigned int i;
  unsigned int image_width = 256;
  unsigned int image_height = 256;

  grk_cparameters parameters;

  grk_image_comp cmptparm;
  grk_image *image;
  grk_codec *codec = nullptr;
  bool bSuccess;
  grk_stream *l_stream = nullptr;
  (void)argc;
  (void)argv;

  int rc = 1;
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_J2K_FMT;
  puts(v);
  cmptparm.prec = 8;
  cmptparm.sgnd = 0;
  cmptparm.dx = 1;
  cmptparm.dy = 1;
  cmptparm.w = image_width;
  cmptparm.h = image_height;

  image = grk_image_new(numcomps, &cmptparm, color_space);
  assert(image);

  for (i = 0; i < image_width * image_height; i++) {
    uint16_t compno;
    for (compno = 0; compno < numcomps; compno++) {
      image->comps[compno].data[i] = 0;
    }
  }

  grk_set_msg_handlers(info_callback, nullptr,
		  	  	  	  warning_callback, nullptr,
					   error_callback, nullptr);

  l_stream =
      grk_stream_create_file_stream("testempty1.j2k", 1024 * 1024, false);
  assert(l_stream);

  codec = grk_compress_create(GRK_CODEC_J2K, l_stream);
  grk_compress_init(codec, &parameters, image);

  bSuccess = grk_compress_start(codec);
  if (!bSuccess) {
    grk_object_unref(l_stream);
    grk_object_unref(codec);
    grk_object_unref(&image->obj);
    rc = 0;
    goto cleanup;
  }

  assert(bSuccess);
  bSuccess = grk_compress(codec);
  assert(bSuccess);
  bSuccess = grk_compress_end(codec);
  assert(bSuccess);

  grk_object_unref(l_stream);

  grk_object_unref(codec);
  grk_object_unref(&image->obj);

cleanup:
  puts("end");
  return rc;
}
