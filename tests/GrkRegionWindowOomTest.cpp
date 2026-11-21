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

// OSS-Fuzz out-of-memory (clusterfuzz-testcase-minimized-grk_decompress_fuzzer-6199551120441344):
// a region decode of 257 components allocated band and split buffers it never read

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "GrkPeakResidentBytes.h"
#include "grok.h"

// the output alone is about 800 MB, the unused buffers took it to 2.5 GB
static const size_t peak_resident_bytes_cap = 1536ULL * 1024ULL * 1024ULL;

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
  // same window as the fuzz harness
  params.dw_x1 = 1024;
  params.dw_y1 = 1024;

  grk_stream_params streamParams;
  memset(&streamParams, 0, sizeof(streamParams));
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(codec)
  {
    grk_header_info headerInfo;
    memset(&headerInfo, 0, sizeof(headerInfo));
    if(grk_decompress_read_header(codec, &headerInfo))
      (void)grk_decompress(codec, nullptr);
    grk_object_unref(codec);
  }
  grk_deinitialize();

  size_t peak = peakResidentBytes();
  printf("region window oom test peak rss %zu bytes on %s\n", peak, path.c_str());
  if(peak > peak_resident_bytes_cap)
  {
    fprintf(stderr, "peak rss %zu bytes exceeds cap %zu bytes\n", peak, peak_resident_bytes_cap);
    return 1;
  }
  return 0;
}
