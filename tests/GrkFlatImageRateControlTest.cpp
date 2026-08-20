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

// rate allocation over an all-zero image: no coding pass has a rate, so the
// slope bounds start at DBL_MAX and the bisection midpoint overflowed to inf,
// after which the convergence test never fired and every layer ran the full
// 128 packet simulations. the 4x4 code blocks make each simulation expensive
// enough that the old behaviour took tens of seconds

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "grok.h"

namespace
{
  const uint16_t NUM_COMPONENTS = 10;
  const uint32_t WIDTH = 256;
  const uint32_t HEIGHT = 256;
  const uint8_t PRECISION = 1;
  const uint16_t NUM_LAYERS = 15;
  const uint32_t CODE_BLOCK_SIZE = 4;
  const uint8_t NUM_RESOLUTIONS = 2;
  const uint8_t CODE_BLOCK_STYLE = GRK_CBLKSTY_LAZY | GRK_CBLKSTY_VSC | GRK_CBLKSTY_PTERM |
                                   GRK_CBLKSTY_SEGSYM;
  const double FIRST_LAYER_RATIO = 80.0;
  const double LAYER_RATIO_STEP = 5.0;
  const size_t OUTPUT_BUFFER_BYTES = (size_t)WIDTH * HEIGHT * NUM_COMPONENTS * sizeof(int32_t);
  // the fixed code takes well under a second here, the broken one over thirty
  const double MAX_SECONDS = 10.0;

  grk_image* makeFlatImage()
  {
    grk_image_comp params[NUM_COMPONENTS] = {};
    for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
    {
      params[c].dx = 1;
      params[c].dy = 1;
      params[c].w = WIDTH;
      params[c].h = HEIGHT;
      params[c].prec = PRECISION;
      params[c].sgnd = true;
    }
    grk_image* image = grk_image_new(NUM_COMPONENTS, params, GRK_CLRSPC_SRGB, true);
    if(!image)
      return nullptr;
    for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
    {
      auto& comp = image->comps[c];
      if(!comp.data)
      {
        grk_object_unref(&image->obj);
        return nullptr;
      }
      // grk_image_new leaves the buffer uninitialized, so a flat image needs the
      // samples written: otherwise the pixels are whatever the allocator returned
      // and the image is not flat at all
      size_t count = (size_t)comp.stride * comp.h;
      if(comp.data_type == GRK_INT_16)
      {
        auto* data = static_cast<int16_t*>(comp.data);
        for(size_t s = 0; s < count; ++s)
          data[s] = 0;
      }
      else
      {
        auto* data = static_cast<int32_t*>(comp.data);
        for(size_t s = 0; s < count; ++s)
          data[s] = 0;
      }
    }
    return image;
  }

  bool compressFlatImage()
  {
    grk_image* image = makeFlatImage();
    if(!image)
    {
      fprintf(stderr, "could not build the source image\n");
      return false;
    }
    grk_cparameters parameters = {};
    grk_compress_set_default_params(&parameters);
    parameters.cod_format = GRK_FMT_J2K;
    parameters.irreversible = true;
    parameters.numresolution = NUM_RESOLUTIONS;
    parameters.cblockw_init = CODE_BLOCK_SIZE;
    parameters.cblockh_init = CODE_BLOCK_SIZE;
    parameters.cblk_sty = CODE_BLOCK_STYLE;
    parameters.numlayers = NUM_LAYERS;
    parameters.allocation_by_rate_distortion = true;
    for(uint16_t i = 0; i < NUM_LAYERS; ++i)
      parameters.layer_rate[i] = FIRST_LAYER_RATIO - LAYER_RATIO_STEP * i;

    std::vector<uint8_t> output(OUTPUT_BUFFER_BYTES);
    grk_stream_params streamParams = {};
    streamParams.buf = output.data();
    streamParams.buf_len = output.size();

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
} // namespace

int main(void)
{
  grk_initialize(nullptr, 0, nullptr);

  auto start = std::chrono::steady_clock::now();
  bool ok = compressFlatImage();
  double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  printf("flat image with %u rate layers compressed in %.2f s\n", NUM_LAYERS, seconds);
  if(ok && seconds > MAX_SECONDS)
  {
    fprintf(stderr, "compression took %.2f s, limit is %.2f s\n", seconds, MAX_SECONDS);
    ok = false;
  }

  grk_deinitialize();
  return ok ? 0 : 1;
}
