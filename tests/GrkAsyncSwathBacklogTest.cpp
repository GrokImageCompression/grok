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

// an asynchronous decode stays a few tile rows ahead of the swath the caller waits on,
// a full wait after a swath wait decodes every tile, and a codec destroyed while its
// decode is held back still returns

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "grok.h"

namespace
{
const uint32_t IMAGE_SIZE = 512;
const uint32_t TILE_SIZE = 32;
const uint32_t TILE_COLS = IMAGE_SIZE / TILE_SIZE;
const uint8_t PRECISION = 8;
// two rows ahead of the waited row, plus slack
const uint32_t MAX_ROWS_DECODED_AHEAD = 4;

grk_image* makeImage(void)
{
  grk_image_comp params = {};
  params.dx = 1;
  params.dy = 1;
  params.w = IMAGE_SIZE;
  params.h = IMAGE_SIZE;
  params.prec = PRECISION;
  grk_image* image = grk_image_new(1, &params, GRK_CLRSPC_GRAY, true);
  if(!image)
    return nullptr;
  auto& comp = image->comps[0];
  auto data = static_cast<int32_t*>(comp.data);
  for(uint32_t y = 0; y < comp.h; ++y)
    for(uint32_t x = 0; x < comp.w; ++x)
      data[(size_t)y * comp.stride + x] = (int32_t)((x * 7 + y * 13) & 0xFF);
  return image;
}

bool compress(const std::string& path, bool writeTLM)
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
  parameters.tile_size_on = true;
  parameters.t_width = TILE_SIZE;
  parameters.t_height = TILE_SIZE;
  parameters.numlayers = 1;
  parameters.write_tlm = writeTLM;

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

void countTile(void*, uint16_t, grk_image*, uint8_t, void* decodedTiles)
{
  static_cast<std::atomic<uint32_t>*>(decodedTiles)->fetch_add(1);
}

bool waitRow(grk_object* codec, uint32_t row)
{
  grk_wait_swath swath = {};
  swath.x0 = 0;
  swath.y0 = row * TILE_SIZE;
  swath.x1 = IMAGE_SIZE;
  swath.y1 = std::min((row + 1) * TILE_SIZE, IMAGE_SIZE);
  grk_decompress_wait(codec, &swath);
  return swath.tile_y1 == row + 1;
}

grk_object* startDecode(const std::string& path, grk_decompress_parameters* params)
{
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  grk_object* codec = grk_decompress_init(&streamParams, params);
  if(!codec)
  {
    fprintf(stderr, "grk_decompress_init failed\n");
    return nullptr;
  }
  grk_header_info headerInfo = {};
  const char* failedStep = nullptr;
  if(!grk_decompress_read_header(codec, &headerInfo))
    failedStep = "grk_decompress_read_header";
  else if(!grk_decompress_update(params, codec))
    failedStep = "grk_decompress_update";
  else if(!grk_decompress(codec, nullptr))
    failedStep = "grk_decompress";
  else if(!waitRow(codec, 0))
    failedStep = "wait for tile row 0";
  if(!failedStep)
    return codec;
  fprintf(stderr, "%s failed\n", failedStep);
  grk_object_unref(codec);
  return nullptr;
}

grk_decompress_parameters asyncParams(void)
{
  grk_decompress_parameters params = {};
  params.asynchronous = true;
  params.simulate_synchronous = true;
  params.core.skip_allocate_composite = true;
  return params;
}

bool decodeStaysBehindSwath(const std::string& path)
{
  std::atomic<uint32_t> decodedTiles{0};
  grk_decompress_parameters params = asyncParams();
  params.decompress_callback = countTile;
  params.decompress_callback_user_data = &decodedTiles;
  grk_object* codec = startDecode(path, &params);
  if(!codec)
    return false;

  // gives an unthrottled decode time to run to the end of the image
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  uint32_t decodedWhileWaiting = decodedTiles.load();
  bool ok = decodedWhileWaiting <= MAX_ROWS_DECODED_AHEAD * TILE_COLS;
  if(!ok)
    fprintf(stderr, "%u tiles decoded while the caller held tile row 0, expected at most %u\n",
            decodedWhileWaiting, MAX_ROWS_DECODED_AHEAD * TILE_COLS);
  for(uint32_t row = 1; row < IMAGE_SIZE / TILE_SIZE; ++row)
  {
    if(!waitRow(codec, row))
    {
      fprintf(stderr, "wait for tile row %u failed\n", row);
      ok = false;
      break;
    }
  }
  grk_object_unref(codec);
  return ok;
}

bool fullWaitDecodesEveryTile(const std::string& path)
{
  std::atomic<uint32_t> decodedTiles{0};
  grk_decompress_parameters params = asyncParams();
  params.decompress_callback = countTile;
  params.decompress_callback_user_data = &decodedTiles;
  grk_object* codec = startDecode(path, &params);
  if(!codec)
    return false;

  grk_decompress_wait(codec, nullptr);
  uint32_t decoded = decodedTiles.load();
  bool ok = decoded == TILE_COLS * TILE_COLS;
  if(!ok)
    fprintf(stderr, "%u of %u tiles decoded after a full wait\n", decoded, TILE_COLS * TILE_COLS);
  grk_object_unref(codec);
  return ok;
}

// hangs if the held back parser is never released
bool destroyDuringDecode(const std::string& path)
{
  grk_decompress_parameters params = asyncParams();
  grk_object* codec = startDecode(path, &params);
  if(!codec)
    return false;
  grk_object_unref(codec);
  return true;
}
} // namespace

int main(void)
{
  // single threaded decodes do not throttle
  grk_initialize(nullptr, 4, nullptr);

  std::string path = "async_swath_backlog.j2k";
  bool ok = compress(path, false) && decodeStaysBehindSwath(path) &&
            fullWaitDecodesEveryTile(path) && destroyDuringDecode(path);
  std::string tlmPath = "async_swath_backlog_tlm.j2k";
  ok = ok && compress(tlmPath, true) && fullWaitDecodesEveryTile(tlmPath) &&
       destroyDuringDecode(tlmPath);
  remove(path.c_str());
  remove(tlmPath.c_str());

  grk_deinitialize();
  return ok ? 0 : 1;
}
