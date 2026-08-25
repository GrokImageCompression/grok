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

// the composite buffer's sample type has to be the one the tiles decode into. an 8 bit
// irreversible multi-tile stream is where the two used to be decided by separate rules that
// disagreed: the tiles took the int16 wavelet and the composite stayed int32.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
const uint32_t imageWidth = 97;
const uint32_t imageHeight = 83;
const uint32_t tileWidth = 32;
const uint32_t tileHeight = 32;
const uint16_t numComponents = 3;
const uint8_t precision = 8;

int32_t sourceSample(uint32_t x, uint32_t y, uint16_t component)
{
  return (int32_t)((x * 5 + y * 11 + component * 37 + ((x ^ y) & 15) * 9) & 0xFF);
}

int32_t readSample(const grk_image_comp& comp, uint32_t x, uint32_t y)
{
  size_t index = (size_t)y * comp.stride + x;
  if(comp.data_type == GRK_INT_16)
    return static_cast<int16_t*>(comp.data)[index];
  return static_cast<int32_t*>(comp.data)[index];
}

bool compressTiled(const std::string& path)
{
  grk_image_comp params[numComponents] = {};
  for(uint16_t c = 0; c < numComponents; ++c)
  {
    params[c].dx = 1;
    params[c].dy = 1;
    params[c].w = imageWidth;
    params[c].h = imageHeight;
    params[c].prec = precision;
    params[c].sgnd = false;
  }
  auto image = grk_image_new(numComponents, params, GRK_CLRSPC_SRGB, true);
  if(!image)
    return false;
  for(uint16_t c = 0; c < numComponents; ++c)
  {
    auto data = static_cast<int32_t*>(image->comps[c].data);
    uint32_t stride = image->comps[c].stride;
    for(uint32_t y = 0; y < imageHeight; ++y)
      for(uint32_t x = 0; x < imageWidth; ++x)
        data[(size_t)y * stride + x] = sourceSample(x, y, c);
  }

  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  // the 9/7 wavelet is what the two eligibility rules used to disagree on
  parameters.irreversible = true;
  parameters.tile_size_on = true;
  parameters.t_width = tileWidth;
  parameters.t_height = tileHeight;
  parameters.numlayers = 1;

  grk_stream_params streamParams = {};
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  auto codec = grk_compress_init(&streamParams, &parameters, image);
  bool ok = false;
  if(codec)
  {
    ok = grk_compress(codec, nullptr) != 0;
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);
  if(!ok)
    fprintf(stderr, "could not compress %s\n", path.c_str());
  return ok;
}

grk_object* openDecoder(const std::string& path)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  auto codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "grk_decompress_init failed for %s\n", path.c_str());
    return nullptr;
  }
  grk_header_info headerInfo = {};
  if(!grk_decompress_read_header(codec, &headerInfo))
  {
    fprintf(stderr, "grk_decompress_read_header failed for %s\n", path.c_str());
    grk_object_unref(codec);
    return nullptr;
  }
  return codec;
}

int runTest(const std::string& path)
{
  auto compositeCodec = openDecoder(path);
  if(!compositeCodec)
    return 1;
  if(!grk_decompress(compositeCodec, nullptr))
  {
    fprintf(stderr, "grk_decompress failed\n");
    grk_object_unref(compositeCodec);
    return 1;
  }
  auto composite = grk_decompress_get_image(compositeCodec);
  if(!composite)
  {
    fprintf(stderr, "grk_decompress_get_image returned null\n");
    grk_object_unref(compositeCodec);
    return 1;
  }

  auto tileCodec = openDecoder(path);
  if(!tileCodec)
  {
    grk_object_unref(compositeCodec);
    return 1;
  }

  int failures = 0;
  const uint16_t tileColumns = (uint16_t)((imageWidth + tileWidth - 1) / tileWidth);
  const uint16_t tileRows = (uint16_t)((imageHeight + tileHeight - 1) / tileHeight);
  for(uint16_t tileIndex = 0; tileIndex < (uint16_t)(tileColumns * tileRows); ++tileIndex)
  {
    if(!grk_decompress_tile(tileCodec, tileIndex))
    {
      fprintf(stderr, "grk_decompress_tile failed for tile %u\n", tileIndex);
      ++failures;
      break;
    }
    auto tileImage = grk_decompress_get_tile_image(tileCodec, tileIndex, false);
    if(!tileImage)
    {
      fprintf(stderr, "grk_decompress_get_tile_image returned null for tile %u\n", tileIndex);
      ++failures;
      break;
    }
    for(uint16_t c = 0; c < numComponents; ++c)
    {
      const auto& tileComp = tileImage->comps[c];
      const auto& compositeComp = composite->comps[c];
      if(tileComp.data_type != compositeComp.data_type)
      {
        fprintf(stderr,
                "tile %u component %u decodes into data type %d but the composite is %d\n",
                tileIndex, c, (int)tileComp.data_type, (int)compositeComp.data_type);
        ++failures;
        continue;
      }
      for(uint32_t y = 0; y < tileComp.h; ++y)
      {
        for(uint32_t x = 0; x < tileComp.w; ++x)
        {
          int32_t fromTile = readSample(tileComp, x, y);
          int32_t fromComposite =
              readSample(compositeComp, tileComp.x0 - compositeComp.x0 + x,
                         tileComp.y0 - compositeComp.y0 + y);
          if(fromTile != fromComposite)
          {
            fprintf(stderr, "tile %u component %u sample (%u,%u) is %d, composite has %d\n",
                    tileIndex, c, x, y, fromTile, fromComposite);
            ++failures;
            y = tileComp.h;
            break;
          }
        }
      }
    }
  }

  grk_object_unref(tileCodec);
  grk_object_unref(compositeCodec);
  return failures ? 1 : 0;
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

  const std::string path = "int16_eligibility.j2k";
  int result = 1;
  if(compressTiled(path))
    result = runTest(path);
  remove(path.c_str());

  grk_deinitialize();
  if(result)
    fprintf(stderr, "grk_int16_eligibility_test: FAILED\n");
  else
    printf("grk_int16_eligibility_test: passed\n");
  return result;
}
