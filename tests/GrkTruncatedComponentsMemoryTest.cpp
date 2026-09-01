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

// a truncated stream declaring hundreds of components must only allocate
// wavelet windows for components that have code blocks. the test stream
// declares 257 components and holds a single empty packet.

#include <cstdio>
#include <cstring>
#include <string>

#include "grok.h"

namespace
{
const uint32_t WINDOW_SIZE = 1024;
// well below the 2.5GB peak of the unfixed code, well above the ~600MB
// composite the decode legitimately needs
const long PEAK_RSS_LIMIT_KB = 1500L * 1024;

#ifdef __linux__
long peakRssKb()
{
  FILE* status = fopen("/proc/self/status", "r");
  if(!status)
    return -1;
  char line[256];
  long kb = -1;
  while(fgets(line, sizeof(line), status))
  {
    if(sscanf(line, "VmHWM: %ld kB", &kb) == 1)
      break;
  }
  fclose(status);
  return kb;
}
#endif
} // namespace

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "usage: %s <truncated codestream>\n", argv[0]);
    return 1;
  }

  grk_initialize(nullptr, 0, nullptr);

  grk_decompress_parameters params = {};
  params.dw_x1 = WINDOW_SIZE;
  params.dw_y1 = WINDOW_SIZE;
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", argv[1]);

  int rc = 1;
  grk_object* codec = grk_decompress_init(&streamParams, &params);
  grk_header_info headerInfo = {};
  if(!codec)
    fprintf(stderr, "grk_decompress_init failed\n");
  else if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "grk_decompress_read_header failed\n");
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "grk_decompress failed\n");
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!image || !image->numcomps)
      fprintf(stderr, "decode produced no image\n");
    else
    {
      bool allComponentsHaveData = true;
      for(uint16_t compno = 0; compno < image->numcomps; ++compno)
      {
        if(!image->comps[compno].data)
        {
          fprintf(stderr, "component %u has null data\n", compno);
          allComponentsHaveData = false;
          break;
        }
      }
      if(allComponentsHaveData)
        rc = 0;
    }
  }
  grk_object_unref(codec);
  grk_deinitialize();

#ifdef __linux__
  long kb = peakRssKb();
  if(kb < 0)
    fprintf(stderr, "could not read VmHWM, skipping peak memory check\n");
  else if(kb > PEAK_RSS_LIMIT_KB)
  {
    fprintf(stderr, "peak rss %ld kB exceeds limit %ld kB\n", kb, PEAK_RSS_LIMIT_KB);
    rc = 1;
  }
  else
    printf("peak rss %ld kB\n", kb);
#endif

  return rc;
}
