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

// reversible 5/3 above 12 bits decodes through the int32 wavelet, which the
// int16 tests never reach. a reduced decode of a signed component overshoots
// the sample range next to an edge and has no dc shift, the case the SIMD
// vertical pass skipped the clamp on. one decode thread keeps every strip
// wider than a SIMD block, whatever the machine's lane count.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "grok.h"

namespace
{
  const uint8_t PRECISION = 13;
  const uint32_t WIDTH = 900;
  const uint32_t HEIGHT = 300;
  const uint32_t IMAGE_X0 = 3;
  const uint32_t IMAGE_Y0 = 5;
  const uint32_t TILE_X0 = 1;
  const uint32_t TILE_Y0 = 3;
  const uint32_t TILE_WIDTH = 320;
  const uint32_t TILE_HEIGHT = 96;
  const uint8_t NUM_RESOLUTIONS = 5;
  const uint32_t EDGE_BLOCK = 8;

  const int32_t SAMPLE_MIN = -(1 << (PRECISION - 1));
  const int32_t SAMPLE_MAX = (1 << (PRECISION - 1)) - 1;

  void discardLog(const char*, void*) {}

  // sharp extremes on the left so a reduced decode overshoots the range, varied
  // values on the right so the lossless round trip has something to reproduce
  int32_t sourceSample(uint32_t x, uint32_t y)
  {
    if(x < WIDTH / 2)
      return ((x / EDGE_BLOCK) + (y / EDGE_BLOCK)) & 1 ? SAMPLE_MAX : SAMPLE_MIN;
    uint32_t noise = (x * 2654435761u + y * 40503u) >> 7;
    return SAMPLE_MIN + (int32_t)(noise % (uint32_t)(SAMPLE_MAX - SAMPLE_MIN + 1));
  }

  bool compress(const std::string& path)
  {
    grk_image_comp params = {};
    params.dx = 1;
    params.dy = 1;
    params.x0 = IMAGE_X0;
    params.y0 = IMAGE_Y0;
    params.w = WIDTH;
    params.h = HEIGHT;
    params.prec = PRECISION;
    params.sgnd = true;
    grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
    if(!image || !image->comps[0].data)
    {
      fprintf(stderr, "could not build the source image\n");
      return false;
    }
    image->x0 = IMAGE_X0;
    image->y0 = IMAGE_Y0;
    image->x1 = IMAGE_X0 + WIDTH;
    image->y1 = IMAGE_Y0 + HEIGHT;

    auto* data = static_cast<int32_t*>(image->comps[0].data);
    uint32_t stride = image->comps[0].stride;
    for(uint32_t y = 0; y < HEIGHT; ++y)
      for(uint32_t x = 0; x < WIDTH; ++x)
        data[(size_t)y * stride + x] = sourceSample(x, y);

    grk_cparameters parameters = {};
    grk_compress_set_default_params(&parameters);
    parameters.cod_format = GRK_FMT_J2K;
    parameters.numresolution = NUM_RESOLUTIONS;
    parameters.tile_size_on = true;
    parameters.tx0 = TILE_X0;
    parameters.ty0 = TILE_Y0;
    parameters.t_width = TILE_WIDTH;
    parameters.t_height = TILE_HEIGHT;
    parameters.image_offset_x0 = IMAGE_X0;
    parameters.image_offset_y0 = IMAGE_Y0;

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

  // hands back the decoded component, or null on failure; caller keeps the codec
  // alive for as long as it reads the samples
  const grk_image_comp* decompress(const std::string& path, uint8_t reduce, grk_object*& codec)
  {
    grk_decompress_parameters params = {};
    params.core.reduce = reduce;
    grk_stream_params streamParams = {};
    streamParams.is_read_stream = true;
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    codec = grk_decompress_init(&streamParams, &params);
    if(!codec)
    {
      fprintf(stderr, "reduce %u: grk_decompress_init failed\n", reduce);
      return nullptr;
    }
    grk_header_info headerInfo = {};
    if(!grk_decompress_read_header(codec, &headerInfo))
    {
      fprintf(stderr, "reduce %u: grk_decompress_read_header failed\n", reduce);
      return nullptr;
    }
    if(!grk_decompress(codec, nullptr))
    {
      fprintf(stderr, "reduce %u: grk_decompress failed\n", reduce);
      return nullptr;
    }
    grk_image* image = grk_decompress_get_image(codec);
    if(!image || !image->comps[0].data)
    {
      fprintf(stderr, "reduce %u: no decoded image\n", reduce);
      return nullptr;
    }
    return image->comps;
  }

  int32_t sampleAt(const grk_image_comp* comp, uint32_t x, uint32_t y)
  {
    size_t index = (size_t)y * comp->stride + x;
    return comp->data_type == GRK_INT_16 ? static_cast<int16_t*>(comp->data)[index]
                                         : static_cast<int32_t*>(comp->data)[index];
  }

  bool roundTripIsExact(const std::string& path)
  {
    grk_object* codec = nullptr;
    auto* comp = decompress(path, 0, codec);
    bool ok = comp != nullptr;
    if(ok && (comp->w != WIDTH || comp->h != HEIGHT))
    {
      fprintf(stderr, "full decode is %ux%u, expected %ux%u\n", comp->w, comp->h, WIDTH, HEIGHT);
      ok = false;
    }
    // the whole point of this test is the int32 wavelet, so say so out loud if
    // the precision ever stops steering the decode onto it
    if(ok && comp->data_type != GRK_INT_32)
    {
      fprintf(stderr, "%u bit reversible decoded as data type %d, expected int32\n", PRECISION,
              (int)comp->data_type);
      ok = false;
    }
    for(uint32_t y = 0; ok && y < HEIGHT; ++y)
      for(uint32_t x = 0; x < WIDTH; ++x)
      {
        int32_t got = sampleAt(comp, x, y);
        int32_t want = sourceSample(x, y);
        if(got != want)
        {
          fprintf(stderr, "lossless round trip lost sample (%u,%u): got %d, expected %d\n", x, y,
                  got, want);
          ok = false;
          break;
        }
      }
    grk_object_unref(codec);
    return ok;
  }

  bool reducedDecodeIsInRange(const std::string& path, uint8_t reduce)
  {
    grk_object* codec = nullptr;
    auto* comp = decompress(path, reduce, codec);
    bool ok = comp != nullptr;
    for(uint32_t y = 0; ok && y < comp->h; ++y)
      for(uint32_t x = 0; x < comp->w; ++x)
      {
        int32_t got = sampleAt(comp, x, y);
        if(got < SAMPLE_MIN || got > SAMPLE_MAX)
        {
          fprintf(stderr, "reduce %u sample (%u,%u) = %d is outside [%d, %d]\n", reduce, x, y, got,
                  SAMPLE_MIN, SAMPLE_MAX);
          ok = false;
          break;
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
  grk_initialize(nullptr, 1, nullptr);

  const std::string path = "int32_reversible_53.j2k";
  int status = 0;
  if(!compress(path))
    status = 1;
  else
  {
    if(!roundTripIsExact(path))
      status = 1;
    for(uint8_t reduce = 1; reduce < NUM_RESOLUTIONS; ++reduce)
      if(!reducedDecodeIsInRange(path, reduce))
        status = 1;
  }
  remove(path.c_str());
  grk_deinitialize();
  return status;
}
