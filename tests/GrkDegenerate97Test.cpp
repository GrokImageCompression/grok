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

// 9/7 round trip over images whose bottom tile row or right tile column is one
// to four samples tall or wide.  a tile that short has decomposition levels of a
// single row or column, which the forward transform skipped: it dropped the dc
// level shift, left the tile buffer holding the integers that the next pass
// reads as floats, and left a lone high-pass sample at half the magnitude the
// decompressor reconstructs from.

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
  const uint16_t NUM_COMPONENTS = 1;
  // floor for the per-precision bound, so that an 8 bit control error of a
  // couple of code values does not make the bound unreachably tight
  const int32_t MIN_ERROR_ALLOWED = 8;

  struct Geometry
  {
    uint32_t width;
    uint32_t height;
    uint32_t tile;
    uint8_t precision;
  };

  int32_t expectedSample(uint32_t x, uint32_t y, uint16_t c, uint8_t precision)
  {
    const uint32_t scale = 1U << (precision - 8);
    const uint32_t phase = (x * 3 + y * 5 + c * 29) & 511;
    const uint32_t triangle = phase < 256 ? phase : 511 - phase;
    return (int32_t)(triangle * scale);
  }

  grk_image* makeImage(const Geometry& geometry)
  {
    grk_image_comp params[NUM_COMPONENTS] = {};
    for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
    {
      params[c].dx = 1;
      params[c].dy = 1;
      params[c].w = geometry.width;
      params[c].h = geometry.height;
      params[c].prec = geometry.precision;
      params[c].sgnd = false;
    }
    grk_image* image = grk_image_new(NUM_COMPONENTS, params, GRK_CLRSPC_GRAY, true);
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
      for(uint32_t y = 0; y < geometry.height; ++y)
        for(uint32_t x = 0; x < geometry.width; ++x)
          data[(size_t)y * stride + x] = expectedSample(x, y, c, geometry.precision);
    }
    return image;
  }

  const char* describe(const Geometry& geometry, std::string& storage)
  {
    storage = std::to_string(geometry.width) + "x" + std::to_string(geometry.height) + " tile " +
              std::to_string(geometry.tile) + " prec " + std::to_string(geometry.precision);
    return storage.c_str();
  }

  bool compress(const Geometry& geometry, const std::string& path)
  {
    grk_image* image = makeImage(geometry);
    std::string name;
    if(!image)
    {
      fprintf(stderr, "%s: could not build the source image\n", describe(geometry, name));
      return false;
    }
    grk_cparameters parameters = {};
    grk_compress_set_default_params(&parameters);
    parameters.cod_format = GRK_FMT_J2K;
    parameters.irreversible = true;
    parameters.numlayers = 1;
    parameters.tile_size_on = true;
    parameters.t_width = geometry.tile;
    parameters.t_height = geometry.tile;

    grk_stream_params streamParams = {};
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

    grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
    bool ok = false;
    if(!codec)
      fprintf(stderr, "%s: grk_compress_init failed\n", describe(geometry, name));
    else
    {
      ok = grk_compress(codec, nullptr) != 0;
      if(!ok)
        fprintf(stderr, "%s: grk_compress failed\n", describe(geometry, name));
      grk_object_unref(codec);
    }
    grk_object_unref(&image->obj);
    return ok;
  }

  bool maxError(const Geometry& geometry, const std::string& path, int32_t& worst)
  {
    grk_decompress_parameters params = {};
    grk_stream_params streamParams = {};
    streamParams.is_read_stream = true;
    snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());
    std::string name;

    grk_object* codec = grk_decompress_init(&streamParams, &params);
    if(!codec)
    {
      fprintf(stderr, "%s: grk_decompress_init failed\n", describe(geometry, name));
      return false;
    }
    bool ok = false;
    grk_header_info headerInfo = {};
    if(!grk_decompress_read_header(codec, &headerInfo))
      fprintf(stderr, "%s: grk_decompress_read_header failed\n", describe(geometry, name));
    else if(!grk_decompress(codec, nullptr))
      fprintf(stderr, "%s: grk_decompress failed\n", describe(geometry, name));
    else
    {
      grk_image* image = grk_decompress_get_image(codec);
      if(!image)
        fprintf(stderr, "%s: grk_decompress_get_image returned null\n", describe(geometry, name));
      else
      {
        ok = true;
        worst = 0;
        for(uint16_t c = 0; c < image->numcomps && ok; ++c)
        {
          const auto& comp = image->comps[c];
          if(comp.w != geometry.width || comp.h != geometry.height || !comp.data)
          {
            fprintf(stderr, "%s: component %u is %ux%u\n", describe(geometry, name), c, comp.w,
                    comp.h);
            ok = false;
            break;
          }
          for(uint32_t y = 0; y < geometry.height; ++y)
          {
            for(uint32_t x = 0; x < geometry.width; ++x)
            {
              int32_t got = comp.data_type == GRK_INT_16
                                ? static_cast<int16_t*>(comp.data)[(size_t)y * comp.stride + x]
                                : static_cast<int32_t*>(comp.data)[(size_t)y * comp.stride + x];
              int32_t error = got - expectedSample(x, y, c, geometry.precision);
              if(error < 0)
                error = -error;
              if(error > worst)
                worst = error;
            }
          }
        }
      }
    }
    grk_object_unref(codec);
    return ok;
  }

  bool runGeometry(const Geometry& geometry, int32_t& worst)
  {
    std::string name;
    describe(geometry, name);
    std::string path = "degenerate_97_" + std::to_string(geometry.width) + "x" +
                       std::to_string(geometry.height) + "_" + std::to_string(geometry.tile) + "_" +
                       std::to_string(geometry.precision) + ".j2k";
    if(!compress(geometry, path))
      return false;
    bool ok = maxError(geometry, path, worst);
    remove(path.c_str());
    return ok;
  }

  // one tile size gives even tile origins, the other odd ones, and the parity of
  // the origin decides whether the short tile's lone row or column is a low-pass
  // or a high-pass coefficient
  bool runTileSize(uint32_t tile, uint8_t precision)
  {
    std::string name;
    const Geometry control = {4 * tile, 4 * tile, tile, precision};
    int32_t controlError = 0;
    if(!runGeometry(control, controlError))
      return false;
    printf("%s: max error %d (control)\n", describe(control, name), controlError);

    // a short tile has fewer coefficients than a full one, so it has no reason
    // to decode any worse
    const int32_t allowed = std::max(2 * controlError, MIN_ERROR_ALLOWED);

    std::vector<Geometry> geometries;
    for(uint32_t height = 1; height <= 4; ++height)
      geometries.push_back({4 * tile, tile + height, tile, precision});
    for(uint32_t width = 1; width <= 4; ++width)
      geometries.push_back({tile + width, 4 * tile, tile, precision});
    // the corner tile is a single sample in both directions
    geometries.push_back({tile + 1, tile + 1, tile, precision});

    bool ok = true;
    for(const auto& geometry : geometries)
    {
      int32_t worst = 0;
      if(!runGeometry(geometry, worst))
      {
        ok = false;
        continue;
      }
      printf("%s: max error %d\n", describe(geometry, name), worst);
      if(worst > allowed)
      {
        fprintf(stderr, "%s: max error %d exceeds %d\n", describe(geometry, name), worst, allowed);
        ok = false;
      }
    }
    return ok;
  }
} // namespace

int main(void)
{
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "");
#else
  unsetenv("GRK_MERCURY");
#endif

  grk_initialize(nullptr, 0, nullptr);

  int result = 0;
  // precision 12 and 16 take the float 9/7 transform, precision 8 the int16 one
  for(uint8_t precision : {(uint8_t)8, (uint8_t)12, (uint8_t)16})
  {
    for(uint32_t tile : {(uint32_t)15, (uint32_t)16})
    {
      if(!runTileSize(tile, precision))
        result = 1;
    }
  }

  grk_deinitialize();
  return result;
}
