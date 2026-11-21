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
// clusterfuzz-testcase-minimized-grk_decompress_fuzzer-6042331325988864).
// The stream is a ~2-billion-column tile with a POC whose end layer is 0
// (CPRL). PacketIter::next_cprl walked every x looking for a packet it could
// never yield, so grk_decompress hung in TileFutureManager::wait. The fix
// returns false from PacketIter::next when the layer/resolution/component
// range is empty. This test only requires the call to RETURN (any bool); a
// hang is caught by the ctest TIMEOUT property. The decode failing cleanly
// is the expected result on this stream.

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

  grk_initialize(nullptr, 0, nullptr);

  grk_decompress_parameters params;
  memset(&params, 0, sizeof(params));
  // Same window the fuzz harness applies.
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
    {
      // The point of the test: this call must RETURN, not hang. Either bool is a
      // pass. A pre-fix build spins forever in the packet iterator and the ctest
      // TIMEOUT fails the test.
      (void)grk_decompress(codec, nullptr);
    }
    grk_object_unref(codec);
  }

  grk_deinitialize();
  printf("packet iter empty poc test returned on %s\n", path.c_str());
  return result;
}
