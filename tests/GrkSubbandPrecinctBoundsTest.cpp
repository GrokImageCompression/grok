/*
 *    Copyright (C) 2016-2026 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "GrkPeakResidentBytes.h"
#include "grok.h"

namespace
{
constexpr uint32_t decodeWindowSize = 1024;
constexpr uint32_t decodeIterations = 100;
constexpr size_t peakResidentBytesLimit = 768ULL * 1024ULL * 1024ULL;
} // namespace

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "usage: %s <fuzz codestream>\n", argv[0]);
    return 1;
  }

#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "1");
#else
  setenv("GRK_MERCURY", "1", 1);
#endif

  grk_initialize(nullptr, 0, nullptr);

  for(uint32_t iteration = 0; iteration < decodeIterations; ++iteration)
  {
    grk_decompress_parameters parameters{};
    parameters.dw_x1 = decodeWindowSize;
    parameters.dw_y1 = decodeWindowSize;

    grk_stream_params streamParameters{};
    streamParameters.is_read_stream = true;
    snprintf(streamParameters.file, sizeof(streamParameters.file), "%s", argv[1]);

    grk_object* codec = grk_decompress_init(&streamParameters, &parameters);
    if(!codec)
    {
      fprintf(stderr, "grk_decompress_init failed on iteration %u\n", iteration);
      grk_deinitialize();
      return 1;
    }

    grk_header_info headerInfo{};
    if(!grk_decompress_read_header(codec, &headerInfo))
    {
      fprintf(stderr, "grk_decompress_read_header failed on iteration %u\n", iteration);
      grk_object_unref(codec);
      grk_deinitialize();
      return 1;
    }

    (void)grk_decompress(codec, nullptr);
    grk_object_unref(codec);
  }

  grk_deinitialize();

  const size_t measuredPeakResidentBytes = peakResidentBytes();
  if(measuredPeakResidentBytes > peakResidentBytesLimit)
  {
    fprintf(stderr, "peak rss %zu bytes exceeds limit %zu bytes\n", measuredPeakResidentBytes,
            peakResidentBytesLimit);
    return 1;
  }

  return 0;
}
