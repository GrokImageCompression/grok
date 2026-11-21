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
// clusterfuzz-testcase-minimized-grk_decompress_fuzzer-6015347287982080).
// The stream is a 17 by 4-billion-row RPCL tile with component dy=1. The
// packet iterator treated every other canvas row as a precinct start, then
// failed validatePrecinct at every x, so it never counted a packet against
// the tile's 8 compressed bytes and hung in TileFutureManager::wait. The
// hang only shows up on a memory stream, which is how the fuzz harness
// feeds the file. This test only requires the call to RETURN (any bool);
// a hang is caught by the ctest TIMEOUT property.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "grok.h"

namespace
{
const int DECODES = 3;
} // namespace

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "usage: %s <fuzz codestream>\n", argv[0]);
    return 1;
  }

  auto file = fopen(argv[1], "rb");
  if(!file)
  {
    fprintf(stderr, "cannot open %s\n", argv[1]);
    return 1;
  }
  fseek(file, 0, SEEK_END);
  auto length = (size_t)ftell(file);
  fseek(file, 0, SEEK_SET);
  std::vector<uint8_t> data(length);
  if(fread(data.data(), 1, length, file) != length)
  {
    fclose(file);
    fprintf(stderr, "cannot read %s\n", argv[1]);
    return 1;
  }
  fclose(file);

  grk_initialize(nullptr, 0, nullptr);

  for(int i = 0; i < DECODES; ++i)
  {
    grk_decompress_parameters params;
    memset(&params, 0, sizeof(params));
    // Same window the fuzz harness applies.
    params.dw_x1 = 1024;
    params.dw_y1 = 1024;

    grk_stream_params streamParams;
    memset(&streamParams, 0, sizeof(streamParams));
    streamParams.buf = data.data();
    streamParams.buf_len = data.size();

    grk_object* codec = grk_decompress_init(&streamParams, &params);
    if(codec)
    {
      grk_header_info headerInfo;
      memset(&headerInfo, 0, sizeof(headerInfo));
      if(grk_decompress_read_header(codec, &headerInfo))
        (void)grk_decompress(codec, nullptr);
      grk_object_unref(codec);
    }
  }

  grk_deinitialize();
  printf("packet iter tall tile test returned on %s\n", argv[1]);
  return 0;
}
