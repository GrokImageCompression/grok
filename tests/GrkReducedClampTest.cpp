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

// a reduced resolution decode of a reversible image reconstructs from the low
// pass alone, which overshoots the sample range next to a sharp edge. the
// decoder has to clamp that, also for signed components whose dc shift is zero.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "grok.h"

namespace
{
  const uint32_t SIZE = 64;
  const uint32_t BLOCK = 8;
  const uint8_t PRECISION = 12;
  const uint8_t REDUCE = 1;

  void discardLog(const char*, void*) {}

  int32_t rangeMin(bool sgnd)
  {
    return sgnd ? -(1 << (PRECISION - 1)) : 0;
  }

  int32_t rangeMax(bool sgnd)
  {
    return sgnd ? (1 << (PRECISION - 1)) - 1 : (1 << PRECISION) - 1;
  }

  bool compress(bool sgnd, const std::string& path)
  {
    grk_image_comp params = {};
    params.dx = 1;
    params.dy = 1;
    params.w = SIZE;
    params.h = SIZE;
    params.prec = PRECISION;
    params.sgnd = sgnd;
    grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
    if(!image || !image->comps[0].data)
    {
      fprintf(stderr, "could not build the source image\n");
      return false;
    }
    auto* data = static_cast<int32_t*>(image->comps[0].data);
    uint32_t stride = image->comps[0].stride;
    for(uint32_t y = 0; y < SIZE; ++y)
      for(uint32_t x = 0; x < SIZE; ++x)
      {
        bool high = ((x / BLOCK) + (y / BLOCK)) & 1;
        data[(size_t)y * stride + x] = high ? rangeMax(sgnd) : rangeMin(sgnd);
      }

    grk_cparameters parameters = {};
    grk_compress_set_default_params(&parameters);
    parameters.cod_format = GRK_FMT_J2K;
    grk_stream_params streamParams = {};
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
    bool ok = codec && grk_compress(codec, nullptr) != 0;
    if(!ok)
      fprintf(stderr, "compress failed\n");
    if(codec)
      grk_object_unref(codec);
    grk_object_unref(&image->obj);
    return ok;
  }

  bool decodedWithinRange(bool sgnd, const std::string& path)
  {
    grk_decompress_parameters params = {};
    params.core.reduce = REDUCE;
    grk_stream_params streamParams = {};
    streamParams.is_read_stream = true;
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    grk_object* codec = grk_decompress_init(&streamParams, &params);
    if(!codec)
    {
      fprintf(stderr, "grk_decompress_init failed\n");
      return false;
    }
    bool ok = false;
    grk_header_info headerInfo = {};
    if(!grk_decompress_read_header(codec, &headerInfo))
      fprintf(stderr, "grk_decompress_read_header failed\n");
    else if(!grk_decompress(codec, nullptr))
      fprintf(stderr, "grk_decompress failed\n");
    else
    {
      grk_image* image = grk_decompress_get_image(codec);
      const uint32_t reduced = SIZE >> REDUCE;
      if(!image || image->comps[0].w != reduced || image->comps[0].h != reduced ||
         !image->comps[0].data)
        fprintf(stderr, "reduced image is not %ux%u\n", reduced, reduced);
      else
      {
        ok = true;
        const auto& comp = image->comps[0];
        for(uint32_t y = 0; y < reduced; ++y)
          for(uint32_t x = 0; x < reduced; ++x)
          {
            int32_t got = comp.data_type == GRK_INT_16
                              ? static_cast<int16_t*>(comp.data)[(size_t)y * comp.stride + x]
                              : static_cast<int32_t*>(comp.data)[(size_t)y * comp.stride + x];
            if(got < rangeMin(sgnd) || got > rangeMax(sgnd))
            {
              fprintf(stderr, "%s sample (%u,%u) = %d is outside [%d, %d]\n",
                      sgnd ? "signed" : "unsigned", x, y, got, rangeMin(sgnd), rangeMax(sgnd));
              ok = false;
              y = reduced;
              break;
            }
          }
      }
    }
    grk_object_unref(codec);
    return ok;
  }
} // namespace

int main(void)
{
  grk_msg_handlers handlers = {};
  handlers.info_callback = discardLog;
  handlers.debug_callback = discardLog;
  handlers.trace_callback = discardLog;
  handlers.warn_callback = discardLog;
  handlers.error_callback = discardLog;
  grk_set_msg_handlers(handlers);

  int status = 0;
  for(bool sgnd : {true, false})
  {
    std::string path = std::string("reduced_clamp_") + (sgnd ? "signed" : "unsigned") + ".j2k";
    if(!compress(sgnd, path) || !decodedWithinRange(sgnd, path))
      status = 1;
  }
  grk_deinitialize();
  return status;
}
