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

// OSS-Fuzz MSan uninitialized partial 5/3
// (clusterfuzz-testcase-minimized-grk_decompress_mercury_fuzzer-5849385926459392).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <filesystem>

#include "grok.h"

namespace
{

// same window as the mercury fuzz harness
const uint8_t kFuzzCase[] = {
    0xff, 0x4f, 0xff, 0x51, 0x00, 0x2f, 0x20, 0x00, 0x00, 0x00, 0x02, 0x20, 0x00, 0x20, 0x20, 0x20,
    0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x06, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x06, 0x00,
    0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x07, 0x01, 0x4d, 0x0c, 0x01, 0x01,
    0x07, 0x01, 0x01, 0xff, 0x52, 0x00, 0x12, 0x01, 0x00, 0x20, 0x20, 0x01, 0x05, 0x04, 0x04, 0x20,
    0x01, 0xff, 0xff, 0xff, 0x88, 0x88, 0x88, 0xff, 0x5c, 0x00, 0x13, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0xff, 0x90, 0x00, 0x0a,
    0x00, 0x00, 0x20, 0x20, 0x20, 0x20, 0x00, 0x20, 0xff, 0x93, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};

bool fuzzPartialWavelet(void)
{
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "1");
#else
  setenv("GRK_MERCURY", "1", 1);
#endif

  auto path = (std::filesystem::temp_directory_path() / "grk_sparse_canvas_fuzz.j2k").string();
  {
    std::ofstream out(path, std::ios::binary);
    if(!out)
    {
      fprintf(stderr, "fuzzPartialWavelet: could not write %s\n", path.c_str());
      return false;
    }
    out.write(reinterpret_cast<const char*>(kFuzzCase), sizeof(kFuzzCase));
  }

  grk_initialize(nullptr, 1, nullptr);

  grk_decompress_parameters params;
  memset(&params, 0, sizeof(params));
  params.dw_x1 = 1024;
  params.dw_y1 = 1024;

  grk_stream_params streamParams;
  memset(&streamParams, 0, sizeof(streamParams));
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  bool ok = true;
  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "fuzzPartialWavelet: grk_decompress_init failed\n");
    ok = false;
  }
  else
  {
    grk_header_info headerInfo;
    memset(&headerInfo, 0, sizeof(headerInfo));
    if(!grk_decompress_read_header(codec, &headerInfo))
    {
      fprintf(stderr, "fuzzPartialWavelet: read_header failed\n");
      ok = false;
    }
    else if(!grk_decompress(codec, nullptr))
    {
      fprintf(stderr, "fuzzPartialWavelet: decompress failed\n");
      ok = false;
    }
    grk_object_unref(codec);
  }
  grk_deinitialize();
  std::error_code ec;
  std::filesystem::remove(path, ec);
  return ok;
}

} // namespace

int main(void)
{
  if(!fuzzPartialWavelet())
    return 1;
  return 0;
}
