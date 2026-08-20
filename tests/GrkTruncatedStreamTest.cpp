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

// a truncated multi-tile stream leaves tiles with unparsed tile parts, which
// the decoder finishes on a best-effort basis. completing such a tile without
// waiting for its tasks freed the tile underneath them, so this decodes the
// same truncated file repeatedly and expects every run to end cleanly.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "grok.h"

namespace
{
  const uint32_t REPEATS = 24;
  const uint32_t REDUCE = 3;

  void discardLog(const char*, void*) {}

  // decoding a truncated stream may fail; the point is that it must not
  // corrupt memory or crash while doing so
  bool decodeEndsCleanly(const std::string& path)
  {
    grk_decompress_parameters params = {};
    params.core.reduce = REDUCE;
    grk_stream_params streamParams = {};
    streamParams.is_read_stream = true;
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    grk_object* codec = grk_decompress_init(&streamParams, &params);
    if(!codec)
      return true;
    grk_header_info headerInfo = {};
    if(grk_decompress_read_header(codec, &headerInfo) && grk_decompress(codec, nullptr))
    {
      grk_image* image = grk_decompress_get_image(codec);
      // touch every sample so a freed tile buffer shows up under a sanitizer
      if(image)
      {
        for(uint16_t c = 0; c < image->numcomps; ++c)
        {
          const auto& comp = image->comps[c];
          if(!comp.data)
            continue;
          int64_t sum = 0;
          for(uint32_t y = 0; y < comp.h; ++y)
            for(uint32_t x = 0; x < comp.w; ++x)
              sum += comp.data_type == GRK_INT_16
                         ? static_cast<int16_t*>(comp.data)[(size_t)y * comp.stride + x]
                         : static_cast<int32_t*>(comp.data)[(size_t)y * comp.stride + x];
          if(sum == INT64_MIN)
            fprintf(stderr, "unreachable\n");
        }
      }
    }
    grk_object_unref(codec);
    return true;
  }
} // namespace

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    fprintf(stderr, "usage: %s <truncated multi-tile codestream>\n", argv[0]);
    return 1;
  }

#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "");
#else
  unsetenv("GRK_MERCURY");
#endif

  grk_msg_handlers handlers = {};
  handlers.info_callback = discardLog;
  handlers.warn_callback = discardLog;
  handlers.error_callback = discardLog;
  grk_set_msg_handlers(handlers);

  grk_initialize(nullptr, 0, nullptr);
  for(uint32_t i = 0; i < REPEATS; ++i)
  {
    if(!decodeEndsCleanly(argv[1]))
    {
      grk_deinitialize();
      return 1;
    }
  }
  grk_deinitialize();
  printf("%u truncated decodes at reduce %u ended cleanly\n", REPEATS, REDUCE);
  return 0;
}
