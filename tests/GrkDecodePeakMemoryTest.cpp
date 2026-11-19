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

#include <cstdio>
#include <cstring>
#include <string>

#include "grok.h"

namespace
{
const uint32_t WINDOW_SIZE = 1024;
// well above the ~600MB the 257 component case legitimately needs
const long DEFAULT_PEAK_RSS_LIMIT_MB = 1500;
// a corrupt stream only has to stay under the memory limit
const char* DECODE_MAY_FAIL_FLAG = "--decode-may-fail";
const char* PEAK_RSS_LIMIT_MB_FLAG = "--peak-rss-limit-mb=";

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
    fprintf(stderr, "usage: %s <codestream> [%s] [%s<N>]\n", argv[0], DECODE_MAY_FAIL_FLAG,
            PEAK_RSS_LIMIT_MB_FLAG);
    return 1;
  }
  bool decodeMayFail = false;
  long peakRssLimitKb = DEFAULT_PEAK_RSS_LIMIT_MB * 1024;
  for(int i = 2; i < argc; ++i)
  {
    size_t limitFlagLength = strlen(PEAK_RSS_LIMIT_MB_FLAG);
    if(strcmp(argv[i], DECODE_MAY_FAIL_FLAG) == 0)
      decodeMayFail = true;
    else if(strncmp(argv[i], PEAK_RSS_LIMIT_MB_FLAG, limitFlagLength) == 0)
      peakRssLimitKb = std::stol(argv[i] + limitFlagLength) * 1024;
    else
    {
      fprintf(stderr, "unknown argument %s\n", argv[i]);
      return 1;
    }
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
  if(decodeMayFail)
    rc = 0;

#ifdef __linux__
  long kb = peakRssKb();
  if(kb < 0)
    fprintf(stderr, "could not read VmHWM, skipping peak memory check\n");
  else if(kb > peakRssLimitKb)
  {
    fprintf(stderr, "peak rss %ld kB exceeds limit %ld kB\n", kb, peakRssLimitKb);
    rc = 1;
  }
  else
    printf("peak rss %ld kB\n", kb);
#endif

  return rc;
}
