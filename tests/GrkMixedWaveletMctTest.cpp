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
const uint32_t WIDTH = 32;
const uint32_t HEIGHT = 32;

grk_image* makeImage()
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
    for(uint32_t y = 0; y < comp.h; ++y)
    {
      for(uint32_t x = 0; x < comp.w; ++x)
      {
        size_t index = (size_t)y * comp.stride + x;
        int32_t value = (int32_t)((x * 7 + y * 3 + c * 50) & 0xFF);
        if(comp.data_type == GRK_INT_16)
          static_cast<int16_t*>(comp.data)[index] = (int16_t)value;
        else
          static_cast<int32_t*>(comp.data)[index] = value;
      }
    }
  }
  return image;
}

std::vector<uint8_t> compressLosslessWithMct()
{
  grk_image* image = makeImage();
  if(!image)
  {
    fprintf(stderr, "could not build the source image\n");
    return {};
  }
  grk_cparameters parameters = {};
  grk_compress_set_default_params(&parameters);
  parameters.cod_format = GRK_FMT_J2K;
  parameters.mct = 1;

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

size_t findMarker(const std::vector<uint8_t>& stream, uint8_t marker)
{
  for(size_t i = 0; i + 1 < stream.size(); ++i)
  {
    if(stream[i] == 0xFF && stream[i + 1] == marker)
      return i;
  }
  return stream.size();
}

// COC and QCC marker segments that switch component 1 to the 9/7 wavelet
std::vector<uint8_t> makeIrreversibleComponentOne(const std::vector<uint8_t>& stream)
{
  size_t cod = findMarker(stream, 0x52);
  // levels, code block width and height, code block style, wavelet
  const size_t SPCOD_OFFSET = 9;
  const size_t SPCOD_BYTES = 5;
  if(cod + SPCOD_OFFSET + SPCOD_BYTES > stream.size())
    return {};
  std::vector<uint8_t> segments = {0xFF, 0x53, 0x00, 0x09, 0x01, 0x00};
  segments.insert(segments.end(), stream.begin() + (long)(cod + SPCOD_OFFSET),
                  stream.begin() + (long)(cod + SPCOD_OFFSET + SPCOD_BYTES));
  segments.back() = 0;
  // scalar derived quantization with two guard bits and one step size
  const std::vector<uint8_t> qcc = {0xFF, 0x5D, 0x00, 0x06, 0x01, 0x41, 0x40, 0x00};
  segments.insert(segments.end(), qcc.begin(), qcc.end());
  return segments;
}

bool decodes(std::vector<uint8_t>& stream)
{
  grk_decompress_parameters params = {};
  grk_stream_params streamParams = {};
  streamParams.is_read_stream = true;
  streamParams.buf = stream.data();
  streamParams.buf_len = stream.size();

  grk_object* codec = grk_decompress_init(&streamParams, &params);
  if(!codec)
    return false;
  grk_header_info headerInfo = {};
  bool ok = grk_decompress_read_header(codec, &headerInfo) && grk_decompress(codec, nullptr);
  grk_object_unref(codec);
  return ok;
}
} // namespace

int main(void)
{
  grk_initialize(nullptr, 0, nullptr);

  auto stream = compressLosslessWithMct();
  auto segments = makeIrreversibleComponentOne(stream);
  size_t sot = findMarker(stream, 0x90);
  const size_t SOT_SEGMENT_BYTES = 12;
  if(segments.empty() || sot + SOT_SEGMENT_BYTES > stream.size())
  {
    fprintf(stderr, "could not find the COD and SOT markers\n");
    grk_deinitialize();
    return EXIT_FAILURE;
  }
  bool ok = true;
  if(!decodes(stream))
  {
    fprintf(stderr, "the unmodified stream did not decode\n");
    ok = false;
  }

  auto mainHeaderMixed = stream;
  mainHeaderMixed.insert(mainHeaderMixed.begin() + (long)sot, segments.begin(), segments.end());
  if(decodes(mainHeaderMixed))
  {
    fprintf(stderr, "mixed wavelets in the main header decoded\n");
    ok = false;
  }

  auto tileHeaderMixed = stream;
  size_t psotOffset = sot + 6;
  uint32_t psot = ((uint32_t)stream[psotOffset] << 24) | ((uint32_t)stream[psotOffset + 1] << 16) |
                  ((uint32_t)stream[psotOffset + 2] << 8) | stream[psotOffset + 3];
  psot += (uint32_t)segments.size();
  for(size_t i = 0; i < 4; ++i)
    tileHeaderMixed[psotOffset + i] = (uint8_t)(psot >> (24 - 8 * i));
  tileHeaderMixed.insert(tileHeaderMixed.begin() + (long)(sot + SOT_SEGMENT_BYTES),
                         segments.begin(), segments.end());
  if(decodes(tileHeaderMixed))
  {
    fprintf(stderr, "mixed wavelets in the tile-part header decoded\n");
    ok = false;
  }

  grk_deinitialize();
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
