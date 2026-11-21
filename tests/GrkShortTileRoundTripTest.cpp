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

// lossless round trip over image heights whose bottom tile row is short.
// the interesting heights are the ones where a resolution level of the
// bottom tile degenerates to one or two rows, which is where the vertical
// 5/3 lifting and the forward dc level shift have both been wrong.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
const uint32_t IMAGE_WIDTH = 61;
const uint32_t TILE_WIDTH = 14;
const uint32_t TILE_HEIGHT = 15;
const uint16_t NUM_COMPONENTS = 3;
const uint8_t PRECISION = 8;
// the decode is deterministic per stream, but the vertical wavelet tasks
// race across tile columns, so decode each stream more than once
const uint32_t DECODES_PER_HEIGHT = 3;

int32_t expectedSample(uint32_t x, uint32_t y, uint16_t c)
{
  return (int32_t)((x * 7 + y * 13 + c * 53 + ((x ^ y) & 31) * 3) & 0xFF);
}

grk_image* makeImage(uint32_t height)
{
  grk_image_comp params[NUM_COMPONENTS] = {};
  for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
  {
    params[c].dx = 1;
    params[c].dy = 1;
    params[c].w = IMAGE_WIDTH;
    params[c].h = height;
    params[c].prec = PRECISION;
    params[c].sgnd = false;
  }
  grk_image* image = grk_image_new(NUM_COMPONENTS, params, GRK_CLRSPC_SRGB, true);
  if(!image)
    return nullptr;
  for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
  {
    auto* data = static_cast<int32_t*>(image->comps[c].data);
    if(!data)
    {
      grk_object_unref(&image->obj);
      return nullptr;
    }
    uint32_t stride = image->comps[c].stride;
    for(uint32_t y = 0; y < height; ++y)
      for(uint32_t x = 0; x < IMAGE_WIDTH; ++x)
        data[(size_t)y * stride + x] = expectedSample(x, y, c);
  }
  return image;
}

bool compress(uint32_t height, const std::string& path)
{
  grk_image* image = makeImage(height);
  if(!image)
  {
    fprintf(stderr, "height %u: could not build the source image\n", height);
    return false;
  }
  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.irreversible = false;
  parameters.tile_size_on = true;
  parameters.t_width = TILE_WIDTH;
  parameters.t_height = TILE_HEIGHT;
  parameters.numlayers = 1;

  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  bool ok = false;
  if(!codec)
    fprintf(stderr, "height %u: grk_compress_init failed\n", height);
  else
  {
    ok = grk_compress(codec, nullptr) != 0;
    if(!ok)
      fprintf(stderr, "height %u: grk_compress failed\n", height);
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);
  return ok;
}

bool decodeMatchesSource(uint32_t height, const std::string& path)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "height %u: grk_decompress_init failed\n", height);
    return false;
  }
  bool ok = false;
  grk_header_info headerInfo = {};
  if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "height %u: grk_decompress_read_header failed\n", height);
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "height %u: grk_decompress failed\n", height);
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!image)
      fprintf(stderr, "height %u: grk_decompress_get_image returned null\n", height);
    else
    {
      ok = true;
      for(uint16_t c = 0; c < image->numcomps && ok; ++c)
      {
        const auto& comp = image->comps[c];
        if(comp.w != IMAGE_WIDTH || comp.h != height || !comp.data)
        {
          fprintf(stderr, "height %u: component %u is %ux%u, expected %ux%u\n", height, c, comp.w,
                  comp.h, IMAGE_WIDTH, height);
          ok = false;
          break;
        }
        for(uint32_t y = 0; y < height && ok; ++y)
        {
          for(uint32_t x = 0; x < IMAGE_WIDTH; ++x)
          {
            int32_t got = comp.data_type == GRK_INT_16
                              ? static_cast<int16_t*>(comp.data)[(size_t)y * comp.stride + x]
                              : static_cast<int32_t*>(comp.data)[(size_t)y * comp.stride + x];
            int32_t want = expectedSample(x, y, c);
            if(got != want)
            {
              fprintf(stderr,
                      "height %u: component %u sample (%u,%u) is %d, expected %d "
                      "(bottom tile row starts at %u)\n",
                      height, c, x, y, got, want, (height - 1) / TILE_HEIGHT * TILE_HEIGHT);
              ok = false;
              break;
            }
          }
        }
      }
    }
  }
  grk_object_unref(codec);
  return ok;
}

bool runHeight(uint32_t height)
{
  std::string path = "short_tile_round_trip_" + std::to_string(height) + ".j2k";
  if(!compress(height, path))
    return false;
  bool ok = true;
  for(uint32_t i = 0; i < DECODES_PER_HEIGHT && ok; ++i)
    ok = decodeMatchesSource(height, path);
  remove(path.c_str());
  return ok;
}
} // namespace

int main(void)
{
  // the fast path is a second decoder; this test is about the classic one
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "0");
#else
  setenv("GRK_MERCURY", "0", 1);
#endif

  grk_initialize(nullptr, 0, nullptr);

  // 31, 61, 91: bottom tile of one row, whose finest level is a single even
  // row, the geometry the forward dc level shift used to skip.
  // 65..68: bottom tile of 5..8 rows, whose level 3 is two rows starting on
  // an odd row, the geometry the inverse vertical lifting used to write
  // across columns instead of down rows.
  // 64, 69, 75: neighbouring heights that must stay exact as well.
  const uint32_t heights[] = {31, 61, 64, 65, 66, 67, 68, 69, 75, 91};

  int result = 0;
  for(uint32_t height : heights)
  {
    if(runHeight(height))
      printf("height %u passed\n", height);
    else
    {
      fprintf(stderr, "height %u FAILED\n", height);
      result = 1;
    }
  }

  grk_deinitialize();
  return result;
}
