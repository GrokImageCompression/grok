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

// Regression test for an OSS-Fuzz timeout (testcase
// clusterfuzz-testcase-minimized-grk_decompress_mercury_fuzzer-6523675860598784).
// Mercury bails (precinct smaller than code-block). Classic then walks a
// 1x137 grid of huge tiles and hung in TileFutureManager::wait until the
// packet iterator budget stopped it. This test only requires the call to
// RETURN (any bool); a hang is caught by the ctest TIMEOUT property.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "grok.h"

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "usage: %s <fuzz codestream>\n", argv[0]);
    return 1;
  }
  std::string path = argv[1];

#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "1");
#else
  setenv("GRK_MERCURY", "1", 1);
#endif

  grk_initialize(nullptr, 0, nullptr);

  grk_decompress_parameters params;
  memset(&params, 0, sizeof(params));
  // Same window the mercury fuzz harness applies.
  params.dw_x1 = 1024;
  params.dw_y1 = 1024;

  grk_stream_params streamParams;
  memset(&streamParams, 0, sizeof(streamParams));
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  int result = 0;
  if(!codec)
  {
    fprintf(stderr, "grk_decompress_init failed\n");
    result = 1;
  }
  else
  {
    grk_header_info headerInfo;
    memset(&headerInfo, 0, sizeof(headerInfo));
    if(grk_decompress_read_header(codec, &headerInfo))
      (void)grk_decompress(codec, nullptr);
    grk_object_unref(codec);
  }

  grk_deinitialize();
  printf("mercury fuzzer timeout test returned on %s\n", path.c_str());
  return result;
}
