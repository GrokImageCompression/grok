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

// Regression test for an OSS-Fuzz heap-buffer-overflow (testcase
// clusterfuzz-testcase-minimized-grk_decompress_mercury_fuzzer-6270477761576960).
// Under GRK_MERCURY the fast path marks multiTileComposite_ int16 (BIBO-headroom
// rule), then bails on this stream leaving the composite type mutated. The classic
// windowed decode that follows inherited int16 while the partial tiles decoded
// int32, so compositing memcpy'd 4 bytes/element into a 2-byte buffer. This runs
// the exact path with a 1024x1024 decode window and requires it to complete.

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
  // Same window the mercury fuzz harness applies; this is what forces the classic
  // fallback into partial (int32) tiles against the composite.
  params.dw_x1 = 1024;
  params.dw_y1 = 1024;

  grk_stream_params streamParams;
  memset(&streamParams, 0, sizeof(streamParams));
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "grk_decompress_init failed\n");
    grk_deinitialize();
    return 1;
  }

  grk_header_info headerInfo;
  memset(&headerInfo, 0, sizeof(headerInfo));
  int result = 0;
  if(!grk_decompress_read_header(codec, &headerInfo))
  {
    fprintf(stderr, "grk_decompress_read_header failed\n");
    result = 1;
  }
  // A crash here (heap-buffer-overflow) is the regression. A clean bool is a pass:
  // the composite and tile sample types now agree, so the decode runs to completion.
  else if(!grk_decompress(codec, nullptr))
  {
    fprintf(stderr, "grk_decompress returned failure\n");
    result = 1;
  }

  grk_object_unref(codec);
  grk_deinitialize();
  if(result == 0)
    printf("mercury composite type test passed on %s\n", path.c_str());
  return result;
}
