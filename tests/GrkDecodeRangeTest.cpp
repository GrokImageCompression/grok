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

// decoded samples have to land inside the declared range. a reduced resolution
// decode reconstructs from the low pass alone and overshoots next to a sharp
// edge, and the int16 9/7 path keeps fractional bits until its last level, so
// both need the final clamp, also on signed components whose dc shift is zero.
// the image is wide enough that every thread's strip fills whole SIMD blocks.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "grok.h"

namespace
{
  const uint32_t WIDTH = 2048;
  const uint32_t HEIGHT = 64;
  const uint32_t BLOCK = 8;

  struct Case
  {
    bool irreversible;
    uint8_t precision;
    bool sgnd;
    uint8_t reduce;
  };

  void discardLog(const char*, void*) {}

  const char* pipelineName = "mercury";

  void useMercury(bool on)
  {
#if defined(_WIN32)
    _putenv_s("GRK_MERCURY", on ? "1" : "0");
#else
    setenv("GRK_MERCURY", on ? "1" : "0", 1);
#endif
  }

  int32_t rangeMin(const Case& c)
  {
    return c.sgnd ? -(1 << (c.precision - 1)) : 0;
  }

  int32_t rangeMax(const Case& c)
  {
    return c.sgnd ? (1 << (c.precision - 1)) - 1 : (1 << c.precision) - 1;
  }

  const char* describe(const Case& c, std::string& storage)
  {
    storage = std::string(pipelineName) + " " + (c.irreversible ? "9/7" : "5/3") + " " +
              std::to_string(c.precision) + " bit " + (c.sgnd ? "signed" : "unsigned") +
              " reduce " + std::to_string(c.reduce);
    return storage.c_str();
  }

  bool compress(const Case& c, const std::string& path)
  {
    std::string name;
    grk_image_comp params = {};
    params.dx = 1;
    params.dy = 1;
    params.w = WIDTH;
    params.h = HEIGHT;
    params.prec = c.precision;
    params.sgnd = c.sgnd;
    grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
    if(!image || !image->comps[0].data)
    {
      fprintf(stderr, "%s: could not build the source image\n", describe(c, name));
      return false;
    }
    auto* data = static_cast<int32_t*>(image->comps[0].data);
    uint32_t stride = image->comps[0].stride;
    for(uint32_t y = 0; y < HEIGHT; ++y)
      for(uint32_t x = 0; x < WIDTH; ++x)
      {
        bool high = ((x / BLOCK) + (y / BLOCK)) & 1;
        data[(size_t)y * stride + x] = high ? rangeMax(c) : rangeMin(c);
      }

    grk_cparameters parameters = {};
    grk_compress_set_default_params(&parameters);
    parameters.cod_format = GRK_FMT_J2K;
    parameters.irreversible = c.irreversible;
    grk_stream_params streamParams = {};
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
    bool ok = codec && grk_compress(codec, nullptr) != 0;
    if(!ok)
      fprintf(stderr, "%s: compress failed\n", describe(c, name));
    if(codec)
      grk_object_unref(codec);
    grk_object_unref(&image->obj);
    return ok;
  }

  bool decodedWithinRange(const Case& c, const std::string& path)
  {
    std::string name;
    grk_decompress_parameters params = {};
    params.core.reduce = c.reduce;
    grk_stream_params streamParams = {};
    streamParams.is_read_stream = true;
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    grk_object* codec = grk_decompress_init(&streamParams, &params);
    if(!codec)
    {
      fprintf(stderr, "%s: grk_decompress_init failed\n", describe(c, name));
      return false;
    }
    bool ok = false;
    grk_header_info headerInfo = {};
    if(!grk_decompress_read_header(codec, &headerInfo))
      fprintf(stderr, "%s: grk_decompress_read_header failed\n", describe(c, name));
    else if(!grk_decompress(codec, nullptr))
      fprintf(stderr, "%s: grk_decompress failed\n", describe(c, name));
    else
    {
      grk_image* image = grk_decompress_get_image(codec);
      const uint32_t width = WIDTH >> c.reduce;
      const uint32_t height = HEIGHT >> c.reduce;
      if(!image || image->comps[0].w != width || image->comps[0].h != height ||
         !image->comps[0].data)
        fprintf(stderr, "%s: decoded image is not %ux%u\n", describe(c, name), width, height);
      else
      {
        ok = true;
        const auto& comp = image->comps[0];
        for(uint32_t y = 0; y < height && ok; ++y)
          for(uint32_t x = 0; x < width; ++x)
          {
            int32_t got = comp.data_type == GRK_INT_16
                              ? static_cast<int16_t*>(comp.data)[(size_t)y * comp.stride + x]
                              : static_cast<int32_t*>(comp.data)[(size_t)y * comp.stride + x];
            if(got < rangeMin(c) || got > rangeMax(c))
            {
              fprintf(stderr, "%s: sample (%u,%u) = %d is outside [%d, %d]\n", describe(c, name),
                      x, y, got, rangeMin(c), rangeMax(c));
              ok = false;
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
  for(bool mercury : {true, false})
  {
    useMercury(mercury);
    pipelineName = mercury ? "mercury" : "classic";
    // 12 bit 5/3 and 8 bit 9/7 both decode through the int16 paths
    for(bool irreversible : {false, true})
      for(bool sgnd : {true, false})
        for(uint8_t reduce : {(uint8_t)0, (uint8_t)1})
        {
          Case c = {irreversible, (uint8_t)(irreversible ? 8 : 12), sgnd, reduce};
          std::string path = std::string("decode_range_") + (irreversible ? "97_" : "53_") +
                             (sgnd ? "signed_" : "unsigned_") + std::to_string(reduce) + ".j2k";
          if(!compress(c, path) || !decodedWithinRange(c, path))
            status = 1;
        }
  }
  grk_deinitialize();
  return status;
}
