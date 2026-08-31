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

// reducing a prec <= 8 9/7 decode down to a single resolution skips the wavelet, so the
// standalone dc shift pass is what removes the Q-format upshift NarrowScaleFilter16
// applied. a constant image stays constant under any reduction, so every decoded sample
// has to land near the constant. whole image and region decode both take that route.
//
// mercury is turned off so the decode runs the classic pipeline.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "grok.h"

namespace
{
const uint32_t IMAGE_WIDTH = 197;
const uint32_t IMAGE_HEIGHT = 143;
const uint8_t PRECISION = 8;
const int32_t CONSTANT_VALUE = 200;
const int32_t TOLERANCE = 3;

bool compress(const std::string& path)
{
  grk_image_comp params = {};
  params.dx = 1;
  params.dy = 1;
  params.w = IMAGE_WIDTH;
  params.h = IMAGE_HEIGHT;
  params.prec = PRECISION;
  params.sgnd = false;
  grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
  if(!image || !image->comps[0].data)
  {
    if(image)
      grk_object_unref(&image->obj);
    fprintf(stderr, "could not build the source image\n");
    return false;
  }
  auto* data = static_cast<int32_t*>(image->comps[0].data);
  uint32_t stride = image->comps[0].stride;
  for(uint32_t y = 0; y < IMAGE_HEIGHT; ++y)
    for(uint32_t x = 0; x < IMAGE_WIDTH; ++x)
      data[(size_t)y * stride + x] = CONSTANT_VALUE;

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.irreversible = true;

  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  bool ok = false;
  if(!codec)
    fprintf(stderr, "grk_compress_init failed\n");
  else
  {
    ok = grk_compress(codec, nullptr) != 0;
    if(!ok)
      fprintf(stderr, "grk_compress failed\n");
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);
  return ok;
}

// window == nullptr decodes the whole image
bool decodeReduced(const std::string& path, uint8_t reduce, const uint32_t* window,
                   const char* label)
{
  grk_decompress_parameters params = {};
  params.core.reduce = reduce;
  if(window)
  {
    params.dw_x0 = window[0];
    params.dw_y0 = window[1];
    params.dw_x1 = window[2];
    params.dw_y1 = window[3];
  }
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "%s: grk_decompress_init failed\n", label);
    return false;
  }
  bool ok = false;
  grk_header_info headerInfo = {};
  if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "%s: grk_decompress_read_header failed\n", label);
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "%s: grk_decompress failed\n", label);
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!image || !image->comps[0].data || image->comps[0].w == 0 || image->comps[0].h == 0)
      fprintf(stderr, "%s: decoded image is empty\n", label);
    else
    {
      const auto& comp = image->comps[0];
      ok = true;
      for(uint32_t y = 0; ok && y < comp.h; ++y)
        for(uint32_t x = 0; ok && x < comp.w; ++x)
        {
          uint64_t index = (uint64_t)y * comp.stride + x;
          int32_t got = comp.data_type == GRK_INT_16 ? static_cast<int16_t*>(comp.data)[index]
                                                     : static_cast<int32_t*>(comp.data)[index];
          if(got < CONSTANT_VALUE - TOLERANCE || got > CONSTANT_VALUE + TOLERANCE)
          {
            fprintf(stderr, "%s: %ux%u sample (%u,%u) is %d, expected about %d\n", label, comp.w,
                    comp.h, x, y, got, CONSTANT_VALUE);
            ok = false;
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
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "0");
#else
  setenv("GRK_MERCURY", "0", 1);
#endif
  grk_initialize(nullptr, 0, nullptr);

  int status = 1;
  std::string path = "reduced_decompress_97_int16.j2k";
  if(compress(path))
  {
    // default codestream has 6 resolutions, reduce 5 leaves one. the window is in
    // full canvas coordinates and lands on a 4x3 region after reduction
    const uint32_t window[4] = {32, 32, 160, 128};
    bool ok =
        decodeReduced(path, 5, nullptr, "whole image") && decodeReduced(path, 5, window, "region");
    status = ok ? 0 : 1;
    remove(path.c_str());
  }

  grk_deinitialize();
  return status;
}
