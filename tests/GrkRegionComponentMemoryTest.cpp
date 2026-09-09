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

// window decode used to keep every component's wavelet window until
// post_decompressT2T1. 32 components at 768x768 used to peak at ~96MB; freeing
// each window after its wavelet lands at ~57MB. the window clips the single
// tile so this hits the per-component free path, not whole-tile decode.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "GrkPeakResidentBytes.h"
#include "grok.h"

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
const uint32_t IMAGE_SIZE = 768;
const uint16_t NUM_COMPONENTS = 32;
const uint8_t PRECISION = 8;
// one row short of the tile, so wholeTileDecompress_ is false
const uint32_t WINDOW[4] = {0, 0, IMAGE_SIZE, IMAGE_SIZE - 1};
// dest is ~38MB of int16. unfixed peak was ~96MB (windows held until tile
// post), fixed peak ~57MB. cap sits between them.
const size_t PEAK_RESIDENT_BYTES_CAP = 80ULL * 1024ULL * 1024ULL;

void disableMercury(void)
{
#if defined(_WIN32)
  _putenv_s("GRK_MERCURY", "0");
#else
  setenv("GRK_MERCURY", "0", 1);
#endif
}

grk_image* makeImage(void)
{
  grk_image_comp params[NUM_COMPONENTS] = {};
  for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
  {
    params[c].dx = 1;
    params[c].dy = 1;
    params[c].w = IMAGE_SIZE;
    params[c].h = IMAGE_SIZE;
    params[c].prec = PRECISION;
    params[c].sgnd = false;
  }
  grk_image* image = grk_image_new(NUM_COMPONENTS, params, GRK_CLRSPC_UNKNOWN, true);
  if(!image)
    return nullptr;
  for(uint16_t componentNumber = 0; componentNumber < NUM_COMPONENTS; ++componentNumber)
  {
    auto* data = static_cast<int32_t*>(image->comps[componentNumber].data);
    if(!data)
    {
      grk_object_unref(&image->obj);
      return nullptr;
    }
    uint32_t stride = image->comps[componentNumber].stride;
    for(uint32_t y = 0; y < IMAGE_SIZE; ++y)
      for(uint32_t x = 0; x < IMAGE_SIZE; ++x)
        data[(size_t)y * stride + x] =
            (int32_t)((x * 7 + y * 13 + componentNumber * 17) & 0xFF);
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
  parameters.mct = 0;
  parameters.numresolution = 5;
  parameters.tile_size_on = true;
  parameters.t_width = IMAGE_SIZE;
  parameters.t_height = IMAGE_SIZE;

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

int decodeWindow(const std::string& path)
{
  disableMercury();
  grk_initialize(nullptr, 1, nullptr);

  grk_decompress_parameters params = {};
  params.dw_x0 = WINDOW[0];
  params.dw_y0 = WINDOW[1];
  params.dw_x1 = WINDOW[2];
  params.dw_y1 = WINDOW[3];

  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  snprintf(streamParams.file, sizeof(streamParams.file), "%s", path.c_str());

  int rc = 1;
  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
    fprintf(stderr, "grk_decompress_init failed\n");
  else
  {
    grk_header_info headerInfo = {};
    if(!grk_decompress_read_header(codec, &headerInfo))
      fprintf(stderr, "grk_decompress_read_header failed\n");
    else if(!grk_decompress(codec, nullptr))
      fprintf(stderr, "grk_decompress failed\n");
    else
    {
      grk_image* image = grk_decompress_get_image(codec);
      if(!image || image->numcomps != NUM_COMPONENTS)
        fprintf(stderr, "decoded image has %u components\n", image ? image->numcomps : 0);
      else if(!image->comps[0].data || image->comps[0].w != WINDOW[2] - WINDOW[0] ||
              image->comps[0].h != WINDOW[3] - WINDOW[1])
        fprintf(stderr, "decoded window is %ux%u\n", image->comps[0].w, image->comps[0].h);
      else
      {
        bool allHaveData = true;
        for(uint16_t componentNumber = 0; componentNumber < NUM_COMPONENTS; ++componentNumber)
        {
          if(!image->comps[componentNumber].data)
          {
            fprintf(stderr, "component %u has null data\n", componentNumber);
            allHaveData = false;
            break;
          }
        }
        if(allHaveData)
          rc = 0;
      }
    }
    grk_object_unref(codec);
  }
  grk_deinitialize();

  size_t peak = peakResidentBytes();
  printf("region component memory test peak rss %zu bytes\n", peak);
#ifdef __linux__
  if(peak > PEAK_RESIDENT_BYTES_CAP)
  {
    fprintf(stderr, "peak rss %zu bytes exceeds cap %zu bytes\n", peak, PEAK_RESIDENT_BYTES_CAP);
    return 1;
  }
#endif
  return rc;
}
} // namespace

int main(int argc, char** argv)
{
  disableMercury();

  if(argc >= 3 && strcmp(argv[1], "--decode") == 0)
    return decodeWindow(argv[2]);

  std::string path = "region_component_memory_test.j2k";
  grk_initialize(nullptr, 1, nullptr);
  bool compressed = compress(path);
  grk_deinitialize();
  if(!compressed)
    return 1;

#if defined(__linux__)
  pid_t pid = fork();
  if(pid < 0)
  {
    fprintf(stderr, "fork failed\n");
    remove(path.c_str());
    return 1;
  }
  if(pid == 0)
  {
    execl("/proc/self/exe", argv[0], "--decode", path.c_str(), (char*)nullptr);
    _exit(127);
  }
  int status = 0;
  if(waitpid(pid, &status, 0) < 0)
  {
    fprintf(stderr, "waitpid failed\n");
    remove(path.c_str());
    return 1;
  }
  remove(path.c_str());
  if(!WIFEXITED(status))
    return 1;
  return WEXITSTATUS(status);
#else
  int rc = decodeWindow(path);
  remove(path.c_str());
  return rc;
#endif
}
