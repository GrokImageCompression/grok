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

// mercury decodes a many-tile image one tile row at a time, and a lost wakeup
// in the weft scheduler used to let a whole tile row emit nothing, shifting
// every later row up by a tile height. many tile columns per row is what makes
// it likely, so the geometry here is wide rather than tall.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "grok.h"

namespace
{
const char* MERCURY_SUCCESS_MARKER = "mercury fast path: decoded";

const uint32_t IMAGE_WIDTH = 1280;
const uint32_t IMAGE_HEIGHT = 128;
const uint32_t TILE_WIDTH = 14;
const uint32_t TILE_HEIGHT = 15;
const uint16_t NUM_COMPONENTS = 3;
const uint8_t PRECISION = 8;
// the drop hit roughly a third of decodes of this geometry before the fix
const uint32_t MERCURY_DECODES = 16;

std::mutex logMutex;
std::string logText;

void appendLog(const char* msg, void*)
{
  std::lock_guard<std::mutex> lock(logMutex);
  logText += msg;
  logText += '\n';
}

void clearLog(void)
{
  std::lock_guard<std::mutex> lock(logMutex);
  logText.clear();
}

std::string takeLog(void)
{
  std::lock_guard<std::mutex> lock(logMutex);
  return logText;
}

void useMercury(bool on)
{
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", on ? "1" : "0");
#else
  setenv("GRK_MERCURY", on ? "1" : "0", 1);
#endif
}

int32_t expectedSample(uint32_t x, uint32_t y, uint16_t c)
{
  return (int32_t)((x * 7 + y * 13 + c * 53 + ((x ^ y) & 31) * 3) & 0xFF);
}

grk_image* makeImage(void)
{
  grk_image_comp params[NUM_COMPONENTS] = {};
  for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
  {
    params[c].dx = 1;
    params[c].dy = 1;
    params[c].w = IMAGE_WIDTH;
    params[c].h = IMAGE_HEIGHT;
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
    for(uint32_t y = 0; y < IMAGE_HEIGHT; ++y)
      for(uint32_t x = 0; x < IMAGE_WIDTH; ++x)
        data[(size_t)y * stride + x] = expectedSample(x, y, c);
  }
  return image;
}

bool compress(const std::string& path)
{
  grk_image* image = makeImage();
  if(!image)
  {
    fprintf(stderr, "could not build the source image\n");
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

// returns the first row that differs from the source, or -1 when exact
long decodeFirstBadRow(const std::string& path, bool mercury, bool& ranMercury)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  useMercury(mercury);
  clearLog();
  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "grk_decompress_init failed\n");
    return -2;
  }
  long firstBad = -1;
  grk_header_info headerInfo = {};
  if(!grk_decompress_read_header(codec, &headerInfo))
  {
    fprintf(stderr, "grk_decompress_read_header failed\n");
    firstBad = -2;
  }
  else if(!grk_decompress(codec, nullptr))
  {
    fprintf(stderr, "grk_decompress failed\n");
    firstBad = -2;
  }
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!image)
    {
      fprintf(stderr, "grk_decompress_get_image returned null\n");
      firstBad = -2;
    }
    else
    {
      for(uint16_t c = 0; c < image->numcomps && firstBad < 0; ++c)
      {
        const auto& comp = image->comps[c];
        if(comp.w != IMAGE_WIDTH || comp.h != IMAGE_HEIGHT || !comp.data)
        {
          fprintf(stderr, "component %u is %ux%u, expected %ux%u\n", c, comp.w, comp.h, IMAGE_WIDTH,
                  IMAGE_HEIGHT);
          firstBad = -2;
          break;
        }
        for(uint32_t y = 0; y < IMAGE_HEIGHT && firstBad < 0; ++y)
        {
          for(uint32_t x = 0; x < IMAGE_WIDTH; ++x)
          {
            int32_t got = comp.data_type == GRK_INT_16
                              ? static_cast<int16_t*>(comp.data)[(size_t)y * comp.stride + x]
                              : static_cast<int32_t*>(comp.data)[(size_t)y * comp.stride + x];
            if(got != expectedSample(x, y, c))
            {
              fprintf(stderr, "component %u sample (%u,%u) is %d, expected %d\n", c, x, y, got,
                      expectedSample(x, y, c));
              firstBad = (long)y;
              break;
            }
          }
        }
      }
    }
  }
  ranMercury = takeLog().find(MERCURY_SUCCESS_MARKER) != std::string::npos;
  grk_object_unref(codec);
  return firstBad;
}
} // namespace

int main(void)
{
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY_DEBUG", "1");
#else
  setenv("GRK_MERCURY_DEBUG", "1", 1);
#endif

  grk_msg_handlers handlers = {};
  handlers.info_callback = appendLog;
  handlers.warn_callback = appendLog;
  handlers.error_callback = appendLog;
  grk_set_msg_handlers(handlers);

  grk_initialize(nullptr, 0, nullptr);

  std::string path = "mercury_tile_row_race.j2k";
  if(!compress(path))
  {
    grk_deinitialize();
    return 1;
  }

  int result = 0;
  bool ranMercury = false;
  if(decodeFirstBadRow(path, false, ranMercury) != -1)
  {
    fprintf(stderr, "the classic decode does not match the source\n");
    result = 1;
  }

  for(uint32_t i = 0; i < MERCURY_DECODES && result == 0; ++i)
  {
    long bad = decodeFirstBadRow(path, true, ranMercury);
    if(!ranMercury)
    {
      fprintf(stderr, "decode %u fell back to the classic pipeline\n", i);
      result = 1;
    }
    else if(bad != -1)
    {
      fprintf(stderr, "mercury decode %u first differs at row %ld (tile height %u)\n", i, bad,
              TILE_HEIGHT);
      result = 1;
    }
  }

  remove(path.c_str());
  if(result == 0)
    printf("%u mercury decodes of %ux%u in %ux%u tiles all matched the source\n", MERCURY_DECODES,
           IMAGE_WIDTH, IMAGE_HEIGHT, TILE_WIDTH, TILE_HEIGHT);

  grk_deinitialize();
  return result;
}
