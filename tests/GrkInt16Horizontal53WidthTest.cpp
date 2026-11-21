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
#include <cstdlib>
#include <string>

#include "grok.h"

namespace
{
const uint32_t MAX_WIDTH = 300;
const uint32_t HEIGHT = 16;

void discardLog(const char*, void*) {}

int32_t sourceSample(uint32_t x, uint32_t y)
{
  return (int32_t)(((x * 2654435761u + y * 40503u) >> 7) & 0xFF);
}

bool compress(const std::string& path, uint32_t width)
{
  grk_image_comp params = {};
  params.dx = 1;
  params.dy = 1;
  params.w = width;
  params.h = HEIGHT;
  params.prec = 8;
  grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
  if(!image || !image->comps[0].data)
  {
    fprintf(stderr, "width %u: could not build the source image\n", width);
    return false;
  }
  image->x1 = width;
  image->y1 = HEIGHT;

  auto* data = static_cast<int32_t*>(image->comps[0].data);
  uint32_t stride = image->comps[0].stride;
  for(uint32_t y = 0; y < HEIGHT; ++y)
    for(uint32_t x = 0; x < width; ++x)
      data[(size_t)y * stride + x] = sourceSample(x, y);

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.numresolution = 2;

  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  bool ok = codec && grk_compress(codec, nullptr) != 0;
  if(!ok)
    fprintf(stderr, "width %u: compress failed\n", width);
  if(codec)
    grk_object_unref(codec);
  grk_object_unref(&image->obj);
  return ok;
}

bool roundTripIsExact(const std::string& path, uint32_t width)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  grk_header_info headerInfo = {};
  grk_image* image = nullptr;
  if(codec && grk_decompress_read_header(codec, &headerInfo) && grk_decompress(codec, nullptr))
    image = grk_decompress_get_image(codec);
  bool ok = image && image->comps[0].data;
  if(!ok)
    fprintf(stderr, "width %u: decompress failed\n", width);
  // the int32 wavelet takes over if the precision ever stops selecting the int16 one
  if(ok && image->comps[0].data_type != GRK_INT_16)
  {
    fprintf(stderr, "width %u: 8 bit reversible decoded as data type %d, expected int16\n", width,
            (int)image->comps[0].data_type);
    ok = false;
  }
  for(uint32_t y = 0; ok && y < HEIGHT; ++y)
    for(uint32_t x = 0; x < width; ++x)
    {
      const grk_image_comp* comp = image->comps;
      int32_t got = static_cast<int16_t*>(comp->data)[(size_t)y * comp->stride + x];
      if(got != sourceSample(x, y))
      {
        fprintf(stderr, "width %u: sample (%u,%u) is %d, expected %d\n", width, x, y, got,
                sourceSample(x, y));
        ok = false;
        break;
      }
    }
  if(codec)
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
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "0");
#else
  setenv("GRK_MERCURY", "0", 1);
#endif
  grk_initialize(nullptr, 1, nullptr);

  const std::string path = "int16_horizontal_53_width.j2k";
  int status = 0;
  for(uint32_t width = 2; width <= MAX_WIDTH; ++width)
  {
    if(!compress(path, width) || !roundTripIsExact(path, width))
      status = 1;
  }
  remove(path.c_str());
  grk_deinitialize();
  return status;
}
