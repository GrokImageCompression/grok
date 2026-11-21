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
#include <vector>

#include "grok.h"

namespace
{
const uint16_t NUM_COMPONENTS = 3;
const uint32_t WIDTH = 64;
const uint32_t HEIGHT = 48;
const uint8_t NUM_RESOLUTIONS = 3;
const int32_t COMPONENT_VALUES[NUM_COMPONENTS] = {100, 60, 200};

struct Window
{
  uint32_t x0, y0, x1, y1;
};

// a flat image has all-zero high bands, so its higher resolution packets carry no data
grk_image* makeFlatImage()
{
  grk_image_comp params[NUM_COMPONENTS] = {};
  for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
  {
    params[c].dx = 1;
    params[c].dy = 1;
    params[c].w = WIDTH;
    params[c].h = HEIGHT;
    params[c].prec = 8;
    params[c].sgnd = false;
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
    size_t count = (size_t)comp.stride * comp.h;
    for(size_t s = 0; s < count; ++s)
    {
      if(comp.data_type == GRK_INT_16)
        static_cast<int16_t*>(comp.data)[s] = (int16_t)COMPONENT_VALUES[c];
      else
        static_cast<int32_t*>(comp.data)[s] = COMPONENT_VALUES[c];
    }
  }
  return image;
}

const uint8_t CSTY_SOP = 0x02;
std::vector<uint8_t> compressFlatImage()
{
  grk_image* image = makeFlatImage();
  if(!image)
  {
    fprintf(stderr, "could not build the source image\n");
    return {};
  }
  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.numresolution = NUM_RESOLUTIONS;
  parameters.prog_order = GRK_LRCP;
  parameters.mct = 1;
  parameters.csty |= CSTY_SOP;

  std::vector<uint8_t> output((size_t)WIDTH * HEIGHT * NUM_COMPONENTS * 4);
  grk_stream_params streamParams = {};
  streamParams.buf = output.data();
  streamParams.buf_len = output.size();

  grk_object* codec = grk_compress_init(&streamParams, &parameters, image);
  uint64_t length = 0;
  if(!codec)
    fprintf(stderr, "grk_compress_init failed\n");
  else
  {
    length = grk_compress(codec, nullptr);
    if(!length)
      fprintf(stderr, "grk_compress failed\n");
    grk_object_unref(codec);
  }
  grk_object_unref(&image->obj);
  output.resize(length);
  return output;
}

int32_t sampleAt(const grk_image_comp& comp, uint64_t index)
{
  if(comp.data_type == GRK_INT_16)
    return static_cast<int16_t*>(comp.data)[index];
  return static_cast<int32_t*>(comp.data)[index];
}

bool isFlat(const grk_image* image, const char* name)
{
  if(image->numcomps != NUM_COMPONENTS)
  {
    fprintf(stderr, "%s: %u components decoded\n", name, image->numcomps);
    return false;
  }
  for(uint16_t c = 0; c < NUM_COMPONENTS; ++c)
  {
    const auto& comp = image->comps[c];
    if(!comp.data)
    {
      fprintf(stderr, "%s: component %u has no data\n", name, c);
      return false;
    }
    for(uint32_t y = 0; y < comp.h; ++y)
    {
      for(uint32_t x = 0; x < comp.w; ++x)
      {
        int32_t sample = sampleAt(comp, (uint64_t)y * comp.stride + x);
        if(sample != COMPONENT_VALUES[c])
        {
          fprintf(stderr, "%s: component %u sample (%u,%u) is %d, expected %d\n", name, c, x, y,
                  sample, COMPONENT_VALUES[c]);
          return false;
        }
      }
    }
  }
  return true;
}

bool decodesFlat(std::vector<uint8_t>& stream, const char* name, const Window* window)
{
  grk_decompress_parameters params = {};
  if(window)
  {
    params.dw_x0 = window->x0;
    params.dw_y0 = window->y0;
    params.dw_x1 = window->x1;
    params.dw_y1 = window->y1;
  }
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  streamParams.buf = stream.data();
  streamParams.buf_len = stream.size();

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
  {
    fprintf(stderr, "%s: grk_decompress_init failed\n", name);
    return false;
  }
  bool ok = false;
  grk_header_info headerInfo = {};
  if(!grk_decompress_read_header(codec, &headerInfo))
    fprintf(stderr, "%s: grk_decompress_read_header failed\n", name);
  else if(!grk_decompress(codec, nullptr))
    fprintf(stderr, "%s: grk_decompress failed\n", name);
  else
  {
    grk_image* image = grk_decompress_get_image(codec);
    if(!image)
      fprintf(stderr, "%s: grk_decompress_get_image returned null\n", name);
    else
      ok = isFlat(image, name);
  }
  grk_object_unref(codec);
  return ok;
}
} // namespace

int main(void)
{
  grk_initialize(nullptr, 0, nullptr);

  auto stream = compressFlatImage();
  // a cut at an SOP marker never splits a packet
  std::vector<size_t> packetStarts;
  for(size_t i = 0; i + 1 < stream.size(); ++i)
  {
    if(stream[i] == 0xFF && stream[i + 1] == 0x91)
      packetStarts.push_back(i);
  }
  size_t expectedPackets = (size_t)NUM_RESOLUTIONS * NUM_COMPONENTS;
  if(packetStarts.size() != expectedPackets)
  {
    fprintf(stderr, "found %zu packets, expected %zu\n", packetStarts.size(), expectedPackets);
    grk_deinitialize();
    return EXIT_FAILURE;
  }

  struct Cut
  {
    const char* name;
    size_t packetsCut;
  };
  // LRCP with one layer ends with each component's top resolution packet
  const Cut cuts[] = {{"all packets", 0}, {"two packets cut", 2}, {"top resolution cut", 3}};
  const Window window = {10, 6, 50, 40};
  const size_t END_OF_CODESTREAM_BYTES = 2;
  bool ok = true;
  for(const auto& cut : cuts)
  {
    size_t end = cut.packetsCut ? packetStarts[packetStarts.size() - cut.packetsCut]
                                : stream.size() - END_OF_CODESTREAM_BYTES;
    std::vector<uint8_t> truncated(stream.begin(), stream.begin() + (long)end);
    ok = decodesFlat(truncated, cut.name, nullptr) && ok;
    ok = decodesFlat(truncated, cut.name, &window) && ok;
  }

  grk_deinitialize();
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
