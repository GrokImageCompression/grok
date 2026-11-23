/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
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
 */

// a code block whose early segments have no data made the bypass decoder run a cleanup pass
// on a raw segment, reading MQ state that was never set up

#include <cstdio>
#include <cstdlib>
#include <string>

#include "grok.h"

int main(int argc, char** argv)
{
  if(argc != 2)
  {
    fprintf(stderr, "usage: %s <codestream>\n", argv[0]);
    return 1;
  }
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "0");
#else
  setenv("GRK_MERCURY", "0", 1);
#endif
  grk_initialize(nullptr, 0, nullptr);

  grk_decompress_parameters params = {};
  params.dw_x1 = 1024;
  params.dw_y1 = 1024;
  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", argv[1]);
  streamParams.is_read_stream = true;

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "grk_decompress_init failed\n");
    return 1;
  }
  grk_header_info headerInfo = {};
  bool ok = grk_decompress_read_header(codec, &headerInfo) && grk_decompress(codec, nullptr);
  if(!ok)
    fprintf(stderr, "decompress failed\n");
  grk_object_unref(codec);
  grk_deinitialize();
  return ok ? 0 : 1;
}
